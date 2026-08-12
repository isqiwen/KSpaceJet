#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::signal::detail::eigen {

template <typename T>
void convolve(ksj::array::VectorView<const T> signal, ksj::array::VectorView<const T> kernel,
              ksj::array::VectorView<T> output) {
  const auto expected_size = signal.empty() || kernel.empty() ? 0U : signal.size() + kernel.size() - 1U;
  if (output.size() != expected_size) {
    throw std::invalid_argument("convolve output dimension mismatch");
  }
  if (expected_size == 0U) {
    return;
  }
  if ((signal.data() == output.data() && !signal.empty()) || (kernel.data() == output.data() && !kernel.empty())) {
    auto temp = ksj::array::make_pooled_vector<T>(expected_size);
    ksj::signal::detail::eigen::convolve(signal, kernel, temp.view());
    ksj::array::copy(temp.view(), output);
    return;
  }

  ksj::array::fill(output, T{});
  for (std::size_t signal_index = 0; signal_index < signal.size(); ++signal_index) {
    for (std::size_t kernel_index = 0; kernel_index < kernel.size(); ++kernel_index) {
      output(signal_index + kernel_index) += signal(signal_index) * kernel(kernel_index);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> convolve(const ksj::array::PooledVector<T>& signal,
                                                   const ksj::array::PooledVector<T>& kernel) {
  auto output =
    ksj::array::make_pooled_vector<T>(signal.empty() || kernel.empty() ? 0U : signal.size() + kernel.size() - 1U);
  ksj::signal::detail::eigen::convolve(ksj::array::as_const_view(signal.view()),
                                       ksj::array::as_const_view(kernel.view()), output.view());
  return output;
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
  if (ksj::array::detail::views_may_overlap(input, output) || ksj::array::detail::views_may_overlap(kernel, output)) {
    auto temp = ksj::array::make_pooled_matrix<T>(expected_rows, expected_cols);
    ksj::signal::detail::eigen::convolve2d_full(input, kernel, temp.view());
    ksj::array::copy(temp.view(), output);
    return;
  }

  ksj::array::fill(output, T{});
  for (std::size_t input_row = 0U; input_row < input.rows(); ++input_row) {
    for (std::size_t input_col = 0U; input_col < input.cols(); ++input_col) {
      const auto input_value = input(input_row, input_col);
      for (std::size_t kernel_row = 0U; kernel_row < kernel.rows(); ++kernel_row) {
        for (std::size_t kernel_col = 0U; kernel_col < kernel.cols(); ++kernel_col) {
          output(input_row + kernel_row, input_col + kernel_col) += input_value * kernel(kernel_row, kernel_col);
        }
      }
    }
  }
}

template <typename T>
void convolve2d_full(const ksj::array::PooledMatrix<T>& input, const ksj::array::PooledMatrix<T>& kernel,
                     ksj::array::PooledMatrix<T>& output) {
  convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> convolve2d_full(const ksj::array::PooledMatrix<T>& input,
                                                          const ksj::array::PooledMatrix<T>& kernel) {
  auto output =
    ksj::array::make_pooled_matrix<T>(input.empty() || kernel.empty() ? 0U : input.rows() + kernel.rows() - 1U,
                                      input.empty() || kernel.empty() ? 0U : input.cols() + kernel.cols() - 1U);
  convolve2d_full(input, kernel, output);
  return output;
}

template <typename T>
void compose_separable_kernel(ksj::array::VectorView<const T> row_kernel, ksj::array::VectorView<const T> col_kernel,
                              ksj::array::ImageView<T> output) {
  if (row_kernel.empty() || col_kernel.empty()) {
    throw std::invalid_argument("compose_separable_kernel kernels must not be empty");
  }
  if (output.rows() != col_kernel.size() || output.cols() != row_kernel.size()) {
    throw std::invalid_argument("compose_separable_kernel output dimension mismatch");
  }

  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = col_kernel(row) * row_kernel(col);
    }
  }
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
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("correlate2d_same output dimension mismatch");
  }
  if ((input.data() == output.data() && !input.empty()) || (kernel.data() == output.data() && !kernel.empty())) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    ksj::signal::detail::eigen::correlate2d_same(input, kernel, temp.view());
    ksj::array::copy(temp.view(), output);
    return;
  }

  const auto row_radius = static_cast<long>(kernel.rows() / 2U);
  const auto col_radius = static_cast<long>(kernel.cols() / 2U);
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      T sum{};
      for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
        const auto input_row = static_cast<long>(row) + static_cast<long>(kernel_row) - row_radius;
        if (input_row < 0 || input_row >= static_cast<long>(input.rows())) {
          continue;
        }
        for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
          const auto input_col = static_cast<long>(col) + static_cast<long>(kernel_col) - col_radius;
          if (input_col < 0 || input_col >= static_cast<long>(input.cols())) {
            continue;
          }
          sum += input(static_cast<std::size_t>(input_row), static_cast<std::size_t>(input_col)) *
                 kernel(kernel_row, kernel_col);
        }
      }
      output(row, col) = sum;
    }
  }
}

template <typename T>
void convolve2d_same(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                     ksj::array::ImageView<T> output) {
  if (input.empty() || kernel.empty()) {
    throw std::invalid_argument("convolve2d_same input and kernel must not be empty");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("convolve2d_same output dimension mismatch");
  }

  const auto row_radius = static_cast<long>(kernel.rows() / 2U);
  const auto col_radius = static_cast<long>(kernel.cols() / 2U);
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      T sum{};
      for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
        const auto input_row = static_cast<long>(row) + static_cast<long>(kernel_row) - row_radius;
        if (input_row < 0 || input_row >= static_cast<long>(input.rows())) {
          continue;
        }
        const auto flipped_kernel_row = kernel.rows() - 1U - kernel_row;
        for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
          const auto input_col = static_cast<long>(col) + static_cast<long>(kernel_col) - col_radius;
          if (input_col < 0 || input_col >= static_cast<long>(input.cols())) {
            continue;
          }
          const auto flipped_kernel_col = kernel.cols() - 1U - kernel_col;
          sum += input(static_cast<std::size_t>(input_row), static_cast<std::size_t>(input_col)) *
                 kernel(flipped_kernel_row, flipped_kernel_col);
        }
      }
      output(row, col) = sum;
    }
  }
}

template <typename T>
void convolve2d_same(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
                     ksj::array::PooledImage<T>& output) {
  convolve2d_same(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()), output.view());
}

template <typename T>
void correlate2d_same_separable(ksj::array::ImageView<const T> input, ksj::array::VectorView<const T> row_kernel,
                                ksj::array::VectorView<const T> col_kernel, ksj::array::ImageView<T> output,
                                ksj::array::ImageView<T> scratch) {
  if (input.empty() || row_kernel.empty() || col_kernel.empty()) {
    throw std::invalid_argument("correlate2d_same_separable input and kernels must not be empty");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols() || scratch.rows() != input.rows() ||
      scratch.cols() != input.cols()) {
    throw std::invalid_argument("correlate2d_same_separable output dimension mismatch");
  }

  const auto col_radius = static_cast<long>(row_kernel.size() / 2U);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      T sum{};
      for (std::size_t kernel_col = 0; kernel_col < row_kernel.size(); ++kernel_col) {
        const auto input_col = static_cast<long>(col) + static_cast<long>(kernel_col) - col_radius;
        if (input_col < 0 || input_col >= static_cast<long>(input.cols())) {
          continue;
        }
        sum += input(row, static_cast<std::size_t>(input_col)) * row_kernel(kernel_col);
      }
      scratch(row, col) = sum;
    }
  }

  const auto row_radius = static_cast<long>(col_kernel.size() / 2U);
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      T sum{};
      for (std::size_t kernel_row = 0; kernel_row < col_kernel.size(); ++kernel_row) {
        const auto input_row = static_cast<long>(row) + static_cast<long>(kernel_row) - row_radius;
        if (input_row < 0 || input_row >= static_cast<long>(input.rows())) {
          continue;
        }
        sum += scratch(static_cast<std::size_t>(input_row), col) * col_kernel(kernel_row);
      }
      output(row, col) = sum;
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same(const ksj::array::PooledImage<T>& input,
                                                          const ksj::array::PooledImage<T>& kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ksj::signal::detail::eigen::correlate2d_same(ksj::array::as_const_view(input.view()),
                                               ksj::array::as_const_view(kernel.view()), output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> convolve2d_same(const ksj::array::PooledImage<T>& input,
                                                         const ksj::array::PooledImage<T>& kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  convolve2d_same(input, kernel, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> correlate2d_same_separable(const ksj::array::PooledImage<T>& input,
                                                                    const ksj::array::PooledVector<T>& row_kernel,
                                                                    const ksj::array::PooledVector<T>& col_kernel) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  auto scratch = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ksj::signal::detail::eigen::correlate2d_same_separable(
    ksj::array::as_const_view(input.view()), ksj::array::as_const_view(row_kernel.view()),
    ksj::array::as_const_view(col_kernel.view()), output.view(), scratch.view());
  return output;
}
} // namespace ksj::signal::detail::eigen
