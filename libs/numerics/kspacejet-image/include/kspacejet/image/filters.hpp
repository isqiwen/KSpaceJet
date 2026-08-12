#pragma once

/// Spatial image filtering kernels, separable filters, and border-treatment options.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_filters.hpp"
#include "kspacejet/image/detail/image_policy.hpp"
#include "kspacejet/image/detail/intel/intel_image_filters.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_filters.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <utility>

namespace ksj::image {

template <typename T>
void filter2d(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
              ksj::array::PooledImage<T>& output, const BorderMode border_mode = BorderMode::constant,
              const FilterAnchor anchor = FilterAnchor::center) {
  if (detail::eigen::aliases_image_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    detail::eigen::filter2d(input, kernel, temp, border_mode, anchor);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  detail::eigen::filter2d(input, kernel, output, border_mode, anchor);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
filter2d(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
         const BorderMode border_mode = BorderMode::constant, const FilterAnchor anchor = FilterAnchor::center) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  filter2d(input, kernel, output, border_mode, anchor);
  return output;
}

template <typename T, typename KernelT>
void filter3d_replicate(ksj::array::CubeView<const T> input, ksj::array::CubeView<KernelT> kernel,
                        ksj::array::CubeView<T> output) {
  ksj::array::copy(input, output);
  detail::eigen::filter3d_replicate(output, kernel);
}

template <typename T, typename KernelT>
void filter3d_replicate(ksj::array::CubeView<T> input, ksj::array::CubeView<KernelT> kernel,
                        ksj::array::CubeView<T> output) {
  filter3d_replicate(ksj::array::as_const_view(input), kernel, output);
}

template <typename T, typename KernelT>
void filter3d_replicate(const ksj::array::PooledCube<T>& input, const ksj::array::PooledCube<KernelT>& kernel,
                        ksj::array::PooledCube<T>& output) {
  filter3d_replicate(input.view(), kernel.view(), output.view());
}

template <typename T, typename KernelT>
[[nodiscard]] ksj::array::PooledCube<T> filter3d_replicate(ksj::array::CubeView<const T> input,
                                                           ksj::array::CubeView<KernelT> kernel) {
  auto output = ksj::array::make_pooled_cube<T>(input.dim0(), input.dim1(), input.dim2());
  filter3d_replicate(input, kernel, output.view());
  return output;
}

template <typename T, typename KernelT>
[[nodiscard]] ksj::array::PooledCube<T> filter3d_replicate(ksj::array::CubeView<T> input,
                                                           ksj::array::CubeView<KernelT> kernel) {
  return filter3d_replicate(ksj::array::as_const_view(input), kernel);
}

template <typename T, typename KernelT>
[[nodiscard]] ksj::array::PooledCube<T> filter3d_replicate(const ksj::array::PooledCube<T>& input,
                                                           const ksj::array::PooledCube<KernelT>& kernel) {
  auto output = ksj::array::make_pooled_cube<T>(input.dim0(), input.dim1(), input.dim2());
  filter3d_replicate(input, kernel, output);
  return output;
}

template <typename T, typename KernelT>
void filter3d_replicate_in_place(ksj::array::CubeView<T> input_output, ksj::array::CubeView<KernelT> kernel) {
  detail::eigen::filter3d_replicate(input_output, kernel);
}

template <typename T, typename KernelT>
void filter3d_replicate_in_place(ksj::array::PooledCube<T>& input_output,
                                 const ksj::array::PooledCube<KernelT>& kernel) {
  filter3d_replicate_in_place(input_output.view(), kernel.view());
}

template <typename T, typename RowSelector, typename ColSelector>
void filter2d_region(const ksj::array::ImageView<const T> input, const ksj::array::ImageView<const T> kernel,
                     ksj::array::ImageView<T> output, RowSelector&& rows, ColSelector&& cols,
                     const BorderMode border_mode = BorderMode::constant,
                     const FilterAnchor anchor = FilterAnchor::center)
  requires(ksj::array::detail::view_selector_v<RowSelector> && ksj::array::detail::view_selector_v<ColSelector> &&
           !ksj::array::detail::fixed_selector_v<RowSelector> && !ksj::array::detail::fixed_selector_v<ColSelector>)
{
  const auto row_selection = ksj::array::detail::normalize_view_selector(std::forward<RowSelector>(rows), input.rows(),
                                                                         "filter2d_region row region is outside input");
  const auto col_selection = ksj::array::detail::normalize_view_selector(
    std::forward<ColSelector>(cols), input.cols(), "filter2d_region column region is outside input");
  if (detail::intel::filter2d_region(input, kernel, output, row_selection, col_selection, anchor)) {
    return;
  }

  detail::eigen::filter2d_region(input, kernel, output, row_selection, col_selection, border_mode, anchor);
}

template <typename T, typename RowSelector, typename ColSelector>
void filter2d_region(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
                     ksj::array::PooledImage<T>& output, RowSelector&& rows, ColSelector&& cols,
                     const BorderMode border_mode = BorderMode::constant,
                     const FilterAnchor anchor = FilterAnchor::center)
  requires(ksj::array::detail::view_selector_v<RowSelector> && ksj::array::detail::view_selector_v<ColSelector> &&
           !ksj::array::detail::fixed_selector_v<RowSelector> && !ksj::array::detail::fixed_selector_v<ColSelector>)
{
  filter2d_region(input.view(), kernel.view(), output.view(), std::forward<RowSelector>(rows),
                  std::forward<ColSelector>(cols), border_mode, anchor);
}

} // namespace ksj::image

namespace ksj::image {

template <typename T>
void box_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_rows,
                const std::size_t kernel_cols, const BorderMode border_mode) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    box_filter(input, temp.view(), kernel_rows, kernel_cols, border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_box_filter<T>(input.size()) &&
      detail::intel::box_filter(input, output, kernel_rows, kernel_cols, border_mode)) {
    return;
  }

  if (detail::prefer_opencv_box_filter<T>(input.size()) &&
      detail::opencv::box_filter(input, output, kernel_rows, kernel_cols, border_mode)) {
    return;
  }

  detail::eigen::box_filter(input, output, kernel_rows, kernel_cols, border_mode);
}

template <typename T>
void box_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_rows,
                const std::size_t kernel_cols, const BorderMode border_mode) {
  box_filter(ksj::array::as_const_view(input), output, kernel_rows, kernel_cols, border_mode);
}

template <typename T>
void box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode) {
  box_filter(input.view(), output.view(), kernel_rows, kernel_cols, border_mode);
}

template <typename T>
void box_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate) {
  box_filter(input, output, kernel_size, kernel_size, border_mode);
}

template <typename T>
void box_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
                const BorderMode border_mode = BorderMode::replicate) {
  box_filter(ksj::array::as_const_view(input), output, kernel_size, kernel_size, border_mode);
}

template <typename T>
void box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate) {
  box_filter(input.view(), output.view(), kernel_size, kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(ksj::array::ImageView<const T> input, const std::size_t kernel_rows,
                                                    const std::size_t kernel_cols, const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  box_filter(input, output.view(), kernel_rows, kernel_cols, border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(ksj::array::ImageView<T> input, const std::size_t kernel_rows,
                                                    const std::size_t kernel_cols, const BorderMode border_mode) {
  return box_filter(ksj::array::as_const_view(input), kernel_rows, kernel_cols, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(const ksj::array::PooledImage<T>& input,
                                                    const std::size_t kernel_rows, const std::size_t kernel_cols,
                                                    const BorderMode border_mode) {
  return box_filter(input.view(), kernel_rows, kernel_cols, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(ksj::array::ImageView<const T> input,
                                                    const std::size_t kernel_size = 3,
                                                    const BorderMode border_mode = BorderMode::replicate) {
  return box_filter(input, kernel_size, kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(ksj::array::ImageView<T> input, const std::size_t kernel_size = 3,
                                                    const BorderMode border_mode = BorderMode::replicate) {
  return box_filter(ksj::array::as_const_view(input), kernel_size, kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(const ksj::array::PooledImage<T>& input,
                                                    const std::size_t kernel_size = 3,
                                                    const BorderMode border_mode = BorderMode::replicate) {
  return box_filter(input.view(), kernel_size, kernel_size, border_mode);
}

template <typename T>
void gaussian_blur(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                   const std::size_t kernel_size = 5, const double sigma = 1.0,
                   const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    gaussian_blur(input, temp.view(), kernel_size, sigma, border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_gaussian_blur<T>(input.size()) &&
      detail::intel::gaussian_blur(input, output, kernel_size, sigma, border_mode)) {
    return;
  }

  if (detail::prefer_opencv_gaussian_blur<T>(input.size()) &&
      detail::opencv::gaussian_blur(input, output, kernel_size, sigma, border_mode)) {
    return;
  }

  detail::eigen::gaussian_blur(input, output, kernel_size, sigma, border_mode);
}

template <typename T>
void gaussian_blur(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 5,
                   const double sigma = 1.0, const BorderMode border_mode = BorderMode::replicate) {
  gaussian_blur(ksj::array::as_const_view(input), output, kernel_size, sigma, border_mode);
}

template <typename T>
void gaussian_blur(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                   const std::size_t kernel_size = 5, const double sigma = 1.0,
                   const BorderMode border_mode = BorderMode::replicate) {
  gaussian_blur(input.view(), output.view(), kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gaussian_blur(ksj::array::ImageView<const T> input,
                                                       const std::size_t kernel_size = 5, const double sigma = 1.0,
                                                       const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  gaussian_blur(input, output.view(), kernel_size, sigma, border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gaussian_blur(ksj::array::ImageView<T> input,
                                                       const std::size_t kernel_size = 5, const double sigma = 1.0,
                                                       const BorderMode border_mode = BorderMode::replicate) {
  return gaussian_blur(ksj::array::as_const_view(input), kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gaussian_blur(const ksj::array::PooledImage<T>& input,
                                                       const std::size_t kernel_size = 5, const double sigma = 1.0,
                                                       const BorderMode border_mode = BorderMode::replicate) {
  return gaussian_blur(input.view(), kernel_size, sigma, border_mode);
}

template <typename T>
void bilateral_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                      const std::size_t diameter = 5, const double sigma_color = 0.1, const double sigma_space = 1.5,
                      const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    bilateral_filter(input, temp.view(), diameter, sigma_color, sigma_space, border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_bilateral_filter<T>(input.size()) &&
      detail::opencv::bilateral_filter(input, output, diameter, sigma_color, sigma_space, border_mode)) {
    return;
  }

  detail::eigen::bilateral_filter(input, output, diameter, sigma_color, sigma_space, border_mode);
}

template <typename T>
void bilateral_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t diameter = 5,
                      const double sigma_color = 0.1, const double sigma_space = 1.5,
                      const BorderMode border_mode = BorderMode::replicate) {
  bilateral_filter(ksj::array::as_const_view(input), output, diameter, sigma_color, sigma_space, border_mode);
}

template <typename T>
void bilateral_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                      const std::size_t diameter = 5, const double sigma_color = 0.1, const double sigma_space = 1.5,
                      const BorderMode border_mode = BorderMode::replicate) {
  bilateral_filter(input.view(), output.view(), diameter, sigma_color, sigma_space, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
bilateral_filter(ksj::array::ImageView<const T> input, const std::size_t diameter = 5, const double sigma_color = 0.1,
                 const double sigma_space = 1.5, const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  bilateral_filter(input, output.view(), diameter, sigma_color, sigma_space, border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
bilateral_filter(ksj::array::ImageView<T> input, const std::size_t diameter = 5, const double sigma_color = 0.1,
                 const double sigma_space = 1.5, const BorderMode border_mode = BorderMode::replicate) {
  return bilateral_filter(ksj::array::as_const_view(input), diameter, sigma_color, sigma_space, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
bilateral_filter(const ksj::array::PooledImage<T>& input, const std::size_t diameter = 5,
                 const double sigma_color = 0.1, const double sigma_space = 1.5,
                 const BorderMode border_mode = BorderMode::replicate) {
  return bilateral_filter(input.view(), diameter, sigma_color, sigma_space, border_mode);
}

template <typename T>
void median_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                   const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    median_filter(input, temp.view(), kernel_size, border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_median_filter<T>(input.size()) &&
      detail::intel::median_filter(input, output, kernel_size, border_mode)) {
    return;
  }

  if (detail::prefer_opencv_median_filter<T>(input.size()) &&
      detail::opencv::median_filter(input, output, kernel_size, border_mode)) {
    return;
  }

  detail::eigen::median_filter(input, output, kernel_size, border_mode);
}

template <typename T>
void median_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size = 3,
                   const BorderMode border_mode = BorderMode::replicate) {
  median_filter(ksj::array::as_const_view(input), output, kernel_size, border_mode);
}

template <typename T>
void median_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                   const std::size_t kernel_size = 3, const BorderMode border_mode = BorderMode::replicate) {
  median_filter(input.view(), output.view(), kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> median_filter(ksj::array::ImageView<const T> input,
                                                       const std::size_t kernel_size = 3,
                                                       const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  median_filter(input, output.view(), kernel_size, border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> median_filter(ksj::array::ImageView<T> input,
                                                       const std::size_t kernel_size = 3,
                                                       const BorderMode border_mode = BorderMode::replicate) {
  return median_filter(ksj::array::as_const_view(input), kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> median_filter(const ksj::array::PooledImage<T>& input,
                                                       const std::size_t kernel_size = 3,
                                                       const BorderMode border_mode = BorderMode::replicate) {
  return median_filter(input.view(), kernel_size, border_mode);
}

inline void median3x3_interior_zero(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output) {
  detail::eigen::median3x3_interior_zero(input, output);
}

inline void median3x3_interior_zero(const ksj::array::PooledImage<float>& input,
                                    ksj::array::PooledImage<float>& output) {
  median3x3_interior_zero(input.view(), output.view());
}

[[nodiscard]] inline ksj::array::PooledImage<float> median3x3_interior_zero(ksj::array::ImageView<const float> input) {
  auto output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  median3x3_interior_zero(input, output.view());
  return output;
}

[[nodiscard]] inline ksj::array::PooledImage<float>
median3x3_interior_zero(const ksj::array::PooledImage<float>& input) {
  return median3x3_interior_zero(ksj::array::as_const_view(input.view()));
}

inline void phase_quality_map3x3(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 const float threshold = 0.070F) {
  detail::eigen::phase_quality_map3x3(input, output, threshold);
}

inline void phase_quality_map3x3(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output,
                                 const float threshold = 0.070F) {
  phase_quality_map3x3(input.view(), output.view(), threshold);
}

[[nodiscard]] inline ksj::array::PooledImage<float> phase_quality_map3x3(ksj::array::ImageView<const float> input,
                                                                         const float threshold = 0.070F) {
  auto output = ksj::array::make_pooled_image<float>(input.rows(), input.cols());
  phase_quality_map3x3(input, output.view(), threshold);
  return output;
}

[[nodiscard]] inline ksj::array::PooledImage<float> phase_quality_map3x3(const ksj::array::PooledImage<float>& input,
                                                                         const float threshold = 0.070F) {
  return phase_quality_map3x3(ksj::array::as_const_view(input.view()), threshold);
}

template <typename T>
void sobel_x(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
             const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    sobel_x(input, temp.view(), border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_sobel_x<T>(input.size()) && detail::intel::sobel_x(input, output, border_mode)) {
    return;
  }

  if (detail::prefer_opencv_sobel_x<T>(input.size()) && detail::opencv::sobel_x(input, output, border_mode)) {
    return;
  }

  detail::eigen::sobel_x(input, output, border_mode);
}

template <typename T>
void sobel_x(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output,
             const BorderMode border_mode = BorderMode::replicate) {
  sobel_x(ksj::array::as_const_view(input), output, border_mode);
}

template <typename T>
void sobel_x(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
             const BorderMode border_mode = BorderMode::replicate) {
  sobel_x(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_x(ksj::array::ImageView<const T> input,
                                                 const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  sobel_x(input, output.view(), border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_x(ksj::array::ImageView<T> input,
                                                 const BorderMode border_mode = BorderMode::replicate) {
  return sobel_x(ksj::array::as_const_view(input), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_x(const ksj::array::PooledImage<T>& input,
                                                 const BorderMode border_mode = BorderMode::replicate) {
  return sobel_x(input.view(), border_mode);
}

template <typename T>
void sobel_y(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
             const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    sobel_y(input, temp.view(), border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_intel_sobel_y<T>(input.size()) && detail::intel::sobel_y(input, output, border_mode)) {
    return;
  }

  if (detail::prefer_opencv_sobel_y<T>(input.size()) && detail::opencv::sobel_y(input, output, border_mode)) {
    return;
  }

  detail::eigen::sobel_y(input, output, border_mode);
}

template <typename T>
void sobel_y(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output,
             const BorderMode border_mode = BorderMode::replicate) {
  sobel_y(ksj::array::as_const_view(input), output, border_mode);
}

template <typename T>
void sobel_y(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
             const BorderMode border_mode = BorderMode::replicate) {
  sobel_y(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_y(ksj::array::ImageView<const T> input,
                                                 const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  sobel_y(input, output.view(), border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_y(ksj::array::ImageView<T> input,
                                                 const BorderMode border_mode = BorderMode::replicate) {
  return sobel_y(ksj::array::as_const_view(input), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_y(const ksj::array::PooledImage<T>& input,
                                                 const BorderMode border_mode = BorderMode::replicate) {
  return sobel_y(input.view(), border_mode);
}

template <typename T>
void gradient_magnitude(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                        const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    gradient_magnitude(input, temp.view(), border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (detail::prefer_opencv_gradient_magnitude<T>(input.size()) &&
      detail::opencv::gradient_magnitude(input, output, border_mode)) {
    return;
  }

  detail::eigen::gradient_magnitude(input, output, border_mode);
}

template <typename T>
void gradient_magnitude(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output,
                        const BorderMode border_mode = BorderMode::replicate) {
  gradient_magnitude(ksj::array::as_const_view(input), output, border_mode);
}

template <typename T>
void gradient_magnitude(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                        const BorderMode border_mode = BorderMode::replicate) {
  gradient_magnitude(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gradient_magnitude(ksj::array::ImageView<const T> input,
                                                            const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  gradient_magnitude(input, output.view(), border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gradient_magnitude(ksj::array::ImageView<T> input,
                                                            const BorderMode border_mode = BorderMode::replicate) {
  return gradient_magnitude(ksj::array::as_const_view(input), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gradient_magnitude(const ksj::array::PooledImage<T>& input,
                                                            const BorderMode border_mode = BorderMode::replicate) {
  return gradient_magnitude(input.view(), border_mode);
}

template <typename T>
void laplacian(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
               const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    laplacian(input, temp.view(), border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  detail::eigen::laplacian(input, output, border_mode);
}

template <typename T>
void laplacian(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output,
               const BorderMode border_mode = BorderMode::replicate) {
  laplacian(ksj::array::as_const_view(input), output, border_mode);
}

template <typename T>
void laplacian(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
               const BorderMode border_mode = BorderMode::replicate) {
  laplacian(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> laplacian(ksj::array::ImageView<const T> input,
                                                   const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  laplacian(input, output.view(), border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> laplacian(ksj::array::ImageView<T> input,
                                                   const BorderMode border_mode = BorderMode::replicate) {
  return laplacian(ksj::array::as_const_view(input), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> laplacian(const ksj::array::PooledImage<T>& input,
                                                   const BorderMode border_mode = BorderMode::replicate) {
  return laplacian(input.view(), border_mode);
}

template <typename T>
void unsharp_mask(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const double amount = 1.0,
                  const std::size_t kernel_size = 5, const double sigma = 1.0,
                  const BorderMode border_mode = BorderMode::replicate) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    unsharp_mask(input, temp.view(), amount, kernel_size, sigma, border_mode);
    ksj::array::copy(temp.view(), output);
    return;
  }

  detail::eigen::unsharp_mask(input, output, amount, kernel_size, sigma, border_mode);
}

template <typename T>
void unsharp_mask(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const double amount = 1.0,
                  const std::size_t kernel_size = 5, const double sigma = 1.0,
                  const BorderMode border_mode = BorderMode::replicate) {
  unsharp_mask(ksj::array::as_const_view(input), output, amount, kernel_size, sigma, border_mode);
}

template <typename T>
void unsharp_mask(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                  const double amount = 1.0, const std::size_t kernel_size = 5, const double sigma = 1.0,
                  const BorderMode border_mode = BorderMode::replicate) {
  unsharp_mask(input.view(), output.view(), amount, kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unsharp_mask(ksj::array::ImageView<const T> input, const double amount = 1.0,
                                                      const std::size_t kernel_size = 5, const double sigma = 1.0,
                                                      const BorderMode border_mode = BorderMode::replicate) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  unsharp_mask(input, output.view(), amount, kernel_size, sigma, border_mode);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unsharp_mask(ksj::array::ImageView<T> input, const double amount = 1.0,
                                                      const std::size_t kernel_size = 5, const double sigma = 1.0,
                                                      const BorderMode border_mode = BorderMode::replicate) {
  return unsharp_mask(ksj::array::as_const_view(input), amount, kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
unsharp_mask(const ksj::array::PooledImage<T>& input, const double amount = 1.0, const std::size_t kernel_size = 5,
             const double sigma = 1.0, const BorderMode border_mode = BorderMode::replicate) {
  return unsharp_mask(input.view(), amount, kernel_size, sigma, border_mode);
}

} // namespace ksj::image
