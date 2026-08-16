// SPDX-License-Identifier: Apache-2.0
//
// Provider descriptor construction and synchronous lifecycle dispatch. Each
// conditioning algorithm remains isolated in src/operators/<operator>.cpp.

#include "provider_api.hpp"

#include "operators/coil_compress.hpp"
#include "operators/noise_prewhiten.hpp"
#include "operators/noncartesian_coil_compress.hpp"
#include "operators/noncartesian_noise_prewhiten.hpp"
#include "operators/phase_correct.hpp"
#include "operators/readout_oversampling_remove.hpp"
#include "provider_state.hpp"
#include "support/kspace_frame.hpp"

#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <new>

namespace ksj::kspace_conditioning::api {

using ::ksj::kspace_conditioning::operators::coil_compress_operator;
using ::ksj::kspace_conditioning::operators::noise_prewhiten_operator;
using ::ksj::kspace_conditioning::operators::noncartesian_coil_compress_operator;
using ::ksj::kspace_conditioning::operators::noncartesian_noise_prewhiten_operator;
using ::ksj::kspace_conditioning::operators::phase_correct_operator;
using ::ksj::kspace_conditioning::operators::readout_oversampling_remove_operator;
using ::ksj::kspace_conditioning::state::ConditioningOperatorImplementation;
using ::ksj::kspace_conditioning::support::calibration_identity_matches_dynamic;
using ::ksj::kspace_conditioning::support::kDynamicInputPort;
using ::ksj::kspace_conditioning::support::kMaximumKspaceBytes;
using ::ksj::kspace_conditioning::support::kOutputPort;
using ::ksj::kspace_conditioning::support::kRequiredAlignment;
using ::ksj::kspace_conditioning::support::kStaticInputPort;
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

[[nodiscard]] const std::array<const ConditioningOperatorImplementation*, 6U>& conditioning_operators() noexcept;

namespace {

constexpr char kProviderId[] = "org.kspacejet.kspace-conditioning";
// SHA-256 of the documented NUL-delimited bundle identity fields: domain,
// Provider ID, then descriptor-order operator IDs.
constexpr char kProviderBundleDigestHex[] = "6ff48e549c4ff3ad41cfebb766e6d298360bf686dc7882f5df9e6401b7dc7325";

constexpr char kErrorBadAbi[] = "K-space-conditioning Provider received incompatible ABI storage";
constexpr char kErrorInvalidArgument[] = "K-space-conditioning Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] = "K-space-conditioning Provider does not expose the requested operator";
constexpr char kErrorLease[] =
  "K-space-conditioning Provider requires the exact configured input batches, one output grant, and no host scratch";
constexpr char kErrorInputPorts[] =
  "K-space-conditioning Provider received absent, duplicate, or incompatible input ABI ports";
constexpr char kErrorDynamicInput[] =
  "K-space-conditioning Provider received an incompatible channel-major k-space input item";
constexpr char kErrorCalibrationInput[] =
  "K-space-conditioning Provider received an incompatible explicit calibration input item";
constexpr char kErrorInputIdentity[] =
  "K-space-conditioning Provider received a keyed calibration item that does not match the dynamic k-space item";
constexpr char kErrorOutput[] = "K-space-conditioning Provider received an incompatible k-space output grant";
constexpr char kErrorTransform[] = "K-space-conditioning Provider could not transform the requested k-space item";
constexpr char kErrorInternal[] = "K-space-conditioning Provider trapped an unexpected internal exception";

[[nodiscard]] ksj_operator_descriptor
make_operator_descriptor(const ConditioningOperatorImplementation& implementation) noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(implementation.id.data(), implementation.id.size());
  descriptor.max_in_flight = 1U;
  descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  descriptor.max_private_threads = 0U;
  descriptor.max_input_items_per_firing = implementation.input_batch_count;
  descriptor.max_output_items_per_firing = 1U;
  descriptor.max_output_bytes_per_firing = kMaximumKspaceBytes;
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
      : operator_descriptors{make_operator_descriptor(*conditioning_operators()[0U]),
                             make_operator_descriptor(*conditioning_operators()[1U]),
                             make_operator_descriptor(*conditioning_operators()[2U]),
                             make_operator_descriptor(*conditioning_operators()[3U]),
                             make_operator_descriptor(*conditioning_operators()[4U]),
                             make_operator_descriptor(*conditioning_operators()[5U])},
        descriptor(make_provider_descriptor(operator_descriptors.data(),
                                            static_cast<std::uint32_t>(operator_descriptors.size()))) {}

  std::array<ksj_operator_descriptor, 6U> operator_descriptors{};
  ksj_provider_descriptor descriptor{};
};

[[nodiscard]] const ProviderMetadata& provider_metadata() noexcept {
  static const ProviderMetadata metadata{};
  return metadata;
}

[[nodiscard]] bool has_expected_static_input(const ConditioningOperatorImplementation& implementation) noexcept {
  return implementation.input_batch_count == 2U && implementation.matches_static_input_type != nullptr &&
         implementation.expected_static_input_byte_count != nullptr;
}

[[nodiscard]] bool is_operator(const ksj_provider_operator* const operator_handle) noexcept {
  if (operator_handle == nullptr || operator_handle->implementation == nullptr) {
    return false;
  }
  const auto& implementation = *operator_handle->implementation;
  if (implementation.configure == nullptr || implementation.is_valid == nullptr ||
      implementation.matches_dynamic_input_type == nullptr || implementation.matches_output_type == nullptr ||
      implementation.output_type == nullptr || implementation.expected_dynamic_input_byte_count == nullptr ||
      implementation.expected_output_byte_count == nullptr || implementation.transform == nullptr ||
      !implementation.is_valid(*operator_handle)) {
    return false;
  }
  return implementation.input_batch_count == 1U || has_expected_static_input(implementation);
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

[[nodiscard]] bool has_valid_input_item(const ksj_input_item_view& item,
                                        bool (*const matches_type)(const ksj_type_descriptor_view&) noexcept,
                                        const std::uint64_t expected_byte_count) noexcept {
  return has_full_compatible_header(&item) && has_full_compatible_header(&item.payload) &&
         item.payload.data != nullptr && item.payload.byte_count == expected_byte_count &&
         item.payload.memory_domain == KSJ_PROVIDER_MEMORY_HOST_PAGEABLE &&
         has_usable_host_memory(item.payload.data, item.payload.alignment, kRequiredAlignment) &&
         valid_borrowed_bytes(item.metadata) && item.metadata.size == 0U && matches_type != nullptr &&
         matches_type(item.payload.type);
}

struct InputSelection {
  const ksj_input_item_view* dynamic_item{nullptr};
  const ksj_input_item_view* calibration_item{nullptr};
};

[[nodiscard]] ksj_status select_exact_input_ports(const ConditioningOperatorImplementation& implementation,
                                                  ksj_firing_lease* const lease,
                                                  const ksj_firing_lease_callbacks* const callbacks,
                                                  InputSelection& selection, ksj_error_view* const out_error) noexcept {
  for (std::uint32_t batch_index = 0U; batch_index < implementation.input_batch_count; ++batch_index) {
    ksj_input_batch_view batch{};
    batch.abi = make_header(sizeof(batch));
    const auto batch_status =
      callbacks->get_input_batch(callbacks->host_context, lease, batch_index, &batch, out_error);
    if (batch_status != KSJ_STATUS_OK) {
      return batch_status;
    }
    if (!has_full_compatible_header(&batch) || batch.item_count != 1U || batch.items == nullptr ||
        !has_full_compatible_header(&batch.items[0U])) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorInputPorts, sizeof(kErrorInputPorts) - 1U);
    }
    if (batch.input_port == kDynamicInputPort) {
      if (selection.dynamic_item != nullptr) {
        return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorInputPorts, sizeof(kErrorInputPorts) - 1U);
      }
      selection.dynamic_item = &batch.items[0U];
      continue;
    }
    if (implementation.input_batch_count == 2U && batch.input_port == kStaticInputPort) {
      if (selection.calibration_item != nullptr) {
        return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorInputPorts, sizeof(kErrorInputPorts) - 1U);
      }
      selection.calibration_item = &batch.items[0U];
      continue;
    }
    return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorInputPorts, sizeof(kErrorInputPorts) - 1U);
  }
  if (selection.dynamic_item == nullptr ||
      (implementation.input_batch_count == 2U && selection.calibration_item == nullptr)) {
    return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorInputPorts, sizeof(kErrorInputPorts) - 1U);
  }
  return KSJ_STATUS_OK;
}

} // namespace

const std::array<const ConditioningOperatorImplementation*, 6U>& conditioning_operators() noexcept {
  static const std::array<const ConditioningOperatorImplementation*, 6U> implementations{
    &noise_prewhiten_operator(),
    &phase_correct_operator(),
    &coil_compress_operator(),
    &readout_oversampling_remove_operator(),
    &noncartesian_noise_prewhiten_operator(),
    &noncartesian_coil_compress_operator(),
  };
  return implementations;
}

const ConditioningOperatorImplementation* find_conditioning_operator(const ksj_utf8_view& id) noexcept {
  for (const auto* const implementation : conditioning_operators()) {
    if (text_equals(id, implementation->id.data(), implementation->id.size())) {
      return implementation;
    }
  }
  return nullptr;
}

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
    const auto* const implementation = find_conditioning_operator(request->operator_id);
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
    KSJ_LOG_ERROR("K-space-conditioning Provider trapped an unexpected exception while creating an Operator");
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
    const auto& implementation = *operator_handle->implementation;

    ksj_firing_lease_info info{};
    info.abi = make_header(sizeof(info));
    const auto info_status = callbacks->get_info(callbacks->host_context, lease, &info, out_error);
    if (info_status != KSJ_STATUS_OK || !has_full_compatible_header(&info) ||
        info.input_batch_count != implementation.input_batch_count || info.output_grant_count != 1U ||
        info.reserved_scratch_bytes != 0U) {
      return info_status == KSJ_STATUS_OK
               ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease, sizeof(kErrorLease) - 1U)
               : info_status;
    }

    InputSelection inputs{};
    const auto selection_status = select_exact_input_ports(implementation, lease, callbacks, inputs, out_error);
    if (selection_status != KSJ_STATUS_OK) {
      return selection_status;
    }

    std::uint64_t dynamic_byte_count = 0U;
    std::uint64_t output_byte_count = 0U;
    if (!implementation.expected_dynamic_input_byte_count(*operator_handle, dynamic_byte_count) ||
        !implementation.expected_output_byte_count(*operator_handle, output_byte_count) ||
        !has_valid_input_item(*inputs.dynamic_item, implementation.matches_dynamic_input_type, dynamic_byte_count)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorDynamicInput, sizeof(kErrorDynamicInput) - 1U);
    }

    const void* calibration_data = nullptr;
    if (implementation.input_batch_count == 2U) {
      std::uint64_t calibration_byte_count = 0U;
      if (!implementation.expected_static_input_byte_count(*operator_handle, calibration_byte_count) ||
          !has_valid_input_item(*inputs.calibration_item, implementation.matches_static_input_type,
                                calibration_byte_count)) {
        return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorCalibrationInput,
                      sizeof(kErrorCalibrationInput) - 1U);
      }
      if (!calibration_identity_matches_dynamic(*inputs.dynamic_item, *inputs.calibration_item)) {
        return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorInputIdentity, sizeof(kErrorInputIdentity) - 1U);
      }
      calibration_data = inputs.calibration_item->payload.data;
    }
    if (info.reserved_output_bytes < output_byte_count) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease, sizeof(kErrorLease) - 1U);
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
    if (!has_full_compatible_header(&output) || output.data == nullptr || output.capacity_bytes < output_byte_count ||
        output.committed_bytes != 0U || output.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(output.data, output.alignment, kRequiredAlignment) ||
        !has_valid_type_descriptor(output.type) || implementation.matches_output_type == nullptr ||
        !implementation.matches_output_type(output.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput, sizeof(kErrorOutput) - 1U);
    }

    if (!implementation.transform(*operator_handle, inputs.dynamic_item->payload.data, calibration_data, output.data)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTransform, sizeof(kErrorTransform) - 1U);
    }

    ksj_output_seal_descriptor seal{};
    seal.abi = make_header(sizeof(seal));
    seal.output_port = kOutputPort;
    seal.produced_item_count = 1U;
    seal.produced_byte_count = output_byte_count;
    seal.semantic_key_hash = inputs.dynamic_item->semantic_key_hash;
    seal.order_key = inputs.dynamic_item->order_key;
    seal.type = implementation.output_type();
    seal.metadata.abi = make_header(sizeof(seal.metadata));
    const auto seal_status =
      callbacks->output_grants->seal(callbacks->output_grants->host_context, grant, &seal, out_error);
    if (seal_status != KSJ_STATUS_OK) {
      return seal_status;
    }
    grant_guard.settled = true;
    write_done(out_result, 1U, implementation.input_batch_count, info.terminal_epoch);
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("K-space-conditioning Provider trapped an unexpected exception while processing a firing");
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
    KSJ_LOG_ERROR("K-space-conditioning Provider trapped an unexpected exception while answering provider query");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal, sizeof(kErrorInternal) - 1U);
  }
}

} // namespace ksj::kspace_conditioning::api
