// SPDX-License-Identifier: Apache-2.0
//
// Two-dimensional Cartesian inverse transforms for every receive channel.
// The numerical transform is delegated to KSpaceJet's public FFT API; this
// file only validates ABI payloads and explicitly changes channel-major
// k-space planes into coil-last image samples.

#include "provider_state.hpp"
#include "operators/cartesian_ifft2_coil_images.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/fft/transforms.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

namespace ksj::cartesian_recon::operators {

using namespace ksj::provider::detail;

namespace {

constexpr char kOperatorId[] = "cartesian_ifft2_coil_images";

constexpr char kErrorBadAbi[] = "Cartesian reconstruction Provider received incompatible ABI storage";
constexpr char kErrorInvalidArgument[] = "Cartesian reconstruction Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] = "Cartesian reconstruction Provider does not expose the requested operator";
constexpr char kErrorUnsupportedConfig[] =
  "Cartesian reconstruction Provider requires canonical {\"channels\":N,\"cols\":N,\"rows\":N} dimensions";
constexpr char kErrorLease[] =
  "Cartesian reconstruction Provider requires one frame, one output grant, and bounded transform scratch";
constexpr char kErrorFrame[] =
  "Cartesian reconstruction Provider requires one exact multi-channel ksj.kspace-frame payload";
constexpr char kErrorOutput[] =
  "Cartesian reconstruction Provider requires one exact ksj.coil-image-frame output grant";
constexpr char kErrorScratch[] = "Cartesian reconstruction Provider scratch grant is too small or incompatible";
constexpr char kErrorInternal[] = "Cartesian reconstruction Provider trapped an unexpected internal exception";

constexpr std::uint32_t kMaximumDimension = 512U;
constexpr std::uint32_t kMaximumChannels = 64U;
constexpr std::uint64_t kMaximumPixels =
  static_cast<std::uint64_t>(kMaximumDimension) * static_cast<std::uint64_t>(kMaximumDimension);
constexpr std::uint64_t kMaximumComplexFrameBytes =
  kMaximumPixels * static_cast<std::uint64_t>(kMaximumChannels) * 2U * sizeof(float);
// One input/result plane, one intermediate plane, and two line workspaces.
// This is the complete caller-owned workspace for the public bounded FFT API;
// the Provider must never obtain pooled numerical scratch behind the host's
// frozen firing reservation.
constexpr std::uint64_t kMaximumScratchComplexElements = 2U * kMaximumPixels + 2U * kMaximumDimension;
constexpr std::uint64_t kMaximumScratchBytes = kMaximumScratchComplexElements * 2U * sizeof(float);
constexpr std::uint32_t kRequiredAlignment = 64U;
constexpr std::uint32_t kInputPort = 0U;
constexpr std::uint32_t kOutputPort = 0U;

using Complex = std::complex<float>;
static_assert(sizeof(Complex) == 2U * sizeof(float), "The complex_float32 payload requires two binary32 components.");

[[nodiscard]] bool has_kspace_frame_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_kspace_frame(&type) != 0;
}

[[nodiscard]] bool has_coil_image_frame_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_coil_image_frame(&type) != 0;
}

[[nodiscard]] bool parse_positive_bounded(const char* first, const char* last, const std::uint32_t maximum,
                                          std::uint32_t& output) noexcept {
  std::uint32_t value = 0U;
  if (!parse_canonical_positive_u32(first, last, value) || value < 2U || value > maximum) {
    return false;
  }
  output = value;
  return true;
}

[[nodiscard]] bool parse_channels(const char* first, const char* last, std::uint32_t& output) noexcept {
  std::uint32_t value = 0U;
  if (!parse_canonical_positive_u32(first, last, value) || value > kMaximumChannels) {
    return false;
  }
  output = value;
  return true;
}

[[nodiscard]] bool parse_canonical_config(const ksj_byte_view& config, std::uint32_t& channels, std::uint32_t& rows,
                                          std::uint32_t& cols) noexcept {
  constexpr std::string_view kPrefix{"{\"channels\":"};
  constexpr std::string_view kCols{",\"cols\":"};
  constexpr std::string_view kRows{",\"rows\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto cols_marker = encoded.find(kCols, kPrefix.size());
  const auto rows_marker =
    cols_marker == std::string_view::npos ? std::string_view::npos : encoded.find(kRows, cols_marker + kCols.size());
  if (cols_marker == std::string_view::npos || rows_marker == std::string_view::npos) {
    return false;
  }
  const char* const base = encoded.data();
  return parse_channels(base + kPrefix.size(), base + cols_marker, channels) &&
         parse_positive_bounded(base + cols_marker + kCols.size(), base + rows_marker, kMaximumDimension, cols) &&
         parse_positive_bounded(base + rows_marker + kRows.size(), base + encoded.size() - kSuffix.size(),
                                kMaximumDimension, rows);
}

[[nodiscard]] bool shape_bytes(const ksj_provider_operator& operator_handle, std::uint64_t& pixel_count,
                               std::uint64_t& complex_frame_bytes, std::uint64_t& scratch_bytes) noexcept {
  if (operator_handle.channels == 0U || operator_handle.channels > kMaximumChannels || operator_handle.rows < 2U ||
      operator_handle.rows > kMaximumDimension || operator_handle.cols < 2U ||
      operator_handle.cols > kMaximumDimension) {
    return false;
  }
  pixel_count = static_cast<std::uint64_t>(operator_handle.rows) * operator_handle.cols;
  complex_frame_bytes = pixel_count * operator_handle.channels * sizeof(Complex);
  const auto line_extent = static_cast<std::uint64_t>(std::max(operator_handle.rows, operator_handle.cols));
  scratch_bytes = (pixel_count * 2U + line_extent * 2U) * sizeof(Complex);
  return pixel_count <= kMaximumPixels && complex_frame_bytes <= kMaximumComplexFrameBytes &&
         scratch_bytes <= kMaximumScratchBytes;
}

[[nodiscard]] bool is_operator(const ksj_provider_operator* operator_handle) noexcept {
  std::uint64_t pixels = 0U;
  std::uint64_t frame_bytes = 0U;
  std::uint64_t scratch_bytes = 0U;
  return operator_handle != nullptr && shape_bytes(*operator_handle, pixels, frame_bytes, scratch_bytes);
}

[[nodiscard]] bool is_context(const ksj_provider_operator* operator_handle,
                              const ksj_execution_context* context) noexcept {
  return is_operator(operator_handle) && context == &operator_handle->context && context->owner == operator_handle &&
         operator_handle->context_active;
}

[[nodiscard]] bool is_key_state(const ksj_provider_operator* operator_handle, const ksj_key_state* key_state) noexcept {
  return is_operator(operator_handle) && key_state == &operator_handle->key_state &&
         key_state->owner == operator_handle && operator_handle->key_state_active;
}

struct ScratchComplexObjects {
  Complex* data{nullptr};
  Complex* intermediate{nullptr};
  Complex* source{nullptr};
  Complex* destination{nullptr};
  std::size_t matrix_count{0U};
  std::size_t vector_count{0U};

  ScratchComplexObjects(Complex* data_storage, Complex* intermediate_storage, Complex* source_storage,
                        Complex* destination_storage, const std::size_t matrix_element_count,
                        const std::size_t vector_element_count)
      : data(data_storage), intermediate(intermediate_storage), source(source_storage),
        destination(destination_storage), matrix_count(matrix_element_count), vector_count(vector_element_count) {}

  ScratchComplexObjects(const ScratchComplexObjects&) = delete;
  ScratchComplexObjects& operator=(const ScratchComplexObjects&) = delete;

  ~ScratchComplexObjects() {
    std::destroy_n(data, matrix_count);
    std::destroy_n(intermediate, matrix_count);
    std::destroy_n(source, vector_count);
    std::destroy_n(destination, vector_count);
  }
};

[[nodiscard]] ksj_operator_descriptor make_operator_descriptor() noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(kOperatorId);
  descriptor.max_in_flight = 1U;
  descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  descriptor.max_input_items_per_firing = 1U;
  descriptor.max_output_items_per_firing = 1U;
  descriptor.max_output_bytes_per_firing = kMaximumComplexFrameBytes;
  descriptor.max_scratch_bytes_per_firing = kMaximumScratchBytes;
  return descriptor;
}

} // namespace

const ksj_operator_descriptor& cartesian_ifft2_coil_images_descriptor() noexcept {
  static const ksj_operator_descriptor descriptor = make_operator_descriptor();
  return descriptor;
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_create(const ksj_operator_create_request* request,
                                                                ksj_provider_operator** out_operator,
                                                                ksj_error_view* out_error) noexcept {
  try {
    if (!has_full_compatible_header(request) || out_operator == nullptr) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
    }
    *out_operator = nullptr;
    if (!text_equals(request->operator_id, kOperatorId)) {
      return reject(out_error, KSJ_STATUS_UNSUPPORTED, kErrorUnsupportedOperator);
    }
    if (request->host_services != nullptr && !has_full_compatible_header(request->host_services)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }
    std::uint32_t channels = 0U;
    std::uint32_t rows = 0U;
    std::uint32_t cols = 0U;
    if (!parse_canonical_config(request->canonical_config, channels, rows, cols)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorUnsupportedConfig);
    }
    auto* const operator_handle = new (std::nothrow) ksj_provider_operator{
      .channels = channels,
      .rows = rows,
      .cols = cols,
    };
    if (operator_handle == nullptr) {
      return reject(out_error, KSJ_STATUS_RESOURCE_EXHAUSTED, kErrorInternal);
    }
    *out_operator = operator_handle;
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("cartesian_ifft2_coil_images trapped an unexpected exception while creating an Operator");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_execution_context_create(
  ksj_provider_operator* operator_handle, const ksj_execution_context_descriptor* descriptor,
  ksj_execution_context** out_context, ksj_error_view* out_error) noexcept {
  if (!is_operator(operator_handle) || !has_full_compatible_header(descriptor) || out_context == nullptr) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (descriptor->host_services != nullptr && !has_full_compatible_header(descriptor->host_services)) {
    return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
  }
  if (operator_handle->context_active) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorInvalidArgument);
  }
  operator_handle->context.owner = operator_handle;
  operator_handle->context_active = true;
  *out_context = &operator_handle->context;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_key_state_init(ksj_provider_operator* operator_handle,
                                                                        ksj_execution_context* context,
                                                                        const ksj_key_state_descriptor* descriptor,
                                                                        ksj_key_state** out_key_state,
                                                                        ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !has_full_compatible_header(descriptor) || out_key_state == nullptr ||
      !valid_borrowed_bytes(descriptor->semantic_key)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  if (operator_handle->key_state_active) {
    return reject(out_error, KSJ_STATUS_FAILED_PRECONDITION, kErrorInvalidArgument);
  }
  operator_handle->key_state.owner = operator_handle;
  operator_handle->key_state_active = true;
  *out_key_state = &operator_handle->key_state;
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_on_start(ksj_provider_operator* operator_handle,
                                                                  ksj_execution_context* context,
                                                                  ksj_key_state* key_state,
                                                                  const ksj_scan_start_descriptor* descriptor,
                                                                  ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_process_batch(
  ksj_provider_operator* operator_handle, ksj_execution_context* context, ksj_key_state* key_state,
  ksj_firing_lease* lease, const ksj_firing_lease_callbacks* callbacks, ksj_process_result* out_result,
  ksj_error_view* out_error) noexcept {
  try {
    if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) || lease == nullptr ||
        !has_full_compatible_header(out_result)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
    }
    if (!has_usable_firing_callbacks(callbacks, KSJ_LEASE_CAP_INPUT_BATCHES | KSJ_LEASE_CAP_OUTPUT_GRANTS |
                                                  KSJ_LEASE_CAP_SCRATCH)) {
      return reject(out_error, KSJ_STATUS_BAD_ABI, kErrorBadAbi);
    }

    std::uint64_t pixel_count = 0U;
    std::uint64_t complex_frame_bytes = 0U;
    std::uint64_t scratch_bytes = 0U;
    if (!shape_bytes(*operator_handle, pixel_count, complex_frame_bytes, scratch_bytes)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorFrame);
    }

    ksj_firing_lease_info info{};
    info.abi = make_header(sizeof(info));
    const auto info_status = callbacks->get_info(callbacks->host_context, lease, &info, out_error);
    if (info_status != KSJ_STATUS_OK || !has_full_compatible_header(&info) || info.input_batch_count != 1U ||
        info.output_grant_count != 1U || info.reserved_output_bytes < complex_frame_bytes ||
        info.reserved_scratch_bytes < scratch_bytes) {
      return info_status == KSJ_STATUS_OK ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease) : info_status;
    }

    ksj_input_batch_view batch{};
    batch.abi = make_header(sizeof(batch));
    const auto batch_status = callbacks->get_input_batch(callbacks->host_context, lease, 0U, &batch, out_error);
    if (batch_status != KSJ_STATUS_OK) {
      return batch_status;
    }
    if (!has_full_compatible_header(&batch) || batch.input_port != kInputPort || batch.item_count != 1U ||
        batch.items == nullptr || !has_full_compatible_header(&batch.items[0U])) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorFrame);
    }
    const auto& input = batch.items[0U];
    if (!has_full_compatible_header(&input.payload) || input.payload.data == nullptr ||
        input.payload.byte_count != complex_frame_bytes ||
        input.payload.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(input.payload.data, input.payload.alignment, kRequiredAlignment) ||
        !valid_borrowed_bytes(input.metadata) || !has_kspace_frame_type(input.payload.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorFrame);
    }

    ksj_output_grant* grant = nullptr;
    const auto acquire_status =
      callbacks->acquire_output_grant(callbacks->host_context, lease, kOutputPort, &grant, out_error);
    if (acquire_status != KSJ_STATUS_OK || grant == nullptr) {
      return acquire_status == KSJ_STATUS_OK ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput)
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
    if (!has_full_compatible_header(&output) || output.data == nullptr || output.capacity_bytes < complex_frame_bytes ||
        output.committed_bytes != 0U || output.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(output.data, output.alignment, kRequiredAlignment) ||
        !has_coil_image_frame_type(output.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput);
    }

    ksj_scratch_view scratch{};
    scratch.abi = make_header(sizeof(scratch));
    const auto scratch_status = callbacks->get_scratch(callbacks->host_context, lease, &scratch, out_error);
    if (scratch_status != KSJ_STATUS_OK) {
      return scratch_status;
    }
    if (!has_full_compatible_header(&scratch) || scratch.data == nullptr || scratch.byte_count < scratch_bytes ||
        scratch.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(scratch.data, scratch.alignment, alignof(Complex))) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorScratch);
    }

    auto* const data_workspace = static_cast<Complex*>(scratch.data);
    auto* const intermediate_workspace = data_workspace + static_cast<std::size_t>(pixel_count);
    const auto line_extent = static_cast<std::size_t>(std::max(operator_handle->rows, operator_handle->cols));
    auto* const source_workspace = intermediate_workspace + static_cast<std::size_t>(pixel_count);
    auto* const destination_workspace = source_workspace + line_extent;
    const auto* const source = static_cast<const std::byte*>(input.payload.data);
    auto* const destination = static_cast<std::byte*>(output.data);
    for (std::uint32_t channel = 0U; channel < operator_handle->channels; ++channel) {
      const std::uint64_t channel_offset = static_cast<std::uint64_t>(channel) * pixel_count * sizeof(Complex);
      for (std::uint64_t index = 0U; index < pixel_count; ++index) {
        float real = 0.0F;
        float imaginary = 0.0F;
        const auto offset = static_cast<std::size_t>(channel_offset + index * sizeof(Complex));
        std::memcpy(&real, source + offset, sizeof(real));
        std::memcpy(&imaginary, source + offset + sizeof(real), sizeof(imaginary));
        std::construct_at(data_workspace + index, real, imaginary);
        std::construct_at(intermediate_workspace + index);
      }
      for (std::size_t index = 0U; index < line_extent; ++index) {
        std::construct_at(source_workspace + index);
        std::construct_at(destination_workspace + index);
      }
      ScratchComplexObjects objects{data_workspace,
                                    intermediate_workspace,
                                    source_workspace,
                                    destination_workspace,
                                    static_cast<std::size_t>(pixel_count),
                                    line_extent};
      auto data_matrix = ksj::array::MatrixView<Complex>{data_workspace, operator_handle->rows, operator_handle->cols};
      const auto intermediate_matrix =
        ksj::array::MatrixView<Complex>{intermediate_workspace, operator_handle->rows, operator_handle->cols};
      const auto source_vector = ksj::array::VectorView<Complex>{source_workspace, line_extent};
      const auto destination_vector = ksj::array::VectorView<Complex>{destination_workspace, line_extent};
      ksj::fft::fft2_inplace_with_workspace(data_matrix, intermediate_matrix, source_vector, destination_vector,
                                            ksj::fft::Direction::inverse, ksj::fft::Normalization::inverse);
      for (std::uint64_t index = 0U; index < pixel_count; ++index) {
        const auto& value = data_workspace[index];
        const std::uint64_t output_index = index * operator_handle->channels + channel;
        const auto output_offset = static_cast<std::size_t>(output_index * sizeof(Complex));
        const float real = value.real();
        const float imaginary = value.imag();
        std::memcpy(destination + output_offset, &real, sizeof(real));
        std::memcpy(destination + output_offset + sizeof(real), &imaginary, sizeof(imaginary));
      }
    }

    ksj_output_seal_descriptor seal{};
    seal.abi = make_header(sizeof(seal));
    seal.output_port = kOutputPort;
    seal.produced_item_count = 1U;
    seal.produced_byte_count = complex_frame_bytes;
    seal.semantic_key_hash = input.semantic_key_hash;
    seal.order_key = input.order_key;
    seal.type = output.type;
    seal.metadata.abi = make_header(sizeof(seal.metadata));
    const auto seal_status =
      callbacks->output_grants->seal(callbacks->output_grants->host_context, grant, &seal, out_error);
    if (seal_status != KSJ_STATUS_OK) {
      return seal_status;
    }
    grant_guard.settled = true;
    write_done(out_result, 1U, 1U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("cartesian_ifft2_coil_images trapped an unexpected exception while processing a firing");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_on_scan_end(
  ksj_provider_operator* operator_handle, ksj_execution_context* context, ksj_key_state* key_state,
  const ksj_scan_end_descriptor* descriptor, ksj_firing_lease* terminal_lease,
  const ksj_firing_lease_callbacks* callbacks, ksj_process_result* out_result, ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor) || descriptor->kind != KSJ_PROVIDER_SCAN_END_NORMAL ||
      descriptor->reserved0 != 0U || terminal_lease == nullptr || !has_full_compatible_header(out_result) ||
      !has_full_compatible_header(callbacks)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  write_done(out_result, 0U, 0U, descriptor->terminal_epoch);
  return KSJ_STATUS_OK;
}

ksj_status KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_on_cancel(ksj_provider_operator* operator_handle,
                                                                   ksj_execution_context* context,
                                                                   ksj_key_state* key_state,
                                                                   const ksj_cancel_context* descriptor,
                                                                   ksj_error_view* out_error) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state) ||
      !has_full_compatible_header(descriptor) || descriptor->reserved0 != 0U ||
      (descriptor->kind != KSJ_PROVIDER_SCAN_END_CANCELLED && descriptor->kind != KSJ_PROVIDER_SCAN_END_FAILED)) {
    return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorInvalidArgument);
  }
  return KSJ_STATUS_OK;
}

void KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_key_state_reset(ksj_provider_operator* operator_handle,
                                                                   ksj_execution_context* context,
                                                                   ksj_key_state* key_state) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state)) {
    return;
  }
  operator_handle->key_state.owner = nullptr;
  operator_handle->key_state_active = false;
}

void KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_execution_context_destroy(ksj_provider_operator* operator_handle,
                                                                             ksj_execution_context* context) noexcept {
  if (!is_context(operator_handle, context)) {
    return;
  }
  operator_handle->context.owner = nullptr;
  operator_handle->context_active = false;
}

void KSJ_PROVIDER_CALL cartesian_ifft2_coil_images_destroy(ksj_provider_operator* operator_handle) noexcept {
  delete operator_handle;
}

} // namespace ksj::cartesian_recon::operators
