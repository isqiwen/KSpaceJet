#include "kspacejet/recon/runtime/provider_node_instance.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_plan_storage.hpp"

#include "kspacejet/recon/type_registry.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

using ksj::base::Result;
using ksj::base::StatusCode;
using ksj::recon::ArtifactDigest;
using ksj::recon::ExecutionPlan;
using ksj::recon::ExecutionPlanSpec;
using ksj::recon::ExecutionProfile;
using ksj::recon::OperatorPlanBindingSpec;
using ksj::recon::Quantity;
using ksj::recon::SynchronousBufferPoolPlanSpec;
using ksj::recon::SynchronousDataEdgePlanSpec;
using ksj::recon::SynchronousDataEndpointKind;
using ksj::recon::SynchronousDynamicInputJoinPolicy;
using ksj::recon::SynchronousInputSourceKind;
using ksj::recon::SynchronousNodeInputBindingPlanSpec;
using ksj::recon::SynchronousNodeOutputBindingPlanSpec;
using ksj::recon::SynchronousNodePlanSpec;
using ksj::recon::SynchronousOutputDestinationKind;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::runtime::DataItemIdentity;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::ProviderNodeInstance;
using ksj::recon::runtime::ProviderNodeInstanceConfig;
using ksj::recon::runtime::ProviderNodeLifecycle;
using ksj::recon::runtime::ProviderNodeStartFacts;
using ksj::recon::runtime::SynchronousFiringOutcome;
using ksj::recon::runtime::SynchronousFiringResult;
using ksj::recon::runtime::SynchronousGraphPlanStorage;

constexpr std::string_view kProviderId = "org.kspacejet.tests.synchronous-firing-lease";
constexpr std::string_view kProviderBundleDigest =
  "sha256:808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
constexpr std::string_view kOperatorId = "synchronous_firing_lease_test_operator";
constexpr std::string_view kPlanDigest = "sha256:101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f";
constexpr std::string_view kResolvedPipelineDigest =
  "sha256:505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f";
constexpr std::string_view kScanFactsDigest = "sha256:707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f";
constexpr std::string_view kEffectivePipelineBindingDigest =
  "sha256:808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
constexpr std::string_view kEnvelopeDigest = "sha256:909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
constexpr std::string_view kMachinePolicyDigest =
  "sha256:b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf";
constexpr Quantity kPayloadCapacityBytes = 64U;
constexpr std::uint64_t kTerminalEpoch = 7U;

[[nodiscard]] Result<ArtifactDigest> parse_digest(const std::string_view value, const std::string_view field_name) {
  return ArtifactDigest::parse(value, field_name);
}

[[nodiscard]] Result<SynchronousBufferPoolPlanSpec>
make_pool(const std::string_view pool_id, const SynchronousDataEndpointKind owner_kind, const std::string_view owner_id,
          const std::string_view owner_port, const TypeDescriptor& type_descriptor) {
  auto metadata = ksj::recon::synchronous_buffer_pool_host_metadata_charged_bytes(1U, "ProviderNodeInstance test pool");
  if (!metadata.ok())
    return metadata.status();
  auto physical = ksj::recon::synchronous_buffer_pool_physical_charge_bytes(1U, kPayloadCapacityBytes, 0U,
                                                                            "ProviderNodeInstance test pool");
  if (!physical.ok())
    return physical.status();
  return SynchronousBufferPoolPlanSpec{.pool_id = std::string(pool_id),
                                       .owner_kind = owner_kind,
                                       .owner_id = std::string(owner_id),
                                       .owner_port_name = std::string(owner_port),
                                       .type_descriptor = type_descriptor,
                                       .memory_domain = TypeMemoryDomain::host_normal,
                                       .slot_count = 1U,
                                       .payload_capacity_bytes = kPayloadCapacityBytes,
                                       .metadata_capacity_bytes = 0U,
                                       .payload_alignment_bytes = type_descriptor.min_alignment_bytes(),
                                       .host_metadata_charged_bytes = metadata.value(),
                                       .descriptor_charged_count = 1U,
                                       .physical_charge_bytes = physical.value()};
}

[[nodiscard]] Result<SynchronousDataEdgePlanSpec>
make_edge(const std::string_view edge_id, const std::string_view source_pool_id,
          const SynchronousDataEndpointKind producer_kind, const std::string_view producer_id,
          const std::string_view producer_port, const Quantity producer_abi_port,
          const SynchronousDataEndpointKind consumer_kind, const std::string_view consumer_id,
          const std::string_view consumer_port, const Quantity consumer_abi_port,
          const TypeDescriptor& type_descriptor) {
  auto metadata = ksj::recon::synchronous_data_edge_host_metadata_charged_bytes(1U, "ProviderNodeInstance test edge");
  if (!metadata.ok())
    return metadata.status();
  return SynchronousDataEdgePlanSpec{.edge_id = std::string(edge_id),
                                     .source_pool_id = std::string(source_pool_id),
                                     .producer_kind = producer_kind,
                                     .producer_id = std::string(producer_id),
                                     .producer_port_name = std::string(producer_port),
                                     .producer_abi_port = producer_abi_port,
                                     .consumer_kind = consumer_kind,
                                     .consumer_id = std::string(consumer_id),
                                     .consumer_port_name = std::string(consumer_port),
                                     .consumer_abi_port = consumer_abi_port,
                                     .type_descriptor = type_descriptor,
                                     .max_items = 1U,
                                     .max_logical_bytes = kPayloadCapacityBytes,
                                     .host_metadata_charged_bytes = metadata.value(),
                                     .descriptor_charged_count = 1U};
}

[[nodiscard]] Result<ExecutionPlan> make_plan(const std::string_view canonical_config,
                                              const Quantity scratch_bytes = 0U) {
  auto image = ksj::recon::types::image_frame();
  if (!image.ok())
    return image.status();
  const auto image_type = std::move(image).value();
  auto input_pool = make_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type);
  if (!input_pool.ok())
    return input_pool.status();
  auto output_pool = make_pool("pool.output", SynchronousDataEndpointKind::node, "node", "output", image_type);
  if (!output_pool.ok())
    return output_pool.status();
  auto input_edge = make_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                              SynchronousDataEndpointKind::node, "node", "input", 0U, image_type);
  if (!input_edge.ok())
    return input_edge.status();
  auto output_edge = make_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "node", "output", 0U,
                               SynchronousDataEndpointKind::egress, "output", "", 0U, image_type);
  if (!output_edge.ok())
    return output_edge.status();
  auto config_digest = ksj::recon::derive_canonical_config_digest(canonical_config, "ProviderNodeInstance test config");
  if (!config_digest.ok())
    return config_digest.status();
  auto plan_digest = parse_digest(kPlanDigest, "ProviderNodeInstance test plan");
  if (!plan_digest.ok())
    return plan_digest.status();

  ExecutionPlanSpec specification;
  specification.inputs = {.resolved_pipeline = std::string(kResolvedPipelineDigest),
                          .scan_facts = std::string(kScanFactsDigest),
                          .effective_pipeline_binding = std::string(kEffectivePipelineBindingDigest),
                          .target_envelope = std::string(kEnvelopeDigest),
                          .machine_policy = std::string(kMachinePolicyDigest)};
  specification.execution_profile = ExecutionProfile::bounded_reconstruction_graph;
  specification.operator_plan_bindings = {
    OperatorPlanBindingSpec{.node_id = "node", .canonical_config_digest = config_digest.value().value()}};
  specification.synchronous_buffer_pool_plans = {std::move(input_pool).value(), std::move(output_pool).value()};
  specification.synchronous_data_edge_plans = {std::move(input_edge).value(), std::move(output_edge).value()};
  specification.synchronous_node_plans = {SynchronousNodePlanSpec{
    .node_id = "node",
    .provider_id = std::string(kProviderId),
    .provider_bundle_digest = std::string(kProviderBundleDigest),
    .operator_id = std::string(kOperatorId),
    .dynamic_input_join_policy = SynchronousDynamicInputJoinPolicy::exact_item_identity,
    .inputs = {SynchronousNodeInputBindingPlanSpec{.port_name = "input",
                                                   .abi_port = 0U,
                                                   .source_kind = SynchronousInputSourceKind::data_edge,
                                                   .source_id = "edge.input",
                                                   .type_descriptor = image_type,
                                                   .maximum_item_count = 1U}},
    .outputs = {SynchronousNodeOutputBindingPlanSpec{.port_name = "output",
                                                     .abi_port = 0U,
                                                     .destination_kind = SynchronousOutputDestinationKind::data_edge,
                                                     .destination_id = "edge.output",
                                                     .pool_id = "pool.output",
                                                     .type_descriptor = image_type,
                                                     .maximum_item_count = 1U}},
    .firing = {.maximum_input_batches = 1U,
               .maximum_input_items = 1U,
               .maximum_output_grants = 1U,
               .maximum_input_payload_bytes = kPayloadCapacityBytes,
               .maximum_scratch_bytes = scratch_bytes,
               .maximum_metadata_bytes = 64U,
               .staging_charged_bytes = 4096U,
               .staging_descriptor_count = 5U,
               .firing_reservation = {.cpu_leaf_permits = 1U}},
    .terminal = {.normal_max_output_items = 0U,
                 .normal_max_output_charged_bytes = 0U,
                 .normal_max_async_tokens = 0U,
                 .cancel_max_async_tokens = 0U}}};
  specification.resource_vector = {
    .host_normal_bytes = 1024U * 1024U, .descriptor_count = 4096U, .cpu_leaf_permits = 1U};
  specification.terminal_occurrences = 1U;
  specification.proof_obligations = {"test.provider-node-instance"};
  return ExecutionPlan::create(std::move(plan_digest).value(), specification);
}

[[nodiscard]] ProviderNodeInstanceConfig make_config(const ExecutionPlan& plan,
                                                     const std::string_view canonical_config) {
  return {
    .module_path = std::filesystem::path(KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE),
    .node_id = "node",
    .canonical_config = std::string(canonical_config),
    .start_facts = ProviderNodeStartFacts{.normalized_scan_facts_digest = plan.inputs().scan_facts(),
                                          .execution_plan_digest = plan.digest(),
                                          .run_id = "runtime-test",
                                          .scan_instance_id = "scan-instance",
                                          .terminal_epoch = kTerminalEpoch},
    .execution_context_id = 41U,
    .resource_domain_id = 43U,
    .max_backend_concurrency = 1U,
    .numa_node = 0U,
    .device_ordinal = 0U,
    .key_state = {
      .semantic_key = {std::byte{0x19}, std::byte{0x2A}}, .placement_key = 47U, .generation = 53U, .home_shard = 59U}};
}

TEST(ProviderNodeInstance, BindsFrozenProviderConfigAndNormalTerminalExactlyOnce) {
  constexpr std::string_view kConfig = R"({"mode":"done-output"})";
  auto plan = make_plan(kConfig);
  ASSERT_TRUE(plan.ok()) << plan.status();
  auto node = ProviderNodeInstance::create(plan.value(), make_config(plan.value(), kConfig));
  ASSERT_TRUE(node.ok()) << node.status();
  auto node_value = std::move(node).value();

  auto invocation = node_value->invocation();
  ASSERT_TRUE(invocation.ok()) << invocation.status();
  EXPECT_EQ("node", invocation.value().node_id);
  EXPECT_EQ(kOperatorId, invocation.value().operator_id);
  ASSERT_TRUE(invocation.value().canonical_config_digest.has_value());
  auto expected_digest = ksj::recon::derive_canonical_config_digest(kConfig, "ProviderNodeInstance expected digest");
  ASSERT_TRUE(expected_digest.ok()) << expected_digest.status();
  EXPECT_EQ(expected_digest.value(), *invocation.value().canonical_config_digest);
  EXPECT_NE(nullptr, invocation.value().operator_handle);
  EXPECT_NE(nullptr, invocation.value().execution_context);
  EXPECT_NE(nullptr, invocation.value().key_state);

  const SynchronousFiringResult terminal{
    .outcome = SynchronousFiringOutcome::done, .provider_status = KSJ_STATUS_OK, .terminal_epoch = kTerminalEpoch};
  EXPECT_TRUE(node_value->complete_normal_terminal(terminal).ok());
  EXPECT_EQ(ProviderNodeLifecycle::normal_terminal_completed, node_value->snapshot().lifecycle);
  EXPECT_FALSE(node_value->invocation().ok());
  EXPECT_EQ(StatusCode::state_error, node_value->complete_normal_terminal(terminal).code());
  EXPECT_EQ(StatusCode::state_error, node_value->cancel("too late").code());
}

TEST(ProviderNodeInstance, SendsAtMostOneAbnormalCancellation) {
  constexpr std::string_view kConfig = R"({"mode":"done-output"})";
  auto plan = make_plan(kConfig);
  ASSERT_TRUE(plan.ok()) << plan.status();
  auto node = ProviderNodeInstance::create(plan.value(), make_config(plan.value(), kConfig));
  ASSERT_TRUE(node.ok()) << node.status();
  auto node_value = std::move(node).value();

  EXPECT_TRUE(node_value->cancel("test failure", 3U).ok());
  const auto snapshot = node_value->snapshot();
  EXPECT_EQ(ProviderNodeLifecycle::cancelled, snapshot.lifecycle);
  EXPECT_TRUE(snapshot.cancellation_invoked);
  EXPECT_FALSE(node_value->invocation().ok());
  EXPECT_EQ(StatusCode::state_error, node_value->cancel("again", 4U).code());
  EXPECT_EQ(StatusCode::state_error, node_value
                                       ->complete_normal_terminal({.outcome = SynchronousFiringOutcome::done,
                                                                   .provider_status = KSJ_STATUS_OK,
                                                                   .terminal_epoch = kTerminalEpoch})
                                       .code());
}

TEST(ProviderNodeInstance, RejectsCanonicalConfigThatDoesNotMatchTheFrozenNodeBinding) {
  constexpr std::string_view kBoundConfig = R"({"mode":"done-output"})";
  constexpr std::string_view kDifferentConfig = R"({"mode":"done-zero-output"})";
  auto plan = make_plan(kBoundConfig);
  ASSERT_TRUE(plan.ok()) << plan.status();
  auto node = ProviderNodeInstance::create(plan.value(), make_config(plan.value(), kDifferentConfig));
  ASSERT_FALSE(node.ok());
  EXPECT_EQ(StatusCode::validation_error, node.status().code());
}

TEST(SynchronousGraphPlanStorage, OwnsExactFrozenSlabsWithAtLeast64ByteAlignment) {
  constexpr std::string_view kConfig = R"({"mode":"done-output"})";
  auto plan = make_plan(kConfig, 16U);
  ASSERT_TRUE(plan.ok()) << plan.status();
  auto storage = SynchronousGraphPlanStorage::create(plan.value());
  ASSERT_TRUE(storage.ok()) << storage.status();
  const auto& executor_storage = storage.value()->executor_storage();
  ASSERT_EQ(2U, executor_storage.buffer_pools.size());
  ASSERT_EQ(2U, executor_storage.data_edges.size());
  ASSERT_EQ(1U, executor_storage.node_scratch.size());
  for (const auto& pool : executor_storage.buffer_pools) {
    EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(pool.storage.payload.data()) % 64U);
    EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(pool.storage.control.data()) % 64U);
    EXPECT_EQ(kPayloadCapacityBytes, pool.storage.payload.size());
    EXPECT_TRUE(pool.storage.metadata.empty());
  }
  for (const auto& edge : executor_storage.data_edges) {
    EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(edge.storage.control.data()) % 64U);
    auto expected = ksj::recon::runtime::fixed_buffer_edge_required_control_storage_bytes(1U);
    ASSERT_TRUE(expected.ok()) << expected.status();
    EXPECT_EQ(expected.value(), edge.storage.control.size());
  }
  const auto& scratch = executor_storage.node_scratch.front();
  EXPECT_EQ("node", scratch.node_id);
  EXPECT_EQ(16U, scratch.storage.size());
  EXPECT_NE(nullptr, scratch.storage.data());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(scratch.storage.data()) % 64U);
}

TEST(SynchronousGraphPlanStorage, MakesStableFrameIdentityWithoutUsingPlacementAsDataIdentity) {
  FrameSlotContext context{};
  context.semantic_key = {
    .encoding_space = 1U,
    .slice = 2U,
    .contrast = 3U,
    .repetition = 4U,
    .set = 5U,
    .phase = 6U,
    .average = 7U,
    .segment = 8U,
  };
  context.order_key = 101U;
  context.placement_key = 103U;
  const auto identity = ksj::recon::runtime::make_data_item_identity(context, 107U);
  EXPECT_EQ(1282725276430990459ULL, identity.semantic_key_hash);
  EXPECT_EQ(101U, identity.order_key);
  EXPECT_EQ(107U, identity.item_ordinal);

  context.placement_key = 109U;
  const auto changed_placement = ksj::recon::runtime::make_data_item_identity(context, 107U);
  EXPECT_EQ(identity.semantic_key_hash, changed_placement.semantic_key_hash);
  EXPECT_EQ(identity.order_key, changed_placement.order_key);
  EXPECT_EQ(identity.item_ordinal, changed_placement.item_ordinal);
  ++context.semantic_key.slice;
  EXPECT_NE(identity.semantic_key_hash, ksj::recon::runtime::make_data_item_identity(context, 107U).semantic_key_hash);
  --context.semantic_key.slice;
  ++context.semantic_key.segment;
  EXPECT_NE(identity.semantic_key_hash, ksj::recon::runtime::make_data_item_identity(context, 107U).semantic_key_hash);
}

} // namespace
