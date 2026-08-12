#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_types.hpp"

namespace ksj::stats::detail::intel {

[[nodiscard]] bool mean(ksj::array::VectorView<const float> input, float& output);
[[nodiscard]] bool mean(ksj::array::VectorView<const double> input, double& output);

template <typename T> [[nodiscard]] bool mean(ksj::array::VectorView<const T> input, T& output) {
  if constexpr (ipp_real_scalar_v<T>) {
    return mean(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

} // namespace ksj::stats::detail::intel
