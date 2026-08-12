#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] bool filter2d_region(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                                   ksj::array::ImageView<float> output, ksj::array::detail::NormalizedSlice rows,
                                   ksj::array::detail::NormalizedSlice cols, FilterAnchor anchor);

[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 std::size_t kernel_size, double sigma, BorderMode border_mode);
[[nodiscard]] bool box_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                              std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode);
[[nodiscard]] bool median_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 std::size_t kernel_size, BorderMode border_mode);
[[nodiscard]] bool sobel_x(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                           BorderMode border_mode);
[[nodiscard]] bool sobel_y(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                           BorderMode border_mode);

template <typename T>
[[nodiscard]] bool filter2d_region(ksj::array::ImageView<const T>, ksj::array::ImageView<const T>,
                                   ksj::array::ImageView<T>, ksj::array::detail::NormalizedSlice,
                                   ksj::array::detail::NormalizedSlice, FilterAnchor) {
  return false;
}

template <typename T>
[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, double,
                                 BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool box_filter(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,
                              BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool median_filter(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, BorderMode) {
  return false;
}

template <typename T> [[nodiscard]] bool sobel_x(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode) {
  return false;
}

template <typename T> [[nodiscard]] bool sobel_y(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode) {
  return false;
}

} // namespace ksj::image::detail::intel
