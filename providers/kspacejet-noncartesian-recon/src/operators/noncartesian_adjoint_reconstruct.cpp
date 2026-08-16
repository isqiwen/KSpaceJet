// SPDX-License-Identifier: Apache-2.0
//
// Direct two-dimensional non-Cartesian adjoint reconstruction. The numerical
// operation is delegated to KSpaceJet's public NUDFT API. This reference
// Operator is deliberately unweighted: it performs neither density
// compensation nor SENSE reconstruction.

#include "provider_state.hpp"
#include "operators/noncartesian_adjoint_reconstruct.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/nufft/direct_nudft.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <type_traits>

namespace ksj::noncartesian_recon::operators {

using namespace ksj::provider::detail;

namespace {

constexpr char kOperatorId[] = "noncartesian_adjoint_reconstruct";

constexpr char kErrorBadAbi[] = "Non-Cartesian reconstruction Provider received incompatible ABI storage";
constexpr char kErrorInvalidArgument[] = "Non-Cartesian reconstruction Provider received an invalid lifecycle argument";
constexpr char kErrorUnsupportedOperator[] =
  "Non-Cartesian reconstruction Provider does not expose the requested operator";
constexpr char kErrorUnsupportedConfig[] =
  "Non-Cartesian reconstruction Provider requires canonical "
  "{\"channels\":N,\"image_cols\":N,\"image_rows\":N,\"sample_count\":N} within its direct-adjoint bound";
constexpr char kErrorLease[] = "Non-Cartesian reconstruction Provider requires two input batches, one output grant, "
                               "and one bounded image scratch plane";
constexpr char kErrorKspace[] =
  "Non-Cartesian reconstruction Provider requires one exact ksj.noncartesian-kspace-frame on input port 0";
constexpr char kErrorTrajectory[] =
  "Non-Cartesian reconstruction Provider requires one exact finite ksj.trajectory-frame on input port 1";
constexpr char kErrorIdentity[] =
  "Non-Cartesian k-space and trajectory inputs must have equal semantic_key_hash and order_key";
constexpr char kErrorOutput[] =
  "Non-Cartesian reconstruction Provider requires one exact ksj.coil-image-frame output grant";
constexpr char kErrorScratch[] = "Non-Cartesian reconstruction Provider scratch grant is too small or incompatible";
constexpr char kErrorInternal[] = "Non-Cartesian reconstruction Provider trapped an unexpected internal exception";

constexpr std::uint32_t kMaximumDimension = 512U;
constexpr std::uint32_t kMaximumChannels = 64U;
constexpr std::uint32_t kMaximumSamples = 65536U;
constexpr std::uint64_t kMaximumPixels =
  static_cast<std::uint64_t>(kMaximumDimension) * static_cast<std::uint64_t>(kMaximumDimension);
constexpr std::uint64_t kMaximumDirectAdjointWork = UINT64_C(1) << 28U;
constexpr std::uint64_t kMaximumKspaceBytes =
  static_cast<std::uint64_t>(kMaximumChannels) * static_cast<std::uint64_t>(kMaximumSamples) * 2U * sizeof(float);
constexpr std::uint64_t kMaximumTrajectoryBytes = static_cast<std::uint64_t>(kMaximumSamples) * 2U * sizeof(float);
constexpr std::uint64_t kMaximumInputBytes = kMaximumKspaceBytes + kMaximumTrajectoryBytes;
constexpr std::uint64_t kMaximumCoilImageBytes =
  kMaximumPixels * static_cast<std::uint64_t>(kMaximumChannels) * 2U * sizeof(float);
constexpr std::uint64_t kMaximumScratchBytes = kMaximumPixels * 2U * sizeof(float);
constexpr std::uint32_t kRequiredAlignment = 64U;
constexpr std::uint32_t kKspaceInputPort = 0U;
constexpr std::uint32_t kTrajectoryInputPort = 1U;
constexpr std::uint32_t kOutputPort = 0U;

using Complex = ksj::base::cf32;
static_assert(sizeof(Complex) == 2U * sizeof(float), "The complex_float32 payload requires two binary32 components.");
static_assert(std::is_trivially_copyable_v<Complex>, "The complex_float32 ABI payload must be trivially copyable.");

[[nodiscard]] bool has_noncartesian_kspace_frame_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_noncartesian_kspace_frame(&type) != 0;
}

[[nodiscard]] bool has_trajectory_frame_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_trajectory_frame(&type) != 0;
}

[[nodiscard]] bool has_coil_image_frame_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_coil_image_frame(&type) != 0;
}

[[nodiscard]] bool parse_positive_bounded(const char* first, const char* last, const std::uint32_t minimum,
                                          const std::uint32_t maximum, std::uint32_t& output) noexcept {
  std::uint32_t value = 0U;
  if (!parse_canonical_positive_u32(first, last, value) || value < minimum || value > maximum) {
    return false;
  }
  output = value;
  return true;
}

[[nodiscard]] bool parse_canonical_config(const ksj_byte_view& config, std::uint32_t& channels,
                                          std::uint32_t& image_rows, std::uint32_t& image_cols,
                                          std::uint32_t& sample_count) noexcept {
  constexpr std::string_view kPrefix{"{\"channels\":"};
  constexpr std::string_view kImageCols{",\"image_cols\":"};
  constexpr std::string_view kImageRows{",\"image_rows\":"};
  constexpr std::string_view kSampleCount{",\"sample_count\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto cols_marker = encoded.find(kImageCols, kPrefix.size());
  const auto rows_marker = cols_marker == std::string_view::npos
                             ? std::string_view::npos
                             : encoded.find(kImageRows, cols_marker + kImageCols.size());
  const auto samples_marker = rows_marker == std::string_view::npos
                                ? std::string_view::npos
                                : encoded.find(kSampleCount, rows_marker + kImageRows.size());
  if (cols_marker == std::string_view::npos || rows_marker == std::string_view::npos ||
      samples_marker == std::string_view::npos) {
    return false;
  }
  const char* const base = encoded.data();
  if (!parse_positive_bounded(base + kPrefix.size(), base + cols_marker, 1U, kMaximumChannels, channels) ||
      !parse_positive_bounded(base + cols_marker + kImageCols.size(), base + rows_marker, 2U, kMaximumDimension,
                              image_cols) ||
      !parse_positive_bounded(base + rows_marker + kImageRows.size(), base + samples_marker, 2U, kMaximumDimension,
                              image_rows) ||
      !parse_positive_bounded(base + samples_marker + kSampleCount.size(), base + encoded.size() - kSuffix.size(), 1U,
                              kMaximumSamples, sample_count)) {
    return false;
  }

  const std::uint64_t work = static_cast<std::uint64_t>(channels) * image_rows * image_cols * sample_count;
  return work <= kMaximumDirectAdjointWork;
}

[[nodiscard]] bool shape_bytes(const ksj_provider_operator& operator_handle, std::uint64_t& pixel_count,
                               std::uint64_t& kspace_bytes, std::uint64_t& trajectory_bytes,
                               std::uint64_t& coil_image_bytes, std::uint64_t& scratch_bytes) noexcept {
  if (operator_handle.channels == 0U || operator_handle.channels > kMaximumChannels ||
      operator_handle.image_rows < 2U || operator_handle.image_rows > kMaximumDimension ||
      operator_handle.image_cols < 2U || operator_handle.image_cols > kMaximumDimension ||
      operator_handle.sample_count == 0U || operator_handle.sample_count > kMaximumSamples) {
    return false;
  }
  pixel_count = static_cast<std::uint64_t>(operator_handle.image_rows) * operator_handle.image_cols;
  kspace_bytes = static_cast<std::uint64_t>(operator_handle.channels) * operator_handle.sample_count * sizeof(Complex);
  trajectory_bytes = static_cast<std::uint64_t>(operator_handle.sample_count) * 2U * sizeof(float);
  coil_image_bytes = pixel_count * operator_handle.channels * sizeof(Complex);
  scratch_bytes = pixel_count * sizeof(Complex);
  const std::uint64_t work = pixel_count * operator_handle.channels * operator_handle.sample_count;
  return pixel_count <= kMaximumPixels && work <= kMaximumDirectAdjointWork && kspace_bytes <= kMaximumKspaceBytes &&
         trajectory_bytes <= kMaximumTrajectoryBytes && kspace_bytes + trajectory_bytes <= kMaximumInputBytes &&
         coil_image_bytes <= kMaximumCoilImageBytes && scratch_bytes <= kMaximumScratchBytes;
}

[[nodiscard]] bool is_operator(const ksj_provider_operator* operator_handle) noexcept {
  std::uint64_t pixel_count = 0U;
  std::uint64_t kspace_bytes = 0U;
  std::uint64_t trajectory_bytes = 0U;
  std::uint64_t coil_image_bytes = 0U;
  std::uint64_t scratch_bytes = 0U;
  return operator_handle != nullptr &&
         shape_bytes(*operator_handle, pixel_count, kspace_bytes, trajectory_bytes, coil_image_bytes, scratch_bytes);
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

struct ScratchComplexPlane {
  Complex* data{nullptr};
  std::size_t count{0U};

  ScratchComplexPlane(Complex* storage, const std::size_t element_count) : data(storage), count(element_count) {
    for (std::size_t index = 0U; index < count; ++index) {
      std::construct_at(data + index);
    }
  }

  ScratchComplexPlane(const ScratchComplexPlane&) = delete;
  ScratchComplexPlane& operator=(const ScratchComplexPlane&) = delete;

  ~ScratchComplexPlane() { std::destroy_n(data, count); }
};

[[nodiscard]] ksj_operator_descriptor make_operator_descriptor() noexcept {
  ksj_operator_descriptor descriptor{};
  descriptor.abi =
    make_header(sizeof(descriptor), KSJ_OPERATOR_CAP_CANCEL_NO_ALLOCATION | KSJ_OPERATOR_CAP_CANCEL_NO_THROW);
  descriptor.operator_id = make_utf8_view(kOperatorId);
  descriptor.max_in_flight = 1U;
  descriptor.thread_safety = KSJ_PROVIDER_SERIAL_INSTANCE;
  descriptor.max_input_items_per_firing = 2U;
  descriptor.max_output_items_per_firing = 1U;
  descriptor.max_output_bytes_per_firing = kMaximumCoilImageBytes;
  descriptor.max_scratch_bytes_per_firing = kMaximumScratchBytes;
  return descriptor;
}

[[nodiscard]] bool has_exact_batch(const ksj_input_batch_view& batch, const std::uint32_t expected_port) noexcept {
  return has_full_compatible_header(&batch) && batch.input_port == expected_port && batch.item_count == 1U &&
         batch.items != nullptr && has_full_compatible_header(&batch.items[0U]);
}

} // namespace

const ksj_operator_descriptor& noncartesian_adjoint_reconstruct_descriptor() noexcept {
  static const ksj_operator_descriptor descriptor = make_operator_descriptor();
  return descriptor;
}

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_create(const ksj_operator_create_request* request,
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
    std::uint32_t image_rows = 0U;
    std::uint32_t image_cols = 0U;
    std::uint32_t sample_count = 0U;
    if (!parse_canonical_config(request->canonical_config, channels, image_rows, image_cols, sample_count)) {
      return reject(out_error, KSJ_STATUS_INVALID_ARGUMENT, kErrorUnsupportedConfig);
    }
    auto* const operator_handle = new (std::nothrow) ksj_provider_operator{
      .channels = channels,
      .image_rows = image_rows,
      .image_cols = image_cols,
      .sample_count = sample_count,
    };
    if (operator_handle == nullptr) {
      return reject(out_error, KSJ_STATUS_RESOURCE_EXHAUSTED, kErrorInternal);
    }
    *out_operator = operator_handle;
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("noncartesian_adjoint_reconstruct trapped an unexpected exception while creating an Operator");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_execution_context_create(
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

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_key_state_init(ksj_provider_operator* operator_handle,
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

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_on_start(ksj_provider_operator* operator_handle,
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

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_process_batch(
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
    std::uint64_t kspace_bytes = 0U;
    std::uint64_t trajectory_bytes = 0U;
    std::uint64_t coil_image_bytes = 0U;
    std::uint64_t scratch_bytes = 0U;
    if (!shape_bytes(*operator_handle, pixel_count, kspace_bytes, trajectory_bytes, coil_image_bytes, scratch_bytes)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorUnsupportedConfig);
    }

    ksj_firing_lease_info info{};
    info.abi = make_header(sizeof(info));
    const auto info_status = callbacks->get_info(callbacks->host_context, lease, &info, out_error);
    if (info_status != KSJ_STATUS_OK || !has_full_compatible_header(&info) || info.input_batch_count != 2U ||
        info.output_grant_count != 1U || info.reserved_output_bytes < coil_image_bytes ||
        info.reserved_scratch_bytes < scratch_bytes) {
      return info_status == KSJ_STATUS_OK ? reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease) : info_status;
    }

    ksj_input_batch_view first_batch{};
    first_batch.abi = make_header(sizeof(first_batch));
    const auto first_batch_status =
      callbacks->get_input_batch(callbacks->host_context, lease, 0U, &first_batch, out_error);
    if (first_batch_status != KSJ_STATUS_OK) {
      return first_batch_status;
    }
    ksj_input_batch_view second_batch{};
    second_batch.abi = make_header(sizeof(second_batch));
    const auto second_batch_status =
      callbacks->get_input_batch(callbacks->host_context, lease, 1U, &second_batch, out_error);
    if (second_batch_status != KSJ_STATUS_OK) {
      return second_batch_status;
    }
    if (!has_full_compatible_header(&first_batch) || !has_full_compatible_header(&second_batch)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease);
    }

    const ksj_input_batch_view* kspace_batch = nullptr;
    const ksj_input_batch_view* trajectory_batch = nullptr;
    for (const auto* candidate : {&first_batch, &second_batch}) {
      if (candidate->input_port == kKspaceInputPort) {
        if (kspace_batch != nullptr) {
          return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorKspace);
        }
        kspace_batch = candidate;
      } else if (candidate->input_port == kTrajectoryInputPort) {
        if (trajectory_batch != nullptr) {
          return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTrajectory);
        }
        trajectory_batch = candidate;
      } else {
        return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorLease);
      }
    }
    if (kspace_batch == nullptr || !has_exact_batch(*kspace_batch, kKspaceInputPort)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorKspace);
    }
    if (trajectory_batch == nullptr || !has_exact_batch(*trajectory_batch, kTrajectoryInputPort)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTrajectory);
    }

    const auto& kspace = kspace_batch->items[0U];
    const auto& trajectory = trajectory_batch->items[0U];
    if (!has_full_compatible_header(&kspace.payload) || kspace.payload.data == nullptr ||
        kspace.payload.byte_count != kspace_bytes ||
        kspace.payload.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(kspace.payload.data, kspace.payload.alignment, kRequiredAlignment) ||
        !valid_borrowed_bytes(kspace.metadata) || !has_noncartesian_kspace_frame_type(kspace.payload.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorKspace);
    }
    if (!has_full_compatible_header(&trajectory.payload) || trajectory.payload.data == nullptr ||
        trajectory.payload.byte_count != trajectory_bytes ||
        trajectory.payload.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(trajectory.payload.data, trajectory.payload.alignment, kRequiredAlignment) ||
        !valid_borrowed_bytes(trajectory.metadata) || !has_trajectory_frame_type(trajectory.payload.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTrajectory);
    }
    if (kspace.semantic_key_hash != trajectory.semantic_key_hash || kspace.order_key != trajectory.order_key) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorIdentity);
    }

    const auto* const trajectory_values = static_cast<const float*>(trajectory.payload.data);
    const std::uint64_t trajectory_value_count = static_cast<std::uint64_t>(operator_handle->sample_count) * 2U;
    for (std::uint64_t index = 0U; index < trajectory_value_count; ++index) {
      if (!std::isfinite(trajectory_values[index])) {
        return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorTrajectory);
      }
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
    if (!has_full_compatible_header(&output) || output.data == nullptr || output.capacity_bytes < coil_image_bytes ||
        output.committed_bytes != 0U || output.memory_domain != KSJ_PROVIDER_MEMORY_HOST_PAGEABLE ||
        !has_usable_host_memory(output.data, output.alignment, kRequiredAlignment) ||
        !has_coil_image_frame_type(output.type)) {
      return reject(out_error, KSJ_STATUS_CONTRACT_VIOLATION, kErrorOutput);
    }

    // The host materializes these exact typed immutable payloads. No sample
    // staging is permitted: only the one output-image plane is granted as
    // scratch for this Operator.
    const auto* const kspace_values = static_cast<const Complex*>(kspace.payload.data);
    auto* const image_plane = static_cast<Complex*>(scratch.data);
    auto* const destination = static_cast<std::byte*>(output.data);
    const auto trajectory_view =
      ksj::array::MatrixView<const float>{trajectory_values, operator_handle->sample_count, 2U};
    const auto grid = ksj::nufft::Grid2D{
      .rows = operator_handle->image_rows,
      .cols = operator_handle->image_cols,
      .row_origin = static_cast<double>(operator_handle->image_rows - 1U) / 2.0,
      .col_origin = static_cast<double>(operator_handle->image_cols - 1U) / 2.0,
    };
    for (std::uint32_t channel = 0U; channel < operator_handle->channels; ++channel) {
      ScratchComplexPlane plane_objects{image_plane, static_cast<std::size_t>(pixel_count)};
      const auto samples = ksj::array::VectorView<const Complex>{kspace_values + static_cast<std::uint64_t>(channel) *
                                                                                   operator_handle->sample_count,
                                                                 operator_handle->sample_count};
      const auto image =
        ksj::array::MatrixView<Complex>{image_plane, operator_handle->image_rows, operator_handle->image_cols};
      ksj::nufft::direct_nudft2_adjoint(grid, samples, trajectory_view, image);
      for (std::uint64_t pixel = 0U; pixel < pixel_count; ++pixel) {
        const auto output_index = pixel * operator_handle->channels + channel;
        const auto output_offset = static_cast<std::size_t>(output_index * sizeof(Complex));
        const float real = image_plane[pixel].real();
        const float imaginary = image_plane[pixel].imag();
        std::memcpy(destination + output_offset, &real, sizeof(real));
        std::memcpy(destination + output_offset + sizeof(real), &imaginary, sizeof(imaginary));
      }
    }

    ksj_output_seal_descriptor seal{};
    seal.abi = make_header(sizeof(seal));
    seal.output_port = kOutputPort;
    seal.produced_item_count = 1U;
    seal.produced_byte_count = coil_image_bytes;
    seal.semantic_key_hash = kspace.semantic_key_hash;
    seal.order_key = kspace.order_key;
    seal.type = output.type;
    seal.metadata = kspace.metadata;
    const auto seal_status =
      callbacks->output_grants->seal(callbacks->output_grants->host_context, grant, &seal, out_error);
    if (seal_status != KSJ_STATUS_OK) {
      return seal_status;
    }
    grant_guard.settled = true;
    write_done(out_result, 1U, 2U, info.terminal_epoch);
    return KSJ_STATUS_OK;
  } catch (...) {
    KSJ_LOG_ERROR("noncartesian_adjoint_reconstruct trapped an unexpected exception while processing a firing");
    return reject(out_error, KSJ_STATUS_INTERNAL_ERROR, kErrorInternal);
  }
}

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_on_scan_end(
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

ksj_status KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_on_cancel(ksj_provider_operator* operator_handle,
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

void KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_key_state_reset(ksj_provider_operator* operator_handle,
                                                                        ksj_execution_context* context,
                                                                        ksj_key_state* key_state) noexcept {
  if (!is_context(operator_handle, context) || !is_key_state(operator_handle, key_state)) {
    return;
  }
  operator_handle->key_state.owner = nullptr;
  operator_handle->key_state_active = false;
}

void KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_execution_context_destroy(
  ksj_provider_operator* operator_handle, ksj_execution_context* context) noexcept {
  if (!is_context(operator_handle, context)) {
    return;
  }
  operator_handle->context.owner = nullptr;
  operator_handle->context_active = false;
}

void KSJ_PROVIDER_CALL noncartesian_adjoint_reconstruct_destroy(ksj_provider_operator* operator_handle) noexcept {
  delete operator_handle;
}

} // namespace ksj::noncartesian_recon::operators
