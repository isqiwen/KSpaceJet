#pragma once

/// Error and similarity metrics for comparing equally shaped numeric arrays.

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_error_metrics.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_error_metrics.hpp"
#include "kspacejet/stats/detail/stats_policy.hpp"

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace ksj::stats {

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> rmse(ksj::array::VectorView<T> diff) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;
  if (diff.empty()) {
    throw std::invalid_argument("rmse input must not be empty");
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_rmse_diff<value_type>(diff.size()) &&
        detail::intel::rmse(ksj::array::as_const_view(diff), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::rmse(ksj::array::as_const_view(diff));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> rmse(ksj::array::VectorView<T> data,
                                                                          ksj::array::VectorView<T> reference) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;
  if (data.size() != reference.size()) {
    throw std::invalid_argument("rmse input dimension mismatch");
  }
  if (data.empty()) {
    throw std::invalid_argument("rmse input must not be empty");
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_rmse_pair<value_type>(data.size()) &&
        detail::intel::rmse(ksj::array::as_const_view(data), ksj::array::as_const_view(reference), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::rmse(ksj::array::as_const_view(data), ksj::array::as_const_view(reference));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] bool equal(ksj::array::VectorView<T> data, ksj::array::VectorView<T> reference,
                         const ksj::array::magnitude_result_t<std::remove_const_t<T>>& precision =
                           std::numeric_limits<ksj::array::magnitude_result_t<std::remove_const_t<T>>>::epsilon()) {
  using value_type = std::remove_const_t<T>;
  if (data.size() != reference.size()) {
    return false;
  }
  if (data.empty()) {
    return true;
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    bool intel_output = false;
    if (detail::prefer_intel_equal<value_type>(data.size()) &&
        detail::intel::equal(ksj::array::as_const_view(data), ksj::array::as_const_view(reference), precision,
                             intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::equal(ksj::array::as_const_view(data), ksj::array::as_const_view(reference), precision);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> rmse(const ksj::array::PooledVector<T>& diff) {
  return rmse(diff.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> rmse(const ksj::array::PooledVector<T>& data,
                                                     const ksj::array::PooledVector<T>& reference) {
  return rmse(data.view(), reference.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] bool equal(const ksj::array::PooledVector<T>& data, const ksj::array::PooledVector<T>& reference,
                         const ksj::array::magnitude_result_t<T>& precision =
                           std::numeric_limits<ksj::array::magnitude_result_t<T>>::epsilon()) {
  return equal(data.view(), reference.view(), precision);
}

} // namespace ksj::stats
