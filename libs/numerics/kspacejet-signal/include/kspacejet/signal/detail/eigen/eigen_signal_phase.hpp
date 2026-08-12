#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::signal::detail::eigen {

template <typename T> [[nodiscard]] T wrap_phase_scalar(T value, const T limit) {
  if (limit <= T{0}) {
    throw std::invalid_argument("wrap_phase limit must be positive");
  }

  const auto period = T{2} * limit;
  while (value <= -limit) {
    value += period;
  }
  while (value > limit) {
    value -= period;
  }
  return value;
}

template <typename T>
void wrap_phase(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output, const T limit) {
  if (output.size() != input.size()) {
    throw std::invalid_argument("wrap_phase output dimension mismatch");
  }
  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0; index < input.size(); ++index) {
      output_data[index] = wrap_phase_scalar(input_data[index], limit);
    }
    return;
  }
  for (std::size_t index = 0; index < input.size(); ++index) {
    output(index) = wrap_phase_scalar(input(index), limit);
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> wrap_phase(const ksj::array::PooledVector<T>& input,
                                                     const T limit = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  wrap_phase(ksj::array::as_const_view(input.view()), output.view(), limit);
  return output;
}

template <typename T>
void wrap_phase(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T limit) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("wrap_phase output dimension mismatch");
  }
  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    for (std::size_t index = 0; index < input.size(); ++index) {
      output_data[index] = wrap_phase_scalar(input_data[index], limit);
    }
    return;
  }
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      output(row, col) = wrap_phase_scalar(input(row, col), limit);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> wrap_phase(const ksj::array::PooledImage<T>& input, const T limit) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  wrap_phase(ksj::array::as_const_view(input.view()), output.view(), limit);
  return output;
}

template <typename T>
void unwrap_phase(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output, const T discontinuity) {
  if (discontinuity <= T{0}) {
    throw std::invalid_argument("unwrap_phase discontinuity must be positive");
  }
  if (output.size() != input.size()) {
    throw std::invalid_argument("unwrap_phase output dimension mismatch");
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp = ksj::array::make_pooled_vector<T>(input.size());
    ksj::signal::detail::eigen::unwrap_phase(input, temp.view(), discontinuity);
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (input.empty()) {
    return;
  }

  output(0) = input(0);
  T correction{};
  const auto period = T{2} * std::numbers::pi_v<T>;
  if (input.is_contiguous() && output.is_contiguous()) {
    const auto* input_data = input.data();
    auto* output_data = output.data();
    output_data[0] = input_data[0];
    for (std::size_t index = 1; index < input.size(); ++index) {
      const auto delta = input_data[index] - input_data[index - 1U];
      if (delta > discontinuity) {
        correction -= period;
      } else if (delta < -discontinuity) {
        correction += period;
      }
      output_data[index] = input_data[index] + correction;
    }
    return;
  }
  for (std::size_t index = 1; index < input.size(); ++index) {
    const auto delta = input(index) - input(index - 1U);
    if (delta > discontinuity) {
      correction -= period;
    } else if (delta < -discontinuity) {
      correction += period;
    }
    output(index) = input(index) + correction;
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> unwrap_phase(const ksj::array::PooledVector<T>& input,
                                                       const T discontinuity = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  unwrap_phase(ksj::array::as_const_view(input.view()), output.view(), discontinuity);
  return output;
}

template <typename T>
void unwrap_phase_2d(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T discontinuity) {
  if (discontinuity <= T{0}) {
    throw std::invalid_argument("unwrap_phase_2d discontinuity must be positive");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("unwrap_phase_2d output dimension mismatch");
  }
  if (input.data() == output.data() && !input.empty()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    ksj::signal::detail::eigen::unwrap_phase_2d(input, temp.view(), discontinuity);
    ksj::array::copy(temp.view(), output);
    return;
  }
  if (input.empty()) {
    return;
  }

  auto row_unwrapped = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  const auto period = T{2} * std::numbers::pi_v<T>;

  for (std::size_t row = 0; row < input.rows(); ++row) {
    row_unwrapped(row, 0) = input(row, 0);
    T correction{};
    for (std::size_t col = 1; col < input.cols(); ++col) {
      const auto delta = input(row, col) - input(row, col - 1U);
      if (delta > discontinuity) {
        correction -= period;
      } else if (delta < -discontinuity) {
        correction += period;
      }
      row_unwrapped(row, col) = input(row, col) + correction;
    }
  }

  for (std::size_t col = 0; col < input.cols(); ++col) {
    output(0, col) = row_unwrapped(0, col);
    T correction{};
    for (std::size_t row = 1; row < input.rows(); ++row) {
      const auto delta = row_unwrapped(row, col) - row_unwrapped(row - 1U, col);
      if (delta > discontinuity) {
        correction -= period;
      } else if (delta < -discontinuity) {
        correction += period;
      }
      output(row, col) = row_unwrapped(row, col) + correction;
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unwrap_phase_2d(const ksj::array::PooledImage<T>& input,
                                                         const T discontinuity = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  unwrap_phase_2d(ksj::array::as_const_view(input.view()), output.view(), discontinuity);
  return output;
}
} // namespace ksj::signal::detail::eigen
