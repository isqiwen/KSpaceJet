#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_types.hpp"

namespace ksj::signal::detail::intel {

[[nodiscard]] bool exponential_window(ksj::array::VectorView<float> output, float alpha, float exponent);
[[nodiscard]] bool exponential_window(ksj::array::VectorView<double> output, double alpha, double exponent);

template <typename T>
[[nodiscard]] bool exponential_window(ksj::array::VectorView<T> output, const T alpha, const T exponent) {
  if constexpr (!ipp_real_scalar_v<T>) {
    (void)output;
    (void)alpha;
    (void)exponent;
    return false;
  } else {
    return exponential_window(output, alpha, exponent);
  }
}

} // namespace ksj::signal::detail::intel
