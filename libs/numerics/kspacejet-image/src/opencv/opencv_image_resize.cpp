#include "kspacejet/image/detail/opencv/opencv_image_resize.hpp"
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
[[nodiscard]] bool resize_remap(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                const int interpolation) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (output.empty()) {
      return true;
    }
    if (input.empty() || !fits_cv_size(input.rows(), input.cols()) || !fits_cv_size(output.rows(), output.cols())) {
      return false;
    }
    if (output.rows() == 1U && output.cols() == 1U) {
      output(0, 0) = input(0, 0);
      return true;
    }

    auto map_x_buffer = ksj::array::make_pooled_image<float>(output.rows(), output.cols());
    auto map_y_buffer = ksj::array::make_pooled_image<float>(output.rows(), output.cols());
    auto map_x = as_opencv(map_x_buffer.view());
    auto map_y = as_opencv(map_y_buffer.view());
    const float row_scale =
      output.rows() > 1 ? static_cast<float>(input.rows() - 1U) / static_cast<float>(output.rows() - 1U) : 0.0F;
    const float col_scale =
      output.cols() > 1 ? static_cast<float>(input.cols() - 1U) / static_cast<float>(output.cols() - 1U) : 0.0F;

    for (std::size_t row = 0; row < output.rows(); ++row) {
      auto* map_x_row = map_x.template ptr<float>(static_cast<int>(row));
      auto* map_y_row = map_y.template ptr<float>(static_cast<int>(row));
      for (std::size_t col = 0; col < output.cols(); ++col) {
        map_x_row[col] = static_cast<float>(col) * col_scale;
        map_y_row[col] = static_cast<float>(row) * row_scale;
      }
    }

    cv::remap(as_opencv(input.view()), as_opencv(output.view()), map_x, map_y, interpolation, cv::BORDER_REPLICATE);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  return resize_remap(input, output, cv::INTER_NEAREST);
}

template <typename T>
[[nodiscard]] bool resize_linear(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  return resize_remap(input, output, cv::INTER_LINEAR);
}

template <typename T>
[[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  return resize_remap(input, output, cv::INTER_CUBIC);
}

template <typename T>
[[nodiscard]] bool resize_lanczos4(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  return resize_remap(input, output, cv::INTER_LANCZOS4);
}

template <typename T>
[[nodiscard]] bool resize_area(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (output.empty()) {
      return true;
    }
    if (input.empty() || !fits_cv_size(input.rows(), input.cols()) || !fits_cv_size(output.rows(), output.cols())) {
      return false;
    }
    cv::resize(as_opencv(input.view()), as_opencv(output.view()),
               cv::Size(static_cast<int>(output.cols()), static_cast<int>(output.rows())), 0.0, 0.0, cv::INTER_AREA);
    return true;
  } else {
    return false;
  }
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool resize_nearest(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return detail::opencv_impl::resize_nearest(input, output);
}

bool resize_nearest(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output) {
  return detail::opencv_impl::resize_nearest(input, output);
}

bool resize_linear(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return detail::opencv_impl::resize_linear(input, output);
}

bool resize_linear(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output) {
  return detail::opencv_impl::resize_linear(input, output);
}

bool resize_cubic(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return detail::opencv_impl::resize_cubic(input, output);
}

bool resize_cubic(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output) {
  return detail::opencv_impl::resize_cubic(input, output);
}

bool resize_area(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return detail::opencv_impl::resize_area(input, output);
}

bool resize_area(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output) {
  return detail::opencv_impl::resize_area(input, output);
}

bool resize_lanczos4(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output) {
  return detail::opencv_impl::resize_lanczos4(input, output);
}

bool resize_lanczos4(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output) {
  return detail::opencv_impl::resize_lanczos4(input, output);
}
} // namespace detail::opencv
} // namespace ksj::image
