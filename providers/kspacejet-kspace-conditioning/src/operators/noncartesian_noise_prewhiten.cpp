// SPDX-License-Identifier: Apache-2.0
//
// Apply one explicit channel-domain whitening matrix to channel-major
// non-Cartesian k-space. The public linalg matmul operates directly on the
// ABI payload views: W[channel_out, channel_in] times X[channel_in, sample].

#include "operators/noncartesian_noise_prewhiten.hpp"

#include "support/kspace_frame.hpp"

#include "kspacejet/array/views.hpp"
#include "kspacejet/linalg/blas.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/provider/detail/provider_support.hpp"
#include "kspacejet/provider/type_registry.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace ksj::kspace_conditioning::operators {

using ::ksj::kspace_conditioning::state::ConditioningOperatorImplementation;
using ::ksj::kspace_conditioning::support::Complex;
using ::ksj::kspace_conditioning::support::complex_matrix_byte_count;
using ::ksj::kspace_conditioning::support::kMaximumChannelCount;
using ::ksj::kspace_conditioning::support::kMaximumNoncartesianSampleCount;
using ::ksj::kspace_conditioning::support::kMinimumNoncartesianSampleCount;
using ::ksj::kspace_conditioning::support::noncartesian_kspace_frame_byte_count;
using ::ksj::provider::detail::has_full_compatible_header;
using ::ksj::provider::detail::has_valid_type_descriptor;
using ::ksj::provider::detail::parse_canonical_positive_u32;

namespace {

constexpr char kOperatorId[] = "noncartesian_noise_prewhiten";
constexpr char kUnsupportedConfigError[] =
  "K-space-conditioning Provider noncartesian_noise_prewhiten requires canonical "
  "{\"channel_count\":N,\"sample_count\":N}, where 1 <= channel_count <= 64 and 1 <= sample_count <= 65536";

[[nodiscard]] bool parse_config(const ksj_byte_view& config, std::uint32_t& channel_count,
                                std::uint32_t& sample_count) noexcept {
  constexpr std::string_view kPrefix{"{\"channel_count\":"};
  constexpr std::string_view kSamples{",\"sample_count\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto samples_offset = encoded.find(kSamples, kPrefix.size());
  if (samples_offset == std::string_view::npos) {
    return false;
  }
  const char* const first = encoded.data();
  const char* const channels_first = first + kPrefix.size();
  const char* const channels_last = first + samples_offset;
  const char* const samples_first = channels_last + kSamples.size();
  const char* const samples_last = first + encoded.size() - kSuffix.size();
  return parse_canonical_positive_u32(channels_first, channels_last, channel_count) &&
         parse_canonical_positive_u32(samples_first, samples_last, sample_count) &&
         channel_count <= kMaximumChannelCount && sample_count >= kMinimumNoncartesianSampleCount &&
         sample_count <= kMaximumNoncartesianSampleCount;
}

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  std::uint32_t channel_count = 0U;
  std::uint32_t sample_count = 0U;
  if (!parse_config(config, channel_count, sample_count)) {
    return false;
  }
  operator_handle.channel_count = channel_count;
  operator_handle.sample_count = sample_count;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  return operator_handle.channel_count != 0U && operator_handle.channel_count <= kMaximumChannelCount &&
         operator_handle.sample_count >= kMinimumNoncartesianSampleCount &&
         operator_handle.sample_count <= kMaximumNoncartesianSampleCount;
}

[[nodiscard]] bool matches_dynamic_input_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_noncartesian_kspace_frame(&type) != 0;
}

[[nodiscard]] bool matches_static_input_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_noise_model(&type) != 0;
}

[[nodiscard]] bool matches_output_type(const ksj_type_descriptor_view& type) noexcept {
  return has_valid_type_descriptor(type) && ksj_type_registry_matches_noncartesian_kspace_frame(&type) != 0;
}

[[nodiscard]] ksj_type_descriptor_view output_type() {
  return ksj_type_registry_noncartesian_kspace_frame();
}

[[nodiscard]] bool expected_dynamic_input_byte_count(const ksj_provider_operator& operator_handle,
                                                     std::uint64_t& byte_count) noexcept {
  return is_valid(operator_handle) &&
         noncartesian_kspace_frame_byte_count(operator_handle.channel_count, operator_handle.sample_count, byte_count);
}

[[nodiscard]] bool expected_static_input_byte_count(const ksj_provider_operator& operator_handle,
                                                    std::uint64_t& byte_count) noexcept {
  return is_valid(operator_handle) &&
         complex_matrix_byte_count(operator_handle.channel_count, operator_handle.channel_count, byte_count);
}

[[nodiscard]] bool expected_output_byte_count(const ksj_provider_operator& operator_handle,
                                              std::uint64_t& byte_count) noexcept {
  return expected_dynamic_input_byte_count(operator_handle, byte_count);
}

[[nodiscard]] bool transform(const ksj_provider_operator& operator_handle, const void* const dynamic_input,
                             const void* const static_input, void* const output) noexcept {
  try {
    if (dynamic_input == nullptr || static_input == nullptr || output == nullptr || !is_valid(operator_handle)) {
      return false;
    }
    const auto channels = static_cast<std::size_t>(operator_handle.channel_count);
    const auto samples = static_cast<std::size_t>(operator_handle.sample_count);
    const auto whitening =
      ksj::array::MatrixView<const Complex>{static_cast<const Complex*>(static_input), channels, channels};
    const auto kspace =
      ksj::array::MatrixView<const Complex>{static_cast<const Complex*>(dynamic_input), channels, samples};
    const auto conditioned = ksj::array::MatrixView<Complex>{static_cast<Complex*>(output), channels, samples};
    ksj::linalg::matmul(whitening, kspace, conditioned);
    return true;
  } catch (...) {
    KSJ_LOG_ERROR("noncartesian_noise_prewhiten trapped an unexpected exception while transforming k-space");
    return false;
  }
}

} // namespace

const ConditioningOperatorImplementation& noncartesian_noise_prewhiten_operator() noexcept {
  static const ConditioningOperatorImplementation implementation{
    .id = kOperatorId,
    .unsupported_config_error = kUnsupportedConfigError,
    .unsupported_config_error_size = sizeof(kUnsupportedConfigError) - 1U,
    .input_batch_count = 2U,
    .configure = &configure,
    .is_valid = &is_valid,
    .matches_dynamic_input_type = &matches_dynamic_input_type,
    .matches_static_input_type = &matches_static_input_type,
    .matches_output_type = &matches_output_type,
    .output_type = &output_type,
    .expected_dynamic_input_byte_count = &expected_dynamic_input_byte_count,
    .expected_static_input_byte_count = &expected_static_input_byte_count,
    .expected_output_byte_count = &expected_output_byte_count,
    .transform = &transform,
  };
  return implementation;
}

} // namespace ksj::kspace_conditioning::operators
