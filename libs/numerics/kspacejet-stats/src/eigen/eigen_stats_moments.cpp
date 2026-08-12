#include "kspacejet/stats/detail/eigen/eigen_stats_moments.hpp"
#include "eigen_stats_common.hpp"

namespace ksj::stats::detail::eigen {

float mean(const ksj::array::VectorView<const float> input) {
  return impl::mean_impl(input);
}

double mean(const ksj::array::VectorView<const double> input) {
  return impl::mean_impl(input);
}

ksj::base::cf32 mean(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::mean_impl(input);
}

ksj::base::cf64 mean(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::mean_impl(input);
}

float variance(const ksj::array::VectorView<const float> input, const VarianceNormalization normalization) {
  return impl::variance_impl(input, normalization);
}

double variance(const ksj::array::VectorView<const double> input, const VarianceNormalization normalization) {
  return impl::variance_impl(input, normalization);
}

float covariance(const ksj::array::VectorView<const float> lhs, const ksj::array::VectorView<const float> rhs,
                 const VarianceNormalization normalization) {
  return impl::covariance_impl(lhs, rhs, normalization);
}

double covariance(const ksj::array::VectorView<const double> lhs, const ksj::array::VectorView<const double> rhs,
                  const VarianceNormalization normalization) {
  return impl::covariance_impl(lhs, rhs, normalization);
}

void covariance(const ksj::array::MatrixView<const float> samples, const ksj::array::MatrixView<float> output,
                const VarianceNormalization normalization) {
  impl::covariance_impl(samples, output, normalization);
}

void covariance(const ksj::array::MatrixView<const double> samples, const ksj::array::MatrixView<double> output,
                const VarianceNormalization normalization) {
  impl::covariance_impl(samples, output, normalization);
}

void covariance(const ksj::array::MatrixView<const ksj::base::cf32> samples,
                const ksj::array::MatrixView<ksj::base::cf32> output, const VarianceNormalization normalization) {
  impl::covariance_impl(samples, output, normalization);
}

void covariance(const ksj::array::MatrixView<const ksj::base::cf64> samples,
                const ksj::array::MatrixView<ksj::base::cf64> output, const VarianceNormalization normalization) {
  impl::covariance_impl(samples, output, normalization);
}

} // namespace ksj::stats::detail::eigen
