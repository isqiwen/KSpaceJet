// SPDX-License-Identifier: Apache-2.0
//
// Provider descriptor construction and synchronous lifecycle dispatch. Each
// reconstruction algorithm remains isolated in src/operators/<operator>.cpp.

#include "provider_api.hpp"

#include "operators/noncartesian_adjoint_reconstruct.hpp"
#include "operators/radial_gridding_reconstruct.hpp"
#include "provider_state.hpp"

#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"

#include <array>
#include <cstdint>
#include <new>

namespace ksj::noncartesian_recon::api {

using ::ksj::noncartesian_recon::state::NonCartesianReconOperatorImplementation;
using namespace ksj::provider::detail;

namespace {

constexpr char kProviderId[] = "org.kspacejet.noncartesian-recon";
// SHA-256 of the documented NUL-delimited bundle identity fields: domain,
// Provider ID, then descriptor-order Operator IDs.
constexpr char kProviderBundleDigestHex[] = "4297da20fe070aae1988f456967aeed82d5bae62f8aad41585e25fe767000ef6";
constexpr char kErrorBadAbi[] = "Non-Cartesian reconstruction Provider received incompatible ABI storage";
constexpr char kErrorInvalidArgument[] = "Non-Cartesian reconstruction Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] =
  "Non-Cartesian reconstruction Provider does not expose the requested operator";
constexpr char kErrorInternal[] = "Non-Cartesian reconstruction Provider trapped an unexpected internal exception";

[[nodiscard]] const std::array<const NonCartesianReconOperatorImplementation*, 2U>&
noncartesian_recon_operators() noexcept {
  static const std::array<const NonCartesianReconOperatorImplementation*, 2U> implementations{
    &operators::noncartesian_adjoint_reconstruct_operator(),
    &operators::radial_gridding_reconstruct_operator(),
  };
  return implementations;
}

[[nodiscard]] const NonCartesianReconOperatorImplementation*
find_noncartesian_recon_operator(const ksj_utf8_view& id) noexcept {
  for (const auto* const implementation : noncartesian_recon_operators()) {
    if (text_equals(id, implementation->id.data(), implementation->id.size())) {
      return implementation;
    }
  }
  return nullptr;
}

[[nodiscard]] bool
is_registered_implementation(const NonCartesianReconOperatorImplementation* implementation) noexcept {
  for (const auto* const candidate : noncartesian_recon_operators()) {
    if (candidate == implementation) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] ksj_operator_descriptor
make_operator_descriptor(const NonCartesianReconOperatorImplementation& implementation) noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(implementation.id.data(), implementation.id.size());
  descriptor.max_in_flight = 1U;
  descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  descriptor.max_private_threads = 0U;
  descriptor.max_input_items_per_firing = 2U;
  descriptor.max_output_items_per_firing = 1U;
  descriptor.max_output_bytes_per_firing = implementation.max_output_bytes_per_firing;
  descriptor.max_scratch_bytes_per_firing = implementation.max_scratch_bytes_per_firing;
  descriptor.max_retained_input_bytes = 0U;
  descriptor.max_async_tail_bytes = 0U;
  return descriptor;
}

[[nodiscard]] ksj_provider_descriptor
make_provider_descriptor(const ksj_operator_descriptor* const operator_descriptors,
                         const std::uint32_t operator_count) noexcept {
  ksj_provider_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_PROVIDER_CAP_SYNC_PROCESS | KSJ_PROVIDER_CAP_NO_PRIVATE_THREADS |
                                      KSJ_PROVIDER_CAP_NO_DIRECT_FILE_IO | KSJ_PROVIDER_CAP_NO_DIRECT_NETWORK_IO);
  descriptor.provider_id = make_utf8_view(kProviderId, sizeof(kProviderId) - 1U);
  descriptor.bundle_digest = make_bundle_digest_from_hex(kProviderBundleDigestHex);
  descriptor.operator_count = operator_count;
  descriptor.reserved0 = 0U;
  descriptor.operators = operator_descriptors;
  return descriptor;
}

struct ProviderMetadata {
  ProviderMetadata() noexcept
      : operator_descriptors{make_operator_descriptor(*noncartesian_recon_operators()[0U]),
                             make_operator_descriptor(*noncartesian_recon_operators()[1U])},
        descriptor(make_provider_descriptor(operator_descriptors.data(),
                                            static_cast<std::uint32_t>(operator_descriptors.size()))) {}

  std::array<ksj_operator_descriptor, 2U> operator_descriptors{};
  ksj_provider_descriptor descriptor{};
};

[[nodiscard]] const ProviderMetadata& provider_metadata() noexcept {
  static const ProviderMetadata metadata{};
  return metadata;
}

[[nodiscard]] bool is_operator(const ksj_provider_operator* const operator_handle) noexcept {
  if (operator_handle == nullptr || !is_registered_implementation(operator_handle->implementation)) {
    return false;
  }
  const auto& implementation = *operator_handle->implementation;
  return !implementation.id.empty() && implementation.unsupported_config_error != nullptr &&
         implementation.unsupported_config_error_size != 0U && implementation.max_output_bytes_per_firing != 0U &&
         implementation.max_scratch_bytes_per_firing != 0U && implementation.configure != nullptr &&
         implementation.is_valid != nullptr && implementation.process != nullptr &&
         implementation.is_valid(*operator_handle);
}

[[nodiscard]] bool is_context(const ksj_provider_operator* const operator_handle,
                              const ksj_execution_context* const context) noexcept {
  return is_operator(operator_handle) && context == &operator_handle->context && context->owner == operator_handle &&
         operator_handle->context_active;
}

[[nodiscard]] bool is_key_state(const ksj_provider_operator* const operator_handle,
                                const ksj_key_state* const key_state) noexcept {
  return is_operator(operator_handle) && key_state == &operator_handle->key_state &&
         key_state->owner == operator_handle && operator_handle->key_state_active;
}

} // namespace

const ksj_provider_descriptor& provider_descriptor() noexcept {
  return provider_metadata().descriptor;
}

ksj_status KSJ_PROVIDER_CALL operator_create(const ksj_operator_create_request* const request,
                                             ksj_provider_operator** const out_operator,
                                             ksj_error_view* const out_error) noexcept {
  try {
    if (!has_full_compatible_header(request) || out_operator == nullptr) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
    }
    *out_operator = nullptr;
    const auto* const implementation = find_noncartesian_recon_operator(request->operator_id);
    if (implementation == nullptr) {
      return reject(out_error, KSJ_STATUS_UNSUPPORTED, kErrorUnsupportedOperator,
                    sizeof(kErrorUnsupportedOperator) - 1U);
    }
    if (request->host_services != nullptr && !has_full_compatible_header(request->host_services)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi, sizeof(kErrorBadAbi) - 1U);
    }

    ksj_provider_operator configured{.implementation = implementation};
    if (!implementation->configure(request->canonical_config, configured)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, implementation->unsupported_config_error,
                    implementation->unsupported_config_error_size);
    }
    if (!is_operator(&configured)) {
      return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
    }

    auto* const operator_handle = new (std::nothrow) ksj_provider_operator{configured};
    if (operator_handle == nullptr) {
      return reject(out_error, KSJ_STATUS_RESOURCE_EXHAUSTED, kErrorInternal, sizeof(kErrorInternal) - 1U);
    }
    *out_operator = operator_handle;
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("Non-Cartesian reconstruction Provider trapped an unexpected exception while creating an Operator");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
  }
}

ksj_status KSJ_PROVIDER_CALL execution_context_create(ksj_provider_operator* const operator_handle,
                                                      const ksj_execution_context_descriptor* const descriptor,
                                                      ksj_execution_context** const out_context,
                                                      ksj_error_view* const out_error) noexcept {
  if (!is_operator(operator_handle) || !has_full_compatible_header(descriptor) || out_context == nullptr) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  if (descriptor->host_services != nullptr && !has_full_compatible_header(descriptor->host_services)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi, sizeof(kErrorBadAbi) - 1U);
  }
  if (operator_handle->context_active) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  operator_handle->context.owner = operator_handle;
  operator_handle->context_active = true;
  *out_context = &operator_handle->context;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL key_state_init(ksj_provider_operator* const operator_handle,
                                            ksj_execution_context* const context,
                                            const ksj_key_state_descriptor* const descriptor,
                                            ksj_key_state** const out_key_state,
                                            ksj_error_view* const out_error) noexcept {
  if (!is_context(operator_handle, context) || !has_full_compatible_header(descriptor) || out_key_state == nullptr ||
      !valid_borrowed_bytes(descriptor->semantic_key)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  if (operator_handle->key_state_active) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  operator_handle->key_state.owner = operator_handle;
  operator_handle->key_state_active = true;
  *out_key_state = &operator_handle->key_state;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_start(ksj_provider_operator* const operator_handle,
                                               ksj_execution_context* const context, ksj_key_state* const key_state,
                                               const ksj_scan_start_descriptor* const descriptor,
                                               ksj_error_view* const out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_process_batch(ksj_provider_operator* const operator_handle,
                                                    ksj_execution_context* const context,
                                                    ksj_key_state* const key_state, ksj_firing_lease* const lease,
                                                    const ksj_firing_lease_callbacks* const callbacks,
                                                    ksj_process_result* const out_result,
                                                    ksj_error_view* const out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) || lease == nullptr ||
      !has_full_compatible_header(out_result)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  return operator_handle->implementation->process(*operator_handle, lease, callbacks, out_result, out_error);
}

ksj_status KSJ_PROVIDER_CALL operator_on_scan_end(ksj_provider_operator* const operator_handle,
                                                  ksj_execution_context* const context, ksj_key_state* const key_state,
                                                  const ksj_scan_end_descriptor* const descriptor,
                                                  ksj_firing_lease* const terminal_lease,
                                                  const ksj_firing_lease_callbacks* const callbacks,
                                                  ksj_process_result* const out_result,
                                                  ksj_error_view* const out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor) || descriptor->kind != KSJ_PROVIDER_SCAN_END_NORMAL ||
      descriptor->reserved0 != 0U || terminal_lease == nullptr || !has_full_compatible_header(out_result) ||
      !has_full_compatible_header(callbacks)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  write_done(out_result, 0U, 0U, descriptor->terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL operator_on_cancel(ksj_provider_operator* const operator_handle,
                                                ksj_execution_context* const context, ksj_key_state* const key_state,
                                                const ksj_cancel_context* const descriptor,
                                                ksj_error_view* const out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor) || descriptor->reserved0 != 0U ||
      (descriptor->kind != KSJ_PROVIDER_SCAN_END_CANCELLED && descriptor->kind != KSJ_PROVIDER_SCAN_END_FAILED)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
  }
  return KSJ_STATUS_OK;
}

void KSJ_PROVIDER_CALL key_state_reset(ksj_provider_operator* const operator_handle,
                                       ksj_execution_context* const context, ksj_key_state* const key_state) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state)) {
    return;
  }
  operator_handle->key_state.owner = nullptr;
  operator_handle->key_state_active = false;
}

void KSJ_PROVIDER_CALL execution_context_destroy(ksj_provider_operator* const operator_handle,
                                                 ksj_execution_context* const context) noexcept {
  if (!is_context(operator_handle, context)) {
    return;
  }
  operator_handle->context.owner = nullptr;
  operator_handle->context_active = false;
}

void KSJ_PROVIDER_CALL operator_destroy(ksj_provider_operator* const operator_handle) noexcept {
  delete operator_handle;
}

ksj_status provider_query(const ksj_provider_query_request* const request,
                          ksj_provider_descriptor* const out_descriptor, ksj_provider_api* const out_api,
                          ksj_error_view* const out_error) noexcept {
  try {
    if (!has_full_compatible_header(request) || !has_full_compatible_header(out_descriptor) ||
        !has_full_compatible_header(out_api)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi, sizeof(kErrorBadAbi) - 1U);
    }
    if (!has_full_compatible_header(&request->host_build_id) ||
        (request->host_build_id.size != 0U && request->host_build_id.data == nullptr)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi, sizeof(kErrorBadAbi) - 1U);
    }
    *out_descriptor = provider_descriptor();
    *out_api = {};
    out_api->abi = make_header(sizeof(*out_api));
    out_api->operator_create = &operator_create;
    out_api->execution_context_create = &execution_context_create;
    out_api->key_state_init = &key_state_init;
    out_api->operator_on_start = &operator_on_start;
    out_api->operator_process_batch = &operator_process_batch;
    out_api->operator_on_scan_end = &operator_on_scan_end;
    out_api->operator_on_cancel = &operator_on_cancel;
    out_api->key_state_reset = &key_state_reset;
    out_api->execution_context_destroy = &execution_context_destroy;
    out_api->operator_destroy = &operator_destroy;
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR(
      "Non-Cartesian reconstruction Provider trapped an unexpected exception while answering provider query");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
  }
}

} // namespace ksj::noncartesian_recon::api
