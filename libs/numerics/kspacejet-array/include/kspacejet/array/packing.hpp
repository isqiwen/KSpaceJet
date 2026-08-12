#pragma once

/// Contiguous packing helpers for backends that cannot consume strided Views directly.

#include "kspacejet/array/pooled_array4d.hpp"
#include "kspacejet/array/pooled_cube.hpp"
#include "kspacejet/array/pooled_image.hpp"
#include "kspacejet/array/pooled_matrix.hpp"
#include "kspacejet/array/pooled_vector.hpp"
#include "kspacejet/array/copy.hpp"
#include "kspacejet/array/views.hpp"

#include <type_traits>

namespace ksj::array {

template <typename T>
[[nodiscard]] VectorView<const std::remove_const_t<T>> pack_contiguous(VectorView<T> input,
                                                                       PooledVector<std::remove_const_t<T>>& scratch) {
  if (input.is_contiguous()) {
    return as_const_view(input);
  }

  scratch.resize(input.size());
  copy(input, scratch.view());
  return as_const_view(scratch.view());
}

template <typename T>
[[nodiscard]] MatrixView<const std::remove_const_t<T>> pack_contiguous(MatrixView<T> input,
                                                                       PooledMatrix<std::remove_const_t<T>>& scratch) {
  if (input.is_contiguous()) {
    return as_const_view(input);
  }

  scratch.resize(input.rows(), input.cols());
  copy(input, scratch.view());
  return as_const_view(scratch.view());
}

template <typename T>
[[nodiscard]] ImageView<const std::remove_const_t<T>> pack_contiguous(ImageView<T> input,
                                                                      PooledImage<std::remove_const_t<T>>& scratch) {
  if (input.is_contiguous()) {
    return as_const_view(input);
  }

  scratch.resize(input.rows(), input.cols());
  copy(input, scratch.view());
  return as_const_view(scratch.view());
}

template <typename T>
[[nodiscard]] CubeView<const std::remove_const_t<T>> pack_contiguous(CubeView<T> input,
                                                                     PooledCube<std::remove_const_t<T>>& scratch) {
  if (input.is_contiguous()) {
    return as_const_view(input);
  }

  scratch.resize(input.dim0(), input.dim1(), input.dim2());
  copy(input, scratch.view());
  return as_const_view(scratch.view());
}

template <typename T>
[[nodiscard]] Array4DView<const std::remove_const_t<T>>
pack_contiguous(Array4DView<T> input, PooledArray4D<std::remove_const_t<T>>& scratch) {
  if (input.is_contiguous()) {
    return as_const_view(input);
  }

  scratch.resize(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  copy(input, scratch.view());
  return as_const_view(scratch.view());
}

} // namespace ksj::array
