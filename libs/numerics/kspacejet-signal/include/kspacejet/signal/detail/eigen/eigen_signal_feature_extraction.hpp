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
void extract_sliding_window_matrix(ksj::array::CubeView<const T> input, const std::size_t window_rows,
                                   const std::size_t window_cols, ksj::array::MatrixView<T> output) {
  if (input.empty()) {
    throw std::invalid_argument("extract_sliding_window_matrix input must not be empty");
  }
  if (window_rows == 0U || window_cols == 0U || window_rows > input.dim0() || window_cols > input.dim1()) {
    throw std::invalid_argument("extract_sliding_window_matrix window dimensions are invalid");
  }

  const std::size_t output_rows = (input.dim0() - window_rows + 1U) * (input.dim1() - window_cols + 1U);
  const std::size_t output_cols = window_rows * window_cols * input.dim2();
  if (output.rows() != output_rows || output.cols() != output_cols) {
    throw std::invalid_argument("extract_sliding_window_matrix output dimension mismatch");
  }

  const std::size_t sample_rows = input.dim0() - window_rows + 1U;
  const std::size_t sample_cols = input.dim1() - window_cols + 1U;

  for (std::size_t sample_row = 0U; sample_row < sample_rows; ++sample_row) {
    for (std::size_t sample_col = 0U; sample_col < sample_cols; ++sample_col) {
      const std::size_t output_row = sample_row * sample_cols + sample_col;
      for (std::size_t window_row = 0U; window_row < window_rows; ++window_row) {
        for (std::size_t window_col = 0U; window_col < window_cols; ++window_col) {
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            const std::size_t output_col = window_row * window_cols * input.dim2() + window_col * input.dim2() + i2;
            output(output_row, output_col) = input(sample_row + window_row, sample_col + window_col, i2);
          }
        }
      }
    }
  }
}

template <typename T>
void normal_equation_without_index(ksj::array::MatrixView<const T> input, const std::size_t excluded_index,
                                   ksj::array::MatrixView<T> matrix, ksj::array::MatrixView<T> rhs,
                                   ksj::array::VectorView<std::size_t> retained_indexes) {
  if (input.empty() || input.rows() != input.cols()) {
    throw std::invalid_argument("normal_equation_without_index input must be a non-empty square matrix");
  }
  if (excluded_index >= input.rows()) {
    throw std::invalid_argument("normal_equation_without_index excluded index is outside input");
  }

  const std::size_t output_size = input.rows() - 1U;
  if (matrix.rows() != output_size || matrix.cols() != output_size || rhs.rows() != output_size || rhs.cols() != 1U ||
      retained_indexes.size() != output_size) {
    throw std::invalid_argument("normal_equation_without_index output dimension mismatch");
  }

  std::size_t out_row = 0U;
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    if (row == excluded_index) {
      continue;
    }

    retained_indexes(out_row) = row;
    rhs(out_row, 0U) = input(row, excluded_index);

    std::size_t out_col = 0U;
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      if (col == excluded_index) {
        continue;
      }
      matrix(out_row, out_col) = input(row, col);
      ++out_col;
    }
    ++out_row;
  }
}
} // namespace ksj::signal::detail::eigen
