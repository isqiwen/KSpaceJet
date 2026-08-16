// SPDX-License-Identifier: Apache-2.0

#include "operators/noise_model_estimate.hpp"

#include "support/calibration_payload.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/linalg/decompositions.hpp"
#include "kspacejet/linalg/whitening.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/stats/moments.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

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
using ::ksj::provider::detail::checked_multiply;
using ::ksj::provider::detail::has_full_compatible_header;
using ::ksj::provider::detail::has_valid_type_descriptor;
using ::ksj::provider::detail::parse_canonical_positive_u32;

namespace {

constexpr char kOperatorId[] = "noise_model_estimate";
constexpr char kUnsupportedConfigError[] =
  "Calibration Provider noise_model_estimate requires canonical {\"channel_count\":N}, where 1 <= N <= 64";
constexpr float kEigenvalueFloor = 1.0e-6F;
constexpr std::uint64_t kMaximumScratchBytes =
  3U * static_cast<std::uint64_t>(kMaximumChannelCount) * kMaximumChannelCount * kComplexFloat32Bytes +
  static_cast<std::uint64_t>(kMaximumChannelCount) * (kComplexFloat32Bytes + sizeof(float));

struct NoiseScratchViews final {
  Complex* covariance{nullptr};
  Complex* eigenvectors{nullptr};
  Complex* eigensolver_workspace{nullptr};
  Complex* means{nullptr};
  float* eigenvalues{nullptr};
};

[[nodiscard]] bool parse_config(const ksj_byte_view& config, std::uint32_t& channel_count) noexcept {
  constexpr std::string_view kPrefix{"{\"channel_count\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const char* const first = encoded.data() + kPrefix.size();
  const char* const last = encoded.data() + encoded.size() - kSuffix.size();
  return parse_canonical_positive_u32(first, last, channel_count) && channel_count <= kMaximumChannelCount;
}

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  std::uint32_t channel_count = 0U;
  if (!parse_config(config, channel_count)) {
    return false;
  }
  operator_handle.channel_count = channel_count;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  return operator_handle.channel_count != 0U && operator_handle.channel_count <= kMaximumChannelCount;
}

[[nodiscard]] bool matches_input_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_noise_calibration_frame(&type) != 0;
}

[[nodiscard]] bool matches_output_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_noise_model(&type) != 0;
}

[[nodiscard]] ksj_type_descriptor_view output_type() {
  return ksj_type_registry_noise_model();
}

[[nodiscard]] bool has_compatible_input_byte_count(const ksj_provider_operator& operator_handle,
                                                   const std::uint64_t byte_count) noexcept {
  std::uint64_t channel_bytes = 0U;
  if (!is_valid(operator_handle) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count), kComplexFloat32Bytes,
                        channel_bytes) ||
      byte_count > kMaximumInputBytes || byte_count % channel_bytes != 0U) {
    return false;
  }
  return byte_count / channel_bytes >= 2U;
}

[[nodiscard]] bool expected_output_byte_count(const ksj_provider_operator& operator_handle,
                                              std::uint64_t& byte_count) noexcept {
  std::uint64_t matrix_element_count = 0U;
  return is_valid(operator_handle) &&
         checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count),
                          static_cast<std::uint64_t>(operator_handle.channel_count), matrix_element_count) &&
         checked_multiply(matrix_element_count, kComplexFloat32Bytes, byte_count) &&
         byte_count <= kMaximumCalibrationOutputBytes;
}

[[nodiscard]] bool required_scratch_bytes(const ksj_provider_operator& operator_handle,
                                          std::uint64_t& byte_count) noexcept {
  std::uint64_t matrix_elements = 0U;
  std::uint64_t matrix_bytes = 0U;
  std::uint64_t triple_matrix_bytes = 0U;
  std::uint64_t mean_bytes = 0U;
  std::uint64_t eigenvalue_bytes = 0U;
  if (!is_valid(operator_handle) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count),
                        static_cast<std::uint64_t>(operator_handle.channel_count), matrix_elements) ||
      !checked_multiply(matrix_elements, kComplexFloat32Bytes, matrix_bytes) ||
      !checked_multiply(matrix_bytes, 3U, triple_matrix_bytes) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count), kComplexFloat32Bytes, mean_bytes) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count), sizeof(float), eigenvalue_bytes) ||
      triple_matrix_bytes > std::numeric_limits<std::uint64_t>::max() - mean_bytes ||
      triple_matrix_bytes + mean_bytes > std::numeric_limits<std::uint64_t>::max() - eigenvalue_bytes) {
    return false;
  }
  byte_count = triple_matrix_bytes + mean_bytes + eigenvalue_bytes;
  return byte_count <= kMaximumScratchBytes;
}

[[nodiscard]] bool map_scratch(const ksj_provider_operator& operator_handle, void* const scratch,
                               const std::uint64_t scratch_byte_count, NoiseScratchViews& views) noexcept {
  std::uint64_t required_bytes = 0U;
  if (scratch == nullptr || !required_scratch_bytes(operator_handle, required_bytes) ||
      scratch_byte_count != required_bytes) {
    return false;
  }
  const auto channels = static_cast<std::size_t>(operator_handle.channel_count);
  const auto matrix_elements = channels * channels;
  auto* cursor = static_cast<std::byte*>(scratch);
  views.covariance = reinterpret_cast<Complex*>(cursor);
  cursor += matrix_elements * sizeof(Complex);
  views.eigenvectors = reinterpret_cast<Complex*>(cursor);
  cursor += matrix_elements * sizeof(Complex);
  views.eigensolver_workspace = reinterpret_cast<Complex*>(cursor);
  cursor += matrix_elements * sizeof(Complex);
  views.means = reinterpret_cast<Complex*>(cursor);
  cursor += channels * sizeof(Complex);
  views.eigenvalues = reinterpret_cast<float*>(cursor);
  return true;
}

[[nodiscard]] bool estimate(const ksj_provider_operator& operator_handle, const void* const input,
                            const std::uint64_t input_byte_count, void* const output,
                            const std::uint64_t output_byte_count, void* const scratch,
                            const std::uint64_t scratch_byte_count) noexcept {
  try {
    if (input == nullptr || output == nullptr || !has_compatible_input_byte_count(operator_handle, input_byte_count)) {
      return false;
    }
    std::uint64_t expected_output_byte_count_value = 0U;
    if (!expected_output_byte_count(operator_handle, expected_output_byte_count_value) ||
        output_byte_count != expected_output_byte_count_value) {
      return false;
    }

    const auto channel_count = static_cast<std::size_t>(operator_handle.channel_count);
    const auto sample_count = static_cast<std::size_t>(
      input_byte_count / (static_cast<std::uint64_t>(operator_handle.channel_count) * kComplexFloat32Bytes));
    NoiseScratchViews scratch_views{};
    if (!map_scratch(operator_handle, scratch, scratch_byte_count, scratch_views)) {
      return false;
    }
    const auto samples =
      ksj::array::MatrixView<const Complex>{static_cast<const Complex*>(input), channel_count, sample_count};
    const auto covariance = ksj::array::MatrixView<Complex>{scratch_views.covariance, channel_count, channel_count};
    const auto eigenvectors = ksj::array::MatrixView<Complex>{scratch_views.eigenvectors, channel_count, channel_count};
    const auto eigensolver_workspace =
      ksj::array::MatrixView<Complex>{scratch_views.eigensolver_workspace, channel_count, channel_count};
    const auto means = ksj::array::VectorView<Complex>{scratch_views.means, channel_count};
    const auto eigenvalues = ksj::array::VectorView<float>{scratch_views.eigenvalues, channel_count};
    const auto whitening = ksj::array::MatrixView<Complex>{static_cast<Complex*>(output), channel_count, channel_count};
    ksj::stats::covariance_channel_major_with_workspace(samples, covariance, means,
                                                        ksj::stats::VarianceNormalization::population);
    if (!ksj::linalg::self_adjoint_eigen_decomposition_with_workspace(covariance.as_const(), eigenvalues, eigenvectors,
                                                                      eigensolver_workspace)) {
      return false;
    }
    ksj::linalg::whitening_matrix_from_self_adjoint_eigen_with_workspace(
      eigenvalues.as_const(), eigenvectors.as_const(), whitening, kEigenvalueFloor);
    return true;
  } catch (...) {
    KSJ_LOG_ERROR("noise_model_estimate trapped an unexpected exception while estimating a noise model");
    return false;
  }
}

} // namespace

const CalibrationOperatorImplementation& noise_model_estimate_operator() noexcept {
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
