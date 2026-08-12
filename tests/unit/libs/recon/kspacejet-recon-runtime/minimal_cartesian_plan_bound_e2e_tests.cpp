#include "kspacejet/recon/runtime/plan_bound_data_plane.hpp"

#include "kspacejet/provider/loader/provider_loader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ksj::base::byte;
using ksj::base::Result;
using ksj::base::Status;
using ksj::provider::loader::ProviderLease;
using ksj::provider::loader::ProviderModule;
using ksj::recon::AdmissionOutcome;
using ksj::recon::AdmissionRecord;
using ksj::recon::ArtifactDigest;
using ksj::recon::BufferPoolPlanSpec;
using ksj::recon::DataEdgePlanSpec;
using ksj::recon::DenseCartesianOrdinalDimensionSpec;
using ksj::recon::DenseKeySlotDimensionSpec;
using ksj::recon::ElementType;
using ksj::recon::ExecutionPlan;
using ksj::recon::ExecutionPlanSpec;
using ksj::recon::ExecutionProfile;
using ksj::recon::KeySlotTablePlanSpec;
using ksj::recon::PayloadKind;
using ksj::recon::PayloadMutability;
using ksj::recon::Quantity;
using ksj::recon::ReorderPlanSpec;
using ksj::recon::ResourceVector;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::StrideKind;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::VerificationRecord;
using ksj::recon::VerificationRecordSpec;
using ksj::recon::runtime::AdmittedPlanBoundDataPlane;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::CartesianLineCoordinate;
using ksj::recon::runtime::CompletedFrameLease;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FixedBufferEdgePollKind;
using ksj::recon::runtime::FixedReorderBufferHandleSidecar;
using ksj::recon::runtime::FixedReorderBufferStorage;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::PlanBoundDataPlaneStorage;
using ksj::recon::runtime::PlanBoundReorderFiringRequest;
using ksj::recon::runtime::PlanBoundSynchronousFiringConfig;
using ksj::recon::runtime::ResourceVectorLedger;
using ksj::recon::runtime::ResourceVectorLedgerReservation;
using ksj::recon::runtime::SynchronousFiringOutcome;
using ksj::recon::runtime::SynchronousProviderInvocation;

constexpr std::string_view kPlanDigest = "sha256:1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::string_view kVerificationDigest =
  "sha256:2222222222222222222222222222222222222222222222222222222222222222";
constexpr std::string_view kTargetDigest = "sha256:3333333333333333333333333333333333333333333333333333333333333333";
constexpr std::string_view kMachineDigest = "sha256:4444444444444444444444444444444444444444444444444444444444444444";
constexpr std::string_view kProviderBundleDigest =
  "sha256:6fc3fb5999f676e000ece47b99e8048d8e9098d5f2ad05f86b9c81b18e75957f";
constexpr std::string_view kProviderContractDigest =
  "sha256:c42136027e84e0e476a879ef8e765d7c59fba1a72112384be3ee33b767f1da1f";
constexpr std::string_view kImageAbiDigest = "sha256:bc161b76c25315236dd5d01fc766635200c1033b7b795bb629d625746f843cbe";
constexpr std::string_view kImagePayloadDigest =
  "sha256:42fb021252293d7b2d5ba913d75a89d4c868e72c6a6c559dde6243d6b0c780fb";
constexpr std::string_view kImageMetadataDigest =
  "sha256:3f9bbd8144c338693445519780fb102144091b34c1bdf0d76ca529e7f453516b";
constexpr char kProviderId[] = "org.kspacejet.minimal.cartesian";
constexpr char kOperatorId[] = "cartesian_ifft2_single_coil";
constexpr char kCanonicalConfig[] = "{\"cols\":2,\"rows\":2}";
constexpr Quantity kOrdinalDomain = 2U;
constexpr Quantity kPoolSlots = 2U;
constexpr Quantity kImagePayloadBytes = 4U * sizeof(float);
// The plan's fixed mutable slot must meet the frozen 64-byte alignment
// granularity. The Provider still seals only its real 2x2 float image size.
constexpr Quantity kPoolPayloadCapacity = 64U;
constexpr Quantity kScratchBytes = 4U * 2U * sizeof(float);
// The frozen reorder window counts only ordinal > next_expected. This E2E
// deliberately holds ordinal 1 before ordinal 0, so one future slot plus the
// direct current-head capability is sufficient.
constexpr Quantity kMaxAheadItems = 1U;

[[nodiscard]] Result<ArtifactDigest> parse_digest(const std::string_view value) {
  return ArtifactDigest::parse(value, "minimal Cartesian plan-bound E2E test digest");
}

[[nodiscard]] ResourceVectorSpec resource_vector_spec(const ResourceVector& resources) {
  ResourceVectorSpec result{
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
  result.devices.reserve(resources.devices().size());
  for (const auto& device : resources.devices()) {
    result.devices.push_back({.device_id = device.device_id(),
                              .device_bytes = device.device_bytes(),
                              .gpu_stream_slots = device.gpu_stream_slots(),
                              .copy_engine_slots = device.copy_engine_slots()});
  }
  return result;
}

struct PlanArtifacts final {
  ExecutionPlan plan;
  VerificationRecord verification;
  AdmissionRecord admission;
};

[[nodiscard]] Result<PlanArtifacts> make_artifacts() {
  auto image_type = TypeDescriptor::create({.type_id = "ksj.image-frame",
                                            .revision = 1U,
                                            .abi_descriptor_digest = std::string(kImageAbiDigest),
                                            .payload_schema_digest = std::string(kImagePayloadDigest),
                                            .payload_kind = PayloadKind::buffer_handle,
                                            .element_type = ElementType::float32,
                                            .rank = 2U,
                                            .dimensions = {"ky", "kx"},
                                            .layout = ksj::recon::LayoutKind::row_major_contiguous,
                                            .strides = StrideKind::canonical,
                                            .explicit_byte_strides = {},
                                            .allowed_memory_domains = {TypeMemoryDomain::host_normal},
                                            .min_alignment_bytes = 64U,
                                            .mutability = PayloadMutability::immutable_after_publish,
                                            .metadata_schema_digest = std::string(kImageMetadataDigest)},
                                           "minimal Cartesian E2E output type");
  if (!image_type.ok()) {
    return image_type.status();
  }
  auto key_charge = ksj::recon::dense_key_slot_host_metadata_charged_bytes(kOrdinalDomain, kOrdinalDomain,
                                                                           "minimal Cartesian E2E key metadata");
  if (!key_charge.ok()) {
    return key_charge.status();
  }
  auto reorder_charge = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
    kOrdinalDomain, kMaxAheadItems, "minimal Cartesian E2E reorder metadata");
  if (!reorder_charge.ok()) {
    return reorder_charge.status();
  }
  auto pool_charge =
    ksj::recon::m37_buffer_pool_host_metadata_charged_bytes(kPoolSlots, "minimal Cartesian E2E pool metadata");
  if (!pool_charge.ok()) {
    return pool_charge.status();
  }
  auto pool_physical = ksj::recon::m37_buffer_pool_physical_charge_bytes(kPoolSlots, kPoolPayloadCapacity, 0U,
                                                                         "minimal Cartesian E2E pool physical");
  if (!pool_physical.ok()) {
    return pool_physical.status();
  }
  auto edge_charge =
    ksj::recon::m37_data_edge_host_metadata_charged_bytes(kPoolSlots, "minimal Cartesian E2E edge metadata");
  if (!edge_charge.ok()) {
    return edge_charge.status();
  }
  auto plan_digest = parse_digest(kPlanDigest);
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }

  ExecutionPlanSpec specification;
  specification.inputs = {
    .resolved_pipeline = std::string(kPlanDigest),
    .scan_descriptor = std::string(kVerificationDigest),
    .target_envelope = std::string(kTargetDigest),
    .machine_policy = std::string(kMachineDigest),
    .provider_contracts = {std::string(kProviderContractDigest)},
  };
  specification.execution_profile = ExecutionProfile::bounded_online;
  specification.key_slot_tables = {
    KeySlotTablePlanSpec{
      .node_id = "reconstruct",
      .dense_dimensions = {DenseKeySlotDimensionSpec{.field = "slice", .minimum = 0U, .cardinality = kOrdinalDomain}},
      .key_domain_bound = kOrdinalDomain,
      .max_distinct_keys = kOrdinalDomain,
      .max_live_keys = kOrdinalDomain,
      .slot_count = kOrdinalDomain,
      .host_metadata_charged_bytes = key_charge.value(),
      .max_items_per_activation = 1U,
      .max_charged_bytes_per_activation = kPoolPayloadCapacity,
    },
  };
  specification.reorder_plans = {
    ReorderPlanSpec{
      .node_id = "reconstruct",
      .order_domain_id = "reconstruct",
      .ordinal_binding_id = std::string(ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
      .completed_frame_input_port = "frame",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .charged_bytes_per_ordinal = kPoolPayloadCapacity,
      .ordinal_dimensions =
        {
          DenseCartesianOrdinalDimensionSpec{.field = "slice", .minimum = 0U, .cardinality = kOrdinalDomain},
        },
      .mapping_algorithm_id = std::string(ksj::recon::kDenseCartesianReorderMappingAlgorithmId),
      .storage_accounting_id = std::string(ksj::recon::kDenseCartesianReorderStorageAccountingId),
      .ordinal_domain_bound = kOrdinalDomain,
      .first_expected_ordinal = ksj::recon::kFirstExpectedReorderOrdinal,
      .last_expected_ordinal = kOrdinalDomain - 1U,
      .max_ahead_items = kMaxAheadItems,
      .max_ahead_charged_bytes = kMaxAheadItems * kPoolPayloadCapacity,
      .max_gap_ordinals = kOrdinalDomain - 1U,
      .occurrence_policy = std::string(ksj::recon::kStrictDenseAllTuplesReorderOccurrencePolicy),
      .publish_policy = std::string(ksj::recon::kNextExpectedOnlyReorderPublishPolicy),
      .certified_skipped_ordinals = {},
      .end_of_input_policy = std::string(ksj::recon::kFailReorderEndOfInputPolicy),
      .handle_storage_charged_bytes = kMaxAheadItems * ksj::recon::kDenseCartesianReorderHandleSidecarChargedBytes,
      .host_metadata_charged_bytes = reorder_charge.value(),
      .descriptor_charged_count = kMaxAheadItems,
    },
  };
  specification.buffer_pool_plans = {
    BufferPoolPlanSpec{
      .pool_id = "minimal-cartesian.image.pool",
      .producer_node_id = "reconstruct",
      .producer_port_name = "image",
      .producer_provider_id = kProviderId,
      .producer_bundle_digest = std::string(kProviderBundleDigest),
      .producer_operator_id = kOperatorId,
      .producer_contract_digest = std::string(kProviderContractDigest),
      .type_descriptor = image_type.value(),
      .memory_domain = TypeMemoryDomain::host_normal,
      .slot_count = kPoolSlots,
      .payload_capacity_bytes = kPoolPayloadCapacity,
      .metadata_capacity_bytes = 0U,
      .payload_alignment_bytes = 64U,
      .storage_accounting_id = std::string(ksj::recon::kM37BufferPoolStorageAccountingId),
      .host_metadata_charged_bytes = pool_charge.value(),
      .descriptor_charged_count = kPoolSlots,
      .physical_charge_bytes = pool_physical.value(),
    },
  };
  specification.data_edge_plans = {
    DataEdgePlanSpec{
      .edge_id = "minimal-cartesian.image.edge",
      .source_pool_id = "minimal-cartesian.image.pool",
      .producer_node_id = "reconstruct",
      .producer_port_name = "image",
      .producer_abi_port = 0U,
      .consumer_node_id = "image_sink",
      .consumer_port_name = "image",
      .type_descriptor = image_type.value(),
      .max_items = kPoolSlots,
      .max_logical_bytes = kPoolSlots * kPoolPayloadCapacity,
      .storage_accounting_id = std::string(ksj::recon::kM37DataEdgeStorageAccountingId),
      .host_metadata_charged_bytes = edge_charge.value(),
      .descriptor_charged_count = kPoolSlots,
      .firing_lease_staging_charged_bytes = ksj::recon::kM37FiringLeaseHostStagingChargedBytes,
      .firing_lease_staging_descriptor_count = ksj::recon::kM37FiringLeaseHostStagingDescriptorCount,
      .terminal_policy = std::string(ksj::recon::kM37NormalEoiDrainCancellationFailTerminalPolicy),
    },
  };
  specification.resource_vector = {
    .host_normal_bytes = key_charge.value() + reorder_charge.value() + pool_physical.value() + edge_charge.value() +
                         ksj::recon::kM37FiringLeaseHostStagingChargedBytes + kScratchBytes,
    .descriptor_count =
      kMaxAheadItems + kPoolSlots + kPoolSlots + ksj::recon::kM37FiringLeaseHostStagingDescriptorCount,
    .cpu_leaf_permits = 1U,
  };
  specification.terminal_occurrences = kOrdinalDomain;
  specification.proof_obligations = {
    std::string(ksj::recon::kM3CompletedFrameSlotBindingProofObligation),
    std::string(ksj::recon::kM3StrictDenseAllTuplesEoiRuntimeAssumption),
    std::string(ksj::recon::kM37PlanBoundDataPlaneProofObligation),
    std::string(ksj::recon::kM37SinglePhysicalPayloadChargeRuntimeAssumption),
  };
  auto plan = ExecutionPlan::create(std::move(plan_digest).value(), specification);
  if (!plan.ok()) {
    return plan.status();
  }
  auto verification_digest = parse_digest(kVerificationDigest);
  if (!verification_digest.ok()) {
    return verification_digest.status();
  }
  auto verification =
    VerificationRecord::create(std::move(verification_digest).value(),
                               VerificationRecordSpec{
                                 .execution_plan_digest = plan.value().digest().value(),
                                 .execution_profile = plan.value().execution_profile(),
                                 .verified_resource_vector = resource_vector_spec(plan.value().resources()),
                                 .verified_terminal_occurrences = plan.value().terminal_occurrences(),
                                 .verified_obligations =
                                   {
                                     std::string(ksj::recon::kM3CompletedFrameSlotBindingVerificationObligation),
                                     std::string(ksj::recon::kM3StrictDenseAllTuplesEoiVerificationObligation),
                                     std::string(ksj::recon::kM37PlanBoundDataPlaneVerificationObligation),
                                     std::string(ksj::recon::kM37SinglePhysicalPayloadChargeVerificationObligation),
                                   },
                               });
  if (!verification.ok()) {
    return verification.status();
  }
  auto admission = AdmissionRecord::create({.execution_plan_digest = plan.value().digest().value(),
                                            .verification_record_digest = verification.value().digest().value(),
                                            .outcome = AdmissionOutcome::admitted,
                                            .reservation = resource_vector_spec(plan.value().resources())});
  if (!admission.ok()) {
    return admission.status();
  }
  return PlanArtifacts{std::move(plan).value(), std::move(verification).value(), std::move(admission).value()};
}

class AlignedBytes final {
public:
  AlignedBytes() = default;

  explicit AlignedBytes(const std::size_t size, const std::size_t alignment = 64U)
      : size_(size), alignment_(alignment) {
    if (size_ != 0U) {
      data_ = static_cast<byte*>(::operator new(size_, std::align_val_t{alignment_}));
    }
  }

  ~AlignedBytes() {
    if (data_ != nullptr) {
      ::operator delete(data_, std::align_val_t{alignment_});
    }
  }

  AlignedBytes(const AlignedBytes&) = delete;
  AlignedBytes& operator=(const AlignedBytes&) = delete;

  AlignedBytes(AlignedBytes&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0U)),
        alignment_(std::exchange(other.alignment_, 64U)) {}

  AlignedBytes& operator=(AlignedBytes&& other) noexcept {
    if (this != &other) {
      if (data_ != nullptr) {
        ::operator delete(data_, std::align_val_t{alignment_});
      }
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0U);
      alignment_ = std::exchange(other.alignment_, 64U);
    }
    return *this;
  }

  [[nodiscard]] byte* data() noexcept { return data_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] ksj::base::ByteSpan view() noexcept { return {data_, size_}; }

private:
  byte* data_{nullptr};
  std::size_t size_{0U};
  std::size_t alignment_{64U};
};

struct RuntimeSlabs final {
  AlignedBytes pool_payload;
  AlignedBytes pool_metadata;
  AlignedBytes pool_control;
  AlignedBytes edge_control;
  AlignedBytes reorder_bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> reorder_sidecars;

  [[nodiscard]] static Result<RuntimeSlabs> create(const ExecutionPlan& plan) {
    const auto& pool = plan.buffer_pool_plans().front();
    const auto& edge = plan.data_edge_plans().front();
    const auto& reorder = plan.reorder_plans().front();
    const auto pool_control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(pool.slot_count());
    if (!pool_control.ok()) {
      return pool_control.status();
    }
    const auto edge_control = ksj::recon::runtime::fixed_buffer_edge_required_control_storage_bytes(edge.max_items());
    if (!edge_control.ok()) {
      return edge_control.status();
    }
    const auto reorder_bookkeeping =
      ksj::recon::runtime::fixed_reorder_buffer_required_bookkeeping_storage_bytes(reorder);
    if (!reorder_bookkeeping.ok()) {
      return reorder_bookkeeping.status();
    }
    try {
      RuntimeSlabs slabs;
      slabs.pool_payload = AlignedBytes{static_cast<std::size_t>(pool.slot_count() * pool.payload_capacity_bytes())};
      slabs.pool_metadata = AlignedBytes{static_cast<std::size_t>(pool.slot_count() * pool.metadata_capacity_bytes())};
      slabs.pool_control = AlignedBytes{pool_control.value()};
      slabs.edge_control = AlignedBytes{edge_control.value()};
      slabs.reorder_bookkeeping = AlignedBytes{reorder_bookkeeping.value()};
      slabs.reorder_sidecars.resize(static_cast<std::size_t>(reorder.max_ahead_items()));
      return slabs;
    } catch (const std::bad_alloc&) {
      return Status::OutOfMemory("unable to allocate minimal Cartesian E2E caller slabs");
    }
  }

  [[nodiscard]] PlanBoundDataPlaneStorage data_plane_storage() noexcept {
    return {.pool = {.payload = pool_payload.view(), .metadata = pool_metadata.view(), .control = pool_control.view()},
            .edge = {.control = edge_control.view()}};
  }

  [[nodiscard]] FixedReorderBufferStorage reorder_storage() noexcept {
    return {.bookkeeping = reorder_bookkeeping.view(),
            .handle_sidecars =
              std::span<FixedReorderBufferHandleSidecar>{reorder_sidecars.data(), reorder_sidecars.size()}};
  }
};

struct GlobalAdmission final {
  std::shared_ptr<ResourceVectorLedger> ledger;
  ResourceVectorLedgerReservation reservation;
};

[[nodiscard]] Result<GlobalAdmission> reserve_global_admission(const PlanArtifacts& artifacts) {
  auto capacity =
    ResourceVectorCapacity::create({.domains = resource_vector_spec(artifacts.plan.resources()),
                                    .host_total_cap_bytes = artifacts.plan.resources().host_total_bytes()},
                                   "minimal Cartesian E2E global admission ledger");
  if (!capacity.ok()) {
    return capacity.status();
  }
  try {
    auto ledger = std::make_shared<ResourceVectorLedger>(std::move(capacity).value());
    auto reservation = ledger->try_reserve(artifacts.plan.resources());
    if (!reservation.ok()) {
      return reservation.status();
    }
    const auto committed = reservation.value().commit();
    if (!committed.ok()) {
      return committed;
    }
    return GlobalAdmission{std::move(ledger), std::move(reservation).value()};
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory("unable to allocate minimal Cartesian E2E global admission ledger");
  }
}

[[nodiscard]] CartesianFrameSlotConfig frame_slot_config(const std::uint32_t slot_id) {
  return {
    .slot_id = slot_id,
    .dimensions =
      {.readout_samples = 2U, .phase_encode_1 = 2U, .phase_encode_2 = 1U, .channels = 1U, .bytes_per_sample = 4U},
    .completion = {.required_indices = {{.phase_encode_1 = 0U, .phase_encode_2 = 0U},
                                        {.phase_encode_1 = 1U, .phase_encode_2 = 0U}}},
    .resource_upper_bound = {.max_total_arrivals = 2U, .max_duplicate_arrivals = 0U, .max_payload_bytes = 8U},
    .duplicate_policy = DuplicateAcquisitionPolicy::reject,
    .incomplete_policy = IncompleteFramePolicy::fail,
  };
}

[[nodiscard]] Result<std::unique_ptr<HostFrameAssembler>> make_host(const PlanArtifacts& artifacts) {
  HostFrameAssemblerConfig configuration{.scan_instance_id = "minimal-cartesian-plan-bound-e2e"};
  // HostFrameAssembler reserves one source head slot beyond the one frozen
  // future reorder slot.
  configuration.frame_slots = {frame_slot_config(1U), frame_slot_config(2U)};
  return HostFrameAssembler::create(artifacts.plan, artifacts.verification, "reconstruct", std::move(configuration));
}

struct Runtime final {
  Runtime(PlanArtifacts artifacts_value, std::shared_ptr<ResourceVectorLedger> global_ledger_value,
          RuntimeSlabs slabs_value, std::unique_ptr<HostFrameAssembler> host_value,
          std::unique_ptr<AdmittedPlanBoundDataPlane> plane_value, AlignedBytes scratch_value) noexcept
      : artifacts(std::move(artifacts_value)), global_ledger(std::move(global_ledger_value)),
        slabs(std::move(slabs_value)), host(std::move(host_value)), plane(std::move(plane_value)),
        scratch(std::move(scratch_value)) {}

  // Keep all raw slabs and the source assembler alive through data-plane
  // destruction, and keep the global ledger alive through its admission token.
  PlanArtifacts artifacts;
  std::shared_ptr<ResourceVectorLedger> global_ledger;
  RuntimeSlabs slabs;
  std::unique_ptr<HostFrameAssembler> host;
  std::unique_ptr<AdmittedPlanBoundDataPlane> plane;
  AlignedBytes scratch;
};

[[nodiscard]] Result<std::unique_ptr<Runtime>> make_runtime() {
  auto artifacts_result = make_artifacts();
  if (!artifacts_result.ok()) {
    return artifacts_result.status();
  }
  auto artifacts = std::move(artifacts_result).value();
  auto slabs = RuntimeSlabs::create(artifacts.plan);
  if (!slabs.ok()) {
    return slabs.status();
  }
  auto host = make_host(artifacts);
  if (!host.ok()) {
    return host.status();
  }
  auto admission_result = reserve_global_admission(artifacts);
  if (!admission_result.ok()) {
    return admission_result.status();
  }
  auto admission = std::move(admission_result).value();
  auto plane = AdmittedPlanBoundDataPlane::create(artifacts.plan, artifacts.verification, artifacts.admission,
                                                  std::move(admission.reservation), slabs.value().data_plane_storage());
  if (!plane.ok()) {
    return plane.status();
  }
  try {
    return std::make_unique<Runtime>(std::move(artifacts), std::move(admission.ledger), std::move(slabs).value(),
                                     std::move(host).value(), std::move(plane).value(), AlignedBytes{kScratchBytes});
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory("unable to allocate minimal Cartesian E2E runtime");
  }
}

[[nodiscard]] FrameSlotContext frame_context(const std::uint16_t slice) {
  FrameSlotContext value;
  value.semantic_key.slice = slice;
  value.order_key = slice;
  value.placement_key = slice;
  return value;
}

[[nodiscard]] Result<CompletedFrameLease> complete_frame(HostFrameAssembler& host, const std::uint16_t slice,
                                                         const std::int16_t dc_amplitude) {
  auto assembling = host.try_begin_frame(frame_context(slice));
  if (!assembling.ok()) {
    return assembling.status();
  }
  std::array<std::int16_t, 8U> samples{dc_amplitude, 0, 0, 0, 0, 0, 0, 0};
  const auto* const bytes = reinterpret_cast<const byte*>(samples.data());
  auto frame = std::move(assembling).value();
  const auto first = frame.scatter(CartesianLineCoordinate{.phase_encode_1 = 0U, .phase_encode_2 = 0U}, {bytes, 8U});
  if (!first.ok()) {
    return first;
  }
  const auto second =
    frame.scatter(CartesianLineCoordinate{.phase_encode_1 = 1U, .phase_encode_2 = 0U}, {bytes + 8U, 8U});
  if (!second.ok()) {
    return second;
  }
  return frame.seal_complete();
}

[[nodiscard]] Result<ResourceVector> firing_reservation() {
  return ResourceVector::create({.host_normal_bytes = kScratchBytes, .cpu_leaf_permits = 1U},
                                "minimal Cartesian E2E firing reservation");
}

[[nodiscard]] Result<ksj::recon::runtime::PlanBoundSynchronousOutputBridge>
make_bridge(AdmittedPlanBoundDataPlane& plane) {
  auto reservation = firing_reservation();
  if (!reservation.ok()) {
    return reservation.status();
  }
  return plane.create_synchronous_one_output_bridge({.firing_reservation = std::move(reservation).value(),
                                                     .maximum_input_batches = 1U,
                                                     .maximum_input_items = 1U,
                                                     .maximum_input_payload_bytes = 16U,
                                                     .maximum_scratch_bytes = kScratchBytes,
                                                     .maximum_metadata_bytes = 64U});
}

[[nodiscard]] PlanBoundReorderFiringRequest firing_request(const std::uint64_t occurrence, AlignedBytes& scratch) {
  return {.resource_occurrence_id = occurrence,
          .slot_generation = occurrence + 1U,
          .terminal_epoch = 77U,
          .scratch = scratch.view()};
}

[[nodiscard]] ksj_provider_abi_header abi_header(const std::uint32_t size,
                                                 const std::uint64_t capabilities = 0U) noexcept {
  return ksj_provider_abi_header_make(size, capabilities);
}

[[nodiscard]] ksj_utf8_view text(const char* const data, const std::uint64_t size) noexcept {
  ksj_utf8_view result{};
  result.abi = abi_header(sizeof(result));
  result.data = data;
  result.size = size;
  return result;
}

[[nodiscard]] ksj_error_view error_storage() noexcept {
  ksj_error_view result{};
  result.abi = abi_header(sizeof(result));
  result.message.abi = abi_header(sizeof(result.message));
  return result;
}

[[nodiscard]] ksj_digest256 digest(const std::uint8_t seed) noexcept {
  ksj_digest256 result{};
  result.abi = abi_header(sizeof(result));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    result.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

struct ProviderInstance final {
  ProviderModule module{};
  ProviderLease lease{};
  ksj_provider_operator* operator_handle{nullptr};
  ksj_execution_context* execution_context{nullptr};
  ksj_key_state* key_state{nullptr};

  ProviderInstance() = default;
  ProviderInstance(const ProviderInstance&) = delete;
  ProviderInstance& operator=(const ProviderInstance&) = delete;
  ProviderInstance(ProviderInstance&&) = delete;
  ProviderInstance& operator=(ProviderInstance&&) = delete;

  ~ProviderInstance() {
    if (!lease.valid() || lease.api() == nullptr) {
      return;
    }
    const auto* const api = lease.api();
    if (key_state != nullptr) {
      api->key_state_reset(operator_handle, execution_context, key_state);
    }
    if (execution_context != nullptr) {
      api->execution_context_destroy(operator_handle, execution_context);
    }
    if (operator_handle != nullptr) {
      api->operator_destroy(operator_handle);
    }
  }

  [[nodiscard]] SynchronousProviderInvocation invocation() const {
    return {.provider = lease,
            .operator_id = kOperatorId,
            .operator_handle = operator_handle,
            .execution_context = execution_context,
            .key_state = key_state};
  }
};

[[nodiscard]] Status initialize_provider(ProviderInstance& instance) {
  auto loaded = ProviderModule::load(std::filesystem::path(KSJ_MINIMAL_CARTESIAN_PROVIDER_MODULE));
  if (!loaded.ok()) {
    return loaded.status();
  }
  instance.module = std::move(loaded).value();
  instance.lease = instance.module.acquire();
  const auto* const descriptor = instance.lease.descriptor();
  const auto* const api = instance.lease.api();
  if (descriptor == nullptr || api == nullptr || descriptor->provider_id != kProviderId) {
    return Status::InternalError("minimal Cartesian E2E Provider has no expected descriptor/API");
  }
  const auto operator_descriptor =
    std::find_if(descriptor->operators.begin(), descriptor->operators.end(), [](const auto& candidate) {
      return candidate.operator_id == kOperatorId;
    });
  if (operator_descriptor == descriptor->operators.end()) {
    return Status::NotFound("minimal Cartesian E2E Provider has no expected operator");
  }

  ksj_operator_create_request create{};
  create.abi = abi_header(sizeof(create));
  create.operator_id = text(kOperatorId, sizeof(kOperatorId) - 1U);
  create.required_contract_digest.abi = abi_header(sizeof(create.required_contract_digest));
  std::copy(operator_descriptor->contract_digest.begin(), operator_descriptor->contract_digest.end(),
            create.required_contract_digest.bytes);
  create.canonical_config.abi = abi_header(sizeof(create.canonical_config));
  create.canonical_config.data = kCanonicalConfig;
  create.canonical_config.size = sizeof(kCanonicalConfig) - 1U;
  auto error = error_storage();
  if (api->operator_create(&create, &instance.operator_handle, &error) != KSJ_STATUS_OK ||
      instance.operator_handle == nullptr) {
    return Status::ValidationError("minimal Cartesian E2E Provider rejected operator creation");
  }

  ksj_execution_context_descriptor context{};
  context.abi = abi_header(sizeof(context));
  context.execution_context_id = 1U;
  context.max_backend_concurrency = 1U;
  error = error_storage();
  if (api->execution_context_create(instance.operator_handle, &context, &instance.execution_context, &error) !=
        KSJ_STATUS_OK ||
      instance.execution_context == nullptr) {
    return Status::ValidationError("minimal Cartesian E2E Provider rejected execution context");
  }

  ksj_key_state_descriptor key{};
  key.abi = abi_header(sizeof(key));
  key.semantic_key.abi = abi_header(sizeof(key.semantic_key));
  key.key_state_generation = 1U;
  error = error_storage();
  if (api->key_state_init(instance.operator_handle, instance.execution_context, &key, &instance.key_state, &error) !=
        KSJ_STATUS_OK ||
      instance.key_state == nullptr) {
    return Status::ValidationError("minimal Cartesian E2E Provider rejected key state");
  }

  ksj_scan_start_descriptor start{};
  start.abi = abi_header(sizeof(start));
  start.run_id = text("run", 3U);
  start.scan_id = text("scan", 4U);
  start.normalized_scan_facts_digest = digest(0x41U);
  start.execution_plan_digest = digest(0x51U);
  start.terminal_epoch = 77U;
  error = error_storage();
  if (api->operator_on_start(instance.operator_handle, instance.execution_context, instance.key_state, &start,
                             &error) != KSJ_STATUS_OK) {
    return Status::ValidationError("minimal Cartesian E2E Provider rejected scan start");
  }
  return Status::Ok();
}

void expect_magnitude_image(const ksj::recon::runtime::ImmutableBufferHandle& handle, const float expected) {
  const auto payload = handle.payload();
  ASSERT_TRUE(payload.ok()) << payload.status();
  ASSERT_EQ(kImagePayloadBytes, payload.value().size());
  std::array<float, 4U> image{};
  std::memcpy(image.data(), payload.value().data(), payload.value().size());
  for (const float pixel : image) {
    EXPECT_NEAR(expected, pixel, 1.0e-5F);
  }
}

TEST(MinimalCartesianPlanBoundE2E,
     RunsTwoRealFramesThroughHostAssemblerProviderReorderAndFrozenEdgeWithOnePayloadCharge) {
  std::shared_ptr<ResourceVectorLedger> retained_global;
  {
    auto created = make_runtime();
    ASSERT_TRUE(created.ok()) << created.status();
    auto runtime = std::move(created).value();
    retained_global = runtime->global_ledger;
    const auto admitted_global = retained_global->snapshot();
    EXPECT_EQ(runtime->artifacts.plan.resources().host_normal_bytes(), admitted_global.used.host_normal_bytes);
    EXPECT_EQ(runtime->artifacts.plan.resources().descriptor_count(), admitted_global.used.descriptor_count);

    ProviderInstance provider;
    ASSERT_TRUE(initialize_provider(provider).ok());
    auto ingress =
      runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
    ASSERT_TRUE(ingress.ok()) << ingress.status();
    auto bridge = make_bridge(*runtime->plane);
    ASSERT_TRUE(bridge.ok()) << bridge.status();

    // A bad frozen Producer identity must fail before the bridge reserves a
    // pool slot or DataEdge credit. The same prepared host frame remains
    // usable for the correct invocation afterward.
    auto completed_one = complete_frame(*runtime->host, 1U, 8);
    ASSERT_TRUE(completed_one.ok()) << completed_one.status();
    auto dispatch_one = ingress.value().try_prepare(completed_one.value());
    ASSERT_TRUE(dispatch_one.ok()) << dispatch_one.status();
    auto foreign_invocation = provider.invocation();
    foreign_invocation.operator_id = "foreign-cartesian-operator";
    const auto edge_before_rejection = runtime->plane->edge_snapshot();
    const auto rejected =
      bridge.value().process_reorder(foreign_invocation, dispatch_one.value(), firing_request(10U, runtime->scratch));
    EXPECT_FALSE(rejected.ok());
    EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
    EXPECT_TRUE(dispatch_one.value().valid());
    const auto edge_after_rejection = runtime->plane->edge_snapshot();
    EXPECT_EQ(edge_before_rejection.reserved_items, edge_after_rejection.reserved_items);
    EXPECT_EQ(edge_before_rejection.queued_items, edge_after_rejection.queued_items);
    EXPECT_EQ(edge_before_rejection.occupied_items, edge_after_rejection.occupied_items);

    auto first_firing = bridge.value().process_reorder(provider.invocation(), dispatch_one.value(),
                                                       firing_request(11U, runtime->scratch));
    ASSERT_TRUE(first_firing.ok()) << first_firing.status();
    ASSERT_EQ(SynchronousFiringOutcome::done, first_firing.value().firing.outcome);
    auto first_output = std::move(first_firing.value().ordered_output);
    ASSERT_TRUE(first_output.valid());
    EXPECT_EQ(ksj::base::StatusCode::unavailable, first_output.try_publish().code());
    EXPECT_TRUE(first_output.valid());
    EXPECT_EQ(1U, runtime->plane->edge_snapshot().reserved_items);
    EXPECT_EQ(0U, runtime->plane->edge_snapshot().queued_items);

    auto completed_zero = complete_frame(*runtime->host, 0U, 4);
    ASSERT_TRUE(completed_zero.ok()) << completed_zero.status();
    auto dispatch_zero = ingress.value().try_prepare(completed_zero.value());
    ASSERT_TRUE(dispatch_zero.ok()) << dispatch_zero.status();
    auto second_firing = bridge.value().process_reorder(provider.invocation(), dispatch_zero.value(),
                                                        firing_request(12U, runtime->scratch));
    ASSERT_TRUE(second_firing.ok()) << second_firing.status();
    ASSERT_EQ(SynchronousFiringOutcome::done, second_firing.value().firing.outcome);
    auto second_output = std::move(second_firing.value().ordered_output);
    ASSERT_TRUE(second_output.valid());
    ASSERT_TRUE(second_output.try_publish().ok());
    ASSERT_TRUE(first_output.try_publish().ok());

    // Pool, reorder, and edge all operate on the context's local mirror
    // ledger. The one global admission charge remains unchanged while the
    // same two BufferHandles move across those stages.
    EXPECT_EQ(admitted_global.used, retained_global->snapshot().used);

    const auto queued = runtime->plane->edge_snapshot();
    EXPECT_EQ(0U, queued.reserved_items);
    EXPECT_EQ(2U, queued.queued_items);
    EXPECT_EQ(2U, queued.occupied_items);
    ASSERT_TRUE(bridge.value().end_of_input().ok());

    auto first_item = runtime->plane->try_acquire_for_sink();
    ASSERT_EQ(FixedBufferEdgePollKind::item, first_item.kind);
    ASSERT_TRUE(first_item.lease.has_value());
    expect_magnitude_image(first_item.lease->buffer(), 1.0F);
    ASSERT_TRUE(first_item.lease->acknowledge_consumed().ok());

    auto second_item = runtime->plane->try_acquire_for_sink();
    ASSERT_EQ(FixedBufferEdgePollKind::item, second_item.kind);
    ASSERT_TRUE(second_item.lease.has_value());
    expect_magnitude_image(second_item.lease->buffer(), 2.0F);
    ASSERT_TRUE(second_item.lease->acknowledge_consumed().ok());

    EXPECT_EQ(FixedBufferEdgePollKind::completed, runtime->plane->try_acquire_for_sink().kind);
    const auto drained = runtime->plane->edge_snapshot();
    EXPECT_EQ(0U, drained.reserved_items);
    EXPECT_EQ(0U, drained.queued_items);
    EXPECT_EQ(0U, drained.leased_items);
    EXPECT_EQ(0U, drained.occupied_items);
  }
  ASSERT_NE(retained_global, nullptr);
  const auto released = retained_global->snapshot();
  EXPECT_TRUE(released.reserved.empty());
  EXPECT_TRUE(released.used.empty());
}

} // namespace
