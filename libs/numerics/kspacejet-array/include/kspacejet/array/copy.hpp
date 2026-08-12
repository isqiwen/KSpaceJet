#pragma once

/// Shape-checked copy, centered copy, and materialization operations for Views and Pooled arrays.

#include "kspacejet/array/detail/array_policy.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_storage.hpp"
#include "kspacejet/array/detail/intel/intel_array_storage.hpp"
#include "kspacejet/array/indexing.hpp"
#include "kspacejet/array/initialization.hpp"
#include "kspacejet/memory/pooled_buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace ksj::array {

enum class CenteredCopyAlignment {
  lower,
  upper,
};

namespace detail {
[[nodiscard]] constexpr std::size_t centered_offset(const std::size_t extent, const std::size_t centered_extent,
                                                    const CenteredCopyAlignment alignment) noexcept {
  if (alignment == CenteredCopyAlignment::lower) {
    return (extent - centered_extent) / 2U;
  }
  return centered_offset(extent, centered_extent);
}

struct ViewMemorySpan {
  std::uintptr_t begin{};
  std::uintptr_t end{};
};

template <typename T> [[nodiscard]] std::uintptr_t pointer_address(T* pointer) noexcept {
  return reinterpret_cast<std::uintptr_t>(pointer);
}

template <typename T>
[[nodiscard]] ViewMemorySpan make_view_memory_span(T* data, const std::size_t max_element_offset) noexcept {
  if (data == nullptr) {
    return {};
  }
  const auto begin = pointer_address(data);
  return {begin, begin + (max_element_offset + 1U) * sizeof(std::remove_const_t<T>)};
}

[[nodiscard]] inline bool memory_spans_overlap(const ViewMemorySpan lhs, const ViewMemorySpan rhs) noexcept {
  return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

template <typename T> [[nodiscard]] ViewMemorySpan view_memory_span(VectorView<T> view) noexcept {
  if (view.empty()) {
    return {};
  }
  return make_view_memory_span(view.data(), (view.size() - 1U) * view.stride());
}

template <typename T> [[nodiscard]] ViewMemorySpan view_memory_span(MatrixView<T> view) noexcept {
  if (view.empty()) {
    return {};
  }
  return make_view_memory_span(view.data(),
                               (view.rows() - 1U) * view.row_stride() + (view.cols() - 1U) * view.col_stride());
}

template <typename T> [[nodiscard]] ViewMemorySpan view_memory_span(ImageView<T> view) noexcept {
  if (view.empty()) {
    return {};
  }
  return make_view_memory_span(view.data(), (view.rows() - 1U) * view.row_stride() + view.cols() - 1U);
}

template <typename T> [[nodiscard]] ViewMemorySpan view_memory_span(CubeView<T> view) noexcept {
  if (view.empty()) {
    return {};
  }
  return make_view_memory_span(view.data(), (view.dim0() - 1U) * view.dim0_stride() +
                                              (view.dim1() - 1U) * view.dim1_stride() +
                                              (view.dim2() - 1U) * view.dim2_stride());
}

template <typename T> [[nodiscard]] ViewMemorySpan view_memory_span(Array4DView<T> view) noexcept {
  if (view.empty()) {
    return {};
  }
  return make_view_memory_span(view.data(),
                               (view.dim0() - 1U) * view.dim0_stride() + (view.dim1() - 1U) * view.dim1_stride() +
                                 (view.dim2() - 1U) * view.dim2_stride() + (view.dim3() - 1U) * view.dim3_stride());
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool views_may_overlap(InputView input, OutputView output) noexcept {
  return memory_spans_overlap(view_memory_span(input), view_memory_span(output));
}

template <typename InputView, typename OutputView> void copy_via_temporary(InputView input, OutputView output) {
  using value_type = typename InputView::value_type;
  auto scratch = ksj::memory::allocate_array<value_type>(input.size());
  for (std::size_t index = 0U; index < input.size(); ++index) {
    scratch.data()[index] = input[index];
  }
  for (std::size_t index = 0U; index < scratch.size(); ++index) {
    output[index] = scratch.data()[index];
  }
}

template <typename InputT, typename OutputT>
void copy_strided_elements(VectorView<InputT> input, VectorView<OutputT> output) {
  if (input.size() == 0U || pointer_address(input.data()) == pointer_address(output.data())) {
    return;
  }

  if constexpr (std::is_same_v<std::remove_const_t<InputT>, std::remove_const_t<OutputT>>) {
    if (!views_may_overlap(input, output)) {
      for (std::size_t index = 0U; index < input.size(); ++index) {
        output[index] = input[index];
      }
      return;
    }

    if (pointer_address(output.data()) > pointer_address(input.data())) {
      for (std::size_t index = input.size(); index-- > 0U;) {
        output[index] = input[index];
      }
      return;
    }

    for (std::size_t index = 0U; index < input.size(); ++index) {
      output[index] = input[index];
    }
    return;
  }

  if (views_may_overlap(input, output)) {
    copy_via_temporary(input, output);
    return;
  }

  for (std::size_t index = 0U; index < input.size(); ++index) {
    output[index] = input[index];
  }
}

template <typename InputView, typename OutputView> void copy_strided_elements(InputView input, OutputView output) {
  if (views_may_overlap(input, output)) {
    copy_via_temporary(input, output);
    return;
  }

  for (std::size_t index = 0U; index < input.size(); ++index) {
    output[index] = input[index];
  }
}

template <typename InputT, typename OutputT>
void copy_contiguous_elements(InputT* input, const std::size_t count, OutputT* output) {
  if (count == 0U || pointer_address(input) == pointer_address(output)) {
    return;
  }

  if constexpr (std::is_same_v<std::remove_const_t<InputT>, std::remove_const_t<OutputT>>) {
    const auto input_span = make_view_memory_span(input, count - 1U);
    const auto output_begin = pointer_address(output);
    if (output_begin > input_span.begin && output_begin < input_span.end) {
      std::copy_backward(input, input + count, output + count);
      return;
    }
  } else if (memory_spans_overlap(make_view_memory_span(input, count - 1U),
                                  make_view_memory_span(output, count - 1U))) {
    auto scratch = ksj::memory::allocate_array<std::remove_const_t<InputT>>(count);
    for (std::size_t index = 0U; index < count; ++index) {
      scratch.data()[index] = input[index];
    }
    std::copy_n(scratch.data(), count, output);
    return;
  }

  std::copy_n(input, count, output);
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool copy_vector_backend(VectorView<InputT> input, VectorView<OutputT> output) {
  if (views_may_overlap(input, output)) {
    return false;
  }
  const auto const_input = as_const_view(input);
  if (prefer_intel_vector_copy(input, output) && intel::copy(const_input, output)) {
    return true;
  }
  if (prefer_eigen_vector_copy(input, output) && eigen::copy(const_input, output)) {
    return true;
  }
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool same_storage_layout(MatrixView<InputT> input, MatrixView<OutputT> output) noexcept {
  return pointer_address(input.data()) == pointer_address(output.data()) && input.rows() == output.rows() &&
         input.cols() == output.cols() && input.row_stride() == output.row_stride() &&
         input.col_stride() == output.col_stride();
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool same_storage_layout(ImageView<InputT> input, ImageView<OutputT> output) noexcept {
  return pointer_address(input.data()) == pointer_address(output.data()) && input.rows() == output.rows() &&
         input.cols() == output.cols() && input.row_stride() == output.row_stride();
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool same_storage_layout(CubeView<InputT> input, CubeView<OutputT> output) noexcept {
  return pointer_address(input.data()) == pointer_address(output.data()) && input.dim0() == output.dim0() &&
         input.dim1() == output.dim1() && input.dim2() == output.dim2() &&
         input.dim0_stride() == output.dim0_stride() && input.dim1_stride() == output.dim1_stride() &&
         input.dim2_stride() == output.dim2_stride();
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool same_storage_layout(Array4DView<InputT> input, Array4DView<OutputT> output) noexcept {
  return pointer_address(input.data()) == pointer_address(output.data()) && input.dim0() == output.dim0() &&
         input.dim1() == output.dim1() && input.dim2() == output.dim2() && input.dim3() == output.dim3() &&
         input.dim0_stride() == output.dim0_stride() && input.dim1_stride() == output.dim1_stride() &&
         input.dim2_stride() == output.dim2_stride() && input.dim3_stride() == output.dim3_stride();
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool copy_inner_contiguous_blocks(MatrixView<InputT> input, MatrixView<OutputT> output) {
  if (input.col_stride() != 1U || output.col_stride() != 1U) {
    return false;
  }
  if (same_storage_layout(input, output)) {
    return true;
  }
  if (views_may_overlap(input, output)) {
    return false;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    copy_contiguous_elements(input.data() + row * input.row_stride(), input.cols(),
                             output.data() + row * output.row_stride());
  }
  return true;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool copy_inner_contiguous_blocks(ImageView<InputT> input, ImageView<OutputT> output) {
  if (same_storage_layout(input, output)) {
    return true;
  }
  if (views_may_overlap(input, output)) {
    return false;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    copy_contiguous_elements(input.data() + row * input.row_stride(), input.cols(),
                             output.data() + row * output.row_stride());
  }
  return true;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool copy_inner_contiguous_blocks(CubeView<InputT> input, CubeView<OutputT> output) {
  if (input.dim2_stride() != 1U || output.dim2_stride() != 1U) {
    return false;
  }
  if (same_storage_layout(input, output)) {
    return true;
  }
  if (views_may_overlap(input, output)) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      copy_contiguous_elements(input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride(), input.dim2(),
                               output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride());
    }
  }
  return true;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool copy_inner_contiguous_blocks(Array4DView<InputT> input, Array4DView<OutputT> output) {
  if (input.dim3_stride() != 1U || output.dim3_stride() != 1U) {
    return false;
  }
  if (same_storage_layout(input, output)) {
    return true;
  }
  if (views_may_overlap(input, output)) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        copy_contiguous_elements(
          input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride(), input.dim3(),
          output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride() + i2 * output.dim2_stride());
      }
    }
  }
  return true;
}
} // namespace detail

template <typename InputT, typename OutputT> void copy(VectorView<InputT> input, VectorView<OutputT> output) {
  detail::validate_same_size(input.size(), output.size(), "vector view copy size mismatch");
  if (input.is_contiguous() && output.is_contiguous()) {
    if (detail::copy_vector_backend(input, output)) {
      return;
    }
    detail::copy_contiguous_elements(input.data(), input.size(), output.data());
    return;
  }
  detail::copy_strided_elements(input, output);
}

template <typename InputT, typename OutputT> void copy(MatrixView<InputT> input, MatrixView<OutputT> output) {
  detail::validate_same_shape(input, output, "matrix view copy shape mismatch");
  if (input.is_contiguous() && output.is_contiguous()) {
    copy(VectorView<InputT>(input.data(), input.size()), VectorView<OutputT>(output.data(), output.size()));
    return;
  }
  if (detail::copy_inner_contiguous_blocks(input, output)) {
    return;
  }
  detail::copy_strided_elements(input, output);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(MatrixView<InputT> input, MatrixView<OutputT> output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  fill(output, background);

  const auto rows = std::min(input.rows(), output.rows());
  const auto cols = std::min(input.cols(), output.cols());
  if (rows == 0U || cols == 0U) {
    return;
  }

  const auto input_row_start = detail::centered_offset(input.rows(), rows, alignment);
  const auto input_col_start = detail::centered_offset(input.cols(), cols, alignment);
  const auto output_row_start = detail::centered_offset(output.rows(), rows, alignment);
  const auto output_col_start = detail::centered_offset(output.cols(), cols, alignment);

  copy(
    input.subview(slice(input_row_start, input_row_start + rows), slice(input_col_start, input_col_start + cols)),
    output.subview(slice(output_row_start, output_row_start + rows), slice(output_col_start, output_col_start + cols)));
}

template <typename InputT, typename OutputT> void copy(ImageView<InputT> input, ImageView<OutputT> output) {
  detail::validate_same_shape(input, output, "image view copy shape mismatch");
  if (input.is_contiguous() && output.is_contiguous()) {
    copy(VectorView<InputT>(input.data(), input.size()), VectorView<OutputT>(output.data(), output.size()));
    return;
  }
  if (detail::copy_inner_contiguous_blocks(input, output)) {
    return;
  }
  detail::copy_strided_elements(input, output);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(ImageView<InputT> input, ImageView<OutputT> output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  fill(output, background);

  const auto rows = std::min(input.rows(), output.rows());
  const auto cols = std::min(input.cols(), output.cols());
  if (rows == 0U || cols == 0U) {
    return;
  }

  const auto input_row_start = detail::centered_offset(input.rows(), rows, alignment);
  const auto input_col_start = detail::centered_offset(input.cols(), cols, alignment);
  const auto output_row_start = detail::centered_offset(output.rows(), rows, alignment);
  const auto output_col_start = detail::centered_offset(output.cols(), cols, alignment);

  copy(
    input.subview(slice(input_row_start, input_row_start + rows), slice(input_col_start, input_col_start + cols)),
    output.subview(slice(output_row_start, output_row_start + rows), slice(output_col_start, output_col_start + cols)));
}

template <typename InputT, typename OutputT> void transpose(MatrixView<InputT> input, MatrixView<OutputT> output) {
  if (output.rows() != input.cols() || output.cols() != input.rows()) {
    throw std::invalid_argument("matrix view transpose output shape mismatch");
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = input(col, row);
    }
  }
}

template <typename InputT, typename OutputT> void transpose(ImageView<InputT> input, ImageView<OutputT> output) {
  if (output.rows() != input.cols() || output.cols() != input.rows()) {
    throw std::invalid_argument("image view transpose output shape mismatch");
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = input(col, row);
    }
  }
}

template <typename InputT, typename OutputT>
void transpose_rotated_180(MatrixView<InputT> input, MatrixView<OutputT> output) {
  if (output.rows() != input.cols() || output.cols() != input.rows()) {
    throw std::invalid_argument("matrix view transpose-rotated output shape mismatch");
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = input(input.rows() - 1U - col, input.cols() - 1U - row);
    }
  }
}

template <typename InputT, typename OutputT>
void transpose_rotated_180(ImageView<InputT> input, ImageView<OutputT> output) {
  if (output.rows() != input.cols() || output.cols() != input.rows()) {
    throw std::invalid_argument("image view transpose-rotated output shape mismatch");
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = input(input.rows() - 1U - col, input.cols() - 1U - row);
    }
  }
}

template <typename InputT, typename OutputT> void copy(CubeView<InputT> input, CubeView<OutputT> output) {
  detail::validate_same_cube_shape(input, output, "cube view copy shape mismatch");
  if (input.is_contiguous() && output.is_contiguous()) {
    copy(VectorView<InputT>(input.data(), input.size()), VectorView<OutputT>(output.data(), output.size()));
    return;
  }
  if (detail::copy_inner_contiguous_blocks(input, output)) {
    return;
  }
  detail::copy_strided_elements(input, output);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(CubeView<InputT> input, CubeView<OutputT> output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  fill(output, background);

  const auto dim0 = std::min(input.dim0(), output.dim0());
  const auto dim1 = std::min(input.dim1(), output.dim1());
  const auto dim2 = std::min(input.dim2(), output.dim2());
  if (dim0 == 0U || dim1 == 0U || dim2 == 0U) {
    return;
  }

  const auto input_dim0_start = detail::centered_offset(input.dim0(), dim0, alignment);
  const auto input_dim1_start = detail::centered_offset(input.dim1(), dim1, alignment);
  const auto input_dim2_start = detail::centered_offset(input.dim2(), dim2, alignment);
  const auto output_dim0_start = detail::centered_offset(output.dim0(), dim0, alignment);
  const auto output_dim1_start = detail::centered_offset(output.dim1(), dim1, alignment);
  const auto output_dim2_start = detail::centered_offset(output.dim2(), dim2, alignment);

  copy(input.subview(slice(input_dim0_start, input_dim0_start + dim0), slice(input_dim1_start, input_dim1_start + dim1),
                     slice(input_dim2_start, input_dim2_start + dim2)),
       output.subview(slice(output_dim0_start, output_dim0_start + dim0),
                      slice(output_dim1_start, output_dim1_start + dim1),
                      slice(output_dim2_start, output_dim2_start + dim2)));
}

template <typename MaskT, typename InputT, typename OutputT, typename MaskValue>
void copy_where_equal(CubeView<MaskT> mask, const MaskValue& value, CubeView<InputT> input, CubeView<OutputT> output) {
  detail::validate_same_cube_shape(mask, input, "cube view copy_where_equal input shape mismatch");
  detail::validate_same_cube_shape(mask, output, "cube view copy_where_equal output shape mismatch");

  if (mask.is_contiguous() && input.is_contiguous() && output.is_contiguous()) {
    const auto* mask_data = mask.data();
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < mask.size(); ++index) {
      if (mask_data[index] == value) {
        output_data[index] = input_data[index];
      }
    }
    return;
  }

  for (std::size_t i0 = 0U; i0 < mask.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < mask.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < mask.dim2(); ++i2) {
        if (mask(i0, i1, i2) == value) {
          output(i0, i1, i2) = input(i0, i1, i2);
        }
      }
    }
  }
}

template <typename MaskT, typename InputT, typename OutputT, typename MaskValue>
void copy_where_equal(const PooledCube<MaskT>& mask, const MaskValue& value, const PooledCube<InputT>& input,
                      PooledCube<OutputT>& output) {
  copy_where_equal(mask.view(), value, input.view(), output.view());
}

template <typename InputT, typename OutputT> void copy(Array4DView<InputT> input, Array4DView<OutputT> output) {
  detail::validate_same_array4d_shape(input, output, "array4d view copy shape mismatch");
  if (input.is_contiguous() && output.is_contiguous()) {
    copy(VectorView<InputT>(input.data(), input.size()), VectorView<OutputT>(output.data(), output.size()));
    return;
  }
  if (detail::copy_inner_contiguous_blocks(input, output)) {
    return;
  }
  detail::copy_strided_elements(input, output);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(Array4DView<InputT> input, Array4DView<OutputT> output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  fill(output, background);

  const auto dim0 = std::min(input.dim0(), output.dim0());
  const auto dim1 = std::min(input.dim1(), output.dim1());
  const auto dim2 = std::min(input.dim2(), output.dim2());
  const auto dim3 = std::min(input.dim3(), output.dim3());
  if (dim0 == 0U || dim1 == 0U || dim2 == 0U || dim3 == 0U) {
    return;
  }

  const auto input_dim0_start = detail::centered_offset(input.dim0(), dim0, alignment);
  const auto input_dim1_start = detail::centered_offset(input.dim1(), dim1, alignment);
  const auto input_dim2_start = detail::centered_offset(input.dim2(), dim2, alignment);
  const auto input_dim3_start = detail::centered_offset(input.dim3(), dim3, alignment);
  const auto output_dim0_start = detail::centered_offset(output.dim0(), dim0, alignment);
  const auto output_dim1_start = detail::centered_offset(output.dim1(), dim1, alignment);
  const auto output_dim2_start = detail::centered_offset(output.dim2(), dim2, alignment);
  const auto output_dim3_start = detail::centered_offset(output.dim3(), dim3, alignment);

  copy(input.subview(slice(input_dim0_start, input_dim0_start + dim0), slice(input_dim1_start, input_dim1_start + dim1),
                     slice(input_dim2_start, input_dim2_start + dim2),
                     slice(input_dim3_start, input_dim3_start + dim3)),
       output.subview(
         slice(output_dim0_start, output_dim0_start + dim0), slice(output_dim1_start, output_dim1_start + dim1),
         slice(output_dim2_start, output_dim2_start + dim2), slice(output_dim3_start, output_dim3_start + dim3)));
}

template <typename InputT, typename OutputT>
void copy(const PooledVector<InputT>& input, PooledVector<OutputT>& output) {
  copy(input.view(), output.view());
}

template <typename InputT, typename OutputT>
void copy(const PooledMatrix<InputT>& input, PooledMatrix<OutputT>& output) {
  copy(input.view(), output.view());
}

template <typename InputT, typename OutputT> void copy(const PooledImage<InputT>& input, PooledImage<OutputT>& output) {
  copy(input.view(), output.view());
}

template <typename InputT, typename OutputT> void copy(const PooledCube<InputT>& input, PooledCube<OutputT>& output) {
  copy(input.view(), output.view());
}

template <typename InputT, typename OutputT>
void copy(const PooledArray4D<InputT>& input, PooledArray4D<OutputT>& output) {
  copy(input.view(), output.view());
}

template <typename InputT> [[nodiscard]] PooledVector<std::remove_const_t<InputT>> copy(VectorView<InputT> input) {
  auto output = PooledVector<std::remove_const_t<InputT>>(input.size());
  copy(input, output.view());
  return output;
}

template <typename InputT> [[nodiscard]] PooledMatrix<std::remove_const_t<InputT>> copy(MatrixView<InputT> input) {
  auto output = PooledMatrix<std::remove_const_t<InputT>>(input.rows(), input.cols());
  copy(input, output.view());
  return output;
}

template <typename InputT> [[nodiscard]] PooledImage<std::remove_const_t<InputT>> copy(ImageView<InputT> input) {
  auto output = PooledImage<std::remove_const_t<InputT>>(input.rows(), input.cols());
  copy(input, output.view());
  return output;
}

template <typename InputT> [[nodiscard]] PooledCube<std::remove_const_t<InputT>> copy(CubeView<InputT> input) {
  auto output = PooledCube<std::remove_const_t<InputT>>(input.dim0(), input.dim1(), input.dim2());
  copy(input, output.view());
  return output;
}

template <typename InputT> [[nodiscard]] PooledArray4D<std::remove_const_t<InputT>> copy(Array4DView<InputT> input) {
  auto output = PooledArray4D<std::remove_const_t<InputT>>(input.dim0(), input.dim1(), input.dim2(), input.dim3());
  copy(input, output.view());
  return output;
}

template <typename InputT> [[nodiscard]] PooledVector<InputT> copy(const PooledVector<InputT>& input) {
  return copy(input.view());
}

template <typename InputT> [[nodiscard]] PooledMatrix<InputT> copy(const PooledMatrix<InputT>& input) {
  return copy(input.view());
}

template <typename InputT> [[nodiscard]] PooledImage<InputT> copy(const PooledImage<InputT>& input) {
  return copy(input.view());
}

template <typename InputT> [[nodiscard]] PooledCube<InputT> copy(const PooledCube<InputT>& input) {
  return copy(input.view());
}

template <typename InputT> [[nodiscard]] PooledArray4D<InputT> copy(const PooledArray4D<InputT>& input) {
  return copy(input.view());
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(const PooledMatrix<InputT>& input, PooledMatrix<OutputT>& output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  copy_centered(input.view(), output.view(), background, alignment);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(const PooledImage<InputT>& input, PooledImage<OutputT>& output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  copy_centered(input.view(), output.view(), background, alignment);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(const PooledCube<InputT>& input, PooledCube<OutputT>& output, const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  copy_centered(input.view(), output.view(), background, alignment);
}

template <typename InputT, typename OutputT, typename Value = std::remove_const_t<OutputT>>
void copy_centered(const PooledArray4D<InputT>& input, PooledArray4D<OutputT>& output,
                   const Value& background = Value{},
                   const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  copy_centered(input.view(), output.view(), background, alignment);
}

template <typename InputT, typename Value = std::remove_const_t<InputT>>
[[nodiscard]] PooledMatrix<std::remove_const_t<InputT>>
copy_centered(MatrixView<InputT> input, const std::size_t rows, const std::size_t cols,
              const Value& background = Value{}, const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  using value_type = std::remove_const_t<InputT>;
  auto output = PooledMatrix<value_type>(rows, cols);
  copy_centered(input, output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = std::remove_const_t<InputT>>
[[nodiscard]] PooledImage<std::remove_const_t<InputT>>
copy_centered(ImageView<InputT> input, const std::size_t rows, const std::size_t cols,
              const Value& background = Value{}, const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  using value_type = std::remove_const_t<InputT>;
  auto output = PooledImage<value_type>(rows, cols);
  copy_centered(input, output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = std::remove_const_t<InputT>>
[[nodiscard]] PooledCube<std::remove_const_t<InputT>>
copy_centered(CubeView<InputT> input, const std::size_t dim0, const std::size_t dim1, const std::size_t dim2,
              const Value& background = Value{}, const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  using value_type = std::remove_const_t<InputT>;
  auto output = PooledCube<value_type>(dim0, dim1, dim2);
  copy_centered(input, output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = std::remove_const_t<InputT>>
[[nodiscard]] PooledArray4D<std::remove_const_t<InputT>>
copy_centered(Array4DView<InputT> input, const std::size_t dim0, const std::size_t dim1, const std::size_t dim2,
              const std::size_t dim3, const Value& background = Value{},
              const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  using value_type = std::remove_const_t<InputT>;
  auto output = PooledArray4D<value_type>(dim0, dim1, dim2, dim3);
  copy_centered(input, output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = InputT>
[[nodiscard]] PooledMatrix<InputT> copy_centered(const PooledMatrix<InputT>& input, const std::size_t rows,
                                                 const std::size_t cols, const Value& background = Value{},
                                                 const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  auto output = PooledMatrix<InputT>(rows, cols);
  copy_centered(input.view(), output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = InputT>
[[nodiscard]] PooledImage<InputT> copy_centered(const PooledImage<InputT>& input, const std::size_t rows,
                                                const std::size_t cols, const Value& background = Value{},
                                                const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  auto output = PooledImage<InputT>(rows, cols);
  copy_centered(input.view(), output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = InputT>
[[nodiscard]] PooledCube<InputT>
copy_centered(const PooledCube<InputT>& input, const std::size_t dim0, const std::size_t dim1, const std::size_t dim2,
              const Value& background = Value{}, const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  auto output = PooledCube<InputT>(dim0, dim1, dim2);
  copy_centered(input.view(), output.view(), background, alignment);
  return output;
}

template <typename InputT, typename Value = InputT>
[[nodiscard]] PooledArray4D<InputT>
copy_centered(const PooledArray4D<InputT>& input, const std::size_t dim0, const std::size_t dim1,
              const std::size_t dim2, const std::size_t dim3, const Value& background = Value{},
              const CenteredCopyAlignment alignment = CenteredCopyAlignment::upper) {
  auto output = PooledArray4D<InputT>(dim0, dim1, dim2, dim3);
  copy_centered(input.view(), output.view(), background, alignment);
  return output;
}

} // namespace ksj::array
