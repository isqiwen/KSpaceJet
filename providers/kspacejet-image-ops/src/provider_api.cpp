// SPDX-License-Identifier: Apache-2.0
//
// Provider-level metadata and lifecycle dispatch. Individual image transforms
// live in src/operators so their contracts and computation stay independent.

#include "provider_api.hpp"
#include "provider_state.hpp"
#include "operators/image_clamp_float32.hpp"
#include "operators/image_offset_float32.hpp"
#include "operators/image_scale_float32.hpp"

#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <new>

namespace ksj::image_ops::api {

using ::ksj::image_ops::operators::image_clamp_float32_operator;
using ::ksj::image_ops::operators::image_offset_float32_operator;
using ::ksj::image_ops::operators::image_scale_float32_operator;
using ::ksj::image_ops::state::ImageOperatorImplementation;
using ::ksj::provider::detail::has_full_compatible_header;
using ::ksj::provider::detail::has_usable_firing_callbacks;
using ::ksj::provider::detail::has_usable_host_memory;
using ::ksj::provider::detail::has_valid_type_descriptor;
using ::ksj::provider::detail::make_bundle_digest_from_hex;
using ::ksj::provider::detail::make_header;
using ::ksj::provider::detail::make_utf8_view;
using ::ksj::provider::detail::OutputGrantGuard;
using ::ksj::provider::detail::reject;
using ::ksj::provider::detail::text_equals;
using ::ksj::provider::detail::valid_borrowed_bytes;
using ::ksj::provider::detail::write_done;

namespace {

constexpr char kProviderId[] = "org.kspacejet.image-ops";
// SHA-256 of the NUL-delimited Provider bundle fields documented in README:
// domain, provider ID, then descriptor-order operator IDs.
constexpr char kProviderBundleDigestHex[] = "8db8232be7b7166a6e6aa2ddd896d1a8ef9aa6c1811ba3530b3796d60f5e6b53";

constexpr char kErrorBadAbi[] = "Image-ops Provider received incompatible ABI storage";
constexpr char kErrorInvalidArgument[] = "Image-ops Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] = "Image-ops Provider does not expose the requested operator";
constexpr char kErrorLease[] = "Image-ops Provider requires one image, one output grant, and no scratch";
constexpr char kErrorImage[] = "Image-ops Provider requires one exact ksj.image-frame payload";
constexpr char kErrorOutput[] = "Image-ops Provider requires one exact ksj.image-frame output grant";
constexpr char kErrorInternal[] = "Image-ops Provider trapped an unexpected internal exception";

constexpr std::uint32_t kInputPort = 0U;
constexpr std::uint32_t kOutputPort = 0U;
constexpr std::uint64_t kMaximumImageBytes = 1024U * 1024U;
constexpr std::uint32_t kRequiredAlignment = 64U;

[[nodiscard]] bool has_image_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_image_frame(&type) != 0;
}

[[nodiscard]] bool has_valid_image_byte_count(const std::uint64_t byte_count) noexcept {
  return byte_count != 0U && byte_count <= kMaximumImageBytes && byte_count % sizeof(float) == 0U;
}

[[nodiscard]] const std::array<const ImageOperatorImplementation*, 3U>& image_operators() noexcept {
  static const std::array<const ImageOperatorImplementation*, 3U> implementations{
    &image_scale_float32_operator(),
    &image_offset_float32_operator(),
    &image_clamp_float32_operator(),
  };
  return implementations;
}

[[nodiscard]] const ImageOperatorImplementation* find_image_operator(const ksj_utf8_view& id) noexcept {
  for (const auto* const implementation : image_operators()) {
    if (text_equals(id, implementation->id.data(), implementation->id.size())) {
      return implementation;
    }
  }
  return nullptr;
}

[[nodiscard]] ksj_operator_descriptor
make_operator_descriptor(const ImageOperatorImplementation& implementation) noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(implementation.id.data(), implementation.id.size());
  descriptor.max_in_flight = 1U;
  descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  descriptor.max_private_threads = 0U;
  descriptor.max_input_items_per_firing = 1U;
  descriptor.max_output_items_per_firing = 1U;
  descriptor.max_output_bytes_per_firing = kMaximumImageBytes;
  descriptor.max_scratch_bytes_per_firing = 0U;
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
      : operator_descriptors{make_operator_descriptor(*image_operators()[0U]),
                             make_operator_descriptor(*image_operators()[1U]),
                             make_operator_descriptor(*image_operators()[2U])},
        descriptor(make_provider_descriptor(operator_descriptors.data(),
                                            static_cast<std::uint32_t>(operator_descriptors.size()))) {}

  std::array<ksj_operator_descriptor, 3U> operator_descriptors{};
  ksj_provider_descriptor descriptor{};
};

[[nodiscard]] const ProviderMetadata& provider_metadata() noexcept {
  static const ProviderMetadata metadata{};
  return metadata;
}

[[nodiscard]] const ksj_provider_descriptor& provider_descriptor() noexcept {
  return provider_metadata().descriptor;
}

[[nodiscard]] bool is_operator(const ksj_provider_operator* const operator_handle) noexcept {
  return operator_handle != nullptr && operator_handle->implementation != nullptr &&
         operator_handle->implementation->is_valid != nullptr &&
         operator_handle->implementation->transform != nullptr &&
         operator_handle->implementation->is_valid(*operator_handle);
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

ksj_status KSJ_PROVIDER_CALL operator_create(const ksj_operator_create_request* const request,
                                             ksj_provider_operator** const out_operator,
                                             ksj_error_view* const out_error) noexcept {
  try {
    if (!has_full_compatible_header(request) || out_operator == nullptr) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
    }
    *out_operator = nullptr;
    const auto* const implementation = find_image_operator(request->operator_id);
    if (implementation == nullptr) {
      return reject(out_error, KSJ_STATUS_UNSUPPORTED, kErrorUnsupportedOperator,
                    sizeof(kErrorUnsupportedOperator) - 1U);
    }
    if (request->host_services != nullptr && !has_full_compatible_header(request->host_services)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi, sizeof(kErrorBadAbi) - 1U);
    }

    if (implementation->configure == nullptr || implementation->unsupported_config_error == nullptr) {
      return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
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
    KSJ_LOG_ERROR("Image-ops Provider trapped an unexpected exception while creating an Operator");
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
  try {
    if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) || lease == nullptr ||
        !has_full_compatible_header(out_result)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument, sizeof(kErrorInvalidArgument) - 1U);
    }
    if (!has_usable_firing_callbacks(callbacks)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi, sizeof(kErrorBadAbi) - 1U);
    }

    ksj_firing_lease_info info{};
    info.abi = make_header(sizeof(info));
    const auto info_status = callbacks->get_info(callbacks->host_context, lease, &info, out_error);
    if (info_status != KSJ_STATUS_OK || !has_full_compatible_header(&info) || info.input_batch_count != 1U ||
        info.output_grant_count != 1U || info.reserved_scratch_bytes != 0U) {
      return info_status == KSJ_STATUS_OK
               ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease, sizeof(kErrorLease) - 1U)
               : info_status;
    }

    ksj_input_batch_view batch{};
    batch.abi = make_header(sizeof(batch));
    const auto batch_status = callbacks->get_input_batch(callbacks->host_context, lease, 0U, &batch, out_error);
    if (batch_status != KSJ_STATUS_OK) {
      return batch_status;
    }
    if (!has_full_compatible_header(&batch) || batch.input_port != kInputPort || batch.item_count != 1U ||
        batch.items == nullptr || !has_full_compatible_header(&batch.items[0U])) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorImage, sizeof(kErrorImage) - 1U);
    }
    const auto& input = batch.items[0U];
    if (!has_full_compatible_header(&input.payload) || input.payload.data == nullptr ||
        !has_valid_image_byte_count(input.payload.byte_count) ||
        input.payload.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(input.payload.data, input.payload.alignment, kRequiredAlignment) ||
        !valid_borrowed_bytes(input.metadata) || !has_image_type(input.payload.type) ||
        info.reserved_output_bytes < input.payload.byte_count) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorImage, sizeof(kErrorImage) - 1U);
    }

    ksj_output_grant* grant = nullptr;
    const auto acquire_status =
      callbacks->acquire_output_grant(callbacks->host_context, lease, kOutputPort, &grant, out_error);
    if (acquire_status != KSJ_STATUS_OK || grant == nullptr) {
      return acquire_status == KSJ_STATUS_OK
               ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput, sizeof(kErrorOutput) - 1U)
               : acquire_status;
    }
    OutputGrantGuard grant_guard{.callbacks = callbacks->output_grants, .grant = grant};

    ksj_mutable_payload_view output{};
    output.abi = make_header(sizeof(output));
    const auto map_status =
      callbacks->output_grants->map_mutable_payload(callbacks->output_grants->host_context, grant, &output, out_error);
    if (map_status != KSJ_STATUS_OK) {
      return map_status;
    }
    if (!has_full_compatible_header(&output) || output.data == nullptr ||
        output.capacity_bytes < input.payload.byte_count || output.committed_bytes != 0U ||
        output.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(output.data, output.alignment, kRequiredAlignment) || !has_image_type(output.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput, sizeof(kErrorOutput) - 1U);
    }

    const auto* const source = static_cast<const std::byte*>(input.payload.data);
    auto* const destination = static_cast<std::byte*>(output.data);
    for (std::uint64_t offset = 0U; offset < input.payload.byte_count; offset += sizeof(float)) {
      float value = 0.0F;
      std::memcpy(&value, source + static_cast<std::size_t>(offset), sizeof(value));
      value = operator_handle->implementation->transform(*operator_handle, value);
      std::memcpy(destination + static_cast<std::size_t>(offset), &value, sizeof(value));
    }

    ksj_output_seal_descriptor seal{};
    seal.abi = make_header(sizeof(seal));
    seal.output_port = kOutputPort;
    seal.produced_item_count = 1U;
    seal.produced_byte_count = input.payload.byte_count;
    seal.semantic_key_hash = input.semantic_key_hash;
    seal.order_key = input.order_key;
    seal.type = output.type;
    seal.metadata = input.metadata;
    const auto seal_status =
      callbacks->output_grants->seal(callbacks->output_grants->host_context, grant, &seal, out_error);
    if (seal_status != KSJ_STATUS_OK) {
      return seal_status;
    }
    grant_guard.settled = true;
    write_done(out_result, 1U, 1U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("Image-ops Provider trapped an unexpected exception while processing a firing");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
  }
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
    KSJ_LOG_ERROR("Image-ops Provider trapped an unexpected exception while answering provider query");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
  }
}

} // namespace ksj::image_ops::api
