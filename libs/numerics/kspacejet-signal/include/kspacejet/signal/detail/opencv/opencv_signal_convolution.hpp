#pragma once

#include "kspacejet/array/array.hpp"

#include <type_traits>

namespace ksj::signal::detail::opencv {

template <typename T>
inline constexpr bool opencv_signal_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

[[nodiscard]] bool correlate2d_same(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                                    ksj::array::ImageView<float> output);
[[nodiscard]] bool correlate2d_same(ksj::array::ImageView<const double> input,
                                    ksj::array::ImageView<const double> kernel, ksj::array::ImageView<double> output);

template <typename T>
[[nodiscard]] bool correlate2d_same(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                                    ksj::array::ImageView<T> output) {
  if constexpr (opencv_signal_scalar_v<T>) {
    return correlate2d_same(input, kernel, output);
  } else {
    (void)input;
    (void)kernel;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool correlate2d_same_separable(ksj::array::ImageView<const float> input,
                                              ksj::array::VectorView<const float> row_kernel,
                                              ksj::array::VectorView<const float> col_kernel,
                                              ksj::array::ImageView<float> output);
[[nodiscard]] bool correlate2d_same_separable(ksj::array::ImageView<const double> input,
                                              ksj::array::VectorView<const double> row_kernel,
                                              ksj::array::VectorView<const double> col_kernel,
                                              ksj::array::ImageView<double> output);

template <typename T>
[[nodiscard]] bool
correlate2d_same_separable(ksj::array::ImageView<const T> input, ksj::array::VectorView<const T> row_kernel,
                           ksj::array::VectorView<const T> col_kernel, ksj::array::ImageView<T> output) {
  if constexpr (opencv_signal_scalar_v<T>) {
    return correlate2d_same_separable(input, row_kernel, col_kernel, output);
  } else {
    (void)input;
    (void)row_kernel;
    (void)col_kernel;
    (void)output;
    return false;
  }
}

} // namespace ksj::signal::detail::opencv
