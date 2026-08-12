#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"

namespace ksj::special::detail::eigen {

[[nodiscard]] float log_gamma(float value);
[[nodiscard]] double log_gamma(double value);

[[nodiscard]] float gamma(float value);
[[nodiscard]] double gamma(double value);

[[nodiscard]] float bessel_i0(float value);
[[nodiscard]] double bessel_i0(double value);

[[nodiscard]] float bessel_j0(float value);
[[nodiscard]] double bessel_j0(double value);

[[nodiscard]] float bessel_j1(float value);
[[nodiscard]] double bessel_j1(double value);

[[nodiscard]] float bessel_j(float order, float value);
[[nodiscard]] double bessel_j(double order, double value);
[[nodiscard]] ksj::base::cf32 bessel_j(float order, ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 bessel_j(double order, ksj::base::cf64 value);

[[nodiscard]] float sin(float value);
[[nodiscard]] double sin(double value);

[[nodiscard]] float cos(float value);
[[nodiscard]] double cos(double value);
[[nodiscard]] ksj::base::cf32 cos(ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 cos(ksj::base::cf64 value);

[[nodiscard]] float tan(float value);
[[nodiscard]] double tan(double value);

[[nodiscard]] float asin(float value);
[[nodiscard]] double asin(double value);

[[nodiscard]] float acos(float value);
[[nodiscard]] double acos(double value);

[[nodiscard]] float atan(float value);
[[nodiscard]] double atan(double value);

[[nodiscard]] float atan2(float y, float x);
[[nodiscard]] double atan2(double y, double x);

[[nodiscard]] float ln(float value);
[[nodiscard]] double ln(double value);
[[nodiscard]] ksj::base::cf32 ln(ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 ln(ksj::base::cf64 value);

[[nodiscard]] float log10(float value);
[[nodiscard]] double log10(double value);

[[nodiscard]] float log2(float value);
[[nodiscard]] double log2(double value);

[[nodiscard]] float sqrt(float value);
[[nodiscard]] double sqrt(double value);
[[nodiscard]] ksj::base::cf32 sqrt(ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 sqrt(ksj::base::cf64 value);

[[nodiscard]] float cbrt(float value);
[[nodiscard]] double cbrt(double value);

[[nodiscard]] float pow(float base, float exponent);
[[nodiscard]] double pow(double base, double exponent);

[[nodiscard]] float erf(float value);
[[nodiscard]] double erf(double value);

[[nodiscard]] float cdf_norm(float value);
[[nodiscard]] double cdf_norm(double value);

[[nodiscard]] float exp(float value);
[[nodiscard]] double exp(double value);
[[nodiscard]] ksj::base::cf32 exp(ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 exp(ksj::base::cf64 value);

[[nodiscard]] float exp2(float value);
[[nodiscard]] double exp2(double value);
[[nodiscard]] float expm1(float value);
[[nodiscard]] double expm1(double value);
[[nodiscard]] float log1p(float value);
[[nodiscard]] double log1p(double value);
[[nodiscard]] float erfc(float value);
[[nodiscard]] double erfc(double value);
[[nodiscard]] float sinpi(float value);
[[nodiscard]] double sinpi(double value);
[[nodiscard]] float cospi(float value);
[[nodiscard]] double cospi(double value);

[[nodiscard]] ksj::base::cf32 sin(ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 sin(ksj::base::cf64 value);
[[nodiscard]] ksj::base::cf32 conj(ksj::base::cf32 value);
[[nodiscard]] ksj::base::cf64 conj(ksj::base::cf64 value);
[[nodiscard]] float abs(ksj::base::cf32 value);
[[nodiscard]] double abs(ksj::base::cf64 value);
[[nodiscard]] float arg(ksj::base::cf32 value);
[[nodiscard]] double arg(ksj::base::cf64 value);

[[nodiscard]] ksj::array::PooledVector<float> gamma(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> gamma(ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<float> log_gamma(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> log_gamma(ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<float> bessel_i0(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> bessel_i0(ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<float> bessel_j0(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> bessel_j0(ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<float> bessel_j1(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> bessel_j1(ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<float> bessel_j(float order, ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> bessel_j(double order, ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<float> sin(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> sin(ksj::array::VectorView<const double> input);

[[nodiscard]] ksj::array::PooledVector<ksj::base::cf32> sin(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf64> sin(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> cos(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> cos(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf32> cos(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf64> cos(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> tan(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> tan(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> asin(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> asin(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> acos(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> acos(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> atan(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> atan(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> atan2(ksj::array::VectorView<const float> y,
                                                    ksj::array::VectorView<const float> x);
[[nodiscard]] ksj::array::PooledVector<double> atan2(ksj::array::VectorView<const double> y,
                                                     ksj::array::VectorView<const double> x);
[[nodiscard]] ksj::array::PooledVector<float> ln(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> ln(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf32> ln(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf64> ln(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> log10(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> log10(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> log2(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> log2(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> sqrt(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> sqrt(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf32> sqrt(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf64> sqrt(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> cbrt(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> cbrt(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> pow(ksj::array::VectorView<const float> base,
                                                  ksj::array::VectorView<const float> exponent);
[[nodiscard]] ksj::array::PooledVector<double> pow(ksj::array::VectorView<const double> base,
                                                   ksj::array::VectorView<const double> exponent);
[[nodiscard]] ksj::array::PooledVector<float> pow(ksj::array::VectorView<const float> base, float exponent);
[[nodiscard]] ksj::array::PooledVector<double> pow(ksj::array::VectorView<const double> base, double exponent);
[[nodiscard]] ksj::array::PooledVector<float> erf(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> erf(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> cdf_norm(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> cdf_norm(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> exp(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> exp(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf32> exp(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf64> exp(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> exp2(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> exp2(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> expm1(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> expm1(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> log1p(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> log1p(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> erfc(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> erfc(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> sinpi(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> sinpi(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<float> cospi(ksj::array::VectorView<const float> input);
[[nodiscard]] ksj::array::PooledVector<double> cospi(ksj::array::VectorView<const double> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf32> conj(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<ksj::base::cf64> conj(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> abs(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<double> abs(ksj::array::VectorView<const ksj::base::cf64> input);
[[nodiscard]] ksj::array::PooledVector<float> arg(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] ksj::array::PooledVector<double> arg(ksj::array::VectorView<const ksj::base::cf64> input);

} // namespace ksj::special::detail::eigen
