#include "kspacejet/stats/detail/intel/intel_stats_norms.hpp"
#include "intel_stats_common.hpp"

#include <ipp.h>

namespace ksj::stats::detail::intel {

bool sum_of_squares(ksj::array::VectorView<const float> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsDotProd_32f(input.data(), input.data(), static_cast<int>(input.size()), &output));
}

bool sum_of_squares(ksj::array::VectorView<const double> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsDotProd_64f(input.data(), input.data(), static_cast<int>(input.size()), &output));
}

bool sum_of_squares(ksj::array::VectorView<const ksj::base::cf32> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  double norm = 0.0;
  const auto* source = reinterpret_cast<const Ipp32fc*>(input.data());
  if (!impl::check_status(ippsNorm_L2_32fc64f(source, static_cast<int>(input.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(norm * norm);
  return true;
}

bool sum_of_squares(ksj::array::VectorView<const ksj::base::cf64> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  double norm = 0.0;
  const auto* source = reinterpret_cast<const Ipp64fc*>(input.data());
  if (!impl::check_status(ippsNorm_L2_64fc64f(source, static_cast<int>(input.size()), &norm))) {
    return false;
  }
  output = norm * norm;
  return true;
}

bool root_sum_of_squares(ksj::array::VectorView<const float> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsNorm_L2_32f(input.data(), static_cast<int>(input.size()), &output));
}

bool root_sum_of_squares(ksj::array::VectorView<const double> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsNorm_L2_64f(input.data(), static_cast<int>(input.size()), &output));
}

bool root_sum_of_squares(ksj::array::VectorView<const ksj::base::cf32> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  double norm = 0.0;
  const auto* source = reinterpret_cast<const Ipp32fc*>(input.data());
  if (!impl::check_status(ippsNorm_L2_32fc64f(source, static_cast<int>(input.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(norm);
  return true;
}

bool root_sum_of_squares(ksj::array::VectorView<const ksj::base::cf64> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  const auto* source = reinterpret_cast<const Ipp64fc*>(input.data());
  return impl::check_status(ippsNorm_L2_64fc64f(source, static_cast<int>(input.size()), &output));
}

bool max_abs(ksj::array::VectorView<const float> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsNorm_Inf_32f(input.data(), static_cast<int>(input.size()), &output));
}

bool max_abs(ksj::array::VectorView<const double> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  return impl::check_status(ippsNorm_Inf_64f(input.data(), static_cast<int>(input.size()), &output));
}

bool max_abs(ksj::array::VectorView<const ksj::base::cf32> input, float& output) {
  if (input.empty()) {
    output = 0.0F;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  const auto* source = reinterpret_cast<const Ipp32fc*>(input.data());
  return impl::check_status(ippsNorm_Inf_32fc32f(source, static_cast<int>(input.size()), &output));
}

bool max_abs(ksj::array::VectorView<const ksj::base::cf64> input, double& output) {
  if (input.empty()) {
    output = 0.0;
    return true;
  }
  if (!impl::is_ipp_compatible(input)) {
    return false;
  }
  const auto* source = reinterpret_cast<const Ipp64fc*>(input.data());
  return impl::check_status(ippsNorm_Inf_64fc64f(source, static_cast<int>(input.size()), &output));
}

bool l1_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs, float& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  return impl::check_status(ippsNormDiff_L1_32f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &output));
}

bool l1_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs, double& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  return impl::check_status(ippsNormDiff_L1_64f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &output));
}

bool l1_distance(ksj::array::VectorView<const ksj::base::cf32> lhs, ksj::array::VectorView<const ksj::base::cf32> rhs,
                 float& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  double norm = 0.0;
  const auto* left = reinterpret_cast<const Ipp32fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp32fc*>(rhs.data());
  if (!impl::check_status(ippsNormDiff_L1_32fc64f(left, right, static_cast<int>(lhs.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(norm);
  return true;
}

bool l1_distance(ksj::array::VectorView<const ksj::base::cf64> lhs, ksj::array::VectorView<const ksj::base::cf64> rhs,
                 double& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  const auto* left = reinterpret_cast<const Ipp64fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp64fc*>(rhs.data());
  return impl::check_status(ippsNormDiff_L1_64fc64f(left, right, static_cast<int>(lhs.size()), &output));
}

bool l2_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs, float& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  return impl::check_status(ippsNormDiff_L2_32f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &output));
}

bool l2_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs, double& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  return impl::check_status(ippsNormDiff_L2_64f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &output));
}

bool l2_distance(ksj::array::VectorView<const ksj::base::cf32> lhs, ksj::array::VectorView<const ksj::base::cf32> rhs,
                 float& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  double norm = 0.0;
  const auto* left = reinterpret_cast<const Ipp32fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp32fc*>(rhs.data());
  if (!impl::check_status(ippsNormDiff_L2_32fc64f(left, right, static_cast<int>(lhs.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(norm);
  return true;
}

bool l2_distance(ksj::array::VectorView<const ksj::base::cf64> lhs, ksj::array::VectorView<const ksj::base::cf64> rhs,
                 double& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  const auto* left = reinterpret_cast<const Ipp64fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp64fc*>(rhs.data());
  return impl::check_status(ippsNormDiff_L2_64fc64f(left, right, static_cast<int>(lhs.size()), &output));
}

bool linf_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs, float& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  return impl::check_status(ippsNormDiff_Inf_32f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &output));
}

bool linf_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs, double& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  return impl::check_status(ippsNormDiff_Inf_64f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &output));
}

bool linf_distance(ksj::array::VectorView<const ksj::base::cf32> lhs, ksj::array::VectorView<const ksj::base::cf32> rhs,
                   float& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  const auto* left = reinterpret_cast<const Ipp32fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp32fc*>(rhs.data());
  return impl::check_status(ippsNormDiff_Inf_32fc32f(left, right, static_cast<int>(lhs.size()), &output));
}

bool linf_distance(ksj::array::VectorView<const ksj::base::cf64> lhs, ksj::array::VectorView<const ksj::base::cf64> rhs,
                   double& output) {
  if (lhs.size() != rhs.size() || !impl::is_ipp_compatible(lhs) || !impl::is_ipp_compatible(rhs)) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  const auto* left = reinterpret_cast<const Ipp64fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp64fc*>(rhs.data());
  return impl::check_status(ippsNormDiff_Inf_64fc64f(left, right, static_cast<int>(lhs.size()), &output));
}

bool squared_l2_distance(ksj::array::CubeView<const float> lhs, ksj::array::CubeView<const float> rhs, float& output) {
  if (lhs.shape().extents != rhs.shape().extents || !lhs.is_contiguous() || !rhs.is_contiguous() ||
      !impl::fits_ipp_length(lhs.size())) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  float norm = 0.0F;
  if (!impl::check_status(ippsNormDiff_L2_32f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &norm))) {
    return false;
  }
  output = norm * norm;
  return true;
}

bool squared_l2_distance(ksj::array::CubeView<const double> lhs, ksj::array::CubeView<const double> rhs,
                         double& output) {
  if (lhs.shape().extents != rhs.shape().extents || !lhs.is_contiguous() || !rhs.is_contiguous() ||
      !impl::fits_ipp_length(lhs.size())) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  double norm = 0.0;
  if (!impl::check_status(ippsNormDiff_L2_64f(lhs.data(), rhs.data(), static_cast<int>(lhs.size()), &norm))) {
    return false;
  }
  output = norm * norm;
  return true;
}

bool squared_l2_distance(ksj::array::CubeView<const ksj::base::cf32> lhs,
                         ksj::array::CubeView<const ksj::base::cf32> rhs, float& output) {
  if (lhs.shape().extents != rhs.shape().extents || !lhs.is_contiguous() || !rhs.is_contiguous() ||
      !impl::fits_ipp_length(lhs.size())) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0F;
    return true;
  }
  double norm = 0.0;
  const auto* left = reinterpret_cast<const Ipp32fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp32fc*>(rhs.data());
  if (!impl::check_status(ippsNormDiff_L2_32fc64f(left, right, static_cast<int>(lhs.size()), &norm))) {
    return false;
  }
  output = static_cast<float>(norm * norm);
  return true;
}

bool squared_l2_distance(ksj::array::CubeView<const ksj::base::cf64> lhs,
                         ksj::array::CubeView<const ksj::base::cf64> rhs, double& output) {
  if (lhs.shape().extents != rhs.shape().extents || !lhs.is_contiguous() || !rhs.is_contiguous() ||
      !impl::fits_ipp_length(lhs.size())) {
    return false;
  }
  if (lhs.empty()) {
    output = 0.0;
    return true;
  }
  double norm = 0.0;
  const auto* left = reinterpret_cast<const Ipp64fc*>(lhs.data());
  const auto* right = reinterpret_cast<const Ipp64fc*>(rhs.data());
  if (!impl::check_status(ippsNormDiff_L2_64fc64f(left, right, static_cast<int>(lhs.size()), &norm))) {
    return false;
  }
  output = norm * norm;
  return true;
}

} // namespace ksj::stats::detail::intel
