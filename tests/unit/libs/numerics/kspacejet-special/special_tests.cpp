#include "kspacejet/base/types.hpp"
#include "kspacejet/special/detail/intel/intel_special_functions.hpp"
#include "kspacejet/special/special.hpp"

#include <array>
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetSpecial, ComputesScalarFunctions) {
  EXPECT_NEAR(24.0, ksj::special::gamma(5.0), 1e-12);
  EXPECT_NEAR(std::log(24.0), ksj::special::log_gamma(5.0), 1e-12);
  EXPECT_NEAR(1.0, ksj::special::bessel_i0(0.0), 1e-12);
  EXPECT_NEAR(1.0, ksj::special::bessel_j0(0.0), 1e-12);
  EXPECT_NEAR(0.0, ksj::special::bessel_j1(0.0), 1e-12);
  EXPECT_NEAR(ksj::special::bessel_j0(1.25), ksj::special::bessel_j(0.0, 1.25), 1e-12);
  EXPECT_NEAR(ksj::special::bessel_j1(1.25), ksj::special::bessel_j(1.0, 1.25), 1e-12);
}

TEST(KSpaceJetSpecial, ComputesPureImaginaryComplexBesselJ) {
  const auto value = ksj::special::bessel_j(1.5, ksj::base::cf64(0.0, 2.0));
  const auto expected_magnitude = std::cyl_bessel_i(1.5, 2.0);
  const auto expected_phase = 1.5 * std::numbers::pi / 2.0;

  EXPECT_NEAR(expected_magnitude * std::cos(expected_phase), value.real(), 1e-12);
  EXPECT_NEAR(expected_magnitude * std::sin(expected_phase), value.imag(), 1e-12);
}

TEST(KSpaceJetSpecial, MapsVectorFunctionsToPooledOutput) {
  auto input = ksj::array::make_pooled_vector<double>(2);
  input(0) = 1.0;
  input(1) = 2.0;

  const auto output = ksj::special::gamma(input);

  ASSERT_EQ(2U, output.size());
  EXPECT_NEAR(1.0, output(0), 1e-12);
  EXPECT_NEAR(1.0, output(1), 1e-12);

  const auto log_output = ksj::special::log_gamma(input);
  ASSERT_EQ(2U, log_output.size());
  EXPECT_NEAR(0.0, log_output(0), 1e-12);
  EXPECT_NEAR(0.0, log_output(1), 1e-12);

  const auto bessel_output = ksj::special::bessel_i0(input);
  ASSERT_EQ(2U, bessel_output.size());
  EXPECT_NEAR(ksj::special::bessel_i0(1.0), bessel_output(0), 1e-12);
  EXPECT_NEAR(ksj::special::bessel_i0(2.0), bessel_output(1), 1e-12);

  const auto bessel_j0_output = ksj::special::bessel_j0(input);
  ASSERT_EQ(2U, bessel_j0_output.size());
  EXPECT_NEAR(ksj::special::bessel_j0(1.0), bessel_j0_output(0), 1e-12);
  EXPECT_NEAR(ksj::special::bessel_j0(2.0), bessel_j0_output(1), 1e-12);
}

TEST(KSpaceJetSpecial, MapsVectorViewFunctionsToPooledOutput) {
  std::array<double, 4> values{1.0, 10.0, 2.0, 20.0};
  const auto every_other = ksj::array::VectorView<const double>(values.data(), values.size())
                             .subview(ksj::array::slice(0U, values.size(), 2U));

  const auto gamma_output = ksj::special::gamma(every_other);
  ASSERT_EQ(2U, gamma_output.size());
  EXPECT_NEAR(1.0, gamma_output(0), 1e-12);
  EXPECT_NEAR(1.0, gamma_output(1), 1e-12);

  const auto bessel_i0_output = ksj::special::bessel_i0(every_other);
  ASSERT_EQ(2U, bessel_i0_output.size());
  EXPECT_NEAR(ksj::special::bessel_i0(1.0), bessel_i0_output(0), 1e-12);
  EXPECT_NEAR(ksj::special::bessel_i0(2.0), bessel_i0_output(1), 1e-12);

  const auto bessel_j_output = ksj::special::bessel_j(1.0, every_other);
  ASSERT_EQ(2U, bessel_j_output.size());
  EXPECT_NEAR(ksj::special::bessel_j1(1.0), bessel_j_output(0), 1e-12);
  EXPECT_NEAR(ksj::special::bessel_j1(2.0), bessel_j_output(1), 1e-12);
}

TEST(KSpaceJetSpecial, MapsVectorSinAndExpFunctionsToPooledOutput) {
  auto real_input = ksj::array::make_pooled_vector<float>(3);
  real_input(0) = 0.0F;
  real_input(1) = 0.5F;
  real_input(2) = 1.0F;

  const auto sin_output = ksj::special::sin(real_input);
  ASSERT_EQ(real_input.size(), sin_output.size());
  for (std::size_t index = 0; index < real_input.size(); ++index) {
    EXPECT_NEAR(std::sin(real_input(index)), sin_output(index), 1e-6F);
  }

  auto complex_input = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  complex_input(0) = ksj::base::cf32(0.0F, 0.0F);
  complex_input(1) = ksj::base::cf32(0.25F, -0.5F);

  const auto exp_output = ksj::special::exp(complex_input);
  ASSERT_EQ(complex_input.size(), exp_output.size());
  for (std::size_t index = 0; index < complex_input.size(); ++index) {
    const auto expected = std::exp(complex_input(index));
    EXPECT_NEAR(expected.real(), exp_output(index).real(), 1e-6F);
    EXPECT_NEAR(expected.imag(), exp_output(index).imag(), 1e-6F);
  }
}

TEST(KSpaceJetSpecial, IntelBackendWrapsVmlSinAndComplexExp) {
  auto real_input = ksj::array::make_pooled_vector<float>(4);
  auto real_output = ksj::array::make_pooled_vector<float>(real_input.size());
  for (std::size_t index = 0; index < real_input.size(); ++index) {
    real_input(index) = static_cast<float>(index) * 0.25F;
  }

  ASSERT_TRUE(ksj::special::detail::intel::sin(ksj::array::as_const_view(real_input.view()), real_output.view()));
  for (std::size_t index = 0; index < real_input.size(); ++index) {
    EXPECT_NEAR(std::sin(real_input(index)), real_output(index), 1e-6F);
  }

  auto complex_input = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  auto complex_output = ksj::array::make_pooled_vector<ksj::base::cf32>(complex_input.size());
  for (std::size_t index = 0; index < complex_input.size(); ++index) {
    complex_input(index) = ksj::base::cf32(static_cast<float>(index) * 0.2F, -static_cast<float>(index) * 0.125F);
  }

  ASSERT_TRUE(ksj::special::detail::intel::exp(ksj::array::as_const_view(complex_input.view()), complex_output.view()));
  for (std::size_t index = 0; index < complex_input.size(); ++index) {
    const auto expected = std::exp(complex_input(index));
    EXPECT_NEAR(expected.real(), complex_output(index).real(), 1e-6F);
    EXPECT_NEAR(expected.imag(), complex_output(index).imag(), 1e-6F);
  }
}

TEST(KSpaceJetSpecial, MapsAdditionalVectorMathFunctions) {
  auto input = ksj::array::make_pooled_vector<double>(3);
  input(0) = 0.25;
  input(1) = 0.5;
  input(2) = 0.75;
  auto exponent = ksj::array::make_pooled_vector<double>(3);
  exponent(0) = 2.0;
  exponent(1) = 3.0;
  exponent(2) = 4.0;

  const auto cos_output = ksj::special::cos(input);
  const auto tan_output = ksj::special::tan(input);
  const auto asin_output = ksj::special::asin(input);
  const auto acos_output = ksj::special::acos(input);
  const auto atan_output = ksj::special::atan(input);
  const auto atan2_output = ksj::special::atan2(input, exponent);
  const auto ln_output = ksj::special::ln(input);
  const auto log10_output = ksj::special::log10(input);
  const auto log2_output = ksj::special::log2(input);
  const auto sqrt_output = ksj::special::sqrt(input);
  const auto cbrt_output = ksj::special::cbrt(input);
  const auto pow_output = ksj::special::pow(input, exponent);
  const auto powx_output = ksj::special::pow(input, 2.5);
  const auto erf_output = ksj::special::erf(input);
  const auto cdf_output = ksj::special::cdf_norm(input);
  const auto cdf_alias_output = ksj::special::cdfnorm(input);

  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(std::cos(input(index)), cos_output(index), 1.0e-12);
    EXPECT_NEAR(std::tan(input(index)), tan_output(index), 1.0e-12);
    EXPECT_NEAR(std::asin(input(index)), asin_output(index), 1.0e-12);
    EXPECT_NEAR(std::acos(input(index)), acos_output(index), 1.0e-12);
    EXPECT_NEAR(std::atan(input(index)), atan_output(index), 1.0e-12);
    EXPECT_NEAR(std::atan2(input(index), exponent(index)), atan2_output(index), 1.0e-12);
    EXPECT_NEAR(std::log(input(index)), ln_output(index), 1.0e-12);
    EXPECT_NEAR(std::log10(input(index)), log10_output(index), 1.0e-12);
    EXPECT_NEAR(std::log2(input(index)), log2_output(index), 1.0e-12);
    EXPECT_NEAR(std::sqrt(input(index)), sqrt_output(index), 1.0e-12);
    EXPECT_NEAR(std::cbrt(input(index)), cbrt_output(index), 1.0e-12);
    EXPECT_NEAR(std::pow(input(index), exponent(index)), pow_output(index), 1.0e-12);
    EXPECT_NEAR(std::pow(input(index), 2.5), powx_output(index), 1.0e-12);
    EXPECT_NEAR(std::erf(input(index)), erf_output(index), 1.0e-12);
    const auto expected_cdf = 0.5 * (1.0 + std::erf(input(index) / std::sqrt(2.0)));
    EXPECT_NEAR(expected_cdf, cdf_output(index), 1.0e-12);
    EXPECT_NEAR(expected_cdf, cdf_alias_output(index), 1.0e-12);
  }
}

TEST(KSpaceJetSpecial, ComputesStableRealSpecialFunctionsForScalarsAndViews) {
  constexpr double kValue = 0.25;
  EXPECT_NEAR(std::exp2(kValue), ksj::special::exp2(kValue), 1.0e-12);
  EXPECT_NEAR(std::expm1(kValue), ksj::special::expm1(kValue), 1.0e-12);
  EXPECT_NEAR(std::log1p(kValue), ksj::special::log1p(kValue), 1.0e-12);
  EXPECT_NEAR(std::erfc(kValue), ksj::special::erfc(kValue), 1.0e-12);
  EXPECT_NEAR(std::sin(std::numbers::pi * kValue), ksj::special::sinpi(kValue), 1.0e-12);
  EXPECT_NEAR(std::cos(std::numbers::pi * kValue), ksj::special::cospi(kValue), 1.0e-12);

  auto input = ksj::array::make_pooled_vector<double>(3U);
  input(0U) = 0.125;
  input(1U) = 0.25;
  input(2U) = 0.5;
  auto output = ksj::array::make_pooled_vector<double>(input.size());
  ksj::special::log1p(input.view(), output.view());
  for (std::size_t index = 0U; index < input.size(); ++index) {
    EXPECT_NEAR(std::log1p(input(index)), output(index), 1.0e-12);
  }

  const auto exp2_output = ksj::special::exp2(input);
  const auto expm1_output = ksj::special::expm1(input);
  const auto erfc_output = ksj::special::erfc(input);
  const auto sinpi_output = ksj::special::sinpi(input);
  const auto cospi_output = ksj::special::cospi(input);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    EXPECT_NEAR(std::exp2(input(index)), exp2_output(index), 1.0e-12);
    EXPECT_NEAR(std::expm1(input(index)), expm1_output(index), 1.0e-12);
    EXPECT_NEAR(std::erfc(input(index)), erfc_output(index), 1.0e-12);
    EXPECT_NEAR(std::sin(std::numbers::pi * input(index)), sinpi_output(index), 1.0e-12);
    EXPECT_NEAR(std::cos(std::numbers::pi * input(index)), cospi_output(index), 1.0e-12);
  }
}

TEST(KSpaceJetSpecial, MapsStableRealSpecialFunctionsAcrossArrayShapes) {
  auto image = ksj::array::make_pooled_image<float>(2U, 2U);
  image(0U, 0U) = 0.125F;
  image(0U, 1U) = 0.25F;
  image(1U, 0U) = 0.5F;
  image(1U, 1U) = 0.75F;

  const auto exp2_image = ksj::special::exp2(image);
  const auto sinpi_image = ksj::special::sinpi(image);
  auto log1p_image = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  ksj::special::log1p(image.view(), log1p_image.view());

  EXPECT_NEAR(std::exp2(image(1U, 0U)), exp2_image(1U, 0U), 1.0e-6F);
  EXPECT_NEAR(std::sin(std::numbers::pi_v<float> * image(0U, 1U)), sinpi_image(0U, 1U), 1.0e-6F);
  EXPECT_NEAR(std::log1p(image(1U, 1U)), log1p_image(1U, 1U), 1.0e-6F);
}

TEST(KSpaceJetSpecial, MapsComplexVectorMathFunctions) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf32>(3);
  input(0) = ksj::base::cf32(0.25F, -0.5F);
  input(1) = ksj::base::cf32(0.5F, 0.25F);
  input(2) = ksj::base::cf32(1.0F, -0.75F);

  const auto sin_output = ksj::special::sin(input);
  const auto cos_output = ksj::special::cos(input);
  const auto ln_output = ksj::special::ln(input);
  const auto sqrt_output = ksj::special::sqrt(input);
  const auto conj_output = ksj::special::conj(input);
  const auto abs_output = ksj::special::abs(input);
  const auto arg_output = ksj::special::arg(input);

  for (std::size_t index = 0; index < input.size(); ++index) {
    const auto expected_sin = std::sin(input(index));
    const auto expected_cos = std::cos(input(index));
    const auto expected_ln = std::log(input(index));
    const auto expected_sqrt = std::sqrt(input(index));
    EXPECT_NEAR(expected_sin.real(), sin_output(index).real(), 1.0e-6F);
    EXPECT_NEAR(expected_sin.imag(), sin_output(index).imag(), 1.0e-6F);
    EXPECT_NEAR(expected_cos.real(), cos_output(index).real(), 1.0e-6F);
    EXPECT_NEAR(expected_cos.imag(), cos_output(index).imag(), 1.0e-6F);
    EXPECT_NEAR(expected_ln.real(), ln_output(index).real(), 1.0e-6F);
    EXPECT_NEAR(expected_ln.imag(), ln_output(index).imag(), 1.0e-6F);
    EXPECT_NEAR(expected_sqrt.real(), sqrt_output(index).real(), 1.0e-6F);
    EXPECT_NEAR(expected_sqrt.imag(), sqrt_output(index).imag(), 1.0e-6F);
    EXPECT_FLOAT_EQ(std::conj(input(index)).real(), conj_output(index).real());
    EXPECT_FLOAT_EQ(std::conj(input(index)).imag(), conj_output(index).imag());
    EXPECT_NEAR(std::abs(input(index)), abs_output(index), 1.0e-6F);
    EXPECT_NEAR(std::arg(input(index)), arg_output(index), 1.0e-6F);
  }
}

TEST(KSpaceJetSpecial, IntelBackendWrapsAdditionalVmlFunctions) {
  auto input = ksj::array::make_pooled_vector<float>(4);
  auto rhs = ksj::array::make_pooled_vector<float>(4);
  auto output = ksj::array::make_pooled_vector<float>(4);
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = static_cast<float>(index + 1U) * 0.125F;
    rhs(index) = static_cast<float>(index + 2U) * 0.25F;
  }

  ASSERT_TRUE(ksj::special::detail::intel::cos(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::cos(input(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::atan2(ksj::array::as_const_view(input.view()),
                                                 ksj::array::as_const_view(rhs.view()), output.view()));
  EXPECT_NEAR(std::atan2(input(2), rhs(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::pow(ksj::array::as_const_view(input.view()),
                                               ksj::array::as_const_view(rhs.view()), output.view()));
  EXPECT_NEAR(std::pow(input(2), rhs(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::cdf_norm(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(0.5F * (1.0F + std::erf(input(2) / std::sqrt(2.0F))), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::exp2(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::exp2(input(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::expm1(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::expm1(input(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::log1p(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::log1p(input(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::erfc(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::erfc(input(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::sinpi(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::sin(std::numbers::pi_v<float> * input(2)), output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::cospi(ksj::array::as_const_view(input.view()), output.view()));
  EXPECT_NEAR(std::cos(std::numbers::pi_v<float> * input(2)), output(2), 1.0e-6F);

  auto complex_input = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  auto complex_output = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  auto real_output = ksj::array::make_pooled_vector<float>(4);
  for (std::size_t index = 0; index < complex_input.size(); ++index) {
    complex_input(index) =
      ksj::base::cf32(static_cast<float>(index + 1U) * 0.125F, -static_cast<float>(index + 2U) * 0.25F);
  }

  ASSERT_TRUE(ksj::special::detail::intel::cos(ksj::array::as_const_view(complex_input.view()), complex_output.view()));
  const auto expected_cos = std::cos(complex_input(2));
  EXPECT_NEAR(expected_cos.real(), complex_output(2).real(), 1.0e-6F);
  EXPECT_NEAR(expected_cos.imag(), complex_output(2).imag(), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::abs(ksj::array::as_const_view(complex_input.view()), real_output.view()));
  EXPECT_NEAR(std::abs(complex_input(2)), real_output(2), 1.0e-6F);
  ASSERT_TRUE(ksj::special::detail::intel::arg(ksj::array::as_const_view(complex_input.view()), real_output.view()));
  EXPECT_NEAR(std::arg(complex_input(2)), real_output(2), 1.0e-6F);
}

TEST(KSpaceJetSpecial, MapsMatrixAndImageSpecialFunctions) {
  auto matrix = ksj::array::make_pooled_matrix<float>(2U, 3U);
  for (std::size_t row = 0U; row < matrix.rows(); ++row) {
    for (std::size_t col = 0U; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<float>(row * matrix.cols() + col + 1U) * 0.25F;
    }
  }

  const auto matrix_exp = ksj::special::exp(matrix);
  const auto matrix_bessel = ksj::special::bessel_j(1.0F, matrix);
  ASSERT_EQ(matrix.rows(), matrix_exp.rows());
  ASSERT_EQ(matrix.cols(), matrix_exp.cols());
  EXPECT_NEAR(std::exp(matrix(1U, 2U)), matrix_exp(1U, 2U), 1.0e-6F);
  EXPECT_NEAR(ksj::special::bessel_j(1.0F, matrix(0U, 1U)), matrix_bessel(0U, 1U), 1.0e-6F);

  const auto matrix_sqrt = ksj::special::sqrt(ksj::array::as_const_view(matrix.view()));
  EXPECT_NEAR(std::sqrt(matrix(0U, 2U)), matrix_sqrt(0U, 2U), 1.0e-6F);

  auto image = ksj::array::make_pooled_image<double>(2U, 2U);
  image(0U, 0U) = 0.25;
  image(0U, 1U) = 0.5;
  image(1U, 0U) = 0.75;
  image(1U, 1U) = 1.0;

  const auto image_cos = ksj::special::cos(image);
  ASSERT_EQ(image.rows(), image_cos.rows());
  ASSERT_EQ(image.cols(), image_cos.cols());
  EXPECT_NEAR(std::cos(image(1U, 1U)), image_cos(1U, 1U), 1.0e-12);

  const auto image_pow = ksj::special::pow(ksj::array::as_const_view(image.view()), 2.0);
  EXPECT_NEAR(std::pow(image(1U, 0U), 2.0), image_pow(1U, 0U), 1.0e-12);
}

TEST(KSpaceJetSpecial, MapsCubeAndArray4DSpecialFunctions) {
  auto cube = ksj::array::make_pooled_cube<double>(2U, 2U, 2U);
  for (std::size_t index = 0U; index < cube.size(); ++index) {
    cube[index] = static_cast<double>(index + 1U) * 0.125;
  }

  const auto cube_erf = ksj::special::erf(cube);
  const auto cube_cdf = ksj::special::cdfnorm(cube);
  ASSERT_EQ(cube.dim0(), cube_erf.dim0());
  ASSERT_EQ(cube.dim1(), cube_erf.dim1());
  ASSERT_EQ(cube.dim2(), cube_erf.dim2());
  EXPECT_NEAR(std::erf(cube(1U, 1U, 1U)), cube_erf(1U, 1U, 1U), 1.0e-12);
  EXPECT_NEAR(ksj::special::cdf_norm(cube(0U, 1U, 1U)), cube_cdf(0U, 1U, 1U), 1.0e-12);

  auto lhs = ksj::array::make_pooled_array4d<float>(1U, 2U, 2U, 2U);
  auto rhs = ksj::array::make_pooled_array4d<float>(1U, 2U, 2U, 2U);
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    lhs[index] = static_cast<float>(index + 1U) * 0.125F;
    rhs[index] = static_cast<float>(index + 2U) * 0.25F;
  }

  const auto atan_output = ksj::special::atan2(lhs, rhs);
  ASSERT_EQ(lhs.dim0(), atan_output.dim0());
  ASSERT_EQ(lhs.dim1(), atan_output.dim1());
  ASSERT_EQ(lhs.dim2(), atan_output.dim2());
  ASSERT_EQ(lhs.dim3(), atan_output.dim3());
  EXPECT_NEAR(std::atan2(lhs(0U, 1U, 1U, 1U), rhs(0U, 1U, 1U, 1U)), atan_output(0U, 1U, 1U, 1U), 1.0e-6F);
}

TEST(KSpaceJetSpecial, MapsComplexImageSpecialFunctionsToRealOutputs) {
  auto input = ksj::array::make_pooled_image<ksj::base::cf32>(2U, 2U);
  input(0U, 0U) = ksj::base::cf32(0.25F, -0.5F);
  input(0U, 1U) = ksj::base::cf32(0.5F, 0.25F);
  input(1U, 0U) = ksj::base::cf32(1.0F, -0.75F);
  input(1U, 1U) = ksj::base::cf32(0.75F, 0.5F);

  const auto magnitude = ksj::special::abs(input);
  const auto phase = ksj::special::arg(input);
  const auto conjugated = ksj::special::conj(input);

  ASSERT_EQ(input.rows(), magnitude.rows());
  ASSERT_EQ(input.cols(), magnitude.cols());
  EXPECT_NEAR(std::abs(input(1U, 0U)), magnitude(1U, 0U), 1.0e-6F);
  EXPECT_NEAR(std::arg(input(1U, 1U)), phase(1U, 1U), 1.0e-6F);
  EXPECT_FLOAT_EQ(std::conj(input(0U, 1U)).real(), conjugated(0U, 1U).real());
  EXPECT_FLOAT_EQ(std::conj(input(0U, 1U)).imag(), conjugated(0U, 1U).imag());
}

} // namespace
