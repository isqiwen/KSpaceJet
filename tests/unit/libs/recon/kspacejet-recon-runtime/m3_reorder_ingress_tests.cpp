#include "kspacejet/recon/runtime/m3_reorder_ingress.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::base::Result;
using ksj::recon::ArtifactDigest;
using ksj::recon::ExecutionPlan;
using ksj::recon::ExecutionPlanSpec;
using ksj::recon::Quantity;
using ksj::recon::ResourceVector;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::VerificationRecord;
using ksj::recon::VerificationRecordSpec;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::CompletedFrameLease;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FixedReorderBuffer;
using ksj::recon::runtime::FixedReorderBufferState;
using ksj::recon::runtime::FrameDispatch;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::M3ReorderIngress;
using ksj::recon::runtime::OpaqueReorderPayloadHandle;
using ksj::recon::runtime::ResourceVectorLedger;

constexpr std::string_view kPlanDigest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view kVerificationDigest =
  "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

struct RuntimeArtifacts {
  ExecutionPlan execution_plan;
  VerificationRecord verification_record;
};

// Heap ownership keeps the caller-provided slab and each address-sealed
// runtime component stable while Result/fixture values move in a test.
struct Runtime {
  RuntimeArtifacts artifacts;
  std::unique_ptr<std::vector<byte>> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  std::unique_ptr<FixedReorderBuffer> buffer;
  std::unique_ptr<HostFrameAssembler> host;
  std::unique_ptr<M3ReorderIngress> ingress;
};

[[nodiscard]] Result<ArtifactDigest> parse_digest(const std::string_view value) {
  return ArtifactDigest::parse(value, "M3ReorderIngress regression test digest");
}

[[nodiscard]] ResourceVectorSpec resource_vector_spec(const ResourceVector& resources) {
  return {
    .host_normal_bytes = resources.host_normal_bytes(),
    .host_pinned_bytes = resources.host_pinned_bytes(),
    .host_hugepage_bytes = resources.host_hugepage_bytes(),
    .shared_host_bytes = resources.shared_host_bytes(),
    .spool_bytes = resources.spool_bytes(),
    .transport_bytes = resources.transport_bytes(),
    .descriptor_count = resources.descriptor_count(),
    .async_token_count = resources.async_token_count(),
    .cpu_leaf_permits = resources.cpu_leaf_permits(),
    .backend_gang_permits = resources.backend_gang_permits(),
    .provider_private_permits = resources.provider_private_permits(),
    .io_slots = resources.io_slots(),
  };
}

[[nodiscard]] Result<RuntimeArtifacts> make_artifacts(const Quantity max_ahead_items = 3U) {
  constexpr Quantity kDomain = 4U;
  constexpr Quantity kChargedBytes = 8U;
  if (max_ahead_items == 0U || max_ahead_items > kDomain) {
    return ksj::base::Status::InvalidArgument("test ahead capacity is outside the frozen domain");
  }
  const auto key_metadata =
    ksj::recon::dense_key_slot_host_metadata_charged_bytes(kDomain, kDomain, "M3 ingress test key metadata");
  if (!key_metadata.ok()) {
    return key_metadata.status();
  }
  const auto reorder_metadata = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
    kDomain, max_ahead_items, "M3 ingress test reorder metadata");
  if (!reorder_metadata.ok()) {
    return reorder_metadata.status();
  }
  const auto plan_digest = parse_digest(kPlanDigest);
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }

  ExecutionPlanSpec specification;
  specification.inputs = {
    .resolved_pipeline = std::string(kPlanDigest),
    .scan_descriptor = std::string(kPlanDigest),
    .target_envelope = std::string(kPlanDigest),
    .machine_policy = std::string(kPlanDigest),
    .provider_contracts = {std::string(kPlanDigest)},
  };
  specification.execution_profile = ksj::recon::ExecutionProfile::offline;
  specification.key_slot_tables = {
    {
      .node_id = "test-node",
      .dense_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = kDomain}},
      .key_domain_bound = kDomain,
      .max_distinct_keys = kDomain,
      .max_live_keys = kDomain,
      .slot_count = kDomain,
      .host_metadata_charged_bytes = key_metadata.value(),
      .max_items_per_activation = 1U,
      .max_charged_bytes_per_activation = kChargedBytes,
    },
  };
  specification.reorder_plans = {
    {
      .node_id = "test-node",
      .order_domain_id = "test-node",
      .ordinal_binding_id = std::string(ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
      .completed_frame_input_port = "completed-frame",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .charged_bytes_per_ordinal = kChargedBytes,
      .ordinal_dimensions = {{.field = "slice", .minimum = 0U, .cardinality = kDomain}},
      .mapping_algorithm_id = std::string(ksj::recon::kDenseCartesianReorderMappingAlgorithmId),
      .storage_accounting_id = std::string(ksj::recon::kDenseCartesianReorderStorageAccountingId),
      .ordinal_domain_bound = kDomain,
      .first_expected_ordinal = ksj::recon::kFirstExpectedReorderOrdinal,
      .last_expected_ordinal = kDomain - 1U,
      .max_ahead_items = max_ahead_items,
      .max_ahead_charged_bytes = max_ahead_items * kChargedBytes,
      .max_gap_ordinals = kDomain - 1U,
      .occurrence_policy = std::string(ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy),
      .publish_policy = std::string(ksj::recon::kNextExpectedOnlyReorderPublishPolicy),
      .certified_skipped_ordinals = {},
      .end_of_input_policy = std::string(ksj::recon::kFailReorderEndOfInputPolicy),
      .handle_storage_charged_bytes = max_ahead_items * ksj::recon::kDenseCartesianReorderHandleSidecarChargedBytes,
      .host_metadata_charged_bytes = reorder_metadata.value(),
      .descriptor_charged_count = max_ahead_items,
    },
  };
  specification.resource_vector = {
    // This fixture intentionally has no M3.7 BufferPoolPlan/DataEdgePlan.
    // Its legacy opaque ReorderPlan therefore owns its full ahead payload in
    // addition to the KeySlot and Reorder bookkeeping charges.
    .host_normal_bytes = key_metadata.value() + reorder_metadata.value() + max_ahead_items * kChargedBytes,
    .descriptor_count = max_ahead_items,
  };
  specification.terminal_occurrences = kDomain;
  specification.proof_obligations = {
    std::string(ksj::recon::kM3CompletedFrameSlotBindingProofObligation),
    std::string(ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption),
  };
  auto plan = ExecutionPlan::create(std::move(plan_digest).value(), specification);
  if (!plan.ok()) {
    return plan.status();
  }
  const auto verification_digest = parse_digest(kVerificationDigest);
  if (!verification_digest.ok()) {
    return verification_digest.status();
  }
  auto verification =
    VerificationRecord::create(std::move(verification_digest).value(),
                               {
                                 .execution_plan_digest = plan.value().digest().value(),
                                 .execution_profile = plan.value().execution_profile(),
                                 .verified_resource_vector = resource_vector_spec(plan.value().resources()),
                                 .verified_terminal_occurrences = plan.value().terminal_occurrences(),
                                 .verified_obligations =
                                   {
                                     std::string(ksj::recon::kM3CompletedFrameSlotBindingVerificationObligation),
                                     std::string(ksj::recon::kM3StrictDenseAllTuplesEoiVerificationObligation),
                                   },
                               });
  if (!verification.ok()) {
    return verification.status();
  }
  return RuntimeArtifacts{std::move(plan).value(), std::move(verification).value()};
}

[[nodiscard]] CartesianFrameSlotConfig frame_slot_config(const std::uint32_t slot_id) {
  return {
    .slot_id = slot_id,
    .dimensions =
      {.readout_samples = 1U, .phase_encode_1 = 1U, .phase_encode_2 = 1U, .channels = 1U, .bytes_per_sample = 1U},
    .completion = {.required_indices = {{.phase_encode_1 = 0U, .phase_encode_2 = 0U}}},
    .resource_upper_bound = {.max_total_arrivals = 1U, .max_duplicate_arrivals = 0U, .max_payload_bytes = 1U},
    .duplicate_policy = DuplicateAcquisitionPolicy::reject,
    .incomplete_policy = IncompleteFramePolicy::fail,
  };
}

[[nodiscard]] HostFrameAssemblerConfig host_config(const Quantity max_ahead_items = 3U) {
  HostFrameAssemblerConfig configuration{
    .scan_instance_id = "m3-reorder-ingress-regression-scan",
  };
  // M3 reserves one source head slot beyond the reorder ahead window so the
  // next completed frame can make progress while the window drains.
  configuration.frame_slots.reserve(static_cast<std::size_t>(max_ahead_items + 1U));
  for (Quantity slot = 0U; slot <= max_ahead_items; ++slot) {
    configuration.frame_slots.push_back(frame_slot_config(static_cast<std::uint32_t>(slot + 1U)));
  }
  return configuration;
}

[[nodiscard]] Result<Runtime> make_unbound_runtime(const Quantity max_ahead_items = 3U) {
  auto artifacts = make_artifacts(max_ahead_items);
  if (!artifacts.ok()) {
    return artifacts.status();
  }
  const auto& reorder_plan = artifacts.value().execution_plan.reorder_plans().front();
  const auto storage_bytes = ksj::recon::runtime::required_storage_bytes(reorder_plan);
  if (!storage_bytes.ok()) {
    return storage_bytes.status();
  }
  auto storage = std::make_unique<std::vector<byte>>(storage_bytes.value(), byte{0});
  const auto host_charge = reorder_plan.host_metadata_charged_bytes();
  const auto capacity = ResourceVectorCapacity::create(
    {.domains = {.host_normal_bytes = host_charge, .descriptor_count = reorder_plan.descriptor_charged_count()},
     .host_total_cap_bytes = host_charge});
  if (!capacity.ok()) {
    return capacity.status();
  }
  auto ledger = std::make_shared<ResourceVectorLedger>(capacity.value());
  auto buffer =
    FixedReorderBuffer::create(artifacts.value().execution_plan, artifacts.value().verification_record, "test-node",
                               {storage->data(), storage->size()}, {.resource_ledger = ledger});
  if (!buffer.ok()) {
    return buffer.status();
  }
  auto fixed_buffer = std::make_unique<FixedReorderBuffer>(std::move(buffer).value());
  auto host = HostFrameAssembler::create(artifacts.value().execution_plan, artifacts.value().verification_record,
                                         "test-node", host_config(max_ahead_items));
  if (!host.ok()) {
    return host.status();
  }
  return Runtime{std::move(artifacts).value(), std::move(storage),      std::move(ledger),
                 std::move(fixed_buffer),      std::move(host).value(), nullptr};
}

[[nodiscard]] Result<Runtime> make_runtime(const Quantity max_ahead_items = 3U) {
  auto unbound = make_unbound_runtime(max_ahead_items);
  if (!unbound.ok()) {
    return unbound.status();
  }
  auto runtime = std::move(unbound).value();
  auto ingress = M3ReorderIngress::create(runtime.artifacts.execution_plan, runtime.artifacts.verification_record,
                                          "test-node", *runtime.host, *runtime.buffer);
  if (!ingress.ok()) {
    return ingress.status();
  }
  runtime.ingress = std::make_unique<M3ReorderIngress>(std::move(ingress).value());
  return runtime;
}

[[nodiscard]] FrameSlotContext context(const std::uint16_t slice) {
  FrameSlotContext value;
  value.semantic_key.slice = slice;
  value.order_key = slice;
  value.placement_key = slice;
  return value;
}

[[nodiscard]] Result<CompletedFrameLease> complete_frame(HostFrameAssembler& host, const std::uint16_t slice) {
  auto assembling = host.try_begin_frame(context(slice));
  if (!assembling.ok()) {
    return assembling.status();
  }
  const std::array<byte, 1U> sample{byte{42U}};
  auto frame = std::move(assembling).value();
  const auto scattered = frame.scatter({.phase_encode_1 = 0U, .phase_encode_2 = 0U}, sample);
  if (!scattered.ok()) {
    return scattered;
  }
  return frame.seal_complete();
}

[[nodiscard]] Result<FrameDispatch> prepare_complete(Runtime& runtime, const std::uint16_t slice,
                                                     const std::uint64_t payload) {
  auto completed = complete_frame(*runtime.host, slice);
  if (!completed.ok()) {
    return completed.status();
  }
  auto dispatch = runtime.ingress->try_prepare(completed.value());
  if (!dispatch.ok()) {
    return dispatch.status();
  }
  auto firing = std::move(dispatch).value();
  const auto committed = firing.commit();
  if (!committed.ok()) {
    return committed;
  }
  const auto completed_status = firing.complete(OpaqueReorderPayloadHandle::from_opaque_id(payload));
  if (!completed_status.ok()) {
    return completed_status;
  }
  return firing;
}

TEST(KSpaceJetM3ReorderIngressRegression, CompletedFrameDispatchBytesMeetFrozenProviderAbiAlignment) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto completed = complete_frame(*runtime.host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  auto prepared = runtime.ingress->try_prepare(completed.value());
  ASSERT_TRUE(prepared.ok()) << prepared.status();
  auto dispatch = std::move(prepared).value();
  ASSERT_TRUE(dispatch.commit().ok());

  const auto bytes = dispatch.input_bytes();
  ASSERT_TRUE(bytes.ok()) << bytes.status();
  ASSERT_NE(nullptr, bytes.value().data());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(bytes.value().data()) %
                  ksj::recon::runtime::kCartesianFrameSlotStorageAlignment);

  ASSERT_TRUE(dispatch.abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, OutOfOrderCompletionGatesPublishAndRetriesAfterHeadAcknowledgement) {
  auto created = make_runtime(3U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto tail = prepare_complete(runtime, 2U, 102U);
  ASSERT_TRUE(tail.ok()) << tail.status();
  auto tail_dispatch = std::move(tail).value();
  EXPECT_EQ(ksj::base::StatusCode::unavailable, tail_dispatch.try_acquire_publish().status().code());

  auto head = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(head.ok()) << head.status();
  auto head_publish = head.value().try_acquire_publish();
  ASSERT_TRUE(head_publish.ok()) << head_publish.status();
  EXPECT_EQ(0U, head_publish.value().output().ordinal);
  ASSERT_TRUE(head_publish.value().acknowledge_published().ok());

  auto middle = prepare_complete(runtime, 1U, 101U);
  ASSERT_TRUE(middle.ok()) << middle.status();
  auto middle_publish = middle.value().try_acquire_publish();
  ASSERT_TRUE(middle_publish.ok()) << middle_publish.status();
  EXPECT_EQ(1U, middle_publish.value().output().ordinal);
  ASSERT_TRUE(middle_publish.value().acknowledge_published().ok());

  auto tail_publish = tail_dispatch.try_acquire_publish();
  ASSERT_TRUE(tail_publish.ok()) << tail_publish.status();
  EXPECT_EQ(2U, tail_publish.value().output().ordinal);
  ASSERT_TRUE(tail_publish.value().acknowledge_published().ok());
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedM3PublishLeaseFailsHostAndDrainsTheReorderPool) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto dispatch = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(dispatch.ok()) << dispatch.status();
  {
    auto publish = dispatch.value().try_acquire_publish();
    ASSERT_TRUE(publish.ok()) << publish.status();
    ASSERT_TRUE(runtime.ingress->abort().ok());
    EXPECT_EQ(FixedReorderBufferState::failed_draining, runtime.buffer->snapshot().state);
    EXPECT_FALSE(runtime.ledger->snapshot().used.empty());
  }
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedCompletedFrameDispatchFailsBothSidesBeforePublish) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  {
    auto dispatch = prepare_complete(runtime, 0U, 100U);
    ASSERT_TRUE(dispatch.ok()) << dispatch.status();
    EXPECT_EQ(1U, runtime.buffer->snapshot().direct_head_items);
    EXPECT_EQ(0U, runtime.buffer->snapshot().retained_items);
  }
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedBoundFrameAssemblyLeaseImmediatelyFailsTheReorderBuffer) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  {
    auto assembling = runtime.host->try_begin_frame(context(0U));
    ASSERT_TRUE(assembling.ok()) << assembling.status();
    EXPECT_TRUE(assembling.value().valid());
  }
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedBoundCompletedLeaseImmediatelyFailsTheReorderBuffer) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  {
    auto completed = complete_frame(*runtime.host, 0U);
    ASSERT_TRUE(completed.ok()) << completed.status();
    EXPECT_TRUE(completed.value().valid());
  }
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedM3PublishLeaseFailsBothSidesWithoutExplicitIngressAbort) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto dispatch = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(dispatch.ok()) << dispatch.status();
  {
    auto publish = dispatch.value().try_acquire_publish();
    ASSERT_TRUE(publish.ok()) << publish.status();
    EXPECT_TRUE(publish.value().valid());
  }
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, CompleteRecyclesHostSlotAndKeepsTheCurrentHeadOutOfAheadCredits) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto dispatch = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(dispatch.ok()) << dispatch.status();
  EXPECT_EQ(4U, runtime.host->snapshot().free_slots);
  EXPECT_EQ(1U, runtime.buffer->snapshot().direct_head_items);
  EXPECT_EQ(0U, runtime.buffer->snapshot().retained_items);

  auto publish = dispatch.value().try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  EXPECT_EQ(1U, runtime.buffer->snapshot().direct_head_items);
  EXPECT_EQ(0U, runtime.buffer->snapshot().retained_items);
  ASSERT_TRUE(publish.value().acknowledge_published().ok());
  EXPECT_EQ(0U, runtime.buffer->snapshot().direct_head_items);
  EXPECT_EQ(0U, runtime.buffer->snapshot().retained_items);
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedLaterSourceLeaseBlocksAnOlderCompletedDispatchFromPublishing) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto older = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(older.ok()) << older.status();
  {
    auto dropped = complete_frame(*runtime.host, 1U);
    ASSERT_TRUE(dropped.ok()) << dropped.status();
  }
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::failed_draining, runtime.buffer->snapshot().state);
  EXPECT_FALSE(runtime.ledger->snapshot().used.empty());

  const auto publish = older.value().try_acquire_publish();
  EXPECT_EQ(ksj::base::StatusCode::state_error, publish.status().code());
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, DroppedLaterSourceLeaseBlocksHeldPublishAcknowledgement) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto older = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(older.ok()) << older.status();
  auto publish = older.value().try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  {
    auto dropped = complete_frame(*runtime.host, 1U);
    ASSERT_TRUE(dropped.ok()) << dropped.status();
  }
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::failed_draining, runtime.buffer->snapshot().state);
  EXPECT_FALSE(runtime.ledger->snapshot().used.empty());

  const auto acknowledged = publish.value().acknowledge_published();
  EXPECT_EQ(ksj::base::StatusCode::state_error, acknowledged.code());
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, AheadCapacityCountsOnlyFutureOrdinalsAndLeavesTheHeadForProgress) {
  auto created = make_runtime(1U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto one = prepare_complete(runtime, 1U, 101U);
  ASSERT_TRUE(one.ok()) << one.status();
  auto zero = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(zero.ok()) << zero.status();
  // Ordinal one occupies the one plan-charged future slot. Ordinal zero is
  // the direct current head, so both can make progress with A=1.
  EXPECT_EQ(1U, runtime.buffer->snapshot().retained_items);
  EXPECT_EQ(1U, runtime.buffer->snapshot().direct_head_items);
  EXPECT_EQ(0U, runtime.buffer->snapshot().free_slots);

  auto two_lease = complete_frame(*runtime.host, 2U);
  ASSERT_TRUE(two_lease.ok()) << two_lease.status();
  EXPECT_EQ(ksj::base::StatusCode::unavailable, runtime.ingress->try_prepare(two_lease.value()).status().code());
  EXPECT_TRUE(two_lease.value().valid());

  auto zero_publish = zero.value().try_acquire_publish();
  ASSERT_TRUE(zero_publish.ok()) << zero_publish.status();
  ASSERT_TRUE(zero_publish.value().acknowledge_published().ok());
  // The window now reaches ordinal two, but its only ahead slot remains
  // owned by ordinal one until ordinal one is published.
  EXPECT_EQ(ksj::base::StatusCode::unavailable, runtime.ingress->try_prepare(two_lease.value()).status().code());
  EXPECT_TRUE(two_lease.value().valid());

  auto one_publish = one.value().try_acquire_publish();
  ASSERT_TRUE(one_publish.ok()) << one_publish.status();
  ASSERT_TRUE(one_publish.value().acknowledge_published().ok());

  auto two = runtime.ingress->try_prepare(two_lease.value());
  ASSERT_TRUE(two.ok()) << two.status();
  EXPECT_FALSE(two_lease.value().valid());
  auto two_dispatch = std::move(two).value();
  ASSERT_TRUE(two_dispatch.commit().ok());
  ASSERT_TRUE(two_dispatch.complete(OpaqueReorderPayloadHandle::from_opaque_id(102U)).ok());
  auto two_publish = two_dispatch.try_acquire_publish();
  ASSERT_TRUE(two_publish.ok()) << two_publish.status();
  ASSERT_TRUE(two_publish.value().acknowledge_published().ok());
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, HostEoiPrecedesAndSurfacesTheStrictReorderGap) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto dispatch = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(dispatch.ok()) << dispatch.status();
  const auto end_status = runtime.ingress->end_of_input();
  EXPECT_EQ(ksj::base::StatusCode::validation_error, end_status.code());
  EXPECT_EQ("REORDER_GAP_AT_EOI", end_status.message());
  EXPECT_TRUE(runtime.host->snapshot().ingress_closed);
  EXPECT_TRUE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::failed_draining, runtime.buffer->snapshot().state);
  ASSERT_TRUE(dispatch.value().abort().ok());
  EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
  EXPECT_TRUE(runtime.ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngressRegression, NormalPublishAcknowledgementDoesNotFailTheHost) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto dispatch = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(dispatch.ok()) << dispatch.status();
  auto publish = dispatch.value().try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  ASSERT_TRUE(publish.value().acknowledge_published().ok());
  EXPECT_FALSE(runtime.host->snapshot().failed);
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, TransferredDispatchCannotAbortThePublishOwner) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto dispatch = prepare_complete(runtime, 0U, 100U);
  ASSERT_TRUE(dispatch.ok()) << dispatch.status();
  auto publish = dispatch.value().try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  EXPECT_EQ(ksj::base::StatusCode::state_error, dispatch.value().abort().code());
  EXPECT_EQ(FixedReorderBufferState::accepting, runtime.buffer->snapshot().state);
  EXPECT_FALSE(runtime.host->snapshot().failed);
  ASSERT_TRUE(publish.value().acknowledge_published().ok());
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, BoundRuntimeRejectsIndependentTerminalControl) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  EXPECT_EQ(ksj::base::StatusCode::state_error, runtime.buffer->end_of_input().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, runtime.buffer->abort().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, runtime.host->end_of_input().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, runtime.host->abort().code());
  EXPECT_EQ(FixedReorderBufferState::accepting, runtime.buffer->snapshot().state);
  EXPECT_FALSE(runtime.host->snapshot().failed);
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, BoundCompletedLeaseRejectsManualConsumptionButIngressCanPerformTheHandoff) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto completed = complete_frame(*runtime.host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  EXPECT_EQ(ksj::base::StatusCode::state_error, completed.value().begin_dispatch().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, completed.value().acknowledge_consumed().code());
  EXPECT_EQ(ksj::base::StatusCode::state_error, completed.value().abandon().code());
  EXPECT_TRUE(completed.value().valid());
  EXPECT_FALSE(runtime.host->snapshot().failed);

  auto prepared = runtime.ingress->try_prepare(completed.value());
  ASSERT_TRUE(prepared.ok()) << prepared.status();
  EXPECT_FALSE(completed.value().valid());
  auto dispatch = std::move(prepared).value();
  ASSERT_TRUE(dispatch.commit().ok());
  ASSERT_TRUE(dispatch.complete(OpaqueReorderPayloadHandle::from_opaque_id(100U)).ok());
  auto publish = dispatch.try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  ASSERT_TRUE(publish.value().acknowledge_published().ok());
  EXPECT_FALSE(runtime.host->snapshot().failed);
  ASSERT_TRUE(runtime.ingress->abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, BindingRefusesAHostThatHasAlreadyStartedSourceWork) {
  auto created = make_unbound_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto completed = complete_frame(*runtime.host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  const auto ingress = M3ReorderIngress::create(runtime.artifacts.execution_plan, runtime.artifacts.verification_record,
                                                "test-node", *runtime.host, *runtime.buffer);
  EXPECT_FALSE(ingress.ok());
  EXPECT_TRUE(completed.value().valid());
  EXPECT_FALSE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::accepting, runtime.buffer->snapshot().state);
  ASSERT_TRUE(completed.value().abandon().ok());

  // A rejected admission must leave the buffer untouched, so a pristine host
  // can still be admitted to it.
  auto pristine_host = HostFrameAssembler::create(runtime.artifacts.execution_plan,
                                                  runtime.artifacts.verification_record, "test-node", host_config());
  ASSERT_TRUE(pristine_host.ok()) << pristine_host.status();
  auto rebound = M3ReorderIngress::create(runtime.artifacts.execution_plan, runtime.artifacts.verification_record,
                                          "test-node", *pristine_host.value(), *runtime.buffer);
  ASSERT_TRUE(rebound.ok()) << rebound.status();
  ASSERT_TRUE(rebound.value().abort().ok());
}

TEST(KSpaceJetM3ReorderIngressRegression, BindingRejectsRecycledPreAdmissionSourceWork) {
  auto created = make_unbound_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  auto completed = complete_frame(*runtime.host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  ASSERT_TRUE(completed.value().begin_dispatch().ok());
  ASSERT_TRUE(completed.value().acknowledge_consumed().ok());
  EXPECT_FALSE(completed.value().valid());
  EXPECT_EQ(4U, runtime.host->snapshot().free_slots);

  auto ingress = M3ReorderIngress::create(runtime.artifacts.execution_plan, runtime.artifacts.verification_record,
                                          "test-node", *runtime.host, *runtime.buffer);
  EXPECT_FALSE(ingress.ok());
  EXPECT_FALSE(runtime.host->snapshot().failed);
  EXPECT_EQ(FixedReorderBufferState::accepting, runtime.buffer->snapshot().state);
}

TEST(KSpaceJetM3ReorderIngressRegression, NormalCompletedIngressDetachesTheHostNotifierBeforeBufferDestruction) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  for (std::uint16_t slice = 0U; slice < 4U; ++slice) {
    auto dispatch = prepare_complete(runtime, slice, 100U + slice);
    ASSERT_TRUE(dispatch.ok()) << dispatch.status();
    auto publish = dispatch.value().try_acquire_publish();
    ASSERT_TRUE(publish.ok()) << publish.status();
    ASSERT_TRUE(publish.value().acknowledge_published().ok());
  }
  ASSERT_TRUE(runtime.ingress->end_of_input().ok());
  EXPECT_EQ(FixedReorderBufferState::completed, runtime.buffer->snapshot().state);

  runtime.ingress.reset();
  runtime.buffer.reset();
  // Host destruction must not dereference the completed buffer's old notifier.
  runtime.host.reset();
}

TEST(KSpaceJetM3ReorderIngressRegression, FailedIngressDetachesTheNotifierBeforeARetainedLeaseDrops) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  {
    auto retained = complete_frame(*runtime.host, 0U);
    ASSERT_TRUE(retained.ok()) << retained.status();
    runtime.ingress.reset();
    EXPECT_TRUE(runtime.host->snapshot().failed);
    EXPECT_EQ(FixedReorderBufferState::failed, runtime.buffer->snapshot().state);
    runtime.buffer.reset();
  }
  // The retained lease was destroyed after its buffer; a detached Host state
  // makes the later Host teardown safe as well.
  runtime.host.reset();
}

TEST(KSpaceJetM3ReorderIngressRegression, EoiDrainingPublishAcknowledgementCompletesBeforeIngressDestruction) {
  auto created = make_runtime();
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  // The frozen domain has four ordinals but only three ahead credits. Publish
  // the head first, then hold the next publish while all remaining ordinals
  // have completed; this is the legal EOI-draining state.
  auto dispatch_0 = prepare_complete(runtime, 0U, 100U);
  auto dispatch_1 = prepare_complete(runtime, 1U, 101U);
  auto dispatch_2 = prepare_complete(runtime, 2U, 102U);
  ASSERT_TRUE(dispatch_0.ok()) << dispatch_0.status();
  ASSERT_TRUE(dispatch_1.ok()) << dispatch_1.status();
  ASSERT_TRUE(dispatch_2.ok()) << dispatch_2.status();

  auto published_0 = dispatch_0.value().try_acquire_publish();
  ASSERT_TRUE(published_0.ok()) << published_0.status();
  ASSERT_TRUE(published_0.value().acknowledge_published().ok());

  auto dispatch_3 = prepare_complete(runtime, 3U, 103U);
  ASSERT_TRUE(dispatch_3.ok()) << dispatch_3.status();

  auto held_publish = dispatch_1.value().try_acquire_publish();
  ASSERT_TRUE(held_publish.ok()) << held_publish.status();
  ASSERT_TRUE(runtime.ingress->end_of_input().ok());
  EXPECT_EQ(FixedReorderBufferState::draining, runtime.buffer->snapshot().state);
  ASSERT_TRUE(held_publish.value().acknowledge_published().ok());
  auto published_2 = dispatch_2.value().try_acquire_publish();
  ASSERT_TRUE(published_2.ok()) << published_2.status();
  ASSERT_TRUE(published_2.value().acknowledge_published().ok());
  auto published_3 = dispatch_3.value().try_acquire_publish();
  ASSERT_TRUE(published_3.ok()) << published_3.status();
  ASSERT_TRUE(published_3.value().acknowledge_published().ok());
  EXPECT_EQ(FixedReorderBufferState::completed, runtime.buffer->snapshot().state);
  runtime.ingress.reset();
  EXPECT_FALSE(runtime.host->snapshot().failed);
}

} // namespace
