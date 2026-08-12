#include "kspacejet/stats/detail/eigen/eigen_stats_reductions.hpp"
#include "eigen_stats_common.hpp"

namespace ksj::stats::detail::eigen {

float sum(const ksj::array::VectorView<const float> input) {
  return impl::sum_impl(input);
}

double sum(const ksj::array::VectorView<const double> input) {
  return impl::sum_impl(input);
}

ksj::base::cf32 sum(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::sum_impl(input);
}

ksj::base::cf64 sum(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::sum_impl(input);
}

float sum_abs(const ksj::array::VectorView<const float> input) {
  return impl::sum_abs_impl(input);
}

double sum_abs(const ksj::array::VectorView<const double> input) {
  return impl::sum_abs_impl(input);
}

float sum_abs(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::sum_abs_impl(input);
}

double sum_abs(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::sum_abs_impl(input);
}

float kahan_sum(const ksj::array::VectorView<const float> input) {
  return impl::kahan_sum_impl(input);
}

double kahan_sum(const ksj::array::VectorView<const double> input) {
  return impl::kahan_sum_impl(input);
}

float pair_sum(const ksj::array::VectorView<const float> input) {
  return impl::pair_sum_impl(input);
}

double pair_sum(const ksj::array::VectorView<const double> input) {
  return impl::pair_sum_impl(input);
}

ksj::base::cf32 pair_sum(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::pair_sum_impl(input);
}

ksj::base::cf64 pair_sum(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::pair_sum_impl(input);
}

std::size_t max_index(const ksj::array::VectorView<const float> input) {
  return impl::max_index_impl(input);
}

std::size_t max_index(const ksj::array::VectorView<const double> input) {
  return impl::max_index_impl(input);
}

std::size_t max_index(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::max_index_impl(input);
}

std::size_t max_index(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::max_index_impl(input);
}

std::size_t min_index(const ksj::array::VectorView<const float> input) {
  return impl::min_index_impl(input);
}

std::size_t min_index(const ksj::array::VectorView<const double> input) {
  return impl::min_index_impl(input);
}

std::size_t min_index(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::min_index_impl(input);
}

std::size_t min_index(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::min_index_impl(input);
}

} // namespace ksj::stats::detail::eigen
