// SPDX-License-Identifier: Apache-2.0

#include "operators/coil_compression_basis_estimate.hpp"

#include "support/calibration_payload.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/linalg/decompositions.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace ksj::calibration::operators {

using ::ksj::calibration::state::CalibrationOperatorImplementation;
using ::ksj::calibration::support::Complex;
using ::ksj::calibration::support::kComplexFloat32Bytes;
using ::ksj::calibration::support::kMaximumCalibrationOutputBytes;
using ::ksj::calibration::support::kMaximumChannelCount;
using ::ksj::calibration::support::kMaximumInputBytes;
using ::ksj::calibration::support::read_complex_float32;
using ::ksj::calibration::support::write_complex_float32;
using ::ksj::provider::detail::checked_multiply;
using ::ksj::provider::detail::has_full_compatible_header;
using ::ksj::provider::detail::has_valid_type_descriptor;
using ::ksj::provider::detail::parse_canonical_positive_u32;

namespace {

constexpr char kOperatorId[] = "coil_compression_basis_estimate";
constexpr char kUnsupportedConfigError[] =
  "Calibration Provider coil_compression_basis_estimate requires canonical "
  "{\"physical_channel_count\":P,\"virtual_channel_count\":V}, where 1 <= V <= P <= 64";
constexpr std::uint64_t kMaximumScratchBytes =
  3U * static_cast<std::uint64_t>(kMaximumChannelCount) * kMaximumChannelCount * kComplexFloat32Bytes +
  static_cast<std::uint64_t>(kMaximumChannelCount) * sizeof(float);

struct CoilScratchViews final {
  Complex* covariance{nullptr};
  Complex* eigenvectors{nullptr};
  Complex* eigensolver_workspace{nullptr};
  float* eigenvalues{nullptr};
};

[[nodiscard]] bool parse_config(const ksj_byte_view& config, std::uint32_t& physical_channel_count,
                                std::uint32_t& virtual_channel_count) noexcept {
  constexpr std::string_view kPrefix{"{\"physical_channel_count\":"};
  constexpr std::string_view kMiddle{",\"virtual_channel_count\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto middle_offset = encoded.find(kMiddle, kPrefix.size());
  if (middle_offset == std::string_view::npos) {
    return false;
  }
  const char* const first = encoded.data();
  const char* const physical_first = first + kPrefix.size();
  const char* const physical_last = first + middle_offset;
  const char* const virtual_first = physical_last + kMiddle.size();
  const char* const virtual_last = first + encoded.size() - kSuffix.size();
  return parse_canonical_positive_u32(physical_first, physical_last, physical_channel_count) &&
         parse_canonical_positive_u32(virtual_first, virtual_last, virtual_channel_count) &&
         physical_channel_count <= kMaximumChannelCount && virtual_channel_count <= physical_channel_count;
}

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  std::uint32_t physical_channel_count = 0U;
  std::uint32_t virtual_channel_count = 0U;
  if (!parse_config(config, physical_channel_count, virtual_channel_count)) {
    return false;
  }
  operator_handle.physical_channel_count = physical_channel_count;
  operator_handle.virtual_channel_count = virtual_channel_count;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  return operator_handle.physical_channel_count != 0U &&
         operator_handle.physical_channel_count <= kMaximumChannelCount &&
         operator_handle.virtual_channel_count != 0U &&
         operator_handle.virtual_channel_count <= operator_handle.physical_channel_count;
}

[[nodiscard]] bool matches_input_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_kspace_frame(&type) != 0;
}

[[nodiscard]] bool matches_output_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_coil_compression_basis(&type) != 0;
}

[[nodiscard]] ksj_type_descriptor_view output_type() {
  return ksj_type_registry_coil_compression_basis();
}

[[nodiscard]] bool input_sample_count(const ksj_provider_operator& operator_handle, const std::uint64_t byte_count,
                                      std::uint64_t& sample_count) noexcept {
  std::uint64_t channel_bytes = 0U;
  if (!is_valid(operator_handle) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.physical_channel_count), kComplexFloat32Bytes,
                        channel_bytes) ||
      byte_count > kMaximumInputBytes || byte_count % channel_bytes != 0U) {
    return false;
  }
  sample_count = byte_count / channel_bytes;
  return sample_count >= 1U;
}

[[nodiscard]] bool has_compatible_input_byte_count(const ksj_provider_operator& operator_handle,
                                                   const std::uint64_t byte_count) noexcept {
  std::uint64_t sample_count = 0U;
  return input_sample_count(operator_handle, byte_count, sample_count);
}

[[nodiscard]] bool expected_output_byte_count(const ksj_provider_operator& operator_handle,
                                              std::uint64_t& byte_count) noexcept {
  std::uint64_t element_count = 0U;
  return is_valid(operator_handle) &&
         checked_multiply(static_cast<std::uint64_t>(operator_handle.virtual_channel_count),
                          static_cast<std::uint64_t>(operator_handle.physical_channel_count), element_count) &&
         checked_multiply(element_count, kComplexFloat32Bytes, byte_count) &&
         byte_count <= kMaximumCalibrationOutputBytes;
}

[[nodiscard]] bool required_scratch_bytes(const ksj_provider_operator& operator_handle,
                                          std::uint64_t& byte_count) noexcept {
  std::uint64_t matrix_elements = 0U;
  std::uint64_t matrix_bytes = 0U;
  std::uint64_t triple_matrix_bytes = 0U;
  std::uint64_t eigenvalue_bytes = 0U;
  if (!is_valid(operator_handle) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.physical_channel_count),
                        static_cast<std::uint64_t>(operator_handle.physical_channel_count), matrix_elements) ||
      !checked_multiply(matrix_elements, kComplexFloat32Bytes, matrix_bytes) ||
      !checked_multiply(matrix_bytes, 3U, triple_matrix_bytes) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.physical_channel_count), sizeof(float),
                        eigenvalue_bytes) ||
      triple_matrix_bytes > std::numeric_limits<std::uint64_t>::max() - eigenvalue_bytes) {
    return false;
  }
  byte_count = triple_matrix_bytes + eigenvalue_bytes;
  return byte_count <= kMaximumScratchBytes;
}

[[nodiscard]] bool map_scratch(const ksj_provider_operator& operator_handle, void* const scratch,
                               const std::uint64_t scratch_byte_count, CoilScratchViews& views) noexcept {
  std::uint64_t required_bytes = 0U;
  if (scratch == nullptr || !required_scratch_bytes(operator_handle, required_bytes) ||
      scratch_byte_count != required_bytes) {
    return false;
  }
  const auto channels = static_cast<std::size_t>(operator_handle.physical_channel_count);
  const auto matrix_elements = channels * channels;
  auto* cursor = static_cast<std::byte*>(scratch);
  views.covariance = reinterpret_cast<Complex*>(cursor);
  cursor += matrix_elements * sizeof(Complex);
  views.eigenvectors = reinterpret_cast<Complex*>(cursor);
  cursor += matrix_elements * sizeof(Complex);
  views.eigensolver_workspace = reinterpret_cast<Complex*>(cursor);
  cursor += matrix_elements * sizeof(Complex);
  views.eigenvalues = reinterpret_cast<float*>(cursor);
  return true;
}

[[nodiscard]] bool estimate(const ksj_provider_operator& operator_handle, const void* const input,
                            const std::uint64_t input_byte_count, void* const output,
                            const std::uint64_t output_byte_count, void* const scratch,
                            const std::uint64_t scratch_byte_count) noexcept {
  try {
    if (input == nullptr || output == nullptr) {
      return false;
    }
    std::uint64_t sample_count_value = 0U;
    std::uint64_t expected_output_byte_count_value = 0U;
    if (!input_sample_count(operator_handle, input_byte_count, sample_count_value) ||
        !expected_output_byte_count(operator_handle, expected_output_byte_count_value) ||
        output_byte_count != expected_output_byte_count_value) {
      return false;
    }

    const auto physical_channel_count = static_cast<std::size_t>(operator_handle.physical_channel_count);
    const auto virtual_channel_count = static_cast<std::size_t>(operator_handle.virtual_channel_count);
    const auto sample_count = static_cast<std::size_t>(sample_count_value);
    CoilScratchViews scratch_views{};
    if (!map_scratch(operator_handle, scratch, scratch_byte_count, scratch_views)) {
      return false;
    }
    const auto covariance =
      ksj::array::MatrixView<Complex>{scratch_views.covariance, physical_channel_count, physical_channel_count};
    const auto eigenvectors =
      ksj::array::MatrixView<Complex>{scratch_views.eigenvectors, physical_channel_count, physical_channel_count};
    const auto eigensolver_workspace = ksj::array::MatrixView<Complex>{scratch_views.eigensolver_workspace,
                                                                       physical_channel_count, physical_channel_count};
    const auto eigenvalues = ksj::array::VectorView<float>{scratch_views.eigenvalues, physical_channel_count};
    for (std::size_t row = 0U; row < physical_channel_count; ++row) {
      for (std::size_t column = 0U; column < physical_channel_count; ++column) {
        covariance(row, column) = Complex{};
      }
    }
    for (std::size_t sample_index = 0U; sample_index < sample_count; ++sample_index) {
      for (std::size_t row = 0U; row < physical_channel_count; ++row) {
        for (std::size_t column = 0U; column <= row; ++column) {
          covariance(row, column) += read_complex_float32(input, row * sample_count + sample_index) *
                                     std::conj(read_complex_float32(input, column * sample_count + sample_index));
        }
      }
    }
    const float reciprocal_sample_count = 1.0F / static_cast<float>(sample_count);
    for (std::size_t row = 0U; row < physical_channel_count; ++row) {
      covariance(row, row) *= reciprocal_sample_count;
      for (std::size_t column = 0U; column < row; ++column) {
        covariance(row, column) *= reciprocal_sample_count;
        covariance(column, row) = std::conj(covariance(row, column));
      }
    }

    if (!ksj::linalg::self_adjoint_eigen_decomposition_with_workspace(covariance.as_const(), eigenvalues, eigenvectors,
                                                                      eigensolver_workspace)) {
      return false;
    }
    for (std::size_t virtual_channel = 0U; virtual_channel < virtual_channel_count; ++virtual_channel) {
      const auto eigenvector_column = physical_channel_count - 1U - virtual_channel;
      for (std::size_t physical_channel = 0U; physical_channel < physical_channel_count; ++physical_channel) {
        write_complex_float32(output, virtual_channel * physical_channel_count + physical_channel,
                              std::conj(eigenvectors(physical_channel, eigenvector_column)));
      }
    }
    return true;
  } catch (...) {
    KSJ_LOG_ERROR("coil_compression_basis_estimate trapped an unexpected exception while estimating a basis");
    return false;
  }
}

} // namespace

const CalibrationOperatorImplementation& coil_compression_basis_estimate_operator() noexcept {
  static const CalibrationOperatorImplementation implementation{
    .id = kOperatorId,
    .maximum_scratch_bytes_per_firing = kMaximumScratchBytes,
    .unsupported_config_error = kUnsupportedConfigError,
    .unsupported_config_error_size = sizeof(kUnsupportedConfigError) - 1U,
    .configure = &configure,
    .is_valid = &is_valid,
    .matches_input_type = &matches_input_type,
    .matches_output_type = &matches_output_type,
    .output_type = &output_type,
    .has_compatible_input_byte_count = &has_compatible_input_byte_count,
    .expected_output_byte_count = &expected_output_byte_count,
    .required_scratch_bytes = &required_scratch_bytes,
    .estimate = &estimate,
  };
  return implementation;
}

} // namespace ksj::calibration::operators
