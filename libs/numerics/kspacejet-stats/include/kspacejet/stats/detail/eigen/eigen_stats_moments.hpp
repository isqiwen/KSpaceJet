#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_types.hpp"

namespace ksj::stats {

enum class VarianceNormalization;

} // namespace ksj::stats

namespace ksj::stats::detail::eigen {

[[nodiscard]] float mean(ksj::array::VectorView<const float> input);
[[nodiscard]] double mean(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::base::cf32 mean(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::base::cf64 mean(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] float variance(ksj::array::VectorView<const float> input, VarianceNormalization normalization);
[[nodiscard]] double variance(ksj::array::VectorView<const double> input, VarianceNormalization normalization);

[[nodiscard]] float covariance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs,
                               VarianceNormalization normalization);
[[nodiscard]] double covariance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs,
                                VarianceNormalization normalization);

void covariance(ksj::array::MatrixView<const float> samples, ksj::array::MatrixView<float> output,
                VarianceNormalization normalization);
void covariance(ksj::array::MatrixView<const double> samples, ksj::array::MatrixView<double> output,
                VarianceNormalization normalization);
void covariance(ksj::array::MatrixView<const ksj::base::cf32> samples, ksj::array::MatrixView<ksj::base::cf32> output,
                VarianceNormalization normalization);
void covariance(ksj::array::MatrixView<const ksj::base::cf64> samples, ksj::array::MatrixView<ksj::base::cf64> output,
                VarianceNormalization normalization);

} // namespace ksj::stats::detail::eigen
