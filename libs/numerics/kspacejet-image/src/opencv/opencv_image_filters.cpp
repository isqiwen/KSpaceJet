#include "kspacejet/image/detail/opencv/opencv_image_filters.hpp"
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
[[nodiscard]] bool box_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (kernel_rows == 0 || kernel_cols == 0 || !valid_image_pair(input, output) ||
        !fits_cv_size(kernel_rows, kernel_cols)) {
      return false;
    }
    cv::blur(as_opencv(input), as_opencv(output),
             cv::Size(static_cast<int>(kernel_cols), static_cast<int>(kernel_rows)), cv::Point(-1, -1),
             border_type(border_mode));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                              const std::size_t kernel_rows, const std::size_t kernel_cols,
                              const BorderMode border_mode) {
  return box_filter(input.view(), output.view(), kernel_rows, kernel_cols, border_mode);
}

template <typename T>
[[nodiscard]] bool gaussian_blur(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                 const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (kernel_size == 0 || kernel_size % 2U == 0 || sigma <= 0.0 || !valid_image_pair(input, output) ||
        !fits_cv_size(kernel_size, kernel_size)) {
      return false;
    }
    cv::GaussianBlur(as_opencv(input), as_opencv(output),
                     cv::Size(static_cast<int>(kernel_size), static_cast<int>(kernel_size)), sigma, sigma,
                     border_type(border_mode));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool gaussian_blur(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                 const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  return ::ksj::image::detail::opencv_impl::gaussian_blur(input.view(), output.view(), kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] bool bilateral_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                    const std::size_t diameter, const double sigma_color, const double sigma_space,
                                    const BorderMode border_mode) {
  if constexpr (std::is_same_v<T, float>) {
    if (diameter == 0 || diameter % 2U == 0 || sigma_color <= 0.0 || sigma_space <= 0.0 ||
        !valid_image_pair(input, output) || !fits_cv_size(diameter, diameter)) {
      return false;
    }
    cv::bilateralFilter(as_opencv(input), as_opencv(output), static_cast<int>(diameter), sigma_color, sigma_space,
                        border_type(border_mode));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool bilateral_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                    const std::size_t diameter, const double sigma_color, const double sigma_space,
                                    const BorderMode border_mode) {
  return ::ksj::image::detail::opencv_impl::bilateral_filter(input.view(), output.view(), diameter, sigma_color,
                                                             sigma_space, border_mode);
}

template <typename T>
[[nodiscard]] bool median_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                 const std::size_t kernel_size, const BorderMode border_mode) {
  if constexpr (std::is_same_v<T, float>) {
    if (kernel_size == 0 || kernel_size % 2U == 0 || border_mode != BorderMode::replicate ||
        !valid_image_pair(input, output) || !fits_cv_size(kernel_size, kernel_size) || kernel_size > 5U) {
      return false;
    }
    if (kernel_size == 1U) {
      as_opencv(input).copyTo(as_opencv(output));
      return true;
    }
    cv::medianBlur(as_opencv(input), as_opencv(output), static_cast<int>(kernel_size));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool median_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                 const std::size_t kernel_size, const BorderMode border_mode) {
  return ::ksj::image::detail::opencv_impl::median_filter(input.view(), output.view(), kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] bool sobel_x(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                           const BorderMode border_mode) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (!valid_image_pair(input, output)) {
      return false;
    }
    cv::Sobel(as_opencv(input), as_opencv(output), cv_type<T>(), 1, 0, 3, 1.0, 0.0, border_type(border_mode));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool sobel_x(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                           const BorderMode border_mode) {
  return sobel_x(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] bool sobel_y(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                           const BorderMode border_mode) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (!valid_image_pair(input, output)) {
      return false;
    }
    cv::Sobel(as_opencv(input), as_opencv(output), cv_type<T>(), 0, 1, 3, 1.0, 0.0, border_type(border_mode));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool sobel_y(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                           const BorderMode border_mode) {
  return sobel_y(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] bool gradient_magnitude(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                      const BorderMode border_mode) {
  if constexpr (opencv_image_scalar_v<T>) {
    if (!valid_image_pair(input, output)) {
      return false;
    }
    auto gx_buffer = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    auto gy_buffer = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    auto gx = as_opencv(gx_buffer.view());
    auto gy = as_opencv(gy_buffer.view());
    cv::Sobel(as_opencv(input), gx, cv_type<T>(), 1, 0, 3, 1.0, 0.0, border_type(border_mode));
    cv::Sobel(as_opencv(input), gy, cv_type<T>(), 0, 1, 3, 1.0, 0.0, border_type(border_mode));
    cv::magnitude(gx, gy, as_opencv(output));
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool gradient_magnitude(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                      const BorderMode border_mode) {
  return gradient_magnitude(input.view(), output.view(), border_mode);
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool box_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode) {
  return detail::opencv_impl::box_filter(input, output, kernel_rows, kernel_cols, border_mode);
}

bool box_filter(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode) {
  return detail::opencv_impl::box_filter(input, output, kernel_rows, kernel_cols, border_mode);
}

bool gaussian_blur(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                   const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  return detail::opencv_impl::gaussian_blur(input, output, kernel_size, sigma, border_mode);
}

bool gaussian_blur(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                   const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  return detail::opencv_impl::gaussian_blur(input, output, kernel_size, sigma, border_mode);
}

bool bilateral_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                      const std::size_t diameter, const double sigma_color, const double sigma_space,
                      const BorderMode border_mode) {
  return detail::opencv_impl::bilateral_filter(input, output, diameter, sigma_color, sigma_space, border_mode);
}

bool median_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                   const std::size_t kernel_size, const BorderMode border_mode) {
  return detail::opencv_impl::median_filter(input, output, kernel_size, border_mode);
}

bool sobel_x(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
             const BorderMode border_mode) {
  return detail::opencv_impl::sobel_x(input, output, border_mode);
}

bool sobel_x(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
             const BorderMode border_mode) {
  return detail::opencv_impl::sobel_x(input, output, border_mode);
}

bool sobel_y(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
             const BorderMode border_mode) {
  return detail::opencv_impl::sobel_y(input, output, border_mode);
}

bool sobel_y(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
             const BorderMode border_mode) {
  return detail::opencv_impl::sobel_y(input, output, border_mode);
}

bool gradient_magnitude(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                        const BorderMode border_mode) {
  return detail::opencv_impl::gradient_magnitude(input, output, border_mode);
}

bool gradient_magnitude(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                        const BorderMode border_mode) {
  return detail::opencv_impl::gradient_magnitude(input, output, border_mode);
}
} // namespace detail::opencv
} // namespace ksj::image
