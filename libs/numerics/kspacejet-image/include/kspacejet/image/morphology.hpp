#pragma once

/// Morphological dilation, erosion, opening, closing, and structuring-element operations.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_morphology.hpp"
#include "kspacejet/image/detail/image_policy.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_morphology.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace ksj::image {

template <typename T>
void dilate(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
            const BorderMode border_mode = BorderMode::replicate,
            const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    dilate(input, temp.view(), kernel_size, border_mode, shape);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_morphology<T>(input.size()) &&
      detail::opencv::dilate(input, output, kernel_size, kernel_size, border_mode, shape)) {
    return;
  }

  detail::eigen::dilate(input, output, kernel_size, kernel_size, border_mode, shape);
}

template <typename T>
void dilate(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
            const BorderMode border_mode = BorderMode::replicate,
            const StructuringElementShape shape = StructuringElementShape::rectangle) {
  dilate(ksj::array::as_const_view(input), output, kernel_size, border_mode, shape);
}

template <typename T>
void dilate(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
            const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate,
            const StructuringElementShape shape = StructuringElementShape::rectangle) {
  dilate(input.view(), output.view(), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
dilate(ksj::array::ImageView<const T> input, const std::size_t kernel_size = 3,
       const BorderMode border_mode = BorderMode::replicate,
       const StructuringElementShape shape = StructuringElementShape::rectangle) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  dilate(input, output.view(), kernel_size, border_mode, shape);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
dilate(ksj::array::ImageView<T> input, const std::size_t kernel_size = 3,
       const BorderMode border_mode = BorderMode::replicate,
       const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return dilate(ksj::array::as_const_view(input), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
dilate(const ksj::array::PooledImage<T>& input, const std::size_t kernel_size = 3,
       const BorderMode border_mode = BorderMode::replicate,
       const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return dilate(input.view(), kernel_size, border_mode, shape);
}

template <typename T>
void erode(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    erode(input, temp.view(), kernel_size, border_mode, shape);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_morphology<T>(input.size()) &&
      detail::opencv::erode(input, output, kernel_size, kernel_size, border_mode, shape)) {
    return;
  }

  detail::eigen::erode(input, output, kernel_size, kernel_size, border_mode, shape);
}

template <typename T>
void erode(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  erode(ksj::array::as_const_view(input), output, kernel_size, border_mode, shape);
}

template <typename T>
void erode(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
           const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  erode(input.view(), output.view(), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
erode(ksj::array::ImageView<const T> input, const std::size_t kernel_size = 3,
      const BorderMode border_mode = BorderMode::replicate,
      const StructuringElementShape shape = StructuringElementShape::rectangle) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  erode(input, output.view(), kernel_size, border_mode, shape);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
erode(ksj::array::ImageView<T> input, const std::size_t kernel_size = 3,
      const BorderMode border_mode = BorderMode::replicate,
      const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return erode(ksj::array::as_const_view(input), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
erode(const ksj::array::PooledImage<T>& input, const std::size_t kernel_size = 3,
      const BorderMode border_mode = BorderMode::replicate,
      const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return erode(input.view(), kernel_size, border_mode, shape);
}

template <typename T>
void morph_open(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate,
                const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    morph_open(input, temp.view(), kernel_size, border_mode, shape);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_morphology<T>(input.size()) &&
      detail::opencv::morph_open(input, output, kernel_size, kernel_size, border_mode, shape)) {
    return;
  }

  detail::eigen::morph_open(input, output, kernel_size, kernel_size, border_mode, shape);
}

template <typename T>
void morph_open(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
                const BorderMode border_mode = BorderMode::replicate,
                const StructuringElementShape shape = StructuringElementShape::rectangle) {
  morph_open(ksj::array::as_const_view(input), output, kernel_size, border_mode, shape);
}

template <typename T>
void morph_open(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate,
                const StructuringElementShape shape = StructuringElementShape::rectangle) {
  morph_open(input.view(), output.view(), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_open(ksj::array::ImageView<const T> input, const std::size_t kernel_size = 3,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  morph_open(input, output.view(), kernel_size, border_mode, shape);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_open(ksj::array::ImageView<T> input, const std::size_t kernel_size = 3,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_open(ksj::array::as_const_view(input), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_open(const ksj::array::PooledImage<T>& input, const std::size_t kernel_size = 3,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_open(input.view(), kernel_size, border_mode, shape);
}

template <typename T>
void morph_open(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_rows,
                const std::size_t kernel_cols, const BorderMode border_mode = BorderMode::replicate,
                const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    morph_open(input, temp.view(), kernel_rows, kernel_cols, border_mode, shape);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_morphology<T>(input.size()) &&
      detail::opencv::morph_open(input, output, kernel_rows, kernel_cols, border_mode, shape)) {
    return;
  }

  detail::eigen::morph_open(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
void morph_open(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_rows,
                const std::size_t kernel_cols, const BorderMode border_mode = BorderMode::replicate,
                const StructuringElementShape shape = StructuringElementShape::rectangle) {
  morph_open(ksj::array::as_const_view(input), output, kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
void morph_open(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_rows, const std::size_t kernel_cols,
                const BorderMode border_mode = BorderMode::replicate,
                const StructuringElementShape shape = StructuringElementShape::rectangle) {
  morph_open(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_open(ksj::array::ImageView<const T> input, const std::size_t kernel_rows, const std::size_t kernel_cols,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  morph_open(input, output.view(), kernel_rows, kernel_cols, border_mode, shape);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_open(ksj::array::ImageView<T> input, const std::size_t kernel_rows, const std::size_t kernel_cols,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_open(ksj::array::as_const_view(input), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_open(const ksj::array::PooledImage<T>& input, const std::size_t kernel_rows, const std::size_t kernel_cols,
           const BorderMode border_mode = BorderMode::replicate,
           const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_open(input.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
void morph_close(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                 const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate,
                 const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    morph_close(input, temp.view(), kernel_size, border_mode, shape);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_morphology<T>(input.size()) &&
      detail::opencv::morph_close(input, output, kernel_size, kernel_size, border_mode, shape)) {
    return;
  }

  detail::eigen::morph_close(input, output, kernel_size, kernel_size, border_mode, shape);
}

template <typename T>
void morph_close(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
                 const BorderMode border_mode = BorderMode::replicate,
                 const StructuringElementShape shape = StructuringElementShape::rectangle) {
  morph_close(ksj::array::as_const_view(input), output, kernel_size, border_mode, shape);
}

template <typename T>
void morph_close(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                 const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate,
                 const StructuringElementShape shape = StructuringElementShape::rectangle) {
  morph_close(input.view(), output.view(), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_close(ksj::array::ImageView<const T> input, const std::size_t kernel_size = 3,
            const BorderMode border_mode = BorderMode::replicate,
            const StructuringElementShape shape = StructuringElementShape::rectangle) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  morph_close(input, output.view(), kernel_size, border_mode, shape);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_close(ksj::array::ImageView<T> input, const std::size_t kernel_size = 3,
            const BorderMode border_mode = BorderMode::replicate,
            const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_close(ksj::array::as_const_view(input), kernel_size, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_close(const ksj::array::PooledImage<T>& input, const std::size_t kernel_size = 3,
            const BorderMode border_mode = BorderMode::replicate,
            const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_close(input.view(), kernel_size, border_mode, shape);
}

inline void distance_transform_l2(ksj::array::ImageView<const std::uint8_t> input,
                                  ksj::array::ImageView<float> output) {
  if (!detail::opencv::distance_transform_l2(input, output)) {
    throw std::runtime_error("distance_transform_l2 OpenCV backend failed");
  }
}

inline void distance_transform_l2(const ksj::array::PooledImage<std::uint8_t>& input,
                                  ksj::array::PooledImage<float>& output) {
  distance_transform_l2(input.view(), output.view());
}

[[nodiscard]] inline ksj::array::PooledImage<float>
distance_transform_l2(ksj::array::ImageView<const std::uint8_t> input) {
  auto output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  distance_transform_l2(input, output.view());
  return output;
}

[[nodiscard]] inline ksj::array::PooledImage<float>
distance_transform_l2(const ksj::array::PooledImage<std::uint8_t>& input) {
  auto output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  distance_transform_l2(input, output);
  return output;
}

} // namespace ksj::image

namespace ksj::image {

template <typename T>
void dilate_cross_value(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T value) {
  detail::eigen::dilate_cross_value(input, output, value);
}

template <typename T>
void dilate_cross_value(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const T value) {
  dilate_cross_value(input.view(), output.view(), value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate_cross_value(ksj::array::ImageView<const T> input, const T value) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  dilate_cross_value(input, output.view(), value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate_cross_value(const ksj::array::PooledImage<T>& input, const T value) {
  return dilate_cross_value(ksj::array::as_const_view(input.view()), value);
}

template <typename T>
void erode_cross_value(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T value,
                       const T background = T{}) {
  detail::eigen::erode_cross_value(input, output, value, background);
}

template <typename T>
void erode_cross_value(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const T value,
                       const T background = T{}) {
  erode_cross_value(input.view(), output.view(), value, background);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode_cross_value(ksj::array::ImageView<const T> input, const T value,
                                                           const T background = T{}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  erode_cross_value(input, output.view(), value, background);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode_cross_value(const ksj::array::PooledImage<T>& input, const T value,
                                                           const T background = T{}) {
  return erode_cross_value(ksj::array::as_const_view(input.view()), value, background);
}

template <typename T>
void dilate_threshold_cross(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                            const T center_threshold, const T neighbor_threshold, const T value) {
  detail::eigen::dilate_threshold_cross(input, output, center_threshold, neighbor_threshold, value);
}

template <typename T>
void dilate_threshold_cross(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                            const T center_threshold, const T neighbor_threshold, const T value) {
  dilate_threshold_cross(input.view(), output.view(), center_threshold, neighbor_threshold, value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate_threshold_cross(ksj::array::ImageView<const T> input,
                                                                const T center_threshold, const T neighbor_threshold,
                                                                const T value) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  dilate_threshold_cross(input, output.view(), center_threshold, neighbor_threshold, value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate_threshold_cross(const ksj::array::PooledImage<T>& input,
                                                                const T center_threshold, const T neighbor_threshold,
                                                                const T value) {
  return dilate_threshold_cross(ksj::array::as_const_view(input.view()), center_threshold, neighbor_threshold, value);
}

template <typename T>
void erode_threshold_cross(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                           const T center_threshold, const T neighbor_threshold, const T value) {
  detail::eigen::erode_threshold_cross(input, output, center_threshold, neighbor_threshold, value);
}

template <typename T>
void erode_threshold_cross(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                           const T center_threshold, const T neighbor_threshold, const T value) {
  erode_threshold_cross(input.view(), output.view(), center_threshold, neighbor_threshold, value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode_threshold_cross(ksj::array::ImageView<const T> input,
                                                               const T center_threshold, const T neighbor_threshold,
                                                               const T value) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  erode_threshold_cross(input, output.view(), center_threshold, neighbor_threshold, value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode_threshold_cross(const ksj::array::PooledImage<T>& input,
                                                               const T center_threshold, const T neighbor_threshold,
                                                               const T value) {
  return erode_threshold_cross(ksj::array::as_const_view(input.view()), center_threshold, neighbor_threshold, value);
}

[[nodiscard]] inline ksj::array::PooledImage<int> disk_structuring_element(const std::size_t radius) {
  return detail::eigen::disk_structuring_element(radius);
}

template <typename T>
void dilate_mask(const ksj::array::ImageView<const T> input, const ksj::array::ImageView<const int> kernel,
                 ksj::array::ImageView<T> output, const T value = T{1}) {
  detail::eigen::dilate_mask(input, kernel, output, value);
}

template <typename T>
void dilate_mask(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<int>& kernel,
                 ksj::array::PooledImage<T>& output, const T value = T{1}) {
  dilate_mask(input.view(), kernel.view(), output.view(), value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate_mask(ksj::array::ImageView<const T> input,
                                                     ksj::array::ImageView<const int> kernel, const T value = T{1}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  dilate_mask(input, kernel, output.view(), value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate_mask(const ksj::array::PooledImage<T>& input,
                                                     const ksj::array::PooledImage<int>& kernel, const T value = T{1}) {
  return dilate_mask(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), value);
}

template <typename T>
void dilate_mask_in_place(ksj::array::ImageView<T> image, const ksj::array::ImageView<const int> kernel,
                          ksj::array::PooledImage<T>& scratch, const T value = T{1}) {
  scratch.resize(image.rows(), image.cols());
  ksj::array::copy(ksj::array::as_const_view(image), scratch.view());
  dilate_mask(ksj::array::as_const_view(scratch.view()), kernel, image, value);
}

template <typename T>
void dilate_mask_in_place(ksj::array::PooledImage<T>& image, const ksj::array::PooledImage<int>& kernel,
                          ksj::array::PooledImage<T>& scratch, const T value = T{1}) {
  dilate_mask_in_place(image.view(), kernel.view(), scratch, value);
}

template <typename T>
void dilate_mask_in_place(ksj::array::ImageView<T> image, const ksj::array::ImageView<const int> kernel,
                          const T value = T{1}) {
  auto scratch = ksj::array::make_pooled_image<T>(image.rows(), image.cols());
  dilate_mask_in_place(image, kernel, scratch, value);
}

template <typename T>
void dilate_mask_in_place(ksj::array::PooledImage<T>& image, const ksj::array::PooledImage<int>& kernel,
                          const T value = T{1}) {
  dilate_mask_in_place(image.view(), kernel.view(), value);
}

template <typename T>
void erode_mask(const ksj::array::ImageView<const T> input, const ksj::array::ImageView<const int> kernel,
                ksj::array::ImageView<T> output, const T background = T{}) {
  detail::eigen::erode_mask(input, kernel, output, background);
}

template <typename T>
void erode_mask(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<int>& kernel,
                ksj::array::PooledImage<T>& output, const T background = T{}) {
  erode_mask(input.view(), kernel.view(), output.view(), background);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode_mask(ksj::array::ImageView<const T> input,
                                                    ksj::array::ImageView<const int> kernel, const T background = T{}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  erode_mask(input, kernel, output.view(), background);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode_mask(const ksj::array::PooledImage<T>& input,
                                                    const ksj::array::PooledImage<int>& kernel,
                                                    const T background = T{}) {
  return erode_mask(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), background);
}

template <typename T>
void erode_mask_in_place(ksj::array::ImageView<T> image, const ksj::array::ImageView<const int> kernel,
                         ksj::array::PooledImage<T>& scratch, const T background = T{}) {
  scratch.resize(image.rows(), image.cols());
  ksj::array::copy(ksj::array::as_const_view(image), scratch.view());
  erode_mask(ksj::array::as_const_view(scratch.view()), kernel, image, background);
}

template <typename T>
void erode_mask_in_place(ksj::array::PooledImage<T>& image, const ksj::array::PooledImage<int>& kernel,
                         ksj::array::PooledImage<T>& scratch, const T background = T{}) {
  erode_mask_in_place(image.view(), kernel.view(), scratch, background);
}

template <typename T>
void erode_mask_in_place(ksj::array::ImageView<T> image, const ksj::array::ImageView<const int> kernel,
                         const T background = T{}) {
  auto scratch = ksj::array::make_pooled_image<T>(image.rows(), image.cols());
  erode_mask_in_place(image, kernel, scratch, background);
}

template <typename T>
void erode_mask_in_place(ksj::array::PooledImage<T>& image, const ksj::array::PooledImage<int>& kernel,
                         const T background = T{}) {
  erode_mask_in_place(image.view(), kernel.view(), background);
}

inline void keep_largest_component(ksj::array::ImageView<int> mask,
                                   const Connectivity connectivity = Connectivity::four) {
  detail::eigen::keep_largest_component(mask, connectivity);
}

inline void keep_largest_component(ksj::array::PooledImage<int>& mask,
                                   const Connectivity connectivity = Connectivity::four) {
  keep_largest_component(mask.view(), connectivity);
}

} // namespace ksj::image
