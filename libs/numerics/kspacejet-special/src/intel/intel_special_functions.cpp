#include "kspacejet/special/detail/intel/intel_special_functions.hpp"

#include <mkl_vml.h>

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::special::detail::intel {
namespace {

[[nodiscard]] bool fits_mkl_length(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<MKL_INT>::max());
}

template <typename T>
[[nodiscard]] bool is_vml_compatible(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) noexcept {
  return input.size() == output.size() && input.is_contiguous() && output.is_contiguous() &&
         fits_mkl_length(input.size());
}

template <typename T, typename Function>
[[nodiscard]] bool vml_unary(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                             Function function) {
  if (!is_vml_compatible(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(input.size()), input.data(), output.data());
  return true;
}

template <typename T, typename Function>
[[nodiscard]] bool vml_unary_mode(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                                  Function function) {
  if (!is_vml_compatible(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  constexpr MKL_INT64 kMode = VML_HA | VML_ERRMODE_IGNORE | VML_FTZDAZ_CURRENT;
  function(static_cast<MKL_INT>(input.size()), input.data(), output.data(), kMode);
  return true;
}

template <typename T, typename MklComplexT, typename Function>
[[nodiscard]] bool vml_complex_unary(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                                     Function function) {
  static_assert(sizeof(T) == sizeof(MklComplexT));
  static_assert(alignof(T) == alignof(MklComplexT));
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(std::is_trivially_copyable_v<MklComplexT>);
  if (!is_vml_compatible(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(input.size()), reinterpret_cast<const MklComplexT*>(input.data()),
           reinterpret_cast<MklComplexT*>(output.data()));
  return true;
}

template <typename T, typename OutputT, typename MklComplexT, typename Function>
[[nodiscard]] bool vml_complex_real_unary(ksj::array::VectorView<const T> input, ksj::array::VectorView<OutputT> output,
                                          Function function) {
  static_assert(sizeof(T) == sizeof(MklComplexT));
  static_assert(alignof(T) == alignof(MklComplexT));
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(std::is_trivially_copyable_v<MklComplexT>);
  if (input.size() != output.size() || !input.is_contiguous() || !output.is_contiguous() ||
      !fits_mkl_length(input.size())) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(input.size()), reinterpret_cast<const MklComplexT*>(input.data()), output.data());
  return true;
}

template <typename T, typename Function>
[[nodiscard]] bool vml_binary(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs,
                              ksj::array::VectorView<T> output, Function function) {
  if (lhs.size() != rhs.size() || lhs.size() != output.size() || !lhs.is_contiguous() || !rhs.is_contiguous() ||
      !output.is_contiguous() || !fits_mkl_length(lhs.size())) {
    return false;
  }
  if (lhs.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(lhs.size()), lhs.data(), rhs.data(), output.data());
  return true;
}

template <typename T, typename Function>
[[nodiscard]] bool vml_binary_scalar(ksj::array::VectorView<const T> input, const T value,
                                     ksj::array::VectorView<T> output, Function function) {
  if (!is_vml_compatible(input, output)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  function(static_cast<MKL_INT>(input.size()), input.data(), value, output.data());
  return true;
}

template <typename T, typename LogGammaFunction, typename ExpFunction>
[[nodiscard]] bool gamma_impl(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output,
                              LogGammaFunction log_gamma_function, ExpFunction exp_function) {
  if (!vml_unary_mode(input, output, log_gamma_function)) {
    return false;
  }
  if (input.empty()) {
    return true;
  }
  constexpr MKL_INT64 kMode = VML_HA | VML_ERRMODE_IGNORE | VML_FTZDAZ_CURRENT;
  exp_function(static_cast<MKL_INT>(output.size()), output.data(), output.data(), kMode);
  return true;
}

} // namespace

bool gamma(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return gamma_impl(input, output, vmsLGamma, vmsExp);
}

bool gamma(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return gamma_impl(input, output, vmdLGamma, vmdExp);
}

bool log_gamma(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary_mode(input, output, vmsLGamma);
}

bool log_gamma(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary_mode(input, output, vmdLGamma);
}

bool bessel_i0(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsI0);
}

bool bessel_i0(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdI0);
}

bool bessel_j0(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsJ0);
}

bool bessel_j0(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdJ0);
}

bool bessel_j1(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsJ1);
}

bool bessel_j1(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdJ1);
}

bool sin(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsSin);
}

bool sin(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdSin);
}

bool sin(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<ksj::base::cf32> output) {
  return vml_complex_unary<ksj::base::cf32, MKL_Complex8>(input, output, vcSin);
}

bool sin(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<ksj::base::cf64> output) {
  return vml_complex_unary<ksj::base::cf64, MKL_Complex16>(input, output, vzSin);
}

bool cos(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsCos);
}

bool cos(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdCos);
}

bool cos(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<ksj::base::cf32> output) {
  return vml_complex_unary<ksj::base::cf32, MKL_Complex8>(input, output, vcCos);
}

bool cos(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<ksj::base::cf64> output) {
  return vml_complex_unary<ksj::base::cf64, MKL_Complex16>(input, output, vzCos);
}

bool tan(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsTan);
}

bool tan(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdTan);
}

bool asin(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsAsin);
}

bool asin(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdAsin);
}

bool acos(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsAcos);
}

bool acos(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdAcos);
}

bool atan(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsAtan);
}

bool atan(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdAtan);
}

bool atan2(ksj::array::VectorView<const float> y, ksj::array::VectorView<const float> x,
           ksj::array::VectorView<float> output) {
  return vml_binary(y, x, output, vsAtan2);
}

bool atan2(ksj::array::VectorView<const double> y, ksj::array::VectorView<const double> x,
           ksj::array::VectorView<double> output) {
  return vml_binary(y, x, output, vdAtan2);
}

bool ln(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsLn);
}

bool ln(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdLn);
}

bool ln(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<ksj::base::cf32> output) {
  return vml_complex_unary<ksj::base::cf32, MKL_Complex8>(input, output, vcLn);
}

bool ln(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<ksj::base::cf64> output) {
  return vml_complex_unary<ksj::base::cf64, MKL_Complex16>(input, output, vzLn);
}

bool log10(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsLog10);
}

bool log10(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdLog10);
}

bool log2(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsLog2);
}

bool log2(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdLog2);
}

bool sqrt(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsSqrt);
}

bool sqrt(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdSqrt);
}

bool sqrt(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<ksj::base::cf32> output) {
  return vml_complex_unary<ksj::base::cf32, MKL_Complex8>(input, output, vcSqrt);
}

bool sqrt(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<ksj::base::cf64> output) {
  return vml_complex_unary<ksj::base::cf64, MKL_Complex16>(input, output, vzSqrt);
}

bool cbrt(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsCbrt);
}

bool cbrt(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdCbrt);
}

bool pow(ksj::array::VectorView<const float> base, ksj::array::VectorView<const float> exponent,
         ksj::array::VectorView<float> output) {
  return vml_binary(base, exponent, output, vsPow);
}

bool pow(ksj::array::VectorView<const double> base, ksj::array::VectorView<const double> exponent,
         ksj::array::VectorView<double> output) {
  return vml_binary(base, exponent, output, vdPow);
}

bool pow(ksj::array::VectorView<const float> base, const float exponent, ksj::array::VectorView<float> output) {
  return vml_binary_scalar(base, exponent, output, vsPowx);
}

bool pow(ksj::array::VectorView<const double> base, const double exponent, ksj::array::VectorView<double> output) {
  return vml_binary_scalar(base, exponent, output, vdPowx);
}

bool erf(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsErf);
}

bool erf(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdErf);
}

bool cdf_norm(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsCdfNorm);
}

bool cdf_norm(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdCdfNorm);
}

bool exp(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsExp);
}

bool exp(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdExp);
}

bool exp2(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsExp2);
}

bool exp2(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdExp2);
}

bool expm1(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsExpm1);
}

bool expm1(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdExpm1);
}

bool log1p(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsLog1p);
}

bool log1p(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdLog1p);
}

bool erfc(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsErfc);
}

bool erfc(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdErfc);
}

bool sinpi(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsSinpi);
}

bool sinpi(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdSinpi);
}

bool cospi(ksj::array::VectorView<const float> input, ksj::array::VectorView<float> output) {
  return vml_unary(input, output, vsCospi);
}

bool cospi(ksj::array::VectorView<const double> input, ksj::array::VectorView<double> output) {
  return vml_unary(input, output, vdCospi);
}

bool exp(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<ksj::base::cf32> output) {
  return vml_complex_unary<ksj::base::cf32, MKL_Complex8>(input, output, vcExp);
}

bool exp(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<ksj::base::cf64> output) {
  return vml_complex_unary<ksj::base::cf64, MKL_Complex16>(input, output, vzExp);
}

bool conj(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<ksj::base::cf32> output) {
  return vml_complex_unary<ksj::base::cf32, MKL_Complex8>(input, output, vcConj);
}

bool conj(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<ksj::base::cf64> output) {
  return vml_complex_unary<ksj::base::cf64, MKL_Complex16>(input, output, vzConj);
}

bool abs(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<float> output) {
  return vml_complex_real_unary<ksj::base::cf32, float, MKL_Complex8>(input, output, vcAbs);
}

bool abs(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<double> output) {
  return vml_complex_real_unary<ksj::base::cf64, double, MKL_Complex16>(input, output, vzAbs);
}

bool arg(ksj::array::VectorView<const ksj::base::cf32> input, ksj::array::VectorView<float> output) {
  return vml_complex_real_unary<ksj::base::cf32, float, MKL_Complex8>(input, output, vcArg);
}

bool arg(ksj::array::VectorView<const ksj::base::cf64> input, ksj::array::VectorView<double> output) {
  return vml_complex_real_unary<ksj::base::cf64, double, MKL_Complex16>(input, output, vzArg);
}

} // namespace ksj::special::detail::intel
