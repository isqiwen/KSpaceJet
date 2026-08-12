#pragma once

/// Signal feature-extraction operations that summarize or transform sampled input sequences.

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_feature_extraction.hpp"
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ksj::signal {

template <typename T> struct NormalEquationWithoutIndexResult {
  ksj::array::PooledMatrix<T> matrix;
  ksj::array::PooledMatrix<T> rhs;
  ksj::array::PooledVector<std::size_t> retained_indexes;
};

template <typename T>
void extract_sliding_window_matrix(ksj::array::CubeView<const T> input, const std::size_t window_rows,
                                   const std::size_t window_cols, ksj::array::MatrixView<T> output) {
  detail::eigen::extract_sliding_window_matrix(input, window_rows, window_cols, output);
}

template <typename T>
void extract_sliding_window_matrix(const ksj::array::PooledCube<T>& input, const std::size_t window_rows,
                                   const std::size_t window_cols, ksj::array::PooledMatrix<T>& output) {
  extract_sliding_window_matrix(ksj::array::as_const_view(input.view()), window_rows, window_cols, output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> extract_sliding_window_matrix(ksj::array::CubeView<const T> input,
                                                                        const std::size_t window_rows,
                                                                        const std::size_t window_cols) {
  if (input.empty()) {
    throw std::invalid_argument("extract_sliding_window_matrix input must not be empty");
  }
  if (window_rows == 0U || window_cols == 0U || window_rows > input.dim0() || window_cols > input.dim1()) {
    throw std::invalid_argument("extract_sliding_window_matrix window dimensions are invalid");
  }

  const std::size_t output_rows = (input.dim0() - window_rows + 1U) * (input.dim1() - window_cols + 1U);
  const std::size_t output_cols = window_rows * window_cols * input.dim2();
  auto output = ksj::array::make_pooled_matrix<T>(output_rows, output_cols);
  extract_sliding_window_matrix(input, window_rows, window_cols, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> extract_sliding_window_matrix(ksj::array::CubeView<T> input,
                                                                        const std::size_t window_rows,
                                                                        const std::size_t window_cols)
  requires(!std::is_const_v<T>)
{
  return extract_sliding_window_matrix(ksj::array::as_const_view(input), window_rows, window_cols);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> extract_sliding_window_matrix(const ksj::array::PooledCube<T>& input,
                                                                        const std::size_t window_rows,
                                                                        const std::size_t window_cols) {
  return extract_sliding_window_matrix(ksj::array::as_const_view(input.view()), window_rows, window_cols);
}

template <typename T>
void normal_equation_without_index(ksj::array::MatrixView<const T> input, const std::size_t excluded_index,
                                   ksj::array::MatrixView<T> matrix, ksj::array::MatrixView<T> rhs,
                                   ksj::array::VectorView<std::size_t> retained_indexes) {
  detail::eigen::normal_equation_without_index(input, excluded_index, matrix, rhs, retained_indexes);
}

template <typename T>
void normal_equation_without_index(const ksj::array::PooledMatrix<T>& input, const std::size_t excluded_index,
                                   ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& rhs,
                                   ksj::array::PooledVector<std::size_t>& retained_indexes) {
  normal_equation_without_index(ksj::array::as_const_view(input.view()), excluded_index, matrix.view(), rhs.view(),
                                retained_indexes.view());
}

template <typename T>
[[nodiscard]] NormalEquationWithoutIndexResult<T> normal_equation_without_index(ksj::array::MatrixView<const T> input,
                                                                                const std::size_t excluded_index) {
  if (input.empty() || input.rows() != input.cols()) {
    throw std::invalid_argument("normal_equation_without_index input must be a non-empty square matrix");
  }
  if (excluded_index >= input.rows()) {
    throw std::invalid_argument("normal_equation_without_index excluded index is outside input");
  }

  const auto output_size = input.rows() - 1U;
  auto result = NormalEquationWithoutIndexResult<T>{
    ksj::array::make_pooled_matrix<T>(output_size, output_size),
    ksj::array::make_pooled_matrix<T>(output_size, 1U),
    ksj::array::make_pooled_vector<std::size_t>(output_size),
  };
  normal_equation_without_index(input, excluded_index, result.matrix.view(), result.rhs.view(),
                                result.retained_indexes.view());
  return result;
}

template <typename T>
[[nodiscard]] NormalEquationWithoutIndexResult<T> normal_equation_without_index(ksj::array::MatrixView<T> input,
                                                                                const std::size_t excluded_index)
  requires(!std::is_const_v<T>)
{
  return normal_equation_without_index(ksj::array::as_const_view(input), excluded_index);
}

template <typename T>
[[nodiscard]] NormalEquationWithoutIndexResult<T>
normal_equation_without_index(const ksj::array::PooledMatrix<T>& input, const std::size_t excluded_index) {
  return normal_equation_without_index(input.view(), excluded_index);
}

} // namespace ksj::signal
