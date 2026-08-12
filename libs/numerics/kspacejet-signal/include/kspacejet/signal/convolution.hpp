#pragma once

/// One- and multi-dimensional convolution APIs with explicit boundary and output semantics.

#include "kspacejet/base/compiler.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_convolution.hpp"
#include "kspacejet/signal/detail/fft/fft_signal_convolution.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_convolution.hpp"
#include "kspacejet/signal/detail/opencv/opencv_signal_convolution.hpp"
#include "kspacejet/signal/detail/signal_policy.hpp"
#include <stdexcept>
#include <type_traits>

namespace ksj::signal {

template <typename T>
KSJ_FORCE_INLINE void convolve(ksj::array::VectorView<const T> signal, ksj::array::VectorView<const T> kernel,
                               ksj::array::VectorView<T> output) {
  const auto expected_size = signal.empty() || kernel.empty() ? 0U : signal.size() + kernel.size() - 1U;
  if (output.size() != expected_size) {
    throw std::invalid_argument("convolve output dimension mismatch");
  }
  if (expected_size == 0U) {
    return;
  }
  if (detail::prefer_intel_convolve<T>(signal.size(), kernel.size()) &&
      detail::intel::convolve(signal, kernel, output)) {
    return;
  }
  detail::eigen::convolve(signal, kernel, output);
}

template <typename T>
void convolve(ksj::array::VectorView<T> signal, ksj::array::VectorView<T> kernel, ksj::array::VectorView<T> output) {
  convolve(ksj::array::as_const_view(signal), ksj::array::as_const_view(kernel), output);
}

template <typename T>
void convolve(const ksj::array::PooledVector<T>& signal, const ksj::array::PooledVector<T>& kernel,
              ksj::array::PooledVector<T>& output) {
  convolve(ksj::array::as_const_view(signal.view()), ksj::array::as_const_view(kernel.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> convolve(ksj::array::VectorView<const T> signal,
                                                   ksj::array::VectorView<const T> kernel) {
  auto output =
    ksj::array::make_pooled_vector<T>(signal.empty() || kernel.empty() ? 0U : signal.size() + kernel.size() - 1U);
  convolve(signal, kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> convolve(const ksj::array::PooledVector<T>& signal,
                                                   const ksj::array::PooledVector<T>& kernel) {
  return convolve(ksj::array::as_const_view(signal.view()), ksj::array::as_const_view(kernel.view()));
}

template <typename T>
void convolve2d_full(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<const T> kernel,
                     ksj::array::MatrixView<T> output) {
  const auto expected_rows = input.empty() || kernel.empty() ? 0U : input.rows() + kernel.rows() - 1U;
  const auto expected_cols = input.empty() || kernel.empty() ? 0U : input.cols() + kernel.cols() - 1U;
  if (output.rows() != expected_rows || output.cols() != expected_cols) {
    throw std::invalid_argument("convolve2d_full output dimension mismatch");
  }
  if (expected_rows == 0U || expected_cols == 0U) {
    return;
  }

  if (detail::prefer_intel_convolve2d_full<T>(input.size(), kernel.size()) &&
      detail::intel::convolve2d_full(input, kernel, output)) {
    return;
  }

  detail::eigen::convolve2d_full(input, kernel, output);
}

template <typename T>
void convolve2d_full(ksj::array::MatrixView<T> input, ksj::array::MatrixView<T> kernel,
                     ksj::array::MatrixView<T> output) {
  convolve2d_full(ksj::array::as_const_view(input), ksj::array::as_const_view(kernel), output);
}

template <typename T>
void convolve2d_full(const ksj::array::PooledMatrix<T>& input, const ksj::array::PooledMatrix<T>& kernel,
                     ksj::array::PooledMatrix<T>& output) {
  convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> convolve2d_full(ksj::array::MatrixView<const T> input,
                                                          ksj::array::MatrixView<const T> kernel) {
  auto output =
    ksj::array::make_pooled_matrix<T>(input.empty() || kernel.empty() ? 0U : input.rows() + kernel.rows() - 1U,
                                      input.empty() || kernel.empty() ? 0U : input.cols() + kernel.cols() - 1U);
  convolve2d_full(input, kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> convolve2d_full(const ksj::array::PooledMatrix<T>& input,
                                                          const ksj::array::PooledMatrix<T>& kernel) {
  return convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()));
}

template <typename T>
void convolve2d_full(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                     ksj::array::ImageView<T> output) {
  convolve2d_full(input.as_matrix_view(), kernel.as_matrix_view(), output.as_matrix_view());
}

template <typename T>
void convolve2d_full(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
                     ksj::array::PooledImage<T>& output) {
  convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> convolve2d_full(ksj::array::ImageView<const T> input,
                                                         ksj::array::ImageView<const T> kernel) {
  auto output =
    ksj::array::make_pooled_image<T>(input.empty() || kernel.empty() ? 0U : input.rows() + kernel.rows() - 1U,
                                     input.empty() || kernel.empty() ? 0U : input.cols() + kernel.cols() - 1U);
  convolve2d_full(input, kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> convolve2d_full(const ksj::array::PooledImage<T>& input,
                                                         const ksj::array::PooledImage<T>& kernel) {
  return convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()));
}

template <typename T>
void correlate2d_same(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                      ksj::array::ImageView<T> output);

template <typename T>
void convolve2d_same(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                     ksj::array::ImageView<T> output) {
  if (input.empty() || kernel.empty()) {
    throw std::invalid_argument("convolve2d_same input and kernel must not be empty");
  }
  if (output.shape().extents != input.shape().extents) {
    throw std::invalid_argument("convolve2d_same output dimension mismatch");
  }

  if (ksj::array::detail::views_may_overlap(input, output) || ksj::array::detail::views_may_overlap(kernel, output)) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    convolve2d_same(input, kernel, temp.view());
    ksj::array::copy(ksj::array::as_const_view(temp.view()), output);
    return;
  }

  if (detail::prefer_intel_correlate2d_same<T>(input.size(), kernel.size()) ||
      detail::prefer_fft_correlate2d_same<T>(input.rows(), input.cols(), kernel.rows(), kernel.cols()) ||
      detail::prefer_opencv_correlate2d_same<T>(input.size(), kernel.size())) {
    auto flipped_kernel = ksj::array::make_pooled_image<T>(kernel.rows(), kernel.cols());
    for (std::size_t row = 0U; row < kernel.rows(); ++row) {
      for (std::size_t col = 0U; col < kernel.cols(); ++col) {
        flipped_kernel(row, col) = kernel(kernel.rows() - 1U - row, kernel.cols() - 1U - col);
      }
    }
    correlate2d_same(input, ksj::array::as_const_view(flipped_kernel.view()), output);
    return;
  }

  detail::eigen::convolve2d_same(input, kernel, output);
}

template <typename T>
void convolve2d_same(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
                     ksj::array::PooledImage<T>& output) {
  convolve2d_same(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> convolve2d_same(ksj::array::ImageView<const T> input,
                                                         ksj::array::ImageView<const T> kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  convolve2d_same(input, kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> convolve2d_same(ksj::array::ImageView<T> input,
                                                         ksj::array::ImageView<T> kernel) {
  return convolve2d_same(ksj::array::as_const_view(input), ksj::array::as_const_view(kernel));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> convolve2d_same(const ksj::array::PooledImage<T>& input,
                                                         const ksj::array::PooledImage<T>& kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  convolve2d_same(input, kernel, output);
  return output;
}

template <typename T>
void compose_separable_kernel(ksj::array::VectorView<const T> row_kernel, ksj::array::VectorView<const T> col_kernel,
                              ksj::array::ImageView<T> output) {
  detail::eigen::compose_separable_kernel(row_kernel, col_kernel, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> compose_separable_kernel(ksj::array::VectorView<const T> row_kernel,
                                                                  ksj::array::VectorView<const T> col_kernel) {
  auto output = ksj::array::make_pooled_image<T>(col_kernel.size(), row_kernel.size());
  compose_separable_kernel(row_kernel, col_kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> compose_separable_kernel(const ksj::array::PooledVector<T>& row_kernel,
                                                                  const ksj::array::PooledVector<T>& col_kernel) {
  auto output = ksj::array::make_pooled_image<T>(col_kernel.size(), row_kernel.size());
  compose_separable_kernel(ksj::array::as_const_view(row_kernel.view()), ksj::array::as_const_view(col_kernel.view()),
                           output.view());
  return output;
}

template <typename T>
void correlate2d_same(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                      ksj::array::ImageView<T> output) {
  if (input.empty() || kernel.empty()) {
    throw std::invalid_argument("correlate2d_same input and kernel must not be empty");
  }
  if (output.shape().extents != input.shape().extents) {
    throw std::invalid_argument("correlate2d_same output dimension mismatch");
  }

  if (detail::prefer_intel_correlate2d_same<T>(input.size(), kernel.size()) &&
      detail::intel::correlate2d_same(input, kernel, output)) {
    return;
  }

  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    if (detail::prefer_fft_correlate2d_same<T>(input.rows(), input.cols(), kernel.rows(), kernel.cols())) {
      detail::fft::correlate2d_same(input, kernel, output);
      return;
    }
  }

  if (detail::prefer_opencv_correlate2d_same<T>(input.size(), kernel.size()) &&
      detail::opencv::correlate2d_same(input, kernel, output)) {
    return;
  }

  detail::eigen::correlate2d_same(input, kernel, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same(ksj::array::ImageView<const T> input,
                                                          ksj::array::ImageView<const T> kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same(input, kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same(ksj::array::ImageView<T> input,
                                                          ksj::array::ImageView<T> kernel) {
  return correlate2d_same(ksj::array::as_const_view(input), ksj::array::as_const_view(kernel));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same(const ksj::array::PooledImage<T>& input,
                                                          const ksj::array::PooledImage<T>& kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), output.view());
  return output;
}

template <typename T>
void correlate2d_same_fft(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                          ksj::array::ImageView<T> output) {
  if (input.empty() || kernel.empty()) {
    throw std::invalid_argument("correlate2d_same_fft input and kernel must not be empty");
  }
  if (output.shape().extents != input.shape().extents) {
    throw std::invalid_argument("correlate2d_same_fft output dimension mismatch");
  }
  detail::fft::correlate2d_same(input, kernel, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_fft(ksj::array::ImageView<const T> input,
                                                              ksj::array::ImageView<const T> kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same_fft(input, kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_fft(ksj::array::ImageView<T> input,
                                                              ksj::array::ImageView<T> kernel) {
  return correlate2d_same_fft(ksj::array::as_const_view(input), ksj::array::as_const_view(kernel));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_fft(const ksj::array::PooledImage<T>& input,
                                                              const ksj::array::PooledImage<T>& kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same_fft(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()),
                       output.view());
  return output;
}

template <typename T>
void correlate2d_same_separable(ksj::array::ImageView<const T> input, ksj::array::VectorView<const T> row_kernel,
                                ksj::array::VectorView<const T> col_kernel, ksj::array::ImageView<T> output,
                                ksj::array::ImageView<T> scratch) {
  if (input.empty() || row_kernel.empty() || col_kernel.empty()) {
    throw std::invalid_argument("correlate2d_same_separable input and kernels must not be empty");
  }
  if (output.shape().extents != input.shape().extents || scratch.shape().extents != input.shape().extents) {
    throw std::invalid_argument("correlate2d_same_separable output dimension mismatch");
  }

  if (detail::prefer_opencv_correlate2d_same_separable<T>(input.size(), row_kernel.size(), col_kernel.size()) &&
      detail::opencv::correlate2d_same_separable(input, row_kernel, col_kernel, output)) {
    return;
  }

  detail::eigen::correlate2d_same_separable(input, row_kernel, col_kernel, output, scratch);
}

template <typename T>
void correlate2d_same_separable(ksj::array::ImageView<const T> input, ksj::array::VectorView<const T> row_kernel,
                                ksj::array::VectorView<const T> col_kernel, ksj::array::ImageView<T> output) {
  auto scratch = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same_separable(input, row_kernel, col_kernel, output, scratch.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_separable(ksj::array::ImageView<const T> input,
                                                                    ksj::array::VectorView<const T> row_kernel,
                                                                    ksj::array::VectorView<const T> col_kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same_separable(input, row_kernel, col_kernel, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_separable(ksj::array::ImageView<T> input,
                                                                    ksj::array::VectorView<T> row_kernel,
                                                                    ksj::array::VectorView<T> col_kernel) {
  return correlate2d_same_separable(ksj::array::as_const_view(input), ksj::array::as_const_view(row_kernel),
                                    ksj::array::as_const_view(col_kernel));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_separable(const ksj::array::PooledImage<T>& input,
                                                                    const ksj::array::PooledVector<T>& row_kernel,
                                                                    const ksj::array::PooledVector<T>& col_kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  correlate2d_same_separable(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(row_kernel.view()),
                             ksj::array::as_const_view(col_kernel.view()), output.view());
  return output;
}

} // namespace ksj::signal
