#include "kspacejet/recon/runtime/synchronous_firing_lease.hpp"

#include "kspacejet/recon/execution_plan.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

// Firing leases and output grants are ABI-opaque to every Provider.  The host
// owns their concrete representation in this translation unit only.
struct ksj_firing_lease {
  std::uint64_t magic{0U};
};

struct ksj_output_grant {
  std::uint64_t magic{0U};
  std::uint32_t slot{0U};
};

namespace ksj::recon::runtime {
namespace {

constexpr std::uint64_t kLeaseMagic = 0x4B534A4C45415345ULL; // "KSJLEASE"
constexpr std::uint64_t kGrantMagic = 0x4B534A4752414E54ULL; // "KSJGRANT"
constexpr std::uint64_t kMaximumTypeStringBytes = 4U * 1024U;

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                  const std::uint64_t capability_bits = 0U) noexcept {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] bool has_compatible_header(const ksj_provider_abi_header* header,
                                         const std::size_t required_size) noexcept {
  return header != nullptr && header->struct_size >= required_size && header->reserved0 == 0U &&
         header->reserved[0] == 0U && header->reserved[1] == 0U;
}

template <typename T> [[nodiscard]] bool has_full_compatible_header(const T* value) noexcept {
  return value != nullptr && has_compatible_header(&value->abi, sizeof(T));
}

[[nodiscard]] SynchronousProviderFailureDetail
capture_provider_failure_detail(const ksj_error_view& raw_error) noexcept {
  SynchronousProviderFailureDetail result;
  if (!has_full_compatible_header(&raw_error)) {
    return result;
  }
  result.status = raw_error.status;
  const auto& message = raw_error.message;
  if (!has_full_compatible_header(&message) || message.size == 0U || message.data == nullptr) {
    return result;
  }
  const auto capacity = static_cast<std::uint64_t>(result.message_storage.size());
  const auto copied = std::min(message.size, capacity);
  std::memcpy(result.message_storage.data(), message.data, static_cast<std::size_t>(copied));
  result.message_bytes = static_cast<std::uint32_t>(copied);
  result.message_truncated = message.size > copied;
  return result;
}

[[nodiscard]] bool is_power_of_two(const std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] std::uint32_t pointer_alignment(const void* pointer) noexcept {
  if (pointer == nullptr) {
    return 1U;
  }
  const auto address = reinterpret_cast<std::uintptr_t>(pointer);
  if (address == 0U) {
    return 1U;
  }

  std::uint32_t alignment = 1U;
  while (alignment < (std::numeric_limits<std::uint32_t>::max() / 2U) &&
         (address & (static_cast<std::uintptr_t>(alignment) << 1U) - 1U) == 0U) {
    alignment <<= 1U;
  }
  return alignment;
}

[[nodiscard]] bool checked_add(const std::uint64_t lhs, const std::uint64_t rhs, std::uint64_t& result) noexcept {
  if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

[[nodiscard]] bool checked_multiply(const std::uint64_t lhs, const std::uint64_t rhs, std::uint64_t& result) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

[[nodiscard]] bool valid_borrowed_utf8_view(const ksj_utf8_view& view, const std::uint64_t maximum_bytes,
                                            const bool allow_empty) noexcept {
  return has_full_compatible_header(&view) && view.size <= maximum_bytes &&
         (view.size == 0U ? allow_empty : view.data != nullptr);
}

[[nodiscard]] bool valid_borrowed_byte_view(const ksj_byte_view& view, const std::uint64_t maximum_bytes) noexcept {
  return has_full_compatible_header(&view) && view.size <= maximum_bytes && (view.size == 0U || view.data != nullptr);
}

[[nodiscard]] bool valid_digest(const ksj_digest256& digest) noexcept {
  return has_full_compatible_header(&digest);
}

[[nodiscard]] bool valid_host_type(const ksj_type_descriptor_view& type) noexcept {
  if (!has_full_compatible_header(&type) || !valid_borrowed_utf8_view(type.type_ref, kMaximumTypeStringBytes, false) ||
      !valid_digest(type.type_identity_digest) || type.rank > std::size(type.stride_bytes) ||
      type.minimum_alignment == 0U || !is_power_of_two(type.minimum_alignment) ||
      (type.allowed_memory_domains & KSJ_PROVIDER_MEMORY_HOST_PAGEABLE) == 0U) {
    return false;
  }
  if (type.rank != 0U && type.dimension_names == nullptr) {
    return false;
  }
  for (std::uint32_t index = 0U; index < type.rank; ++index) {
    if (!valid_borrowed_utf8_view(type.dimension_names[index], kMaximumTypeStringBytes, false)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool type_descriptors_match(const ksj_type_descriptor_view& lhs,
                                          const ksj_type_descriptor_view& rhs) noexcept {
  if (!valid_host_type(lhs) || !valid_host_type(rhs) || lhs.type_ref.size != rhs.type_ref.size ||
      std::memcmp(lhs.type_ref.data, rhs.type_ref.data, static_cast<std::size_t>(lhs.type_ref.size)) != 0 ||
      lhs.payload_kind != rhs.payload_kind ||
      std::memcmp(lhs.type_identity_digest.bytes, rhs.type_identity_digest.bytes, KSJ_PROVIDER_DIGEST256_SIZE) != 0 ||
      lhs.element_type != rhs.element_type || lhs.rank != rhs.rank || lhs.layout_flags != rhs.layout_flags ||
      lhs.allowed_memory_domains != rhs.allowed_memory_domains || lhs.minimum_alignment != rhs.minimum_alignment ||
      lhs.mutability != rhs.mutability ||
      std::memcmp(lhs.stride_bytes, rhs.stride_bytes, sizeof(lhs.stride_bytes)) != 0) {
    return false;
  }
  for (std::uint32_t index = 0U; index < lhs.rank; ++index) {
    const auto& left = lhs.dimension_names[index];
    const auto& right = rhs.dimension_names[index];
    if (left.size != right.size || std::memcmp(left.data, right.data, static_cast<std::size_t>(left.size)) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_host_only_reservation(const ksj::recon::ResourceVector& reservation) noexcept {
  return reservation.host_pinned_bytes() == 0U && reservation.host_hugepage_bytes() == 0U &&
         reservation.shared_host_bytes() == 0U && reservation.spool_bytes() == 0U &&
         reservation.transport_bytes() == 0U && reservation.async_token_count() == 0U &&
         reservation.backend_gang_permits() == 0U && reservation.provider_private_permits() == 0U &&
         reservation.io_slots() == 0U && reservation.devices().empty();
}

[[nodiscard]] ksj::base::Result<ksj::recon::ResourceVector>
make_persistent_staging_reservation(const std::uint64_t host_bytes, const std::uint64_t descriptor_count) {
  return ksj::recon::ResourceVector::create(
    {
      .host_normal_bytes = host_bytes,
      .descriptor_count = descriptor_count,
    },
    "SynchronousFiringLeaseHost persistent staging reservation");
}

[[nodiscard]] ksj::base::Result<ksj::recon::ResourceVector>
combine_persistent_and_firing_reservations(const ksj::recon::ResourceVector& persistent,
                                           const ksj::recon::ResourceVector& firing) {
  std::uint64_t host_normal_bytes = 0U;
  std::uint64_t descriptor_count = 0U;
  if (!checked_add(persistent.host_normal_bytes(), firing.host_normal_bytes(), host_normal_bytes) ||
      !checked_add(persistent.descriptor_count(), firing.descriptor_count(), descriptor_count)) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost persistent and firing ResourceVector accounting overflows");
  }
  return ksj::recon::ResourceVector::create(
    {
      .host_normal_bytes = host_normal_bytes,
      .descriptor_count = descriptor_count,
      .cpu_leaf_permits = firing.cpu_leaf_permits(),
    },
    "SynchronousFiringLeaseHost combined persistent and firing reservation");
}

[[nodiscard]] bool contains_operator(const ksj::provider::loader::ProviderDescriptor& descriptor,
                                     const std::string_view operator_id,
                                     const ksj::provider::loader::OperatorDescriptor*& result) noexcept {
  const auto found =
    std::find_if(descriptor.operators.begin(), descriptor.operators.end(), [operator_id](const auto& candidate) {
      return candidate.operator_id == operator_id;
    });
  if (found == descriptor.operators.end()) {
    return false;
  }
  result = &*found;
  return true;
}

[[nodiscard]] bool known_outcome(const ksj_provider_process_outcome outcome) noexcept {
  return outcome == KSJ_PROVIDER_PROCESS_DONE || outcome == KSJ_PROVIDER_PROCESS_YIELD ||
         outcome == KSJ_PROVIDER_PROCESS_ASYNC_PENDING || outcome == KSJ_PROVIDER_PROCESS_STRUCTURED_FAILURE ||
         outcome == KSJ_PROVIDER_PROCESS_CONTRACT_VIOLATION;
}

} // namespace

struct SynchronousFiringLeaseHost::Impl {
  enum class OutputGrantAccounting : std::uint8_t {
    // The public SynchronousFiringLeaseHost API receives arbitrary caller
    // spans, so its firing ResourceVector must include every output byte.
    externally_supplied,
    // Only the private graph executor may select this path. Its output
    // spans are aliases of a BufferPool slot whose physical charge is already
    // held by the admitted data plane; dynamic firing accounting covers
    // scratch/CPU work but deliberately not the same payload a second time.
    preaccounted_pool_slot,
  };

  enum class ScratchAccounting : std::uint8_t {
    // The public raw-span API receives caller-owned scratch and therefore
    // reserves it for the callback duration.
    externally_supplied,
    // The graph executor supplies an exact frozen node scratch slab whose
    // lifetime credit is already held by the admitted graph.
    preaccounted_graph_slab,
  };

  enum class GrantStateKind : std::uint8_t {
    available,
    acquired,
    mapped,
    sealed,
    released,
  };

  struct GrantState {
    ksj_output_grant abi_grant{};
    const SynchronousOutputGrantSpec* specification{nullptr};
    std::uint32_t slot{0U};
    GrantStateKind state{GrantStateKind::available};
  };

  struct LeaseState {
    Impl* owner{nullptr};
    ksj::provider::loader::ProviderLease provider_pin{};
    const SynchronousFiringRequest* request{nullptr};
    const ksj::provider::loader::OperatorDescriptor* operator_descriptor{nullptr};
    ksj_firing_lease abi_lease{};
    ksj_output_grant_callbacks output_callbacks{};
    ksj_firing_lease_callbacks callbacks{};
    std::uint64_t total_input_items{0U};
    std::uint64_t total_output_capacity_bytes{0U};
    std::uint64_t sealed_output_items{0U};
    std::uint64_t sealed_output_bytes{0U};
    bool callback_contract_violation{false};
    bool unsupported_capability_requested{false};
  };

  explicit Impl(SynchronousFiringLeaseConfig configuration)
      : config(std::move(configuration)), resource_ledger(config.resource_ledger),
        firing_reservation(std::move(config.firing_reservation)) {}

  [[nodiscard]] ksj::base::Status prepare(const SynchronousProviderInvocation& invocation,
                                          const SynchronousFiringRequest& request, const bool terminal,
                                          OutputGrantAccounting output_accounting, ScratchAccounting scratch_accounting,
                                          LeaseState& state);
  [[nodiscard]] ksj::base::Result<SynchronousFiringResult>
  invoke(const SynchronousProviderInvocation& invocation, const SynchronousFiringRequest& request, bool terminal,
         std::uint64_t completed_input_item_count, OutputGrantAccounting output_accounting,
         ScratchAccounting scratch_accounting);

  [[nodiscard]] ksj::base::Status begin_callback();
  void finish_callback() noexcept;
  [[nodiscard]] SynchronousFiringLeaseSnapshot snapshot() const;
  [[nodiscard]] bool static_workspace_requirements(std::uint64_t& host_bytes,
                                                   std::uint64_t& descriptor_count) const noexcept;

  [[nodiscard]] GrantState* grant_for(const ksj_output_grant* grant) noexcept;
  [[nodiscard]] const GrantState* grant_for(const ksj_output_grant* grant) const noexcept;
  [[nodiscard]] static LeaseState* state_for(void* host_context, const ksj_firing_lease* lease) noexcept;
  static void mark_callback_violation(LeaseState* state) noexcept;
  static void mark_unsupported(LeaseState* state) noexcept;
  static void write_error(ksj_error_view* out_error, ksj_status status, const char* message) noexcept;

  static ksj_status KSJ_PROVIDER_CALL get_info(void* host_context, const ksj_firing_lease* lease,
                                               ksj_firing_lease_info* out_info, ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL get_input_batch(void* host_context, const ksj_firing_lease* lease,
                                                      std::uint32_t batch_index, ksj_input_batch_view* out_batch,
                                                      ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL get_scratch(void* host_context, const ksj_firing_lease* lease,
                                                  ksj_scratch_view* out_scratch, ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL get_key_state(void* host_context, const ksj_firing_lease* lease,
                                                    ksj_key_state* key_state, ksj_key_state_view* out_key_state,
                                                    ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL acquire_output_grant(void* host_context, ksj_firing_lease* lease,
                                                           std::uint32_t output_slot, ksj_output_grant** out_grant,
                                                           ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL retain_input(void* host_context, ksj_firing_lease* lease,
                                                   std::uint32_t batch_index, std::uint32_t item_index,
                                                   const ksj_retention_request* request,
                                                   ksj_retention_handle** out_retention,
                                                   ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL release_retention(void* host_context, ksj_retention_handle* retention,
                                                        ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL register_async(void* host_context, ksj_firing_lease* lease,
                                                     const ksj_async_registration* registration,
                                                     ksj_async_token** out_token, ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL complete_async(void* host_context, ksj_async_token* token,
                                                     const ksj_async_completion* completion,
                                                     ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL release_async(void* host_context, ksj_async_token* token,
                                                    ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL get_cancellation(void* host_context, const ksj_firing_lease* lease,
                                                       ksj_cancellation_view* out_cancellation,
                                                       ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL map_output(void* host_context, ksj_output_grant* grant,
                                                 ksj_mutable_payload_view* out_payload,
                                                 ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL seal_output(void* host_context, ksj_output_grant* grant,
                                                  const ksj_output_seal_descriptor* descriptor,
                                                  ksj_error_view* out_error) noexcept;
  static ksj_status KSJ_PROVIDER_CALL release_output(void* host_context, ksj_output_grant* grant,
                                                     ksj_error_view* out_error) noexcept;

  SynchronousFiringLeaseConfig config;
  std::shared_ptr<ResourceVectorLedger> resource_ledger;
  ksj::recon::ResourceVector firing_reservation;
  // This token is reserved before fixed ABI view allocation, committed after
  // that allocation succeeds, and remains live until Impl destruction. Keep
  // it after resource_ledger in declaration order so token destruction can
  // return it to the ledger safely.
  std::optional<ResourceVectorLedgerReservation> persistent_staging_reservation;
  // Protects the callback-local ABI view storage from concurrent/reentrant
  // prepare().  It is deliberately try-lock-only: a worker must never block
  // waiting for another Provider callback or recursively enter this host.
  std::mutex invocation_mutex;
  mutable std::mutex mutex;
  bool callback_active{false};
  std::optional<ksj::recon::ResourceVector> active_reservation;
  std::optional<ksj::recon::ResourceVector> high_water_reservation;
  std::uint64_t callback_count{0U};
  std::uint64_t static_workspace_host_bytes{0U};
  std::uint64_t static_workspace_descriptor_count{0U};
  std::vector<ksj_input_batch_view> input_batches;
  std::vector<ksj_input_item_view> input_items;
  std::vector<GrantState> grants;
  std::vector<SynchronousSealedOutput> sealed_outputs;
};

std::string_view to_string(const SynchronousFiringOutcome outcome) noexcept {
  switch (outcome) {
    case SynchronousFiringOutcome::done:
      return "done";
    case SynchronousFiringOutcome::yielded:
      return "yielded";
    case SynchronousFiringOutcome::structured_failure:
      return "structured_failure";
    case SynchronousFiringOutcome::contract_violation:
      return "contract_violation";
  }
  return "unknown";
}

ksj::base::Result<SynchronousFiringLeaseHost>
SynchronousFiringLeaseHost::create_impl(SynchronousFiringLeaseConfig config,
                                        const StagingAccounting staging_accounting) {
  if (config.resource_ledger == nullptr) {
    return ksj::base::Status::InvalidArgument(
      "SynchronousFiringLeaseHost requires an explicit shared ResourceVectorLedger");
  }
  if (!is_host_only_reservation(config.firing_reservation)) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost supports only host-pageable synchronous resource reservations");
  }
  if (config.firing_reservation.cpu_leaf_permits() == 0U) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost firing reservation requires one CPU leaf permit");
  }
  if (config.maximum_metadata_bytes == 0U) {
    return ksj::base::Status::InvalidArgument("SynchronousFiringLeaseHost maximum_metadata_bytes must be non-zero");
  }

  try {
    // Keep construction ownership local until every fixed vector allocation
    // and every ledger transition has succeeded. In particular, reserve()
    // below may throw after a persistent token was acquired.
    auto implementation = std::make_unique<Impl>(std::move(config));
    if (!implementation->static_workspace_requirements(implementation->static_workspace_host_bytes,
                                                       implementation->static_workspace_descriptor_count)) {
      return ksj::base::Status::ValidationError(
        "SynchronousFiringLeaseHost fixed ABI staging requirements overflow ResourceVector accounting");
    }
    if (staging_accounting == StagingAccounting::self_reserved) {
      auto persistent_staging = make_persistent_staging_reservation(implementation->static_workspace_host_bytes,
                                                                    implementation->static_workspace_descriptor_count);
      if (!persistent_staging.ok()) {
        return persistent_staging.status();
      }
      auto complete_host_claim =
        combine_persistent_and_firing_reservations(persistent_staging.value(), implementation->firing_reservation);
      if (!complete_host_claim.ok()) {
        return complete_host_claim.status();
      }
      if (!implementation->resource_ledger->capacity().can_admit(complete_host_claim.value())) {
        return ksj::base::Status::ValidationError(
          "SynchronousFiringLeaseHost persistent staging plus one firing exceeds shared ResourceVectorLedger capacity");
      }

      // The vector backing storage is allocated at create(), not at firing
      // time. Reserve it before allocating, then retain the committed
      // resource bundle for the full host lifetime so independently-created
      // hosts cannot oversubscribe it.
      auto persistent_reservation = implementation->resource_ledger->try_reserve(persistent_staging.value());
      if (!persistent_reservation.ok()) {
        return persistent_reservation.status();
      }
      implementation->persistent_staging_reservation.emplace(std::move(persistent_reservation).value());
    } else {
      // The graph owns the plan-wide persistent staging charge. Prove the
      // concrete ABI workspace (including the opaque lease control) fits the
      // exact frozen per-node allowance, without imposing any old one-batch
      // or one-output cardinality restriction.
      std::uint64_t concrete_staging_bytes = 0U;
      if (implementation->config.frozen_staging_charged_bytes == 0U ||
          implementation->config.frozen_staging_descriptor_count == 0U ||
          !checked_add(implementation->static_workspace_host_bytes,
                       static_cast<std::uint64_t>(sizeof(ksj_firing_lease)), concrete_staging_bytes) ||
          concrete_staging_bytes > implementation->config.frozen_staging_charged_bytes ||
          implementation->static_workspace_descriptor_count > implementation->config.frozen_staging_descriptor_count) {
        return ksj::base::Status::ValidationError(
          "SynchronousFiringLeaseHost concrete ABI workspace exceeds the frozen preaccounted staging bound");
      }
      if (!implementation->resource_ledger->capacity().can_admit(implementation->firing_reservation)) {
        return ksj::base::Status::ValidationError(
          "SynchronousFiringLeaseHost firing reservation exceeds its local plan ledger capacity");
      }
    }
    implementation->input_batches.reserve(implementation->config.maximum_input_batches);
    implementation->input_items.reserve(implementation->config.maximum_input_items);
    implementation->grants.reserve(implementation->config.maximum_output_grants);
    implementation->sealed_outputs.reserve(implementation->config.maximum_output_grants);
    if (implementation->persistent_staging_reservation.has_value()) {
      const auto committed_staging = implementation->persistent_staging_reservation->commit();
      if (!committed_staging.ok()) {
        return committed_staging;
      }
    }
    return SynchronousFiringLeaseHost{implementation.release()};
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate bounded SynchronousFiringLeaseHost storage");
  } catch (const std::length_error&) {
    return ksj::base::Status::OutOfMemory("SynchronousFiringLeaseHost bounds exceed host vector capacity");
  }
}

ksj::base::Result<SynchronousFiringLeaseHost> SynchronousFiringLeaseHost::create(SynchronousFiringLeaseConfig config) {
  return create_impl(std::move(config), StagingAccounting::self_reserved);
}

ksj::base::Result<SynchronousFiringLeaseHost>
SynchronousFiringLeaseHost::create_preaccounted_staging(SynchronousFiringLeaseConfig config) {
  return create_impl(std::move(config), StagingAccounting::preaccounted_by_plan);
}

SynchronousFiringLeaseHost::SynchronousFiringLeaseHost(Impl* implementation) noexcept
    : implementation_(implementation) {}

SynchronousFiringLeaseHost::~SynchronousFiringLeaseHost() {
  delete implementation_;
}

SynchronousFiringLeaseHost::SynchronousFiringLeaseHost(SynchronousFiringLeaseHost&& other) noexcept
    : implementation_(std::exchange(other.implementation_, nullptr)) {}

SynchronousFiringLeaseHost& SynchronousFiringLeaseHost::operator=(SynchronousFiringLeaseHost&& other) noexcept {
  if (this != &other) {
    delete implementation_;
    implementation_ = std::exchange(other.implementation_, nullptr);
  }
  return *this;
}

ksj::base::Result<SynchronousFiringResult>
SynchronousFiringLeaseHost::process(const SynchronousProviderInvocation& invocation,
                                    const SynchronousFiringRequest& request) {
  if (implementation_ == nullptr) {
    return ksj::base::Status::StateError("SynchronousFiringLeaseHost has been moved from");
  }
  return implementation_->invoke(invocation, request, false, 0U, Impl::OutputGrantAccounting::externally_supplied,
                                 Impl::ScratchAccounting::externally_supplied);
}

ksj::base::Result<SynchronousFiringResult>
SynchronousFiringLeaseHost::process_preaccounted_output(const SynchronousProviderInvocation& invocation,
                                                        const SynchronousFiringRequest& request) {
  if (implementation_ == nullptr) {
    return ksj::base::Status::StateError("SynchronousFiringLeaseHost has been moved from");
  }
  return implementation_->invoke(invocation, request, false, 0U, Impl::OutputGrantAccounting::preaccounted_pool_slot,
                                 Impl::ScratchAccounting::preaccounted_graph_slab);
}

ksj::base::Result<SynchronousFiringResult>
SynchronousFiringLeaseHost::on_scan_end_preaccounted_output(const SynchronousProviderInvocation& invocation,
                                                            const SynchronousFiringRequest& request,
                                                            const std::uint64_t completed_input_item_count) {
  if (implementation_ == nullptr) {
    return ksj::base::Status::StateError("SynchronousFiringLeaseHost has been moved from");
  }
  return implementation_->invoke(invocation, request, true, completed_input_item_count,
                                 Impl::OutputGrantAccounting::preaccounted_pool_slot,
                                 Impl::ScratchAccounting::preaccounted_graph_slab);
}

ksj::base::Result<SynchronousFiringResult>
SynchronousFiringLeaseHost::on_scan_end(const SynchronousProviderInvocation& invocation,
                                        const SynchronousFiringRequest& request,
                                        const std::uint64_t completed_input_item_count) {
  if (implementation_ == nullptr) {
    return ksj::base::Status::StateError("SynchronousFiringLeaseHost has been moved from");
  }
  return implementation_->invoke(invocation, request, true, completed_input_item_count,
                                 Impl::OutputGrantAccounting::externally_supplied,
                                 Impl::ScratchAccounting::externally_supplied);
}

SynchronousFiringLeaseSnapshot SynchronousFiringLeaseHost::snapshot() const {
  if (implementation_ == nullptr) {
    return {};
  }
  return implementation_->snapshot();
}

ksj::base::Status SynchronousFiringLeaseHost::Impl::begin_callback() {
  std::lock_guard lock(mutex);
  if (callback_active) {
    return ksj::base::Status::Unavailable(
      "SynchronousFiringLeaseHost admits only one synchronous Provider callback at a time");
  }
  callback_active = true;
  active_reservation = firing_reservation;
  high_water_reservation = firing_reservation;
  ++callback_count;
  return ksj::base::Status::Ok();
}

void SynchronousFiringLeaseHost::Impl::finish_callback() noexcept {
  std::lock_guard lock(mutex);
  active_reservation.reset();
  callback_active = false;
}

SynchronousFiringLeaseSnapshot SynchronousFiringLeaseHost::Impl::snapshot() const {
  std::lock_guard lock(mutex);
  return {
    .callback_active = callback_active,
    .active_reservation = active_reservation,
    .high_water_reservation = high_water_reservation,
    .callback_count = callback_count,
  };
}

bool SynchronousFiringLeaseHost::Impl::static_workspace_requirements(std::uint64_t& host_bytes,
                                                                     std::uint64_t& descriptor_count) const noexcept {
  // The public raw-span host charges its fixed vector-backed workspace here.
  // Preaccounted graph construction separately adds its callback-local ABI
  // lease control to the frozen staging allowance, preserving the public
  // host's established persistent-accounting behavior.
  host_bytes = 0U;
  descriptor_count = 1U; // The FiringLease itself.
  const auto add_storage = [&host_bytes](const std::uint64_t count, const std::uint64_t element_size) {
    std::uint64_t bytes = 0U;
    return checked_multiply(count, element_size, bytes) && checked_add(host_bytes, bytes, host_bytes);
  };
  const auto add_descriptors = [&descriptor_count](const std::uint64_t count) {
    return checked_add(descriptor_count, count, descriptor_count);
  };
  const auto input_batches_count = static_cast<std::uint64_t>(config.maximum_input_batches);
  const auto input_items_count = static_cast<std::uint64_t>(config.maximum_input_items);
  const auto output_grants_count = static_cast<std::uint64_t>(config.maximum_output_grants);
  return add_storage(input_batches_count, sizeof(ksj_input_batch_view)) &&
         add_storage(input_items_count, sizeof(ksj_input_item_view)) &&
         add_storage(output_grants_count, sizeof(GrantState)) &&
         add_storage(output_grants_count, sizeof(SynchronousSealedOutput)) && add_descriptors(input_batches_count) &&
         add_descriptors(input_items_count) && add_descriptors(output_grants_count) &&
         add_descriptors(output_grants_count);
}

ksj::base::Status SynchronousFiringLeaseHost::Impl::prepare(
  const SynchronousProviderInvocation& invocation, const SynchronousFiringRequest& request, const bool terminal,
  const OutputGrantAccounting output_accounting, const ScratchAccounting scratch_accounting, LeaseState& state) {
  if (!invocation.provider.valid() || invocation.provider.api() == nullptr ||
      invocation.provider.descriptor() == nullptr || invocation.operator_id.empty() ||
      invocation.operator_handle == nullptr || invocation.execution_context == nullptr ||
      invocation.key_state == nullptr) {
    return ksj::base::Status::InvalidArgument(
      "SynchronousFiringLeaseHost requires a pinned loaded Provider and non-null lifecycle handles");
  }
  const auto* api = invocation.provider.api();
  if ((!terminal && api->operator_process_batch == nullptr) || (terminal && api->operator_on_scan_end == nullptr)) {
    return ksj::base::Status::ValidationError("Provider ABI table does not expose the requested synchronous callback");
  }
  const ksj::provider::loader::OperatorDescriptor* operator_descriptor = nullptr;
  if (!contains_operator(*invocation.provider.descriptor(), invocation.operator_id, operator_descriptor)) {
    return ksj::base::Status::ValidationError(
      "Provider invocation operator_id is absent from the loaded Provider descriptor");
  }
  if ((invocation.provider.descriptor()->capability_bits & KSJ_PROVIDER_CAP_SYNC_PROCESS) == 0U ||
      (invocation.provider.descriptor()->capability_bits &
       (KSJ_PROVIDER_CAP_ASYNC_PROCESS | KSJ_PROVIDER_CAP_INPUT_RETENTION)) != 0U) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost requires a sync-only Provider without declared async or retention capability");
  }
  if ((operator_descriptor->capability_bits &
       (KSJ_OPERATOR_CAP_MAY_ASYNC | KSJ_OPERATOR_CAP_MAY_RETAIN_INPUT | KSJ_OPERATOR_CAP_MAY_YIELD)) != 0U) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost rejects Operators declaring async, retention, or yield capability");
  }
  if (operator_descriptor->max_private_threads != 0U) {
    return ksj::base::Status::ValidationError("SynchronousFiringLeaseHost rejects Providers declaring private threads");
  }

  if (terminal && !request.input_batches.empty()) {
    return ksj::base::Status::InvalidArgument(
      "SynchronousFiringLeaseHost normal on_scan_end accepts no ordinary input batches");
  }
  if (terminal && !request.output_grants.empty() &&
      (operator_descriptor->capability_bits & KSJ_OPERATOR_CAP_MAY_EMIT_TERMINAL_OUTPUT) == 0U) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost normal terminal output requires Provider terminal-output capability");
  }
  if (request.input_batches.size() > config.maximum_input_batches ||
      request.output_grants.size() > config.maximum_output_grants) {
    return ksj::base::Status::Unavailable("SynchronousFiringLeaseHost fixed lease view capacity is exhausted");
  }
  if (request.scratch.size() > config.maximum_scratch_bytes ||
      request.scratch.size() > operator_descriptor->max_scratch_bytes_per_firing) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost scratch bytes exceed the frozen firing bound");
  }
  if (request.scratch.size() != 0U && request.scratch.data() == nullptr) {
    return ksj::base::Status::InvalidArgument("SynchronousFiringLeaseHost scratch bytes require non-null host storage");
  }

  input_batches.clear();
  input_items.clear();
  grants.clear();
  sealed_outputs.clear();

  std::uint64_t total_input_items = 0U;
  std::uint64_t total_input_payload_bytes = 0U;
  for (const auto& source_batch : request.input_batches) {
    if (source_batch.items.size() > std::numeric_limits<std::uint32_t>::max()) {
      return ksj::base::Status::ValidationError("SynchronousFiringLeaseHost input batch item count overflows the ABI");
    }
    const auto first_item = input_items.size();
    for (const auto& source_item : source_batch.items) {
      if (input_items.size() >= config.maximum_input_items || !valid_host_type(source_item.type) ||
          !valid_borrowed_byte_view(ksj_byte_view{.abi = make_header(sizeof(ksj_byte_view)),
                                                  .data = source_item.metadata.data(),
                                                  .size = source_item.metadata.size()},
                                    config.maximum_metadata_bytes) ||
          (source_item.payload.size() != 0U && source_item.payload.data() == nullptr) ||
          (source_item.payload.size() != 0U &&
           pointer_alignment(source_item.payload.data()) < source_item.type.minimum_alignment)) {
        return ksj::base::Status::ValidationError(
          "SynchronousFiringLeaseHost input item violates the host-pageable ABI type/layout bound");
      }
      if (!checked_add(total_input_items, 1U, total_input_items) ||
          !checked_add(total_input_payload_bytes, source_item.payload.size(), total_input_payload_bytes)) {
        return ksj::base::Status::ValidationError("SynchronousFiringLeaseHost input accounting overflows");
      }

      ksj_input_item_view item{};
      item.abi = make_header(sizeof(item));
      item.payload.abi = make_header(sizeof(item.payload));
      item.payload.data = source_item.payload.data();
      item.payload.byte_count = source_item.payload.size();
      item.payload.memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
      item.payload.alignment = pointer_alignment(source_item.payload.data());
      item.payload.type = source_item.type;
      item.metadata.abi = make_header(sizeof(item.metadata));
      item.metadata.data = source_item.metadata.data();
      item.metadata.size = source_item.metadata.size();
      item.semantic_key_hash = source_item.semantic_key_hash;
      item.order_key = source_item.order_key;
      item.item_ordinal = source_item.item_ordinal;
      input_items.push_back(item);
    }

    ksj_input_batch_view batch{};
    batch.abi = make_header(sizeof(batch));
    batch.items = source_batch.items.empty() ? nullptr : input_items.data() + first_item;
    batch.item_count = static_cast<std::uint32_t>(source_batch.items.size());
    batch.input_port = source_batch.input_port;
    batch.batch_id = source_batch.batch_id;
    batch.order_domain = source_batch.order_domain;
    input_batches.push_back(batch);
  }
  if (total_input_items > operator_descriptor->max_input_items_per_firing ||
      total_input_payload_bytes > config.maximum_input_payload_bytes) {
    return ksj::base::Status::ValidationError("SynchronousFiringLeaseHost input exceeds the frozen firing bound");
  }

  std::uint64_t total_output_capacity_bytes = 0U;
  std::uint64_t total_output_item_capacity = 0U;
  for (std::size_t index = 0U; index < request.output_grants.size(); ++index) {
    const auto& specification = request.output_grants[index];
    if (!valid_host_type(specification.required_type) ||
        (specification.storage.size() != 0U && specification.storage.data() == nullptr) ||
        (specification.metadata_storage.size() != 0U && specification.metadata_storage.data() == nullptr) ||
        specification.metadata_storage.size() > config.maximum_metadata_bytes ||
        (specification.storage.size() != 0U &&
         pointer_alignment(specification.storage.data()) < specification.required_type.minimum_alignment) ||
        !checked_add(total_output_capacity_bytes, specification.storage.size(), total_output_capacity_bytes) ||
        !checked_add(total_output_capacity_bytes, specification.metadata_storage.size(), total_output_capacity_bytes) ||
        !checked_add(total_output_item_capacity, specification.maximum_item_count, total_output_item_capacity)) {
      return ksj::base::Status::ValidationError(
        "SynchronousFiringLeaseHost output grant violates the host-pageable ABI type/layout bound");
    }
    grants.push_back({
      .abi_grant = {.magic = kGrantMagic, .slot = static_cast<std::uint32_t>(index)},
      .specification = &specification,
      .slot = static_cast<std::uint32_t>(index),
    });
  }
  if (total_output_capacity_bytes > operator_descriptor->max_output_bytes_per_firing ||
      total_output_item_capacity > operator_descriptor->max_output_items_per_firing) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost output grants exceed the Provider declaration");
  }
  if ((!request.output_grants.empty()) && !request.commit_outputs) {
    return ksj::base::Status::InvalidArgument(
      "SynchronousFiringLeaseHost requires one atomic output-commit callback for declared output grants");
  }

  const auto dynamically_charged_output_bytes =
    output_accounting == OutputGrantAccounting::externally_supplied ? total_output_capacity_bytes : 0U;
  const auto dynamically_charged_scratch_bytes =
    scratch_accounting == ScratchAccounting::externally_supplied ? request.scratch.size() : 0U;
  if (scratch_accounting == ScratchAccounting::preaccounted_graph_slab &&
      request.scratch.size() != config.maximum_scratch_bytes) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost graph scratch must exactly match the frozen node scratch bound");
  }
  std::uint64_t minimum_host_bytes = 0U;
  if (!checked_add(dynamically_charged_output_bytes, dynamically_charged_scratch_bytes, minimum_host_bytes) ||
      firing_reservation.host_normal_bytes() < minimum_host_bytes) {
    return ksj::base::Status::ValidationError(
      "SynchronousFiringLeaseHost firing ResourceVector host_normal_bytes does not cover its dynamic output/scratch "
      "reservation");
  }

  state.owner = this;
  state.provider_pin = invocation.provider;
  state.request = &request;
  state.operator_descriptor = operator_descriptor;
  state.abi_lease.magic = kLeaseMagic;
  state.total_input_items = total_input_items;
  state.total_output_capacity_bytes = total_output_capacity_bytes;
  state.output_callbacks = {
    .abi = make_header(sizeof(state.output_callbacks)),
    .host_context = &state,
    .map_mutable_payload = &Impl::map_output,
    .seal = &Impl::seal_output,
    .release = &Impl::release_output,
  };
  std::uint64_t lease_capabilities =
    KSJ_LEASE_CAP_INPUT_BATCHES | KSJ_LEASE_CAP_OUTPUT_GRANTS | KSJ_LEASE_CAP_CANCELLATION;
  if (!request.scratch.empty()) {
    lease_capabilities |= KSJ_LEASE_CAP_SCRATCH;
  }
  state.callbacks = {
    .abi = make_header(sizeof(state.callbacks), lease_capabilities),
    .host_context = &state,
    .output_grants = &state.output_callbacks,
    .get_info = &Impl::get_info,
    .get_input_batch = &Impl::get_input_batch,
    .get_scratch = &Impl::get_scratch,
    .get_key_state = &Impl::get_key_state,
    .acquire_output_grant = &Impl::acquire_output_grant,
    .retain_input = &Impl::retain_input,
    .release_retention = &Impl::release_retention,
    .register_async = &Impl::register_async,
    .complete_async = &Impl::complete_async,
    .release_async = &Impl::release_async,
    .get_cancellation = &Impl::get_cancellation,
  };
  return ksj::base::Status::Ok();
}

ksj::base::Result<SynchronousFiringResult> SynchronousFiringLeaseHost::Impl::invoke(
  const SynchronousProviderInvocation& invocation, const SynchronousFiringRequest& request, const bool terminal,
  const std::uint64_t completed_input_item_count, const OutputGrantAccounting output_accounting,
  const ScratchAccounting scratch_accounting) {
  std::unique_lock invocation_lock(invocation_mutex, std::try_to_lock);
  if (!invocation_lock.owns_lock()) {
    return ksj::base::Status::Unavailable(
      "SynchronousFiringLeaseHost has an active callback and cannot be re-entered or run concurrently");
  }
  LeaseState state{};
  const auto prepared = prepare(invocation, request, terminal, output_accounting, scratch_accounting, state);
  if (!prepared.ok()) {
    return prepared;
  }

  // Acquire the full, indivisible firing bundle only after all pre-callback
  // validation succeeds. A failed reservation therefore cannot consume any
  // input or mutate the shared ledger's accounts.
  auto ledger_reservation = resource_ledger->try_reserve(firing_reservation);
  if (!ledger_reservation.ok()) {
    return ledger_reservation.status();
  }
  const auto begun = begin_callback();
  if (!begun.ok()) {
    return begun;
  }
  struct FinishCallback final {
    Impl* owner;
    ResourceVectorLedgerReservation reservation;

    ~FinishCallback() noexcept {
      // Keep callback_active true until the shared bundle has been returned.
      // release() is expected to succeed for this unique token; its own
      // destructor provides a final no-throw cleanup attempt if a system
      // exception were to occur while taking the ledger lock.
      try {
        static_cast<void>(reservation.release());
      } catch (...) {}
      owner->finish_callback();
    }
  } finish{this, std::move(ledger_reservation).value()};

  ksj_process_result raw_result{};
  raw_result.abi = make_header(sizeof(raw_result));
  ksj_error_view raw_error{};
  raw_error.abi = make_header(sizeof(raw_error));
  raw_error.message.abi = make_header(sizeof(raw_error.message));

  ksj_status provider_status = KSJ_STATUS_INTERNAL_ERROR;
  try {
    if (terminal) {
      ksj_scan_end_descriptor scan_end{};
      scan_end.abi = make_header(sizeof(scan_end));
      scan_end.kind = KSJ_PROVIDER_SCAN_END_NORMAL;
      scan_end.terminal_epoch = request.terminal_epoch;
      scan_end.completed_input_item_count = completed_input_item_count;
      provider_status = invocation.provider.api()->operator_on_scan_end(
        invocation.operator_handle, invocation.execution_context, invocation.key_state, &scan_end, &state.abi_lease,
        &state.callbacks, &raw_result, &raw_error);
    } else {
      provider_status = invocation.provider.api()->operator_process_batch(
        invocation.operator_handle, invocation.execution_context, invocation.key_state, &state.abi_lease,
        &state.callbacks, &raw_result, &raw_error);
    }
  } catch (...) {
    return SynchronousFiringResult{
      .outcome = SynchronousFiringOutcome::contract_violation,
      .provider_status = KSJ_STATUS_CONTRACT_VIOLATION,
      .terminal_epoch = request.terminal_epoch,
    };
  }

  const auto provider_failure = capture_provider_failure_detail(raw_error);
  const auto contract_failure = [&state, &request, provider_status, &provider_failure]() {
    return SynchronousFiringResult{
      .outcome = SynchronousFiringOutcome::contract_violation,
      .provider_status = provider_status,
      .provider_failure = provider_failure,
      .sealed_output_count = static_cast<std::uint32_t>(state.owner->sealed_outputs.size()),
      .sealed_output_bytes = state.sealed_output_bytes,
      .terminal_epoch = request.terminal_epoch,
    };
  };
  const auto structured_failure = [&state, &request, provider_status, &provider_failure]() {
    return SynchronousFiringResult{
      .outcome = SynchronousFiringOutcome::structured_failure,
      .provider_status = provider_status,
      .provider_failure = provider_failure,
      .sealed_output_count = static_cast<std::uint32_t>(state.owner->sealed_outputs.size()),
      .sealed_output_bytes = state.sealed_output_bytes,
      .terminal_epoch = request.terminal_epoch,
    };
  };

  if (state.callback_contract_violation) {
    return contract_failure();
  }
  if (provider_status != KSJ_STATUS_OK) {
    return provider_status == KSJ_STATUS_CONTRACT_VIOLATION || provider_status == KSJ_STATUS_BAD_ABI
             ? contract_failure()
             : structured_failure();
  }
  if (!has_full_compatible_header(&raw_result) || !known_outcome(raw_result.outcome) ||
      raw_result.terminal_epoch != request.terminal_epoch || raw_result.async_token != nullptr ||
      raw_result.sealed_output_count != state.owner->sealed_outputs.size()) {
    return contract_failure();
  }
  if (state.unsupported_capability_requested || raw_result.outcome == KSJ_PROVIDER_PROCESS_ASYNC_PENDING) {
    return contract_failure();
  }

  if (raw_result.outcome == KSJ_PROVIDER_PROCESS_YIELD) {
    // This synchronous host has no transactional key-state API or scheduler resume protocol.
    // A raw ABI YIELD is therefore never safe to retry in this slice.
    return contract_failure();
  }
  if (raw_result.outcome == KSJ_PROVIDER_PROCESS_STRUCTURED_FAILURE) {
    return structured_failure();
  }
  if (raw_result.outcome == KSJ_PROVIDER_PROCESS_CONTRACT_VIOLATION) {
    return contract_failure();
  }
  if (raw_result.outcome != KSJ_PROVIDER_PROCESS_DONE ||
      raw_result.consumed_input_item_count != (terminal ? 0U : state.total_input_items)) {
    return contract_failure();
  }
  const auto unsettled_grant = std::find_if(grants.begin(), grants.end(), [](const GrantState& grant) {
    return grant.state == GrantStateKind::acquired || grant.state == GrantStateKind::mapped;
  });
  if (unsettled_grant != grants.end()) {
    return contract_failure();
  }

  if (!sealed_outputs.empty()) {
    try {
      const auto commit_status = request.commit_outputs(sealed_outputs);
      if (!commit_status.ok()) {
        return structured_failure();
      }
    } catch (...) {
      return structured_failure();
    }
  }
  return SynchronousFiringResult{
    .outcome = SynchronousFiringOutcome::done,
    .provider_status = provider_status,
    .provider_failure = provider_failure,
    .consumed_input_item_count = raw_result.consumed_input_item_count,
    .sealed_output_count = static_cast<std::uint32_t>(sealed_outputs.size()),
    .committed_output_count = static_cast<std::uint32_t>(sealed_outputs.size()),
    .sealed_output_bytes = state.sealed_output_bytes,
    .terminal_epoch = request.terminal_epoch,
  };
}

SynchronousFiringLeaseHost::Impl::GrantState*
SynchronousFiringLeaseHost::Impl::grant_for(const ksj_output_grant* const grant) noexcept {
  if (grant == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(grants.begin(), grants.end(), [grant](const GrantState& candidate) {
    return grant == &candidate.abi_grant && grant->magic == kGrantMagic && grant->slot == candidate.slot;
  });
  return found == grants.end() ? nullptr : &*found;
}

const SynchronousFiringLeaseHost::Impl::GrantState*
SynchronousFiringLeaseHost::Impl::grant_for(const ksj_output_grant* const grant) const noexcept {
  if (grant == nullptr) {
    return nullptr;
  }
  const auto found = std::find_if(grants.begin(), grants.end(), [grant](const GrantState& candidate) {
    return grant == &candidate.abi_grant && grant->magic == kGrantMagic && grant->slot == candidate.slot;
  });
  return found == grants.end() ? nullptr : &*found;
}

SynchronousFiringLeaseHost::Impl::LeaseState*
SynchronousFiringLeaseHost::Impl::state_for(void* const host_context, const ksj_firing_lease* const lease) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  if (state == nullptr || state->owner == nullptr || lease != &state->abi_lease || lease->magic != kLeaseMagic) {
    return nullptr;
  }
  return state;
}

void SynchronousFiringLeaseHost::Impl::mark_callback_violation(LeaseState* const state) noexcept {
  if (state != nullptr) {
    state->callback_contract_violation = true;
  }
}

void SynchronousFiringLeaseHost::Impl::mark_unsupported(LeaseState* const state) noexcept {
  if (state != nullptr) {
    state->unsupported_capability_requested = true;
  }
}

void SynchronousFiringLeaseHost::Impl::write_error(ksj_error_view* const out_error, const ksj_status status,
                                                   const char* const message) noexcept {
  if (!has_full_compatible_header(out_error)) {
    return;
  }
  *out_error = {};
  out_error->abi = make_header(sizeof(*out_error));
  out_error->status = status;
  out_error->message.abi = make_header(sizeof(out_error->message));
  out_error->message.data = message;
  out_error->message.size = std::strlen(message);
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::get_info(void* const host_context,
                                                                        const ksj_firing_lease* const lease,
                                                                        ksj_firing_lease_info* const out_info,
                                                                        ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr || !has_full_compatible_header(out_info)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease info request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  *out_info = {};
  out_info->abi = make_header(sizeof(*out_info));
  out_info->resource_occurrence_id = state->request->resource_occurrence_id;
  out_info->slot_generation = state->request->slot_generation;
  out_info->terminal_epoch = state->request->terminal_epoch;
  out_info->input_batch_count = static_cast<std::uint32_t>(state->owner->input_batches.size());
  out_info->output_grant_count = static_cast<std::uint32_t>(state->owner->grants.size());
  out_info->reserved_output_bytes = state->total_output_capacity_bytes;
  out_info->reserved_scratch_bytes = state->request->scratch.size();
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::get_input_batch(
  void* const host_context, const ksj_firing_lease* const lease, const std::uint32_t batch_index,
  ksj_input_batch_view* const out_batch, ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr || !has_full_compatible_header(out_batch) || batch_index >= state->owner->input_batches.size()) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease input batch request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  *out_batch = state->owner->input_batches[batch_index];
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::get_scratch(void* const host_context,
                                                                           const ksj_firing_lease* const lease,
                                                                           ksj_scratch_view* const out_scratch,
                                                                           ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr || !has_full_compatible_header(out_scratch)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease scratch request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  if (state->request->scratch.empty()) {
    mark_unsupported(state);
    write_error(out_error, KSJ_STATUS_UNSUPPORTED, "scratch is not reserved for this firing");
    return KSJ_STATUS_UNSUPPORTED;
  }
  *out_scratch = {};
  out_scratch->abi = make_header(sizeof(*out_scratch));
  out_scratch->data = state->request->scratch.data();
  out_scratch->byte_count = state->request->scratch.size();
  out_scratch->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_scratch->alignment = pointer_alignment(state->request->scratch.data());
  out_scratch->resource_occurrence_id = state->request->resource_occurrence_id;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::get_key_state(void* const host_context,
                                                                             const ksj_firing_lease* const lease,
                                                                             ksj_key_state*, ksj_key_state_view*,
                                                                             ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr) {
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease key-state request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  mark_unsupported(state);
  write_error(out_error, KSJ_STATUS_UNSUPPORTED,
              "SynchronousFiringLeaseHost does not expose mutable key-state bytes through a firing lease");
  return KSJ_STATUS_UNSUPPORTED;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::acquire_output_grant(
  void* const host_context, ksj_firing_lease* const lease, const std::uint32_t output_slot,
  ksj_output_grant** const out_grant, ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr || out_grant == nullptr || output_slot >= state->owner->grants.size()) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease output-grant request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  auto& grant = state->owner->grants[output_slot];
  if (grant.state != GrantStateKind::available) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "output grant was acquired more than once");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  grant.state = GrantStateKind::acquired;
  *out_grant = &grant.abi_grant;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::retain_input(
  void* const host_context, ksj_firing_lease* const lease, std::uint32_t, std::uint32_t, const ksj_retention_request*,
  ksj_retention_handle** const out_retention, ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr) {
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease retention request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  if (out_retention != nullptr) {
    *out_retention = nullptr;
  }
  mark_unsupported(state);
  write_error(out_error, KSJ_STATUS_UNSUPPORTED, "SynchronousFiringLeaseHost does not support input retention");
  return KSJ_STATUS_UNSUPPORTED;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::release_retention(
  void* const host_context, ksj_retention_handle*, ksj_error_view* const out_error) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  mark_unsupported(state);
  write_error(out_error, KSJ_STATUS_UNSUPPORTED, "SynchronousFiringLeaseHost does not support input retention");
  return KSJ_STATUS_UNSUPPORTED;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::register_async(
  void* const host_context, ksj_firing_lease* const lease, const ksj_async_registration*,
  ksj_async_token** const out_token, ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr) {
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease async registration");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  if (out_token != nullptr) {
    *out_token = nullptr;
  }
  mark_unsupported(state);
  write_error(out_error, KSJ_STATUS_UNSUPPORTED,
              "SynchronousFiringLeaseHost does not support asynchronous Provider work");
  return KSJ_STATUS_UNSUPPORTED;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::complete_async(
  void* const host_context, ksj_async_token*, const ksj_async_completion*, ksj_error_view* const out_error) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  mark_unsupported(state);
  write_error(out_error, KSJ_STATUS_UNSUPPORTED,
              "SynchronousFiringLeaseHost does not support asynchronous Provider work");
  return KSJ_STATUS_UNSUPPORTED;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::release_async(void* const host_context, ksj_async_token*,
                                                                             ksj_error_view* const out_error) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  mark_unsupported(state);
  write_error(out_error, KSJ_STATUS_UNSUPPORTED,
              "SynchronousFiringLeaseHost does not support asynchronous Provider work");
  return KSJ_STATUS_UNSUPPORTED;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::get_cancellation(
  void* const host_context, const ksj_firing_lease* const lease, ksj_cancellation_view* const out_cancellation,
  ksj_error_view* const out_error) noexcept {
  auto* state = state_for(host_context, lease);
  if (state == nullptr || !has_full_compatible_header(out_cancellation)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid firing-lease cancellation request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  *out_cancellation = {};
  out_cancellation->abi = make_header(sizeof(*out_cancellation));
  out_cancellation->state = KSJ_PROVIDER_NOT_CANCELLED;
  out_cancellation->terminal_epoch = state->request->terminal_epoch;
  out_cancellation->cancellation_generation = 0U;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::map_output(void* const host_context,
                                                                          ksj_output_grant* const grant,
                                                                          ksj_mutable_payload_view* const out_payload,
                                                                          ksj_error_view* const out_error) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  if (state == nullptr || state->owner == nullptr || !has_full_compatible_header(out_payload)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid output-grant map request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  auto* grant_state = state->owner->grant_for(grant);
  if (grant_state == nullptr || grant_state->state != GrantStateKind::acquired) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "output grant cannot be mapped in its current state");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  const auto& specification = *grant_state->specification;
  *out_payload = {};
  out_payload->abi = make_header(sizeof(*out_payload));
  out_payload->data = specification.storage.data();
  out_payload->capacity_bytes = specification.storage.size();
  out_payload->committed_bytes = 0U;
  out_payload->memory_domain = KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
  out_payload->alignment = pointer_alignment(specification.storage.data());
  out_payload->type = specification.required_type;
  grant_state->state = GrantStateKind::mapped;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::seal_output(
  void* const host_context, ksj_output_grant* const grant, const ksj_output_seal_descriptor* const descriptor,
  ksj_error_view* const out_error) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  if (state == nullptr || state->owner == nullptr || !has_full_compatible_header(descriptor)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid output-grant seal request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  auto* grant_state = state->owner->grant_for(grant);
  if (grant_state == nullptr || grant_state->state != GrantStateKind::mapped) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "output grant cannot be sealed in its current state");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  const auto& specification = *grant_state->specification;
  if (descriptor->output_port != specification.output_port ||
      descriptor->produced_item_count > specification.maximum_item_count ||
      descriptor->produced_byte_count > specification.storage.size() ||
      !valid_borrowed_byte_view(descriptor->metadata, state->owner->config.maximum_metadata_bytes) ||
      descriptor->metadata.size > specification.metadata_storage.size() ||
      !type_descriptors_match(descriptor->type, specification.required_type)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "output seal exceeds the frozen grant contract");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  std::uint64_t aggregate_bytes = 0U;
  std::uint64_t aggregate_items = 0U;
  if (!checked_add(state->sealed_output_bytes, descriptor->produced_byte_count, aggregate_bytes) ||
      !checked_add(state->sealed_output_items, descriptor->produced_item_count, aggregate_items) ||
      aggregate_bytes > state->operator_descriptor->max_output_bytes_per_firing ||
      aggregate_items > state->operator_descriptor->max_output_items_per_firing) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "sealed output bytes exceed the Provider declaration");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  state->sealed_output_bytes = aggregate_bytes;
  state->sealed_output_items = aggregate_items;
  auto normalized_descriptor = *descriptor;
  normalized_descriptor.type = specification.required_type;
  normalized_descriptor.metadata = {};
  normalized_descriptor.metadata.abi = make_header(sizeof(normalized_descriptor.metadata));
  normalized_descriptor.metadata.data = specification.metadata_storage.data();
  normalized_descriptor.metadata.size = descriptor->metadata.size;
  if (descriptor->metadata.size != 0U) {
    std::memcpy(specification.metadata_storage.data(), descriptor->metadata.data,
                static_cast<std::size_t>(descriptor->metadata.size));
  }
  state->owner->sealed_outputs.push_back({
    .output_slot = grant_state->slot,
    .payload =
      ksj::base::ConstByteSpan{specification.storage.data(), static_cast<std::size_t>(descriptor->produced_byte_count)},
    .descriptor = normalized_descriptor,
  });
  grant_state->state = GrantStateKind::sealed;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL SynchronousFiringLeaseHost::Impl::release_output(
  void* const host_context, ksj_output_grant* const grant, ksj_error_view* const out_error) noexcept {
  auto* state = static_cast<LeaseState*>(host_context);
  if (state == nullptr || state->owner == nullptr) {
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "invalid output-grant release request");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  auto* grant_state = state->owner->grant_for(grant);
  if (grant_state == nullptr ||
      (grant_state->state != GrantStateKind::acquired && grant_state->state != GrantStateKind::mapped)) {
    mark_callback_violation(state);
    write_error(out_error, KSJ_STATUS_CONTRACT_VIOLATION, "output grant cannot be released in its current state");
    return KSJ_STATUS_CONTRACT_VIOLATION;
  }
  grant_state->state = GrantStateKind::released;
  return KSJ_STATUS_OK;
}

} // namespace ksj::recon::runtime
