#include "kspacejet/stats/detail/eigen/eigen_stats_fitting.hpp"
#include "eigen_stats_common.hpp"

namespace ksj::stats::detail::eigen {

std::optional<LinearFitParameters> linear_fit(const ksj::array::VectorView<const float> x,
                                              const ksj::array::VectorView<const float> y) {
  return impl::linear_fit_impl(x, y);
}

std::optional<LinearFitParameters> linear_fit(const ksj::array::VectorView<const float> x,
                                              const ksj::array::VectorView<const double> y) {
  return impl::linear_fit_impl(x, y);
}

std::optional<LinearFitParameters> linear_fit(const ksj::array::VectorView<const double> x,
                                              const ksj::array::VectorView<const float> y) {
  return impl::linear_fit_impl(x, y);
}

std::optional<LinearFitParameters> linear_fit(const ksj::array::VectorView<const double> x,
                                              const ksj::array::VectorView<const double> y) {
  return impl::linear_fit_impl(x, y);
}

} // namespace ksj::stats::detail::eigen
