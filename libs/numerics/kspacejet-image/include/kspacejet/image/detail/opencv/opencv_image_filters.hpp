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

[[nodiscard]] bool box_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                              std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode);
[[nodiscard]] bool box_filter(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                              std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode);
template <typename T>
[[nodiscard]] bool box_filter(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,
                              BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode) {
  return box_filter(input.view(), output.view(), kernel_rows, kernel_cols, border_mode);
}

[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 std::size_t kernel_size, double sigma, BorderMode border_mode);
[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                                 std::size_t kernel_size, double sigma, BorderMode border_mode);
template <typename T>
[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, double,
                                 BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool gaussian_blur(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                 const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  return gaussian_blur(input.view(), output.view(), kernel_size, sigma, border_mode);
}

[[nodiscard]] bool bilateral_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                    std::size_t diameter, double sigma_color, double sigma_space,
                                    BorderMode border_mode);
template <typename T>
[[nodiscard]] bool bilateral_filter(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, double,
                                    double, BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool bilateral_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                    const std::size_t diameter, const double sigma_color, const double sigma_space,
                                    const BorderMode border_mode) {
  return bilateral_filter(input.view(), output.view(), diameter, sigma_color, sigma_space, border_mode);
}

[[nodiscard]] bool median_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 std::size_t kernel_size, BorderMode border_mode);
template <typename T>
[[nodiscard]] bool median_filter(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool median_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                 const std::size_t kernel_size, const BorderMode border_mode) {
  return median_filter(input.view(), output.view(), kernel_size, border_mode);
}

[[nodiscard]] bool sobel_x(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                           BorderMode border_mode);
[[nodiscard]] bool sobel_x(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                           BorderMode border_mode);
template <typename T> [[nodiscard]] bool sobel_x(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool sobel_x(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                           const BorderMode border_mode) {
  return sobel_x(input.view(), output.view(), border_mode);
}

[[nodiscard]] bool sobel_y(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                           BorderMode border_mode);
[[nodiscard]] bool sobel_y(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                           BorderMode border_mode);
template <typename T> [[nodiscard]] bool sobel_y(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool sobel_y(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                           const BorderMode border_mode) {
  return sobel_y(input.view(), output.view(), border_mode);
}

[[nodiscard]] bool gradient_magnitude(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                      BorderMode border_mode);
[[nodiscard]] bool gradient_magnitude(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                                      BorderMode border_mode);
template <typename T>
[[nodiscard]] bool gradient_magnitude(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode) {
  return false;
}

template <typename T>
[[nodiscard]] bool gradient_magnitude(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                      const BorderMode border_mode) {
  return gradient_magnitude(input.view(), output.view(), border_mode);
}
} // namespace ksj::image::detail::opencv
