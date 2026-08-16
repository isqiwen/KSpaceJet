// SPDX-License-Identifier: Apache-2.0

#include "operators/phase_correction_estimate.hpp"

#include "support/calibration_payload.hpp"

#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <cmath>
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
using ::ksj::calibration::support::kMaximumReadoutSampleCount;
using ::ksj::calibration::support::read_complex_float32;
using ::ksj::calibration::support::write_complex_float32;
using ::ksj::provider::detail::checked_multiply;
using ::ksj::provider::detail::has_full_compatible_header;
using ::ksj::provider::detail::has_valid_type_descriptor;
using ::ksj::provider::detail::parse_canonical_positive_u32;

namespace {

constexpr char kOperatorId[] = "phase_correction_estimate";
constexpr char kUnsupportedConfigError[] =
  "Calibration Provider phase_correction_estimate requires canonical "
  "{\"channel_count\":N,\"readout_sample_count\":K}, where 1 <= N <= 64 and 1 <= K <= 4096";

[[nodiscard]] bool parse_config(const ksj_byte_view& config, std::uint32_t& channel_count,
                                std::uint32_t& readout_sample_count) noexcept {
  constexpr std::string_view kPrefix{"{\"channel_count\":"};
  constexpr std::string_view kMiddle{",\"readout_sample_count\":"};
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
  const char* const channel_first = first + kPrefix.size();
  const char* const channel_last = first + middle_offset;
  const char* const readout_first = channel_last + kMiddle.size();
  const char* const readout_last = first + encoded.size() - kSuffix.size();
  return parse_canonical_positive_u32(channel_first, channel_last, channel_count) &&
         parse_canonical_positive_u32(readout_first, readout_last, readout_sample_count) &&
         channel_count <= kMaximumChannelCount && readout_sample_count <= kMaximumReadoutSampleCount;
}

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  std::uint32_t channel_count = 0U;
  std::uint32_t readout_sample_count = 0U;
  if (!parse_config(config, channel_count, readout_sample_count)) {
    return false;
  }
  operator_handle.channel_count = channel_count;
  operator_handle.readout_sample_count = readout_sample_count;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  return operator_handle.channel_count != 0U && operator_handle.channel_count <= kMaximumChannelCount &&
         operator_handle.readout_sample_count != 0U &&
         operator_handle.readout_sample_count <= kMaximumReadoutSampleCount;
}

[[nodiscard]] bool matches_input_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_phase_reference_frame(&type) != 0;
}

[[nodiscard]] bool matches_output_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_phase_model(&type) != 0;
}

[[nodiscard]] ksj_type_descriptor_view output_type() {
  return ksj_type_registry_phase_model();
}

[[nodiscard]] bool input_element_count(const ksj_provider_operator& operator_handle, const std::uint64_t byte_count,
                                       std::uint64_t& element_count) noexcept {
  std::uint64_t channel_readout_count = 0U;
  std::uint64_t frame_bytes = 0U;
  if (!is_valid(operator_handle) ||
      !checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count),
                        static_cast<std::uint64_t>(operator_handle.readout_sample_count), channel_readout_count) ||
      !checked_multiply(channel_readout_count, kComplexFloat32Bytes, frame_bytes) || frame_bytes == 0U ||
      byte_count > kMaximumInputBytes || byte_count % frame_bytes != 0U) {
    return false;
  }
  element_count = byte_count / frame_bytes;
  return element_count >= 1U;
}

[[nodiscard]] bool has_compatible_input_byte_count(const ksj_provider_operator& operator_handle,
                                                   const std::uint64_t byte_count) noexcept {
  std::uint64_t reference_line_count = 0U;
  return input_element_count(operator_handle, byte_count, reference_line_count);
}

[[nodiscard]] bool expected_output_byte_count(const ksj_provider_operator& operator_handle,
                                              std::uint64_t& byte_count) noexcept {
  std::uint64_t element_count = 0U;
  return is_valid(operator_handle) &&
         checked_multiply(static_cast<std::uint64_t>(operator_handle.channel_count),
                          static_cast<std::uint64_t>(operator_handle.readout_sample_count), element_count) &&
         checked_multiply(element_count, kComplexFloat32Bytes, byte_count) &&
         byte_count <= kMaximumCalibrationOutputBytes;
}

[[nodiscard]] bool required_scratch_bytes(const ksj_provider_operator&, std::uint64_t& byte_count) noexcept {
  byte_count = 0U;
  return true;
}

[[nodiscard]] bool estimate(const ksj_provider_operator& operator_handle, const void* const input,
                            const std::uint64_t input_byte_count, void* const output,
                            const std::uint64_t output_byte_count, void* const scratch,
                            const std::uint64_t scratch_byte_count) noexcept {
  if (input == nullptr || output == nullptr) {
    return false;
  }
  if (scratch != nullptr || scratch_byte_count != 0U) {
    return false;
  }
  std::uint64_t reference_line_count_value = 0U;
  std::uint64_t expected_output_byte_count_value = 0U;
  if (!input_element_count(operator_handle, input_byte_count, reference_line_count_value) ||
      !expected_output_byte_count(operator_handle, expected_output_byte_count_value) ||
      output_byte_count != expected_output_byte_count_value) {
    return false;
  }

  const auto channel_count = static_cast<std::size_t>(operator_handle.channel_count);
  const auto reference_line_count = static_cast<std::size_t>(reference_line_count_value);
  const auto readout_sample_count = static_cast<std::size_t>(operator_handle.readout_sample_count);
  for (std::size_t channel = 0U; channel < channel_count; ++channel) {
    for (std::size_t readout = 0U; readout < readout_sample_count; ++readout) {
      Complex sum{};
      for (std::size_t reference_line = 0U; reference_line < reference_line_count; ++reference_line) {
        const auto input_index = (channel * reference_line_count + reference_line) * readout_sample_count + readout;
        sum += read_complex_float32(input, input_index);
      }
      const float magnitude = std::abs(sum);
      const Complex correction = magnitude == 0.0F ? Complex{1.0F, 0.0F} : std::conj(sum) / magnitude;
      write_complex_float32(output, channel * readout_sample_count + readout, correction);
    }
  }
  return true;
}

} // namespace

const CalibrationOperatorImplementation& phase_correction_estimate_operator() noexcept {
  static const CalibrationOperatorImplementation implementation{
    .id = kOperatorId,
    .maximum_scratch_bytes_per_firing = 0U,
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
