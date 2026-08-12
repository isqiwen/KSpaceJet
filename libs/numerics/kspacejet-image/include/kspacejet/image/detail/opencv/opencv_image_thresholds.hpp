#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::image::detail::opencv {

[[nodiscard]] bool threshold(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                             float threshold_value, float low_value, float high_value);
[[nodiscard]] bool threshold(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                             double threshold_value, double low_value, double high_value);
template <typename T> [[nodiscard]] bool threshold(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, T, T, T) {
  return false;
}

[[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output);
[[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output);
template <typename T> [[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const T>, ksj::array::ImageView<T>) {
  return false;
}
} // namespace ksj::image::detail::opencv
