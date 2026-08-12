#include "kspacejet/special/detail/eigen/eigen_special_functions.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include <boost/math/special_functions.hpp>

#include <unsupported/Eigen/SpecialFunctions>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace ksj::special::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;

template <typename T> [[nodiscard]] T bessel_j_complex_real(const T order, const T real_value) {
  return static_cast<T>(boost::math::cyl_bessel_j(order, real_value));
}

template <typename T> [[nodiscard]] std::complex<T> bessel_j_complex_impl(const T order, const std::complex<T> value) {
  const auto real = value.real();
  const auto imag = value.imag();
  const auto scale = std::max({T{1}, std::abs(real), std::abs(imag)});
  const auto tolerance = T{64} * std::numeric_limits<T>::epsilon() * scale;

  if (std::abs(imag) <= tolerance) {
    return {bessel_j_complex_real(order, real), T{0}};
  }

  if (std::abs(real) > tolerance) {
    throw std::domain_error("ksj::special::bessel_j currently supports real or pure-imaginary complex arguments");
  }

  const auto magnitude = std::abs(imag);
  const auto modified = static_cast<T>(boost::math::cyl_bessel_i(order, magnitude));
  const auto phase_sign = imag < T{0} ? T{-1} : T{1};
  const auto phase = phase_sign * order * std::numbers::pi_v<T> / T{2};
  return {modified * std::cos(phase), modified * std::sin(phase)};
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> vector_gamma(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = as_eigen(input).array().lgamma().exp();
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> vector_log_gamma(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = as_eigen(input).array().lgamma();
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> vector_bessel_i0(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = Eigen::bessel_i0(as_eigen(input).array());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> vector_bessel_j0(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = Eigen::bessel_j0(as_eigen(input).array());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> vector_bessel_j1(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = Eigen::bessel_j1(as_eigen(input).array());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> vector_bessel_j(const T order, ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    output(index) = bessel_j(order, input(index));
  }
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> vector_sin(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = as_eigen(input).array().sin();
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledVector<T> vector_exp(ksj::array::VectorView<const T> input) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  as_eigen(output).array() = as_eigen(input).array().exp();
  return output;
}

template <typename InputT, typename OutputT, typename Function>
[[nodiscard]] ksj::array::PooledVector<OutputT> vector_unary_expr(ksj::array::VectorView<const InputT> input,
                                                                  Function function) {
  auto output = ksj::array::make_pooled_vector<OutputT>(input.size());
  as_eigen(output).array() = as_eigen(input).array().unaryExpr(function);
  return output;
}

template <typename T, typename Function>
[[nodiscard]] ksj::array::PooledVector<T> vector_unary_expr(ksj::array::VectorView<const T> input, Function function) {
  return vector_unary_expr<T, T>(input, function);
}

template <typename T, typename Function>
[[nodiscard]] ksj::array::PooledVector<T> vector_binary_expr(ksj::array::VectorView<const T> lhs,
                                                             ksj::array::VectorView<const T> rhs, Function function) {
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("special binary vector inputs must have the same size");
  }
  auto output = ksj::array::make_pooled_vector<T>(lhs.size());
  as_eigen(output).array() = as_eigen(lhs).array().binaryExpr(as_eigen(rhs).array(), function);
  return output;
}

} // namespace

float log_gamma(const float value) {
  using Eigen::numext::lgamma;
  return lgamma(value);
}

double log_gamma(const double value) {
  using Eigen::numext::lgamma;
  return lgamma(value);
}

float gamma(const float value) {
  return std::exp(log_gamma(value));
}

double gamma(const double value) {
  return std::exp(log_gamma(value));
}

float bessel_i0(const float value) {
  using Eigen::numext::bessel_i0;
  return bessel_i0(value);
}

double bessel_i0(const double value) {
  using Eigen::numext::bessel_i0;
  return bessel_i0(value);
}

float bessel_j0(const float value) {
  using Eigen::numext::bessel_j0;
  return bessel_j0(value);
}

double bessel_j0(const double value) {
  using Eigen::numext::bessel_j0;
  return bessel_j0(value);
}

float bessel_j1(const float value) {
  using Eigen::numext::bessel_j1;
  return bessel_j1(value);
}

double bessel_j1(const double value) {
  using Eigen::numext::bessel_j1;
  return bessel_j1(value);
}

float bessel_j(const float order, const float value) {
  return boost::math::cyl_bessel_j(order, value);
}

double bessel_j(const double order, const double value) {
  return boost::math::cyl_bessel_j(order, value);
}

ksj::base::cf32 bessel_j(const float order, const ksj::base::cf32 value) {
  return bessel_j_complex_impl(order, value);
}

ksj::base::cf64 bessel_j(const double order, const ksj::base::cf64 value) {
  return bessel_j_complex_impl(order, value);
}

float sin(const float value) {
  return std::sin(value);
}

double sin(const double value) {
  return std::sin(value);
}

ksj::base::cf32 sin(const ksj::base::cf32 value) {
  return std::sin(value);
}

ksj::base::cf64 sin(const ksj::base::cf64 value) {
  return std::sin(value);
}

float cos(const float value) {
  return std::cos(value);
}

double cos(const double value) {
  return std::cos(value);
}

ksj::base::cf32 cos(const ksj::base::cf32 value) {
  return std::cos(value);
}

ksj::base::cf64 cos(const ksj::base::cf64 value) {
  return std::cos(value);
}

float tan(const float value) {
  return std::tan(value);
}

double tan(const double value) {
  return std::tan(value);
}

float asin(const float value) {
  return std::asin(value);
}

double asin(const double value) {
  return std::asin(value);
}

float acos(const float value) {
  return std::acos(value);
}

double acos(const double value) {
  return std::acos(value);
}

float atan(const float value) {
  return std::atan(value);
}

double atan(const double value) {
  return std::atan(value);
}

float atan2(const float y, const float x) {
  return std::atan2(y, x);
}

double atan2(const double y, const double x) {
  return std::atan2(y, x);
}

float ln(const float value) {
  return std::log(value);
}

double ln(const double value) {
  return std::log(value);
}

ksj::base::cf32 ln(const ksj::base::cf32 value) {
  return std::log(value);
}

ksj::base::cf64 ln(const ksj::base::cf64 value) {
  return std::log(value);
}

float log10(const float value) {
  return std::log10(value);
}

double log10(const double value) {
  return std::log10(value);
}

float log2(const float value) {
  return std::log2(value);
}

double log2(const double value) {
  return std::log2(value);
}

float sqrt(const float value) {
  return std::sqrt(value);
}

double sqrt(const double value) {
  return std::sqrt(value);
}

ksj::base::cf32 sqrt(const ksj::base::cf32 value) {
  return std::sqrt(value);
}

ksj::base::cf64 sqrt(const ksj::base::cf64 value) {
  return std::sqrt(value);
}

float cbrt(const float value) {
  return std::cbrt(value);
}

double cbrt(const double value) {
  return std::cbrt(value);
}

float pow(const float base, const float exponent) {
  return std::pow(base, exponent);
}

double pow(const double base, const double exponent) {
  return std::pow(base, exponent);
}

float erf(const float value) {
  return std::erf(value);
}

double erf(const double value) {
  return std::erf(value);
}

float cdf_norm(const float value) {
  return 0.5F * (1.0F + std::erf(value / std::sqrt(2.0F)));
}

double cdf_norm(const double value) {
  return 0.5 * (1.0 + std::erf(value / std::sqrt(2.0)));
}

float exp(const float value) {
  return std::exp(value);
}

double exp(const double value) {
  return std::exp(value);
}

ksj::base::cf32 exp(const ksj::base::cf32 value) {
  return std::exp(value);
}

ksj::base::cf64 exp(const ksj::base::cf64 value) {
  return std::exp(value);
}

float exp2(const float value) {
  return std::exp2(value);
}

double exp2(const double value) {
  return std::exp2(value);
}

float expm1(const float value) {
  return std::expm1(value);
}

double expm1(const double value) {
  return std::expm1(value);
}

float log1p(const float value) {
  return std::log1p(value);
}

double log1p(const double value) {
  return std::log1p(value);
}

float erfc(const float value) {
  return std::erfc(value);
}

double erfc(const double value) {
  return std::erfc(value);
}

float sinpi(const float value) {
  return std::sin(std::numbers::pi_v<float> * value);
}

double sinpi(const double value) {
  return std::sin(std::numbers::pi_v<double> * value);
}

float cospi(const float value) {
  return std::cos(std::numbers::pi_v<float> * value);
}

double cospi(const double value) {
  return std::cos(std::numbers::pi_v<double> * value);
}

ksj::base::cf32 conj(const ksj::base::cf32 value) {
  return std::conj(value);
}

ksj::base::cf64 conj(const ksj::base::cf64 value) {
  return std::conj(value);
}

float abs(const ksj::base::cf32 value) {
  return std::abs(value);
}

double abs(const ksj::base::cf64 value) {
  return std::abs(value);
}

float arg(const ksj::base::cf32 value) {
  return std::arg(value);
}

double arg(const ksj::base::cf64 value) {
  return std::arg(value);
}

ksj::array::PooledVector<float> gamma(ksj::array::VectorView<const float> input) {
  return vector_gamma(input);
}

ksj::array::PooledVector<double> gamma(ksj::array::VectorView<const double> input) {
  return vector_gamma(input);
}

ksj::array::PooledVector<float> log_gamma(ksj::array::VectorView<const float> input) {
  return vector_log_gamma(input);
}

ksj::array::PooledVector<double> log_gamma(ksj::array::VectorView<const double> input) {
  return vector_log_gamma(input);
}

ksj::array::PooledVector<float> bessel_i0(ksj::array::VectorView<const float> input) {
  return vector_bessel_i0(input);
}

ksj::array::PooledVector<double> bessel_i0(ksj::array::VectorView<const double> input) {
  return vector_bessel_i0(input);
}

ksj::array::PooledVector<float> bessel_j0(ksj::array::VectorView<const float> input) {
  return vector_bessel_j0(input);
}

ksj::array::PooledVector<double> bessel_j0(ksj::array::VectorView<const double> input) {
  return vector_bessel_j0(input);
}

ksj::array::PooledVector<float> bessel_j1(ksj::array::VectorView<const float> input) {
  return vector_bessel_j1(input);
}

ksj::array::PooledVector<double> bessel_j1(ksj::array::VectorView<const double> input) {
  return vector_bessel_j1(input);
}

ksj::array::PooledVector<float> bessel_j(const float order, ksj::array::VectorView<const float> input) {
  return vector_bessel_j(order, input);
}

ksj::array::PooledVector<double> bessel_j(const double order, ksj::array::VectorView<const double> input) {
  return vector_bessel_j(order, input);
}

ksj::array::PooledVector<float> sin(ksj::array::VectorView<const float> input) {
  return vector_sin(input);
}

ksj::array::PooledVector<double> sin(ksj::array::VectorView<const double> input) {
  return vector_sin(input);
}

ksj::array::PooledVector<ksj::base::cf32> sin(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr(input, [](const ksj::base::cf32 value) {
    return std::sin(value);
  });
}

ksj::array::PooledVector<ksj::base::cf64> sin(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr(input, [](const ksj::base::cf64 value) {
    return std::sin(value);
  });
}

ksj::array::PooledVector<float> cos(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::cos(value);
  });
}

ksj::array::PooledVector<double> cos(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::cos(value);
  });
}

ksj::array::PooledVector<ksj::base::cf32> cos(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr(input, [](const ksj::base::cf32 value) {
    return std::cos(value);
  });
}

ksj::array::PooledVector<ksj::base::cf64> cos(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr(input, [](const ksj::base::cf64 value) {
    return std::cos(value);
  });
}

ksj::array::PooledVector<float> tan(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::tan(value);
  });
}

ksj::array::PooledVector<double> tan(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::tan(value);
  });
}

ksj::array::PooledVector<float> asin(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::asin(value);
  });
}

ksj::array::PooledVector<double> asin(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::asin(value);
  });
}

ksj::array::PooledVector<float> acos(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::acos(value);
  });
}

ksj::array::PooledVector<double> acos(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::acos(value);
  });
}

ksj::array::PooledVector<float> atan(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::atan(value);
  });
}

ksj::array::PooledVector<double> atan(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::atan(value);
  });
}

ksj::array::PooledVector<float> atan2(ksj::array::VectorView<const float> y, ksj::array::VectorView<const float> x) {
  return vector_binary_expr(y, x, [](const float lhs, const float rhs) {
    return std::atan2(lhs, rhs);
  });
}

ksj::array::PooledVector<double> atan2(ksj::array::VectorView<const double> y, ksj::array::VectorView<const double> x) {
  return vector_binary_expr(y, x, [](const double lhs, const double rhs) {
    return std::atan2(lhs, rhs);
  });
}

ksj::array::PooledVector<float> ln(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::log(value);
  });
}

ksj::array::PooledVector<double> ln(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::log(value);
  });
}

ksj::array::PooledVector<ksj::base::cf32> ln(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr(input, [](const ksj::base::cf32 value) {
    return std::log(value);
  });
}

ksj::array::PooledVector<ksj::base::cf64> ln(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr(input, [](const ksj::base::cf64 value) {
    return std::log(value);
  });
}

ksj::array::PooledVector<float> log10(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::log10(value);
  });
}

ksj::array::PooledVector<double> log10(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::log10(value);
  });
}

ksj::array::PooledVector<float> log2(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::log2(value);
  });
}

ksj::array::PooledVector<double> log2(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::log2(value);
  });
}

ksj::array::PooledVector<float> sqrt(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::sqrt(value);
  });
}

ksj::array::PooledVector<double> sqrt(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::sqrt(value);
  });
}

ksj::array::PooledVector<ksj::base::cf32> sqrt(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr(input, [](const ksj::base::cf32 value) {
    return std::sqrt(value);
  });
}

ksj::array::PooledVector<ksj::base::cf64> sqrt(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr(input, [](const ksj::base::cf64 value) {
    return std::sqrt(value);
  });
}

ksj::array::PooledVector<float> cbrt(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::cbrt(value);
  });
}

ksj::array::PooledVector<double> cbrt(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::cbrt(value);
  });
}

ksj::array::PooledVector<float> pow(ksj::array::VectorView<const float> base,
                                    ksj::array::VectorView<const float> exponent) {
  return vector_binary_expr(base, exponent, [](const float lhs, const float rhs) {
    return std::pow(lhs, rhs);
  });
}

ksj::array::PooledVector<double> pow(ksj::array::VectorView<const double> base,
                                     ksj::array::VectorView<const double> exponent) {
  return vector_binary_expr(base, exponent, [](const double lhs, const double rhs) {
    return std::pow(lhs, rhs);
  });
}

ksj::array::PooledVector<float> pow(ksj::array::VectorView<const float> base, const float exponent) {
  return vector_unary_expr(base, [exponent](const float value) {
    return std::pow(value, exponent);
  });
}

ksj::array::PooledVector<double> pow(ksj::array::VectorView<const double> base, const double exponent) {
  return vector_unary_expr(base, [exponent](const double value) {
    return std::pow(value, exponent);
  });
}

ksj::array::PooledVector<float> erf(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::erf(value);
  });
}

ksj::array::PooledVector<double> erf(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::erf(value);
  });
}

ksj::array::PooledVector<float> cdf_norm(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return cdf_norm(value);
  });
}

ksj::array::PooledVector<double> cdf_norm(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return cdf_norm(value);
  });
}

ksj::array::PooledVector<float> exp(ksj::array::VectorView<const float> input) {
  return vector_exp(input);
}

ksj::array::PooledVector<double> exp(ksj::array::VectorView<const double> input) {
  return vector_exp(input);
}

ksj::array::PooledVector<ksj::base::cf32> exp(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_exp(input);
}

ksj::array::PooledVector<ksj::base::cf64> exp(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_exp(input);
}

ksj::array::PooledVector<float> exp2(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::exp2(value);
  });
}

ksj::array::PooledVector<double> exp2(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::exp2(value);
  });
}

ksj::array::PooledVector<float> expm1(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::expm1(value);
  });
}

ksj::array::PooledVector<double> expm1(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::expm1(value);
  });
}

ksj::array::PooledVector<float> log1p(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::log1p(value);
  });
}

ksj::array::PooledVector<double> log1p(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::log1p(value);
  });
}

ksj::array::PooledVector<float> erfc(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return std::erfc(value);
  });
}

ksj::array::PooledVector<double> erfc(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return std::erfc(value);
  });
}

ksj::array::PooledVector<float> sinpi(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return sinpi(value);
  });
}

ksj::array::PooledVector<double> sinpi(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return sinpi(value);
  });
}

ksj::array::PooledVector<float> cospi(ksj::array::VectorView<const float> input) {
  return vector_unary_expr(input, [](const float value) {
    return cospi(value);
  });
}

ksj::array::PooledVector<double> cospi(ksj::array::VectorView<const double> input) {
  return vector_unary_expr(input, [](const double value) {
    return cospi(value);
  });
}

ksj::array::PooledVector<ksj::base::cf32> conj(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr(input, [](const ksj::base::cf32 value) {
    return std::conj(value);
  });
}

ksj::array::PooledVector<ksj::base::cf64> conj(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr(input, [](const ksj::base::cf64 value) {
    return std::conj(value);
  });
}

ksj::array::PooledVector<float> abs(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr<ksj::base::cf32, float>(input, [](const ksj::base::cf32 value) {
    return std::abs(value);
  });
}

ksj::array::PooledVector<double> abs(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr<ksj::base::cf64, double>(input, [](const ksj::base::cf64 value) {
    return std::abs(value);
  });
}

ksj::array::PooledVector<float> arg(ksj::array::VectorView<const ksj::base::cf32> input) {
  return vector_unary_expr<ksj::base::cf32, float>(input, [](const ksj::base::cf32 value) {
    return std::arg(value);
  });
}

ksj::array::PooledVector<double> arg(ksj::array::VectorView<const ksj::base::cf64> input) {
  return vector_unary_expr<ksj::base::cf64, double>(input, [](const ksj::base::cf64 value) {
    return std::arg(value);
  });
}

} // namespace ksj::special::detail::eigen
