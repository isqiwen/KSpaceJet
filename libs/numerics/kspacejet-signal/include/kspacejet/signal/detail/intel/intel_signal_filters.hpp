#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/types.hpp"
#include "kspacejet/signal/workspace.hpp"

#include <cstddef>
#include <type_traits>

namespace ksj::signal::detail::intel {

[[nodiscard]] bool median_filter_in_place(ksj::array::VectorView<float> input, std::size_t kernel_size,
                                          SignalBorderMode border_mode);
[[nodiscard]] bool median_filter_in_place(ksj::array::VectorView<float> input, std::size_t kernel_size,
                                          SignalBorderMode border_mode, MedianFilterWorkspace<float>& workspace);

[[nodiscard]] bool fir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> taps,
                              ksj::array::VectorView<float> output);
[[nodiscard]] bool fir_filter(ksj::array::VectorView<const double> input, ksj::array::VectorView<const double> taps,
                              ksj::array::VectorView<double> output);
[[nodiscard]] bool fir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> taps,
                              ksj::array::VectorView<float> output, FirFilterWorkspace<float>& workspace);
[[nodiscard]] bool fir_filter(ksj::array::VectorView<const double> input, ksj::array::VectorView<const double> taps,
                              ksj::array::VectorView<double> output, FirFilterWorkspace<double>& workspace);

[[nodiscard]] bool iir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> numerator,
                              ksj::array::VectorView<const float> denominator, ksj::array::VectorView<float> output);
[[nodiscard]] bool iir_filter(ksj::array::VectorView<const double> input,
                              ksj::array::VectorView<const double> numerator,
                              ksj::array::VectorView<const double> denominator, ksj::array::VectorView<double> output);
[[nodiscard]] bool iir_filter(ksj::array::VectorView<const float> input, ksj::array::VectorView<const float> numerator,
                              ksj::array::VectorView<const float> denominator, ksj::array::VectorView<float> output,
                              IirFilterWorkspace<float>& workspace);
[[nodiscard]] bool iir_filter(ksj::array::VectorView<const double> input,
                              ksj::array::VectorView<const double> numerator,
                              ksj::array::VectorView<const double> denominator, ksj::array::VectorView<double> output,
                              IirFilterWorkspace<double>& workspace);

template <typename T>
[[nodiscard]] bool median_filter_in_place(ksj::array::VectorView<T> input, const std::size_t kernel_size,
                                          const SignalBorderMode border_mode) {
  if constexpr (std::is_same_v<T, float>) {
    return median_filter_in_place(input, kernel_size, border_mode);
  } else {
    (void)input;
    (void)kernel_size;
    (void)border_mode;
    return false;
  }
}

template <typename T>
[[nodiscard]] bool median_filter_in_place(ksj::array::VectorView<T> input, const std::size_t kernel_size,
                                          const SignalBorderMode border_mode, MedianFilterWorkspace<T>& workspace) {
  if constexpr (std::is_same_v<T, float>) {
    return median_filter_in_place(input, kernel_size, border_mode, workspace);
  } else {
    (void)input;
    (void)kernel_size;
    (void)border_mode;
    (void)workspace;
    return false;
  }
}

template <typename T>
[[nodiscard]] bool fir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> taps,
                              ksj::array::VectorView<T> output) {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return fir_filter(input, taps, output);
  } else {
    (void)input;
    (void)taps;
    (void)output;
    return false;
  }
}

template <typename T>
[[nodiscard]] bool fir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> taps,
                              ksj::array::VectorView<T> output, FirFilterWorkspace<T>& workspace) {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return fir_filter(input, taps, output, workspace);
  } else {
    (void)input;
    (void)taps;
    (void)output;
    (void)workspace;
    return false;
  }
}

template <typename T>
[[nodiscard]] bool iir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> numerator,
                              ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output) {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return iir_filter(input, numerator, denominator, output);
  } else {
    (void)input;
    (void)numerator;
    (void)denominator;
    (void)output;
    return false;
  }
}

template <typename T>
[[nodiscard]] bool iir_filter(ksj::array::VectorView<const T> input, ksj::array::VectorView<const T> numerator,
                              ksj::array::VectorView<const T> denominator, ksj::array::VectorView<T> output,
                              IirFilterWorkspace<T>& workspace) {
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return iir_filter(input, numerator, denominator, output, workspace);
  } else {
    (void)input;
    (void)numerator;
    (void)denominator;
    (void)output;
    (void)workspace;
    return false;
  }
}

} // namespace ksj::signal::detail::intel
