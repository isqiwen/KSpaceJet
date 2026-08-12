#pragma once

/// Frequency-domain shift and inverse-shift operations for FFT-ordered arrays.

#include "kspacejet/array/array.hpp"
#include "kspacejet/fft/detail/fft_shift_algorithms.hpp"
#include "kspacejet/fft/types.hpp"

#include <type_traits>

namespace ksj::fft {

template <typename T> void fftshift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void ifftshift(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void fftshift(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void fftshift(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void fftshift_in_place(ksj::array::VectorView<T> data) {
  detail::algorithms::fftshift(data);
}

template <typename T> void ifftshift(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void ifftshift(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void ifftshift_in_place(ksj::array::VectorView<T> data) {
  detail::algorithms::ifftshift(data);
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> fftshift(const ksj::array::PooledVector<T>& input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  fftshift(input, output);
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> ifftshift(const ksj::array::PooledVector<T>& input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  ifftshift(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> fftshift(ksj::array::VectorView<T> input) {
  auto output = ksj::array::make_pooled_vector<std::remove_const_t<T>>(input.size());
  fftshift(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>> ifftshift(ksj::array::VectorView<T> input) {
  auto output = ksj::array::make_pooled_vector<std::remove_const_t<T>>(input.size());
  ifftshift(input, output.view());
  return output;
}

template <typename T> void fftshift(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void ifftshift(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void fftshift(const ksj::array::PooledCube<T>& input, ksj::array::PooledCube<T>& output) {
  detail::algorithms::fftshift(ksj::array::as_const_view(input.view()), output.view());
}

template <typename T> void ifftshift(const ksj::array::PooledCube<T>& input, ksj::array::PooledCube<T>& output) {
  detail::algorithms::ifftshift(ksj::array::as_const_view(input.view()), output.view());
}

template <typename T> void fftshift(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void fftshift(ksj::array::MatrixView<T> input, ksj::array::MatrixView<T> output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void fftshift_in_place(ksj::array::MatrixView<T> data) {
  detail::algorithms::fftshift(data);
}

template <typename T> void ifftshift(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void ifftshift(ksj::array::MatrixView<T> input, ksj::array::MatrixView<T> output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void ifftshift_in_place(ksj::array::MatrixView<T> data) {
  detail::algorithms::ifftshift(data);
}

template <typename T> void fftshift(ksj::array::CubeView<const T> input, ksj::array::CubeView<T> output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void fftshift(ksj::array::CubeView<T> input, ksj::array::CubeView<T> output) {
  detail::algorithms::fftshift(input, output);
}

template <typename T> void fftshift_in_place(ksj::array::CubeView<T> data) {
  detail::algorithms::fftshift(data);
}

template <typename T> void ifftshift(ksj::array::CubeView<const T> input, ksj::array::CubeView<T> output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void ifftshift(ksj::array::CubeView<T> input, ksj::array::CubeView<T> output) {
  detail::algorithms::ifftshift(input, output);
}

template <typename T> void ifftshift_in_place(ksj::array::CubeView<T> data) {
  detail::algorithms::ifftshift(data);
}

template <typename T> [[nodiscard]] ksj::array::PooledMatrix<T> fftshift(const ksj::array::PooledMatrix<T>& input) {
  auto output = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
  fftshift(input, output);
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledMatrix<T> ifftshift(const ksj::array::PooledMatrix<T>& input) {
  auto output = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
  ifftshift(input, output);
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledCube<T> fftshift(const ksj::array::PooledCube<T>& input) {
  auto output = ksj::array::make_pooled_cube<T>(input.dim0(), input.dim1(), input.dim2());
  fftshift(input, output);
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledCube<T> ifftshift(const ksj::array::PooledCube<T>& input) {
  auto output = ksj::array::make_pooled_cube<T>(input.dim0(), input.dim1(), input.dim2());
  ifftshift(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<std::remove_const_t<T>> fftshift(ksj::array::MatrixView<T> input) {
  auto output = ksj::array::make_pooled_matrix<std::remove_const_t<T>>(input.rows(), input.cols());
  fftshift(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<std::remove_const_t<T>> ifftshift(ksj::array::MatrixView<T> input) {
  auto output = ksj::array::make_pooled_matrix<std::remove_const_t<T>>(input.rows(), input.cols());
  ifftshift(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledCube<std::remove_const_t<T>> fftshift(ksj::array::CubeView<T> input) {
  auto output = ksj::array::make_pooled_cube<std::remove_const_t<T>>(input.dim0(), input.dim1(), input.dim2());
  fftshift(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledCube<std::remove_const_t<T>> ifftshift(ksj::array::CubeView<T> input) {
  auto output = ksj::array::make_pooled_cube<std::remove_const_t<T>>(input.dim0(), input.dim1(), input.dim2());
  ifftshift(input, output.view());
  return output;
}

} // namespace ksj::fft
