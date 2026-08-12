#include "kspacejet/stats/detail/eigen/eigen_stats_error_metrics.hpp"
#include "eigen_stats_common.hpp"

namespace ksj::stats::detail::eigen {

float rmse(const ksj::array::VectorView<const float> diff) {
  return impl::rmse_impl(diff);
}

double rmse(const ksj::array::VectorView<const double> diff) {
  return impl::rmse_impl(diff);
}

float rmse(const ksj::array::VectorView<const ksj::base::cf32> diff) {
  return impl::rmse_impl(diff);
}

double rmse(const ksj::array::VectorView<const ksj::base::cf64> diff) {
  return impl::rmse_impl(diff);
}

float rmse(const ksj::array::VectorView<const float> data, const ksj::array::VectorView<const float> reference) {
  return impl::rmse_impl(data, reference);
}

double rmse(const ksj::array::VectorView<const double> data, const ksj::array::VectorView<const double> reference) {
  return impl::rmse_impl(data, reference);
}

float rmse(const ksj::array::VectorView<const ksj::base::cf32> data,
           const ksj::array::VectorView<const ksj::base::cf32> reference) {
  return impl::rmse_impl(data, reference);
}

double rmse(const ksj::array::VectorView<const ksj::base::cf64> data,
            const ksj::array::VectorView<const ksj::base::cf64> reference) {
  return impl::rmse_impl(data, reference);
}

bool equal(const ksj::array::VectorView<const float> data, const ksj::array::VectorView<const float> reference,
           const float precision) {
  return impl::equal_impl(data, reference, precision);
}

bool equal(const ksj::array::VectorView<const double> data, const ksj::array::VectorView<const double> reference,
           const double precision) {
  return impl::equal_impl(data, reference, precision);
}

bool equal(const ksj::array::VectorView<const ksj::base::cf32> data,
           const ksj::array::VectorView<const ksj::base::cf32> reference, const float precision) {
  return impl::equal_impl(data, reference, precision);
}

bool equal(const ksj::array::VectorView<const ksj::base::cf64> data,
           const ksj::array::VectorView<const ksj::base::cf64> reference, const double precision) {
  return impl::equal_impl(data, reference, precision);
}

} // namespace ksj::stats::detail::eigen
