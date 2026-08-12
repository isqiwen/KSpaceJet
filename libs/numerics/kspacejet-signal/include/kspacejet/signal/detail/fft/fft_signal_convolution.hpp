#pragma once

#include "kspacejet/array/array.hpp"

#include <type_traits>

namespace ksj::signal::detail::fft {

void correlate2d_same(ksj::array::ImageView<const float> input, ksj::array::ImageView<const float> kernel,
                      ksj::array::ImageView<float> output);
void correlate2d_same(ksj::array::ImageView<const double> input, ksj::array::ImageView<const double> kernel,
                      ksj::array::ImageView<double> output);

template <typename T>
void correlate2d_same(ksj::array::ImageView<const T>, ksj::array::ImageView<const T>, ksj::array::ImageView<T>) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "FFT signal convolution supports float and double");
}

} // namespace ksj::signal::detail::fft
