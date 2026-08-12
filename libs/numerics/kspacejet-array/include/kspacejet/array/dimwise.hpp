#pragma once

/// Dimension-wise array transforms and reductions that preserve KSpaceJet row-major dimension semantics.

#include "kspacejet/array/detail/array_policy.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_dimwise.hpp"
#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/indexing.hpp"
#include "kspacejet/array/pooled_array4d.hpp"
#include "kspacejet/array/pooled_cube.hpp"
#include "kspacejet/array/pooled_matrix.hpp"
#include "kspacejet/array/scalar_traits.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace ksj::array {

namespace detail {

template <typename Array4DT, typename CubeT>
void validate_array4d_cube_tail_shape(Array4DView<Array4DT> array4d, CubeView<CubeT> cube, const char* message) {
  if (array4d.dim1() != cube.extent(0U) || array4d.dim2() != cube.extent(1U) || array4d.dim3() != cube.extent(2U)) {
    throw std::invalid_argument(message);
  }
}

template <typename OutputT, typename LeftT, typename RightT>
[[nodiscard]] std::remove_const_t<OutputT> multiply_conjugate(const LeftT& left, const RightT& right) noexcept
  requires(is_complex_v<OutputT> && is_complex_v<LeftT> && is_complex_v<RightT>)
{
  return {left.real() * right.real() + left.imag() * right.imag(),
          left.imag() * right.real() - left.real() * right.imag()};
}

struct SlidingPatchMatrixShape {
  std::size_t rows{};
  std::size_t cols{};
};

template <typename InputT>
[[nodiscard]] SlidingPatchMatrixShape
sliding_patch_matrix_shape(Array4DView<InputT> input, const std::size_t kernel_dim1, const std::size_t kernel_dim2,
                           const std::size_t kernel_dim3) {
  if (kernel_dim1 == 0U || kernel_dim2 == 0U || kernel_dim3 == 0U) {
    throw std::invalid_argument("array4d patch kernel dimensions must be nonzero");
  }
  if (kernel_dim1 > input.dim1() || kernel_dim2 > input.dim2() || kernel_dim3 > input.dim3()) {
    throw std::invalid_argument("array4d patch kernel dimensions exceed input dimensions");
  }

  const auto patch_dim1 = input.dim1() - kernel_dim1 + 1U;
  const auto patch_dim2 = input.dim2() - kernel_dim2 + 1U;
  const auto patch_dim3 = input.dim3() - kernel_dim3 + 1U;
  return {patch_dim1 * patch_dim2 * patch_dim3, input.dim0() * kernel_dim1 * kernel_dim2 * kernel_dim3};
}

template <typename InputT, typename OutputT>
void validate_sliding_patch_matrix_shape(Array4DView<InputT> input, const std::size_t kernel_dim1,
                                         const std::size_t kernel_dim2, const std::size_t kernel_dim3,
                                         MatrixView<OutputT> output) {
  const auto expected = sliding_patch_matrix_shape(input, kernel_dim1, kernel_dim2, kernel_dim3);
  if (output.rows() != expected.rows || output.cols() != expected.cols) {
    throw std::invalid_argument("array4d patch matrix output shape mismatch");
  }
}

} // namespace detail

template <typename Array4DT, typename CubeT, typename OutputT>
void multiply_array4d_by_cube(Array4DView<Array4DT> array4d, CubeView<CubeT> cube, Array4DView<OutputT> output) {
  detail::validate_array4d_cube_tail_shape(array4d, cube, "array4d cube multiply input shape mismatch");
  detail::validate_same_array4d_shape(array4d, output, "array4d cube multiply output shape mismatch");

  if (detail::prefer_eigen_array4d_cube_multiply(array4d, cube, output) &&
      detail::eigen::multiply_array4d_by_cube_contiguous(array4d, cube, output)) {
    return;
  }

  for (std::size_t i0 = 0U; i0 < array4d.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < cube.extent(0U); ++i1) {
      for (std::size_t i2 = 0U; i2 < cube.extent(1U); ++i2) {
        for (std::size_t i3 = 0U; i3 < cube.extent(2U); ++i3) {
          output(i0, i1, i2, i3) = array4d(i0, i1, i2, i3) * cube(i1, i2, i3);
        }
      }
    }
  }
}

template <typename Array4DT, typename CubeT>
void multiply_array4d_by_cube_in_place(Array4DView<Array4DT> array4d, CubeView<CubeT> cube) {
  multiply_array4d_by_cube(array4d, cube, array4d);
}

template <typename CubeT, typename Array4DT, typename OutputT>
void multiply_cube_by_array4d(CubeView<CubeT> cube, Array4DView<Array4DT> array4d, Array4DView<OutputT> output) {
  detail::validate_array4d_cube_tail_shape(array4d, cube, "cube array4d multiply input shape mismatch");
  detail::validate_same_array4d_shape(array4d, output, "cube array4d multiply output shape mismatch");

  if (detail::prefer_eigen_array4d_cube_multiply(array4d, cube, output) &&
      detail::eigen::multiply_cube_by_array4d_contiguous(cube, array4d, output)) {
    return;
  }

  for (std::size_t i0 = 0U; i0 < array4d.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < cube.extent(0U); ++i1) {
      for (std::size_t i2 = 0U; i2 < cube.extent(1U); ++i2) {
        for (std::size_t i3 = 0U; i3 < cube.extent(2U); ++i3) {
          output(i0, i1, i2, i3) = cube(i1, i2, i3) * array4d(i0, i1, i2, i3);
        }
      }
    }
  }
}

template <typename LeftT, typename RightT, typename OutputT>
void reduce_conjugate_product(Array4DView<LeftT> left, Array4DView<RightT> right, CubeView<OutputT> output,
                              const Dim dim)
  requires(is_complex_v<LeftT> && is_complex_v<RightT> && is_complex_v<OutputT>)
{
  detail::validate_supported_dim(dim, Dim::dim0, "conjugate product reduction currently supports Dim::dim0");
  detail::validate_same_array4d_shape(left, right, "conjugate product reduction input shape mismatch");
  detail::validate_array4d_cube_tail_shape(left, output, "conjugate product reduction output shape mismatch");

  if (detail::prefer_eigen_reduce_conjugate_product(left, right, output, dim) &&
      detail::eigen::reduce_conjugate_product_contiguous(left, right, output, dim)) {
    return;
  }

  for (std::size_t i1 = 0U; i1 < output.extent(0U); ++i1) {
    for (std::size_t i2 = 0U; i2 < output.extent(1U); ++i2) {
      for (std::size_t i3 = 0U; i3 < output.extent(2U); ++i3) {
        std::remove_const_t<OutputT> sum{};
        for (std::size_t i0 = 0U; i0 < left.dim0(); ++i0) {
          sum += detail::multiply_conjugate<OutputT>(left(i0, i1, i2, i3), right(i0, i1, i2, i3));
        }
        output(i1, i2, i3) = sum;
      }
    }
  }
}

template <typename CubeT, typename Array4DT, typename OutputT>
void multiply_cube_by_abs_sum_squared(CubeView<CubeT> cube, Array4DView<Array4DT> array4d, CubeView<OutputT> output,
                                      const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  detail::validate_supported_dim(dim, Dim::dim0, "cube abs-sum-squared multiply currently supports Dim::dim0");
  detail::validate_array4d_cube_tail_shape(array4d, cube, "cube abs-sum-squared multiply input shape mismatch");
  detail::validate_same_cube_shape(cube, output, "cube abs-sum-squared multiply output shape mismatch");

  if (detail::prefer_eigen_cube_abs_sum_squared(cube, array4d, output, dim) &&
      detail::eigen::multiply_cube_by_abs_sum_squared_contiguous(cube, array4d, output, dim)) {
    return;
  }

  for (std::size_t i1 = 0U; i1 < cube.extent(0U); ++i1) {
    for (std::size_t i2 = 0U; i2 < cube.extent(1U); ++i2) {
      for (std::size_t i3 = 0U; i3 < cube.extent(2U); ++i3) {
        real_scalar_t<Array4DT> magnitude_sum{};
        for (std::size_t i0 = 0U; i0 < array4d.dim0(); ++i0) {
          magnitude_sum += std::abs(array4d(i0, i1, i2, i3));
        }
        output(i1, i2, i3) = cube(i1, i2, i3) * magnitude_sum * magnitude_sum;
      }
    }
  }
}

template <typename CubeT, typename Array4DT>
void multiply_cube_by_abs_sum_squared_in_place(CubeView<CubeT> cube, Array4DView<Array4DT> array4d, const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  multiply_cube_by_abs_sum_squared(cube, array4d, cube, dim);
}

template <typename InputT, typename MaskT>
void broadcast_abs_presence_mask(Array4DView<InputT> input, CubeView<MaskT> output, const Dim dim,
                                 const real_scalar_t<InputT> threshold) {
  detail::validate_supported_dim(dim, Dim::dim1, "abs presence mask broadcast currently supports Dim::dim1");
  detail::validate_array4d_cube_tail_shape(input, output, "abs presence mask output shape mismatch");

  for (std::size_t i1 = 0U; i1 < output.extent(0U); ++i1) {
    for (std::size_t i3 = 0U; i3 < output.extent(2U); ++i3) {
      bool detected = false;
      for (std::size_t i0 = 0U; i0 < input.dim0() && !detected; ++i0) {
        for (std::size_t i2 = 0U; i2 < output.extent(1U); ++i2) {
          if (std::abs(input(i0, i1, i2, i3)) > threshold) {
            detected = true;
            break;
          }
        }
      }
      const auto value = static_cast<std::remove_const_t<MaskT>>(detected ? 1 : 0);
      for (std::size_t i2 = 0U; i2 < output.extent(1U); ++i2) {
        output(i1, i2, i3) = value;
      }
    }
  }
}

template <typename SourceT, typename OutputT>
void copy_block_split(Array4DView<SourceT> source, const Dim block_dim, const std::size_t block_index,
                      const Dim split_dim, Array4DView<OutputT> output) {
  detail::validate_supported_dim(block_dim, Dim::dim1, "array4d block copy currently supports block Dim::dim1");
  detail::validate_supported_dim(split_dim, Dim::dim3, "array4d block copy currently supports split Dim::dim3");
  if (source.dim0() != output.dim0() || source.dim2() != output.dim1() ||
      source.dim3() != output.dim2() * output.dim3()) {
    throw std::invalid_argument("array4d block copy shape mismatch");
  }
  if (block_index >= source.dim1()) {
    throw std::out_of_range("array4d block copy index is out of range");
  }

  const auto output_block_size = output.dim1() * output.dim2() * output.dim3();
  if (source.is_contiguous() && output.is_contiguous()) {
    const auto* source_data = source.data();
    auto* output_data = output.data();
    const auto source_dim0_stride = source.dim1() * output_block_size;
    for (std::size_t i0 = 0U; i0 < output.dim0(); ++i0) {
      const auto* source_block = source_data + i0 * source_dim0_stride + block_index * output_block_size;
      auto* output_block = output_data + i0 * output_block_size;
      std::copy_n(source_block, output_block_size, output_block);
    }
    return;
  }

  for (std::size_t i0 = 0U; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < output.dim2(); ++i2) {
        for (std::size_t i3 = 0U; i3 < output.dim3(); ++i3) {
          output(i0, i1, i2, i3) = source(i0, block_index, i1, i2 * output.dim3() + i3);
        }
      }
    }
  }
}

template <typename InputT, typename OutputT>
void extract_sliding_patch_matrix(Array4DView<InputT> input, const std::size_t kernel_dim1,
                                  const std::size_t kernel_dim2, const std::size_t kernel_dim3,
                                  MatrixView<OutputT> output) {
  detail::validate_sliding_patch_matrix_shape(input, kernel_dim1, kernel_dim2, kernel_dim3, output);

  const auto dim0_count = input.dim0();
  const auto patch_dim1 = input.dim1() - kernel_dim1 + 1U;
  const auto patch_dim2 = input.dim2() - kernel_dim2 + 1U;
  const auto patch_dim3 = input.dim3() - kernel_dim3 + 1U;
  const auto kernel_size = kernel_dim1 * kernel_dim2 * kernel_dim3;

  if (detail::prefer_eigen_sliding_patch_matrix(input, output) &&
      detail::eigen::extract_sliding_patch_matrix_contiguous(input, kernel_dim1, kernel_dim2, kernel_dim3, output)) {
    return;
  }

  for (std::size_t kernel_i3 = 0U; kernel_i3 < kernel_dim3; ++kernel_i3) {
    for (std::size_t kernel_i2 = 0U; kernel_i2 < kernel_dim2; ++kernel_i2) {
      for (std::size_t kernel_i1 = 0U; kernel_i1 < kernel_dim1; ++kernel_i1) {
        const auto kernel_index = (kernel_i1 * kernel_dim2 + kernel_i2) * kernel_dim3 + kernel_i3;
        for (std::size_t i0 = 0U; i0 < dim0_count; ++i0) {
          const auto output_col = kernel_size * i0 + kernel_index;
          for (std::size_t patch_i1 = 0U; patch_i1 < patch_dim1; ++patch_i1) {
            for (std::size_t patch_i2 = 0U; patch_i2 < patch_dim2; ++patch_i2) {
              for (std::size_t patch_i3 = 0U; patch_i3 < patch_dim3; ++patch_i3) {
                const auto output_row = (patch_i1 * patch_dim2 + patch_i2) * patch_dim3 + patch_i3;
                output(output_row, output_col) =
                  input(i0, patch_i1 + kernel_i1, patch_i2 + kernel_i2, patch_i3 + kernel_i3);
              }
            }
          }
        }
      }
    }
  }
}

template <typename Array4DT, typename CubeT, typename OutputT>
void multiply_array4d_by_cube(const PooledArray4D<Array4DT>& array4d, const PooledCube<CubeT>& cube,
                              PooledArray4D<OutputT>& output) {
  multiply_array4d_by_cube(array4d.view(), cube.view(), output.view());
}

template <typename Array4DT, typename CubeT>
void multiply_array4d_by_cube_in_place(PooledArray4D<Array4DT>& array4d, const PooledCube<CubeT>& cube) {
  multiply_array4d_by_cube_in_place(array4d.view(), cube.view());
}

template <typename CubeT, typename Array4DT, typename OutputT>
void multiply_cube_by_array4d(const PooledCube<CubeT>& cube, const PooledArray4D<Array4DT>& array4d,
                              PooledArray4D<OutputT>& output) {
  multiply_cube_by_array4d(cube.view(), array4d.view(), output.view());
}

template <typename LeftT, typename RightT, typename OutputT>
void reduce_conjugate_product(const PooledArray4D<LeftT>& left, const PooledArray4D<RightT>& right,
                              PooledCube<OutputT>& output, const Dim dim)
  requires(is_complex_v<LeftT> && is_complex_v<RightT> && is_complex_v<OutputT>)
{
  reduce_conjugate_product(left.view(), right.view(), output.view(), dim);
}

template <typename CubeT, typename Array4DT, typename OutputT>
void multiply_cube_by_abs_sum_squared(const PooledCube<CubeT>& cube, const PooledArray4D<Array4DT>& array4d,
                                      PooledCube<OutputT>& output, const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  multiply_cube_by_abs_sum_squared(cube.view(), array4d.view(), output.view(), dim);
}

template <typename CubeT, typename Array4DT>
void multiply_cube_by_abs_sum_squared_in_place(PooledCube<CubeT>& cube, const PooledArray4D<Array4DT>& array4d,
                                               const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  multiply_cube_by_abs_sum_squared_in_place(cube.view(), array4d.view(), dim);
}

template <typename InputT, typename MaskT>
void broadcast_abs_presence_mask(const PooledArray4D<InputT>& input, PooledCube<MaskT>& output, const Dim dim,
                                 const real_scalar_t<InputT> threshold) {
  broadcast_abs_presence_mask(input.view(), output.view(), dim, threshold);
}

template <typename SourceT, typename OutputT>
void copy_block_split(const PooledArray4D<SourceT>& source, const Dim block_dim, const std::size_t block_index,
                      const Dim split_dim, PooledArray4D<OutputT>& output) {
  copy_block_split(source.view(), block_dim, block_index, split_dim, output.view());
}

template <typename InputT, typename OutputT>
void extract_sliding_patch_matrix(const PooledArray4D<InputT>& input, const std::size_t kernel_dim1,
                                  const std::size_t kernel_dim2, const std::size_t kernel_dim3,
                                  PooledMatrix<OutputT>& output) {
  extract_sliding_patch_matrix(input.view(), kernel_dim1, kernel_dim2, kernel_dim3, output.view());
}

template <typename Array4DT, typename CubeT>
[[nodiscard]] PooledArray4D<std::common_type_t<std::remove_const_t<Array4DT>, std::remove_const_t<CubeT>>>
multiply_array4d_by_cube(Array4DView<Array4DT> array4d, CubeView<CubeT> cube) {
  using output_type = std::common_type_t<std::remove_const_t<Array4DT>, std::remove_const_t<CubeT>>;
  auto output = make_pooled_array4d<output_type>(array4d.dim0(), array4d.dim1(), array4d.dim2(), array4d.dim3());
  multiply_array4d_by_cube(array4d, cube, output.view());
  return output;
}

template <typename Array4DT, typename CubeT>
[[nodiscard]] PooledArray4D<std::common_type_t<std::remove_const_t<Array4DT>, std::remove_const_t<CubeT>>>
multiply_array4d_by_cube(const PooledArray4D<Array4DT>& array4d, const PooledCube<CubeT>& cube) {
  return multiply_array4d_by_cube(array4d.view(), cube.view());
}

template <typename CubeT, typename Array4DT>
[[nodiscard]] PooledArray4D<std::common_type_t<std::remove_const_t<CubeT>, std::remove_const_t<Array4DT>>>
multiply_cube_by_array4d(CubeView<CubeT> cube, Array4DView<Array4DT> array4d) {
  using output_type = std::common_type_t<std::remove_const_t<CubeT>, std::remove_const_t<Array4DT>>;
  auto output = make_pooled_array4d<output_type>(array4d.dim0(), array4d.dim1(), array4d.dim2(), array4d.dim3());
  multiply_cube_by_array4d(cube, array4d, output.view());
  return output;
}

template <typename CubeT, typename Array4DT>
[[nodiscard]] PooledArray4D<std::common_type_t<std::remove_const_t<CubeT>, std::remove_const_t<Array4DT>>>
multiply_cube_by_array4d(const PooledCube<CubeT>& cube, const PooledArray4D<Array4DT>& array4d) {
  return multiply_cube_by_array4d(cube.view(), array4d.view());
}

template <typename LeftT, typename RightT>
[[nodiscard]] PooledCube<std::common_type_t<std::remove_const_t<LeftT>, std::remove_const_t<RightT>>>
reduce_conjugate_product(Array4DView<LeftT> left, Array4DView<RightT> right, const Dim dim)
  requires(is_complex_v<LeftT> && is_complex_v<RightT>)
{
  using output_type = std::common_type_t<std::remove_const_t<LeftT>, std::remove_const_t<RightT>>;
  static_assert(is_complex_v<output_type>, "reduce_conjugate_product requires complex output");
  auto output = make_pooled_cube<output_type>(left.dim1(), left.dim2(), left.dim3());
  reduce_conjugate_product(left, right, output.view(), dim);
  return output;
}

template <typename LeftT, typename RightT>
[[nodiscard]] PooledCube<std::common_type_t<std::remove_const_t<LeftT>, std::remove_const_t<RightT>>>
reduce_conjugate_product(const PooledArray4D<LeftT>& left, const PooledArray4D<RightT>& right, const Dim dim)
  requires(is_complex_v<LeftT> && is_complex_v<RightT>)
{
  return reduce_conjugate_product(left.view(), right.view(), dim);
}

template <typename CubeT, typename Array4DT>
[[nodiscard]] PooledCube<std::common_type_t<std::remove_const_t<CubeT>, real_scalar_t<Array4DT>>>
multiply_cube_by_abs_sum_squared(CubeView<CubeT> cube, Array4DView<Array4DT> array4d, const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  using output_type = std::common_type_t<std::remove_const_t<CubeT>, real_scalar_t<Array4DT>>;
  auto output = make_pooled_cube<output_type>(cube.dim0(), cube.dim1(), cube.dim2());
  multiply_cube_by_abs_sum_squared(cube, array4d, output.view(), dim);
  return output;
}

template <typename CubeT, typename Array4DT>
[[nodiscard]] PooledCube<std::common_type_t<std::remove_const_t<CubeT>, real_scalar_t<Array4DT>>>
multiply_cube_by_abs_sum_squared(const PooledCube<CubeT>& cube, const PooledArray4D<Array4DT>& array4d, const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  return multiply_cube_by_abs_sum_squared(cube.view(), array4d.view(), dim);
}

template <typename MaskT = std::uint8_t, typename InputT>
[[nodiscard]] PooledCube<MaskT> broadcast_abs_presence_mask(Array4DView<InputT> input, const Dim dim,
                                                            const real_scalar_t<InputT> threshold) {
  auto output = make_pooled_cube<MaskT>(input.dim1(), input.dim2(), input.dim3());
  broadcast_abs_presence_mask(input, output.view(), dim, threshold);
  return output;
}

template <typename MaskT = std::uint8_t, typename InputT>
[[nodiscard]] PooledCube<MaskT> broadcast_abs_presence_mask(const PooledArray4D<InputT>& input, const Dim dim,
                                                            const real_scalar_t<InputT> threshold) {
  return broadcast_abs_presence_mask<MaskT>(input.view(), dim, threshold);
}

template <typename InputT>
[[nodiscard]] PooledMatrix<std::remove_const_t<InputT>>
extract_sliding_patch_matrix(Array4DView<InputT> input, const std::size_t kernel_dim1, const std::size_t kernel_dim2,
                             const std::size_t kernel_dim3) {
  const auto shape = detail::sliding_patch_matrix_shape(input, kernel_dim1, kernel_dim2, kernel_dim3);
  auto output = make_pooled_matrix<std::remove_const_t<InputT>>(shape.rows, shape.cols);
  extract_sliding_patch_matrix(input, kernel_dim1, kernel_dim2, kernel_dim3, output.view());
  return output;
}

template <typename InputT>
[[nodiscard]] PooledMatrix<InputT>
extract_sliding_patch_matrix(const PooledArray4D<InputT>& input, const std::size_t kernel_dim1,
                             const std::size_t kernel_dim2, const std::size_t kernel_dim3) {
  return extract_sliding_patch_matrix(input.view(), kernel_dim1, kernel_dim2, kernel_dim3);
}

} // namespace ksj::array
