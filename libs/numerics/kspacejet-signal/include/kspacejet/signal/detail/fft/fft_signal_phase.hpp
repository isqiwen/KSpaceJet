#pragma once

#include "kspacejet/array/array.hpp"

#include <type_traits>

namespace ksj::signal::detail::fft {

void unwrap_phase_laplacian_2d(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output);
void unwrap_phase_laplacian_2d(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output);

template <typename T> void unwrap_phase_laplacian_2d(ksj::array::ImageView<const T>, ksj::array::ImageView<T>) {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                "unwrap_phase_laplacian_2d supports float and double");
}

} // namespace ksj::signal::detail::fft
