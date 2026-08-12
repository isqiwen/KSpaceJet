#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

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

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/xphoto.hpp>

namespace ksj::image::detail::opencv_impl {

template <typename T>
inline constexpr bool opencv_image_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

template <typename T> [[nodiscard]] int cv_type() {
  using value_type = std::remove_const_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return CV_32FC1;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return CV_64FC1;
  } else if constexpr (std::is_same_v<value_type, unsigned char>) {
    return CV_8UC1;
  } else if constexpr (std::is_same_v<value_type, std::uint16_t>) {
    return CV_16UC1;
  } else if constexpr (std::is_same_v<value_type, std::int32_t>) {
    return CV_32SC1;
  } else {
    return -1;
  }
}

[[nodiscard]] inline int border_type(const BorderMode mode) noexcept {
  switch (mode) {
    case BorderMode::constant:
      return cv::BORDER_CONSTANT;
    case BorderMode::replicate:
      return cv::BORDER_REPLICATE;
    case BorderMode::reflect:
      return cv::BORDER_REFLECT;
    case BorderMode::reflect101:
      return cv::BORDER_REFLECT_101;
  }
  return cv::BORDER_REPLICATE;
}

[[nodiscard]] inline int structuring_element_shape(const StructuringElementShape shape) noexcept {
  switch (shape) {
    case StructuringElementShape::rectangle:
      return cv::MORPH_RECT;
    case StructuringElementShape::ellipse:
      return cv::MORPH_ELLIPSE;
  }
  return cv::MORPH_RECT;
}

[[nodiscard]] inline bool fits_cv_size(const std::size_t rows, const std::size_t cols) noexcept {
  return rows <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
         cols <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename T> [[nodiscard]] cv::Mat as_opencv(ksj::array::ImageView<T> image) {
  using value_type = std::remove_const_t<T>;
  return cv::Mat(static_cast<int>(image.rows()), static_cast<int>(image.cols()), cv_type<value_type>(),
                 const_cast<value_type*>(image.data()), image.row_stride_bytes());
}

[[nodiscard]] inline cv::Scalar as_opencv_scalar(const BgrColor color) noexcept {
  return {static_cast<double>(color.blue), static_cast<double>(color.green), static_cast<double>(color.red)};
}

template <typename T> [[nodiscard]] bool valid_bgr_image(ksj::array::CubeView<T> image) noexcept {
  return image.dim2() == 3U && image.dim2_stride() == 1U && image.dim1_stride() == 3U && !image.empty() &&
         fits_cv_size(image.dim0(), image.dim1());
}

template <typename T> [[nodiscard]] cv::Mat as_bgr_opencv(ksj::array::CubeView<T> image) {
  using value_type = std::remove_const_t<T>;
  return cv::Mat(static_cast<int>(image.dim0()), static_cast<int>(image.dim1()), CV_8UC3,
                 const_cast<value_type*>(image.data()), image.dim0_stride() * sizeof(value_type));
}

template <typename T>
[[nodiscard]] bool valid_image_pair(const ksj::array::PooledImage<T>& input,
                                    const ksj::array::PooledImage<T>& output) noexcept {
  return opencv_image_scalar_v<T> && input.rows() == output.rows() && input.cols() == output.cols() && !input.empty() &&
         fits_cv_size(input.rows(), input.cols());
}

template <typename T>
[[nodiscard]] bool valid_image_pair(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) noexcept {
  return opencv_image_scalar_v<T> && input.rows() == output.rows() && input.cols() == output.cols() && !input.empty() &&
         fits_cv_size(input.rows(), input.cols());
}
} // namespace ksj::image::detail::opencv_impl
