#include "kspacejet/recon/runtime/plan_bound_data_plane.hpp"

#include "kspacejet/provider/v1/provider.h"
#include "kspacejet/recon/operator_contract.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {
namespace {

[[nodiscard]] bool contains_identifier(const std::vector<std::string>& identifiers,
                                       const std::string_view expected) noexcept {
  return std::find(identifiers.begin(), identifiers.end(), expected) != identifiers.end();
}

[[nodiscard]] bool checked_add_quantity(const Quantity lhs, const Quantity rhs, Quantity& result) noexcept {
  if (rhs > std::numeric_limits<Quantity>::max() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

[[nodiscard]] bool checked_multiply_quantity(const Quantity lhs, const Quantity rhs, Quantity& result) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<Quantity>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

[[nodiscard]] bool checked_multiply_size(const Quantity lhs, const Quantity rhs, std::size_t& result) noexcept {
  if (lhs > std::numeric_limits<std::size_t>::max() || rhs > std::numeric_limits<std::size_t>::max()) {
    return false;
  }
  const auto left = static_cast<std::size_t>(lhs);
  const auto right = static_cast<std::size_t>(rhs);
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] bool checked_add_size(const std::size_t lhs, const std::size_t rhs, std::size_t& result) noexcept {
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

[[nodiscard]] ResourceVectorSpec resource_vector_spec(const ResourceVector& source) {
  ResourceVectorSpec result{
    .host_normal_bytes = source.host_normal_bytes(),
    .host_pinned_bytes = source.host_pinned_bytes(),
    .host_hugepage_bytes = source.host_hugepage_bytes(),
    .shared_host_bytes = source.shared_host_bytes(),
    .spool_bytes = source.spool_bytes(),
    .transport_bytes = source.transport_bytes(),
    .descriptor_count = source.descriptor_count(),
    .async_token_count = source.async_token_count(),
    .cpu_leaf_permits = source.cpu_leaf_permits(),
    .backend_gang_permits = source.backend_gang_permits(),
    .provider_private_permits = source.provider_private_permits(),
    .io_slots = source.io_slots(),
  };
  result.devices.reserve(source.devices().size());
  for (const auto& device : source.devices()) {
    result.devices.push_back({.device_id = device.device_id(),
                              .device_bytes = device.device_bytes(),
                              .gpu_stream_slots = device.gpu_stream_slots(),
                              .copy_engine_slots = device.copy_engine_slots()});
  }
  return result;
}

[[nodiscard]] ksj::base::Result<std::shared_ptr<ResourceVectorLedger>>
make_local_plan_ledger(const ResourceVector& resources) {
  auto capacity = ResourceVectorCapacity::create(
    {.domains = resource_vector_spec(resources), .host_total_cap_bytes = resources.host_total_bytes()},
    "AdmittedPlanBoundDataPlane local plan budget");
  if (!capacity.ok()) {
    return capacity.status();
  }
  try {
    return std::make_shared<ResourceVectorLedger>(std::move(capacity).value());
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate AdmittedPlanBoundDataPlane local ledger");
  }
}

[[nodiscard]] bool is_host_normal_immutable_buffer(const TypeDescriptor& type) noexcept {
  return type.payload_kind() == PayloadKind::buffer_handle &&
         type.mutability() == PayloadMutability::immutable_after_publish &&
         std::find(type.allowed_memory_domains().begin(), type.allowed_memory_domains().end(),
                   TypeMemoryDomain::host_normal) != type.allowed_memory_domains().end();
}

[[nodiscard]] ksj::base::Status
validate_artifact_relation(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record,
                           const AdmissionRecord& admission_record,
                           const ResourceVectorLedgerReservation& admission_reservation) {
  if (verification_record.execution_plan_digest() != execution_plan.digest() ||
      verification_record.execution_profile() != execution_plan.execution_profile() ||
      !verification_record.verified_resource_vector().exactly_matches(execution_plan.resources()) ||
      verification_record.verified_terminal_occurrences() != execution_plan.terminal_occurrences()) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane VerificationRecord does not exactly bind the ExecutionPlan");
  }
  if (admission_record.outcome() != AdmissionOutcome::admitted ||
      admission_record.execution_plan_digest() != execution_plan.digest() ||
      admission_record.verification_record_digest() != verification_record.digest() ||
      !admission_record.reservation().exactly_matches(execution_plan.resources()) ||
      !admission_record.reservation().exactly_matches(verification_record.verified_resource_vector())) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane requires an admitted record with exact plan, verification, and resource binding");
  }
  if (!admission_reservation.valid() || !admission_reservation.committed() ||
      admission_reservation.amount() == nullptr ||
      !admission_reservation.amount()->exactly_matches(admission_record.reservation())) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane requires the one committed global reservation named by AdmissionRecord");
  }
  if (!is_currently_supported_in_process(execution_plan.execution_profile())) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane supports only currently supported in-process execution profiles");
  }
  if (!contains_identifier(execution_plan.proof_obligations(), kM37PlanBoundDataPlaneProofObligation) ||
      !contains_identifier(execution_plan.proof_obligations(), kM37SinglePhysicalPayloadChargeRuntimeAssumption) ||
      !contains_identifier(verification_record.verified_obligations(), kM37PlanBoundDataPlaneVerificationObligation) ||
      !contains_identifier(verification_record.verified_obligations(),
                           kM37SinglePhysicalPayloadChargeVerificationObligation)) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane requires the M3.7 plan-bound and single-payload-charge obligations");
  }
  return ksj::base::Status::Ok();
}

struct SelectedDataPlanePlans {
  const BufferPoolPlan* pool{nullptr};
  const DataEdgePlan* edge{nullptr};
  const ReorderPlan* reorder{nullptr};
};

[[nodiscard]] ksj::base::Result<SelectedDataPlanePlans>
select_narrow_data_plane_plans(const ExecutionPlan& execution_plan) {
  if (execution_plan.buffer_pool_plans().size() != 1U || execution_plan.data_edge_plans().size() != 1U ||
      execution_plan.reorder_plans().size() != 1U) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane M3.7 supports exactly one BufferPoolPlan, DataEdgePlan, and ReorderPlan");
  }
  const auto& pool = execution_plan.buffer_pool_plans().front();
  const auto& edge = execution_plan.data_edge_plans().front();
  const auto& reorder = execution_plan.reorder_plans().front();
  if (edge.source_pool_id() != pool.pool_id() || edge.producer_node_id() != pool.producer_node_id() ||
      edge.producer_port_name() != pool.producer_port_name() ||
      !edge.type_descriptor().exactly_matches(pool.type_descriptor()) ||
      !is_host_normal_immutable_buffer(pool.type_descriptor()) ||
      pool.memory_domain() != TypeMemoryDomain::host_normal ||
      pool.payload_alignment_bytes() != pool.type_descriptor().min_alignment_bytes() ||
      edge.max_items() != pool.slot_count() || edge.producer_abi_port() > std::numeric_limits<std::uint32_t>::max() ||
      edge.max_logical_bytes() == 0U || edge.terminal_policy() != kM37NormalEoiDrainCancellationFailTerminalPolicy ||
      reorder.node_id() != edge.producer_node_id() || reorder.ordered_output_port() != edge.producer_port_name() ||
      reorder.outputs_per_ordinal() != 1U || reorder.charged_bytes_per_ordinal() == 0U) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane pool/edge/reorder topology is not the M3.7 narrow one-output shape");
  }
  Quantity complete_slot_logical_bytes = 0U;
  if (!checked_add_quantity(pool.payload_capacity_bytes(), pool.metadata_capacity_bytes(),
                            complete_slot_logical_bytes) ||
      edge.max_logical_bytes() < complete_slot_logical_bytes ||
      reorder.charged_bytes_per_ordinal() != complete_slot_logical_bytes) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane DataEdgePlan/ReorderPlan cannot reserve one full BufferPoolPlan slot");
  }
  return SelectedDataPlanePlans{.pool = &pool, .edge = &edge, .reorder = &reorder};
}

[[nodiscard]] ksj::base::Status validate_runtime_storage_proof(const BufferPoolPlan& pool_plan,
                                                               const DataEdgePlan& edge_plan,
                                                               const PlanBoundDataPlaneStorage& storage) {
  const auto pool_control = fixed_buffer_pool_required_control_storage_bytes(pool_plan.slot_count());
  if (!pool_control.ok()) {
    return pool_control.status();
  }
  const auto edge_control = fixed_buffer_edge_required_control_storage_bytes(edge_plan.max_items());
  if (!edge_control.ok()) {
    return edge_control.status();
  }
  if (pool_control.value() > pool_plan.host_metadata_charged_bytes()) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane concrete FixedBufferPool control storage exceeds its frozen plan charge");
  }
  if (edge_control.value() > edge_plan.host_metadata_charged_bytes()) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane concrete FixedBufferEdge control storage exceeds its frozen plan charge");
  }

  std::size_t payload_bytes = 0U;
  std::size_t metadata_bytes = 0U;
  std::size_t external_bytes = 0U;
  if (!checked_multiply_size(pool_plan.slot_count(), pool_plan.payload_capacity_bytes(), payload_bytes) ||
      !checked_multiply_size(pool_plan.slot_count(), pool_plan.metadata_capacity_bytes(), metadata_bytes) ||
      !checked_add_size(payload_bytes, metadata_bytes, external_bytes) ||
      !checked_add_size(external_bytes, pool_control.value(), external_bytes) ||
      external_bytes > std::numeric_limits<Quantity>::max() ||
      static_cast<Quantity>(external_bytes) != pool_plan.physical_charge_bytes()) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane concrete BufferPool slabs do not exactly match the frozen physical charge");
  }
  if (storage.pool.payload.size() != payload_bytes || storage.pool.metadata.size() != metadata_bytes ||
      storage.pool.control.size() != pool_control.value() || storage.edge.control.size() != edge_control.value()) {
    return ksj::base::Status::InvalidArgument(
      "AdmittedPlanBoundDataPlane caller slabs must exactly match the selected fixed pool and edge bounds");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Result<ResourceVector> make_firing_lease_staging_reservation(const DataEdgePlan& edge_plan) {
  if (edge_plan.firing_lease_staging_charged_bytes() != kM37FiringLeaseHostStagingChargedBytes ||
      edge_plan.firing_lease_staging_descriptor_count() != kM37FiringLeaseHostStagingDescriptorCount) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane requires the exact frozen M3.7 firing-lease staging charge");
  }
  return ResourceVector::create({.host_normal_bytes = edge_plan.firing_lease_staging_charged_bytes(),
                                 .descriptor_count = edge_plan.firing_lease_staging_descriptor_count()},
                                "AdmittedPlanBoundDataPlane frozen firing-lease staging");
}

[[nodiscard]] std::uint8_t hex_value(const char value) noexcept {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(10U + value - 'a');
  }
  return 0xFFU;
}

[[nodiscard]] bool write_digest(const ArtifactDigest& source, ksj_digest256& destination) noexcept {
  constexpr std::size_t kPrefixBytes = 7U;
  constexpr std::size_t kHexBytes = KSJ_PROVIDER_DIGEST256_SIZE * 2U;
  const auto& encoded = source.value();
  if (encoded.size() != kPrefixBytes + kHexBytes || !std::string_view(encoded).starts_with("sha256:")) {
    return false;
  }
  destination = {};
  destination.abi = ksj_provider_abi_header_make(sizeof(destination), 0U);
  for (std::size_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    const auto high = hex_value(encoded[kPrefixBytes + index * 2U]);
    const auto low = hex_value(encoded[kPrefixBytes + index * 2U + 1U]);
    if (high == 0xFFU || low == 0xFFU) {
      return false;
    }
    destination.bytes[index] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  return true;
}

[[nodiscard]] bool digest_matches_loader_value(const ArtifactDigest& expected,
                                               const ksj::provider::loader::Digest256& actual) noexcept {
  ksj_digest256 encoded{};
  return write_digest(expected, encoded) && std::memcmp(encoded.bytes, actual.data(), KSJ_PROVIDER_DIGEST256_SIZE) == 0;
}

[[nodiscard]] ksj::base::Status
validate_frozen_provider_invocation(const BufferPoolPlan& pool_plan,
                                    const SynchronousProviderInvocation& invocation) noexcept {
  // The plan's provider provenance is a separate authorization boundary from
  // the exact ABI TypeDescriptor below.  Do this before reserving any pool or
  // edge credit, so a wrong loaded bundle/operator cannot perturb admitted
  // data-plane state merely by attempting a firing.
  if (!invocation.provider.valid() || invocation.provider.descriptor() == nullptr ||
      invocation.provider.api() == nullptr || invocation.operator_handle == nullptr ||
      invocation.execution_context == nullptr || invocation.key_state == nullptr) {
    return ksj::base::Status::ValidationError(
      "PlanBoundSynchronousOutputBridge requires a live Provider invocation matching the frozen pool producer");
  }
  const auto* const descriptor = invocation.provider.descriptor();
  if (descriptor->provider_id != pool_plan.producer_provider_id() ||
      !digest_matches_loader_value(pool_plan.producer_bundle_digest(), descriptor->bundle_digest) ||
      invocation.operator_id != pool_plan.producer_operator_id()) {
    return ksj::base::Status::ValidationError(
      "PlanBoundSynchronousOutputBridge Provider identity does not match the frozen BufferPoolPlan producer");
  }
  const auto found = std::find_if(descriptor->operators.begin(), descriptor->operators.end(),
                                  [&](const ksj::provider::loader::OperatorDescriptor& candidate) {
                                    return candidate.operator_id == pool_plan.producer_operator_id();
                                  });
  if (found == descriptor->operators.end() ||
      !digest_matches_loader_value(pool_plan.producer_contract_digest(), found->contract_digest)) {
    return ksj::base::Status::ValidationError(
      "PlanBoundSynchronousOutputBridge Provider operator contract does not match the frozen BufferPoolPlan");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Result<std::uint32_t> to_u32(const Quantity value, const char* const field_name) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    return ksj::base::Status::ValidationError(std::string("frozen TypeDescriptor ") + field_name +
                                              " cannot be represented by Provider ABI v1");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] ksj::base::Result<std::uint32_t> payload_kind_to_abi(const PayloadKind value) {
  switch (value) {
    case PayloadKind::buffer_handle:
      return KSJ_PAYLOAD_KIND_BUFFER_HANDLE;
    case PayloadKind::message_handle:
      return KSJ_PAYLOAD_KIND_MESSAGE_HANDLE;
    case PayloadKind::control_token:
      return KSJ_PAYLOAD_KIND_CONTROL_TOKEN;
    case PayloadKind::opaque_handle:
      return KSJ_PAYLOAD_KIND_OPAQUE_HANDLE;
  }
  return ksj::base::Status::ValidationError("frozen TypeDescriptor has an unknown payload kind");
}

[[nodiscard]] ksj::base::Result<std::uint32_t> element_type_to_abi(const ElementType value) {
  switch (value) {
    case ElementType::none:
      return KSJ_ELEMENT_TYPE_NONE;
    case ElementType::uint8:
      return KSJ_ELEMENT_TYPE_UINT8;
    case ElementType::int16:
      return KSJ_ELEMENT_TYPE_INT16;
    case ElementType::uint16:
      return KSJ_ELEMENT_TYPE_UINT16;
    case ElementType::int32:
      return KSJ_ELEMENT_TYPE_INT32;
    case ElementType::uint32:
      return KSJ_ELEMENT_TYPE_UINT32;
    case ElementType::float32:
      return KSJ_ELEMENT_TYPE_FLOAT32;
    case ElementType::float64:
      return KSJ_ELEMENT_TYPE_FLOAT64;
    case ElementType::complex_int16:
      return KSJ_ELEMENT_TYPE_COMPLEX_INT16;
    case ElementType::complex_float32:
      return KSJ_ELEMENT_TYPE_COMPLEX_FLOAT32;
    case ElementType::complex_float64:
      return KSJ_ELEMENT_TYPE_COMPLEX_FLOAT64;
  }
  return ksj::base::Status::ValidationError("frozen TypeDescriptor has an unknown element type");
}

[[nodiscard]] ksj::base::Result<std::uint64_t> layout_flags_to_abi(const LayoutKind layout, const StrideKind strides) {
  std::uint64_t flags = 0U;
  switch (layout) {
    case LayoutKind::canonical_contiguous:
      flags = KSJ_TYPE_LAYOUT_CANONICAL_CONTIGUOUS;
      break;
    case LayoutKind::channel_major_contiguous:
      flags = KSJ_TYPE_LAYOUT_CHANNEL_MAJOR_CONTIGUOUS;
      break;
    case LayoutKind::row_major_contiguous:
      flags = KSJ_TYPE_LAYOUT_ROW_MAJOR_CONTIGUOUS;
      break;
    case LayoutKind::column_major_contiguous:
      flags = KSJ_TYPE_LAYOUT_COLUMN_MAJOR_CONTIGUOUS;
      break;
    case LayoutKind::opaque:
      flags = KSJ_TYPE_LAYOUT_OPAQUE;
      break;
  }
  switch (strides) {
    case StrideKind::canonical:
      return flags | KSJ_TYPE_STRIDES_CANONICAL;
    case StrideKind::explicit_byte_strides:
      return flags | KSJ_TYPE_STRIDES_EXPLICIT_BYTE;
  }
  return ksj::base::Status::ValidationError("frozen TypeDescriptor has an unknown stride encoding");
}

[[nodiscard]] ksj::base::Result<std::uint32_t>
memory_domains_to_abi(const std::vector<TypeMemoryDomain>& memory_domains) {
  std::uint32_t result = 0U;
  for (const auto domain : memory_domains) {
    switch (domain) {
      case TypeMemoryDomain::host_normal:
      case TypeMemoryDomain::host_hugepage:
        result |= KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
        break;
      case TypeMemoryDomain::host_pinned:
        result |= KSJ_PROVIDER_MEMORY_HOST_PINNED;
        break;
      case TypeMemoryDomain::shared_host:
        result |= KSJ_PROVIDER_MEMORY_SHARED;
        break;
      case TypeMemoryDomain::cuda_device:
        result |= KSJ_PROVIDER_MEMORY_DEVICE;
        break;
    }
  }
  if (result == 0U) {
    return ksj::base::Status::ValidationError("frozen TypeDescriptor has no Provider ABI memory-domain bit");
  }
  return result;
}

} // namespace

namespace detail {

// Plan-owned ABI view backing storage.  It is intentionally private to this
// translation unit and has no constructor accepting a raw ksj_type_descriptor_view.
// The bridge can therefore hand a Provider only the exact frozen descriptor.
struct FrozenAbiTypeDescriptor final {
  std::string type_id;
  std::vector<std::string> dimensions;
  std::vector<ksj_utf8_view> dimension_views;
  ksj_type_descriptor_view view{};
};

// The context and every plan-bound sink lease share this move-only global
// admission token.  The token is intentionally not given to pool/edge/
// reorder components; they use the local mirror ledger instead.  Keeping it
// here prevents a context destructor from releasing the sole physical charge
// while a downstream edge consumer still exposes its BufferHandle.
struct PlanBoundAdmissionLifetime final {
  explicit PlanBoundAdmissionLifetime(ResourceVectorLedgerReservation value) noexcept : reservation(std::move(value)) {}

  ResourceVectorLedgerReservation reservation{};
};

} // namespace detail

namespace {

[[nodiscard]] ksj::base::Result<std::shared_ptr<const detail::FrozenAbiTypeDescriptor>>
make_frozen_abi_type_descriptor(const TypeDescriptor& source) {
  if (source.rank() > 8U || source.rank() != source.dimensions().size()) {
    return ksj::base::Status::ValidationError(
      "frozen TypeDescriptor rank cannot be represented by Provider ABI v1 output descriptor");
  }
  auto revision = to_u32(source.revision(), "revision");
  if (!revision.ok()) {
    return revision.status();
  }
  auto rank = to_u32(source.rank(), "rank");
  if (!rank.ok()) {
    return rank.status();
  }
  auto alignment = to_u32(source.min_alignment_bytes(), "min_alignment_bytes");
  if (!alignment.ok()) {
    return alignment.status();
  }
  auto payload_kind = payload_kind_to_abi(source.payload_kind());
  if (!payload_kind.ok()) {
    return payload_kind.status();
  }
  auto element_type = element_type_to_abi(source.element_type());
  if (!element_type.ok()) {
    return element_type.status();
  }
  auto layout_flags = layout_flags_to_abi(source.layout(), source.strides());
  if (!layout_flags.ok()) {
    return layout_flags.status();
  }
  auto memory_domains = memory_domains_to_abi(source.allowed_memory_domains());
  if (!memory_domains.ok()) {
    return memory_domains.status();
  }

  try {
    auto frozen = std::make_shared<detail::FrozenAbiTypeDescriptor>();
    frozen->type_id = source.type_id();
    frozen->dimensions = source.dimensions();
    frozen->dimension_views.reserve(frozen->dimensions.size());
    for (const auto& dimension : frozen->dimensions) {
      frozen->dimension_views.push_back({.abi = ksj_provider_abi_header_make(sizeof(ksj_utf8_view), 0U),
                                         .data = dimension.data(),
                                         .size = static_cast<std::uint64_t>(dimension.size())});
    }

    frozen->view = {};
    frozen->view.abi = ksj_provider_abi_header_make(sizeof(frozen->view), 0U);
    frozen->view.type_id = {.abi = ksj_provider_abi_header_make(sizeof(ksj_utf8_view), 0U),
                            .data = frozen->type_id.data(),
                            .size = static_cast<std::uint64_t>(frozen->type_id.size())};
    frozen->view.revision = revision.value();
    frozen->view.payload_kind = payload_kind.value();
    if (!write_digest(source.payload_schema_digest(), frozen->view.payload_schema_digest) ||
        !write_digest(source.abi_descriptor_digest(), frozen->view.descriptor_digest) ||
        !write_digest(source.metadata_schema_digest(), frozen->view.metadata_schema_digest)) {
      return ksj::base::Status::ValidationError(
        "frozen TypeDescriptor digest cannot be represented by Provider ABI v1 output descriptor");
    }
    frozen->view.element_type = element_type.value();
    frozen->view.rank = rank.value();
    frozen->view.dimension_names = frozen->dimension_views.empty() ? nullptr : frozen->dimension_views.data();
    frozen->view.layout_flags = layout_flags.value();
    if (source.strides() == StrideKind::explicit_byte_strides) {
      if (source.explicit_byte_strides().size() != source.rank()) {
        return ksj::base::Status::ValidationError(
          "frozen TypeDescriptor explicit stride count cannot be represented by Provider ABI v1");
      }
      for (std::size_t index = 0U; index < source.explicit_byte_strides().size(); ++index) {
        frozen->view.stride_bytes[index] = source.explicit_byte_strides()[index];
      }
    }
    frozen->view.allowed_memory_domains = memory_domains.value();
    frozen->view.minimum_alignment = alignment.value();
    frozen->view.mutability = source.mutability() == PayloadMutability::immutable_after_publish
                                ? KSJ_PAYLOAD_MUTABILITY_IMMUTABLE_AFTER_PUBLISH
                                : KSJ_PAYLOAD_MUTABILITY_MUTABLE_EXCLUSIVE;
    frozen->view.reserved0 = 0U;
    return std::shared_ptr<const detail::FrozenAbiTypeDescriptor>{std::move(frozen)};
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate frozen Provider ABI TypeDescriptor view");
  }
}

[[nodiscard]] bool is_output_reservation_only_scratch(const ResourceVector& reservation,
                                                      const std::uint64_t maximum_scratch_bytes) noexcept {
  return reservation.host_normal_bytes() == maximum_scratch_bytes && reservation.descriptor_count() == 0U &&
         reservation.host_pinned_bytes() == 0U && reservation.host_hugepage_bytes() == 0U &&
         reservation.shared_host_bytes() == 0U && reservation.spool_bytes() == 0U &&
         reservation.transport_bytes() == 0U && reservation.async_token_count() == 0U &&
         reservation.backend_gang_permits() == 0U && reservation.provider_private_permits() == 0U &&
         reservation.io_slots() == 0U && reservation.devices().empty() && reservation.cpu_leaf_permits() != 0U;
}

[[nodiscard]] SynchronousFiringResult bridge_contract_failure(const SynchronousFiringResult& source) noexcept {
  return {.outcome = SynchronousFiringOutcome::contract_violation,
          .provider_status = KSJ_STATUS_CONTRACT_VIOLATION,
          .consumed_input_item_count = source.consumed_input_item_count,
          .sealed_output_count = source.sealed_output_count,
          .committed_output_count = 0U,
          .sealed_output_bytes = source.sealed_output_bytes,
          .terminal_epoch = source.terminal_epoch};
}

[[nodiscard]] std::uint64_t frame_semantic_key_hash(const FrameSlotContext& context) noexcept {
  // This is a deterministic transport identity for the Provider ABI only;
  // M3 ordering itself remains bound to the non-forgeable FrameDispatch
  // ordinal. Mix individual fields rather than hashing raw struct bytes so
  // padding and host endianness never become part of the ABI contract.
  constexpr std::uint64_t kOffset = 1469598103934665603ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash = kOffset;
  const auto mix_u16 = [&hash](const std::uint16_t value) noexcept {
    hash ^= static_cast<std::uint8_t>(value & 0xFFU);
    hash *= kPrime;
    hash ^= static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    hash *= kPrime;
  };
  mix_u16(context.semantic_key.encoding_space);
  mix_u16(context.semantic_key.slice);
  mix_u16(context.semantic_key.contrast);
  mix_u16(context.semantic_key.repetition);
  mix_u16(context.semantic_key.set);
  mix_u16(context.semantic_key.phase);
  mix_u16(context.semantic_key.average);
  return hash;
}

[[nodiscard]] bool satisfies_alignment(const ksj::base::ConstByteSpan bytes,
                                       const std::uint32_t minimum_alignment) noexcept {
  return bytes.data() != nullptr && minimum_alignment != 0U &&
         reinterpret_cast<std::uintptr_t>(bytes.data()) % minimum_alignment == 0U;
}

} // namespace

PlanBoundM3ReorderIngress::PlanBoundM3ReorderIngress(AdmittedPlanBoundDataPlane* const owner) noexcept
    : owner_(owner) {}

PlanBoundM3ReorderIngress::~PlanBoundM3ReorderIngress() {
  release_noexcept();
}

PlanBoundM3ReorderIngress::PlanBoundM3ReorderIngress(PlanBoundM3ReorderIngress&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}

PlanBoundM3ReorderIngress& PlanBoundM3ReorderIngress::operator=(PlanBoundM3ReorderIngress&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    owner_ = std::exchange(other.owner_, nullptr);
  }
  return *this;
}

bool PlanBoundM3ReorderIngress::valid() const noexcept {
  return owner_ != nullptr && owner_->has_live_context_ingress();
}

ksj::base::Result<PlanBoundFrameDispatch> PlanBoundM3ReorderIngress::try_prepare(CompletedFrameLease& lease) {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("PlanBoundM3ReorderIngress is invalid or moved from");
  }
  auto dispatch = owner_->try_prepare_from_context_ingress(lease);
  if (!dispatch.ok()) {
    return dispatch.status();
  }
  return PlanBoundFrameDispatch{owner_, std::move(dispatch).value()};
}

void PlanBoundM3ReorderIngress::release_noexcept() noexcept {
  // This is only a facade. The context owns the ingress authority and keeps
  // it live for outstanding FrameDispatch/ordered-output capabilities; a
  // dropped facade must not create a second buffer or invalidate that state.
  owner_ = nullptr;
}

PlanBoundFrameDispatch::PlanBoundFrameDispatch(AdmittedPlanBoundDataPlane* const owner, FrameDispatch dispatch) noexcept
    : owner_(owner), dispatch_(std::move(dispatch)) {}

PlanBoundFrameDispatch::~PlanBoundFrameDispatch() {
  release_noexcept();
}

PlanBoundFrameDispatch::PlanBoundFrameDispatch(PlanBoundFrameDispatch&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), dispatch_(std::move(other.dispatch_)) {}

PlanBoundFrameDispatch& PlanBoundFrameDispatch::operator=(PlanBoundFrameDispatch&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    owner_ = std::exchange(other.owner_, nullptr);
    dispatch_ = std::move(other.dispatch_);
  }
  return *this;
}

bool PlanBoundFrameDispatch::valid() const noexcept {
  return owner_ != nullptr && owner_->owns_context_dispatch(dispatch_);
}

Quantity PlanBoundFrameDispatch::ordinal() const noexcept {
  return dispatch_.ordinal();
}

void PlanBoundFrameDispatch::release_noexcept() noexcept {
  if (owner_ == nullptr) {
    return;
  }
  try {
    if (dispatch_.valid()) {
      static_cast<void>(dispatch_.abort());
    }
    static_cast<void>(owner_->abort_no_bridge_check());
  } catch (...) {
    // A dropped context-bound dispatch must never leave a raw Provider/reorder
    // escape route alive. The embedded FrameDispatch also fails closed during
    // its own no-throw destruction.
  }
  disarm();
}

void PlanBoundFrameDispatch::disarm() noexcept {
  owner_ = nullptr;
  dispatch_ = FrameDispatch{};
}

PlanBoundSinkLease::PlanBoundSinkLease(std::shared_ptr<detail::PlanBoundAdmissionLifetime> admission_lifetime,
                                       FixedBufferEdgeConsumerLease edge_lease) noexcept
    : admission_lifetime_(std::move(admission_lifetime)), edge_lease_(std::move(edge_lease)) {}

PlanBoundSinkLease::~PlanBoundSinkLease() {
  release_noexcept();
}

PlanBoundSinkLease::PlanBoundSinkLease(PlanBoundSinkLease&& other) noexcept
    : admission_lifetime_(std::move(other.admission_lifetime_)), edge_lease_(std::move(other.edge_lease_)) {}

PlanBoundSinkLease& PlanBoundSinkLease::operator=(PlanBoundSinkLease&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    admission_lifetime_ = std::move(other.admission_lifetime_);
    edge_lease_ = std::move(other.edge_lease_);
  }
  return *this;
}

bool PlanBoundSinkLease::valid() const noexcept {
  return admission_lifetime_ != nullptr && admission_lifetime_->reservation.valid() &&
         admission_lifetime_->reservation.committed() && edge_lease_.valid();
}

ksj::base::Status PlanBoundSinkLease::acknowledge_consumed() {
  if (!valid()) {
    return ksj::base::Status::StateError("PlanBoundSinkLease is invalid or already settled");
  }
  const auto acknowledged = edge_lease_.acknowledge_consumed();
  if (!acknowledged.ok()) {
    return acknowledged;
  }
  // The handle/edge credit has been released before the admission lifetime.
  edge_lease_ = FixedBufferEdgeConsumerLease{};
  admission_lifetime_.reset();
  return ksj::base::Status::Ok();
}

void PlanBoundSinkLease::release_noexcept() noexcept {
  // The raw edge lease fails closed when unacknowledged.  Keep the admission
  // token until after that destructor-equivalent settlement returns the pool
  // handle and its local edge credit.
  edge_lease_ = FixedBufferEdgeConsumerLease{};
  admission_lifetime_.reset();
}

void PlanBoundSinkLease::disarm() noexcept {
  edge_lease_ = FixedBufferEdgeConsumerLease{};
  admission_lifetime_.reset();
}

AdmittedPlanBoundDataPlane::AdmittedPlanBoundDataPlane(
  ExecutionPlan execution_plan, VerificationRecord verification_record, BufferPoolPlan pool_plan,
  DataEdgePlan edge_plan, std::shared_ptr<detail::PlanBoundAdmissionLifetime> admission_lifetime,
  std::shared_ptr<ResourceVectorLedger> local_ledger, ResourceVectorLedgerReservation firing_lease_staging_reservation,
  std::unique_ptr<FixedBufferPool> pool, std::unique_ptr<FixedBufferEdge> edge,
  std::shared_ptr<const detail::FrozenAbiTypeDescriptor> input_type,
  std::shared_ptr<const detail::FrozenAbiTypeDescriptor> output_type) noexcept
    : admission_lifetime_(std::move(admission_lifetime)), local_ledger_(std::move(local_ledger)),
      firing_lease_staging_reservation_(std::move(firing_lease_staging_reservation)), pool_(std::move(pool)),
      edge_(std::move(edge)), input_type_(std::move(input_type)), output_type_(std::move(output_type)),
      execution_plan_(std::move(execution_plan)), verification_record_(std::move(verification_record)),
      pool_plan_(std::move(pool_plan)), edge_plan_(std::move(edge_plan)) {}

AdmittedPlanBoundDataPlane::~AdmittedPlanBoundDataPlane() {
  // The FixedBufferEdge destructor itself is fail-closed.  Explicitly abort
  // first so queued handles return to the pool before the local ledger and
  // then the one global admission reservation settle.
  static_cast<void>(abort_no_bridge_check());
}

ksj::base::Result<std::unique_ptr<AdmittedPlanBoundDataPlane>>
AdmittedPlanBoundDataPlane::create(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record,
                                   const AdmissionRecord& admission_record,
                                   ResourceVectorLedgerReservation admission_reservation,
                                   const PlanBoundDataPlaneStorage storage) {
  const auto artifacts =
    validate_artifact_relation(execution_plan, verification_record, admission_record, admission_reservation);
  if (!artifacts.ok()) {
    return artifacts;
  }
  auto selected = select_narrow_data_plane_plans(execution_plan);
  if (!selected.ok()) {
    return selected.status();
  }
  const auto storage_proof = validate_runtime_storage_proof(*selected.value().pool, *selected.value().edge, storage);
  if (!storage_proof.ok()) {
    return storage_proof;
  }
  auto output_type = make_frozen_abi_type_descriptor(selected.value().pool->type_descriptor());
  if (!output_type.ok()) {
    return output_type.status();
  }
  auto completed_frame_type = completed_frame_slot_context_type();
  if (!completed_frame_type.ok()) {
    return completed_frame_type.status();
  }
  auto input_type = make_frozen_abi_type_descriptor(completed_frame_type.value());
  if (!input_type.ok()) {
    return input_type.status();
  }
  auto local_ledger = make_local_plan_ledger(execution_plan.resources());
  if (!local_ledger.ok()) {
    return local_ledger.status();
  }
  auto staging_amount = make_firing_lease_staging_reservation(*selected.value().edge);
  if (!staging_amount.ok()) {
    return staging_amount.status();
  }
  auto staging_reservation = local_ledger.value()->try_reserve(staging_amount.value());
  if (!staging_reservation.ok()) {
    return staging_reservation.status();
  }
  const auto staging_committed = staging_reservation.value().commit();
  if (!staging_committed.ok()) {
    return staging_committed;
  }
  auto pool = FixedBufferPool::create({.occupancy_ledger = local_ledger.value(),
                                       .type_descriptor = selected.value().pool->type_descriptor(),
                                       .slot_count = selected.value().pool->slot_count(),
                                       .payload_capacity_bytes = selected.value().pool->payload_capacity_bytes(),
                                       .metadata_capacity_bytes = selected.value().pool->metadata_capacity_bytes()},
                                      storage.pool);
  if (!pool.ok()) {
    return pool.status();
  }
  auto edge =
    FixedBufferEdge::create({.occupancy_ledger = local_ledger.value(),
                             .source_pool = pool.value().get(),
                             .max_items = selected.value().edge->max_items(),
                             .max_logical_bytes = selected.value().edge->max_logical_bytes(),
                             .charged_control_storage_bytes = selected.value().edge->host_metadata_charged_bytes(),
                             .charged_descriptor_count = selected.value().edge->descriptor_charged_count()},
                            storage.edge);
  if (!edge.ok()) {
    return edge.status();
  }

  try {
    auto admission_lifetime = std::make_shared<detail::PlanBoundAdmissionLifetime>(std::move(admission_reservation));
    return std::unique_ptr<AdmittedPlanBoundDataPlane>(new AdmittedPlanBoundDataPlane(
      execution_plan, verification_record, *selected.value().pool, *selected.value().edge,
      std::move(admission_lifetime), std::move(local_ledger).value(), std::move(staging_reservation).value(),
      std::move(pool).value(), std::move(edge).value(), std::move(input_type).value(), std::move(output_type).value()));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate AdmittedPlanBoundDataPlane");
  }
}

ksj::base::Result<PlanBoundSynchronousOutputBridge>
AdmittedPlanBoundDataPlane::create_synchronous_one_output_bridge(PlanBoundSynchronousFiringConfig config) {
  if (bridge_active_ || pending_ordered_outputs_ != 0U || reorder_input_closed_) {
    return ksj::base::Status::StateError("AdmittedPlanBoundDataPlane permits only one M3.7 producer bridge and no "
                                         "replacement while ordered output is pending or after EndOfInput");
  }
  if (pool_ == nullptr || edge_ == nullptr || output_type_ == nullptr || local_ledger_ == nullptr ||
      input_type_ == nullptr || admission_lifetime_ == nullptr || !admission_lifetime_->reservation.valid() ||
      !admission_lifetime_->reservation.committed() || !firing_lease_staging_reservation_.valid() ||
      !firing_lease_staging_reservation_.committed()) {
    return ksj::base::Status::StateError("AdmittedPlanBoundDataPlane is not live");
  }
  if (config.maximum_input_batches != 1U || config.maximum_input_items != 1U || config.maximum_metadata_bytes == 0U ||
      !is_output_reservation_only_scratch(config.firing_reservation, config.maximum_scratch_bytes)) {
    return ksj::base::Status::ValidationError(
      "PlanBoundSynchronousOutputBridge requires one completed-frame input and a scratch/CPU-only firing reservation");
  }

  SynchronousFiringLeaseConfig host_config{local_ledger_, config.firing_reservation};
  host_config.maximum_input_batches = config.maximum_input_batches;
  host_config.maximum_input_items = config.maximum_input_items;
  host_config.maximum_output_grants = 1U;
  host_config.maximum_input_payload_bytes = config.maximum_input_payload_bytes;
  host_config.maximum_scratch_bytes = config.maximum_scratch_bytes;
  host_config.maximum_metadata_bytes = config.maximum_metadata_bytes;
  auto host = SynchronousFiringLeaseHost::create_preaccounted_staging(std::move(host_config));
  if (!host.ok()) {
    return host.status();
  }
  bridge_active_ = true;
  return PlanBoundSynchronousOutputBridge{this, std::move(host).value()};
}

ksj::base::Result<PlanBoundM3ReorderIngress> AdmittedPlanBoundDataPlane::create_m3_reorder_ingress(
  const std::string_view producer_node_id, HostFrameAssembler& assembler, const FixedReorderBufferStorage storage) {
  if (local_ledger_ == nullptr || edge_ == nullptr || admission_lifetime_ == nullptr ||
      !admission_lifetime_->reservation.valid() || !admission_lifetime_->reservation.committed() ||
      execution_plan_.reorder_plans().size() != 1U || producer_node_id != edge_plan_.producer_node_id() ||
      execution_plan_.reorder_plans().front().node_id() != producer_node_id) {
    return ksj::base::Status::ValidationError(
      "AdmittedPlanBoundDataPlane reorder ingress must bind its one frozen producer node and local plan ledger");
  }
  if (reorder_buffer_ != nullptr || reorder_ingress_.has_value()) {
    return ksj::base::Status::StateError(
      "AdmittedPlanBoundDataPlane permits exactly one context-owned M3 reorder buffer and ingress");
  }
  auto created = FixedReorderBuffer::create(execution_plan_, verification_record_, producer_node_id, storage,
                                            {.resource_ledger = local_ledger_});
  if (!created.ok()) {
    return created.status();
  }

  // M3ReorderIngress retains the exact FixedReorderBuffer address for every
  // FrameDispatch it issues. Move the buffer into its final context-owned
  // allocation before binding the ingress; binding a Result-local movable
  // buffer would leave the ingress pointing at a moved-from object.
  try {
    reorder_buffer_ = std::make_unique<FixedReorderBuffer>(std::move(created).value());
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to retain context-owned FixedReorderBuffer");
  }
  auto ingress =
    M3ReorderIngress::create(execution_plan_, verification_record_, producer_node_id, assembler, *reorder_buffer_);
  if (!ingress.ok()) {
    // M3ReorderIngress::create rolls back its temporary binding before it
    // returns an error. Releasing this still-unbound buffer also returns its
    // local plan-ledger reservation and caller-slab claim.
    reorder_buffer_.reset();
    return ingress.status();
  }

  // Both remaining moves are noexcept. The context keeps the buffer before
  // the ingress in declaration order, so reverse teardown destroys the raw
  // ingress authority before its buffer and preserves the coupled host
  // lifetime.
  reorder_ingress_.emplace(std::move(ingress).value());
  reorder_input_closed_ = false;
  return PlanBoundM3ReorderIngress{this};
}

PlanBoundSinkPoll AdmittedPlanBoundDataPlane::try_acquire_for_sink() {
  if (edge_ == nullptr || admission_lifetime_ == nullptr || !admission_lifetime_->reservation.valid() ||
      !admission_lifetime_->reservation.committed()) {
    return {.kind = FixedBufferEdgePollKind::failed};
  }
  auto polled = edge_->try_acquire();
  if (polled.kind != FixedBufferEdgePollKind::item) {
    return {.kind = polled.kind};
  }
  if (!polled.lease.has_value()) {
    return {.kind = FixedBufferEdgePollKind::failed};
  }
  PlanBoundSinkPoll result{.kind = FixedBufferEdgePollKind::item};
  result.lease.emplace(PlanBoundSinkLease{admission_lifetime_, std::move(*polled.lease)});
  return result;
}

FixedBufferEdgeSnapshot AdmittedPlanBoundDataPlane::edge_snapshot() const {
  return edge_ == nullptr ? FixedBufferEdgeSnapshot{} : edge_->snapshot();
}

ksj::base::Status AdmittedPlanBoundDataPlane::abort_no_bridge_check() {
  if (edge_ == nullptr) {
    return ksj::base::Status::StateError("AdmittedPlanBoundDataPlane has no live edge");
  }
  ksj::base::Status reorder_status = ksj::base::Status::Ok();
  if (reorder_ingress_.has_value()) {
    reorder_status = reorder_ingress_->abort();
  }
  const auto edge_status = edge_->abort();
  return !reorder_status.ok() ? reorder_status : edge_status;
}

ksj::base::Status AdmittedPlanBoundDataPlane::end_of_input_from_bridge() {
  if (edge_ == nullptr || !reorder_ingress_.has_value() || reorder_buffer_ == nullptr) {
    return ksj::base::Status::StateError(
      "PlanBoundSynchronousOutputBridge EndOfInput requires its context-owned M3 reorder ingress");
  }
  if (reorder_input_closed_) {
    return ksj::base::Status::StateError("PlanBoundSynchronousOutputBridge EndOfInput was already accepted");
  }
  // EOI is source/reorder first. FixedBufferEdge can accept ordered commits
  // while close-pending, so closing it only after the upstream fence has been
  // accepted cannot truncate an out-of-order reorder completion.
  const auto upstream_closed = reorder_ingress_->end_of_input();
  if (!upstream_closed.ok()) {
    // A retryable source/reorder quiescence miss preserves the open edge and
    // all detached credits. Any other upstream terminal failure (notably a
    // strict-dense gap at EOI) must fail the coupled downstream edge too;
    // otherwise a stale queued prefix could look like normal completed output.
    if (upstream_closed.code() != ksj::base::StatusCode::unavailable) {
      static_cast<void>(edge_->abort());
    }
    return upstream_closed;
  }
  reorder_input_closed_ = true;
  const auto edge_closed = edge_->end_of_input();
  if (!edge_closed.ok()) {
    static_cast<void>(abort_no_bridge_check());
  }
  return edge_closed;
}

ksj::base::Result<FrameDispatch>
AdmittedPlanBoundDataPlane::try_prepare_from_context_ingress(CompletedFrameLease& lease) {
  if (!has_live_context_ingress()) {
    return ksj::base::Status::StateError("PlanBoundM3ReorderIngress is no longer the context-owned accepting ingress");
  }
  return reorder_ingress_->try_prepare(lease);
}

bool AdmittedPlanBoundDataPlane::owns_context_dispatch(const FrameDispatch& dispatch) const noexcept {
  return reorder_buffer_ != nullptr && reorder_ingress_.has_value() && dispatch.valid() &&
         dispatch.buffer_ == reorder_buffer_.get() && dispatch.ingress_identity_ != 0U &&
         dispatch.ingress_identity_ == reorder_ingress_->ingress_identity_;
}

bool AdmittedPlanBoundDataPlane::has_live_context_ingress() const noexcept {
  return reorder_buffer_ != nullptr && reorder_ingress_.has_value() && !reorder_input_closed_ &&
         reorder_ingress_->bound_ && !reorder_ingress_->terminal_ &&
         reorder_ingress_->reorder_buffer_ == reorder_buffer_.get() && reorder_ingress_->ingress_identity_ != 0U;
}

ksj::base::Status AdmittedPlanBoundDataPlane::retain_pending_ordered_output() {
  if (pending_ordered_outputs_ == std::numeric_limits<Quantity>::max()) {
    return ksj::base::Status::Unavailable("AdmittedPlanBoundDataPlane ordered-output capability counter is exhausted");
  }
  ++pending_ordered_outputs_;
  return ksj::base::Status::Ok();
}

void AdmittedPlanBoundDataPlane::release_pending_ordered_output_noexcept() noexcept {
  if (pending_ordered_outputs_ == 0U) {
    try {
      static_cast<void>(abort_no_bridge_check());
    } catch (...) {}
    return;
  }
  --pending_ordered_outputs_;
}

void AdmittedPlanBoundDataPlane::release_bridge_noexcept(const bool normal_end_of_input) noexcept {
  bridge_active_ = false;
  if (!normal_end_of_input) {
    try {
      static_cast<void>(abort_no_bridge_check());
    } catch (...) {
      // No ordered output may survive a dropped producer bridge even if a
      // platform mutex/system exception prevents reporting the abort. This
      // applies equally while a PlanBoundOrderedOutput holds a detached edge
      // credit: later publication would otherwise revive a half-closed scan.
    }
  }
}

PlanBoundOrderedOutput::PlanBoundOrderedOutput(AdmittedPlanBoundDataPlane* owner, FrameDispatch dispatch,
                                               FixedBufferEdgeProducerReservation edge_reservation) noexcept
    : owner_(owner), dispatch_(std::move(dispatch)), edge_reservation_(std::move(edge_reservation)) {}

PlanBoundOrderedOutput::~PlanBoundOrderedOutput() {
  release_noexcept();
}

PlanBoundOrderedOutput::PlanBoundOrderedOutput(PlanBoundOrderedOutput&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), dispatch_(std::move(other.dispatch_)),
      edge_reservation_(std::move(other.edge_reservation_)) {}

PlanBoundOrderedOutput& PlanBoundOrderedOutput::operator=(PlanBoundOrderedOutput&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    owner_ = std::exchange(other.owner_, nullptr);
    dispatch_ = std::move(other.dispatch_);
    edge_reservation_ = std::move(other.edge_reservation_);
  }
  return *this;
}

bool PlanBoundOrderedOutput::valid() const noexcept {
  return owner_ != nullptr && dispatch_.valid() && edge_reservation_.valid();
}

Quantity PlanBoundOrderedOutput::ordinal() const noexcept {
  return dispatch_.ordinal();
}

ksj::base::Status PlanBoundOrderedOutput::try_publish() {
  if (!valid()) {
    return ksj::base::Status::StateError("PlanBoundOrderedOutput is invalid or already settled");
  }
  auto publish = dispatch_.try_acquire_publish();
  if (!publish.ok()) {
    if (publish.status().code() == ksj::base::StatusCode::unavailable) {
      return publish.status();
    }
    const auto failure = publish.status();
    static_cast<void>(abort());
    return failure;
  }
  const auto committed = publish.value().commit_to_edge(edge_reservation_);
  if (!committed.ok()) {
    static_cast<void>(abort());
    return committed;
  }

  owner_->release_pending_ordered_output_noexcept();
  disarm();
  return ksj::base::Status::Ok();
}

ksj::base::Status PlanBoundOrderedOutput::abort() {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("PlanBoundOrderedOutput is invalid or moved from");
  }
  ksj::base::Status dispatch_status = ksj::base::Status::Ok();
  if (dispatch_.valid()) {
    dispatch_status = dispatch_.abort();
  }
  const auto edge_status = owner_->abort_no_bridge_check();
  owner_->release_pending_ordered_output_noexcept();
  disarm();
  return !dispatch_status.ok() ? dispatch_status : edge_status;
}

void PlanBoundOrderedOutput::release_noexcept() noexcept {
  if (owner_ == nullptr) {
    return;
  }
  try {
    if (dispatch_.valid()) {
      static_cast<void>(dispatch_.abort());
    }
    static_cast<void>(owner_->abort_no_bridge_check());
    owner_->release_pending_ordered_output_noexcept();
  } catch (...) {
    // A dropped ordered output cannot leave its pre-reserved edge credit or
    // retained reorder handle observable as ordinary output.
  }
  disarm();
}

void PlanBoundOrderedOutput::disarm() noexcept {
  owner_ = nullptr;
  dispatch_ = FrameDispatch{};
  edge_reservation_ = FixedBufferEdgeProducerReservation{};
}

PlanBoundSynchronousOutputBridge::PlanBoundSynchronousOutputBridge(AdmittedPlanBoundDataPlane* owner,
                                                                   SynchronousFiringLeaseHost host) noexcept
    : owner_(owner), host_(std::move(host)) {}

PlanBoundSynchronousOutputBridge::PlanBoundSynchronousOutputBridge(PlanBoundSynchronousOutputBridge&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), host_(std::move(other.host_)),
      normal_end_of_input_(std::exchange(other.normal_end_of_input_, false)) {}

PlanBoundSynchronousOutputBridge&
PlanBoundSynchronousOutputBridge::operator=(PlanBoundSynchronousOutputBridge&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    owner_ = std::exchange(other.owner_, nullptr);
    host_ = std::move(other.host_);
    normal_end_of_input_ = std::exchange(other.normal_end_of_input_, false);
  }
  return *this;
}

PlanBoundSynchronousOutputBridge::~PlanBoundSynchronousOutputBridge() {
  release_noexcept();
}

bool PlanBoundSynchronousOutputBridge::valid() const noexcept {
  return owner_ != nullptr;
}

ksj::base::Result<PlanBoundReorderFiringResult>
PlanBoundSynchronousOutputBridge::process_reorder(const SynchronousProviderInvocation& invocation,
                                                  PlanBoundFrameDispatch& plan_dispatch,
                                                  const PlanBoundReorderFiringRequest& request) {
  if (owner_ == nullptr || normal_end_of_input_) {
    return ksj::base::Status::StateError("PlanBoundSynchronousOutputBridge is invalid or its input is closed");
  }
  if (owner_->pool_ == nullptr || owner_->edge_ == nullptr || owner_->input_type_ == nullptr ||
      owner_->output_type_ == nullptr || !owner_->has_live_context_ingress() || plan_dispatch.owner_ != owner_ ||
      !plan_dispatch.valid() || !owner_->owns_context_dispatch(plan_dispatch.dispatch_)) {
    return ksj::base::Status::StateError(
      "PlanBoundSynchronousOutputBridge requires its one live context-owned M3 ingress and FrameDispatch");
  }
  const auto provider_binding = validate_frozen_provider_invocation(owner_->pool_plan_, invocation);
  if (!provider_binding.ok()) {
    return provider_binding;
  }
  auto& dispatch = plan_dispatch.dispatch_;

  const auto fail_dispatch_and_plane = [&]() noexcept {
    try {
      if (dispatch.valid()) {
        static_cast<void>(dispatch.abort());
      }
      static_cast<void>(owner_->abort_no_bridge_check());
    } catch (...) {}
    plan_dispatch.disarm();
  };

  Quantity full_slot_logical_bytes = 0U;
  if (!checked_add_quantity(owner_->pool_plan_.payload_capacity_bytes(), owner_->pool_plan_.metadata_capacity_bytes(),
                            full_slot_logical_bytes)) {
    fail_dispatch_and_plane();
    return ksj::base::Status::InternalError("PlanBoundSynchronousOutputBridge full slot accounting overflowed");
  }

  // Reserve all output capacity before committing the reorder permit or
  // entering Provider code. A normal capacity miss leaves `dispatch` prepared
  // and retryable, because neither it nor the Provider has observed work.
  auto edge_reservation = owner_->edge_->try_reserve(full_slot_logical_bytes);
  if (!edge_reservation.ok()) {
    return edge_reservation.status();
  }
  auto mutable_lease = owner_->pool_->try_acquire();
  if (!mutable_lease.ok()) {
    return mutable_lease.status();
  }
  auto payload = mutable_lease.value().writable_payload();
  auto metadata = mutable_lease.value().writable_metadata();
  if (!payload.ok() || !metadata.ok()) {
    fail_dispatch_and_plane();
    return !payload.ok() ? payload.status() : metadata.status();
  }

  const auto dispatch_committed = dispatch.commit();
  if (!dispatch_committed.ok()) {
    fail_dispatch_and_plane();
    return dispatch_committed;
  }
  const auto input_bytes = dispatch.input_bytes();
  const auto input_context = dispatch.input_context();
  if (!input_bytes.ok() || !input_context.ok() ||
      !satisfies_alignment(input_bytes.ok() ? input_bytes.value() : ksj::base::ConstByteSpan{},
                           owner_->input_type_->view.minimum_alignment)) {
    const auto failure = !input_bytes.ok() ? input_bytes.status()
                         : !input_context.ok()
                           ? input_context.status()
                           : ksj::base::Status::ValidationError(
                               "FrameDispatch bytes do not meet the frozen completed-frame ABI alignment");
    fail_dispatch_and_plane();
    return failure;
  }

  const auto semantic_hash = frame_semantic_key_hash(input_context.value());
  const auto ordinal = dispatch.ordinal();
  std::array<SynchronousInputItem, 1U> input_items{{
    {.payload = input_bytes.value(),
     .metadata = {},
     .type = owner_->input_type_->view,
     .semantic_key_hash = semantic_hash,
     .order_key = input_context.value().order_key,
     .item_ordinal = ordinal},
  }};
  std::array<SynchronousInputBatch, 1U> input_batches{{
    {.items = input_items,
     // M3.7 binds exactly one completed-frame input. The sole ABI input port
     // is therefore zero; callers cannot select a raw alternate port.
     .input_port = 0U,
     .batch_id = ordinal,
     .order_domain = 0U},
  }};
  std::array<SynchronousOutputGrantSpec, 1U> output_grants{{
    {.storage = payload.value(),
     .metadata_storage = metadata.value(),
     .output_port = static_cast<std::uint32_t>(owner_->edge_plan_.producer_abi_port()),
     .maximum_item_count = 1U,
     .required_type = owner_->output_type_->view},
  }};

  bool output_completed = false;
  bool output_commit_failed = false;
  SynchronousFiringRequest host_request{
    .resource_occurrence_id = request.resource_occurrence_id,
    .slot_generation = request.slot_generation,
    .terminal_epoch = request.terminal_epoch,
    .input_batches = input_batches,
    .output_grants = output_grants,
    .scratch = request.scratch,
  };
  host_request.commit_outputs =
    [&mutable_lease, &dispatch, &output_completed, &output_commit_failed, this, expected_semantic_hash = semantic_hash,
     expected_order_key = input_context.value().order_key](const std::span<const SynchronousSealedOutput> outputs) {
      if (outputs.size() != 1U || outputs.front().output_slot != 0U ||
          outputs.front().descriptor.output_port != owner_->edge_plan_.producer_abi_port() ||
          outputs.front().descriptor.produced_item_count != 1U ||
          outputs.front().descriptor.semantic_key_hash != expected_semantic_hash ||
          outputs.front().descriptor.order_key != expected_order_key || !mutable_lease.value().valid() ||
          !dispatch.valid()) {
        output_commit_failed = true;
        return ksj::base::Status::ValidationError(
          "PlanBoundSynchronousOutputBridge requires one exact sealed output for its FrameDispatch identity");
      }
      const auto metadata_data = static_cast<const ksj::base::byte*>(outputs.front().descriptor.metadata.data);
      auto sealed = mutable_lease.value().seal(
        owner_->pool_plan_.type_descriptor(), outputs.front().descriptor.produced_byte_count,
        ksj::base::ConstByteSpan{metadata_data, static_cast<std::size_t>(outputs.front().descriptor.metadata.size)});
      if (!sealed.ok()) {
        output_commit_failed = true;
        return sealed.status();
      }
      auto handle = std::move(sealed).value();
      const auto completed = dispatch.complete(handle);
      if (!completed.ok()) {
        output_commit_failed = true;
        return completed;
      }
      output_completed = true;
      return ksj::base::Status::Ok();
    };

  const auto result = host_.process_preaccounted_output(invocation, host_request);
  if (!result.ok()) {
    // Once the dispatch was committed, even a host-side pre-callback failure
    // is no longer retryable: fail the coupled source/reorder/edge path.
    fail_dispatch_and_plane();
    return result.status();
  }
  if (result.value().outcome != SynchronousFiringOutcome::done || output_commit_failed || !output_completed ||
      result.value().sealed_output_count != 1U || result.value().committed_output_count != 1U) {
    fail_dispatch_and_plane();
    return PlanBoundReorderFiringResult{
      .firing = result.value().outcome == SynchronousFiringOutcome::done ? bridge_contract_failure(result.value())
                                                                         : result.value(),
    };
  }
  const auto retained = owner_->retain_pending_ordered_output();
  if (!retained.ok()) {
    fail_dispatch_and_plane();
    return PlanBoundReorderFiringResult{.firing = bridge_contract_failure(result.value())};
  }
  auto ordered_output = PlanBoundOrderedOutput{owner_, std::move(dispatch), std::move(edge_reservation).value()};
  plan_dispatch.disarm();
  return PlanBoundReorderFiringResult{.firing = result.value(), .ordered_output = std::move(ordered_output)};
}

ksj::base::Status PlanBoundSynchronousOutputBridge::end_of_input() {
  if (owner_ == nullptr || normal_end_of_input_) {
    return ksj::base::Status::StateError("PlanBoundSynchronousOutputBridge EndOfInput is invalid or already applied");
  }
  const auto closed = owner_->end_of_input_from_bridge();
  if (closed.ok()) {
    normal_end_of_input_ = true;
  }
  return closed;
}

ksj::base::Status PlanBoundSynchronousOutputBridge::abort() {
  if (owner_ == nullptr) {
    return ksj::base::Status::StateError("PlanBoundSynchronousOutputBridge is invalid or moved from");
  }
  const auto aborted = owner_->abort_no_bridge_check();
  if (aborted.ok()) {
    normal_end_of_input_ = false;
  }
  return aborted;
}

void PlanBoundSynchronousOutputBridge::release_noexcept() noexcept {
  if (owner_ != nullptr) {
    owner_->release_bridge_noexcept(normal_end_of_input_);
  }
  disarm();
}

void PlanBoundSynchronousOutputBridge::disarm() noexcept {
  owner_ = nullptr;
  normal_end_of_input_ = false;
}

} // namespace ksj::recon::runtime
