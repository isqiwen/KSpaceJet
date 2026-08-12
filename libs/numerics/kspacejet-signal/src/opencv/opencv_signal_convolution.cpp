#include "kspacejet/signal/detail/opencv/opencv_signal_convolution.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::signal::detail::opencv {
namespace {

template <typename T> [[nodiscard]] int cv_type() {
  using value_type = std::remove_const_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return CV_32FC1;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return CV_64FC1;
  } else {
    return -1;
  }
}

[[nodiscard]] bool fits_cv_size(const std::size_t rows, const std::size_t cols) noexcept {
  return rows <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
         cols <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

template <typename T> [[nodiscard]] cv::Mat as_opencv(ksj::array::ImageView<T> image) {
  using value_type = std::remove_const_t<T>;
  return cv::Mat(static_cast<int>(image.rows()), static_cast<int>(image.cols()), cv_type<value_type>(),
                 const_cast<value_type*>(image.data()), image.row_stride_bytes());
}

template <typename T> [[nodiscard]] cv::Mat as_opencv(ksj::array::VectorView<T> vector) {
  using value_type = std::remove_const_t<T>;
  return cv::Mat(1, static_cast<int>(vector.size()), cv_type<value_type>(), const_cast<value_type*>(vector.data()));
}

template <typename T>
[[nodiscard]] bool correlate2d_same_impl(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                                         ksj::array::ImageView<T> output) {
  if (input.empty() || kernel.empty() || output.rows() != input.rows() || output.cols() != input.cols() ||
      !fits_cv_size(input.rows(), input.cols()) || !fits_cv_size(kernel.rows(), kernel.cols())) {
    return false;
  }

  cv::filter2D(as_opencv(input), as_opencv(output), cv_type<T>(), as_opencv(kernel), cv::Point(-1, -1), 0.0,
               cv::BORDER_CONSTANT);
  return true;
}

template <typename T>
[[nodiscard]] bool
correlate2d_same_separable_impl(ksj::array::ImageView<const T> input, ksj::array::VectorView<const T> row_kernel,
                                ksj::array::VectorView<const T> col_kernel, ksj::array::ImageView<T> output) {
  if (input.empty() || row_kernel.empty() || col_kernel.empty() || output.rows() != input.rows() ||
      output.cols() != input.cols() || !fits_cv_size(input.rows(), input.cols()) ||
      !fits_cv_size(row_kernel.size(), col_kernel.size()) || row_kernel.stride() != 1U || col_kernel.stride() != 1U) {
    return false;
  }

  const auto col_kernel_cv = as_opencv(col_kernel).reshape(1, static_cast<int>(col_kernel.size()));
  cv::sepFilter2D(as_opencv(input), as_opencv(output), cv_type<T>(), as_opencv(row_kernel), col_kernel_cv,
                  cv::Point(-1, -1), 0.0, cv::BORDER_CONSTANT);
  return true;
}

} // namespace

bool correlate2d_same(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                      ksj::array::ImageView<float> output) {
  return correlate2d_same_impl(input, kernel, output);
}

bool correlate2d_same(ksj::array::ImageView<const double> input, ksj::array::ImageView<const double> kernel,
                      ksj::array::ImageView<double> output) {
  return correlate2d_same_impl(input, kernel, output);
}

bool correlate2d_same_separable(ksj::array::ImageView<const float> input,
                                ksj::array::VectorView<const float> row_kernel,
                                ksj::array::VectorView<const float> col_kernel, ksj::array::ImageView<float> output) {
  return correlate2d_same_separable_impl(input, row_kernel, col_kernel, output);
}

bool correlate2d_same_separable(ksj::array::ImageView<const double> input,
                                ksj::array::VectorView<const double> row_kernel,
                                ksj::array::VectorView<const double> col_kernel, ksj::array::ImageView<double> output) {
  return correlate2d_same_separable_impl(input, row_kernel, col_kernel, output);
}

} // namespace ksj::signal::detail::opencv
