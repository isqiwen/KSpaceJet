#pragma once

/// Mean, variance, covariance, and higher-moment statistical operations.

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_moments.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_moments.hpp"
#include "kspacejet/stats/detail/stats_policy.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ksj::stats {

enum class VarianceNormalization {
  population,
  sample,
};

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::reduction_result_t<std::remove_const_t<T>> mean(ksj::array::VectorView<T> input) {
  if (input.empty()) {
    throw std::invalid_argument("mean input must not be empty");
  }
  using result_type = ksj::array::reduction_result_t<std::remove_const_t<T>>;
  using value_type = std::remove_const_t<T>;

  if constexpr (detail::intel::ipp_real_scalar_v<value_type>) {
    value_type intel_output{};
    if (detail::prefer_intel_mean<value_type>(input.size()) &&
        detail::intel::mean(ksj::array::as_const_view(input), intel_output)) {
      return static_cast<result_type>(intel_output);
    }
  }

  return detail::eigen::mean(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::reduction_result_t<std::remove_const_t<T>>
variance(ksj::array::VectorView<T> input, const VarianceNormalization normalization = VarianceNormalization::sample) {
  if (input.empty()) {
    throw std::invalid_argument("variance input must not be empty");
  }
  if (normalization == VarianceNormalization::sample && input.size() < 2U) {
    throw std::invalid_argument("sample variance requires at least two values");
  }

  return detail::eigen::variance(ksj::array::as_const_view(input), normalization);
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::reduction_result_t<std::remove_const_t<T>>
covariance(ksj::array::VectorView<T> lhs, ksj::array::VectorView<T> rhs,
           const VarianceNormalization normalization = VarianceNormalization::sample) {
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("covariance dimension mismatch");
  }
  if (lhs.empty()) {
    throw std::invalid_argument("covariance input must not be empty");
  }
  if (normalization == VarianceNormalization::sample && lhs.size() < 2U) {
    throw std::invalid_argument("sample covariance requires at least two values");
  }

  return detail::eigen::covariance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs), normalization);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
void covariance(ksj::array::MatrixView<T> samples,
                ksj::array::MatrixView<ksj::array::reduction_result_t<std::remove_const_t<T>>> output,
                const VarianceNormalization normalization = VarianceNormalization::sample) {
  if (output.rows() != samples.cols() || output.cols() != samples.cols()) {
    throw std::invalid_argument("covariance output dimension mismatch");
  }
  if (samples.rows() == 0U || samples.cols() == 0U) {
    throw std::invalid_argument("covariance input must not be empty");
  }
  if (normalization == VarianceNormalization::sample && samples.rows() < 2U) {
    throw std::invalid_argument("sample covariance requires at least two samples");
  }

  detail::eigen::covariance(ksj::array::as_const_view(samples), output, normalization);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::PooledMatrix<ksj::array::reduction_result_t<std::remove_const_t<T>>>
covariance(ksj::array::MatrixView<T> samples,
           const VarianceNormalization normalization = VarianceNormalization::sample) {
  using result_type = ksj::array::reduction_result_t<std::remove_const_t<T>>;
  auto output = ksj::array::make_pooled_matrix<result_type>(samples.cols(), samples.cols());
  covariance(samples, output.view(), normalization);
  return output;
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::reduction_result_t<T> mean(const ksj::array::PooledVector<T>& input) {
  return mean(input.view());
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<T>)
[[nodiscard]] ksj::array::reduction_result_t<T>
variance(const ksj::array::PooledVector<T>& input,
         const VarianceNormalization normalization = VarianceNormalization::sample) {
  return variance(input.view(), normalization);
}

template <typename T>
  requires(detail::eigen::supported_real_scalar_v<T>)
[[nodiscard]] ksj::array::reduction_result_t<T>
covariance(const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs,
           const VarianceNormalization normalization = VarianceNormalization::sample) {
  return covariance(lhs.view(), rhs.view(), normalization);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
void covariance(const ksj::array::PooledMatrix<T>& samples,
                ksj::array::PooledMatrix<ksj::array::reduction_result_t<T>>& output,
                const VarianceNormalization normalization = VarianceNormalization::sample) {
  covariance(samples.view(), output.view(), normalization);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::PooledMatrix<ksj::array::reduction_result_t<T>>
covariance(const ksj::array::PooledMatrix<T>& samples,
           const VarianceNormalization normalization = VarianceNormalization::sample) {
  return covariance(samples.view(), normalization);
}

} // namespace ksj::stats
