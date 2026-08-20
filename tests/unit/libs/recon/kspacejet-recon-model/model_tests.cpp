#include "kspacejet/recon/model.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ksj::recon;

constexpr std::string_view kDigest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

[[nodiscard]] ArtifactDigest artifact_digest(const std::string_view field_name = "test digest") {
  auto parsed = ArtifactDigest::parse(kDigest, field_name);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] TypeDescriptor image_type() {
  auto descriptor = types::image_frame();
  EXPECT_TRUE(descriptor.ok()) << descriptor.status();
  return std::move(descriptor).value();
}

[[nodiscard]] SynchronousBufferPoolPlanSpec
pool_spec(const std::string_view id, const SynchronousDataEndpointKind owner_kind, const std::string_view owner_id,
          const std::string_view owner_port, const TypeDescriptor& descriptor) {
  auto metadata = synchronous_buffer_pool_host_metadata_charged_bytes(1U, "test pool metadata");
  auto physical = synchronous_buffer_pool_physical_charge_bytes(1U, 64U, 0U, "test pool physical");
  EXPECT_TRUE(metadata.ok()) << metadata.status();
  EXPECT_TRUE(physical.ok()) << physical.status();
  return {
    .pool_id = std::string(id),
    .owner_kind = owner_kind,
    .owner_id = std::string(owner_id),
    .owner_port_name = std::string(owner_port),
    .type_descriptor = descriptor,
    .memory_domain = TypeMemoryDomain::host_normal,
    .slot_count = 1U,
    .payload_capacity_bytes = 64U,
    .metadata_capacity_bytes = 0U,
    .payload_alignment_bytes = descriptor.min_alignment_bytes(),
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = 1U,
    .physical_charge_bytes = physical.value(),
  };
}

[[nodiscard]] SynchronousDataEdgePlanSpec
edge_spec(const std::string_view id, const std::string_view pool_id, const SynchronousDataEndpointKind producer_kind,
          const std::string_view producer_id, const std::string_view producer_port, const Quantity producer_abi_port,
          const SynchronousDataEndpointKind consumer_kind, const std::string_view consumer_id,
          const std::string_view consumer_port, const Quantity consumer_abi_port, const TypeDescriptor& descriptor) {
  auto metadata = synchronous_data_edge_host_metadata_charged_bytes(1U, "test edge metadata");
  EXPECT_TRUE(metadata.ok()) << metadata.status();
  return {
    .edge_id = std::string(id),
    .source_pool_id = std::string(pool_id),
    .producer_kind = producer_kind,
    .producer_id = std::string(producer_id),
    .producer_port_name = std::string(producer_port),
    .producer_abi_port = producer_abi_port,
    .consumer_kind = consumer_kind,
    .consumer_id = std::string(consumer_id),
    .consumer_port_name = std::string(consumer_port),
    .consumer_abi_port = consumer_abi_port,
    .type_descriptor = descriptor,
    .max_items = 1U,
    .max_logical_bytes = 64U,
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = 1U,
  };
}

[[nodiscard]] ExecutionPlanSpec valid_execution_plan_spec() {
  const auto descriptor = image_type();
  ExecutionPlanSpec result;
  result.inputs = {
    .resolved_pipeline = std::string(kDigest),
    .scan_descriptor = std::string(kDigest),
    .target_envelope = std::string(kDigest),
    .machine_policy = std::string(kDigest),
  };
  result.operator_plan_bindings = {{.node_id = "node", .canonical_config_digest = std::string(kDigest)}};
  result.execution_profile = ExecutionProfile::offline_reference;
  result.synchronous_buffer_pool_plans = {
    pool_spec("pool.ingress", SynchronousDataEndpointKind::ingress, "ingress", "", descriptor),
    pool_spec("pool.output", SynchronousDataEndpointKind::node, "node", "output", descriptor),
  };
  result.synchronous_data_edge_plans = {
    edge_spec("edge.input", "pool.ingress", SynchronousDataEndpointKind::ingress, "ingress", "", 0U,
              SynchronousDataEndpointKind::node, "node", "input", 0U, descriptor),
    edge_spec("edge.output", "pool.output", SynchronousDataEndpointKind::node, "node", "output", 0U,
              SynchronousDataEndpointKind::egress, "egress", "", 0U, descriptor),
  };
  result.synchronous_node_plans = {{
    .node_id = "node",
    .provider_id = "org.kspacejet.tests.generic",
    .provider_bundle_digest = std::string(kDigest),
    .operator_id = "copy",
    .dynamic_input_join_policy = SynchronousDynamicInputJoinPolicy::exact_item_identity,
    .inputs = {{.port_name = "input",
                .abi_port = 0U,
                .source_kind = SynchronousInputSourceKind::data_edge,
                .source_id = "edge.input",
                .type_descriptor = descriptor,
                .maximum_item_count = 1U}},
    .outputs = {{.port_name = "output",
                 .abi_port = 0U,
                 .destination_kind = SynchronousOutputDestinationKind::data_edge,
                 .destination_id = "edge.output",
                 .pool_id = "pool.output",
                 .type_descriptor = descriptor,
                 .maximum_item_count = 1U}},
    .firing = {.maximum_input_batches = 1U,
               .maximum_input_items = 1U,
               .maximum_output_grants = 1U,
               .maximum_input_payload_bytes = 64U,
               .maximum_scratch_bytes = 0U,
               .maximum_metadata_bytes = 1U,
               .staging_charged_bytes = 64U,
               .staging_descriptor_count = 1U,
               .firing_reservation = {.cpu_leaf_permits = 1U}},
    .terminal = {},
  }};
  result.resource_vector = {.host_normal_bytes = 4096U, .descriptor_count = 64U, .cpu_leaf_permits = 1U};
  result.terminal_occurrences = 2U;
  result.proof_obligations = {"test.generic-synchronous-plan"};
  return result;
}

[[nodiscard]] OperatorContractSpec basic_contract_spec() {
  return {
    .operator_id = "copy",
    .ports =
      {
        {.name = "input", .type_ref = "ksj.kspace-frame", .direction = PortDirection::input},
        {.name = "output", .type_ref = "ksj.image-frame", .direction = PortDirection::output},
      },
  };
}

[[nodiscard]] NodePlanningRequirementsSpec basic_requirements() {
  return {
    .execution = {.input_granularity = InputGranularity::acquisition,
                  .max_active_keys = 1U,
                  .max_in_flight = 1U,
                  .max_items_per_activation = 1U},
    .batch = {.min_items = 1U, .preferred_items = 1U, .max_items = 1U, .max_charged_bytes = 64U},
    .rates = {.kind = RateKind::sdf,
              .static_phases = {{.inputs = {{.port_name = "input", .items = 1U, .charged_bytes = 64U}},
                                 .outputs = {{.port_name = "output", .items = 1U, .charged_bytes = 64U}}}}},
    .resources = {.output_items = 1U, .output_charged_bytes = 64U, .cpu_permits = 1U},
  };
}

TEST(KSpaceJetReconModelArtifactDigest, AcceptsCanonicalLowerCaseSha256) {
  const auto parsed = ArtifactDigest::parse(kDigest, "test digest");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(kDigest, parsed.value().value());
  EXPECT_FALSE(ArtifactDigest::parse("sha256:1234", "test digest").ok());
  EXPECT_FALSE(
    ArtifactDigest::parse("sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", "test digest")
      .ok());
}

TEST(KSpaceJetReconModelArtifactDigest, DerivesStableCanonicalConfigIdentity) {
  const auto empty = derive_canonical_config_digest("{}");
  ASSERT_TRUE(empty.ok()) << empty.status();
  EXPECT_EQ("sha256:113d6dfdc042c5b439b9ce57594a079a1f364755e26e126cd9db7bc11125c6bc", empty.value().value());
  EXPECT_FALSE(derive_canonical_config_digest("").ok());
}

TEST(KSpaceJetReconModelExecutionProfile, AcceptsCanonicalNamesAndRejectsLegacyNames) {
  const auto offline_reference = parse_execution_profile("offline-reference");
  const auto bounded_graph = parse_execution_profile("bounded-reconstruction-graph");
  const auto provider_development = parse_execution_profile("provider-development");
  const auto embedded_incremental = parse_execution_profile("embedded-incremental");
  const auto isolated_runtime = parse_execution_profile("isolated-provider-runtime");

  ASSERT_TRUE(offline_reference.ok()) << offline_reference.status();
  ASSERT_TRUE(bounded_graph.ok()) << bounded_graph.status();
  ASSERT_TRUE(provider_development.ok()) << provider_development.status();
  ASSERT_TRUE(embedded_incremental.ok()) << embedded_incremental.status();
  ASSERT_TRUE(isolated_runtime.ok()) << isolated_runtime.status();
  EXPECT_EQ(ExecutionProfile::offline_reference, offline_reference.value());
  EXPECT_EQ(ExecutionProfile::bounded_reconstruction_graph, bounded_graph.value());
  EXPECT_EQ(ExecutionProfile::provider_development, provider_development.value());
  EXPECT_EQ(ExecutionProfile::embedded_incremental, embedded_incremental.value());
  EXPECT_EQ(ExecutionProfile::isolated_provider_runtime, isolated_runtime.value());
  EXPECT_EQ("offline-reference", to_string(offline_reference.value()));
  EXPECT_EQ("bounded-reconstruction-graph", to_string(bounded_graph.value()));
  EXPECT_EQ("provider-development", to_string(provider_development.value()));
  EXPECT_EQ("embedded-incremental", to_string(embedded_incremental.value()));
  EXPECT_EQ("isolated-provider-runtime", to_string(isolated_runtime.value()));
  EXPECT_TRUE(is_currently_supported_in_process(offline_reference.value()));
  EXPECT_TRUE(is_currently_supported_in_process(bounded_graph.value()));
  EXPECT_FALSE(is_currently_supported_in_process(provider_development.value()));
  EXPECT_FALSE(is_currently_supported_in_process(embedded_incremental.value()));
  EXPECT_FALSE(is_currently_supported_in_process(isolated_runtime.value()));
  EXPECT_TRUE(requires_provider_isolation(isolated_runtime.value()));
  EXPECT_FALSE(requires_provider_isolation(provider_development.value()));

  for (const std::string_view legacy :
       {"offline", "bounded-online", "isolated-strict-online", "deadline-qualified-online", "research-unbounded"}) {
    EXPECT_FALSE(parse_execution_profile(legacy).ok()) << legacy;
  }
}

TEST(KSpaceJetReconModelOperatorContract, SupportsExplicitMultiInputPorts) {
  auto specification = basic_contract_spec();
  specification.ports.insert(
    specification.ports.begin() + 1,
    {.name = "trajectory", .type_ref = "ksj.trajectory-frame", .direction = PortDirection::input});
  const auto contract = OperatorContract::create(specification);
  ASSERT_TRUE(contract.ok()) << contract.status();
  ASSERT_EQ(3U, contract.value().ports().size());
  EXPECT_EQ("trajectory", contract.value().ports()[1].name);
}

TEST(KSpaceJetReconModelNodePlanningRequirements, ValidatesOutputReservationsAndMultiInputRates) {
  auto specification = basic_contract_spec();
  specification.ports.insert(
    specification.ports.begin() + 1,
    {.name = "trajectory", .type_ref = "ksj.trajectory-frame", .direction = PortDirection::input});
  const auto contract = OperatorContract::create(specification);
  ASSERT_TRUE(contract.ok()) << contract.status();

  auto requirements = basic_requirements();
  requirements.execution.max_items_per_activation = 2U;
  requirements.batch = {.min_items = 2U, .preferred_items = 2U, .max_items = 2U, .max_charged_bytes = 128U};
  requirements.rates.static_phases.front().inputs.push_back(
    {.port_name = "trajectory", .items = 1U, .charged_bytes = 64U});
  EXPECT_TRUE(NodePlanningRequirements::create(requirements, contract.value()).ok());

  requirements.resources.output_items = 0U;
  EXPECT_FALSE(NodePlanningRequirements::create(requirements, contract.value()).ok());
}

TEST(KSpaceJetReconModelExecutionPlan, CreatesOnlyGenericSynchronousTopology) {
  auto plan = ExecutionPlan::create(artifact_digest(), valid_execution_plan_spec());
  ASSERT_TRUE(plan.ok()) << plan.status();
  EXPECT_EQ(1U, plan.value().synchronous_node_plans().size());
  EXPECT_EQ(2U, plan.value().synchronous_buffer_pool_plans().size());
  EXPECT_EQ(2U, plan.value().synchronous_data_edge_plans().size());

  auto missing_edge = valid_execution_plan_spec();
  missing_edge.synchronous_data_edge_plans.pop_back();
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), missing_edge).ok());

  auto wrong_accounting = valid_execution_plan_spec();
  --wrong_accounting.synchronous_buffer_pool_plans.front().host_metadata_charged_bytes;
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), wrong_accounting).ok());

  auto duplicate_binding = valid_execution_plan_spec();
  duplicate_binding.operator_plan_bindings.push_back(duplicate_binding.operator_plan_bindings.front());
  EXPECT_FALSE(ExecutionPlan::create(artifact_digest(), duplicate_binding).ok());
}

TEST(KSpaceJetReconModelRecords, ValidateFinitePlanVerificationAndAdmissionFields) {
  const auto plan = ExecutionPlan::create(artifact_digest(), valid_execution_plan_spec());
  ASSERT_TRUE(plan.ok()) << plan.status();

  VerificationRecordSpec verification{
    .execution_plan_digest = plan.value().digest().value(),
    .execution_profile = plan.value().execution_profile(),
    .verified_resource_vector = {.host_normal_bytes = plan.value().resources().host_normal_bytes(),
                                 .descriptor_count = plan.value().resources().descriptor_count(),
                                 .cpu_leaf_permits = plan.value().resources().cpu_leaf_permits()},
    .verified_terminal_occurrences = plan.value().terminal_occurrences(),
    .verified_obligations = {"test.generic-synchronous-plan"},
  };
  EXPECT_TRUE(VerificationRecord::create(artifact_digest(), verification).ok());
  verification.verified_terminal_occurrences = 0U;
  EXPECT_FALSE(VerificationRecord::create(artifact_digest(), verification).ok());

  AdmissionRecordSpec admission{
    .execution_plan_digest = std::string(kDigest),
    .verification_record_digest = std::string(kDigest),
    .outcome = AdmissionOutcome::admitted,
    .reservation = {},
    .reason = std::string(8U, 'r'),
  };
  EXPECT_TRUE(AdmissionRecord::create(admission).ok());
  admission.reason = "";
  EXPECT_FALSE(AdmissionRecord::create(admission).ok());
}

} // namespace
