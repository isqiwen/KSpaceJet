#include "kspacejet/stats/detail/intel/intel_stats_reductions.hpp"
#include "intel_stats_common.hpp"

#include <ipp.h>

namespace ksj::stats::detail::intel {

bool sum(ksj::array::VectorView<const float> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsSum_32f(input.data(), static_cast<int>(input.size()), &output, ippAlgHintAccurate));
}

bool sum(ksj::array::VectorView<const double> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsSum_64f(input.data(), static_cast<int>(input.size()), &output));
}

bool sum_abs(ksj::array::VectorView<const float> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsNorm_L1_32f(input.data(), static_cast<int>(input.size()), &output));
}

bool sum_abs(ksj::array::VectorView<const double> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsNorm_L1_64f(input.data(), static_cast<int>(input.size()), &output));
}

bool sum_abs(ksj::array::VectorView<const ksj::base::cf32> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  double norm = 0.0;
  const auto* source = reinterpret_cast<const Ipp32fc*>(input.data());
  if (!impl::check_status(ippsNorm_L1_32fc64f(source, static_cast<int>(input.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(norm);
  return true;
}

bool sum_abs(ksj::array::VectorView<const ksj::base::cf64> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  const auto* source = reinterpret_cast<const Ipp64fc*>(input.data());
  return impl::check_status(ippsNorm_L1_64fc64f(source, static_cast<int>(input.size()), &output));
}

bool max_index(ksj::array::VectorView<const float> input, std::size_t& output) {
  if (input.empty() || !impl::is_ipp_compatible(input)) {
    return false;
  }
  float value = 0.0F;
  int index = 0;
  if (!impl::check_status(ippsMaxIndx_32f(input.data(), static_cast<int>(input.size()), &value, &index))) {
    return false;
  }
  output = static_cast<std::size_t>(index);
  return true;
}

bool max_index(ksj::array::VectorView<const double> input, std::size_t& output) {
  if (input.empty() || !impl::is_ipp_compatible(input)) {
    return false;
  }
  double value = 0.0;
  int index = 0;
  if (!impl::check_status(ippsMaxIndx_64f(input.data(), static_cast<int>(input.size()), &value, &index))) {
    return false;
  }
  output = static_cast<std::size_t>(index);
  return true;
}

bool min_index(ksj::array::VectorView<const float> input, std::size_t& output) {
  if (input.empty() || !impl::is_ipp_compatible(input)) {
    return false;
  }
  float value = 0.0F;
  int index = 0;
  if (!impl::check_status(ippsMinIndx_32f(input.data(), static_cast<int>(input.size()), &value, &index))) {
    return false;
  }
  output = static_cast<std::size_t>(index);
  return true;
}

bool min_index(ksj::array::VectorView<const double> input, std::size_t& output) {
  if (input.empty() || !impl::is_ipp_compatible(input)) {
    return false;
  }
  double value = 0.0;
  int index = 0;
  if (!impl::check_status(ippsMinIndx_64f(input.data(), static_cast<int>(input.size()), &value, &index))) {
    return false;
  }
  output = static_cast<std::size_t>(index);
  return true;
}

} // namespace ksj::stats::detail::intel
