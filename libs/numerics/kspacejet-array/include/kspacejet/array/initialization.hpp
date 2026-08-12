#pragma once

/// Explicit array initialization and factory operations such as fill, zeros, ones, and identity.

#include "kspacejet/array/detail/array_policy.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_storage.hpp"
#include "kspacejet/array/detail/intel/intel_array_storage.hpp"
#include "kspacejet/array/scalar_traits.hpp"
#include "kspacejet/array/views.hpp"
#include "kspacejet/memory/allocation_properties.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

namespace detail {
template <typename T>
using linspace_scale_t = std::conditional_t<std::is_integral_v<real_scalar_t<T>>, long double, real_scalar_t<T>>;

template <typename T> inline constexpr bool always_false_v = false;

[[nodiscard]] inline std::mt19937_64 make_uniform_random_generator() {
  std::random_device random_device;
  std::array<std::random_device::result_type, 8> seed_data{};
  for (auto& value : seed_data) {
    value = random_device();
  }
  std::seed_seq seed(seed_data.begin(), seed_data.end());
  return std::mt19937_64(seed);
}

template <typename T, typename StartT, typename StopT>
void assign_linspace_value(T& output, const StartT& start, const StopT& stop, const std::size_t index,
                           const std::size_t count) {
  using value_type = std::remove_cv_t<T>;
  if (count == 1U || index == 0U) {
    output = static_cast<value_type>(start);
    return;
  }
  if (index + 1U == count) {
    output = static_cast<value_type>(stop);
    return;
  }

  using scale_type = linspace_scale_t<value_type>;
  const auto start_value = static_cast<value_type>(start);
  const auto stop_value = static_cast<value_type>(stop);
  const auto fraction = static_cast<scale_type>(index) / static_cast<scale_type>(count - 1U);
  output = static_cast<value_type>(start_value + (stop_value - start_value) * fraction);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
[[nodiscard]] std::remove_cv_t<T> sample_uniform_random_value(const LowerT& lower, const UpperT& upper,
                                                              UniformRandomBitGenerator& generator) {
  using value_type = std::remove_cv_t<T>;
  if constexpr (is_complex_v<value_type>) {
    using real_type = real_scalar_t<value_type>;
    const auto lower_value = static_cast<real_type>(lower);
    const auto upper_value = static_cast<real_type>(upper);
    if (lower_value > upper_value) {
      throw std::invalid_argument("uniform random lower bound must not exceed upper bound");
    }
    std::uniform_real_distribution<real_type> distribution(lower_value, upper_value);
    return value_type{distribution(generator), distribution(generator)};
  } else if constexpr (std::is_integral_v<value_type>) {
    using distribution_value_type = std::conditional_t<std::is_signed_v<value_type>, long long, unsigned long long>;
    const auto lower_value = static_cast<distribution_value_type>(lower);
    const auto upper_value = static_cast<distribution_value_type>(upper);
    if (lower_value > upper_value) {
      throw std::invalid_argument("uniform random lower bound must not exceed upper bound");
    }
    std::uniform_int_distribution<distribution_value_type> distribution(lower_value, upper_value);
    return static_cast<value_type>(distribution(generator));
  } else if constexpr (std::is_floating_point_v<value_type>) {
    const auto lower_value = static_cast<value_type>(lower);
    const auto upper_value = static_cast<value_type>(upper);
    if (lower_value > upper_value) {
      throw std::invalid_argument("uniform random lower bound must not exceed upper bound");
    }
    std::uniform_real_distribution<value_type> distribution(lower_value, upper_value);
    return distribution(generator);
  } else {
    static_assert(always_false_v<value_type>, "uniform random fill requires arithmetic or std::complex scalar type");
  }
}

template <typename T, typename Value>
[[nodiscard]] bool fill_inner_contiguous_blocks(MatrixView<T> output, const Value& value) {
  if (output.col_stride() != 1U) {
    return false;
  }
  const auto output_value = static_cast<std::remove_const_t<T>>(value);
  for (std::size_t row = 0U; row < output.rows(); ++row) {
    std::fill_n(output.data() + row * output.row_stride(), output.cols(), output_value);
  }
  return true;
}

template <typename T, typename Value>
[[nodiscard]] bool fill_inner_contiguous_blocks(ImageView<T> output, const Value& value) {
  const auto output_value = static_cast<std::remove_const_t<T>>(value);
  for (std::size_t row = 0U; row < output.rows(); ++row) {
    std::fill_n(output.data() + row * output.row_stride(), output.cols(), output_value);
  }
  return true;
}

template <typename T, typename Value>
[[nodiscard]] bool fill_inner_contiguous_blocks(CubeView<T> output, const Value& value) {
  if (output.dim2_stride() != 1U) {
    return false;
  }
  const auto output_value = static_cast<std::remove_const_t<T>>(value);
  for (std::size_t i0 = 0U; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < output.dim1(); ++i1) {
      std::fill_n(output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride(), output.dim2(), output_value);
    }
  }
  return true;
}

template <typename T, typename Value>
[[nodiscard]] bool fill_inner_contiguous_blocks(Array4DView<T> output, const Value& value) {
  if (output.dim3_stride() != 1U) {
    return false;
  }
  const auto output_value = static_cast<std::remove_const_t<T>>(value);
  for (std::size_t i0 = 0U; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < output.dim2(); ++i2) {
        std::fill_n(output.data() + i0 * output.dim0_stride() + i1 * output.dim1_stride() + i2 * output.dim2_stride(),
                    output.dim3(), output_value);
      }
    }
  }
  return true;
}

template <typename T, typename Value> [[nodiscard]] bool fill_vector_backend(VectorView<T> output, const Value& value) {
  const auto output_value = static_cast<std::remove_const_t<T>>(value);
  if (prefer_intel_vector_fill(output) && intel::fill(output, output_value)) {
    return true;
  }
  if (prefer_eigen_vector_fill(output) && eigen::fill(output, output_value)) {
    return true;
  }
  return false;
}
} // namespace detail

template <typename T, typename Value> void fill(VectorView<T> output, const Value& value) {
  if (output.is_contiguous()) {
    if (detail::fill_vector_backend(output, value)) {
      return;
    }
    std::fill_n(output.data(), output.size(), static_cast<std::remove_const_t<T>>(value));
    return;
  }
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = value;
  }
}

template <typename T, typename Value> void fill(MatrixView<T> output, const Value& value) {
  if (output.is_contiguous()) {
    fill(VectorView<T>(output.data(), output.size()), value);
    return;
  }
  if (detail::fill_inner_contiguous_blocks(output, value)) {
    return;
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = value;
    }
  }
}

template <typename T, typename Value> void fill(ImageView<T> output, const Value& value) {
  if (output.is_contiguous()) {
    fill(VectorView<T>(output.data(), output.size()), value);
    return;
  }
  if (detail::fill_inner_contiguous_blocks(output, value)) {
    return;
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = value;
    }
  }
}

template <typename T, typename Value> void fill(CubeView<T> output, const Value& value) {
  if (output.is_contiguous()) {
    fill(VectorView<T>(output.data(), output.size()), value);
    return;
  }
  if (detail::fill_inner_contiguous_blocks(output, value)) {
    return;
  }
  for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
        output(i0, i1, i2) = value;
      }
    }
  }
}

template <typename T, typename Value> void fill(Array4DView<T> output, const Value& value) {
  if (output.is_contiguous()) {
    fill(VectorView<T>(output.data(), output.size()), value);
    return;
  }
  if (detail::fill_inner_contiguous_blocks(output, value)) {
    return;
  }
  for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
        for (std::size_t i3 = 0; i3 < output.dim3(); ++i3) {
          output(i0, i1, i2, i3) = value;
        }
      }
    }
  }
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(VectorView<T> output, const StartT& start, const StopT& stop) {
  const auto count = output.size();
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < count; ++index) {
      detail::assign_linspace_value(output_data[index], start, stop, index, count);
    }
    return;
  }
  for (std::size_t index = 0; index < count; ++index) {
    detail::assign_linspace_value(output[index], start, stop, index, count);
  }
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(MatrixView<T> output, const StartT& start, const StopT& stop) {
  const auto count = output.size();
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < count; ++index) {
      detail::assign_linspace_value(output_data[index], start, stop, index, count);
    }
    return;
  }
  std::size_t index = 0U;
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      detail::assign_linspace_value(output(row, col), start, stop, index++, count);
    }
  }
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(ImageView<T> output, const StartT& start, const StopT& stop) {
  const auto count = output.size();
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < count; ++index) {
      detail::assign_linspace_value(output_data[index], start, stop, index, count);
    }
    return;
  }
  std::size_t index = 0U;
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      detail::assign_linspace_value(output(row, col), start, stop, index++, count);
    }
  }
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(CubeView<T> output, const StartT& start, const StopT& stop) {
  const auto count = output.size();
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < count; ++index) {
      detail::assign_linspace_value(output_data[index], start, stop, index, count);
    }
    return;
  }
  std::size_t index = 0U;
  for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
        detail::assign_linspace_value(output(i0, i1, i2), start, stop, index++, count);
      }
    }
  }
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(Array4DView<T> output, const StartT& start, const StopT& stop) {
  const auto count = output.size();
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < count; ++index) {
      detail::assign_linspace_value(output_data[index], start, stop, index, count);
    }
    return;
  }
  std::size_t index = 0U;
  for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
        for (std::size_t i3 = 0; i3 < output.dim3(); ++i3) {
          detail::assign_linspace_value(output(i0, i1, i2, i3), start, stop, index++, count);
        }
      }
    }
  }
}

template <typename T> void set_identity(MatrixView<T> output) {
  fill(output, T{});
  const auto diagonal_count = std::min(output.rows(), output.cols());
  for (std::size_t index = 0; index < diagonal_count; ++index) {
    output(index, index) = static_cast<std::remove_const_t<T>>(1);
  }
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(VectorView<T> output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
    return;
  }
  for (std::size_t index = 0; index < output.size(); ++index) {
    output[index] = detail::sample_uniform_random_value<T>(lower, upper, generator);
  }
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(VectorView<T> output, const LowerT& lower, const UpperT& upper) {
  auto generator = detail::make_uniform_random_generator();
  fill_uniform_random(output, lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(MatrixView<T> output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
    return;
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
  }
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(MatrixView<T> output, const LowerT& lower, const UpperT& upper) {
  auto generator = detail::make_uniform_random_generator();
  fill_uniform_random(output, lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(ImageView<T> output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
    return;
  }
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
  }
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(ImageView<T> output, const LowerT& lower, const UpperT& upper) {
  auto generator = detail::make_uniform_random_generator();
  fill_uniform_random(output, lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(CubeView<T> output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
    return;
  }
  for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
        output(i0, i1, i2) = detail::sample_uniform_random_value<T>(lower, upper, generator);
      }
    }
  }
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(CubeView<T> output, const LowerT& lower, const UpperT& upper) {
  auto generator = detail::make_uniform_random_generator();
  fill_uniform_random(output, lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(Array4DView<T> output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  if (output.is_contiguous()) {
    auto* output_data = output.data();
    for (std::size_t index = 0; index < output.size(); ++index) {
      output_data[index] = detail::sample_uniform_random_value<T>(lower, upper, generator);
    }
    return;
  }
  for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
        for (std::size_t i3 = 0; i3 < output.dim3(); ++i3) {
          output(i0, i1, i2, i3) = detail::sample_uniform_random_value<T>(lower, upper, generator);
        }
      }
    }
  }
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(Array4DView<T> output, const LowerT& lower, const UpperT& upper) {
  auto generator = detail::make_uniform_random_generator();
  fill_uniform_random(output, lower, upper, generator);
}

template <typename T, typename Value> void fill(PooledVector<T>& output, const Value& value) {
  fill(output.view(), value);
}

template <typename T, typename Value> void fill(PooledMatrix<T>& output, const Value& value) {
  fill(output.view(), value);
}

template <typename T, typename Value> void fill(PooledImage<T>& output, const Value& value) {
  fill(output.view(), value);
}

template <typename T, typename Value> void fill(PooledCube<T>& output, const Value& value) {
  fill(output.view(), value);
}

template <typename T, typename Value> void fill(PooledArray4D<T>& output, const Value& value) {
  fill(output.view(), value);
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(PooledVector<T>& output, const StartT& start, const StopT& stop) {
  fill_linspace(output.view(), start, stop);
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(PooledMatrix<T>& output, const StartT& start, const StopT& stop) {
  fill_linspace(output.view(), start, stop);
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(PooledImage<T>& output, const StartT& start, const StopT& stop) {
  fill_linspace(output.view(), start, stop);
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(PooledCube<T>& output, const StartT& start, const StopT& stop) {
  fill_linspace(output.view(), start, stop);
}

template <typename T, typename StartT, typename StopT>
void fill_linspace(PooledArray4D<T>& output, const StartT& start, const StopT& stop) {
  fill_linspace(output.view(), start, stop);
}

template <typename T> void set_identity(PooledMatrix<T>& output) {
  set_identity(output.view());
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(PooledVector<T>& output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  fill_uniform_random(output.view(), lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(PooledVector<T>& output, const LowerT& lower, const UpperT& upper) {
  fill_uniform_random(output.view(), lower, upper);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(PooledMatrix<T>& output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  fill_uniform_random(output.view(), lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(PooledMatrix<T>& output, const LowerT& lower, const UpperT& upper) {
  fill_uniform_random(output.view(), lower, upper);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(PooledImage<T>& output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  fill_uniform_random(output.view(), lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(PooledImage<T>& output, const LowerT& lower, const UpperT& upper) {
  fill_uniform_random(output.view(), lower, upper);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(PooledCube<T>& output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  fill_uniform_random(output.view(), lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(PooledCube<T>& output, const LowerT& lower, const UpperT& upper) {
  fill_uniform_random(output.view(), lower, upper);
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
void fill_uniform_random(PooledArray4D<T>& output, const LowerT& lower, const UpperT& upper,
                         UniformRandomBitGenerator& generator) {
  fill_uniform_random(output.view(), lower, upper, generator);
}

template <typename T, typename LowerT, typename UpperT>
void fill_uniform_random(PooledArray4D<T>& output, const LowerT& lower, const UpperT& upper) {
  fill_uniform_random(output.view(), lower, upper);
}

template <typename T, typename Value>
[[nodiscard]] PooledVector<T> full_vector(std::size_t size, const Value& value,
                                          ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>::constant(size, value, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledVector<T> zeros_vector(std::size_t size, ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>::zeros(size, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledVector<T> ones_vector(std::size_t size, ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>::ones(size, std::move(properties));
}

template <typename T, typename StartT, typename StopT>
[[nodiscard]] PooledVector<T> linspace_vector(std::size_t size, const StartT& start, const StopT& stop,
                                              ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>::linspace(size, start, stop, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
[[nodiscard]] PooledVector<T> uniform_random_vector(std::size_t size, const LowerT& lower, const UpperT& upper,
                                                    UniformRandomBitGenerator& generator,
                                                    ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>::uniform_random(size, lower, upper, generator, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT>
[[nodiscard]] PooledVector<T> uniform_random_vector(std::size_t size, const LowerT& lower, const UpperT& upper,
                                                    ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>::uniform_random(size, lower, upper, std::move(properties));
}

template <typename T, typename Value>
[[nodiscard]] PooledMatrix<T> full_matrix(std::size_t rows, std::size_t cols, const Value& value,
                                          ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::constant(rows, cols, value, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledMatrix<T> zeros_matrix(std::size_t rows, std::size_t cols,
                                           ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::zeros(rows, cols, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledMatrix<T> ones_matrix(std::size_t rows, std::size_t cols,
                                          ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::ones(rows, cols, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledMatrix<T> identity_matrix(std::size_t rows, std::size_t cols,
                                              ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::identity(rows, cols, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledMatrix<T> eye_matrix(std::size_t rows, std::size_t cols,
                                         ksj::memory::AllocationProperties properties = {}) {
  return identity_matrix<T>(rows, cols, std::move(properties));
}

template <typename T, typename StartT, typename StopT>
[[nodiscard]] PooledMatrix<T> linspace_matrix(std::size_t rows, std::size_t cols, const StartT& start,
                                              const StopT& stop, ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::linspace(rows, cols, start, stop, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
[[nodiscard]] PooledMatrix<T> uniform_random_matrix(std::size_t rows, std::size_t cols, const LowerT& lower,
                                                    const UpperT& upper, UniformRandomBitGenerator& generator,
                                                    ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::uniform_random(rows, cols, lower, upper, generator, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT>
[[nodiscard]] PooledMatrix<T> uniform_random_matrix(std::size_t rows, std::size_t cols, const LowerT& lower,
                                                    const UpperT& upper,
                                                    ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>::uniform_random(rows, cols, lower, upper, std::move(properties));
}

template <typename T, typename Value>
[[nodiscard]] PooledImage<T> full_image(std::size_t rows, std::size_t cols, const Value& value,
                                        ksj::memory::AllocationProperties properties = {}) {
  return PooledImage<T>::constant(rows, cols, value, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledImage<T> zeros_image(std::size_t rows, std::size_t cols,
                                         ksj::memory::AllocationProperties properties = {}) {
  return PooledImage<T>::zeros(rows, cols, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledImage<T> ones_image(std::size_t rows, std::size_t cols,
                                        ksj::memory::AllocationProperties properties = {}) {
  return PooledImage<T>::ones(rows, cols, std::move(properties));
}

template <typename T, typename StartT, typename StopT>
[[nodiscard]] PooledImage<T> linspace_image(std::size_t rows, std::size_t cols, const StartT& start, const StopT& stop,
                                            ksj::memory::AllocationProperties properties = {}) {
  return PooledImage<T>::linspace(rows, cols, start, stop, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
[[nodiscard]] PooledImage<T> uniform_random_image(std::size_t rows, std::size_t cols, const LowerT& lower,
                                                  const UpperT& upper, UniformRandomBitGenerator& generator,
                                                  ksj::memory::AllocationProperties properties = {}) {
  return PooledImage<T>::uniform_random(rows, cols, lower, upper, generator, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT>
[[nodiscard]] PooledImage<T> uniform_random_image(std::size_t rows, std::size_t cols, const LowerT& lower,
                                                  const UpperT& upper,
                                                  ksj::memory::AllocationProperties properties = {}) {
  return PooledImage<T>::uniform_random(rows, cols, lower, upper, std::move(properties));
}

template <typename T, typename Value>
[[nodiscard]] PooledCube<T> full_cube(std::size_t dim0, std::size_t dim1, std::size_t dim2, const Value& value,
                                      ksj::memory::AllocationProperties properties = {}) {
  return PooledCube<T>::constant(dim0, dim1, dim2, value, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledCube<T> zeros_cube(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                       ksj::memory::AllocationProperties properties = {}) {
  return PooledCube<T>::zeros(dim0, dim1, dim2, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledCube<T> ones_cube(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                      ksj::memory::AllocationProperties properties = {}) {
  return PooledCube<T>::ones(dim0, dim1, dim2, std::move(properties));
}

template <typename T, typename StartT, typename StopT>
[[nodiscard]] PooledCube<T> linspace_cube(std::size_t dim0, std::size_t dim1, std::size_t dim2, const StartT& start,
                                          const StopT& stop, ksj::memory::AllocationProperties properties = {}) {
  return PooledCube<T>::linspace(dim0, dim1, dim2, start, stop, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
[[nodiscard]] PooledCube<T>
uniform_random_cube(std::size_t dim0, std::size_t dim1, std::size_t dim2, const LowerT& lower, const UpperT& upper,
                    UniformRandomBitGenerator& generator, ksj::memory::AllocationProperties properties = {}) {
  return PooledCube<T>::uniform_random(dim0, dim1, dim2, lower, upper, generator, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT>
[[nodiscard]] PooledCube<T> uniform_random_cube(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                const LowerT& lower, const UpperT& upper,
                                                ksj::memory::AllocationProperties properties = {}) {
  return PooledCube<T>::uniform_random(dim0, dim1, dim2, lower, upper, std::move(properties));
}

template <typename T, typename Value>
[[nodiscard]] PooledArray4D<T> full_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                            const Value& value, ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>::constant(dim0, dim1, dim2, dim3, value, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledArray4D<T> zeros_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                             ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>::zeros(dim0, dim1, dim2, dim3, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledArray4D<T> ones_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                            ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>::ones(dim0, dim1, dim2, dim3, std::move(properties));
}

template <typename T, typename StartT, typename StopT>
[[nodiscard]] PooledArray4D<T> linspace_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                                const StartT& start, const StopT& stop,
                                                ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>::linspace(dim0, dim1, dim2, dim3, start, stop, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
[[nodiscard]] PooledArray4D<T> uniform_random_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                      std::size_t dim3, const LowerT& lower, const UpperT& upper,
                                                      UniformRandomBitGenerator& generator,
                                                      ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>::uniform_random(dim0, dim1, dim2, dim3, lower, upper, generator, std::move(properties));
}

template <typename T, typename LowerT, typename UpperT>
[[nodiscard]] PooledArray4D<T> uniform_random_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                      std::size_t dim3, const LowerT& lower, const UpperT& upper,
                                                      ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>::uniform_random(dim0, dim1, dim2, dim3, lower, upper, std::move(properties));
}

} // namespace ksj::array
