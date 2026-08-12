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
using ksj::recon::runtime::DenseCartesianOrdinalMapper;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FixedReorderBuffer;
using ksj::recon::runtime::FixedReorderBufferState;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::M3ReorderIngress;
using ksj::recon::runtime::OpaqueReorderPayloadHandle;
using ksj::recon::runtime::ResourceVectorLedger;

constexpr std::string_view kPlanDigest = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
constexpr std::string_view kVerificationDigest =
  "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr std::string_view kOtherPlanDigest = "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
constexpr std::string_view kOtherVerificationDigest =
  "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

struct SemanticDomain {
  Quantity encoding{1U};
  Quantity average{1U};
  Quantity slice{4U};
  Quantity contrast{1U};
  Quantity phase{1U};
  Quantity repetition{1U};
  Quantity set{1U};
};

struct RuntimeArtifacts {
  ExecutionPlan execution_plan;
  VerificationRecord verification_record;
};

struct BoundIngress {
  std::unique_ptr<HostFrameAssembler> assembler;
  M3ReorderIngress ingress;
};

[[nodiscard]] Result<ArtifactDigest> artifact_digest(const std::string_view value) {
  return ArtifactDigest::parse(value, "M3.5 reorder ingress test digest");
}

[[nodiscard]] Quantity domain_size(const SemanticDomain& domain) {
  return domain.encoding * domain.average * domain.slice * domain.contrast * domain.phase * domain.repetition *
         domain.set;
}

[[nodiscard]] std::vector<ksj::recon::DenseKeySlotDimensionSpec> key_dimensions(const SemanticDomain& domain) {
  return {
    {.field = "encoding", .minimum = 0U, .cardinality = domain.encoding},
    {.field = "average", .minimum = 0U, .cardinality = domain.average},
    {.field = "slice", .minimum = 0U, .cardinality = domain.slice},
    {.field = "contrast", .minimum = 0U, .cardinality = domain.contrast},
    {.field = "phase", .minimum = 0U, .cardinality = domain.phase},
    {.field = "repetition", .minimum = 0U, .cardinality = domain.repetition},
    {.field = "set", .minimum = 0U, .cardinality = domain.set},
  };
}

[[nodiscard]] std::vector<ksj::recon::DenseCartesianOrdinalDimensionSpec>
ordinal_dimensions(const SemanticDomain& domain) {
  return {
    {.field = "encoding", .minimum = 0U, .cardinality = domain.encoding},
    {.field = "average", .minimum = 0U, .cardinality = domain.average},
    {.field = "slice", .minimum = 0U, .cardinality = domain.slice},
    {.field = "contrast", .minimum = 0U, .cardinality = domain.contrast},
    {.field = "phase", .minimum = 0U, .cardinality = domain.phase},
    {.field = "repetition", .minimum = 0U, .cardinality = domain.repetition},
    {.field = "set", .minimum = 0U, .cardinality = domain.set},
  };
}

[[nodiscard]] Result<ExecutionPlanSpec> make_execution_plan_spec(const SemanticDomain domain = {},
                                                                 const Quantity max_ahead_items = 3U,
                                                                 const Quantity charged_bytes = 8U) {
  const auto cardinality = domain_size(domain);
  if (cardinality == 0U || max_ahead_items == 0U || max_ahead_items > cardinality || charged_bytes == 0U) {
    return ksj::base::Status::InvalidArgument("M3.5 reorder ingress test domain is invalid");
  }
  const auto key_metadata =
    ksj::recon::dense_key_slot_host_metadata_charged_bytes(cardinality, cardinality, "test KeySlot metadata");
  if (!key_metadata.ok()) {
    return key_metadata.status();
  }
  const auto reorder_metadata = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
    cardinality, max_ahead_items, "test Reorder metadata");
  if (!reorder_metadata.ok()) {
    return reorder_metadata.status();
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
      .dense_dimensions = key_dimensions(domain),
      .key_domain_bound = cardinality,
      .max_distinct_keys = cardinality,
      .max_live_keys = cardinality,
      .slot_count = cardinality,
      .host_metadata_charged_bytes = key_metadata.value(),
      .max_items_per_activation = 1U,
      .max_charged_bytes_per_activation = charged_bytes,
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
      .charged_bytes_per_ordinal = charged_bytes,
      .ordinal_dimensions = ordinal_dimensions(domain),
      .mapping_algorithm_id = std::string(ksj::recon::kDenseCartesianReorderMappingAlgorithmId),
      .storage_accounting_id = std::string(ksj::recon::kDenseCartesianReorderStorageAccountingId),
      .ordinal_domain_bound = cardinality,
      .first_expected_ordinal = ksj::recon::kFirstExpectedReorderOrdinal,
      .last_expected_ordinal = cardinality - 1U,
      .max_ahead_items = max_ahead_items,
      .max_ahead_charged_bytes = max_ahead_items * charged_bytes,
      .max_gap_ordinals = cardinality - 1U,
      .occurrence_policy = std::string(ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy),
      .publish_policy = std::string(ksj::recon::kNextExpectedOnlyReorderPublishPolicy),
      .certified_skipped_ordinals = {},
      .end_of_input_policy = std::string(ksj::recon::kFailReorderEndOfInputPolicy),
      .host_metadata_charged_bytes = reorder_metadata.value(),
      .descriptor_charged_count = max_ahead_items,
    },
  };
  specification.resource_vector = {
    .host_normal_bytes = key_metadata.value() + reorder_metadata.value() + max_ahead_items * charged_bytes,
    .descriptor_count = max_ahead_items,
  };
  specification.terminal_occurrences = cardinality;
  specification.proof_obligations = {
    std::string(ksj::recon::kM3CompletedFrameSlotBindingProofObligation),
    std::string(ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption),
  };
  return specification;
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

[[nodiscard]] Result<RuntimeArtifacts>
make_artifacts(const SemanticDomain domain = {}, const Quantity max_ahead_items = 3U, const Quantity charged_bytes = 8U,
               const std::string_view plan_digest_value = kPlanDigest,
               const std::string_view verification_digest_value = kVerificationDigest) {
  const auto specification = make_execution_plan_spec(domain, max_ahead_items, charged_bytes);
  if (!specification.ok()) {
    return specification.status();
  }
  auto plan_digest = artifact_digest(plan_digest_value);
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }
  auto plan = ExecutionPlan::create(std::move(plan_digest).value(), specification.value());
  if (!plan.ok()) {
    return plan.status();
  }
  auto verification_digest = artifact_digest(verification_digest_value);
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

[[nodiscard]] Quantity credit_pool_host_bytes(const ksj::recon::ReorderPlan& plan) {
  return plan.host_metadata_charged_bytes() + plan.max_ahead_charged_bytes();
}

[[nodiscard]] Result<std::shared_ptr<ResourceVectorLedger>> make_ledger(const ksj::recon::ReorderPlan& plan) {
  const auto capacity = ResourceVectorCapacity::create({
    .domains =
      {
        .host_normal_bytes = credit_pool_host_bytes(plan),
        .descriptor_count = plan.descriptor_charged_count(),
      },
    .host_total_cap_bytes = credit_pool_host_bytes(plan),
  });
  if (!capacity.ok()) {
    return capacity.status();
  }
  return std::make_shared<ResourceVectorLedger>(capacity.value());
}

[[nodiscard]] Result<FixedReorderBuffer> make_buffer(const RuntimeArtifacts& artifacts, std::vector<byte>& storage,
                                                     std::shared_ptr<ResourceVectorLedger>& ledger) {
  const auto& plan = artifacts.execution_plan.reorder_plans().front();
  const auto required = ksj::recon::runtime::required_storage_bytes(plan);
  if (!required.ok()) {
    return required.status();
  }
  storage.assign(required.value(), byte{0});
  auto created_ledger = make_ledger(plan);
  if (!created_ledger.ok()) {
    return created_ledger.status();
  }
  ledger = std::move(created_ledger).value();
  return FixedReorderBuffer::create(artifacts.execution_plan, artifacts.verification_record, "test-node",
                                    {storage.data(), storage.size()}, {.resource_ledger = ledger});
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

[[nodiscard]] HostFrameAssemblerConfig assembler_config() {
  return {
    .scan_instance_id = "m3-reorder-ingress-test-scan",
    .frame_slots = {frame_slot_config(1U), frame_slot_config(2U), frame_slot_config(3U), frame_slot_config(4U)},
  };
}

[[nodiscard]] Result<BoundIngress> bind_ingress(const RuntimeArtifacts& artifacts, FixedReorderBuffer& buffer) {
  auto host = HostFrameAssembler::create(artifacts.execution_plan, artifacts.verification_record, "test-node",
                                         assembler_config());
  if (!host.ok()) {
    return host.status();
  }
  auto ingress = M3ReorderIngress::create(artifacts.execution_plan, artifacts.verification_record, "test-node",
                                          *host.value(), buffer);
  if (!ingress.ok()) {
    return ingress.status();
  }
  return BoundIngress{std::move(host).value(), std::move(ingress).value()};
}

[[nodiscard]] FrameSlotContext context(const std::uint16_t slice = 0U) {
  FrameSlotContext value;
  value.semantic_key.slice = slice;
  value.order_key = slice;
  value.placement_key = slice;
  return value;
}

[[nodiscard]] Result<CompletedFrameLease> complete_frame(HostFrameAssembler& assembler, const FrameSlotContext value) {
  auto acquired = assembler.try_begin_frame(value);
  if (!acquired.ok()) {
    return acquired.status();
  }
  auto assembling = std::move(acquired).value();
  const std::array<byte, 1U> sample{byte{42U}};
  const auto scattered = assembling.scatter({.phase_encode_1 = 0U, .phase_encode_2 = 0U}, sample);
  if (!scattered.ok()) {
    return scattered;
  }
  return assembling.seal_complete();
}

TEST(KSpaceJetDenseCartesianOrdinalMapper, MapsTheFullFrozenFrameSemanticKeyInMixedRadixOrder) {
  const SemanticDomain domain{
    .encoding = 2U, .average = 2U, .slice = 3U, .contrast = 2U, .phase = 2U, .repetition = 2U, .set = 2U};
  auto artifacts = make_artifacts(domain, 3U);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();

  const auto& plan = artifacts.value().execution_plan.reorder_plans().front();
  auto mapper = DenseCartesianOrdinalMapper::create(plan);
  ASSERT_TRUE(mapper.ok()) << mapper.status();
  auto first = context();
  auto last = context(2U);
  last.semantic_key.encoding_space = 1U;
  last.semantic_key.average = 1U;
  last.semantic_key.contrast = 1U;
  last.semantic_key.phase = 1U;
  last.semantic_key.repetition = 1U;
  last.semantic_key.set = 1U;
  EXPECT_EQ(0U, mapper.value().ordinal(first).value());
  EXPECT_EQ(191U, mapper.value().ordinal(last).value());
  auto outside_encoding = first;
  outside_encoding.semantic_key.encoding_space = 2U;
  EXPECT_FALSE(mapper.value().ordinal(outside_encoding).ok());
}

TEST(KSpaceJetFixedReorderBuffer, RequiresVerifiedArtifactsAndOwnsItsSelectedPlanSlice) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(buffer.ok()) << buffer.status();
  EXPECT_EQ(FixedReorderBufferState::accepting, buffer.value().snapshot().state);
  EXPECT_EQ(credit_pool_host_bytes(artifacts.value().execution_plan.reorder_plans().front()),
            ledger->snapshot().used.host_normal_bytes);

  auto different = make_artifacts({}, 3U, 8U, kOtherPlanDigest, kOtherVerificationDigest);
  ASSERT_TRUE(different.ok()) << different.status();
  auto foreign_host = HostFrameAssembler::create(
    different.value().execution_plan, different.value().verification_record, "test-node", assembler_config());
  ASSERT_TRUE(foreign_host.ok()) << foreign_host.status();
  EXPECT_FALSE(M3ReorderIngress::create(different.value().execution_plan, different.value().verification_record,
                                        "test-node", *foreign_host.value(), buffer.value())
                 .ok());
  auto correct_host = HostFrameAssembler::create(
    artifacts.value().execution_plan, artifacts.value().verification_record, "test-node", assembler_config());
  ASSERT_TRUE(correct_host.ok()) << correct_host.status();
  EXPECT_TRUE(M3ReorderIngress::create(artifacts.value().execution_plan, artifacts.value().verification_record,
                                       "test-node", *correct_host.value(), buffer.value())
                .ok());
}

TEST(KSpaceJetM3ReorderIngress, CouplesHostLeaseToOrderedPublishAndCompletesHostBeforeReorderEoi) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();

  for (std::uint16_t slice = 0U; slice < 4U; ++slice) {
    auto completed = complete_frame(*runtime.assembler, context(slice));
    ASSERT_TRUE(completed.ok()) << completed.status();
    auto dispatch = runtime.ingress.try_prepare(completed.value());
    ASSERT_TRUE(dispatch.ok()) << dispatch.status();
    EXPECT_FALSE(completed.value().valid());
    ASSERT_TRUE(dispatch.value().commit().ok());
    const auto bytes = dispatch.value().input_bytes();
    ASSERT_TRUE(bytes.ok()) << bytes.status();
    EXPECT_EQ(1U, bytes.value().size());
    ASSERT_TRUE(dispatch.value().complete(OpaqueReorderPayloadHandle::from_opaque_id(100U + slice)).ok());
    auto published = dispatch.value().try_acquire_publish();
    ASSERT_TRUE(published.ok()) << published.status();
    EXPECT_EQ(slice, published.value().output().ordinal);
    ASSERT_TRUE(published.value().acknowledge_published().ok());
  }
  EXPECT_EQ(4U, runtime.assembler->snapshot().free_slots);
  EXPECT_TRUE(runtime.ingress.end_of_input().ok());
  EXPECT_EQ(FixedReorderBufferState::completed, buffer.snapshot().state);
}

TEST(KSpaceJetM3ReorderIngress, UnavailableKeepsCompletedLeaseForRetry) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();

  auto completed = complete_frame(*runtime.assembler, context(3U));
  ASSERT_TRUE(completed.ok()) << completed.status();
  const auto unavailable = runtime.ingress.try_prepare(completed.value());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, unavailable.status().code());
  EXPECT_TRUE(completed.value().valid());
  EXPECT_EQ(FixedReorderBufferState::accepting, buffer.snapshot().state);
  EXPECT_EQ(ksj::base::StatusCode::state_error, completed.value().abandon().code());
  EXPECT_TRUE(completed.value().valid());
  ASSERT_TRUE(runtime.ingress.abort().ok());
}

TEST(KSpaceJetM3ReorderIngress, ForeignAndMovedLeasesDoNotPoisonTheReceivingScan) {
  auto artifacts = make_artifacts();
  auto foreign_artifacts = make_artifacts({}, 3U, 8U, kOtherPlanDigest, kOtherVerificationDigest);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  ASSERT_TRUE(foreign_artifacts.ok()) << foreign_artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();

  auto foreign_host =
    HostFrameAssembler::create(foreign_artifacts.value().execution_plan, foreign_artifacts.value().verification_record,
                               "test-node", assembler_config());
  ASSERT_TRUE(foreign_host.ok()) << foreign_host.status();
  auto foreign = complete_frame(*foreign_host.value(), context());
  ASSERT_TRUE(foreign.ok()) << foreign.status();
  EXPECT_EQ(ksj::base::StatusCode::state_error, runtime.ingress.try_prepare(foreign.value()).status().code());
  EXPECT_TRUE(foreign.value().valid());
  EXPECT_EQ(FixedReorderBufferState::accepting, buffer.snapshot().state);

  auto moved = std::move(foreign).value();
  EXPECT_EQ(ksj::base::StatusCode::state_error, runtime.ingress.try_prepare(foreign.value()).status().code());
  EXPECT_EQ(FixedReorderBufferState::accepting, buffer.snapshot().state);
  ASSERT_TRUE(moved.abandon().ok());
  ASSERT_TRUE(runtime.ingress.abort().ok());
}

TEST(KSpaceJetM3ReorderIngress, SameIssuerSemanticViolationFailsBothSidesClosed) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();

  auto outside_domain = complete_frame(*runtime.assembler, context(4U));
  ASSERT_TRUE(outside_domain.ok()) << outside_domain.status();
  const auto rejected = runtime.ingress.try_prepare(outside_domain.value());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
  EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
  EXPECT_TRUE(runtime.assembler->snapshot().failed);
  EXPECT_TRUE(ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngress, DroppedDispatchFailsClosedAndSettlesBothLeases) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();

  auto completed = complete_frame(*runtime.assembler, context());
  ASSERT_TRUE(completed.ok()) << completed.status();
  {
    auto dispatch = runtime.ingress.try_prepare(completed.value());
    ASSERT_TRUE(dispatch.ok()) << dispatch.status();
    EXPECT_TRUE(dispatch.value().valid());
  }
  EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
  EXPECT_TRUE(runtime.assembler->snapshot().failed);
  EXPECT_TRUE(ledger->snapshot().used.empty());
}

TEST(KSpaceJetM3ReorderIngress, SourceEoiBlocksReorderEoiUntilIngressCancelsTheCompletedLease) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> storage;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_buffer(artifacts.value(), storage, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();

  auto completed = complete_frame(*runtime.assembler, context());
  ASSERT_TRUE(completed.ok()) << completed.status();
  EXPECT_EQ(ksj::base::StatusCode::unavailable, runtime.ingress.end_of_input().code());
  EXPECT_EQ(FixedReorderBufferState::accepting, buffer.snapshot().state);
  EXPECT_EQ(ksj::base::StatusCode::state_error, completed.value().abandon().code());
  EXPECT_TRUE(completed.value().valid());
  ASSERT_TRUE(runtime.ingress.abort().ok());
  EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
  EXPECT_TRUE(runtime.assembler->snapshot().failed);
}

} // namespace
