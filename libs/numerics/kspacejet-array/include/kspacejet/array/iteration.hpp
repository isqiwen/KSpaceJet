#pragma once

/// Indexed and zipped traversal helpers for algorithms that need explicit row-major iteration.

#include "kspacejet/array/detail/traversal.hpp"

#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

namespace detail {
template <typename View> using span_element_t = std::remove_pointer_t<decltype(std::declval<View&>().data())>;
} // namespace detail

template <typename View> [[nodiscard]] auto contiguous_span(View&& input) -> std::span<detail::span_element_t<View>> {
  if (!input.is_contiguous()) {
    throw std::invalid_argument("contiguous_span requires a contiguous array view");
  }
  return std::span<detail::span_element_t<View>>(input.data(), input.size());
}

template <typename View>
[[nodiscard]] auto try_contiguous_span(View&& input) noexcept
  -> std::optional<std::span<detail::span_element_t<View>>> {
  if (!input.is_contiguous()) {
    return std::nullopt;
  }
  return std::span<detail::span_element_t<View>>(input.data(), input.size());
}

template <typename T, typename F> F for_each(VectorView<T> input, F f) {
  return detail::for_each_elements(input, f);
}

template <typename T, typename F> F for_each(MatrixView<T> input, F f) {
  return detail::for_each_elements(input, f);
}

template <typename T, typename F> F for_each(ImageView<T> input, F f) {
  return detail::for_each_elements(input, f);
}

template <typename T, typename F> F for_each(CubeView<T> input, F f) {
  return detail::for_each_elements(input, f);
}

template <typename T, typename F> F for_each(Array4DView<T> input, F f) {
  return detail::for_each_elements(input, f);
}

template <typename T, typename F> F for_each(PooledVector<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(const PooledVector<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(PooledMatrix<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(const PooledMatrix<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(PooledImage<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(const PooledImage<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(PooledCube<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(const PooledCube<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(PooledArray4D<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each(const PooledArray4D<T>& input, F f) {
  return for_each(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(VectorView<T> input, F f) {
  return detail::for_each_contiguous_blocks(input, f);
}

template <typename T, typename F> F for_each_contiguous_block(MatrixView<T> input, F f) {
  return detail::for_each_contiguous_blocks(input, f);
}

template <typename T, typename F> F for_each_contiguous_block(ImageView<T> input, F f) {
  return detail::for_each_contiguous_blocks(input, f);
}

template <typename T, typename F> F for_each_contiguous_block(CubeView<T> input, F f) {
  return detail::for_each_contiguous_blocks(input, f);
}

template <typename T, typename F> F for_each_contiguous_block(Array4DView<T> input, F f) {
  return detail::for_each_contiguous_blocks(input, f);
}

template <typename T, typename F> F for_each_contiguous_block(PooledVector<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(const PooledVector<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(PooledMatrix<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(const PooledMatrix<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(PooledImage<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(const PooledImage<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(PooledCube<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(const PooledCube<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(PooledArray4D<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_contiguous_block(const PooledArray4D<T>& input, F f) {
  return for_each_contiguous_block(input.view(), f);
}

template <typename T, typename F> F for_each_linear_indexed(VectorView<T> input, F f) {
  return detail::for_each_linear_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_linear_indexed(MatrixView<T> input, F f) {
  return detail::for_each_linear_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_linear_indexed(ImageView<T> input, F f) {
  return detail::for_each_linear_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_linear_indexed(CubeView<T> input, F f) {
  return detail::for_each_linear_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_linear_indexed(Array4DView<T> input, F f) {
  return detail::for_each_linear_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_indexed(VectorView<T> input, F f) {
  return detail::for_each_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_indexed(MatrixView<T> input, F f) {
  return detail::for_each_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_indexed(ImageView<T> input, F f) {
  return detail::for_each_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_indexed(CubeView<T> input, F f) {
  return detail::for_each_indexed_elements(input, f);
}

template <typename T, typename F> F for_each_indexed(Array4DView<T> input, F f) {
  return detail::for_each_indexed_elements(input, f);
}

template <typename LhsT, typename RhsT, typename F> F for_each_zip(VectorView<LhsT> lhs, VectorView<RhsT> rhs, F f) {
  detail::validate_same_size(lhs.size(), rhs.size(), "vector view zip size mismatch");
  return detail::for_each_zip_elements(lhs, rhs, f);
}

template <typename LhsT, typename RhsT, typename F> F for_each_zip(MatrixView<LhsT> lhs, MatrixView<RhsT> rhs, F f) {
  detail::validate_same_shape(lhs, rhs, "matrix view zip shape mismatch");
  return detail::for_each_zip_elements(lhs, rhs, f);
}

template <typename LhsT, typename RhsT, typename F> F for_each_zip(ImageView<LhsT> lhs, ImageView<RhsT> rhs, F f) {
  detail::validate_same_shape(lhs, rhs, "image view zip shape mismatch");
  return detail::for_each_zip_elements(lhs, rhs, f);
}

template <typename LhsT, typename RhsT, typename F> F for_each_zip(CubeView<LhsT> lhs, CubeView<RhsT> rhs, F f) {
  detail::validate_same_cube_shape(lhs, rhs, "cube view zip shape mismatch");
  return detail::for_each_zip_elements(lhs, rhs, f);
}

template <typename LhsT, typename RhsT, typename F> F for_each_zip(Array4DView<LhsT> lhs, Array4DView<RhsT> rhs, F f) {
  detail::validate_same_array4d_shape(lhs, rhs, "array4d view zip shape mismatch");
  return detail::for_each_zip_elements(lhs, rhs, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename F>
F for_each_zip(VectorView<FirstT> first, VectorView<SecondT> second, VectorView<ThirdT> third, F f) {
  detail::validate_same_size(first.size(), second.size(), "vector view ternary zip input size mismatch");
  detail::validate_same_size(first.size(), third.size(), "vector view ternary zip input size mismatch");
  return detail::for_each_zip_elements(first, second, third, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename F>
F for_each_zip(MatrixView<FirstT> first, MatrixView<SecondT> second, MatrixView<ThirdT> third, F f) {
  detail::validate_same_shape(first, second, "matrix view ternary zip input shape mismatch");
  detail::validate_same_shape(first, third, "matrix view ternary zip input shape mismatch");
  return detail::for_each_zip_elements(first, second, third, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename F>
F for_each_zip(ImageView<FirstT> first, ImageView<SecondT> second, ImageView<ThirdT> third, F f) {
  detail::validate_same_shape(first, second, "image view ternary zip input shape mismatch");
  detail::validate_same_shape(first, third, "image view ternary zip input shape mismatch");
  return detail::for_each_zip_elements(first, second, third, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename F>
F for_each_zip(CubeView<FirstT> first, CubeView<SecondT> second, CubeView<ThirdT> third, F f) {
  detail::validate_same_cube_shape(first, second, "cube view ternary zip input shape mismatch");
  detail::validate_same_cube_shape(first, third, "cube view ternary zip input shape mismatch");
  return detail::for_each_zip_elements(first, second, third, f);
}

template <typename FirstT, typename SecondT, typename ThirdT, typename F>
F for_each_zip(Array4DView<FirstT> first, Array4DView<SecondT> second, Array4DView<ThirdT> third, F f) {
  detail::validate_same_array4d_shape(first, second, "array4d view ternary zip input shape mismatch");
  detail::validate_same_array4d_shape(first, third, "array4d view ternary zip input shape mismatch");
  return detail::for_each_zip_elements(first, second, third, f);
}

} // namespace ksj::array
