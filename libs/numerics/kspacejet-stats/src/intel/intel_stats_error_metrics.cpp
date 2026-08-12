#include "kspacejet/stats/detail/intel/intel_stats_error_metrics.hpp"
#include "intel_stats_common.hpp"

#include <ipp.h>

#include <cmath>

namespace ksj::stats::detail::intel {
namespace {

template <typename T>
[[nodiscard]] bool same_vector_layout(const ksj::array::VectorView<const T> lhs,
                                      const ksj::array::VectorView<const T> rhs) noexcept {
  return lhs.size() == rhs.size() && impl::is_ipp_compatible(lhs) && impl::is_ipp_compatible(rhs);
}

[[nodiscard]] float root_mean_square(const float norm, const std::size_t size) {
  return norm / std::sqrt(static_cast<float>(size));
}

[[nodiscard]] double root_mean_square(const double norm, const std::size_t size) {
  return norm / std::sqrt(static_cast<double>(size));
}

} // namespace

bool rmse(ksj::array::VectorView<const float> diff, float& output) {
  if (diff.empty() || !impl::is_ipp_compatible(diff)) {
    return false;
  }
  float norm = 0.0F;
  if (!impl::check_status(ippsNorm_L2_32f(diff.data(), static_cast<int>(diff.size()), &norm))) {
    return false;
  }
  output = root_mean_square(norm, diff.size());
  return true;
}

bool rmse(ksj::array::VectorView<const double> diff, double& output) {
  if (diff.empty() || !impl::is_ipp_compatible(diff)) {
    return false;
  }
  double norm = 0.0;
  if (!impl::check_status(ippsNorm_L2_64f(diff.data(), static_cast<int>(diff.size()), &norm))) {
    return false;
  }
  output = root_mean_square(norm, diff.size());
  return true;
}

bool rmse(ksj::array::VectorView<const ksj::base::cf32> diff, float& output) {
  if (diff.empty() || !impl::is_ipp_compatible(diff)) {
    return false;
  }
  double norm = 0.0;
  const auto* source = reinterpret_cast<const Ipp32fc*>(diff.data());
  if (!impl::check_status(ippsNorm_L2_32fc64f(source, static_cast<int>(diff.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(root_mean_square(norm, diff.size()));
  return true;
}

bool rmse(ksj::array::VectorView<const ksj::base::cf64> diff, double& output) {
  if (diff.empty() || !impl::is_ipp_compatible(diff)) {
    return false;
  }
  const auto* source = reinterpret_cast<const Ipp64fc*>(diff.data());
  double norm = 0.0;
  if (!impl::check_status(ippsNorm_L2_64fc64f(source, static_cast<int>(diff.size()), &norm))) {
    return false;
  }
  output = root_mean_square(norm, diff.size());
  return true;
}

bool rmse(ksj::array::VectorView<const float> data, ksj::array::VectorView<const float> reference, float& output) {
  if (data.empty() || !same_vector_layout(data, reference)) {
    return false;
  }
  float norm = 0.0F;
  if (!impl::check_status(ippsNormDiff_L2_32f(data.data(), reference.data(), static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = root_mean_square(norm, data.size());
  return true;
}

bool rmse(ksj::array::VectorView<const double> data, ksj::array::VectorView<const double> reference, double& output) {
  if (data.empty() || !same_vector_layout(data, reference)) {
    return false;
  }
  double norm = 0.0;
  if (!impl::check_status(ippsNormDiff_L2_64f(data.data(), reference.data(), static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = root_mean_square(norm, data.size());
  return true;
}

bool rmse(ksj::array::VectorView<const ksj::base::cf32> data, ksj::array::VectorView<const ksj::base::cf32> reference,
          float& output) {
  if (data.empty() || !same_vector_layout(data, reference)) {
    return false;
  }
  double norm = 0.0;
  const auto* lhs = reinterpret_cast<const Ipp32fc*>(data.data());
  const auto* rhs = reinterpret_cast<const Ipp32fc*>(reference.data());
  if (!impl::check_status(ippsNormDiff_L2_32fc64f(lhs, rhs, static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(root_mean_square(norm, data.size()));
  return true;
}

bool rmse(ksj::array::VectorView<const ksj::base::cf64> data, ksj::array::VectorView<const ksj::base::cf64> reference,
          double& output) {
  if (data.empty() || !same_vector_layout(data, reference)) {
    return false;
  }
  double norm = 0.0;
  const auto* lhs = reinterpret_cast<const Ipp64fc*>(data.data());
  const auto* rhs = reinterpret_cast<const Ipp64fc*>(reference.data());
  if (!impl::check_status(ippsNormDiff_L2_64fc64f(lhs, rhs, static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = root_mean_square(norm, data.size());
  return true;
}

bool equal(ksj::array::VectorView<const float> data, ksj::array::VectorView<const float> reference,
           const float precision, bool& output) {
  if (data.size() != reference.size()) {
    output = false;
    return true;
  }
  if (data.empty()) {
    output = true;
    return true;
  }
  if (!same_vector_layout(data, reference)) {
    return false;
  }
  float norm = 0.0F;
  if (!impl::check_status(ippsNormDiff_Inf_32f(data.data(), reference.data(), static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = norm <= precision;
  return true;
}

bool equal(ksj::array::VectorView<const double> data, ksj::array::VectorView<const double> reference,
           const double precision, bool& output) {
  if (data.size() != reference.size()) {
    output = false;
    return true;
  }
  if (data.empty()) {
    output = true;
    return true;
  }
  if (!same_vector_layout(data, reference)) {
    return false;
  }
  double norm = 0.0;
  if (!impl::check_status(ippsNormDiff_Inf_64f(data.data(), reference.data(), static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = norm <= precision;
  return true;
}

bool equal(ksj::array::VectorView<const ksj::base::cf32> data, ksj::array::VectorView<const ksj::base::cf32> reference,
           const float precision, bool& output) {
  if (data.size() != reference.size()) {
    output = false;
    return true;
  }
  if (data.empty()) {
    output = true;
    return true;
  }
  if (!same_vector_layout(data, reference)) {
    return false;
  }
  float norm = 0.0F;
  const auto* lhs = reinterpret_cast<const Ipp32fc*>(data.data());
  const auto* rhs = reinterpret_cast<const Ipp32fc*>(reference.data());
  if (!impl::check_status(ippsNormDiff_Inf_32fc32f(lhs, rhs, static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = norm <= precision;
  return true;
}

bool equal(ksj::array::VectorView<const ksj::base::cf64> data, ksj::array::VectorView<const ksj::base::cf64> reference,
           const double precision, bool& output) {
  if (data.size() != reference.size()) {
    output = false;
    return true;
  }
  if (data.empty()) {
    output = true;
    return true;
  }
  if (!same_vector_layout(data, reference)) {
    return false;
  }
  double norm = 0.0;
  const auto* lhs = reinterpret_cast<const Ipp64fc*>(data.data());
  const auto* rhs = reinterpret_cast<const Ipp64fc*>(reference.data());
  if (!impl::check_status(ippsNormDiff_Inf_64fc64f(lhs, rhs, static_cast<int>(data.size()), &norm))) {
    return false;
  }
  output = norm <= precision;
  return true;
}

} // namespace ksj::stats::detail::intel
