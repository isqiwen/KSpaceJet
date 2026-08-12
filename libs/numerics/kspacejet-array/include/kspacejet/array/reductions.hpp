#pragma once

/// Scalar and dimension-wise reductions, norms, extrema, and index-returning reduction operations.

#include "kspacejet/array/detail/array_policy.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_reductions.hpp"
#include "kspacejet/array/detail/intel/intel_array_reductions.hpp"
#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/scalar_traits.hpp"
#include "kspacejet/array/indexing.hpp"
#include "kspacejet/array/iteration.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

enum class MatrixDifferenceAxis {
  row,
  column,
};

template <typename T> struct ForwardDifferenceStats {
  T sum{};
  std::size_t count{};

  [[nodiscard]] T mean() const noexcept { return sum / static_cast<T>(count); }
};

template <typename LhsT, typename RhsT, typename OutputT>
void sum_product_across(CubeView<LhsT> lhs, CubeView<RhsT> rhs, MatrixView<OutputT> output, const Dim dim) {
  detail::validate_supported_dim(dim, Dim::dim2, "cube view sum_product_across currently supports Dim::dim2");
  detail::validate_same_cube_shape(lhs, rhs, "cube view sum_product_across input shape mismatch");
  if (output.rows() != lhs.dim0() || output.cols() != lhs.dim1()) {
    throw std::invalid_argument("cube view sum_product_across output shape mismatch");
  }

  using sum_type =
    std::common_type_t<std::remove_const_t<LhsT>, std::remove_const_t<RhsT>, std::remove_const_t<OutputT>>;
  if (lhs.is_contiguous() && rhs.is_contiguous() && output.is_contiguous()) {
    const auto* lhs_data = lhs.data();
    const auto* rhs_data = rhs.data();
    auto* output_data = output.data();
    const auto dim01_count = lhs.dim0() * lhs.dim1();
    const auto dim2_count = lhs.dim2();
    for (std::size_t dim01 = 0U; dim01 < dim01_count; ++dim01) {
      sum_type sum{};
      const auto dim01_offset = dim01 * dim2_count;
      for (std::size_t i2 = 0U; i2 < dim2_count; ++i2) {
        const auto index = dim01_offset + i2;
        sum += lhs_data[index] * rhs_data[index];
      }
      output_data[dim01] = static_cast<std::remove_const_t<OutputT>>(sum);
    }
    return;
  }

  for (std::size_t i0 = 0; i0 < lhs.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < lhs.dim1(); ++i1) {
      sum_type sum{};
      for (std::size_t i2 = 0; i2 < lhs.dim2(); ++i2) {
        sum += lhs(i0, i1, i2) * rhs(i0, i1, i2);
      }
      output(i0, i1) = static_cast<std::remove_const_t<OutputT>>(sum);
    }
  }
}

template <typename LhsT, typename RhsT>
using sum_product_result_t =
  std::remove_cvref_t<decltype(std::declval<std::remove_const_t<LhsT>>() * std::declval<std::remove_const_t<RhsT>>())>;

template <typename LhsT, typename RhsT>
[[nodiscard]] PooledMatrix<sum_product_result_t<LhsT, RhsT>> sum_product_across(CubeView<LhsT> lhs, CubeView<RhsT> rhs,
                                                                                const Dim dim) {
  detail::validate_supported_dim(dim, Dim::dim2, "cube view sum_product_across currently supports Dim::dim2");
  auto output = PooledMatrix<sum_product_result_t<LhsT, RhsT>>(lhs.dim0(), lhs.dim1());
  sum_product_across(lhs, rhs, output.view(), dim);
  return output;
}

template <typename LhsT, typename RhsT>
[[nodiscard]] std::size_t count_not_equal(CubeView<LhsT> lhs, CubeView<RhsT> rhs) {
  detail::validate_same_cube_shape(lhs, rhs, "cube view count_not_equal shape mismatch");

  std::size_t count = 0U;
  for_each_zip(lhs, rhs, [&count](const auto& lhs_value, const auto& rhs_value) {
    if (lhs_value != rhs_value) {
      ++count;
    }
  });
  return count;
}

template <typename T, typename MaskT>
[[nodiscard]] ForwardDifferenceStats<std::remove_const_t<T>>
forward_difference_stats_below_threshold(MatrixView<T> input, MatrixView<MaskT> mask, const MatrixDifferenceAxis axis,
                                         const std::remove_const_t<T> threshold) {
  detail::validate_same_shape(input, mask, "matrix view forward difference mask shape mismatch");

  ForwardDifferenceStats<std::remove_const_t<T>> stats{};
  if (axis == MatrixDifferenceAxis::row) {
    if (input.is_contiguous() && mask.is_contiguous()) {
      const auto* input_data = input.data();
      const auto* mask_data = mask.data();
      for (std::size_t row = 1U; row < input.rows(); ++row) {
        const auto row_offset = row * input.cols();
        const auto previous_row_offset = row_offset - input.cols();
        for (std::size_t col = 0U; col < input.cols(); ++col) {
          const auto index = row_offset + col;
          const auto difference = input_data[index] - input_data[previous_row_offset + col];
          using std::abs;
          if (abs(difference) < threshold && mask_data[index] > MaskT{}) {
            stats.sum += difference;
            ++stats.count;
          }
        }
      }
      return stats;
    }

    for (std::size_t row = 1U; row < input.rows(); ++row) {
      for (std::size_t col = 0U; col < input.cols(); ++col) {
        const auto difference = input(row, col) - input(row - 1U, col);
        using std::abs;
        if (abs(difference) < threshold && mask(row, col) > MaskT{}) {
          stats.sum += difference;
          ++stats.count;
        }
      }
    }
    return stats;
  }

  if (input.is_contiguous() && mask.is_contiguous()) {
    const auto* input_data = input.data();
    const auto* mask_data = mask.data();
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      const auto row_offset = row * input.cols();
      for (std::size_t col = 1U; col < input.cols(); ++col) {
        const auto index = row_offset + col;
        const auto difference = input_data[index] - input_data[index - 1U];
        using std::abs;
        if (abs(difference) < threshold && mask_data[index] > MaskT{}) {
          stats.sum += difference;
          ++stats.count;
        }
      }
    }
    return stats;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 1U; col < input.cols(); ++col) {
      const auto difference = input(row, col) - input(row, col - 1U);
      using std::abs;
      if (abs(difference) < threshold && mask(row, col) > MaskT{}) {
        stats.sum += difference;
        ++stats.count;
      }
    }
  }
  return stats;
}

template <typename T, typename IndexT>
void argmin_groups(CubeView<T> input, const std::size_t group_count, CubeView<IndexT> output, const Dim dim) {
  detail::validate_supported_dim(dim, Dim::dim2, "cube view argmin_groups currently supports Dim::dim2");
  if (group_count == 0U) {
    throw std::invalid_argument("cube view argmin_groups group_count must be nonzero");
  }
  if (input.dim2() % group_count != 0U) {
    throw std::invalid_argument("cube view argmin_groups input extent must be divisible by group_count");
  }

  const auto grouped_dim2 = input.dim2() / group_count;
  if (output.dim0() != input.dim0() || output.dim1() != input.dim1() || output.dim2() != grouped_dim2) {
    throw std::invalid_argument("cube view argmin_groups output shape mismatch");
  }

  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    const auto dim01_count = input.dim0() * input.dim1();
    const auto input_dim2_count = input.dim2();
    for (std::size_t dim01 = 0U; dim01 < dim01_count; ++dim01) {
      const auto* input_dim01 = input_data + dim01 * input_dim2_count;
      auto* output_dim01 = output_data + dim01 * grouped_dim2;
      for (std::size_t i2 = 0U; i2 < grouped_dim2; ++i2) {
        std::size_t best_group = 0U;
        auto best_value = input_dim01[i2];
        for (std::size_t group = 1U; group < group_count; ++group) {
          const auto value = input_dim01[i2 + group * grouped_dim2];
          if (value < best_value) {
            best_value = value;
            best_group = group;
          }
        }
        output_dim01[i2] = static_cast<std::remove_const_t<IndexT>>(best_group);
      }
    }
    return;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < grouped_dim2; ++i2) {
        std::size_t best_group = 0U;
        auto best_value = input(i0, i1, i2);
        for (std::size_t group = 1U; group < group_count; ++group) {
          const auto value = input(i0, i1, i2 + group * grouped_dim2);
          if (value < best_value) {
            best_value = value;
            best_group = group;
          }
        }
        output(i0, i1, i2) = static_cast<std::remove_const_t<IndexT>>(best_group);
      }
    }
  }
}

template <typename IndexT = std::size_t, typename T>
[[nodiscard]] PooledCube<IndexT> argmin_groups(CubeView<T> input, const std::size_t group_count, const Dim dim) {
  detail::validate_supported_dim(dim, Dim::dim2, "cube view argmin_groups currently supports Dim::dim2");
  if (group_count == 0U) {
    throw std::invalid_argument("cube view argmin_groups group_count must be nonzero");
  }
  if (input.dim2() % group_count != 0U) {
    throw std::invalid_argument("cube view argmin_groups input extent must be divisible by group_count");
  }

  auto output = PooledCube<IndexT>(input.dim0(), input.dim1(), input.dim2() / group_count);
  argmin_groups(input, group_count, output.view(), dim);
  return output;
}

namespace detail {

template <typename T, typename OutputT>
void validate_matrix_dim_output(MatrixView<T> input, VectorView<OutputT> output, const Dim dim, const char* message) {
  validate_rank_dim(dim, 2U, message);
  const auto expected = dim == Dim::dim0 ? input.cols() : input.rows();
  if (output.size() != expected) {
    throw std::invalid_argument(message);
  }
}

template <typename T, typename OutputT>
void validate_cube_dim_output(CubeView<T> input, MatrixView<OutputT> output, const Dim dim, const char* message) {
  validate_rank_dim(dim, 3U, message);
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  if (output.rows() != rows || output.cols() != cols) {
    throw std::invalid_argument(message);
  }
}

} // namespace detail

template <typename T, typename OutputT> void sum(MatrixView<T> input, VectorView<OutputT> output, const Dim dim) {
  detail::validate_matrix_dim_output(input, output, dim, "matrix sum output shape mismatch");
  if (dim == Dim::dim0) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      std::remove_const_t<OutputT> total{};
      for (std::size_t row = 0U; row < input.rows(); ++row) {
        total += input(row, col);
      }
      output(col) = total;
    }
    return;
  }

  if (input.col_stride() == 1U) {
    std::size_t row = 0U;
    for_each_contiguous_block(input, [&](const auto block) {
      std::remove_const_t<OutputT> total{};
      for (const auto& value : block) {
        total += value;
      }
      output(row++) = total;
    });
    return;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    std::remove_const_t<OutputT> total{};
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      total += input(row, col);
    }
    output(row) = total;
  }
}

template <typename T, typename OutputT> void sum(CubeView<T> input, MatrixView<OutputT> output, const Dim dim) {
  detail::validate_cube_dim_output(input, output, dim, "cube sum output shape mismatch");
  switch (dim) {
    case Dim::dim0:
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          std::remove_const_t<OutputT> total{};
          for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
            total += input(i0, i1, i2);
          }
          output(i1, i2) = total;
        }
      }
      return;
    case Dim::dim1:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          std::remove_const_t<OutputT> total{};
          for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
            total += input(i0, i1, i2);
          }
          output(i0, i2) = total;
        }
      }
      return;
    case Dim::dim2:
      if (input.dim2_stride() == 1U) {
        std::size_t output_index = 0U;
        for_each_contiguous_block(input, [&](const auto block) {
          std::remove_const_t<OutputT> total{};
          for (const auto& value : block) {
            total += value;
          }
          output[output_index++] = total;
        });
        return;
      }

      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          std::remove_const_t<OutputT> total{};
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            total += input(i0, i1, i2);
          }
          output(i0, i1) = total;
        }
      }
      return;
    default:
      throw std::out_of_range("cube sum dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  }
}

template <typename T, typename OutputT, typename Compare>
void reduce_extreme(MatrixView<T> input, VectorView<OutputT> output, const Dim dim, Compare compare,
                    const char* message) {
  if (input.empty()) {
    throw std::invalid_argument("matrix extreme reduction input must be non-empty");
  }
  detail::validate_matrix_dim_output(input, output, dim, message);
  if (dim == Dim::dim0) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      auto best = input(0U, col);
      for (std::size_t row = 1U; row < input.rows(); ++row) {
        const auto value = input(row, col);
        if (compare(value, best)) {
          best = value;
        }
      }
      output(col) = best;
    }
    return;
  }

  if (input.col_stride() == 1U) {
    std::size_t row = 0U;
    for_each_contiguous_block(input, [&](const auto block) {
      auto best = block[0U];
      for (std::size_t index = 1U; index < block.size(); ++index) {
        const auto value = block[index];
        if (compare(value, best)) {
          best = value;
        }
      }
      output(row++) = best;
    });
    return;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    auto best = input(row, 0U);
    for (std::size_t col = 1U; col < input.cols(); ++col) {
      const auto value = input(row, col);
      if (compare(value, best)) {
        best = value;
      }
    }
    output(row) = best;
  }
}

template <typename T, typename OutputT, typename Compare>
void reduce_extreme(CubeView<T> input, MatrixView<OutputT> output, const Dim dim, Compare compare,
                    const char* message) {
  if (input.empty()) {
    throw std::invalid_argument("cube extreme reduction input must be non-empty");
  }
  detail::validate_cube_dim_output(input, output, dim, message);
  switch (dim) {
    case Dim::dim0:
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          auto best = input(0U, i1, i2);
          for (std::size_t i0 = 1U; i0 < input.dim0(); ++i0) {
            const auto value = input(i0, i1, i2);
            if (compare(value, best)) {
              best = value;
            }
          }
          output(i1, i2) = best;
        }
      }
      return;
    case Dim::dim1:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          auto best = input(i0, 0U, i2);
          for (std::size_t i1 = 1U; i1 < input.dim1(); ++i1) {
            const auto value = input(i0, i1, i2);
            if (compare(value, best)) {
              best = value;
            }
          }
          output(i0, i2) = best;
        }
      }
      return;
    case Dim::dim2:
      if (input.dim2_stride() == 1U) {
        std::size_t output_index = 0U;
        for_each_contiguous_block(input, [&](const auto block) {
          auto best = block[0U];
          for (std::size_t index = 1U; index < block.size(); ++index) {
            const auto value = block[index];
            if (compare(value, best)) {
              best = value;
            }
          }
          output[output_index++] = best;
        });
        return;
      }

      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          auto best = input(i0, i1, 0U);
          for (std::size_t i2 = 1U; i2 < input.dim2(); ++i2) {
            const auto value = input(i0, i1, i2);
            if (compare(value, best)) {
              best = value;
            }
          }
          output(i0, i1) = best;
        }
      }
      return;
    default:
      throw std::out_of_range("cube extreme reduction dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  }
}

template <typename T, typename OutputT> void min(MatrixView<T> input, VectorView<OutputT> output, const Dim dim) {
  reduce_extreme(input, output, dim, std::less<typename MatrixView<T>::value_type>{},
                 "matrix min output shape mismatch");
}

template <typename T, typename OutputT> void max(MatrixView<T> input, VectorView<OutputT> output, const Dim dim) {
  reduce_extreme(input, output, dim, std::greater<typename MatrixView<T>::value_type>{},
                 "matrix max output shape mismatch");
}

template <typename T, typename OutputT> void min(CubeView<T> input, MatrixView<OutputT> output, const Dim dim) {
  reduce_extreme(input, output, dim, std::less<typename CubeView<T>::value_type>{}, "cube min output shape mismatch");
}

template <typename T, typename OutputT> void max(CubeView<T> input, MatrixView<OutputT> output, const Dim dim) {
  reduce_extreme(input, output, dim, std::greater<typename CubeView<T>::value_type>{},
                 "cube max output shape mismatch");
}

template <typename T, typename OutputT, typename Compare>
void reduce_extreme_index(MatrixView<T> input, VectorView<OutputT> output, const Dim dim, Compare compare,
                          const char* message) {
  if (input.empty()) {
    throw std::invalid_argument("matrix arg reduction input must be non-empty");
  }
  detail::validate_matrix_dim_output(input, output, dim, message);
  if (dim == Dim::dim0) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      std::size_t best_index = 0U;
      auto best = input(0U, col);
      for (std::size_t row = 1U; row < input.rows(); ++row) {
        const auto value = input(row, col);
        if (compare(value, best)) {
          best = value;
          best_index = row;
        }
      }
      output(col) = static_cast<std::remove_const_t<OutputT>>(best_index);
    }
    return;
  }

  if (input.col_stride() == 1U) {
    std::size_t row = 0U;
    for_each_contiguous_block(input, [&](const auto block) {
      std::size_t best_index = 0U;
      auto best = block[0U];
      for (std::size_t index = 1U; index < block.size(); ++index) {
        const auto value = block[index];
        if (compare(value, best)) {
          best = value;
          best_index = index;
        }
      }
      output(row++) = static_cast<std::remove_const_t<OutputT>>(best_index);
    });
    return;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    std::size_t best_index = 0U;
    auto best = input(row, 0U);
    for (std::size_t col = 1U; col < input.cols(); ++col) {
      const auto value = input(row, col);
      if (compare(value, best)) {
        best = value;
        best_index = col;
      }
    }
    output(row) = static_cast<std::remove_const_t<OutputT>>(best_index);
  }
}

template <typename T, typename OutputT, typename Compare>
void reduce_extreme_index(CubeView<T> input, MatrixView<OutputT> output, const Dim dim, Compare compare,
                          const char* message) {
  if (input.empty()) {
    throw std::invalid_argument("cube arg reduction input must be non-empty");
  }
  detail::validate_cube_dim_output(input, output, dim, message);
  switch (dim) {
    case Dim::dim0:
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          std::size_t best_index = 0U;
          auto best = input(0U, i1, i2);
          for (std::size_t i0 = 1U; i0 < input.dim0(); ++i0) {
            const auto value = input(i0, i1, i2);
            if (compare(value, best)) {
              best = value;
              best_index = i0;
            }
          }
          output(i1, i2) = static_cast<std::remove_const_t<OutputT>>(best_index);
        }
      }
      return;
    case Dim::dim1:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          std::size_t best_index = 0U;
          auto best = input(i0, 0U, i2);
          for (std::size_t i1 = 1U; i1 < input.dim1(); ++i1) {
            const auto value = input(i0, i1, i2);
            if (compare(value, best)) {
              best = value;
              best_index = i1;
            }
          }
          output(i0, i2) = static_cast<std::remove_const_t<OutputT>>(best_index);
        }
      }
      return;
    case Dim::dim2:
      if (input.dim2_stride() == 1U) {
        std::size_t output_index = 0U;
        for_each_contiguous_block(input, [&](const auto block) {
          std::size_t best_index = 0U;
          auto best = block[0U];
          for (std::size_t index = 1U; index < block.size(); ++index) {
            const auto value = block[index];
            if (compare(value, best)) {
              best = value;
              best_index = index;
            }
          }
          output[output_index++] = static_cast<std::remove_const_t<OutputT>>(best_index);
        });
        return;
      }

      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          std::size_t best_index = 0U;
          auto best = input(i0, i1, 0U);
          for (std::size_t i2 = 1U; i2 < input.dim2(); ++i2) {
            const auto value = input(i0, i1, i2);
            if (compare(value, best)) {
              best = value;
              best_index = i2;
            }
          }
          output(i0, i1) = static_cast<std::remove_const_t<OutputT>>(best_index);
        }
      }
      return;
    default:
      throw std::out_of_range("cube arg reduction dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  }
}

template <typename T, typename OutputT> void argmin(MatrixView<T> input, VectorView<OutputT> output, const Dim dim) {
  reduce_extreme_index(input, output, dim, std::less<typename MatrixView<T>::value_type>{},
                       "matrix argmin output shape mismatch");
}

template <typename T, typename OutputT> void argmax(MatrixView<T> input, VectorView<OutputT> output, const Dim dim) {
  reduce_extreme_index(input, output, dim, std::greater<typename MatrixView<T>::value_type>{},
                       "matrix argmax output shape mismatch");
}

template <typename T, typename OutputT> void argmin(CubeView<T> input, MatrixView<OutputT> output, const Dim dim) {
  reduce_extreme_index(input, output, dim, std::less<typename CubeView<T>::value_type>{},
                       "cube argmin output shape mismatch");
}

template <typename T, typename OutputT> void argmax(CubeView<T> input, MatrixView<OutputT> output, const Dim dim) {
  reduce_extreme_index(input, output, dim, std::greater<typename CubeView<T>::value_type>{},
                       "cube argmax output shape mismatch");
}

template <typename T> [[nodiscard]] PooledVector<std::remove_const_t<T>> sum(MatrixView<T> input, const Dim dim) {
  auto output = PooledVector<std::remove_const_t<T>>(dim == Dim::dim0 ? input.cols() : input.rows());
  sum(input, output.view(), dim);
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<std::remove_const_t<T>> sum(CubeView<T> input, const Dim dim) {
  detail::validate_rank_dim(dim, 3U, "cube sum dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  auto output = PooledMatrix<std::remove_const_t<T>>(rows, cols);
  sum(input, output.view(), dim);
  return output;
}

template <typename T> [[nodiscard]] PooledVector<std::remove_const_t<T>> min(MatrixView<T> input, const Dim dim) {
  auto output = PooledVector<std::remove_const_t<T>>(dim == Dim::dim0 ? input.cols() : input.rows());
  min(input, output.view(), dim);
  return output;
}

template <typename T> [[nodiscard]] PooledVector<std::remove_const_t<T>> max(MatrixView<T> input, const Dim dim) {
  auto output = PooledVector<std::remove_const_t<T>>(dim == Dim::dim0 ? input.cols() : input.rows());
  max(input, output.view(), dim);
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<std::remove_const_t<T>> min(CubeView<T> input, const Dim dim) {
  detail::validate_rank_dim(dim, 3U, "cube min dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  auto output = PooledMatrix<std::remove_const_t<T>>(rows, cols);
  min(input, output.view(), dim);
  return output;
}

template <typename T> [[nodiscard]] PooledMatrix<std::remove_const_t<T>> max(CubeView<T> input, const Dim dim) {
  detail::validate_rank_dim(dim, 3U, "cube max dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  auto output = PooledMatrix<std::remove_const_t<T>>(rows, cols);
  max(input, output.view(), dim);
  return output;
}

template <typename IndexT = std::size_t, typename T>
[[nodiscard]] PooledVector<IndexT> argmin(MatrixView<T> input, const Dim dim) {
  auto output = PooledVector<IndexT>(dim == Dim::dim0 ? input.cols() : input.rows());
  argmin(input, output.view(), dim);
  return output;
}

template <typename IndexT = std::size_t, typename T>
[[nodiscard]] PooledVector<IndexT> argmax(MatrixView<T> input, const Dim dim) {
  auto output = PooledVector<IndexT>(dim == Dim::dim0 ? input.cols() : input.rows());
  argmax(input, output.view(), dim);
  return output;
}

template <typename IndexT = std::size_t, typename T>
[[nodiscard]] PooledMatrix<IndexT> argmin(CubeView<T> input, const Dim dim) {
  detail::validate_rank_dim(dim, 3U, "cube argmin dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  auto output = PooledMatrix<IndexT>(rows, cols);
  argmin(input, output.view(), dim);
  return output;
}

template <typename IndexT = std::size_t, typename T>
[[nodiscard]] PooledMatrix<IndexT> argmax(CubeView<T> input, const Dim dim) {
  detail::validate_rank_dim(dim, 3U, "cube argmax dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  auto output = PooledMatrix<IndexT>(rows, cols);
  argmax(input, output.view(), dim);
  return output;
}

namespace detail {

template <typename T, typename Acc> [[nodiscard]] bool sum_vector_backend(VectorView<T> input, Acc& init) {
  using input_value_type = std::remove_const_t<T>;
  using acc_type = std::remove_cv_t<Acc>;
  if constexpr (std::is_same_v<input_value_type, acc_type>) {
    auto backend_sum = acc_type{};
    const auto const_input = as_const_view(input);
    if (prefer_intel_vector_sum(input) && intel::sum(const_input, backend_sum)) {
      init += backend_sum;
      return true;
    }
    if (prefer_eigen_vector_sum(input) && eigen::sum(const_input, backend_sum)) {
      init += backend_sum;
      return true;
    }
  }
  return false;
}

template <typename T> [[nodiscard]] bool min_vector_backend(VectorView<T> input, std::remove_const_t<T>& output) {
  const auto const_input = as_const_view(input);
  if (prefer_intel_vector_reduction_minmax(input) && intel::min(const_input, output)) {
    return true;
  }
  return false;
}

template <typename T> [[nodiscard]] bool max_vector_backend(VectorView<T> input, std::remove_const_t<T>& output) {
  const auto const_input = as_const_view(input);
  if (prefer_intel_vector_reduction_minmax(input) && intel::max(const_input, output)) {
    return true;
  }
  return false;
}

} // namespace detail

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(VectorView<T> input, Acc init) {
  if (detail::sum_vector_backend(input, init)) {
    return init;
  }
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(MatrixView<T> input, Acc init) {
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(ImageView<T> input, Acc init) {
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(CubeView<T> input, Acc init) {
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(Array4DView<T> input, Acc init) {
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(VectorView<T> input, Acc init, Predicate predicate) {
  for_each(input, [&init, &predicate](const auto& value) {
    if (predicate(value)) {
      init += value;
    }
  });
  return init;
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(MatrixView<T> input, Acc init, Predicate predicate) {
  for_each(input, [&init, &predicate](const auto& value) {
    if (predicate(value)) {
      init += value;
    }
  });
  return init;
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(ImageView<T> input, Acc init, Predicate predicate) {
  for_each(input, [&init, &predicate](const auto& value) {
    if (predicate(value)) {
      init += value;
    }
  });
  return init;
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(CubeView<T> input, Acc init, Predicate predicate) {
  for_each(input, [&init, &predicate](const auto& value) {
    if (predicate(value)) {
      init += value;
    }
  });
  return init;
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(Array4DView<T> input, Acc init, Predicate predicate) {
  for_each(input, [&init, &predicate](const auto& value) {
    if (predicate(value)) {
      init += value;
    }
  });
  return init;
}

template <typename View, typename Acc> [[nodiscard]] Acc sum(const View& input, Acc init) {
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename T, typename Acc> [[nodiscard]] Acc sum(VectorView<T> input, Acc init) {
  if (detail::sum_vector_backend(input, init)) {
    return init;
  }
  for_each(input, [&init](const auto& value) {
    init += value;
  });
  return init;
}

template <typename View> [[nodiscard]] typename std::remove_cvref_t<View>::value_type sum(const View& input) {
  return sum(input, typename std::remove_cvref_t<View>::value_type{});
}

template <typename T> [[nodiscard]] std::remove_const_t<T> sum(VectorView<T> input) {
  return sum(input, std::remove_const_t<T>{});
}

template <typename View> using mean_result_t = reduction_result_t<typename std::remove_cvref_t<View>::value_type>;

template <typename View> [[nodiscard]] mean_result_t<View> mean(const View& input) {
  if (input.empty()) {
    throw std::invalid_argument("array mean input must be non-empty");
  }

  using result_type = mean_result_t<View>;
  const auto total = static_cast<result_type>(sum(input, result_type{}));
  if constexpr (is_complex_v<result_type>) {
    return total / static_cast<real_scalar_t<result_type>>(input.size());
  } else {
    return total / static_cast<result_type>(input.size());
  }
}

namespace detail {
template <typename T> [[nodiscard]] constexpr real_scalar_t<T> squared_magnitude(const T& value) noexcept {
  if constexpr (is_complex_v<T>) {
    return std::norm(value);
  } else {
    return value * value;
  }
}
} // namespace detail

template <typename View>
[[nodiscard]] real_scalar_t<typename std::remove_cvref_t<View>::value_type> squared_norm(const View& input) {
  using result_type = real_scalar_t<typename std::remove_cvref_t<View>::value_type>;
  result_type output{};
  for_each(input, [&output](const auto& value) {
    output += detail::squared_magnitude(value);
  });
  return output;
}

template <typename View> using norm_result_t = magnitude_result_t<typename std::remove_cvref_t<View>::value_type>;

template <typename View> [[nodiscard]] norm_result_t<View> norm(const View& input) {
  using std::sqrt;
  return sqrt(squared_norm(input));
}

template <typename View> [[nodiscard]] norm_result_t<View> sum_abs(const View& input) {
  norm_result_t<View> output{};
  for_each(input, [&output](const auto& value) {
    using std::abs;
    output += static_cast<norm_result_t<View>>(abs(value));
  });
  return output;
}

template <typename LhsView, typename RhsView>
using distance_result_t = real_scalar_t<std::common_type_t<typename std::remove_cvref_t<LhsView>::value_type,
                                                           typename std::remove_cvref_t<RhsView>::value_type>>;

template <typename LhsView, typename RhsView>
[[nodiscard]] distance_result_t<LhsView, RhsView> squared_distance(const LhsView& lhs, const RhsView& rhs) {
  distance_result_t<LhsView, RhsView> output{};
  for_each_zip(lhs, rhs, [&output](const auto& lhs_value, const auto& rhs_value) {
    output += detail::squared_magnitude(lhs_value - rhs_value);
  });
  return output;
}

template <typename LhsView, typename RhsView, typename Precision>
[[nodiscard]] bool all_close(const LhsView& lhs, const RhsView& rhs, const Precision& precision) {
  bool output = true;
  for_each_zip(lhs, rhs, [&output, &precision](const auto& lhs_value, const auto& rhs_value) {
    using std::abs;
    output = output && abs(lhs_value - rhs_value) <= precision;
  });
  return output;
}

template <typename T, typename OutputT>
void squared_norm_across(CubeView<T> input, MatrixView<OutputT> output, const Dim dim) {
  detail::validate_cube_dim_output(input, output, dim, "cube squared_norm_across output shape mismatch");
  switch (dim) {
    case Dim::dim0:
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          std::remove_const_t<OutputT> total{};
          for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
            total += static_cast<std::remove_const_t<OutputT>>(detail::squared_magnitude(input(i0, i1, i2)));
          }
          output(i1, i2) = total;
        }
      }
      return;
    case Dim::dim1:
      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          std::remove_const_t<OutputT> total{};
          for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
            total += static_cast<std::remove_const_t<OutputT>>(detail::squared_magnitude(input(i0, i1, i2)));
          }
          output(i0, i2) = total;
        }
      }
      return;
    case Dim::dim2:
      if (input.dim2_stride() == 1U) {
        std::size_t output_index = 0U;
        for_each_contiguous_block(input, [&](const auto block) {
          std::remove_const_t<OutputT> total{};
          for (const auto& value : block) {
            total += static_cast<std::remove_const_t<OutputT>>(detail::squared_magnitude(value));
          }
          output[output_index++] = total;
        });
        return;
      }

      for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
        for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
          std::remove_const_t<OutputT> total{};
          for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
            total += static_cast<std::remove_const_t<OutputT>>(detail::squared_magnitude(input(i0, i1, i2)));
          }
          output(i0, i1) = total;
        }
      }
      return;
    default:
      throw std::out_of_range("cube squared_norm_across dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  }
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<std::remove_const_t<T>>> squared_norm_across(CubeView<T> input,
                                                                                      const Dim dim) {
  detail::validate_rank_dim(dim, 3U, "cube squared_norm_across dim must be Dim::dim0, Dim::dim1, or Dim::dim2");
  const auto rows = dim == Dim::dim0 ? input.dim1() : input.dim0();
  const auto cols = dim == Dim::dim2 ? input.dim1() : input.dim2();
  auto output = PooledMatrix<real_scalar_t<std::remove_const_t<T>>>(rows, cols);
  squared_norm_across(input, output.view(), dim);
  return output;
}

template <typename View> [[nodiscard]] std::size_t count_nonzero(const View& input) {
  using value_type = typename std::remove_cvref_t<View>::value_type;
  std::size_t count = 0U;
  for_each(input, [&count](const auto& value) {
    if (value != value_type{}) {
      ++count;
    }
  });
  return count;
}

template <typename View, typename Predicate> [[nodiscard]] bool all_of(const View& input, Predicate predicate) {
  if constexpr (requires {
                  input.is_contiguous();
                  input.data();
                  input.size();
                  input[0U];
                }) {
    if (input.is_contiguous()) {
      const auto* input_data = input.data();
      for (std::size_t index = 0U; index < input.size(); ++index) {
        if (!predicate(input_data[index])) {
          return false;
        }
      }
      return true;
    }
    for (std::size_t index = 0U; index < input.size(); ++index) {
      if (!predicate(input[index])) {
        return false;
      }
    }
    return true;
  }

  bool result = true;
  for_each(input, [&result, &predicate](const auto& value) {
    result = result && predicate(value);
  });
  return result;
}

template <typename View, typename Predicate> [[nodiscard]] bool any_of(const View& input, Predicate predicate) {
  if constexpr (requires {
                  input.is_contiguous();
                  input.data();
                  input.size();
                  input[0U];
                }) {
    if (input.is_contiguous()) {
      const auto* input_data = input.data();
      for (std::size_t index = 0U; index < input.size(); ++index) {
        if (predicate(input_data[index])) {
          return true;
        }
      }
      return false;
    }
    for (std::size_t index = 0U; index < input.size(); ++index) {
      if (predicate(input[index])) {
        return true;
      }
    }
    return false;
  }

  bool result = false;
  for_each(input, [&result, &predicate](const auto& value) {
    result = result || predicate(value);
  });
  return result;
}

namespace detail {

template <typename View, typename Compare>
[[nodiscard]] typename View::pointer max_element_view(View input, Compare compare) {
  typename View::pointer largest = nullptr;
  for_each(input, [&largest, &compare](auto& value) {
    if (largest == nullptr || compare(*largest, value)) {
      largest = &value;
    }
  });
  return largest;
}

template <typename View, typename Compare>
[[nodiscard]] typename View::pointer min_element_view(View input, Compare compare) {
  typename View::pointer smallest = nullptr;
  for_each(input, [&smallest, &compare](auto& value) {
    if (smallest == nullptr || compare(value, *smallest)) {
      smallest = &value;
    }
  });
  return smallest;
}

} // namespace detail

template <typename T, typename Compare>
[[nodiscard]] typename VectorView<T>::pointer max_element(VectorView<T> input, Compare compare) {
  return detail::max_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename VectorView<T>::pointer max_element(VectorView<T> input) {
  return max_element(input, std::less<typename VectorView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename MatrixView<T>::pointer max_element(MatrixView<T> input, Compare compare) {
  return detail::max_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename MatrixView<T>::pointer max_element(MatrixView<T> input) {
  return max_element(input, std::less<typename MatrixView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename ImageView<T>::pointer max_element(ImageView<T> input, Compare compare) {
  return detail::max_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename ImageView<T>::pointer max_element(ImageView<T> input) {
  return max_element(input, std::less<typename ImageView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename CubeView<T>::pointer max_element(CubeView<T> input, Compare compare) {
  return detail::max_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename CubeView<T>::pointer max_element(CubeView<T> input) {
  return max_element(input, std::less<typename CubeView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename Array4DView<T>::pointer max_element(Array4DView<T> input, Compare compare) {
  return detail::max_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename Array4DView<T>::pointer max_element(Array4DView<T> input) {
  return max_element(input, std::less<typename Array4DView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename VectorView<T>::pointer min_element(VectorView<T> input, Compare compare) {
  return detail::min_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename VectorView<T>::pointer min_element(VectorView<T> input) {
  return min_element(input, std::less<typename VectorView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename MatrixView<T>::pointer min_element(MatrixView<T> input, Compare compare) {
  return detail::min_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename MatrixView<T>::pointer min_element(MatrixView<T> input) {
  return min_element(input, std::less<typename MatrixView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename ImageView<T>::pointer min_element(ImageView<T> input, Compare compare) {
  return detail::min_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename ImageView<T>::pointer min_element(ImageView<T> input) {
  return min_element(input, std::less<typename ImageView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename CubeView<T>::pointer min_element(CubeView<T> input, Compare compare) {
  return detail::min_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename CubeView<T>::pointer min_element(CubeView<T> input) {
  return min_element(input, std::less<typename CubeView<T>::value_type>{});
}

template <typename T, typename Compare>
[[nodiscard]] typename Array4DView<T>::pointer min_element(Array4DView<T> input, Compare compare) {
  return detail::min_element_view(input, compare);
}

template <typename T> [[nodiscard]] typename Array4DView<T>::pointer min_element(Array4DView<T> input) {
  return min_element(input, std::less<typename Array4DView<T>::value_type>{});
}

template <typename T> [[nodiscard]] std::remove_const_t<T> min(VectorView<T> input) {
  if (input.empty()) {
    throw std::invalid_argument("vector view min input must be non-empty");
  }
  auto result = std::remove_const_t<T>{};
  if (detail::min_vector_backend(input, result)) {
    return result;
  }
  const auto* smallest = min_element(input);
  return *smallest;
}

template <typename T> [[nodiscard]] std::remove_const_t<T> max(VectorView<T> input) {
  if (input.empty()) {
    throw std::invalid_argument("vector view max input must be non-empty");
  }
  auto result = std::remove_const_t<T>{};
  if (detail::max_vector_backend(input, result)) {
    return result;
  }
  const auto* largest = max_element(input);
  return *largest;
}

template <typename LhsT, typename RhsT>
[[nodiscard]] std::size_t count_not_equal(const PooledCube<LhsT>& lhs, const PooledCube<RhsT>& rhs) {
  return count_not_equal(lhs.view(), rhs.view());
}

template <typename T, typename MaskT>
[[nodiscard]] ForwardDifferenceStats<T>
forward_difference_stats_below_threshold(const PooledMatrix<T>& input, const PooledMatrix<MaskT>& mask,
                                         const MatrixDifferenceAxis axis, const T threshold) {
  return forward_difference_stats_below_threshold(input.view(), mask.view(), axis, threshold);
}

template <typename T, typename IndexT>
void argmin_groups(const PooledCube<T>& input, const std::size_t group_count, PooledCube<IndexT>& output,
                   const Dim dim) {
  argmin_groups(input.view(), group_count, output.view(), dim);
}

template <typename IndexT = std::size_t, typename T>
[[nodiscard]] PooledCube<IndexT> argmin_groups(const PooledCube<T>& input, const std::size_t group_count,
                                               const Dim dim) {
  return argmin_groups<IndexT>(input.view(), group_count, dim);
}

template <typename LhsT, typename RhsT, typename OutputT>
void sum_product_across(const PooledCube<LhsT>& lhs, const PooledCube<RhsT>& rhs, PooledMatrix<OutputT>& output,
                        const Dim dim) {
  sum_product_across(lhs.view(), rhs.view(), output.view(), dim);
}

template <typename LhsT, typename RhsT>
[[nodiscard]] PooledMatrix<sum_product_result_t<LhsT, RhsT>>
sum_product_across(const PooledCube<LhsT>& lhs, const PooledCube<RhsT>& rhs, const Dim dim) {
  return sum_product_across(lhs.view(), rhs.view(), dim);
}

template <typename T>
[[nodiscard]] PooledMatrix<real_scalar_t<T>> squared_norm_across(const PooledCube<T>& input, const Dim dim) {
  return squared_norm_across(input.view(), dim);
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(const PooledVector<T>& input, Acc init) {
  return accumulate(input.view(), init);
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(const PooledMatrix<T>& input, Acc init) {
  return accumulate(input.view(), init);
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(const PooledImage<T>& input, Acc init) {
  return accumulate(input.view(), init);
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(const PooledCube<T>& input, Acc init) {
  return accumulate(input.view(), init);
}

template <typename T, typename Acc> [[nodiscard]] Acc accumulate(const PooledArray4D<T>& input, Acc init) {
  return accumulate(input.view(), init);
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(const PooledVector<T>& input, Acc init, Predicate predicate) {
  return accumulate_if(input.view(), init, predicate);
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(const PooledMatrix<T>& input, Acc init, Predicate predicate) {
  return accumulate_if(input.view(), init, predicate);
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(const PooledImage<T>& input, Acc init, Predicate predicate) {
  return accumulate_if(input.view(), init, predicate);
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(const PooledCube<T>& input, Acc init, Predicate predicate) {
  return accumulate_if(input.view(), init, predicate);
}

template <typename T, typename Acc, typename Predicate>
[[nodiscard]] Acc accumulate_if(const PooledArray4D<T>& input, Acc init, Predicate predicate) {
  return accumulate_if(input.view(), init, predicate);
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledVector<T>::const_view_type::pointer max_element(const PooledVector<T>& input,
                                                                             Compare compare) {
  return max_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledVector<T>::const_view_type::pointer max_element(const PooledVector<T>& input) {
  return max_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledMatrix<T>::const_view_type::pointer max_element(const PooledMatrix<T>& input,
                                                                             Compare compare) {
  return max_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledMatrix<T>::const_view_type::pointer max_element(const PooledMatrix<T>& input) {
  return max_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledImage<T>::const_view_type::pointer max_element(const PooledImage<T>& input,
                                                                            Compare compare) {
  return max_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledImage<T>::const_view_type::pointer max_element(const PooledImage<T>& input) {
  return max_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledCube<T>::const_view_type::pointer max_element(const PooledCube<T>& input,
                                                                           Compare compare) {
  return max_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledCube<T>::const_view_type::pointer max_element(const PooledCube<T>& input) {
  return max_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledArray4D<T>::const_view_type::pointer max_element(const PooledArray4D<T>& input,
                                                                              Compare compare) {
  return max_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledArray4D<T>::const_view_type::pointer max_element(const PooledArray4D<T>& input) {
  return max_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledVector<T>::const_view_type::pointer min_element(const PooledVector<T>& input,
                                                                             Compare compare) {
  return min_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledVector<T>::const_view_type::pointer min_element(const PooledVector<T>& input) {
  return min_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledMatrix<T>::const_view_type::pointer min_element(const PooledMatrix<T>& input,
                                                                             Compare compare) {
  return min_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledMatrix<T>::const_view_type::pointer min_element(const PooledMatrix<T>& input) {
  return min_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledImage<T>::const_view_type::pointer min_element(const PooledImage<T>& input,
                                                                            Compare compare) {
  return min_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledImage<T>::const_view_type::pointer min_element(const PooledImage<T>& input) {
  return min_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledCube<T>::const_view_type::pointer min_element(const PooledCube<T>& input,
                                                                           Compare compare) {
  return min_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledCube<T>::const_view_type::pointer min_element(const PooledCube<T>& input) {
  return min_element(input.view());
}

template <typename T, typename Compare>
[[nodiscard]] typename PooledArray4D<T>::const_view_type::pointer min_element(const PooledArray4D<T>& input,
                                                                              Compare compare) {
  return min_element(input.view(), compare);
}

template <typename T>
[[nodiscard]] typename PooledArray4D<T>::const_view_type::pointer min_element(const PooledArray4D<T>& input) {
  return min_element(input.view());
}

template <typename View> [[nodiscard]] typename std::remove_cvref_t<View>::value_type min(const View& input) {
  const auto* value = min_element(input);
  if (value == nullptr) {
    throw std::invalid_argument("array min input must be non-empty");
  }
  return *value;
}

template <typename View> [[nodiscard]] typename std::remove_cvref_t<View>::value_type max(const View& input) {
  const auto* value = max_element(input);
  if (value == nullptr) {
    throw std::invalid_argument("array max input must be non-empty");
  }
  return *value;
}

template <typename View>
[[nodiscard]] std::pair<typename std::remove_cvref_t<View>::value_type, typename std::remove_cvref_t<View>::value_type>
minmax(const View& input) {
  return {min(input), max(input)};
}

} // namespace ksj::array
