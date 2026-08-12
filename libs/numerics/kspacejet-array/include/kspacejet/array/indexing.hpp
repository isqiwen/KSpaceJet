#pragma once

/// Bounds-checked linear and multidimensional indexing helpers for KSpaceJet row-major arrays.

#include "kspacejet/array/views.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ksj::array {

namespace detail {
inline void validate_same_size(const std::size_t lhs, const std::size_t rhs, const char* message) {
  if (lhs != rhs) {
    throw std::invalid_argument(message);
  }
}

template <typename Lhs, typename Rhs> void validate_same_shape(const Lhs& lhs, const Rhs& rhs, const char* message) {
  if (lhs.rows() != rhs.rows() || lhs.cols() != rhs.cols()) {
    throw std::invalid_argument(message);
  }
}

template <typename Lhs, typename Rhs>
void validate_same_cube_shape(const Lhs& lhs, const Rhs& rhs, const char* message) {
  if (lhs.dim0() != rhs.dim0() || lhs.dim1() != rhs.dim1() || lhs.dim2() != rhs.dim2()) {
    throw std::invalid_argument(message);
  }
}

template <typename Lhs, typename Rhs>
void validate_same_array4d_shape(const Lhs& lhs, const Rhs& rhs, const char* message) {
  if (lhs.dim0() != rhs.dim0() || lhs.dim1() != rhs.dim1() || lhs.dim2() != rhs.dim2() || lhs.dim3() != rhs.dim3()) {
    throw std::invalid_argument(message);
  }
}

[[nodiscard]] constexpr std::size_t centered_offset(const std::size_t extent,
                                                    const std::size_t centered_extent) noexcept {
  return extent / 2U - centered_extent / 2U;
}

struct CubeIndices {
  std::size_t dim0{};
  std::size_t dim1{};
  std::size_t dim2{};
};

[[nodiscard]] constexpr CubeIndices linear_index_to_cube_indices(const std::size_t linear_index, const std::size_t dim0,
                                                                 const std::size_t dim1, const std::size_t dim2) {
  if (linear_index >= dim0 * dim1 * dim2) {
    throw std::out_of_range("cube view linear index is out of range");
  }

  const auto i2 = linear_index % dim2;
  const auto i1 = (linear_index / dim2) % dim1;
  const auto i0 = linear_index / (dim1 * dim2);
  return {i0, i1, i2};
}

template <typename IndexT>
[[nodiscard]] std::size_t checked_linear_index(const IndexT value, const std::size_t size, const char* message) {
  if constexpr (std::is_signed_v<std::remove_cv_t<IndexT>>) {
    if (value < 0) {
      throw std::out_of_range(message);
    }
  }
  const auto index = static_cast<std::size_t>(value);
  if (index >= size) {
    throw std::out_of_range(message);
  }
  return index;
}
} // namespace detail

template <typename T, typename Predicate>
[[nodiscard]] std::size_t count_matching(CubeView<T> input, Predicate predicate) {
  std::size_t count = 0U;
  for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
        if (predicate(input(i0, i1, i2))) {
          ++count;
        }
      }
    }
  }
  return count;
}

template <typename T, typename Predicate>
[[nodiscard]] std::size_t count_true_runs(VectorView<T> input, Predicate predicate,
                                          const std::size_t min_run_length = 1U) {
  if (min_run_length == 0U) {
    throw std::invalid_argument("vector view count_true_runs min run length must be positive");
  }

  std::size_t run_count = 0U;
  std::size_t index = 0U;
  while (index < input.size()) {
    while (index < input.size() && !predicate(input(index))) {
      ++index;
    }
    const auto run_start = index;
    while (index < input.size() && predicate(input(index))) {
      ++index;
    }
    if (index - run_start >= min_run_length) {
      ++run_count;
    }
  }
  return run_count;
}

template <typename T, typename IndexT, typename Predicate>
[[nodiscard]] std::size_t find_true_run_bounds(VectorView<T> input, Predicate predicate, VectorView<IndexT> output,
                                               const std::size_t min_run_length = 1U) {
  if (min_run_length == 0U) {
    throw std::invalid_argument("vector view find_true_run_bounds min run length must be positive");
  }

  const auto run_count = count_true_runs(input, predicate, min_run_length);
  if (output.size() < run_count * 2U) {
    throw std::invalid_argument("vector view find_true_run_bounds output is too small");
  }

  std::size_t output_index = 0U;
  std::size_t index = 0U;
  while (index < input.size()) {
    while (index < input.size() && !predicate(input(index))) {
      ++index;
    }
    const auto run_start = index;
    while (index < input.size() && predicate(input(index))) {
      ++index;
    }
    if (index - run_start >= min_run_length) {
      output(output_index++) = static_cast<std::remove_const_t<IndexT>>(run_start);
      output(output_index++) = static_cast<std::remove_const_t<IndexT>>(index - 1U);
    }
  }
  return run_count;
}

template <typename IndexT = std::size_t, typename T, typename Predicate>
[[nodiscard]] PooledVector<IndexT> find_true_run_bounds(VectorView<T> input, Predicate predicate,
                                                        const std::size_t min_run_length = 1U) {
  auto output = PooledVector<IndexT>(count_true_runs(input, predicate, min_run_length) * 2U);
  static_cast<void>(find_true_run_bounds(input, predicate, output.view(), min_run_length));
  return output;
}

template <typename T, typename IndexT, typename Predicate>
[[nodiscard]] std::size_t find_linear_indices(CubeView<T> input, Predicate predicate, VectorView<IndexT> output) {
  const auto required_count = count_matching(input, predicate);
  if (output.size() < required_count) {
    throw std::invalid_argument("cube view find_linear_indices output is too small");
  }

  std::size_t output_index = 0U;
  std::size_t linear_index = 0U;
  for (std::size_t i0 = 0; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < input.dim2(); ++i2) {
        if (predicate(input(i0, i1, i2))) {
          output(output_index++) = static_cast<IndexT>(linear_index);
        }
        ++linear_index;
      }
    }
  }

  return required_count;
}

template <typename T, typename Predicate>
[[nodiscard]] std::size_t count_matching(const PooledCube<T>& input, Predicate predicate) {
  return count_matching(input.view(), predicate);
}

template <typename T, typename IndexT, typename Predicate>
[[nodiscard]] std::size_t find_linear_indices(const PooledCube<T>& input, Predicate predicate,
                                              PooledVector<IndexT>& output) {
  return find_linear_indices(input.view(), predicate, output.view());
}

template <typename IndexT = std::size_t, typename T, typename Predicate>
[[nodiscard]] PooledVector<IndexT> find_linear_indices(CubeView<T> input, Predicate predicate) {
  auto output = PooledVector<IndexT>(count_matching(input, predicate));
  static_cast<void>(find_linear_indices(input, predicate, output.view()));
  return output;
}

template <typename IndexT = std::size_t, typename T, typename Predicate>
[[nodiscard]] PooledVector<IndexT> find_linear_indices(const PooledCube<T>& input, Predicate predicate) {
  return find_linear_indices<IndexT>(input.view(), predicate);
}

template <typename InputT, typename IndexT, typename OutputT>
void gather_linear_indices(CubeView<InputT> input, VectorView<IndexT> indices, VectorView<OutputT> output) {
  if (output.size() < indices.size()) {
    throw std::invalid_argument("cube view gather_linear_indices output is too small");
  }
  if (input.is_contiguous() && indices.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    const auto* index_data = indices.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < indices.size(); ++index) {
      output_data[index] = input_data[static_cast<std::size_t>(index_data[index])];
    }
    return;
  }
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    const auto position = detail::linear_index_to_cube_indices(static_cast<std::size_t>(indices(index)), input.dim0(),
                                                               input.dim1(), input.dim2());
    output(index) = input(position.dim0, position.dim1, position.dim2);
  }
}

template <typename InputView, typename IndexT, typename OutputT>
void take(InputView input, VectorView<IndexT> indices, VectorView<OutputT> output) {
  if (output.size() < indices.size()) {
    throw std::invalid_argument("take output is too small");
  }
  if (input.is_contiguous() && indices.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    const auto* index_data = indices.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < indices.size(); ++index) {
      output_data[index] =
        input_data[detail::checked_linear_index(index_data[index], input.size(), "take index is out of range")];
    }
    return;
  }
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    output(index) = input[detail::checked_linear_index(indices(index), input.size(), "take index is out of range")];
  }
}

template <typename InputView, typename IndexT, typename OutputT>
void gather(InputView input, VectorView<IndexT> indices, VectorView<OutputT> output) {
  take(input, indices, output);
}

template <typename InputView, typename IndexT>
[[nodiscard]] PooledVector<std::remove_const_t<typename InputView::value_type>> take(InputView input,
                                                                                     VectorView<IndexT> indices) {
  auto output = PooledVector<std::remove_const_t<typename InputView::value_type>>(indices.size());
  take(input, indices, output.view());
  return output;
}

template <typename InputView, typename IndexT>
[[nodiscard]] PooledVector<std::remove_const_t<typename InputView::value_type>> gather(InputView input,
                                                                                       VectorView<IndexT> indices) {
  return take(input, indices);
}

template <typename InputT, typename IndexT>
[[nodiscard]] PooledVector<InputT> take(const PooledVector<InputT>& input, const PooledVector<IndexT>& indices) {
  return take(input.view(), indices.view());
}

template <typename InputT, typename IndexT>
[[nodiscard]] PooledVector<InputT> gather(const PooledVector<InputT>& input, const PooledVector<IndexT>& indices) {
  return gather(input.view(), indices.view());
}

template <typename InputT, typename IndexT, typename OutputView>
void scatter(VectorView<InputT> input, VectorView<IndexT> indices, OutputView output) {
  if (input.size() < indices.size()) {
    throw std::invalid_argument("scatter input is too small");
  }
  if (input.is_contiguous() && indices.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    const auto* index_data = indices.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < indices.size(); ++index) {
      output_data[detail::checked_linear_index(index_data[index], output.size(), "scatter index is out of range")] =
        input_data[index];
    }
    return;
  }
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    output[detail::checked_linear_index(indices(index), output.size(), "scatter index is out of range")] = input(index);
  }
}

template <typename InputT, typename IndexT, typename OutputT>
void scatter_linear_indices(VectorView<InputT> input, VectorView<IndexT> indices, CubeView<OutputT> output) {
  if (input.size() < indices.size()) {
    throw std::invalid_argument("cube view scatter_linear_indices input is too small");
  }
  if (input.is_contiguous() && indices.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    const auto* index_data = indices.data();
    auto* output_data = output.data();
    for (std::size_t index = 0U; index < indices.size(); ++index) {
      output_data[static_cast<std::size_t>(index_data[index])] = input_data[index];
    }
    return;
  }
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    const auto position = detail::linear_index_to_cube_indices(static_cast<std::size_t>(indices(index)), output.dim0(),
                                                               output.dim1(), output.dim2());
    output(position.dim0, position.dim1, position.dim2) = input(index);
  }
}

template <typename OutputT, typename IndexT, typename Value>
void fill_linear_indices(CubeView<OutputT> output, VectorView<IndexT> indices, const Value& value) {
  if (output.is_contiguous() && indices.is_contiguous()) {
    auto* output_data = output.data();
    const auto* index_data = indices.data();
    const auto output_value = static_cast<std::remove_const_t<OutputT>>(value);
    for (std::size_t index = 0U; index < indices.size(); ++index) {
      output_data[static_cast<std::size_t>(index_data[index])] = output_value;
    }
    return;
  }
  for (std::size_t index = 0U; index < indices.size(); ++index) {
    const auto position = detail::linear_index_to_cube_indices(static_cast<std::size_t>(indices(index)), output.dim0(),
                                                               output.dim1(), output.dim2());
    output(position.dim0, position.dim1, position.dim2) = static_cast<std::remove_const_t<OutputT>>(value);
  }
}

template <typename InputT, typename IndexT>
[[nodiscard]] PooledVector<std::remove_const_t<InputT>> gather_linear_indices(CubeView<InputT> input,
                                                                              VectorView<IndexT> indices) {
  auto output = PooledVector<std::remove_const_t<InputT>>(indices.size());
  gather_linear_indices(input, indices, output.view());
  return output;
}

template <typename InputT, typename IndexT, typename OutputT>
void scatter_linear_indices(const PooledVector<InputT>& input, const PooledVector<IndexT>& indices,
                            PooledCube<OutputT>& output) {
  scatter_linear_indices(input.view(), indices.view(), output.view());
}

template <typename OutputT, typename IndexT, typename Value>
void fill_linear_indices(PooledCube<OutputT>& output, const PooledVector<IndexT>& indices, const Value& value) {
  fill_linear_indices(output.view(), indices.view(), value);
}

template <typename InputT, typename IndexT>
[[nodiscard]] PooledVector<std::remove_const_t<InputT>> gather_linear_indices(const PooledCube<InputT>& input,
                                                                              const PooledVector<IndexT>& indices) {
  return gather_linear_indices(input.view(), indices.view());
}

} // namespace ksj::array
