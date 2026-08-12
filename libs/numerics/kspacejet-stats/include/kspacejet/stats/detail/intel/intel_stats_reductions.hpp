#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_types.hpp"

namespace ksj::stats::detail::intel {

[[nodiscard]] bool sum(ksj::array::VectorView<const float> input, float& output);
[[nodiscard]] bool sum(ksj::array::VectorView<const double> input, double& output);

template <typename T> [[nodiscard]] bool sum(ksj::array::VectorView<const T> input, T& output) {
  if constexpr (ipp_real_scalar_v<T>) {
    return sum(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool sum_abs(ksj::array::VectorView<const float> input, float& output);
[[nodiscard]] bool sum_abs(ksj::array::VectorView<const double> input, double& output);
[[nodiscard]] bool sum_abs(ksj::array::VectorView<const ksj::base::cf32> input, float& output);
[[nodiscard]] bool sum_abs(ksj::array::VectorView<const ksj::base::cf64> input, double& output);

template <typename T, typename Output>
[[nodiscard]] bool sum_abs(ksj::array::VectorView<const T> input, Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return sum_abs(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool max_index(ksj::array::VectorView<const float> input, std::size_t& output);
[[nodiscard]] bool max_index(ksj::array::VectorView<const double> input, std::size_t& output);

template <typename T> [[nodiscard]] bool max_index(ksj::array::VectorView<const T> input, std::size_t& output) {
  if constexpr (ipp_real_scalar_v<T>) {
    return max_index(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool min_index(ksj::array::VectorView<const float> input, std::size_t& output);
[[nodiscard]] bool min_index(ksj::array::VectorView<const double> input, std::size_t& output);

template <typename T> [[nodiscard]] bool min_index(ksj::array::VectorView<const T> input, std::size_t& output) {
  if constexpr (ipp_real_scalar_v<T>) {
    return min_index(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

} // namespace ksj::stats::detail::intel
