// SPDX-License-Identifier: Apache-2.0

#include "image_scale_float32.hpp"

#include "support/image_transform_config.hpp"

#include <cmath>

namespace ksj::image_ops::operators {

using ::ksj::image_ops::state::ImageOperatorImplementation;
using ::ksj::image_ops::support::parse_single_finite_decimal_config;
namespace {

constexpr char kOperatorId[] = "image_scale_float32";
constexpr char kUnsupportedConfigError[] = "Image-ops Provider requires canonical {\"factor\":<finite-decimal>}";

[[nodiscard]] bool configure(const ksj_byte_view& config, ksj_provider_operator& operator_handle) noexcept {
  float factor = 0.0F;
  if (!parse_single_finite_decimal_config(config, "{\"factor\":", factor)) {
    return false;
  }
  operator_handle.scalar = factor;
  return true;
}

[[nodiscard]] bool is_valid(const ksj_provider_operator& operator_handle) noexcept {
  return std::isfinite(operator_handle.scalar);
}

[[nodiscard]] float transform(const ksj_provider_operator& operator_handle, const float value) noexcept {
  return value * operator_handle.scalar;
}

} // namespace

const ImageOperatorImplementation& image_scale_float32_operator() noexcept {
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
