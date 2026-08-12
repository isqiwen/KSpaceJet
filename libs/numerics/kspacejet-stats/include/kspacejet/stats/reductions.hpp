#pragma once

/// Statistical reductions that return scalar summaries or dimension-wise dense outputs.

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_reductions.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_reductions.hpp"
#include "kspacejet/stats/detail/stats_policy.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ksj::stats {

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::remove_const_t<T> sum(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;

  if constexpr (detail::intel::ipp_real_scalar_v<value_type>) {
    value_type intel_output{};
    if (detail::prefer_intel_sum<value_type>(input.size()) &&
        detail::intel::sum(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::sum(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] T sum(const ksj::array::PooledVector<T>& input) {
  return sum(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> sum_abs(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_sum_abs<value_type>(input.size()) &&
        detail::intel::sum_abs(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::sum_abs(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::remove_const_t<T> kahan_sum(ksj::array::VectorView<T> input) {
  return detail::eigen::kahan_sum(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::remove_const_t<T> pair_sum(ksj::array::VectorView<T> input) {
  return detail::eigen::pair_sum(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::size_t max_index(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  if (input.empty()) {
    throw std::invalid_argument("max_index input must not be empty");
  }

  if constexpr (detail::intel::ipp_real_scalar_v<value_type>) {
    std::size_t intel_output = 0U;
    if (detail::prefer_intel_max_index<value_type>(input.size()) &&
        detail::intel::max_index(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::max_index(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::size_t min_index(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  if (input.empty()) {
    throw std::invalid_argument("min_index input must not be empty");
  }

  if constexpr (detail::intel::ipp_real_scalar_v<value_type>) {
    std::size_t intel_output = 0U;
    if (detail::prefer_intel_min_index<value_type>(input.size()) &&
        detail::intel::min_index(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::min_index(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::remove_const_t<T> max_value(ksj::array::VectorView<T> input) {
  return input(max_index(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] std::remove_const_t<T> min_value(ksj::array::VectorView<T> input) {
  return input(min_index(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> sum_abs(const ksj::array::PooledVector<T>& input) {
  return sum_abs(input.view());
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<T>)
[[nodiscard]] T kahan_sum(const ksj::array::PooledVector<T>& input) {
  return kahan_sum(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] T pair_sum(const ksj::array::PooledVector<T>& input) {
  return pair_sum(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] std::size_t max_index(const ksj::array::PooledVector<T>& input) {
  return max_index(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] std::size_t min_index(const ksj::array::PooledVector<T>& input) {
  return min_index(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] T max_value(const ksj::array::PooledVector<T>& input) {
  return max_value(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] T min_value(const ksj::array::PooledVector<T>& input) {
  return min_value(input.view());
}

} // namespace ksj::stats
