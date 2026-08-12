#pragma once

/// Threshold-selection and threshold-application operations for numeric measurements.

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_thresholds.hpp"

#include <cstddef>
#include <type_traits>

namespace ksj::stats {

template <typename T> using otsu_threshold_result_t = ksj::array::reduction_result_t<std::remove_const_t<T>>;

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] otsu_threshold_result_t<T> otsu_threshold(ksj::array::VectorView<T> input,
                                                        const std::size_t interval_count = 256U) {
  return detail::eigen::otsu_threshold(ksj::array::as_const_view(input), interval_count);
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<T>)
[[nodiscard]] otsu_threshold_result_t<T> otsu_threshold(const ksj::array::PooledVector<T>& input,
                                                        const std::size_t interval_count = 256U) {
  return otsu_threshold(input.view(), interval_count);
}

} // namespace ksj::stats
