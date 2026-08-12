#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_types.hpp"

namespace ksj::stats::detail::intel {

[[nodiscard]] bool sum_of_squares(ksj::array::VectorView<const float> input, float& output);
[[nodiscard]] bool sum_of_squares(ksj::array::VectorView<const double> input, double& output);
[[nodiscard]] bool sum_of_squares(ksj::array::VectorView<const ksj::base::cf32> input, float& output);
[[nodiscard]] bool sum_of_squares(ksj::array::VectorView<const ksj::base::cf64> input, double& output);

template <typename T, typename Output>
[[nodiscard]] bool sum_of_squares(ksj::array::VectorView<const T> input, Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return sum_of_squares(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool root_sum_of_squares(ksj::array::VectorView<const float> input, float& output);
[[nodiscard]] bool root_sum_of_squares(ksj::array::VectorView<const double> input, double& output);
[[nodiscard]] bool root_sum_of_squares(ksj::array::VectorView<const ksj::base::cf32> input, float& output);
[[nodiscard]] bool root_sum_of_squares(ksj::array::VectorView<const ksj::base::cf64> input, double& output);

template <typename T, typename Output>
[[nodiscard]] bool root_sum_of_squares(ksj::array::VectorView<const T> input, Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return root_sum_of_squares(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool max_abs(ksj::array::VectorView<const float> input, float& output);
[[nodiscard]] bool max_abs(ksj::array::VectorView<const double> input, double& output);
[[nodiscard]] bool max_abs(ksj::array::VectorView<const ksj::base::cf32> input, float& output);
[[nodiscard]] bool max_abs(ksj::array::VectorView<const ksj::base::cf64> input, double& output);

template <typename T, typename Output>
[[nodiscard]] bool max_abs(ksj::array::VectorView<const T> input, Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return max_abs(input, output);
  } else {
    (void)input;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool l1_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs,
                               float& output);
[[nodiscard]] bool l1_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs,
                               double& output);
[[nodiscard]] bool l1_distance(ksj::array::VectorView<const ksj::base::cf32> lhs,
                               ksj::array::VectorView<const ksj::base::cf32> rhs, float& output);
[[nodiscard]] bool l1_distance(ksj::array::VectorView<const ksj::base::cf64> lhs,
                               ksj::array::VectorView<const ksj::base::cf64> rhs, double& output);

[[nodiscard]] bool l2_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs,
                               float& output);
[[nodiscard]] bool l2_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs,
                               double& output);
[[nodiscard]] bool l2_distance(ksj::array::VectorView<const ksj::base::cf32> lhs,
                               ksj::array::VectorView<const ksj::base::cf32> rhs, float& output);
[[nodiscard]] bool l2_distance(ksj::array::VectorView<const ksj::base::cf64> lhs,
                               ksj::array::VectorView<const ksj::base::cf64> rhs, double& output);

[[nodiscard]] bool linf_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs,
                                 float& output);
[[nodiscard]] bool linf_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs,
                                 double& output);
[[nodiscard]] bool linf_distance(ksj::array::VectorView<const ksj::base::cf32> lhs,
                                 ksj::array::VectorView<const ksj::base::cf32> rhs, float& output);
[[nodiscard]] bool linf_distance(ksj::array::VectorView<const ksj::base::cf64> lhs,
                                 ksj::array::VectorView<const ksj::base::cf64> rhs, double& output);

template <typename T, typename Output>
[[nodiscard]] bool l1_distance(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs,
                               Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return l1_distance(lhs, rhs, output);
  } else {
    (void)lhs;
    (void)rhs;
    (void)output;
    return false;
  }
}

template <typename T, typename Output>
[[nodiscard]] bool l2_distance(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs,
                               Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return l2_distance(lhs, rhs, output);
  } else {
    (void)lhs;
    (void)rhs;
    (void)output;
    return false;
  }
}

template <typename T, typename Output>
[[nodiscard]] bool linf_distance(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs,
                                 Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return linf_distance(lhs, rhs, output);
  } else {
    (void)lhs;
    (void)rhs;
    (void)output;
    return false;
  }
}

[[nodiscard]] bool squared_l2_distance(ksj::array::CubeView<const float> lhs, ksj::array::CubeView<const float> rhs,
                                       float& output);
[[nodiscard]] bool squared_l2_distance(ksj::array::CubeView<const double> lhs, ksj::array::CubeView<const double> rhs,
                                       double& output);
[[nodiscard]] bool squared_l2_distance(ksj::array::CubeView<const ksj::base::cf32> lhs,
                                       ksj::array::CubeView<const ksj::base::cf32> rhs, float& output);
[[nodiscard]] bool squared_l2_distance(ksj::array::CubeView<const ksj::base::cf64> lhs,
                                       ksj::array::CubeView<const ksj::base::cf64> rhs, double& output);

template <typename T, typename Output>
[[nodiscard]] bool squared_l2_distance(ksj::array::CubeView<const T> lhs, ksj::array::CubeView<const T> rhs,
                                       Output& output) {
  if constexpr (ipp_magnitude_scalar_v<T>) {
    return squared_l2_distance(lhs, rhs, output);
  } else {
    (void)lhs;
    (void)rhs;
    (void)output;
    return false;
  }
}

} // namespace ksj::stats::detail::intel
