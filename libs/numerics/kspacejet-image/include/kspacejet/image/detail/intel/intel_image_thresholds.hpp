#pragma once

#include "kspacejet/array/array.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] bool threshold(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                             float threshold_value, float low_value, float high_value);
template <typename T> [[nodiscard]] bool threshold(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, T, T, T) {
  return false;
}

[[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output);
template <typename T> [[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const T>, ksj::array::ImageView<T>) {
  return false;
}

} // namespace ksj::image::detail::intel
