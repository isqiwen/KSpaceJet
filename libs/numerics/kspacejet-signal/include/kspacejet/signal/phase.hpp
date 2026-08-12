#pragma once

/// Phase unwrapping, phase-table construction, and complex phasor signal operations.

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_phase.hpp"
#include "kspacejet/signal/detail/fft/fft_signal_phase.hpp"
#include <numbers>
#include <stdexcept>
#include <type_traits>

namespace ksj::signal {

template <typename T>
void wrap_phase(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                const T limit = std::numbers::pi_v<T>) {
  detail::eigen::wrap_phase(input, output, limit);
}

template <typename T>
void wrap_phase(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output,
                const T limit = std::numbers::pi_v<T>) {
  wrap_phase(ksj::array::as_const_view(input), output, limit);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> wrap_phase(const ksj::array::PooledVector<T>& input,
                                                     const T limit = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  wrap_phase(ksj::array::as_const_view(input.view()), output.view(), limit);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>>
wrap_phase(ksj::array::VectorView<T> input,
           const std::remove_const_t<T> limit = std::numbers::pi_v<std::remove_const_t<T>>) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  wrap_phase(ksj::array::as_const_view(input), output.view(), limit);
  return output;
}

template <typename T>
void wrap_phase(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                const T limit = std::numbers::pi_v<T>) {
  detail::eigen::wrap_phase(input, output, limit);
}

template <typename T>
void wrap_phase(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output,
                const T limit = std::numbers::pi_v<T>) {
  wrap_phase(ksj::array::as_const_view(input), output, limit);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> wrap_phase(const ksj::array::PooledImage<T>& input,
                                                    const T limit = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  wrap_phase(ksj::array::as_const_view(input.view()), output.view(), limit);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<std::remove_const_t<T>>
wrap_phase(ksj::array::ImageView<T> input,
           const std::remove_const_t<T> limit = std::numbers::pi_v<std::remove_const_t<T>>) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_image<value_type>(input.rows(), input.cols());
  wrap_phase(ksj::array::as_const_view(input), output.view(), limit);
  return output;
}

template <typename T>
void unwrap_phase(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                  const T discontinuity = std::numbers::pi_v<T>) {
  detail::eigen::unwrap_phase(input, output, discontinuity);
}

template <typename T>
void unwrap_phase(ksj::array::VectorView<T> input, ksj::array::VectorView<T> output,
                  const T discontinuity = std::numbers::pi_v<T>) {
  unwrap_phase(ksj::array::as_const_view(input), output, discontinuity);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> unwrap_phase(const ksj::array::PooledVector<T>& input,
                                                       const T discontinuity = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  unwrap_phase(ksj::array::as_const_view(input.view()), output.view(), discontinuity);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<std::remove_const_t<T>>
unwrap_phase(ksj::array::VectorView<T> input,
             const std::remove_const_t<T> discontinuity = std::numbers::pi_v<std::remove_const_t<T>>) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_vector<value_type>(input.size());
  unwrap_phase(ksj::array::as_const_view(input), output.view(), discontinuity);
  return output;
}

template <typename T>
void unwrap_phase_2d(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                     const T discontinuity = std::numbers::pi_v<T>) {
  detail::eigen::unwrap_phase_2d(input, output, discontinuity);
}

template <typename T>
void unwrap_phase_2d(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output,
                     const T discontinuity = std::numbers::pi_v<T>) {
  unwrap_phase_2d(ksj::array::as_const_view(input), output, discontinuity);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unwrap_phase_2d(const ksj::array::PooledImage<T>& input,
                                                         const T discontinuity = std::numbers::pi_v<T>) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  unwrap_phase_2d(ksj::array::as_const_view(input.view()), output.view(), discontinuity);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<std::remove_const_t<T>>
unwrap_phase_2d(ksj::array::ImageView<T> input,
                const std::remove_const_t<T> discontinuity = std::numbers::pi_v<std::remove_const_t<T>>) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_image<value_type>(input.rows(), input.cols());
  unwrap_phase_2d(ksj::array::as_const_view(input), output.view(), discontinuity);
  return output;
}

template <typename T>
void unwrap_phase_laplacian_2d(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output) {
  if (output.shape().extents != input.shape().extents) {
    throw std::invalid_argument("unwrap_phase_laplacian_2d output dimension mismatch");
  }
  detail::fft::unwrap_phase_laplacian_2d(input, output);
}

template <typename T> void unwrap_phase_laplacian_2d(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output) {
  unwrap_phase_laplacian_2d(ksj::array::as_const_view(input), output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unwrap_phase_laplacian_2d(const ksj::array::PooledImage<T>& input) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  unwrap_phase_laplacian_2d(ksj::array::as_const_view(input.view()), output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<std::remove_const_t<T>>
unwrap_phase_laplacian_2d(ksj::array::ImageView<T> input) {
  using value_type = std::remove_const_t<T>;
  auto output = ksj::array::make_pooled_image<value_type>(input.rows(), input.cols());
  unwrap_phase_laplacian_2d(ksj::array::as_const_view(input), output.view());
  return output;
}

} // namespace ksj::signal
