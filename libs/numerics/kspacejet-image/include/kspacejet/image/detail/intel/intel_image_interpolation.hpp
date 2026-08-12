#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/image/types.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] InterpolationResult rotate_cubic_bspline_smooth(ksj::array::ImageView<const float> input,
                                                              ksj::array::ImageView<float> output,
                                                              double angle_degrees);
[[nodiscard]] InterpolationResult rotate_cubic_bspline_smooth(ksj::array::ImageView<const ksj::base::cf32> input,
                                                              ksj::array::ImageView<ksj::base::cf32> output,
                                                              double angle_degrees);
[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_linear(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);

template <typename T>
[[nodiscard]] InterpolationResult rotate_cubic_bspline_smooth(ksj::array::ImageView<const T>, ksj::array::ImageView<T>,
                                                              double) {
  return {InterpolationStatus::backend_error, 0};
}

template <typename T>
[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

template <typename T> [[nodiscard]] bool resize_linear(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

template <typename T> [[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

[[nodiscard]] InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                               InterpolationAxis axis, float ratio);

} // namespace ksj::image::detail::intel
