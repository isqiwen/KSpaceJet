#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"

namespace ksj::special::detail::intel {

[[nodiscard]] bool gamma(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool gamma(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool log_gamma(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool log_gamma(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool bessel_i0(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool bessel_i0(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool bessel_j0(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool bessel_j0(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool bessel_j1(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool bessel_j1(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool sin(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool sin(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);
[[nodiscard]] bool sin(ksj::array::VectorView<const ksj::base::cf32> input,
                       ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool sin(ksj::array::VectorView<const ksj::base::cf64> input,
                       ksj::array::VectorView<ksj::base::cf64> output);

[[nodiscard]] bool cos(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool cos(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);
[[nodiscard]] bool cos(ksj::array::VectorView<const ksj::base::cf32> input,
                       ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool cos(ksj::array::VectorView<const ksj::base::cf64> input,
                       ksj::array::VectorView<ksj::base::cf64> output);

[[nodiscard]] bool tan(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool tan(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool asin(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool asin(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool acos(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool acos(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool atan(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool atan(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool atan2(ksj::array::VectorView<const float> y, ksj::array::VectorView<const float> x,
                         ksj::array::VectorView<float> output);
[[nodiscard]] bool atan2(ksj::array::VectorView<const double> y, ksj::array::VectorView<const double> x,
                         ksj::array::VectorView<double> output);

[[nodiscard]] bool ln(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool ln(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);
[[nodiscard]] bool ln(ksj::array::VectorView<const ksj::base::cf32> input,
                      ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool ln(ksj::array::VectorView<const ksj::base::cf64> input,
                      ksj::array::VectorView<ksj::base::cf64> output);

[[nodiscard]] bool log10(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool log10(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool log2(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool log2(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool sqrt(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool sqrt(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);
[[nodiscard]] bool sqrt(ksj::array::VectorView<const ksj::base::cf32> input,
                        ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool sqrt(ksj::array::VectorView<const ksj::base::cf64> input,
                        ksj::array::VectorView<ksj::base::cf64> output);

[[nodiscard]] bool cbrt(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool cbrt(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool pow(ksj::array::VectorView<const float> base, ksj::array::VectorView<const float> exponent,
                       ksj::array::VectorView<float> output);
[[nodiscard]] bool pow(ksj::array::VectorView<const double> base, ksj::array::VectorView<const double> exponent,
                       ksj::array::VectorView<double> output);
[[nodiscard]] bool pow(ksj::array::VectorView<const float> base, float exponent, ksj::array::VectorView<float> output);
[[nodiscard]] bool pow(ksj::array::VectorView<const double> base, double exponent,
                       ksj::array::VectorView<double> output);

[[nodiscard]] bool erf(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool erf(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool cdf_norm(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool cdf_norm(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool exp(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool exp(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool exp2(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool exp2(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool expm1(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool expm1(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool log1p(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool log1p(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool erfc(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool erfc(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool sinpi(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool sinpi(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);

[[nodiscard]] bool cospi(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool cospi(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output);
[[nodiscard]] bool exp(ksj::array::VectorView<const ksj::base::cf32> input,
                       ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool exp(ksj::array::VectorView<const ksj::base::cf64> input,
                       ksj::array::VectorView<ksj::base::cf64> output);

[[nodiscard]] bool conj(ksj::array::VectorView<const ksj::base::cf32> input,
                        ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool conj(ksj::array::VectorView<const ksj::base::cf64> input,
                        ksj::array::VectorView<ksj::base::cf64> output);
[[nodiscard]] bool abs(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool abs(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<double> output);
[[nodiscard]] bool arg(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<float> output);
[[nodiscard]] bool arg(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<double> output);

template <typename InputT, typename OutputT>
[[nodiscard]] bool gamma(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool log_gamma(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool bessel_i0(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool bessel_j0(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool bessel_j1(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool sin(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool cos(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool tan(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool asin(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool acos(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool atan(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool atan2(ksj::array::VectorView<LhsT>, ksj::array::VectorView<RhsT>,
                         ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool ln(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool log10(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool log2(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool sqrt(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool cbrt(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool pow(ksj::array::VectorView<LhsT>, ksj::array::VectorView<RhsT>,
                       ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename ExponentT, typename OutputT>
[[nodiscard]] bool pow(ksj::array::VectorView<InputT>, ExponentT, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool erf(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool cdf_norm(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool exp(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool exp2(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool expm1(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool log1p(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool erfc(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool sinpi(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool cospi(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool conj(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool abs(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool arg(ksj::array::VectorView<InputT>, ksj::array::VectorView<OutputT>) noexcept {
  return false;
}

} // namespace ksj::special::detail::intel
