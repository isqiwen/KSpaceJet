#include "kspacejet/stats/detail/intel/intel_stats_moments.hpp"
#include "intel_stats_common.hpp"

#include <ipp.h>

namespace ksj::stats::detail::intel {

bool mean(ksj::array::VectorView<const float> input, float& output) {
  if (input.empty()) {
    return false;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsMean_32f(input.data(), static_cast<int>(input.size()), &output, ippAlgHintAccurate));
}

bool mean(ksj::array::VectorView<const double> input, double& output) {
  if (input.empty()) {
    return false;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsMean_64f(input.data(), static_cast<int>(input.size()), &output));
}

} // namespace ksj::stats::detail::intel
