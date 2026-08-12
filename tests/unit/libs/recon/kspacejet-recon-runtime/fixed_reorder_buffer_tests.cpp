#include "kspacejet/recon/runtime/m3_reorder_ingress.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::base::Result;
using ksj::recon::ArtifactDigest;
using ksj::recon::ElementType;
using ksj::recon::ExecutionPlan;
using ksj::recon::ExecutionPlanSpec;
using ksj::recon::PayloadKind;
using ksj::recon::PayloadMutability;
using ksj::recon::Quantity;
using ksj::recon::ResourceVector;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::VerificationRecord;
using ksj::recon::VerificationRecordSpec;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::CompletedFrameLease;
using ksj::recon::runtime::DenseCartesianOrdinalMapper;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FixedBufferEdge;
using ksj::recon::runtime::FixedBufferEdgePollKind;
using ksj::recon::runtime::FixedBufferEdgeStorage;
using ksj::recon::runtime::FixedBufferPool;
using ksj::recon::runtime::FixedBufferPoolStorage;
using ksj::recon::runtime::FixedReorderBuffer;
using ksj::recon::runtime::FixedReorderBufferHandleSidecar;
using ksj::recon::runtime::FixedReorderBufferState;
using ksj::recon::runtime::FixedReorderBufferStorage;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::ImmutableBufferHandle;
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
constexpr std::string_view kHandlePayloadDigest =
  "sha256:abababababababababababababababababababababababababababababababab";
constexpr std::string_view kHandleAbiDescriptorDigest =
  "sha256:cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd";
constexpr std::string_view kHandleMetadataDigest =
  "sha256:efefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefef";
constexpr Quantity kHandlePoolPayloadCapacity = 8U;

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
      .handle_storage_charged_bytes = max_ahead_items * ksj::recon::kDenseCartesianReorderHandleSidecarChargedBytes,
      .host_metadata_charged_bytes = reorder_metadata.value(),
      .descriptor_charged_count = max_ahead_items,
    },
  };
  specification.resource_vector = {
    // This fixture intentionally has no M3.7 BufferPoolPlan/DataEdgePlan.
    // Its legacy opaque ReorderPlan therefore owns its full ahead payload in
    // addition to the KeySlot and Reorder bookkeeping charges.
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
  return plan.host_metadata_charged_bytes();
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

[[nodiscard]] Result<FixedReorderBuffer>
make_typed_handle_buffer(const RuntimeArtifacts& artifacts, std::vector<byte>& bookkeeping,
                         std::vector<FixedReorderBufferHandleSidecar>& handle_sidecars,
                         std::shared_ptr<ResourceVectorLedger>& ledger) {
  const auto& plan = artifacts.execution_plan.reorder_plans().front();
  const auto required = ksj::recon::runtime::fixed_reorder_buffer_required_bookkeeping_storage_bytes(plan);
  if (!required.ok()) {
    return required.status();
  }
  bookkeeping.assign(required.value(), byte{0});
  handle_sidecars.clear();
  handle_sidecars.resize(static_cast<std::size_t>(plan.max_ahead_items()));
  auto created_ledger = make_ledger(plan);
  if (!created_ledger.ok()) {
    return created_ledger.status();
  }
  ledger = std::move(created_ledger).value();
  return FixedReorderBuffer::create(
    artifacts.execution_plan, artifacts.verification_record, "test-node",
    FixedReorderBufferStorage{
      .bookkeeping = {bookkeeping.data(), bookkeeping.size()},
      .handle_sidecars = std::span<FixedReorderBufferHandleSidecar>{handle_sidecars.data(), handle_sidecars.size()}},
    {.resource_ledger = ledger});
}

[[nodiscard]] Result<TypeDescriptor> make_reorder_handle_type() {
  return TypeDescriptor::create({
    .type_id = "ksj.fixed-reorder-buffer-handle-test",
    .revision = 1U,
    .abi_descriptor_digest = std::string(kHandleAbiDescriptorDigest),
    .payload_schema_digest = std::string(kHandlePayloadDigest),
    .payload_kind = PayloadKind::buffer_handle,
    .element_type = ElementType::uint8,
    .rank = 1U,
    .dimensions = {"sample"},
    .layout = ksj::recon::LayoutKind::canonical_contiguous,
    .strides = ksj::recon::StrideKind::canonical,
    .explicit_byte_strides = {},
    .allowed_memory_domains = {TypeMemoryDomain::host_normal},
    .min_alignment_bytes = 1U,
    .mutability = PayloadMutability::immutable_after_publish,
    .metadata_schema_digest = std::string(kHandleMetadataDigest),
  });
}

struct HandlePoolSlabs {
  std::vector<byte> payload;
  std::vector<byte> metadata;
  std::vector<byte> control;

  [[nodiscard]] FixedBufferPoolStorage view() {
    return {
      .payload = {payload.data(), payload.size()},
      .metadata = {metadata.data(), metadata.size()},
      .control = {control.data(), control.size()},
    };
  }
};

[[nodiscard]] Result<std::unique_ptr<FixedBufferPool>>
make_handle_pool(const TypeDescriptor& type_descriptor, HandlePoolSlabs& slabs, const Quantity slot_count,
                 std::shared_ptr<ResourceVectorLedger> occupancy_ledger = nullptr) {
  const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(slot_count);
  if (!control.ok()) {
    return control.status();
  }
  slabs = {.payload = std::vector<byte>(static_cast<std::size_t>(slot_count * kHandlePoolPayloadCapacity), byte{0}),
           .metadata = {},
           .control = std::vector<byte>(control.value(), byte{0})};
  return FixedBufferPool::create({.occupancy_ledger = std::move(occupancy_ledger),
                                  .type_descriptor = type_descriptor,
                                  .slot_count = slot_count,
                                  .payload_capacity_bytes = kHandlePoolPayloadCapacity,
                                  .metadata_capacity_bytes = 0U},
                                 slabs.view());
}

[[nodiscard]] Result<ImmutableBufferHandle> seal_handle(FixedBufferPool& pool, const TypeDescriptor& type_descriptor,
                                                        const byte value) {
  auto acquired = pool.try_acquire();
  if (!acquired.ok()) {
    return acquired.status();
  }
  auto lease = std::move(acquired).value();
  const auto payload = lease.writable_payload();
  if (!payload.ok()) {
    return payload.status();
  }
  payload.value()[0] = value;
  return lease.seal(type_descriptor, 1U, {});
}

struct AlignedEdgeControl {
  std::vector<std::max_align_t> words;
  std::size_t byte_count{0U};

  [[nodiscard]] ksj::base::ByteSpan view() { return {reinterpret_cast<byte*>(words.data()), byte_count}; }
};

[[nodiscard]] Result<AlignedEdgeControl> make_edge_control(const Quantity max_items) {
  const auto required = ksj::recon::runtime::fixed_buffer_edge_required_control_storage_bytes(max_items);
  if (!required.ok()) {
    return required.status();
  }
  const auto words = (required.value() + sizeof(std::max_align_t) - 1U) / sizeof(std::max_align_t);
  return AlignedEdgeControl{.words = std::vector<std::max_align_t>(words), .byte_count = required.value()};
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

[[nodiscard]] HostFrameAssemblerConfig assembler_config(const Quantity max_ahead_items = 3U) {
  HostFrameAssemblerConfig configuration{.scan_instance_id = "m3-reorder-ingress-test-scan"};
  configuration.frame_slots.reserve(static_cast<std::size_t>(max_ahead_items + 1U));
  for (Quantity slot = 0U; slot <= max_ahead_items; ++slot) {
    configuration.frame_slots.push_back(frame_slot_config(static_cast<std::uint32_t>(slot + 1U)));
  }
  return configuration;
}

[[nodiscard]] Result<BoundIngress> bind_ingress(const RuntimeArtifacts& artifacts, FixedReorderBuffer& buffer) {
  const auto max_ahead_items = artifacts.execution_plan.reorder_plans().front().max_ahead_items();
  auto host = HostFrameAssembler::create(artifacts.execution_plan, artifacts.verification_record, "test-node",
                                         assembler_config(max_ahead_items));
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
  // With A=2, ordinal three is beyond the frozen future-only window.
  auto artifacts = make_artifacts({}, 2U);
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

TEST(KSpaceJetFixedReorderBufferM37, RejectsTypedSidecarStorageThatDoesNotMatchTheFrozenPlan) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  const auto& plan = artifacts.value().execution_plan.reorder_plans().front();
  const auto bookkeeping_bytes = ksj::recon::runtime::fixed_reorder_buffer_required_bookkeeping_storage_bytes(plan);
  ASSERT_TRUE(bookkeeping_bytes.ok()) << bookkeeping_bytes.status();
  std::vector<byte> bookkeeping(bookkeeping_bytes.value(), byte{0});
  std::vector<FixedReorderBufferHandleSidecar> undersized_sidecars(
    static_cast<std::size_t>(plan.max_ahead_items() - 1U));
  auto ledger = make_ledger(plan);
  ASSERT_TRUE(ledger.ok()) << ledger.status();

  const auto rejected =
    FixedReorderBuffer::create(artifacts.value().execution_plan, artifacts.value().verification_record, "test-node",
                               FixedReorderBufferStorage{.bookkeeping = {bookkeeping.data(), bookkeeping.size()},
                                                         .handle_sidecars =
                                                           std::span<FixedReorderBufferHandleSidecar>{
                                                             undersized_sidecars.data(), undersized_sidecars.size()}},
                               {.resource_ledger = ledger.value()});
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, rejected.status().code());
  EXPECT_TRUE(ledger.value()->snapshot().reserved.empty());
  EXPECT_TRUE(ledger.value()->snapshot().used.empty());
}

TEST(KSpaceJetFixedReorderBufferM37, ClaimsBookkeepingAndTypedSidecarRangesForItsFullCapabilityLifetime) {
  auto artifacts = make_artifacts();
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> sidecars;
  std::shared_ptr<ResourceVectorLedger> first_ledger;
  auto first = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, first_ledger);
  ASSERT_TRUE(first.ok()) << first.status();

  const auto& plan = artifacts.value().execution_plan.reorder_plans().front();
  auto second_ledger = make_ledger(plan);
  ASSERT_TRUE(second_ledger.ok()) << second_ledger.status();
  const auto overlapping = FixedReorderBuffer::create(
    artifacts.value().execution_plan, artifacts.value().verification_record, "test-node",
    FixedReorderBufferStorage{.bookkeeping = {bookkeeping.data(), bookkeeping.size()},
                              .handle_sidecars =
                                std::span<FixedReorderBufferHandleSidecar>{sidecars.data(), sidecars.size()}},
    {.resource_ledger = second_ledger.value()});
  EXPECT_FALSE(overlapping.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, overlapping.status().code());
  EXPECT_TRUE(second_ledger.value()->snapshot().reserved.empty());
  EXPECT_TRUE(second_ledger.value()->snapshot().used.empty());
}

TEST(KSpaceJetFixedReorderBufferM37,
     PreacquiredOutOfOrderEdgeCreditsCommitHandlesInReorderPublishOrderWithoutPayloadDoubleCharge) {
  SemanticDomain domain{};
  domain.slice = 2U;
  // One frozen ahead slot holds ordinal one; ordinal zero travels in the
  // direct head capability. Together they exercise the A=1, domain=2 bound.
  auto artifacts = make_artifacts(domain, 1U, 8U);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> sidecars;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();
  auto type_descriptor = make_reorder_handle_type();
  ASSERT_TRUE(type_descriptor.ok()) << type_descriptor.status();
  HandlePoolSlabs pool_slabs;
  const auto pool_control_bytes = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(2U);
  ASSERT_TRUE(pool_control_bytes.ok()) << pool_control_bytes.status();
  const auto pool_physical_bytes = static_cast<Quantity>(2U * kHandlePoolPayloadCapacity + pool_control_bytes.value());
  const auto pool_capacity = ResourceVectorCapacity::create({
    .domains =
      {
        .host_normal_bytes = pool_physical_bytes,
        .descriptor_count = 2U,
      },
    .host_total_cap_bytes = pool_physical_bytes,
  });
  ASSERT_TRUE(pool_capacity.ok()) << pool_capacity.status();
  auto pool_ledger = std::make_shared<ResourceVectorLedger>(pool_capacity.value());
  auto created_pool = make_handle_pool(type_descriptor.value(), pool_slabs, 2U, pool_ledger);
  ASSERT_TRUE(created_pool.ok()) << created_pool.status();
  auto pool = std::move(created_pool).value();
  EXPECT_EQ(pool_physical_bytes, pool_ledger->snapshot().used.host_normal_bytes);
  EXPECT_EQ(2U, pool_ledger->snapshot().used.descriptor_count);
  const auto& plan = artifacts.value().execution_plan.reorder_plans().front();
  EXPECT_EQ(plan.host_metadata_charged_bytes(), ledger->snapshot().used.host_normal_bytes);
  EXPECT_EQ(plan.descriptor_charged_count(), ledger->snapshot().used.descriptor_count);

  auto edge_control = make_edge_control(2U);
  ASSERT_TRUE(edge_control.ok()) << edge_control.status();
  auto created_edge = FixedBufferEdge::create({.occupancy_ledger = nullptr,
                                               .source_pool = pool.get(),
                                               .max_items = 2U,
                                               .max_logical_bytes = 2U * plan.charged_bytes_per_ordinal()},
                                              FixedBufferEdgeStorage{.control = edge_control.value().view()});
  ASSERT_TRUE(created_edge.ok()) << created_edge.status();
  auto edge = std::move(created_edge).value();

  // Reserve both downstream credits before either Provider callback. The
  // ordinal-one reservation intentionally comes first, proving that detached
  // credit acquisition cannot dictate FIFO order at ordered publication.
  auto ordinal_one_edge_credit = edge->try_reserve(plan.charged_bytes_per_ordinal());
  ASSERT_TRUE(ordinal_one_edge_credit.ok()) << ordinal_one_edge_credit.status();
  auto ordinal_zero_edge_credit = edge->try_reserve(plan.charged_bytes_per_ordinal());
  ASSERT_TRUE(ordinal_zero_edge_credit.ok()) << ordinal_zero_edge_credit.status();
  EXPECT_EQ(2U, edge->snapshot().reserved_items);
  EXPECT_EQ(0U, edge->snapshot().queued_items);
  EXPECT_EQ(2U, edge->snapshot().occupied_items);
  EXPECT_EQ(2U * plan.charged_bytes_per_ordinal(), edge->snapshot().occupied_logical_bytes);

  auto ordinal_one_frame = complete_frame(*runtime.assembler, context(1U));
  ASSERT_TRUE(ordinal_one_frame.ok()) << ordinal_one_frame.status();
  auto ordinal_one_dispatch_result = runtime.ingress.try_prepare(ordinal_one_frame.value());
  ASSERT_TRUE(ordinal_one_dispatch_result.ok()) << ordinal_one_dispatch_result.status();
  auto ordinal_one_dispatch = std::move(ordinal_one_dispatch_result).value();
  ASSERT_TRUE(ordinal_one_dispatch.commit().ok());
  auto ordinal_one_handle = seal_handle(*pool, type_descriptor.value(), byte{0xD4U});
  ASSERT_TRUE(ordinal_one_handle.ok()) << ordinal_one_handle.status();
  ASSERT_TRUE(ordinal_one_dispatch.complete(ordinal_one_handle.value()).ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, ordinal_one_dispatch.try_acquire_publish().status().code());

  auto ordinal_zero_frame = complete_frame(*runtime.assembler, context(0U));
  ASSERT_TRUE(ordinal_zero_frame.ok()) << ordinal_zero_frame.status();
  auto ordinal_zero_dispatch_result = runtime.ingress.try_prepare(ordinal_zero_frame.value());
  ASSERT_TRUE(ordinal_zero_dispatch_result.ok()) << ordinal_zero_dispatch_result.status();
  auto ordinal_zero_dispatch = std::move(ordinal_zero_dispatch_result).value();
  ASSERT_TRUE(ordinal_zero_dispatch.commit().ok());
  auto ordinal_zero_handle = seal_handle(*pool, type_descriptor.value(), byte{0xC3U});
  ASSERT_TRUE(ordinal_zero_handle.ok()) << ordinal_zero_handle.status();
  ASSERT_TRUE(ordinal_zero_dispatch.complete(ordinal_zero_handle.value()).ok());
  EXPECT_EQ(1U, buffer.snapshot().retained_items);
  EXPECT_EQ(1U, buffer.snapshot().direct_head_items);
  EXPECT_EQ(0U, buffer.snapshot().free_slots);

  auto ordinal_zero_publish = ordinal_zero_dispatch.try_acquire_publish();
  ASSERT_TRUE(ordinal_zero_publish.ok()) << ordinal_zero_publish.status();
  ASSERT_TRUE(ordinal_zero_publish.value().commit_to_edge(ordinal_zero_edge_credit.value()).ok());
  EXPECT_FALSE(ordinal_zero_publish.value().valid());
  EXPECT_FALSE(ordinal_zero_edge_credit.value().valid());
  EXPECT_EQ(1U, edge->snapshot().reserved_items);
  EXPECT_EQ(1U, edge->snapshot().queued_items);
  EXPECT_EQ(1U, buffer.snapshot().retained_items);
  EXPECT_EQ(0U, buffer.snapshot().direct_head_items);

  auto ordinal_one_publish = ordinal_one_dispatch.try_acquire_publish();
  ASSERT_TRUE(ordinal_one_publish.ok()) << ordinal_one_publish.status();
  ASSERT_TRUE(ordinal_one_publish.value().commit_to_edge(ordinal_one_edge_credit.value()).ok());
  EXPECT_FALSE(ordinal_one_publish.value().valid());
  EXPECT_FALSE(ordinal_one_edge_credit.value().valid());
  EXPECT_EQ(0U, edge->snapshot().reserved_items);
  EXPECT_EQ(2U, edge->snapshot().queued_items);
  EXPECT_EQ(2U, edge->snapshot().occupied_items);

  auto first_edge_item = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, first_edge_item.kind);
  ASSERT_TRUE(first_edge_item.lease.has_value());
  const auto first_bytes = first_edge_item.lease->buffer().payload();
  ASSERT_TRUE(first_bytes.ok()) << first_bytes.status();
  ASSERT_EQ(1U, first_bytes.value().size());
  EXPECT_EQ(byte{0xC3U}, first_bytes.value()[0]);
  ASSERT_TRUE(first_edge_item.lease->acknowledge_consumed().ok());

  auto second_edge_item = edge->try_acquire();
  ASSERT_EQ(FixedBufferEdgePollKind::item, second_edge_item.kind);
  ASSERT_TRUE(second_edge_item.lease.has_value());
  const auto second_bytes = second_edge_item.lease->buffer().payload();
  ASSERT_TRUE(second_bytes.ok()) << second_bytes.status();
  ASSERT_EQ(1U, second_bytes.value().size());
  EXPECT_EQ(byte{0xD4U}, second_bytes.value()[0]);
  ASSERT_TRUE(second_edge_item.lease->acknowledge_consumed().ok());
  EXPECT_EQ(2U, pool->snapshot().free_slots);
  EXPECT_EQ(0U, edge->snapshot().occupied_items);
  EXPECT_EQ(2U, edge->snapshot().free_slots);

  ASSERT_TRUE(edge->end_of_input().ok());
  ASSERT_TRUE(runtime.ingress.end_of_input().ok());
  EXPECT_EQ(FixedBufferEdgePollKind::completed, edge->try_acquire().kind);
  EXPECT_EQ(FixedReorderBufferState::completed, buffer.snapshot().state);
  EXPECT_TRUE(ledger->snapshot().used.empty());
  edge.reset();
  pool.reset();
  EXPECT_TRUE(pool_ledger->snapshot().used.empty());
}

TEST(KSpaceJetFixedReorderBufferM37, FailedInternalEdgeCommitClosesReorderAndReleasesTheUntransferredHandle) {
  SemanticDomain domain{};
  domain.slice = 2U;
  auto artifacts = make_artifacts(domain, 1U, 8U);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> sidecars;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();
  auto type_descriptor = make_reorder_handle_type();
  ASSERT_TRUE(type_descriptor.ok()) << type_descriptor.status();
  HandlePoolSlabs pool_slabs;
  auto created_pool = make_handle_pool(type_descriptor.value(), pool_slabs, 1U);
  ASSERT_TRUE(created_pool.ok()) << created_pool.status();
  auto pool = std::move(created_pool).value();
  auto edge_control = make_edge_control(1U);
  ASSERT_TRUE(edge_control.ok()) << edge_control.status();
  auto created_edge = FixedBufferEdge::create(
    {.occupancy_ledger = nullptr, .source_pool = pool.get(), .max_items = 1U, .max_logical_bytes = 0U},
    FixedBufferEdgeStorage{.control = edge_control.value().view()});
  ASSERT_TRUE(created_edge.ok()) << created_edge.status();
  auto edge = std::move(created_edge).value();

  auto frame = complete_frame(*runtime.assembler, context(0U));
  ASSERT_TRUE(frame.ok()) << frame.status();
  auto dispatch_result = runtime.ingress.try_prepare(frame.value());
  ASSERT_TRUE(dispatch_result.ok()) << dispatch_result.status();
  auto dispatch = std::move(dispatch_result).value();
  ASSERT_TRUE(dispatch.commit().ok());
  auto handle = seal_handle(*pool, type_descriptor.value(), byte{0xE5U});
  ASSERT_TRUE(handle.ok()) << handle.status();
  ASSERT_TRUE(dispatch.complete(handle.value()).ok());
  auto publish = dispatch.try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  auto edge_reservation = edge->try_reserve(0U);
  ASSERT_TRUE(edge_reservation.ok()) << edge_reservation.status();
  EXPECT_FALSE(publish.value().commit_to_edge(edge_reservation.value()).ok());
  EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
  EXPECT_EQ(1U, pool->snapshot().free_slots);
}

TEST(KSpaceJetFixedReorderBufferM37, TypedPublishDirectAcknowledgementFailsClosedInsteadOfDroppingItsHandle) {
  SemanticDomain domain{};
  domain.slice = 2U;
  auto artifacts = make_artifacts(domain, 1U, 8U);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> sidecars;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();
  auto type_descriptor = make_reorder_handle_type();
  ASSERT_TRUE(type_descriptor.ok()) << type_descriptor.status();
  HandlePoolSlabs pool_slabs;
  auto created_pool = make_handle_pool(type_descriptor.value(), pool_slabs, 1U);
  ASSERT_TRUE(created_pool.ok()) << created_pool.status();
  auto pool = std::move(created_pool).value();

  auto frame = complete_frame(*runtime.assembler, context(0U));
  ASSERT_TRUE(frame.ok()) << frame.status();
  auto dispatch_result = runtime.ingress.try_prepare(frame.value());
  ASSERT_TRUE(dispatch_result.ok()) << dispatch_result.status();
  auto dispatch = std::move(dispatch_result).value();
  ASSERT_TRUE(dispatch.commit().ok());
  auto handle = seal_handle(*pool, type_descriptor.value(), byte{0xB7U});
  ASSERT_TRUE(handle.ok()) << handle.status();
  ASSERT_TRUE(dispatch.complete(handle.value()).ok());
  auto publish = dispatch.try_acquire_publish();
  ASSERT_TRUE(publish.ok()) << publish.status();
  ASSERT_TRUE(publish.value().has_buffer_handle());

  const auto acknowledged = publish.value().acknowledge_published();
  EXPECT_EQ(ksj::base::StatusCode::state_error, acknowledged.code());
  EXPECT_FALSE(publish.value().valid());
  EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
  EXPECT_EQ(1U, pool->snapshot().free_slots);
}

TEST(KSpaceJetFixedReorderBufferM37, DroppedOrGapFailedPublishReleasesItsRetainedHandleExactlyOnce) {
  {
    SemanticDomain domain{};
    domain.slice = 2U;
    auto artifacts = make_artifacts(domain, 1U, 8U);
    ASSERT_TRUE(artifacts.ok()) << artifacts.status();
    std::vector<byte> bookkeeping;
    std::vector<FixedReorderBufferHandleSidecar> sidecars;
    std::shared_ptr<ResourceVectorLedger> ledger;
    auto created_buffer = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, ledger);
    ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
    auto buffer = std::move(created_buffer).value();
    auto bound = bind_ingress(artifacts.value(), buffer);
    ASSERT_TRUE(bound.ok()) << bound.status();
    auto runtime = std::move(bound).value();
    auto type_descriptor = make_reorder_handle_type();
    ASSERT_TRUE(type_descriptor.ok()) << type_descriptor.status();
    HandlePoolSlabs pool_slabs;
    auto created_pool = make_handle_pool(type_descriptor.value(), pool_slabs, 1U);
    ASSERT_TRUE(created_pool.ok()) << created_pool.status();
    auto pool = std::move(created_pool).value();

    auto frame = complete_frame(*runtime.assembler, context(0U));
    ASSERT_TRUE(frame.ok()) << frame.status();
    auto dispatch_result = runtime.ingress.try_prepare(frame.value());
    ASSERT_TRUE(dispatch_result.ok()) << dispatch_result.status();
    auto dispatch = std::move(dispatch_result).value();
    ASSERT_TRUE(dispatch.commit().ok());
    auto handle = seal_handle(*pool, type_descriptor.value(), byte{0x6DU});
    ASSERT_TRUE(handle.ok()) << handle.status();
    ASSERT_TRUE(dispatch.complete(handle.value()).ok());
    {
      auto publish = dispatch.try_acquire_publish();
      ASSERT_TRUE(publish.ok()) << publish.status();
      EXPECT_TRUE(publish.value().has_buffer_handle());
    }
    EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
    EXPECT_EQ(1U, pool->snapshot().free_slots);
  }

  {
    SemanticDomain domain{};
    domain.slice = 2U;
    auto artifacts = make_artifacts(domain, 1U, 8U);
    ASSERT_TRUE(artifacts.ok()) << artifacts.status();
    std::vector<byte> bookkeeping;
    std::vector<FixedReorderBufferHandleSidecar> sidecars;
    std::shared_ptr<ResourceVectorLedger> ledger;
    auto created_buffer = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, ledger);
    ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
    auto buffer = std::move(created_buffer).value();
    auto bound = bind_ingress(artifacts.value(), buffer);
    ASSERT_TRUE(bound.ok()) << bound.status();
    auto runtime = std::move(bound).value();
    auto type_descriptor = make_reorder_handle_type();
    ASSERT_TRUE(type_descriptor.ok()) << type_descriptor.status();
    HandlePoolSlabs pool_slabs;
    auto created_pool = make_handle_pool(type_descriptor.value(), pool_slabs, 1U);
    ASSERT_TRUE(created_pool.ok()) << created_pool.status();
    auto pool = std::move(created_pool).value();

    auto frame = complete_frame(*runtime.assembler, context(1U));
    ASSERT_TRUE(frame.ok()) << frame.status();
    {
      auto dispatch_result = runtime.ingress.try_prepare(frame.value());
      ASSERT_TRUE(dispatch_result.ok()) << dispatch_result.status();
      auto dispatch = std::move(dispatch_result).value();
      ASSERT_TRUE(dispatch.commit().ok());
      auto handle = seal_handle(*pool, type_descriptor.value(), byte{0x73U});
      ASSERT_TRUE(handle.ok()) << handle.status();
      ASSERT_TRUE(dispatch.complete(handle.value()).ok());
      EXPECT_EQ(ksj::base::StatusCode::validation_error, runtime.ingress.end_of_input().code());
      EXPECT_EQ(FixedReorderBufferState::failed_draining, buffer.snapshot().state);
    }
    EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
    EXPECT_EQ(1U, pool->snapshot().free_slots);
  }
}

TEST(KSpaceJetFixedReorderBufferM37, ExplicitIngressAbortReleasesACompletedDirectHeadHandle) {
  SemanticDomain domain{};
  domain.slice = 2U;
  auto artifacts = make_artifacts(domain, 1U, 8U);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  std::vector<byte> bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> sidecars;
  std::shared_ptr<ResourceVectorLedger> ledger;
  auto created_buffer = make_typed_handle_buffer(artifacts.value(), bookkeeping, sidecars, ledger);
  ASSERT_TRUE(created_buffer.ok()) << created_buffer.status();
  auto buffer = std::move(created_buffer).value();
  auto bound = bind_ingress(artifacts.value(), buffer);
  ASSERT_TRUE(bound.ok()) << bound.status();
  auto runtime = std::move(bound).value();
  auto type_descriptor = make_reorder_handle_type();
  ASSERT_TRUE(type_descriptor.ok()) << type_descriptor.status();
  HandlePoolSlabs pool_slabs;
  auto created_pool = make_handle_pool(type_descriptor.value(), pool_slabs, 1U);
  ASSERT_TRUE(created_pool.ok()) << created_pool.status();
  auto pool = std::move(created_pool).value();

  auto frame = complete_frame(*runtime.assembler, context(0U));
  ASSERT_TRUE(frame.ok()) << frame.status();
  auto dispatch_result = runtime.ingress.try_prepare(frame.value());
  ASSERT_TRUE(dispatch_result.ok()) << dispatch_result.status();
  auto dispatch = std::move(dispatch_result).value();
  ASSERT_TRUE(dispatch.commit().ok());
  auto handle = seal_handle(*pool, type_descriptor.value(), byte{0xA7U});
  ASSERT_TRUE(handle.ok()) << handle.status();
  ASSERT_TRUE(dispatch.complete(handle.value()).ok());
  ASSERT_TRUE(runtime.ingress.abort().ok());
  EXPECT_EQ(FixedReorderBufferState::failed_draining, buffer.snapshot().state);
  dispatch = ksj::recon::runtime::FrameDispatch{};
  EXPECT_EQ(FixedReorderBufferState::failed, buffer.snapshot().state);
  EXPECT_EQ(1U, pool->snapshot().free_slots);
}

} // namespace
