#pragma once

#include "kspacejet/array/array.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace ksj::fft::detail::algorithms {

template <typename T>
void vector_shift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output,
                  const std::size_t shift) {
  for (std::size_t index = 0; index < input.size(); ++index) {
    output(index) = input((index + shift) % input.size());
  }
}

template <typename T>
void vector_view_shift(const ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                       const std::size_t shift) {
  for (std::size_t index = 0; index < input.size(); ++index) {
    output(index) = input((index + shift) % input.size());
  }
}

template <typename T>
void vector_view_shift(const ksj::array::VectorView<T> input, ksj::array::VectorView<T> output,
                       const std::size_t shift) {
  vector_view_shift(ksj::array::as_const_view(input), output, shift);
}

template <typename T>
void matrix_shift(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output,
                  const std::size_t row_shift, const std::size_t col_shift) {
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      output(row, col) = input((row + row_shift) % input.rows(), (col + col_shift) % input.cols());
    }
  }
}

template <typename T>
void matrix_view_shift(const ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                       const std::size_t row_shift, const std::size_t col_shift) {
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      output(row, col) = input((row + row_shift) % input.rows(), (col + col_shift) % input.cols());
    }
  }
}

template <typename T>
void matrix_view_shift(const ksj::array::MatrixView<T> input, ksj::array::MatrixView<T> output,
                       const std::size_t row_shift, const std::size_t col_shift) {
  matrix_view_shift(ksj::array::as_const_view(input), output, row_shift, col_shift);
}

template <typename T>
void cube_view_shift(const ksj::array::CubeView<const T> input, ksj::array::CubeView<T> output,
                     const std::size_t dim0_shift, const std::size_t dim1_shift, const std::size_t dim2_shift) {
  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        output(i0, i1, i2) =
          input((i0 + dim0_shift) % input.dim0(), (i1 + dim1_shift) % input.dim1(), (i2 + dim2_shift) % input.dim2());
      }
    }
  }
}

template <typename T>
void cube_view_shift(const ksj::array::CubeView<T> input, ksj::array::CubeView<T> output, const std::size_t dim0_shift,
                     const std::size_t dim1_shift, const std::size_t dim2_shift) {
  cube_view_shift(ksj::array::as_const_view(input), output, dim0_shift, dim1_shift, dim2_shift);
}

template <typename T> void vector_shift_inplace(std::span<T> data, const std::size_t shift) {
  if (data.empty()) {
    return;
  }

  const auto normalized_shift = shift % data.size();
  std::rotate(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(normalized_shift), data.end());
}

template <typename T> void fftshift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("fftshift output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t shift = (input.size() + 1U) / 2U;
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_vector<T>(input.size());
    vector_shift(input, temp, shift);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  vector_shift(input, output, shift);
}

template <typename T> void fftshift(std::span<T> data) {
  vector_shift_inplace(data, (data.size() + 1U) / 2U);
}

template <typename T> void fftshift(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("fftshift vector view output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  vector_view_shift(input, output, (input.size() + 1U) / 2U);
}

template <typename T> void fftshift(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output) {
  fftshift(ksj::array::as_const_view(input), output);
}

template <typename T> void fftshift(ksj::array::VectorView<T> data) {
  if (data.empty()) {
    return;
  }

  auto temp = ksj::array::make_pooled_vector<T>(data.size());
  vector_view_shift(ksj::array::as_const_view(data), temp.view(), (data.size() + 1U) / 2U);
  ksj::array::copy(temp.view(), data);
}

template <typename T> void ifftshift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("ifftshift output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t shift = input.size() / 2U;
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_vector<T>(input.size());
    vector_shift(input, temp, shift);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  vector_shift(input, output, shift);
}

template <typename T> void ifftshift(std::span<T> data) {
  vector_shift_inplace(data, data.size() / 2U);
}

template <typename T> void ifftshift(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("ifftshift vector view output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  vector_view_shift(input, output, input.size() / 2U);
}

template <typename T> void ifftshift(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output) {
  ifftshift(ksj::array::as_const_view(input), output);
}

template <typename T> void ifftshift(ksj::array::VectorView<T> data) {
  if (data.empty()) {
    return;
  }

  auto temp = ksj::array::make_pooled_vector<T>(data.size());
  vector_view_shift(ksj::array::as_const_view(data), temp.view(), data.size() / 2U);
  ksj::array::copy(temp.view(), data);
}

template <typename T> void fftshift(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("fftshift output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t row_shift = (input.rows() + 1U) / 2U;
  const std::size_t col_shift = (input.cols() + 1U) / 2U;
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
    matrix_shift(input, temp, row_shift, col_shift);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  matrix_shift(input, output, row_shift, col_shift);
}

template <typename T> void ifftshift(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("ifftshift output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t row_shift = input.rows() / 2U;
  const std::size_t col_shift = input.cols() / 2U;
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
    matrix_shift(input, temp, row_shift, col_shift);
    std::copy(temp.data(), temp.data() + temp.size(), output.data());
    return;
  }

  matrix_shift(input, output, row_shift, col_shift);
}

template <typename T> void fftshift(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("fftshift matrix view output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t row_shift = (input.rows() + 1U) / 2U;
  const std::size_t col_shift = (input.cols() + 1U) / 2U;
  matrix_view_shift(input, output, row_shift, col_shift);
}

template <typename T> void fftshift(ksj::array::MatrixView<T> input, ksj::array::MatrixView<T> output) {
  fftshift(ksj::array::as_const_view(input), output);
}

template <typename T> void fftshift(ksj::array::MatrixView<T> data) {
  if (data.empty()) {
    return;
  }

  auto temp = ksj::array::make_pooled_matrix<T>(data.rows(), data.cols());
  matrix_view_shift(ksj::array::as_const_view(data), temp.view(), (data.rows() + 1U) / 2U, (data.cols() + 1U) / 2U);
  ksj::array::copy(temp.view(), data);
}

template <typename T> void ifftshift(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("ifftshift matrix view output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t row_shift = input.rows() / 2U;
  const std::size_t col_shift = input.cols() / 2U;
  matrix_view_shift(input, output, row_shift, col_shift);
}

template <typename T> void ifftshift(ksj::array::MatrixView<T> input, ksj::array::MatrixView<T> output) {
  ifftshift(ksj::array::as_const_view(input), output);
}

template <typename T> void ifftshift(ksj::array::MatrixView<T> data) {
  if (data.empty()) {
    return;
  }

  auto temp = ksj::array::make_pooled_matrix<T>(data.rows(), data.cols());
  matrix_view_shift(ksj::array::as_const_view(data), temp.view(), data.rows() / 2U, data.cols() / 2U);
  ksj::array::copy(temp.view(), data);
}

template <typename T> void fftshift(ksj::array::CubeView<const T> input, ksj::array::CubeView<T> output) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("fftshift cube view output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t dim0_shift = (input.dim0() + 1U) / 2U;
  const std::size_t dim1_shift = (input.dim1() + 1U) / 2U;
  const std::size_t dim2_shift = (input.dim2() + 1U) / 2U;
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_cube<T>(input.dim0(), input.dim1(), input.dim2());
    cube_view_shift(input, temp.view(), dim0_shift, dim1_shift, dim2_shift);
    ksj::array::copy(temp.view(), output);
    return;
  }

  cube_view_shift(input, output, dim0_shift, dim1_shift, dim2_shift);
}

template <typename T> void fftshift(ksj::array::CubeView<T> input, ksj::array::CubeView<T> output) {
  fftshift(ksj::array::as_const_view(input), output);
}

template <typename T> void fftshift(ksj::array::CubeView<T> data) {
  fftshift(ksj::array::as_const_view(data), data);
}

template <typename T> void ifftshift(ksj::array::CubeView<const T> input, ksj::array::CubeView<T> output) {
  if (input.dim0() != output.dim0() || input.dim1() != output.dim1() || input.dim2() != output.dim2()) {
    throw std::invalid_argument("ifftshift cube view output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const std::size_t dim0_shift = input.dim0() / 2U;
  const std::size_t dim1_shift = input.dim1() / 2U;
  const std::size_t dim2_shift = input.dim2() / 2U;
  if (input.data() == output.data()) {
    auto temp = ksj::array::make_pooled_cube<T>(input.dim0(), input.dim1(), input.dim2());
    cube_view_shift(input, temp.view(), dim0_shift, dim1_shift, dim2_shift);
    ksj::array::copy(temp.view(), output);
    return;
  }

  cube_view_shift(input, output, dim0_shift, dim1_shift, dim2_shift);
}

template <typename T> void ifftshift(ksj::array::CubeView<T> input, ksj::array::CubeView<T> output) {
  ifftshift(ksj::array::as_const_view(input), output);
}

template <typename T> void ifftshift(ksj::array::CubeView<T> data) {
  ifftshift(ksj::array::as_const_view(data), data);
}

} // namespace ksj::fft::detail::algorithms
