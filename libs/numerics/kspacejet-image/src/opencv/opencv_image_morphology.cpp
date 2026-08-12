#include "kspacejet/image/detail/opencv/opencv_image_morphology.hpp"
#include "kspacejet/base/types.hpp"
#include "opencv_image_common.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::image::detail::opencv_impl {

template <typename T>
[[nodiscard]] bool dilate(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                          const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                          const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (kernel_rows == 0 || kernel_cols == 0 || !valid_image_pair(input, output) ||
        !fits_cv_size(kernel_rows, kernel_cols)) {
      return false;
    }
    const auto kernel = cv::getStructuringElement(
      structuring_element_shape(shape), cv::Size(static_cast<int>(kernel_cols), static_cast<int>(kernel_rows)));
    cv::dilate(as_opencv(input), as_opencv(output), kernel, cv::Point(-1, -1), 1, border_type(border_mode),
               cv::Scalar(0.0));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool dilate(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                          const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                          const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return dilate(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] bool erode(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                         const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                         const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (kernel_rows == 0 || kernel_cols == 0 || !valid_image_pair(input, output) ||
        !fits_cv_size(kernel_rows, kernel_cols)) {
      return false;
    }
    const auto kernel = cv::getStructuringElement(
      structuring_element_shape(shape), cv::Size(static_cast<int>(kernel_cols), static_cast<int>(kernel_rows)));
    cv::erode(as_opencv(input), as_opencv(output), kernel, cv::Point(-1, -1), 1, border_type(border_mode),
              cv::Scalar(0.0));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool erode(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                         const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                         const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return erode(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] bool morph_open(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode,
                              const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (kernel_rows == 0 || kernel_cols == 0 || !valid_image_pair(input, output) ||
        !fits_cv_size(kernel_rows, kernel_cols)) {
      return false;
    }
    const auto kernel = cv::getStructuringElement(
      structuring_element_shape(shape), cv::Size(static_cast<int>(kernel_cols), static_cast<int>(kernel_rows)));
    cv::morphologyEx(as_opencv(input), as_opencv(output), cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1,
                     border_type(border_mode), cv::Scalar(0.0));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool morph_open(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode,
                              const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_open(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] bool morph_close(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                               const std::size_t kernel_rows, const std::size_t kernel_cols,
                               const BorderMode border_mode,
                               const StructuringElementShape shape = StructuringElementShape::rectangle) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (kernel_rows == 0 || kernel_cols == 0 || !valid_image_pair(input, output) ||
        !fits_cv_size(kernel_rows, kernel_cols)) {
      return false;
    }
    const auto kernel = cv::getStructuringElement(
      structuring_element_shape(shape), cv::Size(static_cast<int>(kernel_cols), static_cast<int>(kernel_rows)));
    cv::morphologyEx(as_opencv(input), as_opencv(output), cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 1,
                     border_type(border_mode), cv::Scalar(0.0));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool morph_close(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                               const std::size_t kernel_rows, const std::size_t kernel_cols,
                               const BorderMode border_mode,
                               const StructuringElementShape shape = StructuringElementShape::rectangle) {
  return morph_close(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

[[nodiscard]] inline bool distance_transform_l2(ksj::array::ImageView<const std::uint8_t> input,
                                                ksj::array::ImageView<float> output) {
  if (input.rows() != output.rows() || input.cols() != output.cols() || !fits_cv_size(input.rows(), input.cols())) {
    return false;
  }
  if (input.empty()) {
    return true;
  }

  cv::distanceTransform(as_opencv(input), as_opencv(output), cv::DIST_L2, 3);
  return true;
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool dilate(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
            const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
            const StructuringElementShape shape) {
  return detail::opencv_impl::dilate(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool dilate(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
            const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
            const StructuringElementShape shape) {
  return detail::opencv_impl::dilate(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool erode(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output, const std::size_t kernel_rows,
           const std::size_t kernel_cols, const BorderMode border_mode, const StructuringElementShape shape) {
  return detail::opencv_impl::erode(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool erode(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
           const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
           const StructuringElementShape shape) {
  return detail::opencv_impl::erode(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool morph_open(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                const StructuringElementShape shape) {
  return detail::opencv_impl::morph_open(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool morph_open(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                const StructuringElementShape shape) {
  return detail::opencv_impl::morph_open(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool morph_close(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                 const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                 const StructuringElementShape shape) {
  return detail::opencv_impl::morph_close(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool morph_close(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                 const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                 const StructuringElementShape shape) {
  return detail::opencv_impl::morph_close(input, output, kernel_rows, kernel_cols, border_mode, shape);
}

bool distance_transform_l2(ksj::array::ImageView<const std::uint8_t> input, ksj::array::ImageView<float> output) {
  return detail::opencv_impl::distance_transform_l2(input, output);
}
} // namespace detail::opencv
} // namespace ksj::image
