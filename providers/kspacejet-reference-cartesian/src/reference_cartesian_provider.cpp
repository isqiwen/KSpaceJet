// SPDX-License-Identifier: Apache-2.0
//
// Minimal open Provider ABI v1 reference implementation.  It deliberately
// does not perform image reconstruction: this gives the runtime an auditable
// bounded Cartesian lifecycle sink without importing historical/private
// reconstruction code into the public project.

#include "kspacejet/provider/v1/provider.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Provider ABI opaque handles are intentionally defined only inside this
// dynamic library.  This provider has no mutable per-operator, per-context,
// or per-key state, so stable singleton tokens avoid cross-boundary ownership
// and unaccounted allocations.
struct ksj_provider_operator {};
struct ksj_execution_context {};
struct ksj_key_state {};

namespace {

constexpr char kProviderId[] = "org.kspacejet.reference.cartesian";
constexpr char kOperatorId[] = "cartesian_reference_sink";
constexpr char kCanonicalEmptyConfig[] = "{}";
constexpr char kErrorBadAbi[] = "Provider ABI v1 request or output storage is incompatible";
constexpr char kErrorInvalidArgument[] = "Reference Cartesian Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] = "Reference Cartesian Provider does not expose the requested operator";
constexpr char kErrorContractDigest[] = "Required contract digest does not match the advertised reference operator";
constexpr char kErrorUnsupportedConfig[] = "Reference Cartesian Provider accepts only canonical empty config {}";
constexpr char kErrorInputBound[] = "Reference Cartesian Provider input batch exceeds its declared bound";
constexpr char kErrorTerminalKind[] = "Reference Cartesian Provider received an invalid terminal callback kind";
constexpr char kErrorHostLease[] = "Reference Cartesian Provider requires valid host firing-lease callbacks";
constexpr char kErrorInternal[] = "Reference Cartesian Provider trapped an unexpected internal exception";

constexpr std::uint32_t kMaximumInputBatchesPerFiring = 64U;
constexpr std::uint32_t kMaximumInputItemsPerFiring = 256U;
constexpr std::uint32_t kMaximumInFlight = 64U;

ksj_provider_operator kOperatorToken;
ksj_execution_context kExecutionContextToken;
ksj_key_state kKeyStateToken;

[[nodiscard]] ksj_provider_abi_header make_header(const std::uint32_t struct_size,
                                                  const std::uint64_t capability_bits = 0U) noexcept {
  return ksj_provider_abi_header_make(struct_size, capability_bits);
}

[[nodiscard]] bool has_compatible_header(const ksj_provider_abi_header* header,
                                         const std::size_t required_size) noexcept {
  return header != nullptr && header->struct_size >= required_size && header->abi_major == KSJ_PROVIDER_ABI_MAJOR &&
         header->abi_minor <= KSJ_PROVIDER_ABI_MINOR && header->reserved[0] == 0U && header->reserved[1] == 0U;
}

template <typename T> [[nodiscard]] bool has_full_compatible_header(const T* value) noexcept {
  return value != nullptr && has_compatible_header(&value->abi, sizeof(T));
}

[[nodiscard]] ksj_utf8_view make_utf8_view(const char* data, const std::uint64_t size) noexcept {
  ksj_utf8_view view{};
  view.abi = make_header(sizeof(view));
  view.data = data;
  view.size = size;
  return view;
}

[[nodiscard]] ksj_digest256 make_digest(const std::uint8_t seed) noexcept {
  ksj_digest256 digest{};
  digest.abi = make_header(sizeof(digest));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    digest.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return digest;
}

void set_error(ksj_error_view* out_error, const ksj_status status, const char* message,
               const std::uint64_t message_size) noexcept {
  if (!has_full_compatible_header(out_error)) {
    return;
  }

  *out_error = {};
  out_error->abi = make_header(sizeof(*out_error));
  out_error->status = status;
  out_error->category = 0U;
  out_error->provider_error_code = 0U;
  out_error->message = make_utf8_view(message, message_size);
}

template <std::size_t N>
[[nodiscard]] ksj_status reject(ksj_error_view* out_error, const ksj_status status, const char (&message)[N]) noexcept {
  set_error(out_error, status, message, static_cast<std::uint64_t>(N - 1U));
  return status;
}

[[nodiscard]] bool matches_operator_id(const ksj_utf8_view& operator_id) noexcept {
  constexpr std::size_t kOperatorIdSize = sizeof(kOperatorId) - 1U;
  return has_full_compatible_header(&operator_id) && operator_id.data != nullptr &&
         operator_id.size == kOperatorIdSize && std::memcmp(operator_id.data, kOperatorId, kOperatorIdSize) == 0;
}

[[nodiscard]] bool matches_digest(const ksj_digest256& lhs, const ksj_digest256& rhs) noexcept {
  return std::memcmp(lhs.bytes, rhs.bytes, KSJ_PROVIDER_DIGEST256_SIZE) == 0;
}

[[nodiscard]] bool is_canonical_empty_config(const ksj_byte_view& config) noexcept {
  constexpr std::size_t kCanonicalEmptyConfigSize = sizeof(kCanonicalEmptyConfig) - 1U;
  return has_full_compatible_header(&config) && config.data != nullptr && config.size == kCanonicalEmptyConfigSize &&
         std::memcmp(config.data, kCanonicalEmptyConfig, kCanonicalEmptyConfigSize) == 0;
}

[[nodiscard]] bool is_reference_operator(const ksj_provider_operator* operator_handle) noexcept {
  return operator_handle == &kOperatorToken;
}

[[nodiscard]] bool is_reference_context(const ksj_execution_context* context) noexcept {
  return context == &kExecutionContextToken;
}

[[nodiscard]] bool is_reference_key_state(const ksj_key_state* key_state) noexcept {
  return key_state == &kKeyStateToken;
}

[[nodiscard]] bool has_valid_process_result_storage(const ksj_process_result* out_result) noexcept {
  return has_full_compatible_header(out_result);
}

void write_done(ksj_process_result* out_result, const std::uint64_t consumed_input_item_count,
                const std::uint64_t terminal_epoch) noexcept {
  *out_result = {};
  out_result->abi = make_header(sizeof(*out_result));
  out_result->outcome = KSJ_PROVIDER_PROCESS_DONE;
  out_result->sealed_output_count = 0U;
  out_result->consumed_input_item_count = consumed_input_item_count;
  out_result->terminal_epoch = terminal_epoch;
  out_result->async_token = nullptr;
}

[[nodiscard]] bool has_usable_firing_callbacks(const ksj_firing_lease_callbacks_v1* callbacks) noexcept {
  constexpr std::size_t kRequiredSize =
    offsetof(ksj_firing_lease_callbacks_v1, get_input_batch) + sizeof(callbacks->get_input_batch);
  return callbacks != nullptr && has_compatible_header(&callbacks->abi, kRequiredSize) &&
         callbacks->get_info != nullptr && callbacks->get_input_batch != nullptr;
}

[[nodiscard]] bool has_normal_terminal_callbacks(const ksj_firing_lease_callbacks_v1* callbacks) noexcept {
  constexpr std::size_t kRequiredSize =
    offsetof(ksj_firing_lease_callbacks_v1, output_grants) + sizeof(callbacks->output_grants);
  return callbacks != nullptr && has_compatible_header(&callbacks->abi, kRequiredSize);
}

[[nodiscard]] ksj_operator_descriptor make_operator_descriptor() noexcept;
[[nodiscard]] ksj_provider_descriptor
make_provider_descriptor(const ksj_operator_descriptor* operator_descriptor) noexcept;

struct ProviderMetadata {
  ProviderMetadata() noexcept
      : operator_descriptor(make_operator_descriptor()),
        provider_descriptor(make_provider_descriptor(&operator_descriptor)) {}

  ksj_operator_descriptor operator_descriptor{};
  ksj_provider_descriptor provider_descriptor{};
};

[[nodiscard]] ksj_operator_descriptor make_operator_descriptor() noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(kOperatorId, sizeof(kOperatorId) - 1U);
  descriptor.interface_revision = 1U;
  descriptor.max_in_flight = kMaximumInFlight;
  descriptor.interface_digest = make_digest(0x31U);
  descriptor.contract_digest = make_digest(0x61U);
  descriptor.thread_safety = KSJ_PROVIDER_FULLY_REENTRANT;
  descriptor.max_private_threads = 0U;
  descriptor.max_input_items_per_firing = kMaximumInputItemsPerFiring;
  descriptor.max_output_items_per_firing = 0U;
  descriptor.max_output_bytes_per_firing = 0U;
  descriptor.max_scratch_bytes_per_firing = 0U;
  descriptor.max_retained_input_bytes = 0U;
  descriptor.max_async_tail_bytes = 0U;
  return descriptor;
}

[[nodiscard]] ksj_provider_descriptor
make_provider_descriptor(const ksj_operator_descriptor* operator_descriptor) noexcept {
  ksj_provider_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS | KSJ_PROVIDER_CAP_NO_PRIVATE_THREADS |
                                      KSJ_PROVIDER_CAP_NO_DIRECT_FILE_IO | KSJ_PROVIDER_CAP_NO_DIRECT_NETWORK_IO);
  descriptor.provider_id = make_utf8_view(kProviderId, sizeof(kProviderId) - 1U);
  descriptor.version.abi = make_header(sizeof(descriptor.version));
  descriptor.version.major = 1U;
  descriptor.version.minor = 0U;
  descriptor.version.patch = 0U;
  descriptor.version.prerelease = 0U;
  descriptor.provider_abi_major = KSJ_PROVIDER_ABI_MAJOR;
  descriptor.provider_abi_minor = KSJ_PROVIDER_ABI_MINOR;
  descriptor.bundle_digest = make_digest(0xA1U);
  descriptor.operator_count = 1U;
  descriptor.reserved0 = 0U;
  descriptor.operators = operator_descriptor;
  return descriptor;
}

[[nodiscard]] const ProviderMetadata& provider_metadata() noexcept {
  static const ProviderMetadata metadata{};
  return metadata;
}

ksj_status KSJ_PROVIDER_CALL operator_create(const ksj_operator_create_request* request,
                                             ksj_provider_operator** out_operator, ksj_error_view* out_error) noexcept {
  if (!has_full_compatible_header(request) || out_operator == nullptr ||
      !has_full_compatible_header(&request->required_contract_digest)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  *out_operator = nullptr;
  if (!matches_operator_id(request->operator_id)) {
    return reject(out_error, KSJ_STATUS_UNSUPPORTED, kErrorUnsupportedOperator);
  }
  if (!matches_digest(request->required_contract_digest, provider_metadata().operator_descriptor.contract_digest)) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorContractDigest);
  }
  if (!is_canonical_empty_config(request->canonical_config)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorUnsupportedConfig);
  }
  if (request->host_services != nullptr && !has_full_compatible_header(request->host_services)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
  }

  *out_operator = &kOperatorToken;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL execution_context_create(ksj_provider_operator* operator_handle,
                                                      const ksj_execution_context_descriptor* descriptor,
                                                      ksj_execution_context** out_context,
                                                      ksj_error_view* out_error) noexcept {
  if (!is_reference_operator(operator_handle) || !has_full_compatible_header(descriptor) || out_context == nullptr) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (descriptor->host_services != nullptr && !has_full_compatible_header(descriptor->host_services)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
  }

  *out_context = &kExecutionContextToken;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL key_state_init(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                            const ksj_key_state_descriptor* descriptor, ksj_key_state** out_key_state,
                                            ksj_error_view* out_error) noexcept {
  if (!is_reference_operator(operator_handle) || !is_reference_context(context) ||
      !has_full_compatible_header(descriptor) || out_key_state == nullptr) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }

  *out_key_state = &kKeyStateToken;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_start(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                               ksj_key_state* key_state, const ksj_scan_start_descriptor* descriptor,
                                               ksj_error_view* out_error) noexcept {
  if (!is_reference_operator(operator_handle) || !is_reference_context(context) || !is_reference_key_state(key_state) ||
      !has_full_compatible_header(descriptor)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_process_batch(ksj_provider_operator* operator_handle,
                                                    ksj_execution_context* context, ksj_key_state* key_state,
                                                    ksj_firing_lease* lease,
                                                    const ksj_firing_lease_callbacks_v1* lease_callbacks,
                                                    ksj_process_result* out_result,
                                                    ksj_error_view* out_error) noexcept {
  if (!is_reference_operator(operator_handle) || !is_reference_context(context) || !is_reference_key_state(key_state) ||
      lease == nullptr || !has_valid_process_result_storage(out_result)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (!has_usable_firing_callbacks(lease_callbacks)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorHostLease);
  }

  ksj_firing_lease_info lease_info{};
  lease_info.abi = make_header(sizeof(lease_info));
  const ksj_status lease_status =
    lease_callbacks->get_info(lease_callbacks->host_context, lease, &lease_info, out_error);
  if (lease_status != KSJ_STATUS_OK) {
    return lease_status;
  }
  constexpr std::size_t kLeaseInfoRequiredSize =
    offsetof(ksj_firing_lease_info, terminal_epoch) + sizeof(lease_info.terminal_epoch);
  if (!has_compatible_header(&lease_info.abi, kLeaseInfoRequiredSize)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorHostLease);
  }
  if (lease_info.input_batch_count > kMaximumInputBatchesPerFiring) {
    return reject(out_error, KSJ_STATUS_RESOURCE_EXHAUSTED, kErrorInputBound);
  }

  std::uint64_t consumed_item_count = 0U;
  for (std::uint32_t batch_index = 0U; batch_index < lease_info.input_batch_count; ++batch_index) {
    ksj_input_batch_view input_batch{};
    input_batch.abi = make_header(sizeof(input_batch));
    const ksj_status batch_status =
      lease_callbacks->get_input_batch(lease_callbacks->host_context, lease, batch_index, &input_batch, out_error);
    if (batch_status != KSJ_STATUS_OK) {
      return batch_status;
    }
    constexpr std::size_t kInputBatchRequiredSize =
      offsetof(ksj_input_batch_view, item_count) + sizeof(input_batch.item_count);
    if (!has_compatible_header(&input_batch.abi, kInputBatchRequiredSize)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorHostLease);
    }
    if (input_batch.item_count > kMaximumInputItemsPerFiring - consumed_item_count) {
      return reject(out_error, KSJ_STATUS_RESOURCE_EXHAUSTED, kErrorInputBound);
    }
    consumed_item_count += input_batch.item_count;
  }

  // The reference operator consumes host-declared input counts only.  It
  // neither maps payload bytes nor asks for an OutputGrant, keeping this M1
  // conformance path free of hidden allocation or image-publication semantics.
  write_done(out_result, consumed_item_count, lease_info.terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_scan_end(ksj_provider_operator* operator_handle,
                                                  ksj_execution_context* context, ksj_key_state* key_state,
                                                  const ksj_scan_end_descriptor* descriptor,
                                                  ksj_firing_lease* terminal_lease,
                                                  const ksj_firing_lease_callbacks_v1* lease_callbacks,
                                                  ksj_process_result* out_result, ksj_error_view* out_error) noexcept {
  if (!is_reference_operator(operator_handle) || !is_reference_context(context) || !is_reference_key_state(key_state) ||
      !has_full_compatible_header(descriptor) || terminal_lease == nullptr ||
      !has_valid_process_result_storage(out_result)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (descriptor->kind != KSJ_PROVIDER_SCAN_END_NORMAL) {
    return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTerminalKind);
  }
  if (!has_normal_terminal_callbacks(lease_callbacks)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorHostLease);
  }

  // Normal terminal completion is deliberately output-free for this reference
  // sink.  A future image-producing Provider must obtain and seal explicit
  // bounded OutputGrants here; it must never use the cancellation callback.
  write_done(out_result, 0U, descriptor->terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_cancel(ksj_provider_operator* operator_handle, ksj_execution_context* context,
                                                ksj_key_state* key_state, const ksj_cancel_context* context_descriptor,
                                                ksj_error_view* out_error) noexcept {
  if (!is_reference_operator(operator_handle) || !is_reference_context(context) || !is_reference_key_state(key_state) ||
      !has_full_compatible_header(context_descriptor)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (context_descriptor->kind != KSJ_PROVIDER_SCAN_END_CANCELLED &&
      context_descriptor->kind != KSJ_PROVIDER_SCAN_END_FAILED) {
    return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTerminalKind);
  }

  // This callback has no lease, no output grant table and no ordinary MRI data
  // publication route by ABI design.  The reference provider has no pending
  // resources, so bounded cancellation cleanup is a no-op.
  return KSJ_STATUS_OK;
}

void KSJ_PROVIDER_CALL key_state_reset(ksj_provider_operator*, ksj_execution_context*, ksj_key_state*) noexcept {}

void KSJ_PROVIDER_CALL execution_context_destroy(ksj_provider_operator*, ksj_execution_context*) noexcept {}

void KSJ_PROVIDER_CALL operator_destroy(ksj_provider_operator*) noexcept {}

[[nodiscard]] ksj_provider_api_v1 make_provider_api() noexcept {
  ksj_provider_api_v1 api{};
  api.abi = make_header(sizeof(api));
  api.operator_create = &operator_create;
  api.execution_context_create = &execution_context_create;
  api.key_state_init = &key_state_init;
  api.operator_on_start = &operator_on_start;
  api.operator_process_batch = &operator_process_batch;
  api.operator_on_scan_end = &operator_on_scan_end;
  api.operator_on_cancel = &operator_on_cancel;
  api.key_state_reset = &key_state_reset;
  api.execution_context_destroy = &execution_context_destroy;
  api.operator_destroy = &operator_destroy;
  return api;
}

[[nodiscard]] const ksj_provider_api_v1& provider_api() noexcept {
  static const ksj_provider_api_v1 api = make_provider_api();
  return api;
}

[[nodiscard]] ksj_status provider_query_impl(const ksj_provider_query_request* request,
                                             ksj_provider_descriptor* out_descriptor, ksj_provider_api_v1* out_api,
                                             ksj_error_view* out_error) noexcept {
  if (!has_full_compatible_header(request) || !has_full_compatible_header(out_descriptor) ||
      !has_full_compatible_header(out_api)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
  }
  if (request->minimum_abi_minor > KSJ_PROVIDER_ABI_MINOR || request->maximum_abi_minor < KSJ_PROVIDER_ABI_MINOR) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
  }

  *out_descriptor = provider_metadata().provider_descriptor;
  *out_api = provider_api();
  return KSJ_STATUS_OK;
}

} // namespace

// The C header cannot spell noexcept while remaining C11-compatible.  Keep
// the exported ABI boundary exception-safe explicitly instead.
KSJ_PROVIDER_ENTRY ksj_status KSJ_PROVIDER_CALL ksj_provider_query(const ksj_provider_query_request* request,
                                                                   ksj_provider_descriptor* out_descriptor,
                                                                   ksj_provider_api_v1* out_api,
                                                                   ksj_error_view* out_error) {
  try {
    return provider_query_impl(request, out_descriptor, out_api, out_error);
  } catch (...) {
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}
