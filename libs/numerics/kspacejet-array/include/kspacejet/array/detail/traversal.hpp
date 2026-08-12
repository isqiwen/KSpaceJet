#pragma once

#include "kspacejet/array/views.hpp"

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace ksj::array::detail {

template <typename... Args> [[nodiscard]] bool transform_inner_contiguous_blocks(Args&&...) {
  return false;
}

template <typename... Args> [[nodiscard]] bool for_each_inner_contiguous_blocks(Args&&...) {
  return false;
}

template <typename View> using view_data_element_t = std::remove_pointer_t<decltype(std::declval<View>().data())>;

template <typename T> [[nodiscard]] bool has_inner_contiguous_blocks(VectorView<T> input) noexcept {
  return input.stride() == 1U;
}

template <typename T> [[nodiscard]] bool has_inner_contiguous_blocks(MatrixView<T> input) noexcept {
  return input.col_stride() == 1U;
}

template <typename T> [[nodiscard]] bool has_inner_contiguous_blocks(ImageView<T>) noexcept {
  return true;
}

template <typename T> [[nodiscard]] bool has_inner_contiguous_blocks(CubeView<T> input) noexcept {
  return input.dim2_stride() == 1U;
}

template <typename T> [[nodiscard]] bool has_inner_contiguous_blocks(Array4DView<T> input) noexcept {
  return input.dim3_stride() == 1U;
}

template <typename T> [[nodiscard]] std::size_t inner_block_length(VectorView<T> input) noexcept {
  return input.size();
}

template <typename T> [[nodiscard]] std::size_t inner_block_length(MatrixView<T> input) noexcept {
  return input.cols();
}

template <typename T> [[nodiscard]] std::size_t inner_block_length(ImageView<T> input) noexcept {
  return input.cols();
}

template <typename T> [[nodiscard]] std::size_t inner_block_length(CubeView<T> input) noexcept {
  return input.dim2();
}

template <typename T> [[nodiscard]] std::size_t inner_block_length(Array4DView<T> input) noexcept {
  return input.dim3();
}

template <typename View> [[nodiscard]] std::size_t inner_block_count(View input) noexcept {
  const auto length = inner_block_length(input);
  if (length == 0U) {
    return 0U;
  }
  return input.size() / length;
}

template <typename First, typename... Rest>
[[nodiscard]] bool has_matching_inner_contiguous_blocks(First first, Rest... rest) noexcept {
  if (!has_inner_contiguous_blocks(first) || (... || !has_inner_contiguous_blocks(rest))) {
    return false;
  }
  const auto length = inner_block_length(first);
  return length != 0U && ((inner_block_length(rest) == length) && ...);
}

template <typename T>
[[nodiscard]] auto inner_block_data(VectorView<T> input, std::size_t, std::size_t, std::size_t) noexcept {
  return input.data();
}

template <typename T>
[[nodiscard]] auto inner_block_data(MatrixView<T> input, const std::size_t i0, std::size_t, std::size_t) noexcept {
  return input.data() + i0 * input.row_stride();
}

template <typename T>
[[nodiscard]] auto inner_block_data(ImageView<T> input, const std::size_t i0, std::size_t, std::size_t) noexcept {
  return input.data() + i0 * input.row_stride();
}

template <typename T>
[[nodiscard]] auto inner_block_data(CubeView<T> input, const std::size_t i0, const std::size_t i1,
                                    std::size_t) noexcept {
  return input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride();
}

template <typename T>
[[nodiscard]] auto inner_block_data(Array4DView<T> input, const std::size_t i0, const std::size_t i1,
                                    const std::size_t i2) noexcept {
  return input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride();
}

template <typename View>
[[nodiscard]] auto inner_block_span(View input, const std::size_t i0, const std::size_t i1, const std::size_t i2,
                                    const std::size_t length) noexcept {
  return std::span<view_data_element_t<View>>(inner_block_data(input, i0, i1, i2), length);
}

template <typename T, typename F> void for_each_inner_block_coordinate(VectorView<T>, F&& f) {
  f(0U, 0U, 0U);
}

template <typename T, typename F> void for_each_inner_block_coordinate(MatrixView<T> input, F&& f) {
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    f(row, 0U, 0U);
  }
}

template <typename T, typename F> void for_each_inner_block_coordinate(ImageView<T> input, F&& f) {
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    f(row, 0U, 0U);
  }
}

template <typename T, typename F> void for_each_inner_block_coordinate(CubeView<T> input, F&& f) {
  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      f(i0, i1, 0U);
    }
  }
}

template <typename T, typename F> void for_each_inner_block_coordinate(Array4DView<T> input, F&& f) {
  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        f(i0, i1, i2);
      }
    }
  }
}

template <typename F, typename First, typename... Rest>
[[nodiscard]] bool for_each_inner_contiguous_span_group(F& f, First first, Rest... rest) {
  if (!has_matching_inner_contiguous_blocks(first, rest...)) {
    return false;
  }

  const auto length = inner_block_length(first);
  for_each_inner_block_coordinate(first, [&](const std::size_t i0, const std::size_t i1, const std::size_t i2) {
    f(inner_block_span(first, i0, i1, i2, length), inner_block_span(rest, i0, i1, i2, length)...);
  });
  return true;
}

template <typename T, typename F> [[nodiscard]] bool for_each_inner_contiguous_blocks(MatrixView<T> input, F& f) {
  if (input.col_stride() != 1U) {
    return false;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    auto* input_data = input.data() + row * input.row_stride();
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      f(input_data[col]);
    }
  }
  return true;
}

template <typename T, typename F> [[nodiscard]] bool for_each_inner_contiguous_blocks(ImageView<T> input, F& f) {
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    auto* input_data = input.data() + row * input.row_stride();
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      f(input_data[col]);
    }
  }
  return true;
}

template <typename T, typename F> [[nodiscard]] bool for_each_inner_contiguous_blocks(CubeView<T> input, F& f) {
  if (input.dim2_stride() != 1U) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      auto* input_data = input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride();
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        f(input_data[i2]);
      }
    }
  }
  return true;
}

template <typename T, typename F> [[nodiscard]] bool for_each_inner_contiguous_blocks(Array4DView<T> input, F& f) {
  if (input.dim3_stride() != 1U) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        auto* input_data =
          input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride();
        for (std::size_t i3 = 0U; i3 < input.dim3(); ++i3) {
          f(input_data[i3]);
        }
      }
    }
  }
  return true;
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(MatrixView<InputT> input, MatrixView<OutputT> output, F& f) {
  if (input.col_stride() != 1U || output.col_stride() != 1U) {
    return false;
  }

  for (std::size_t row = 0U; row < input.rows(); ++row) {
    const auto* input_data = input.data() + row * input.row_stride();
    auto* output_data = output.data() + row * output.row_stride();
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      output_data[col] = f(input_data[col]);
    }
  }
  return true;
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(MatrixView<InputT> input, ImageView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(input, output.as_matrix_view(), f);
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(ImageView<InputT> input, MatrixView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(input.as_matrix_view(), output, f);
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(ImageView<InputT> input, ImageView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(input.as_matrix_view(), output.as_matrix_view(), f);
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(CubeView<InputT> input, CubeView<OutputT> output, F& f) {
  if (input.dim2_stride() != 1U || output.dim2_stride() != 1U) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      const auto* input_data = input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride();
      auto* output_data = output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride();
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        output_data[i2] = f(input_data[i2]);
      }
    }
  }
  return true;
}

template <typename InputT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(Array4DView<InputT> input, Array4DView<OutputT> output, F& f) {
  if (input.dim3_stride() != 1U || output.dim3_stride() != 1U) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        const auto* input_data =
          input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride();
        auto* output_data =
          output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride() + i2 * output.dim2_stride();
        for (std::size_t i3 = 0U; i3 < input.dim3(); ++i3) {
          output_data[i3] = f(input_data[i3]);
        }
      }
    }
  }
  return true;
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(MatrixView<LhsT> lhs, MatrixView<RhsT> rhs,
                                                     MatrixView<OutputT> output, F& f) {
  if (lhs.col_stride() != 1U || rhs.col_stride() != 1U || output.col_stride() != 1U) {
    return false;
  }

  for (std::size_t row = 0U; row < lhs.rows(); ++row) {
    const auto* lhs_data = lhs.data() + row * lhs.row_stride();
    const auto* rhs_data = rhs.data() + row * rhs.row_stride();
    auto* output_data = output.data() + row * output.row_stride();
    for (std::size_t col = 0U; col < lhs.cols(); ++col) {
      output_data[col] = f(lhs_data[col], rhs_data[col]);
    }
  }
  return true;
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(MatrixView<LhsT> lhs, ImageView<RhsT> rhs,
                                                     ImageView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(lhs, rhs.as_matrix_view(), output.as_matrix_view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(ImageView<LhsT> lhs, MatrixView<RhsT> rhs,
                                                     ImageView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(lhs.as_matrix_view(), rhs, output.as_matrix_view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(ImageView<LhsT> lhs, ImageView<RhsT> rhs,
                                                     MatrixView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(lhs.as_matrix_view(), rhs.as_matrix_view(), output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(MatrixView<LhsT> lhs, ImageView<RhsT> rhs,
                                                     MatrixView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(lhs, rhs.as_matrix_view(), output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(ImageView<LhsT> lhs, MatrixView<RhsT> rhs,
                                                     MatrixView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(lhs.as_matrix_view(), rhs, output, f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(ImageView<LhsT> lhs, ImageView<RhsT> rhs,
                                                     ImageView<OutputT> output, F& f) {
  return transform_inner_contiguous_blocks(lhs.as_matrix_view(), rhs.as_matrix_view(), output.as_matrix_view(), f);
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(CubeView<LhsT> lhs, CubeView<RhsT> rhs, CubeView<OutputT> output,
                                                     F& f) {
  if (lhs.dim2_stride() != 1U || rhs.dim2_stride() != 1U || output.dim2_stride() != 1U) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < lhs.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < lhs.dim1(); ++i1) {
      const auto* lhs_data = lhs.data() + i0 * lhs.dim0_stride() + i1 * lhs.dim1_stride();
      const auto* rhs_data = rhs.data() + i0 * rhs.dim0_stride() + i1 * rhs.dim1_stride();
      auto* output_data = output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride();
      for (std::size_t i2 = 0U; i2 < lhs.dim2(); ++i2) {
        output_data[i2] = f(lhs_data[i2], rhs_data[i2]);
      }
    }
  }
  return true;
}

template <typename LhsT, typename RhsT, typename OutputT, typename F>
[[nodiscard]] bool transform_inner_contiguous_blocks(Array4DView<LhsT> lhs, Array4DView<RhsT> rhs,
                                                     Array4DView<OutputT> output, F& f) {
  if (lhs.dim3_stride() != 1U || rhs.dim3_stride() != 1U || output.dim3_stride() != 1U) {
    return false;
  }

  for (std::size_t i0 = 0U; i0 < lhs.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < lhs.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < lhs.dim2(); ++i2) {
        const auto* lhs_data = lhs.data() + i0 * lhs.dim0_stride() + i1 * lhs.dim1_stride() + i2 * lhs.dim2_stride();
        const auto* rhs_data = rhs.data() + i0 * rhs.dim0_stride() + i1 * rhs.dim1_stride() + i2 * rhs.dim2_stride();
        auto* output_data =
          output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride() + i2 * output.dim2_stride();
        for (std::size_t i3 = 0U; i3 < lhs.dim3(); ++i3) {
          output_data[i3] = f(lhs_data[i3], rhs_data[i3]);
        }
      }
    }
  }
  return true;
}

template <typename View, typename F> F for_each_elements(View&& input, F f) {
  if (input.is_contiguous()) {
    for (std::size_t index = 0; index < input.size(); ++index) {
      f(input.data()[index]);
    }
    return f;
  }
  if (for_each_inner_contiguous_blocks(input, f)) {
    return f;
  }
  for (std::size_t index = 0; index < input.size(); ++index) {
    f(input[index]);
  }
  return f;
}

template <typename T, typename F> F for_each_contiguous_blocks(VectorView<T> input, F f) {
  if (input.stride() == 1U) {
    f(std::span<T>(input.data(), input.size()));
    return f;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    f(std::span<T>(&input[index], 1U));
  }
  return f;
}

template <typename T, typename F> F for_each_contiguous_blocks(MatrixView<T> input, F f) {
  if (input.col_stride() == 1U) {
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      f(std::span<T>(input.data() + row * input.row_stride(), input.cols()));
    }
    return f;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    f(std::span<T>(&input[index], 1U));
  }
  return f;
}

template <typename T, typename F> F for_each_contiguous_blocks(ImageView<T> input, F f) {
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    f(std::span<T>(input.data() + row * input.row_stride(), input.cols()));
  }
  return f;
}

template <typename T, typename F> F for_each_contiguous_blocks(CubeView<T> input, F f) {
  if (input.dim2_stride() == 1U) {
    for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        f(std::span<T>(input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride(), input.dim2()));
      }
    }
    return f;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    f(std::span<T>(&input[index], 1U));
  }
  return f;
}

template <typename T, typename F> F for_each_contiguous_blocks(Array4DView<T> input, F f) {
  if (input.dim3_stride() == 1U) {
    for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
      for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
        for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
          f(std::span<T>(input.data() + i0 * input.dim0_stride() + i1 * input.dim1_stride() + i2 * input.dim2_stride(),
                         input.dim3()));
        }
      }
    }
    return f;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    f(std::span<T>(&input[index], 1U));
  }
  return f;
}

template <typename View, typename F> F for_each_linear_indexed_elements(View&& input, F f) {
  if (input.is_contiguous()) {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      f(index, input.data()[index]);
    }
    return f;
  }
  std::size_t block_linear_index = 0U;
  auto block_function = [&f, &block_linear_index](auto input_block) {
    for (std::size_t index = 0U; index < input_block.size(); ++index) {
      f(block_linear_index++, input_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, input)) {
    return f;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    f(index, input[index]);
  }
  return f;
}

template <typename T, typename F> F for_each_indexed_elements(VectorView<T> input, F f) {
  for (std::size_t index = 0U; index < input.size(); ++index) {
    f(Index<1U>(index), input[index]);
  }
  return f;
}

template <typename T, typename F> F for_each_indexed_elements(MatrixView<T> input, F f) {
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      f(Index<2U>(row, col), input(row, col));
    }
  }
  return f;
}

template <typename T, typename F> F for_each_indexed_elements(ImageView<T> input, F f) {
  return for_each_indexed_elements(input.as_matrix_view(), f);
}

template <typename T, typename F> F for_each_indexed_elements(CubeView<T> input, F f) {
  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        f(Index<3U>(i0, i1, i2), input(i0, i1, i2));
      }
    }
  }
  return f;
}

template <typename T, typename F> F for_each_indexed_elements(Array4DView<T> input, F f) {
  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        for (std::size_t i3 = 0U; i3 < input.dim3(); ++i3) {
          f(Index<4U>(i0, i1, i2, i3), input(i0, i1, i2, i3));
        }
      }
    }
  }
  return f;
}

template <typename Lhs, typename Rhs, typename F> F for_each_zip_elements(Lhs&& lhs, Rhs&& rhs, F f) {
  if (lhs.is_contiguous() && rhs.is_contiguous()) {
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
      f(lhs.data()[index], rhs.data()[index]);
    }
    return f;
  }
  auto block_function = [&f](auto lhs_block, auto rhs_block) {
    for (std::size_t index = 0U; index < lhs_block.size(); ++index) {
      f(lhs_block[index], rhs_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, lhs, rhs)) {
    return f;
  }
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    f(lhs[index], rhs[index]);
  }
  return f;
}

template <typename First, typename Second, typename Third, typename F>
F for_each_zip_elements(First&& first, Second&& second, Third&& third, F f) {
  if (first.is_contiguous() && second.is_contiguous() && third.is_contiguous()) {
    for (std::size_t index = 0U; index < first.size(); ++index) {
      f(first.data()[index], second.data()[index], third.data()[index]);
    }
    return f;
  }
  auto block_function = [&f](auto first_block, auto second_block, auto third_block) {
    for (std::size_t index = 0U; index < first_block.size(); ++index) {
      f(first_block[index], second_block[index], third_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, first, second, third)) {
    return f;
  }
  for (std::size_t index = 0U; index < first.size(); ++index) {
    f(first[index], second[index], third[index]);
  }
  return f;
}

template <typename Input, typename Output, typename F> F transform_elements(Input&& input, Output&& output, F f) {
  if (input.is_contiguous() && output.is_contiguous()) {
    for (std::size_t index = 0; index < input.size(); ++index) {
      output.data()[index] = f(input.data()[index]);
    }
    return f;
  }
  if (transform_inner_contiguous_blocks(input, output, f)) {
    return f;
  }
  for (std::size_t index = 0; index < input.size(); ++index) {
    output[index] = f(input[index]);
  }
  return f;
}

template <typename Input, typename Output, typename F>
F transform_indexed_elements(Input&& input, Output&& output, F f) {
  if (input.is_contiguous() && output.is_contiguous()) {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = f(index, input.data()[index]);
    }
    return f;
  }
  std::size_t block_linear_index = 0U;
  auto block_function = [&f, &block_linear_index](auto input_block, auto output_block) {
    for (std::size_t index = 0U; index < input_block.size(); ++index) {
      output_block[index] = f(block_linear_index++, input_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, input, output)) {
    return f;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output[index] = f(index, input[index]);
  }
  return f;
}

template <typename Lhs, typename Rhs, typename Output, typename F>
F transform_elements(Lhs&& lhs, Rhs&& rhs, Output&& output, F f) {
  if (lhs.is_contiguous() && rhs.is_contiguous() && output.is_contiguous()) {
    for (std::size_t index = 0; index < lhs.size(); ++index) {
      output.data()[index] = f(lhs.data()[index], rhs.data()[index]);
    }
    return f;
  }
  if (transform_inner_contiguous_blocks(lhs, rhs, output, f)) {
    return f;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    output[index] = f(lhs[index], rhs[index]);
  }
  return f;
}

template <typename First, typename Second, typename Third, typename Output, typename F>
F transform_elements(First&& first, Second&& second, Third&& third, Output&& output, F f) {
  if (first.is_contiguous() && second.is_contiguous() && third.is_contiguous() && output.is_contiguous()) {
    for (std::size_t index = 0; index < first.size(); ++index) {
      output.data()[index] = f(first.data()[index], second.data()[index], third.data()[index]);
    }
    return f;
  }
  auto block_function = [&f](auto first_block, auto second_block, auto third_block, auto output_block) {
    for (std::size_t index = 0U; index < first_block.size(); ++index) {
      output_block[index] = f(first_block[index], second_block[index], third_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, first, second, third, output)) {
    return f;
  }
  for (std::size_t index = 0; index < first.size(); ++index) {
    output[index] = f(first[index], second[index], third[index]);
  }
  return f;
}

template <typename First, typename Second, typename Third, typename Fourth, typename Output, typename F>
F transform_elements(First&& first, Second&& second, Third&& third, Fourth&& fourth, Output&& output, F f) {
  if (first.is_contiguous() && second.is_contiguous() && third.is_contiguous() && fourth.is_contiguous() &&
      output.is_contiguous()) {
    for (std::size_t index = 0; index < first.size(); ++index) {
      output.data()[index] = f(first.data()[index], second.data()[index], third.data()[index], fourth.data()[index]);
    }
    return f;
  }
  auto block_function = [&f](auto first_block, auto second_block, auto third_block, auto fourth_block,
                             auto output_block) {
    for (std::size_t index = 0U; index < first_block.size(); ++index) {
      output_block[index] = f(first_block[index], second_block[index], third_block[index], fourth_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, first, second, third, fourth, output)) {
    return f;
  }
  for (std::size_t index = 0; index < first.size(); ++index) {
    output[index] = f(first[index], second[index], third[index], fourth[index]);
  }
  return f;
}

template <typename First, typename Second, typename Third, typename Fourth, typename Fifth, typename Output, typename F>
F transform_elements(First&& first, Second&& second, Third&& third, Fourth&& fourth, Fifth&& fifth, Output&& output,
                     F f) {
  if (first.is_contiguous() && second.is_contiguous() && third.is_contiguous() && fourth.is_contiguous() &&
      fifth.is_contiguous() && output.is_contiguous()) {
    for (std::size_t index = 0; index < first.size(); ++index) {
      output.data()[index] =
        f(first.data()[index], second.data()[index], third.data()[index], fourth.data()[index], fifth.data()[index]);
    }
    return f;
  }
  auto block_function = [&f](auto first_block, auto second_block, auto third_block, auto fourth_block, auto fifth_block,
                             auto output_block) {
    for (std::size_t index = 0U; index < first_block.size(); ++index) {
      output_block[index] =
        f(first_block[index], second_block[index], third_block[index], fourth_block[index], fifth_block[index]);
    }
  };
  if (for_each_inner_contiguous_span_group(block_function, first, second, third, fourth, fifth, output)) {
    return f;
  }
  for (std::size_t index = 0; index < first.size(); ++index) {
    output[index] = f(first[index], second[index], third[index], fourth[index], fifth[index]);
  }
  return f;
}

} // namespace ksj::array::detail
