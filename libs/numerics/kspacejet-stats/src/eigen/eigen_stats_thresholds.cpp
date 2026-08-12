#include "kspacejet/stats/detail/eigen/eigen_stats_thresholds.hpp"
#include "eigen_stats_common.hpp"

namespace ksj::stats::detail::eigen {

float otsu_threshold(const ksj::array::VectorView<const float> input, const std::size_t interval_count) {
  return impl::otsu_threshold_impl(input, interval_count);
}

double otsu_threshold(const ksj::array::VectorView<const double> input, const std::size_t interval_count) {
  return impl::otsu_threshold_impl(input, interval_count);
}

} // namespace ksj::stats::detail::eigen
