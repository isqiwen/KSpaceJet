#pragma once

#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/scalar_traits.hpp"
#include "kspacejet/array/indexing.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <type_traits>

namespace ksj::array::detail::eigen {

#if defined(_MSC_VER)
#define KSJ_ARRAY_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define KSJ_ARRAY_RESTRICT __restrict__
#else
#define KSJ_ARRAY_RESTRICT
#endif

template <typename Array4DT, typename CubeT, typename OutputT>
[[nodiscard]] bool multiply_array4d_by_cube_contiguous(Array4DView<Array4DT> array4d, CubeView<CubeT> cube,
                                                       Array4DView<OutputT> output) {
  if (!array4d.is_contiguous() || !cube.is_contiguous() || !output.is_contiguous()) {
    return false;
  }

  const auto dim0_count = array4d.dim0();
  const auto cube_size = cube.size();
  const auto* KSJ_ARRAY_RESTRICT array4d_data = array4d.data();
  const auto* KSJ_ARRAY_RESTRICT cube_data = cube.data();
  auto* KSJ_ARRAY_RESTRICT output_data = output.data();

  for (std::size_t i0 = 0U; i0 < dim0_count; ++i0) {
    const auto dim0_offset = i0 * cube_size;
    const auto* array4d_ptr = array4d_data + dim0_offset;
    auto* output_ptr = output_data + dim0_offset;
    for (std::size_t index = 0U; index < cube_size; ++index) {
      output_ptr[index] = array4d_ptr[index] * cube_data[index];
    }
  }
  return true;
}

template <typename CubeT, typename Array4DT, typename OutputT>
[[nodiscard]] bool multiply_cube_by_array4d_contiguous(CubeView<CubeT> cube, Array4DView<Array4DT> array4d,
                                                       Array4DView<OutputT> output) {
  if (!cube.is_contiguous() || !array4d.is_contiguous() || !output.is_contiguous()) {
    return false;
  }

  const auto dim0_count = array4d.dim0();
  const auto cube_size = cube.size();
  const auto* KSJ_ARRAY_RESTRICT cube_data = cube.data();
  const auto* KSJ_ARRAY_RESTRICT array4d_data = array4d.data();
  auto* KSJ_ARRAY_RESTRICT output_data = output.data();

  for (std::size_t i0 = 0U; i0 < dim0_count; ++i0) {
    const auto dim0_offset = i0 * cube_size;
    const auto* array4d_ptr = array4d_data + dim0_offset;
    auto* output_ptr = output_data + dim0_offset;
    for (std::size_t index = 0U; index < cube_size; ++index) {
      output_ptr[index] = cube_data[index] * array4d_ptr[index];
    }
  }
  return true;
}

template <typename LeftT, typename RightT, typename OutputT>
[[nodiscard]] bool reduce_conjugate_product_contiguous(Array4DView<LeftT> left, Array4DView<RightT> right,
                                                       CubeView<OutputT> output, const Dim dim)
  requires(is_complex_v<LeftT> && is_complex_v<RightT> && is_complex_v<OutputT>)
{
  if (dim != Dim::dim0) {
    return false;
  }
  if (!left.is_contiguous() || !right.is_contiguous() || !output.is_contiguous()) {
    return false;
  }

  const auto dim0_count = left.dim0();
  const auto cube_size = output.size();
  const auto* KSJ_ARRAY_RESTRICT left_data = left.data();
  const auto* KSJ_ARRAY_RESTRICT right_data = right.data();
  auto* KSJ_ARRAY_RESTRICT output_data = output.data();

  std::fill_n(output_data, cube_size, std::remove_const_t<OutputT>{});
  for (std::size_t i0 = 0U; i0 < dim0_count; ++i0) {
    const auto dim0_offset = i0 * cube_size;
    const auto* left_ptr = left_data + dim0_offset;
    const auto* right_ptr = right_data + dim0_offset;
    for (std::size_t index = 0U; index < cube_size; ++index) {
      const auto& left_value = left_ptr[index];
      const auto& right_value = right_ptr[index];
      output_data[index] +=
        std::remove_const_t<OutputT>{left_value.real() * right_value.real() + left_value.imag() * right_value.imag(),
                                     left_value.imag() * right_value.real() - left_value.real() * right_value.imag()};
    }
  }
  return true;
}

template <typename CubeT, typename Array4DT, typename OutputT>
[[nodiscard]] bool multiply_cube_by_abs_sum_squared_contiguous(CubeView<CubeT> cube, Array4DView<Array4DT> array4d,
                                                               CubeView<OutputT> output, const Dim dim)
  requires(is_complex_v<Array4DT>)
{
  if (dim != Dim::dim0) {
    return false;
  }
  if (!cube.is_contiguous() || !array4d.is_contiguous() || !output.is_contiguous()) {
    return false;
  }

  const auto dim0_count = array4d.dim0();
  const auto cube_size = cube.size();
  const auto* KSJ_ARRAY_RESTRICT cube_data = cube.data();
  const auto* KSJ_ARRAY_RESTRICT array4d_data = array4d.data();
  auto* KSJ_ARRAY_RESTRICT output_data = output.data();

  for (std::size_t index = 0U; index < cube_size; ++index) {
    real_scalar_t<Array4DT> magnitude_sum{};
    for (std::size_t i0 = 0U; i0 < dim0_count; ++i0) {
      magnitude_sum += std::abs(array4d_data[i0 * cube_size + index]);
    }
    output_data[index] = cube_data[index] * magnitude_sum * magnitude_sum;
  }
  return true;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool extract_sliding_patch_matrix_contiguous(Array4DView<InputT> input, const std::size_t kernel_dim1,
                                                           const std::size_t kernel_dim2, const std::size_t kernel_dim3,
                                                           MatrixView<OutputT> output) {
  if (!input.is_contiguous() || !output.is_contiguous()) {
    return false;
  }

  const auto dim0_count = input.dim0();
  const auto input_dim1 = input.dim1();
  const auto input_dim2 = input.dim2();
  const auto input_dim3 = input.dim3();
  const auto patch_dim1 = input_dim1 - kernel_dim1 + 1U;
  const auto patch_dim2 = input_dim2 - kernel_dim2 + 1U;
  const auto patch_dim3 = input_dim3 - kernel_dim3 + 1U;
  const auto kernel_size = kernel_dim1 * kernel_dim2 * kernel_dim3;
  const auto output_cols = output.cols();
  const auto cube_size = input_dim1 * input_dim2 * input_dim3;

  const auto* KSJ_ARRAY_RESTRICT input_data = input.data();
  auto* KSJ_ARRAY_RESTRICT output_data = output.data();

  for (std::size_t patch_i1 = 0U; patch_i1 < patch_dim1; ++patch_i1) {
    for (std::size_t patch_i2 = 0U; patch_i2 < patch_dim2; ++patch_i2) {
      for (std::size_t patch_i3 = 0U; patch_i3 < patch_dim3; ++patch_i3) {
        const auto output_row = (patch_i1 * patch_dim2 + patch_i2) * patch_dim3 + patch_i3;
        auto* KSJ_ARRAY_RESTRICT output_row_data = output_data + output_row * output_cols;
        for (std::size_t i0 = 0U; i0 < dim0_count; ++i0) {
          const auto* KSJ_ARRAY_RESTRICT input_dim0_data = input_data + i0 * cube_size;
          auto* KSJ_ARRAY_RESTRICT output_dim0_data = output_row_data + i0 * kernel_size;
          for (std::size_t kernel_i1 = 0U; kernel_i1 < kernel_dim1; ++kernel_i1) {
            for (std::size_t kernel_i2 = 0U; kernel_i2 < kernel_dim2; ++kernel_i2) {
              const auto input_index =
                ((patch_i1 + kernel_i1) * input_dim2 + patch_i2 + kernel_i2) * input_dim3 + patch_i3;
              const auto output_index = (kernel_i1 * kernel_dim2 + kernel_i2) * kernel_dim3;
              for (std::size_t kernel_i3 = 0U; kernel_i3 < kernel_dim3; ++kernel_i3) {
                output_dim0_data[output_index + kernel_i3] = input_dim0_data[input_index + kernel_i3];
              }
            }
          }
        }
      }
    }
  }
  return true;
}

#undef KSJ_ARRAY_RESTRICT

} // namespace ksj::array::detail::eigen
