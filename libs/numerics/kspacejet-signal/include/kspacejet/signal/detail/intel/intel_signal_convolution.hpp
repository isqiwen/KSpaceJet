#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_types.hpp"

namespace ksj::signal::detail::intel {

[[nodiscard]] bool convolve(ksj::array::VectorView<const float> signal, ksj::array::VectorView<const float> kernel,
                            ksj::array::VectorView<float> output);
[[nodiscard]] bool convolve(ksj::array::VectorView<const double> signal, ksj::array::VectorView<const double> kernel,
                            ksj::array::VectorView<double> output);

template <typename T>
[[nodiscard]] bool convolve(ksj::array::VectorView<const T> signal, ksj::array::VectorView<const T> kernel,
                            ksj::array::VectorView<T> output) {
  if constexpr (ipp_real_scalar_v<T>) {
    return convolve(signal, kernel, output);
  } else {
    (void)signal;
    (void)kernel;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool convolve2d_full(ksj::array::MatrixView<const float> input,
                                   ksj::array::MatrixView<const float> kernel, ksj::array::MatrixView<float> output);

template <typename T>
[[nodiscard]] bool convolve2d_full(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<const T> kernel,
                                   ksj::array::MatrixView<T> output) {
  if constexpr (std::is_same_v<T, float>) {
    return convolve2d_full(input, kernel, output);
  } else {
    (void)input;
    (void)kernel;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool correlate2d_same(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                                    ksj::array::ImageView<float> output);

template <typename T>
[[nodiscard]] bool correlate2d_same(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                                    ksj::array::ImageView<T> output) {
  if constexpr (std::is_same_v<T, float>) {
    return correlate2d_same(input, kernel, output);
  } else {
    (void)input;
    (void)kernel;
    (void)output;
    return false;
  }
}

} // namespace ksj::signal::detail::intel
