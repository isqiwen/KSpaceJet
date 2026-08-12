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

[[nodiscard]] bool dilate(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                          std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                          StructuringElementShape shape = StructuringElementShape::rectangle);
[[nodiscard]] bool dilate(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                          std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                          StructuringElementShape shape = StructuringElementShape::rectangle);
template <typename T>
[[nodiscard]] bool dilate(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,
                          BorderMode, StructuringElementShape = StructuringElementShape::rectangle) {
  return false;
}

template <typename T>
[[nodiscard]] bool dilate(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                          const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                          const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return dilate(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

[[nodiscard]] bool erode(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                         std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                         StructuringElementShape shape = StructuringElementShape::rectangle);
[[nodiscard]] bool erode(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                         std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                         StructuringElementShape shape = StructuringElementShape::rectangle);
template <typename T>
[[nodiscard]] bool erode(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t, BorderMode,
                         StructuringElementShape = StructuringElementShape::rectangle) {
  return false;
}

template <typename T>
[[nodiscard]] bool erode(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                         const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                         const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return erode(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

[[nodiscard]] bool morph_open(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                              std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                              StructuringElementShape shape = StructuringElementShape::rectangle);
[[nodiscard]] bool morph_open(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                              std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                              StructuringElementShape shape = StructuringElementShape::rectangle);
template <typename T>
[[nodiscard]] bool morph_open(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,
                              BorderMode, StructuringElementShape = StructuringElementShape::rectangle) {
  return false;
}

template <typename T>
[[nodiscard]] bool morph_open(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode,
                              const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_open(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

[[nodiscard]] bool morph_close(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                               std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                               StructuringElementShape shape = StructuringElementShape::rectangle);
[[nodiscard]] bool morph_close(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                               std::size_t kernel_rows, std::size_t kernel_cols, BorderMode border_mode,
                               StructuringElementShape shape = StructuringElementShape::rectangle);
template <typename T>
[[nodiscard]] bool morph_close(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,
                               BorderMode, StructuringElementShape = StructuringElementShape::rectangle) {
  return false;
}

template <typename T>
[[nodiscard]] bool morph_close(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                               const std::size_t kernel_rows, const std::size_t kernel_cols,
                               const BorderMode border_mode,
                               const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_close(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

[[nodiscard]] bool distance_transform_l2(ksj::array::ImageView<const std::uint8_t> input,
                                         ksj::array::ImageView<float> output);
} // namespace ksj::image::detail::opencv
