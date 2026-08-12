#include "kspacejet/image/detail/opencv/opencv_image_thresholds.hpp"
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
[[nodiscard]] bool threshold(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                             const T threshold_value, const T low_value, const T high_value) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (!valid_image_pair(input, output)) {
      return false;
    }

    const auto src = as_opencv(input);
    auto dst = as_opencv(output);
    auto mask_buffer = ksj::array::make_pooled_image<unsigned char>(input.rows(), input.cols());
    auto mask = as_opencv(mask_buffer.view());
    cv::compare(src, cv::Scalar(static_cast<double>(threshold_value)), mask, cv::CMP_GE);
    dst.setTo(cv::Scalar(static_cast<double>(low_value)));
    dst.setTo(cv::Scalar(static_cast<double>(high_value)), mask);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool normalize_minmax(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (!valid_image_pair(input, output)) {
      return false;
    }
    cv::normalize(as_opencv(input), as_opencv(output), 0.0, 1.0, cv::NORM_MINMAX);
    return true;
  } else {
    return false;
  }
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool threshold(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
               const float threshold_value, const float low_value, const float high_value) {
  return detail::opencv_impl::threshold(input, output, threshold_value, low_value, high_value);
}

bool threshold(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
               const double threshold_value, const double low_value, const double high_value) {
  return detail::opencv_impl::threshold(input, output, threshold_value, low_value, high_value);
}

bool normalize_minmax(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output) {
  return detail::opencv_impl::normalize_minmax(input, output);
}

bool normalize_minmax(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output) {
  return detail::opencv_impl::normalize_minmax(input, output);
}
} // namespace detail::opencv
} // namespace ksj::image
