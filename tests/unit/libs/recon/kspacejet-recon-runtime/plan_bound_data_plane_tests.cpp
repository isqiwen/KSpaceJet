#include "kspacejet/recon/runtime/plan_bound_data_plane.hpp"

#include "kspacejet/provider/loader/provider_loader.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
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
using ksj::recon::AdmissionRecordSpec;
using ksj::recon::ArtifactDigest;
using ksj::recon::BufferPoolPlanSpec;
using ksj::recon::DataEdgePlanSpec;
using ksj::recon::DenseCartesianOrdinalDimensionSpec;
using ksj::recon::DenseKeySlotDimensionSpec;
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
using ksj::recon::TypeDescriptorSpec;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::VerificationRecord;
using ksj::recon::VerificationRecordSpec;
using ksj::recon::runtime::AdmittedPlanBoundDataPlane;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::CartesianLineCoordinate;
using ksj::recon::runtime::CompletedFrameLease;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FixedBufferEdgeLifecycle;
using ksj::recon::runtime::FixedBufferEdgePollKind;
using ksj::recon::runtime::FixedReorderBufferHandleSidecar;
using ksj::recon::runtime::FixedReorderBufferStorage;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::PlanBoundDataPlaneStorage;
using ksj::recon::runtime::PlanBoundFrameDispatch;
using ksj::recon::runtime::PlanBoundM3ReorderIngress;
using ksj::recon::runtime::PlanBoundOrderedOutput;
using ksj::recon::runtime::PlanBoundReorderFiringRequest;
using ksj::recon::runtime::PlanBoundSynchronousFiringConfig;
using ksj::recon::runtime::ResourceVectorLedger;
using ksj::recon::runtime::ResourceVectorLedgerReservation;
using ksj::recon::runtime::SynchronousFiringOutcome;
using ksj::recon::runtime::SynchronousProviderInvocation;

constexpr std::string_view kPlanDigest = "sha256:1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::string_view kVerificationDigest =
  "sha256:2222222222222222222222222222222222222222222222222222222222222222";
constexpr std::string_view kBundleDigest = "sha256:808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
constexpr std::string_view kContractDigest = "sha256:404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f";
constexpr std::string_view kOutputAbiDigest = "sha256:2122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f40";
constexpr std::string_view kOutputPayloadDigest =
  "sha256:3132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f50";
constexpr std::string_view kOutputMetadataDigest =
  "sha256:5152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f70";
constexpr char kProviderId[] = "org.kspacejet.tests.synchronous-firing-lease";
constexpr char kOperatorId[] = "synchronous_firing_lease_test_operator";

template <typename T>
concept HasRawDispatchCommit = requires(T& value) { value.commit(); };

template <typename T>
concept HasRawDispatchAbort = requires(T& value) { value.abort(); };

template <typename T>
concept HasRawDispatchPublish = requires(T& value) { value.try_acquire_publish(); };

template <typename T>
concept HasDirectPublishAcknowledgement = requires(T& value) { value.acknowledge_published(); };

static_assert(!HasRawDispatchCommit<PlanBoundFrameDispatch>);
static_assert(!HasRawDispatchAbort<PlanBoundFrameDispatch>);
static_assert(!HasRawDispatchPublish<PlanBoundFrameDispatch>);
static_assert(!HasDirectPublishAcknowledgement<PlanBoundOrderedOutput>);
static_assert(!std::is_convertible_v<PlanBoundFrameDispatch, ksj::recon::runtime::FrameDispatch>);

[[nodiscard]] Result<ArtifactDigest> parse_digest(const std::string_view value) {
  return ArtifactDigest::parse(value, "plan-bound data-plane test digest");
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

[[nodiscard]] Result<PlanArtifacts> make_artifacts(const Quantity ordinal_domain) {
  constexpr Quantity kPoolSlots = 2U;
  // The synchronous test Provider declares a 64-byte aggregate output bound.
  // M3.7 preacquires the whole pool slot as its output grant, so make that
  // frozen grant exactly one aligned 64-byte payload and no metadata slab.
  // The exercised Provider modes seal four payload bytes and no metadata.
  constexpr Quantity kPayloadCapacity = 64U;
  constexpr Quantity kMetadataCapacity = 0U;
  constexpr Quantity kFullSlotLogicalBytes = kPayloadCapacity + kMetadataCapacity;
  constexpr Quantity kMaxAheadItems = 1U;
  if (ordinal_domain < 2U) {
    return Status::InvalidArgument("plan-bound data-plane test needs at least two frame ordinals");
  }

  auto type = TypeDescriptor::create({.type_id = "org.kspacejet.tests.plan-bound-image",
                                      .revision = 1U,
                                      .abi_descriptor_digest = std::string(kOutputAbiDigest),
                                      .payload_schema_digest = std::string(kOutputPayloadDigest),
                                      .payload_kind = PayloadKind::buffer_handle,
                                      .element_type = ksj::recon::ElementType::uint8,
                                      .rank = 0U,
                                      .dimensions = {},
                                      .layout = ksj::recon::LayoutKind::canonical_contiguous,
                                      .strides = StrideKind::canonical,
                                      .explicit_byte_strides = {},
                                      .allowed_memory_domains = {TypeMemoryDomain::host_normal},
                                      .min_alignment_bytes = 64U,
                                      .mutability = PayloadMutability::immutable_after_publish,
                                      .metadata_schema_digest = std::string(kOutputMetadataDigest)},
                                     "plan-bound test output type");
  if (!type.ok()) {
    return type.status();
  }
  auto key_charge = ksj::recon::dense_key_slot_host_metadata_charged_bytes(ordinal_domain, ordinal_domain,
                                                                           "plan-bound test key metadata");
  if (!key_charge.ok()) {
    return key_charge.status();
  }
  auto reorder_charge = ksj::recon::dense_cartesian_reorder_host_metadata_charged_bytes(
    ordinal_domain, kMaxAheadItems, "plan-bound test reorder metadata");
  if (!reorder_charge.ok()) {
    return reorder_charge.status();
  }
  auto pool_charge =
    ksj::recon::m37_buffer_pool_host_metadata_charged_bytes(kPoolSlots, "plan-bound test pool metadata");
  if (!pool_charge.ok()) {
    return pool_charge.status();
  }
  auto pool_physical = ksj::recon::m37_buffer_pool_physical_charge_bytes(
    kPoolSlots, kPayloadCapacity, kMetadataCapacity, "plan-bound test pool physical");
  if (!pool_physical.ok()) {
    return pool_physical.status();
  }
  auto edge_charge = ksj::recon::m37_data_edge_host_metadata_charged_bytes(kPoolSlots, "plan-bound test edge metadata");
  if (!edge_charge.ok()) {
    return edge_charge.status();
  }
  const auto plan_digest = parse_digest(kPlanDigest);
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }

  ExecutionPlanSpec specification;
  specification.inputs = {
    .resolved_pipeline = std::string(kPlanDigest),
    .scan_descriptor = std::string(kVerificationDigest),
    .target_envelope = std::string(kOutputAbiDigest),
    .machine_policy = std::string(kOutputPayloadDigest),
    .provider_contracts = {std::string(kContractDigest)},
  };
  specification.execution_profile = ExecutionProfile::bounded_online;
  specification.key_slot_tables = {
    KeySlotTablePlanSpec{
      .node_id = "reconstruct",
      .dense_dimensions = {DenseKeySlotDimensionSpec{.field = "slice", .minimum = 0U, .cardinality = ordinal_domain}},
      .key_domain_bound = ordinal_domain,
      .max_distinct_keys = ordinal_domain,
      .max_live_keys = ordinal_domain,
      .slot_count = ordinal_domain,
      .host_metadata_charged_bytes = key_charge.value(),
      .max_items_per_activation = 1U,
      .max_charged_bytes_per_activation = kFullSlotLogicalBytes,
    },
  };
  specification.reorder_plans = {
    ReorderPlanSpec{
      .node_id = "reconstruct",
      .order_domain_id = "reconstruct",
      .ordinal_binding_id = std::string(ksj::recon::kCompletedFrameSlotContextSemanticKeyOrdinalBindingId),
      .completed_frame_input_port = "completed-frame",
      .ordered_output_port = "image",
      .outputs_per_ordinal = 1U,
      .charged_bytes_per_ordinal = kFullSlotLogicalBytes,
      .ordinal_dimensions =
        {
          DenseCartesianOrdinalDimensionSpec{.field = "slice", .minimum = 0U, .cardinality = ordinal_domain},
        },
      .mapping_algorithm_id = std::string(ksj::recon::kDenseCartesianReorderMappingAlgorithmId),
      .storage_accounting_id = std::string(ksj::recon::kDenseCartesianReorderStorageAccountingId),
      .ordinal_domain_bound = ordinal_domain,
      .first_expected_ordinal = ksj::recon::kFirstExpectedReorderOrdinal,
      .last_expected_ordinal = ordinal_domain - 1U,
      .max_ahead_items = kMaxAheadItems,
      .max_ahead_charged_bytes = kMaxAheadItems * kFullSlotLogicalBytes,
      .max_gap_ordinals = ordinal_domain - 1U,
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
      .pool_id = "image.pool",
      .producer_node_id = "reconstruct",
      .producer_port_name = "image",
      .producer_provider_id = kProviderId,
      .producer_bundle_digest = std::string(kBundleDigest),
      .producer_operator_id = kOperatorId,
      .producer_contract_digest = std::string(kContractDigest),
      .type_descriptor = type.value(),
      .memory_domain = TypeMemoryDomain::host_normal,
      .slot_count = kPoolSlots,
      .payload_capacity_bytes = kPayloadCapacity,
      .metadata_capacity_bytes = kMetadataCapacity,
      .payload_alignment_bytes = 64U,
      .storage_accounting_id = std::string(ksj::recon::kM37BufferPoolStorageAccountingId),
      .host_metadata_charged_bytes = pool_charge.value(),
      .descriptor_charged_count = kPoolSlots,
      .physical_charge_bytes = pool_physical.value(),
    },
  };
  specification.data_edge_plans = {
    DataEdgePlanSpec{
      .edge_id = "image.edge",
      .source_pool_id = "image.pool",
      .producer_node_id = "reconstruct",
      .producer_port_name = "image",
      .producer_abi_port = 0U,
      .consumer_node_id = "sink",
      .consumer_port_name = "image",
      .type_descriptor = type.value(),
      .max_items = kPoolSlots,
      .max_logical_bytes = kPoolSlots * kFullSlotLogicalBytes,
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
                         ksj::recon::kM37FiringLeaseHostStagingChargedBytes,
    .descriptor_count =
      kMaxAheadItems + kPoolSlots + kPoolSlots + ksj::recon::kM37FiringLeaseHostStagingDescriptorCount,
    .cpu_leaf_permits = 1U,
  };
  specification.terminal_occurrences = ordinal_domain;
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
  const auto verification_digest = parse_digest(kVerificationDigest);
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
  [[nodiscard]] const byte* data() const noexcept { return data_; }
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
  AlignedBytes shared_pool_edge;
  AlignedBytes reorder_bookkeeping;
  std::vector<FixedReorderBufferHandleSidecar> reorder_sidecars;
  std::size_t pool_payload_bytes{0U};
  std::size_t edge_control_bytes{0U};
  std::size_t reorder_bookkeeping_bytes{0U};
  bool pool_edge_overlap{false};

  [[nodiscard]] static Result<RuntimeSlabs> create(const ExecutionPlan& plan, const bool overlap_pool_edge = false) {
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
    const auto payload_bytes = static_cast<std::size_t>(pool.slot_count() * pool.payload_capacity_bytes());
    const auto metadata_bytes = static_cast<std::size_t>(pool.slot_count() * pool.metadata_capacity_bytes());
    try {
      RuntimeSlabs slabs;
      slabs.pool_payload = AlignedBytes{payload_bytes};
      slabs.pool_metadata = AlignedBytes{metadata_bytes};
      slabs.pool_control = AlignedBytes{pool_control.value()};
      slabs.edge_control = AlignedBytes{edge_control.value()};
      slabs.reorder_bookkeeping = AlignedBytes{reorder_bookkeeping.value()};
      slabs.reorder_sidecars.resize(static_cast<std::size_t>(reorder.max_ahead_items()));
      slabs.pool_payload_bytes = payload_bytes;
      slabs.edge_control_bytes = edge_control.value();
      slabs.reorder_bookkeeping_bytes = reorder_bookkeeping.value();
      slabs.pool_edge_overlap = overlap_pool_edge;
      if (overlap_pool_edge) {
        slabs.shared_pool_edge = AlignedBytes{std::max(payload_bytes, edge_control.value())};
      }
      return slabs;
    } catch (const std::bad_alloc&) {
      return Status::OutOfMemory("unable to allocate fixed plan-bound data-plane test slabs");
    }
  }

  [[nodiscard]] PlanBoundDataPlaneStorage data_plane_storage() noexcept {
    const auto payload =
      pool_edge_overlap ? ksj::base::ByteSpan{shared_pool_edge.data(), pool_payload_bytes} : pool_payload.view();
    const auto edge =
      pool_edge_overlap ? ksj::base::ByteSpan{shared_pool_edge.data(), edge_control_bytes} : edge_control.view();
    return {.pool = {.payload = payload, .metadata = pool_metadata.view(), .control = pool_control.view()},
            .edge = {.control = edge}};
  }

  [[nodiscard]] FixedReorderBufferStorage reorder_storage() noexcept {
    return {.bookkeeping = reorder_bookkeeping.view(),
            .handle_sidecars =
              std::span<FixedReorderBufferHandleSidecar>{reorder_sidecars.data(), reorder_sidecars.size()}};
  }

  [[nodiscard]] FixedReorderBufferStorage reorder_overlapping_pool_storage() noexcept {
    return {.bookkeeping = {pool_payload.data(), reorder_bookkeeping_bytes},
            .handle_sidecars =
              std::span<FixedReorderBufferHandleSidecar>{reorder_sidecars.data(), reorder_sidecars.size()}};
  }

  [[nodiscard]] FixedReorderBufferStorage reorder_overlapping_edge_storage() noexcept {
    return {.bookkeeping = {edge_control.data(), reorder_bookkeeping_bytes},
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
                                   "plan-bound test global admission ledger");
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
    return Status::OutOfMemory("unable to allocate plan-bound test global admission ledger");
  }
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

[[nodiscard]] Result<std::unique_ptr<HostFrameAssembler>> make_host(const PlanArtifacts& artifacts) {
  const auto max_ahead = artifacts.plan.reorder_plans().front().max_ahead_items();
  HostFrameAssemblerConfig configuration{.scan_instance_id = "plan-bound-data-plane-test-scan"};
  configuration.frame_slots.reserve(static_cast<std::size_t>(max_ahead + 1U));
  for (Quantity index = 0U; index <= max_ahead; ++index) {
    configuration.frame_slots.push_back(frame_slot_config(static_cast<std::uint32_t>(index + 1U)));
  }
  return HostFrameAssembler::create(artifacts.plan, artifacts.verification, "reconstruct", std::move(configuration));
}

struct Runtime final {
  Runtime(PlanArtifacts artifacts_value, std::shared_ptr<ResourceVectorLedger> global_ledger_value,
          RuntimeSlabs slabs_value, std::unique_ptr<HostFrameAssembler> host_value,
          std::unique_ptr<AdmittedPlanBoundDataPlane> plane_value) noexcept
      : artifacts(std::move(artifacts_value)), global_ledger(std::move(global_ledger_value)),
        slabs(std::move(slabs_value)), host(std::move(host_value)), plane(std::move(plane_value)) {}

  PlanArtifacts artifacts;
  // The declaration order keeps the global ledger alive through destruction
  // of the context's one outer-owned admission token.
  std::shared_ptr<ResourceVectorLedger> global_ledger;
  RuntimeSlabs slabs;
  std::unique_ptr<HostFrameAssembler> host;
  std::unique_ptr<AdmittedPlanBoundDataPlane> plane;
};

[[nodiscard]] Result<std::unique_ptr<Runtime>> make_runtime(const Quantity ordinal_domain) {
  auto artifacts = make_artifacts(ordinal_domain);
  if (!artifacts.ok()) {
    return artifacts.status();
  }
  auto slabs = RuntimeSlabs::create(artifacts.value().plan);
  if (!slabs.ok()) {
    return slabs.status();
  }
  auto host = make_host(artifacts.value());
  if (!host.ok()) {
    return host.status();
  }
  auto admission = reserve_global_admission(artifacts.value());
  if (!admission.ok()) {
    return admission.status();
  }
  auto admission_value = std::move(admission).value();
  auto global_ledger = admission_value.ledger;
  auto plane = AdmittedPlanBoundDataPlane::create(artifacts.value().plan, artifacts.value().verification,
                                                  artifacts.value().admission, std::move(admission_value.reservation),
                                                  slabs.value().data_plane_storage());
  if (!plane.ok()) {
    return plane.status();
  }
  try {
    return std::make_unique<Runtime>(std::move(artifacts).value(), std::move(global_ledger), std::move(slabs).value(),
                                     std::move(host).value(), std::move(plane).value());
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory("unable to allocate plan-bound data-plane test runtime");
  }
}

[[nodiscard]] FrameSlotContext frame_context(const std::uint16_t slice) {
  FrameSlotContext value;
  value.semantic_key.slice = slice;
  value.order_key = slice;
  value.placement_key = slice;
  return value;
}

[[nodiscard]] Result<CompletedFrameLease> complete_frame(HostFrameAssembler& host, const std::uint16_t slice) {
  auto assembling = host.try_begin_frame(frame_context(slice));
  if (!assembling.ok()) {
    return assembling.status();
  }
  const std::array<byte, 1U> sample{byte{0x24U}};
  auto frame = std::move(assembling).value();
  const auto scattered = frame.scatter(CartesianLineCoordinate{.phase_encode_1 = 0U, .phase_encode_2 = 0U}, sample);
  if (!scattered.ok()) {
    return scattered;
  }
  return frame.seal_complete();
}

[[nodiscard]] Result<ResourceVector> firing_reservation() {
  return ResourceVector::create({.cpu_leaf_permits = 1U}, "plan-bound test firing CPU reservation");
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
                                                     .maximum_input_payload_bytes = 1U,
                                                     .maximum_scratch_bytes = 0U,
                                                     .maximum_metadata_bytes = 16U});
}

[[nodiscard]] PlanBoundReorderFiringRequest firing_request(const std::uint64_t occurrence) {
  return {.resource_occurrence_id = occurrence, .slot_generation = occurrence + 1U, .terminal_epoch = 7U};
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

[[nodiscard]] Status initialize_provider(ProviderInstance& instance, const std::string_view canonical_config) {
  auto loaded = ProviderModule::load(std::filesystem::path(KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE));
  if (!loaded.ok()) {
    return loaded.status();
  }
  instance.module = std::move(loaded).value();
  instance.lease = instance.module.acquire();
  const auto* const descriptor = instance.lease.descriptor();
  const auto* const api = instance.lease.api();
  if (descriptor == nullptr || api == nullptr) {
    return Status::InternalError("plan-bound test Provider has no descriptor/API");
  }
  const auto operator_descriptor =
    std::find_if(descriptor->operators.begin(), descriptor->operators.end(), [](const auto& candidate) {
      return candidate.operator_id == kOperatorId;
    });
  if (operator_descriptor == descriptor->operators.end()) {
    return Status::NotFound("plan-bound test Provider has no expected operator");
  }

  ksj_operator_create_request create{};
  create.abi = abi_header(sizeof(create));
  create.operator_id = text(kOperatorId, sizeof(kOperatorId) - 1U);
  create.required_contract_digest.abi = abi_header(sizeof(create.required_contract_digest));
  std::copy(operator_descriptor->contract_digest.begin(), operator_descriptor->contract_digest.end(),
            create.required_contract_digest.bytes);
  create.canonical_config.abi = abi_header(sizeof(create.canonical_config));
  create.canonical_config.data = canonical_config.data();
  create.canonical_config.size = canonical_config.size();
  auto error = error_storage();
  if (api->operator_create(&create, &instance.operator_handle, &error) != KSJ_STATUS_OK ||
      instance.operator_handle == nullptr) {
    return Status::ValidationError("plan-bound test Provider rejected operator creation");
  }

  ksj_execution_context_descriptor context{};
  context.abi = abi_header(sizeof(context));
  context.execution_context_id = 1U;
  context.max_backend_concurrency = 1U;
  error = error_storage();
  if (api->execution_context_create(instance.operator_handle, &context, &instance.execution_context, &error) !=
        KSJ_STATUS_OK ||
      instance.execution_context == nullptr) {
    return Status::ValidationError("plan-bound test Provider rejected execution context");
  }

  ksj_key_state_descriptor key{};
  key.abi = abi_header(sizeof(key));
  key.semantic_key.abi = abi_header(sizeof(key.semantic_key));
  key.key_state_generation = 1U;
  error = error_storage();
  if (api->key_state_init(instance.operator_handle, instance.execution_context, &key, &instance.key_state, &error) !=
        KSJ_STATUS_OK ||
      instance.key_state == nullptr) {
    return Status::ValidationError("plan-bound test Provider rejected key state");
  }

  ksj_scan_start_descriptor start{};
  start.abi = abi_header(sizeof(start));
  start.run_id = text("run", 3U);
  start.scan_id = text("scan", 4U);
  start.normalized_scan_facts_digest = digest(0x41U);
  start.execution_plan_digest = digest(0x51U);
  start.terminal_epoch = 7U;
  error = error_storage();
  if (api->operator_on_start(instance.operator_handle, instance.execution_context, instance.key_state, &start,
                             &error) != KSJ_STATUS_OK) {
    return Status::ValidationError("plan-bound test Provider rejected scan start");
  }
  return Status::Ok();
}

TEST(KSpaceJetPlanBoundDataPlane, ProductCapabilitiesExposeNoRawDispatchOrDirectTypedAck) {
  EXPECT_FALSE((HasRawDispatchCommit<PlanBoundFrameDispatch>));
  EXPECT_FALSE((HasRawDispatchAbort<PlanBoundFrameDispatch>));
  EXPECT_FALSE((HasRawDispatchPublish<PlanBoundFrameDispatch>));
  EXPECT_FALSE((HasDirectPublishAcknowledgement<PlanBoundOrderedOutput>));
}

TEST(KSpaceJetPlanBoundDataPlane, RetainsOneGlobalAdmissionWhileComponentsUseOnlyTheLocalMirrorLedger) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  const auto expected = runtime->artifacts.plan.resources();
  const auto global_before = runtime->global_ledger->snapshot();
  EXPECT_TRUE(global_before.reserved.empty());
  EXPECT_EQ(expected.host_normal_bytes(), global_before.used.host_normal_bytes);
  EXPECT_EQ(expected.descriptor_count(), global_before.used.descriptor_count);
  EXPECT_EQ(expected.cpu_leaf_permits(), global_before.used.cpu_leaf_permits);

  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  const auto duplicate =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(ksj::base::StatusCode::state_error, duplicate.status().code());
  const auto global_after_components = runtime->global_ledger->snapshot();
  EXPECT_EQ(global_before.used, global_after_components.used);

  auto retained_global = runtime->global_ledger;
  ingress = PlanBoundM3ReorderIngress{};
  runtime.reset();
  const auto released = retained_global->snapshot();
  EXPECT_TRUE(released.reserved.empty());
  EXPECT_TRUE(released.used.empty());
}

TEST(KSpaceJetPlanBoundDataPlane, RejectsPoolAndEdgeCallerSlabOverlapBeforeContextEscapes) {
  auto artifacts = make_artifacts(2U);
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto slabs = RuntimeSlabs::create(artifacts.value().plan, true);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto admission = reserve_global_admission(artifacts.value());
  ASSERT_TRUE(admission.ok()) << admission.status();
  auto retained_global = admission.value().ledger;

  auto plane = AdmittedPlanBoundDataPlane::create(artifacts.value().plan, artifacts.value().verification,
                                                  artifacts.value().admission, std::move(admission).value().reservation,
                                                  slabs.value().data_plane_storage());
  EXPECT_FALSE(plane.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, plane.status().code());
  EXPECT_TRUE(retained_global->snapshot().reserved.empty());
  EXPECT_TRUE(retained_global->snapshot().used.empty());
}

TEST(KSpaceJetPlanBoundDataPlane, RejectsCallerControlStorageSmallerThanTheFrozenPlanProof) {
  {
    auto artifacts = make_artifacts(2U);
    ASSERT_TRUE(artifacts.ok()) << artifacts.status();
    auto slabs = RuntimeSlabs::create(artifacts.value().plan);
    ASSERT_TRUE(slabs.ok()) << slabs.status();
    auto admission = reserve_global_admission(artifacts.value());
    ASSERT_TRUE(admission.ok()) << admission.status();
    auto retained_global = admission.value().ledger;
    auto storage = slabs.value().data_plane_storage();
    ASSERT_GT(storage.pool.control.size(), 0U);
    storage.pool.control = storage.pool.control.first(storage.pool.control.size() - 1U);

    auto plane = AdmittedPlanBoundDataPlane::create(artifacts.value().plan, artifacts.value().verification,
                                                    artifacts.value().admission,
                                                    std::move(admission).value().reservation, storage);
    EXPECT_FALSE(plane.ok());
    EXPECT_EQ(ksj::base::StatusCode::invalid_argument, plane.status().code());
    EXPECT_TRUE(retained_global->snapshot().reserved.empty());
    EXPECT_TRUE(retained_global->snapshot().used.empty());
  }

  {
    auto artifacts = make_artifacts(2U);
    ASSERT_TRUE(artifacts.ok()) << artifacts.status();
    auto slabs = RuntimeSlabs::create(artifacts.value().plan);
    ASSERT_TRUE(slabs.ok()) << slabs.status();
    auto admission = reserve_global_admission(artifacts.value());
    ASSERT_TRUE(admission.ok()) << admission.status();
    auto retained_global = admission.value().ledger;
    auto storage = slabs.value().data_plane_storage();
    ASSERT_GT(storage.edge.control.size(), 0U);
    storage.edge.control = storage.edge.control.first(storage.edge.control.size() - 1U);

    auto plane = AdmittedPlanBoundDataPlane::create(artifacts.value().plan, artifacts.value().verification,
                                                    artifacts.value().admission,
                                                    std::move(admission).value().reservation, storage);
    EXPECT_FALSE(plane.ok());
    EXPECT_EQ(ksj::base::StatusCode::invalid_argument, plane.status().code());
    EXPECT_TRUE(retained_global->snapshot().reserved.empty());
    EXPECT_TRUE(retained_global->snapshot().used.empty());
  }
}

TEST(KSpaceJetPlanBoundDataPlane, ContextRejectsReorderStorageOverlappingEitherPoolOrEdge) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();

  ASSERT_LE(runtime->slabs.reorder_bookkeeping_bytes, runtime->slabs.pool_payload_bytes);
  const auto pool_overlap = runtime->plane->create_m3_reorder_ingress(
    "reconstruct", *runtime->host, runtime->slabs.reorder_overlapping_pool_storage());
  EXPECT_FALSE(pool_overlap.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, pool_overlap.status().code());

  ASSERT_LE(runtime->slabs.reorder_bookkeeping_bytes, runtime->slabs.edge_control_bytes);
  const auto edge_overlap = runtime->plane->create_m3_reorder_ingress(
    "reconstruct", *runtime->host, runtime->slabs.reorder_overlapping_edge_storage());
  EXPECT_FALSE(edge_overlap.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, edge_overlap.status().code());

  // Both rejected allocations release their local claim/budget immediately;
  // the normal context-owned ingress can still bind once afterward.
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
}

TEST(KSpaceJetPlanBoundDataPlane, RejectsWrongProviderIdentityBeforePoolOrEdgeCreditIsReserved) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  auto bridge = make_bridge(*runtime->plane);
  ASSERT_TRUE(bridge.ok()) << bridge.status();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  auto completed = complete_frame(*runtime->host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  auto prepared = ingress.value().try_prepare(completed.value());
  ASSERT_TRUE(prepared.ok()) << prepared.status();
  auto invocation = provider.invocation();
  invocation.operator_id = "foreign-operator";
  const auto before = runtime->plane->edge_snapshot();
  const auto rejected = bridge.value().process_reorder(invocation, prepared.value(), firing_request(1U));
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
  EXPECT_TRUE(prepared.value().valid());
  const auto after = runtime->plane->edge_snapshot();
  EXPECT_EQ(before.reserved_items, after.reserved_items);
  EXPECT_EQ(before.queued_items, after.queued_items);
  EXPECT_EQ(before.occupied_items, after.occupied_items);
}

TEST(KSpaceJetPlanBoundDataPlane, ProviderUnsettledOutputFailsClosedAndReturnsPreacquiredCredits) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  auto bridge = make_bridge(*runtime->plane);
  ASSERT_TRUE(bridge.ok()) << bridge.status();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"unsettled-output\"}").ok());

  auto completed = complete_frame(*runtime->host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  auto prepared = ingress.value().try_prepare(completed.value());
  ASSERT_TRUE(prepared.ok()) << prepared.status();
  auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(2U));
  ASSERT_TRUE(fired.ok()) << fired.status();
  EXPECT_EQ(SynchronousFiringOutcome::contract_violation, fired.value().firing.outcome);
  EXPECT_FALSE(fired.value().ordered_output.valid());
  EXPECT_FALSE(prepared.value().valid());
  const auto edge = runtime->plane->edge_snapshot();
  EXPECT_EQ(0U, edge.reserved_items);
  EXPECT_EQ(0U, edge.queued_items);
  EXPECT_EQ(0U, edge.occupied_items);
  EXPECT_EQ(FixedBufferEdgeLifecycle::failed, edge.lifecycle);
}

TEST(KSpaceJetPlanBoundDataPlane, TwoFramesFlowThroughContextOwnedReorderAndDrainTheFrozenEdgeInOrder) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  auto bridge = make_bridge(*runtime->plane);
  ASSERT_TRUE(bridge.ok()) << bridge.status();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  for (std::uint16_t slice = 0U; slice < 2U; ++slice) {
    auto completed = complete_frame(*runtime->host, slice);
    ASSERT_TRUE(completed.ok()) << completed.status();
    auto prepared = ingress.value().try_prepare(completed.value());
    ASSERT_TRUE(prepared.ok()) << prepared.status();
    auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(slice + 10U));
    ASSERT_TRUE(fired.ok()) << fired.status();
    ASSERT_EQ(SynchronousFiringOutcome::done, fired.value().firing.outcome);
    ASSERT_TRUE(fired.value().ordered_output.valid());
    ASSERT_TRUE(fired.value().ordered_output.try_publish().ok());
  }
  EXPECT_EQ(2U, runtime->plane->edge_snapshot().queued_items);

  for (std::uint32_t index = 0U; index < 2U; ++index) {
    auto item = runtime->plane->try_acquire_for_sink();
    ASSERT_EQ(FixedBufferEdgePollKind::item, item.kind);
    ASSERT_TRUE(item.lease.has_value());
    const auto payload = item.lease->buffer().payload();
    ASSERT_TRUE(payload.ok()) << payload.status();
    ASSERT_EQ(4U, payload.value().size());
    EXPECT_EQ(byte{0x5AU}, payload.value()[0]);
    ASSERT_TRUE(item.lease->acknowledge_consumed().ok());
  }
  ASSERT_TRUE(bridge.value().end_of_input().ok());
  EXPECT_EQ(FixedBufferEdgePollKind::completed, runtime->plane->try_acquire_for_sink().kind);
}

TEST(KSpaceJetPlanBoundDataPlane, UpstreamEoiGapAbortsTheEdgeInsteadOfClosingItNormally) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  auto bridge = make_bridge(*runtime->plane);
  ASSERT_TRUE(bridge.ok()) << bridge.status();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  auto completed = complete_frame(*runtime->host, 0U);
  ASSERT_TRUE(completed.ok()) << completed.status();
  auto prepared = ingress.value().try_prepare(completed.value());
  ASSERT_TRUE(prepared.ok()) << prepared.status();
  auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(30U));
  ASSERT_TRUE(fired.ok()) << fired.status();
  ASSERT_TRUE(fired.value().ordered_output.valid());

  const auto eoi = bridge.value().end_of_input();
  EXPECT_EQ(ksj::base::StatusCode::validation_error, eoi.code());
  EXPECT_EQ("REORDER_GAP_AT_EOI", eoi.message());
  EXPECT_NE(FixedBufferEdgeLifecycle::accepting, runtime->plane->edge_snapshot().lifecycle);
  EXPECT_EQ(FixedBufferEdgePollKind::failed, runtime->plane->try_acquire_for_sink().kind);
}

TEST(KSpaceJetPlanBoundDataPlane, DroppingBridgeWithAPendingOrderedOutputFailsTheWholeCoupledPath) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());
  std::optional<PlanBoundOrderedOutput> pending;
  {
    auto bridge = make_bridge(*runtime->plane);
    ASSERT_TRUE(bridge.ok()) << bridge.status();
    auto completed = complete_frame(*runtime->host, 0U);
    ASSERT_TRUE(completed.ok()) << completed.status();
    auto prepared = ingress.value().try_prepare(completed.value());
    ASSERT_TRUE(prepared.ok()) << prepared.status();
    auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(40U));
    ASSERT_TRUE(fired.ok()) << fired.status();
    ASSERT_TRUE(fired.value().ordered_output.valid());
    pending.emplace(std::move(fired.value().ordered_output));
  }
  EXPECT_EQ(FixedBufferEdgePollKind::failed, runtime->plane->try_acquire_for_sink().kind);
  EXPECT_NE(FixedBufferEdgeLifecycle::accepting, runtime->plane->edge_snapshot().lifecycle);
  pending.reset();
  EXPECT_EQ(FixedBufferEdgeLifecycle::failed, runtime->plane->edge_snapshot().lifecycle);
}

TEST(KSpaceJetPlanBoundDataPlane, SinkLeaseRetainsGlobalAdmissionUntilTheLastObservableHandleSettles) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  std::optional<ksj::recon::runtime::PlanBoundSinkLease> retained_sink;
  {
    auto ingress =
      runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
    ASSERT_TRUE(ingress.ok()) << ingress.status();
    auto bridge = make_bridge(*runtime->plane);
    ASSERT_TRUE(bridge.ok()) << bridge.status();
    for (std::uint16_t slice = 0U; slice < 2U; ++slice) {
      auto completed = complete_frame(*runtime->host, slice);
      ASSERT_TRUE(completed.ok()) << completed.status();
      auto prepared = ingress.value().try_prepare(completed.value());
      ASSERT_TRUE(prepared.ok()) << prepared.status();
      auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(45U + slice));
      ASSERT_TRUE(fired.ok()) << fired.status();
      ASSERT_TRUE(fired.value().ordered_output.try_publish().ok());
    }
    ASSERT_TRUE(bridge.value().end_of_input().ok());
    auto sink = runtime->plane->try_acquire_for_sink();
    ASSERT_EQ(FixedBufferEdgePollKind::item, sink.kind);
    ASSERT_TRUE(sink.lease.has_value());
    ASSERT_TRUE(sink.lease->valid());
    retained_sink.emplace(std::move(*sink.lease));
  }

  auto retained_global = runtime->global_ledger;
  // Caller slab provenance must outlive every sink handle. Keep the raw
  // allocations independently alive while the context object is destroyed.
  auto retained_slabs = std::move(runtime->slabs);
  (void)retained_slabs;
  runtime.reset();
  EXPECT_FALSE(retained_global->snapshot().used.empty());
  ASSERT_TRUE(retained_sink->acknowledge_consumed().ok());
  retained_sink.reset();
  EXPECT_TRUE(retained_global->snapshot().reserved.empty());
  EXPECT_TRUE(retained_global->snapshot().used.empty());
}

TEST(KSpaceJetPlanBoundDataPlane, DroppingAnUnacknowledgedSinkLeaseAfterContextTeardownReleasesAdmission) {
  auto created = make_runtime(2U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  std::optional<ksj::recon::runtime::PlanBoundSinkLease> retained_sink;
  {
    auto ingress =
      runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
    ASSERT_TRUE(ingress.ok()) << ingress.status();
    auto bridge = make_bridge(*runtime->plane);
    ASSERT_TRUE(bridge.ok()) << bridge.status();
    for (std::uint16_t slice = 0U; slice < 2U; ++slice) {
      auto completed = complete_frame(*runtime->host, slice);
      ASSERT_TRUE(completed.ok()) << completed.status();
      auto prepared = ingress.value().try_prepare(completed.value());
      ASSERT_TRUE(prepared.ok()) << prepared.status();
      auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(60U + slice));
      ASSERT_TRUE(fired.ok()) << fired.status();
      ASSERT_TRUE(fired.value().ordered_output.try_publish().ok());
    }
    ASSERT_TRUE(bridge.value().end_of_input().ok());
    auto sink = runtime->plane->try_acquire_for_sink();
    ASSERT_EQ(FixedBufferEdgePollKind::item, sink.kind);
    ASSERT_TRUE(sink.lease.has_value());
    retained_sink.emplace(std::move(*sink.lease));
  }

  auto retained_global = runtime->global_ledger;
  auto retained_slabs = std::move(runtime->slabs);
  (void)retained_slabs;
  runtime.reset();
  EXPECT_FALSE(retained_global->snapshot().used.empty());

  // PlanBoundSinkLease's no-throw drop must settle the raw edge lease first,
  // then release the final shared admission lifetime.
  retained_sink.reset();
  EXPECT_TRUE(retained_global->snapshot().reserved.empty());
  EXPECT_TRUE(retained_global->snapshot().used.empty());
}

TEST(KSpaceJetPlanBoundDataPlane, QueuePressureLeavesPreparedDispatchRetryableAndReturnsCreditsOnDrain) {
  auto created = make_runtime(3U);
  ASSERT_TRUE(created.ok()) << created.status();
  auto runtime = std::move(created).value();
  auto ingress =
    runtime->plane->create_m3_reorder_ingress("reconstruct", *runtime->host, runtime->slabs.reorder_storage());
  ASSERT_TRUE(ingress.ok()) << ingress.status();
  auto bridge = make_bridge(*runtime->plane);
  ASSERT_TRUE(bridge.ok()) << bridge.status();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  for (std::uint16_t slice = 0U; slice < 2U; ++slice) {
    auto completed = complete_frame(*runtime->host, slice);
    ASSERT_TRUE(completed.ok()) << completed.status();
    auto prepared = ingress.value().try_prepare(completed.value());
    ASSERT_TRUE(prepared.ok()) << prepared.status();
    auto fired = bridge.value().process_reorder(provider.invocation(), prepared.value(), firing_request(50U + slice));
    ASSERT_TRUE(fired.ok()) << fired.status();
    ASSERT_TRUE(fired.value().ordered_output.try_publish().ok());
  }
  ASSERT_EQ(2U, runtime->plane->edge_snapshot().queued_items);

  auto third_completed = complete_frame(*runtime->host, 2U);
  ASSERT_TRUE(third_completed.ok()) << third_completed.status();
  auto third_dispatch = ingress.value().try_prepare(third_completed.value());
  ASSERT_TRUE(third_dispatch.ok()) << third_dispatch.status();
  const auto pressure =
    bridge.value().process_reorder(provider.invocation(), third_dispatch.value(), firing_request(52U));
  EXPECT_FALSE(pressure.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, pressure.status().code());
  EXPECT_TRUE(third_dispatch.value().valid());
  EXPECT_EQ(2U, runtime->plane->edge_snapshot().queued_items);
  EXPECT_EQ(0U, runtime->plane->edge_snapshot().reserved_items);

  for (std::uint32_t index = 0U; index < 2U; ++index) {
    auto item = runtime->plane->try_acquire_for_sink();
    ASSERT_EQ(FixedBufferEdgePollKind::item, item.kind);
    ASSERT_TRUE(item.lease.has_value());
    ASSERT_TRUE(item.lease->acknowledge_consumed().ok());
  }
  EXPECT_EQ(0U, runtime->plane->edge_snapshot().occupied_items);

  auto retried = bridge.value().process_reorder(provider.invocation(), third_dispatch.value(), firing_request(53U));
  ASSERT_TRUE(retried.ok()) << retried.status();
  ASSERT_TRUE(retried.value().ordered_output.try_publish().ok());
  auto final_item = runtime->plane->try_acquire_for_sink();
  ASSERT_EQ(FixedBufferEdgePollKind::item, final_item.kind);
  ASSERT_TRUE(final_item.lease.has_value());
  ASSERT_TRUE(final_item.lease->acknowledge_consumed().ok());
  ASSERT_TRUE(bridge.value().end_of_input().ok());
  EXPECT_EQ(FixedBufferEdgePollKind::completed, runtime->plane->try_acquire_for_sink().kind);
}

} // namespace
