// SPDX-License-Identifier: Apache-2.0

#include "image_clamp_float32.hpp"

#include "support/image_transform_config.hpp"

#include "kspacejet/provider/detail/provider_support.hpp"

#include <cmath>
#include <limits>
#include <string_view>

namespace ksj::image_ops::operators {

using ::ksj::image_ops::state::ImageOperatorImplementation;
using ::ksj::image_ops::support::parse_canonical_finite_decimal;
using ::ksj::provider::detail::has_full_compatible_header;

namespace {

constexpr char kOperatorId[] = "image_clamp_float32";
constexpr char kUnsupportedConfigError[] =
  "Image-ops Provider requires canonical {\"maximum\":<finite-decimal>,\"minimum\":<finite-decimal>}";

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  constexpr std::string_view kPrefix{"{\"maximum\":"};
  constexpr std::string_view kSeparator{",\"minimum\":"};
  constexpr std::string_view kSuffix{"}"};
  if (!has_full_compatible_header(&config) || config.data == nullptr || config.size == 0U ||
      config.size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  const auto encoded = std::string_view{static_cast<const char*>(config.data), static_cast<std::size_t>(config.size)};
  if (!encoded.starts_with(kPrefix) || !encoded.ends_with(kSuffix)) {
    return false;
  }
  const auto maximum_first = kPrefix.size();
  const auto separator_offset = encoded.find(kSeparator, maximum_first);
  if (separator_offset == std::string_view::npos) {
    return false;
  }
  const char* const first = encoded.data();
  const char* const maximum_value_first = first + maximum_first;
  const char* const maximum_value_last = first + separator_offset;
  const char* const minimum_value_first = maximum_value_last + kSeparator.size();
  const char* const minimum_value_last = first + encoded.size() - kSuffix.size();
  float maximum = 0.0F;
  float minimum = 0.0F;
  if (!parse_canonical_finite_decimal(maximum_value_first, maximum_value_last, maximum) ||
      !parse_canonical_finite_decimal(minimum_value_first, minimum_value_last, minimum) || minimum > maximum) {
    return false;
  }
  operator_handle.clamp_minimum = minimum;
  operator_handle.clamp_maximum = maximum;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  return std::isfinite(operator_handle.clamp_minimum) && std::isfinite(operator_handle.clamp_maximum) &&
         operator_handle.clamp_minimum <= operator_handle.clamp_maximum;
}

[[nodiscard]] float transform(const ksj_provider_operator& operator_handle, const float value) noexcept {
  // This comparison form intentionally preserves NaN while clamping both
  // infinities to the declared closed interval.
  if (value < operator_handle.clamp_minimum) {
    return operator_handle.clamp_minimum;
  }
  if (value > operator_handle.clamp_maximum) {
    return operator_handle.clamp_maximum;
  }
  return value;
}

} // namespace

const ImageOperatorImplementation& image_clamp_float32_operator() noexcept {
  static const ImageOperatorImplementation implementation{
    .id = kOperatorId,
    .unsupported_config_error = kUnsupportedConfigError,
    .unsupported_config_error_size = sizeof(kUnsupportedConfigError) - 1U,
    .configure = &configure,
    .is_valid = &is_valid,
    .transform = &transform,
  };
  return implementation;
}

} // namespace ksj::image_ops::operators
