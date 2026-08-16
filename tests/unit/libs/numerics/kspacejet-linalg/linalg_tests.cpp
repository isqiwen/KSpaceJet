#include "../eigen_test_adapter.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/linalg/linalg.hpp"
#include "kspacejet/stats/stats.hpp"

#include <array>
#include <complex>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>
#include <mkl_cblas.h>

namespace {

constexpr float kCf32Tolerance = 1.0e-5F;

[[nodiscard]] MKL_Complex8* test_mkl_complex_cast(ksj::base::cf32* data) noexcept {
  return reinterpret_cast<MKL_Complex8*>(data);
}

TEST(KSpaceJetLinalg, UsesMeasuredBlasDispatchBoundaries) {
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_gemv(16U, 16U));

  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_dot<float>(16U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_dot<float>(32U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_dot<double>(16U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_dot<double>(32U));
}

TEST(KSpaceJetLinalg, UsesSeparateComplexAllocatingAndWorkspaceDispatchBoundaries) {
  using ksj::base::cf32;
  using ksj::base::cf64;

  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_inverse<cf32>(16U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_inverse<cf32>(32U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_inverse_workspace<cf32>(2U));

  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_lu<cf32>(64U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_lu<cf32>(128U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_lu<cf64>(64U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_lu<cf64>(128U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_lu_workspace<cf32>(2U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_lu_workspace<cf32>(4U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_lu_matrix<cf32>(128U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_lu_matrix<cf32>(256U, 4U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_lu_matrix<cf64>(64U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_lu_matrix<cf64>(128U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_lu_matrix_workspace<cf32>(2U, 2U));

  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_qr<cf32>(16U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_qr<cf32>(32U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_qr_workspace<cf32>(4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_qr_workspace<cf32>(8U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_qr_matrix<cf32>(32U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_qr_matrix<cf32>(64U, 4U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_qr_matrix_workspace<cf32>(16U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_qr_matrix_workspace<cf32>(32U, 4U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_solve_qr_matrix<cf64>(16U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_qr_matrix<cf64>(32U, 4U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_solve_qr_matrix_workspace<cf64>(8U, 4U));

  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_general_eigen<cf32>(256U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_general_eigen_workspace<cf64>(256U));
  EXPECT_FALSE(ksj::linalg::detail::prefer_intel_svd<cf32>(128U, 128U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_svd<cf64>(128U, 128U));
  EXPECT_TRUE(ksj::linalg::detail::prefer_intel_svd_workspace<cf32>(2U, 2U));
}

TEST(KSpaceJetLinalg, SupportsStackAllocatedSmallVector2d) {
  const ksj::linalg::Vector2d lhs{3.0, 4.0};
  const ksj::linalg::Vector2d rhs{1.0, 2.0};

  EXPECT_EQ(2U, ksj::linalg::Vector2d::size());
  EXPECT_DOUBLE_EQ(3.0, lhs.x);
  EXPECT_DOUBLE_EQ(4.0, lhs.y);
  EXPECT_DOUBLE_EQ(3.0, lhs[0]);
  EXPECT_DOUBLE_EQ(4.0, lhs.at(1));
  EXPECT_DOUBLE_EQ(5.0, lhs.length());
  EXPECT_DOUBLE_EQ(11.0, ksj::linalg::Vector2d::dot(lhs, rhs));
  EXPECT_DOUBLE_EQ(2.0, ksj::linalg::Vector2d::cross(lhs, rhs));

  const auto normalized = lhs.normalized();
  EXPECT_DOUBLE_EQ(0.6, normalized.x);
  EXPECT_DOUBLE_EQ(0.8, normalized.y);

  auto accumulated = lhs;
  accumulated += rhs;
  accumulated *= 0.5;
  EXPECT_DOUBLE_EQ(2.0, accumulated.x);
  EXPECT_DOUBLE_EQ(3.0, accumulated.y);

  EXPECT_THROW((void)lhs.at(2), std::out_of_range);
}

TEST(KSpaceJetLinalg, SupportsStackAllocatedSmallMatrix3d) {
  const auto identity = ksj::linalg::identity_matrix3();
  const ksj::linalg::Vector3d point{1.0, 2.0, 3.0};

  const auto same_point = ksj::linalg::matrix_times_point(identity, point);

  EXPECT_EQ(3U, ksj::linalg::Matrix3d::rows());
  EXPECT_EQ(3U, ksj::linalg::Matrix3d::cols());
  EXPECT_EQ(9U, ksj::linalg::Matrix3d::size());
  EXPECT_DOUBLE_EQ(1.0, identity(0, 0));
  EXPECT_DOUBLE_EQ(1.0, identity.at(4));
  EXPECT_EQ(identity.data(), identity.begin());

  EXPECT_DOUBLE_EQ(1.0, same_point.x);
  EXPECT_DOUBLE_EQ(2.0, same_point.y);
  EXPECT_DOUBLE_EQ(3.0, same_point.z);
  EXPECT_DOUBLE_EQ(2.0, same_point.at(1));
  EXPECT_EQ(same_point.data(), same_point.begin());

  const auto cross = ksj::linalg::Vector3d::cross({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
  EXPECT_DOUBLE_EQ(0.0, cross.x);
  EXPECT_DOUBLE_EQ(0.0, cross.y);
  EXPECT_DOUBLE_EQ(1.0, cross.z);
  EXPECT_DOUBLE_EQ(0.0, ksj::linalg::Vector3d::dot({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}));
  EXPECT_THROW((void)identity(3, 0), std::out_of_range);
  EXPECT_THROW((void)same_point.at(3), std::out_of_range);
}

TEST(KSpaceJetLinalg, SupportsSmallMatrix3dRotationMultiplyAndInverse) {
  const auto rotation = ksj::linalg::rotation_z(std::numbers::pi / 2.0);
  const auto point = ksj::linalg::point_times_matrix({1.0, 0.0, 0.0}, rotation);

  EXPECT_NEAR(0.0, point.x, 1.0e-12);
  EXPECT_NEAR(1.0, point.y, 1.0e-12);
  EXPECT_NEAR(0.0, point.z, 1.0e-12);

  const auto inverse = ksj::linalg::inverse(rotation);
  ASSERT_TRUE(inverse.has_value());
  const auto product = ksj::linalg::multiply(rotation, *inverse);

  EXPECT_NEAR(1.0, ksj::linalg::determinant(rotation), 1.0e-12);
  EXPECT_NEAR(1.0, product[0], 1.0e-12);
  EXPECT_NEAR(1.0, product[4], 1.0e-12);
  EXPECT_NEAR(1.0, product[8], 1.0e-12);
  EXPECT_NEAR(0.0, product[1], 1.0e-12);
  EXPECT_NEAR(0.0, product[3], 1.0e-12);

  const ksj::linalg::Matrix3d singular{
    1.0, 2.0, 3.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0,
  };
  EXPECT_FALSE(ksj::linalg::inverse(singular).has_value());

  const ksj::linalg::Matrix3d near_singular{
    1.0e-15, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
  };
  EXPECT_FALSE(ksj::linalg::inverse(near_singular).has_value());
  EXPECT_TRUE(ksj::linalg::inverse(near_singular, 1.0e-18).has_value());
}

TEST(KSpaceJetLinalg, SupportsSmallMatrix3fRotationMultiplyAndInverse) {
  const auto identity = ksj::linalg::identity_matrix3<float>();
  const ksj::linalg::Vector3f source{1.0F, 2.0F, 3.0F};
  const auto same_point = ksj::linalg::matrix_times_point(identity, source);

  EXPECT_FLOAT_EQ(1.0F, same_point.x);
  EXPECT_FLOAT_EQ(2.0F, same_point.y);
  EXPECT_FLOAT_EQ(3.0F, same_point.z);
  EXPECT_FLOAT_EQ(1.0F, same_point[0]);
  EXPECT_FLOAT_EQ(2.0F, same_point[1]);
  EXPECT_FLOAT_EQ(3.0F, same_point[2]);

  auto indexed = ksj::linalg::Vector3f{};
  indexed[0] = 4.0F;
  indexed[1] = 5.0F;
  indexed[2] = 6.0F;
  EXPECT_FLOAT_EQ(4.0F, indexed.x);
  EXPECT_FLOAT_EQ(5.0F, indexed.y);
  EXPECT_FLOAT_EQ(6.0F, indexed.z);

  const auto rotation = ksj::linalg::rotation_z(std::numbers::pi_v<float> / 2.0F);
  const auto inverse = ksj::linalg::inverse(rotation);
  ASSERT_TRUE(inverse.has_value());
  const auto product = ksj::linalg::multiply(rotation, *inverse);

  EXPECT_NEAR(1.0F, ksj::linalg::determinant(rotation), 1.0e-5F);
  EXPECT_NEAR(1.0F, product[0], 1.0e-5F);
  EXPECT_NEAR(1.0F, product[4], 1.0e-5F);
  EXPECT_NEAR(1.0F, product[8], 1.0e-5F);
  EXPECT_NEAR(0.0F, product[1], 1.0e-5F);
  EXPECT_NEAR(0.0F, product[3], 1.0e-5F);

  const ksj::linalg::Matrix3f near_singular{
    1.0e-8F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F,
  };
  EXPECT_FALSE(ksj::linalg::inverse(near_singular).has_value());
  EXPECT_TRUE(ksj::linalg::inverse(near_singular, 1.0e-10F).has_value());
}

TEST(KSpaceJetLinalg, SolvesStackAllocatedLeastSquares4x2) {
  const ksj::linalg::Matrix4x2d matrix{
    1.0, 0.5, 0.0, 0.25, 1.0, 2.0, 0.0, 1.0,
  };
  const ksj::linalg::Vector4d rhs{
    12.0,
    1.0,
    18.0,
    4.0,
  };

  EXPECT_EQ(4U, ksj::linalg::Vector4d::size());
  EXPECT_DOUBLE_EQ(12.0, rhs.x);
  EXPECT_DOUBLE_EQ(4.0, rhs.w);
  EXPECT_DOUBLE_EQ(18.0, rhs.at(2));
  EXPECT_EQ(rhs.data(), rhs.begin());
  EXPECT_DOUBLE_EQ(17.0, ksj::linalg::Vector4d::dot(rhs, {1.0, 1.0, 0.0, 1.0}));
  EXPECT_EQ(4U, ksj::linalg::Matrix4x2d::rows());
  EXPECT_EQ(2U, ksj::linalg::Matrix4x2d::cols());
  EXPECT_EQ(8U, ksj::linalg::Matrix4x2d::size());
  EXPECT_DOUBLE_EQ(2.0, matrix(2, 1));
  EXPECT_EQ(matrix.data(), matrix.begin());

  const auto solution = ksj::linalg::least_squares_4x2(matrix, rhs);

  ASSERT_TRUE(solution.has_value());
  EXPECT_NEAR(10.0, solution->at(0), 1.0e-12);
  EXPECT_NEAR(4.0, solution->at(1), 1.0e-12);

  const auto pseudo_inverse = ksj::linalg::pseudo_inverse_4x2(matrix);
  ASSERT_TRUE(pseudo_inverse.has_value());
  EXPECT_EQ(2U, ksj::linalg::Matrix2x4d::rows());
  EXPECT_EQ(4U, ksj::linalg::Matrix2x4d::cols());
  EXPECT_EQ(8U, ksj::linalg::Matrix2x4d::size());
  EXPECT_EQ(pseudo_inverse->data(), pseudo_inverse->begin());
  EXPECT_DOUBLE_EQ((*pseudo_inverse)(0, 0), pseudo_inverse->at(0));

  const ksj::linalg::Matrix4x2d singular{};
  EXPECT_FALSE(ksj::linalg::least_squares_4x2(singular, rhs).has_value());
  EXPECT_THROW((void)matrix(4, 0), std::out_of_range);
  EXPECT_THROW((void)pseudo_inverse->at(8), std::out_of_range);
  EXPECT_THROW((void)rhs.at(4), std::out_of_range);
}

TEST(KSpaceJetLinalg, MatmulReturnsPooledMatrix) {
  auto lhs = ksj::array::make_pooled_matrix<double>(2, 3);
  auto rhs = ksj::array::make_pooled_matrix<double>(3, 2);

  as_eigen(lhs) << 1.0, 2.0, 3.0, 4.0, 5.0, 6.0;
  as_eigen(rhs) << 7.0, 8.0, 9.0, 10.0, 11.0, 12.0;

  auto output = ksj::linalg::matmul(lhs, rhs);

  EXPECT_EQ(2U, output.rows());
  EXPECT_EQ(2U, output.cols());
  EXPECT_DOUBLE_EQ(58.0, output(0, 0));
  EXPECT_DOUBLE_EQ(139.0, output(1, 0));
  EXPECT_DOUBLE_EQ(64.0, output(0, 1));
  EXPECT_DOUBLE_EQ(154.0, output(1, 1));

  const auto scaled_output = ksj::linalg::matmul(lhs, rhs, 0.5);
  EXPECT_EQ(2U, scaled_output.rows());
  EXPECT_EQ(2U, scaled_output.cols());
  EXPECT_DOUBLE_EQ(29.0, scaled_output(0, 0));
  EXPECT_DOUBLE_EQ(77.0, scaled_output(1, 1));
}

TEST(KSpaceJetLinalg, MatmulSupportsStridedRowMajorMatrixViews) {
  std::array<double, 8> lhs_storage{1.0, 2.0, 3.0, -1.0, 4.0, 5.0, 6.0, -1.0};
  std::array<double, 9> rhs_storage{7.0, 8.0, -1.0, 9.0, 10.0, -1.0, 11.0, 12.0, -1.0};
  std::array<double, 6> output_storage{};

  ksj::linalg::matmul(
    ksj::array::MatrixView<const double>(lhs_storage.data(), 2U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U)),
    ksj::array::MatrixView<const double>(rhs_storage.data(), 3U, 3U).subview(ksj::array::_, ksj::array::slice(0U, 2U)),
    ksj::array::MatrixView<double>(output_storage.data(), 2U, 3U).subview(ksj::array::_, ksj::array::slice(0U, 2U)));

  EXPECT_DOUBLE_EQ(58.0, output_storage[0]);
  EXPECT_DOUBLE_EQ(64.0, output_storage[1]);
  EXPECT_DOUBLE_EQ(139.0, output_storage[3]);
  EXPECT_DOUBLE_EQ(154.0, output_storage[4]);
}

TEST(KSpaceJetLinalg, GemvReturnsPooledVector) {
  auto matrix = ksj::array::make_pooled_matrix<float>(2, 3);
  auto vector = ksj::array::make_pooled_vector<float>(3);
  auto output = ksj::array::make_pooled_vector<float>(2);

  as_eigen(matrix) << 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F;
  as_eigen(vector) << 2.0F, 3.0F, 4.0F;

  ksj::linalg::gemv(matrix, vector, output);

  EXPECT_EQ(2U, output.size());
  EXPECT_FLOAT_EQ(20.0F, output(0));
  EXPECT_FLOAT_EQ(47.0F, output(1));

  auto returned = ksj::linalg::gemv(matrix, vector);
  EXPECT_FLOAT_EQ(20.0F, returned(0));
  EXPECT_FLOAT_EQ(47.0F, returned(1));
}

TEST(KSpaceJetLinalg, GemvSupportsStridedRowMajorMatrixView) {
  std::array<float, 8> matrix_storage{1.0F, 2.0F, 3.0F, -1.0F, 4.0F, 5.0F, 6.0F, -1.0F};
  std::array<float, 3> vector_storage{2.0F, 3.0F, 4.0F};
  std::array<float, 4> output_storage{};

  ksj::linalg::gemv(
    ksj::array::MatrixView<const float>(matrix_storage.data(), 2U, 4U)
      .subview(ksj::array::_, ksj::array::slice(0U, 3U)),
    ksj::array::VectorView<const float>(vector_storage.data(), vector_storage.size()),
    ksj::array::VectorView<float>(output_storage.data(), output_storage.size()).subview(ksj::array::slice(0U, 4U, 2U)));

  EXPECT_FLOAT_EQ(20.0F, output_storage[0]);
  EXPECT_FLOAT_EQ(47.0F, output_storage[2]);
}

TEST(KSpaceJetLinalg, IntelGemvSupportsComplexCblasBackend) {
  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(2U, 3U);
  auto vector = ksj::array::make_pooled_vector<ksj::base::cf32>(3U);
  auto output = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);

  matrix(0U, 0U) = {1.0F, 1.0F};
  matrix(0U, 1U) = {2.0F, -1.0F};
  matrix(0U, 2U) = {-0.5F, 0.25F};
  matrix(1U, 0U) = {0.0F, 2.0F};
  matrix(1U, 1U) = {-3.0F, 0.5F};
  matrix(1U, 2U) = {4.0F, 0.0F};
  vector(0U) = {2.0F, -1.0F};
  vector(1U) = {-1.0F, 0.5F};
  vector(2U) = {0.25F, 2.0F};

  ASSERT_TRUE(ksj::linalg::detail::intel::gemv(ksj::array::as_const_view(matrix.view()),
                                               ksj::array::as_const_view(vector.view()), output.view()));

  for (std::size_t row = 0U; row < matrix.rows(); ++row) {
    ksj::base::cf32 expected{};
    for (std::size_t col = 0U; col < matrix.cols(); ++col) {
      expected += matrix(row, col) * vector(col);
    }
    EXPECT_NEAR(expected.real(), output(row).real(), kCf32Tolerance);
    EXPECT_NEAR(expected.imag(), output(row).imag(), kCf32Tolerance);
  }
}

TEST(KSpaceJetLinalg, IntelDotSupportsRealIppBackend) {
  auto lhs32 = ksj::array::make_pooled_vector<float>(3U);
  auto rhs32 = ksj::array::make_pooled_vector<float>(3U);
  lhs32(0U) = 1.5F;
  lhs32(1U) = -2.0F;
  lhs32(2U) = 0.25F;
  rhs32(0U) = 4.0F;
  rhs32(1U) = -3.0F;
  rhs32(2U) = 8.0F;

  float dot32{};
  ASSERT_TRUE(ksj::linalg::detail::intel::dot(ksj::array::as_const_view(lhs32.view()),
                                              ksj::array::as_const_view(rhs32.view()), dot32));
  EXPECT_FLOAT_EQ(14.0F, dot32);

  float dotu32{};
  ASSERT_TRUE(ksj::linalg::detail::intel::dotu(ksj::array::as_const_view(lhs32.view()),
                                               ksj::array::as_const_view(rhs32.view()), dotu32));
  EXPECT_FLOAT_EQ(dot32, dotu32);

  auto lhs64 = ksj::array::make_pooled_vector<double>(3U);
  auto rhs64 = ksj::array::make_pooled_vector<double>(3U);
  lhs64(0U) = 1.0;
  lhs64(1U) = 2.0;
  lhs64(2U) = 3.0;
  rhs64(0U) = 4.0;
  rhs64(1U) = 5.0;
  rhs64(2U) = 6.0;

  double dot64{};
  ASSERT_TRUE(ksj::linalg::detail::intel::dot(ksj::array::as_const_view(lhs64.view()),
                                              ksj::array::as_const_view(rhs64.view()), dot64));
  EXPECT_DOUBLE_EQ(32.0, dot64);

  double dotu64{};
  ASSERT_TRUE(ksj::linalg::detail::intel::dotu(ksj::array::as_const_view(lhs64.view()),
                                               ksj::array::as_const_view(rhs64.view()), dotu64));
  EXPECT_DOUBLE_EQ(dot64, dotu64);
}

TEST(KSpaceJetLinalg, IntelBlas1SupportsCopyScaleAndAxpy) {
  auto input = ksj::array::make_pooled_vector<float>(4U);
  auto copied = ksj::array::make_pooled_vector<float>(4U);
  auto scaled = ksj::array::make_pooled_vector<float>(4U);
  auto y = ksj::array::make_pooled_vector<float>(4U);
  auto axpy = ksj::array::make_pooled_vector<float>(4U);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    input(index) = static_cast<float>(index + 1U);
    y(index) = static_cast<float>(10U + index);
  }

  ASSERT_TRUE(ksj::linalg::detail::intel::copy(ksj::array::as_const_view(input.view()), copied.view()));
  ASSERT_TRUE(ksj::linalg::detail::intel::scale(ksj::array::as_const_view(input.view()), 2.5F, scaled.view()));
  ASSERT_TRUE(ksj::linalg::detail::intel::axpy(0.5F, ksj::array::as_const_view(input.view()),
                                               ksj::array::as_const_view(y.view()), axpy.view()));

  for (std::size_t index = 0U; index < input.size(); ++index) {
    EXPECT_FLOAT_EQ(input(index), copied(index));
    EXPECT_FLOAT_EQ(input(index) * 2.5F, scaled(index));
    EXPECT_FLOAT_EQ(y(index) + input(index) * 0.5F, axpy(index));
  }

  auto cx = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto cy = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto cout = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  cx(0U) = {1.0F, 2.0F};
  cx(1U) = {-3.0F, 0.5F};
  cy(0U) = {10.0F, -1.0F};
  cy(1U) = {2.0F, 4.0F};

  ASSERT_TRUE(ksj::linalg::detail::intel::axpy(ksj::base::cf32{0.25F, -0.5F}, ksj::array::as_const_view(cx.view()),
                                               ksj::array::as_const_view(cy.view()), cout.view()));
  for (std::size_t index = 0U; index < cx.size(); ++index) {
    const auto expected = cy(index) + ksj::base::cf32{0.25F, -0.5F} * cx(index);
    EXPECT_NEAR(expected.real(), cout(index).real(), kCf32Tolerance);
    EXPECT_NEAR(expected.imag(), cout(index).imag(), kCf32Tolerance);
  }
}

TEST(KSpaceJetLinalg, DotSupportsRealAndComplex) {
  auto x = ksj::array::make_pooled_vector<double>(3);
  auto y = ksj::array::make_pooled_vector<double>(3);
  as_eigen(x) << 1.0, 2.0, 3.0;
  as_eigen(y) << 4.0, 5.0, 6.0;
  EXPECT_DOUBLE_EQ(32.0, ksj::linalg::dot(x, y));

  auto cx = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  auto cy = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  as_eigen(cx) << ksj::base::cf64{1.0, 2.0}, ksj::base::cf64{3.0, -1.0};
  as_eigen(cy) << ksj::base::cf64{2.0, 1.0}, ksj::base::cf64{0.5, 4.0};

  const auto value = ksj::linalg::dot(cx, cy);
  EXPECT_NEAR(1.5, value.real(), 1.0e-12);
  EXPECT_NEAR(9.5, value.imag(), 1.0e-12);

  const auto unconjugated = ksj::linalg::dotu(cx, cy);
  EXPECT_NEAR(5.5, unconjugated.real(), 1.0e-12);
  EXPECT_NEAR(16.5, unconjugated.imag(), 1.0e-12);
  EXPECT_NEAR(std::sqrt(15.0), ksj::linalg::norm_l2(cx), 1.0e-12);

  std::array<double, 5> strided_x{1.0, -1.0, 2.0, -1.0, 3.0};
  std::array<double, 5> strided_y{4.0, -1.0, 5.0, -1.0, 6.0};
  EXPECT_DOUBLE_EQ(32.0, ksj::linalg::dot(ksj::array::VectorView<const double>(strided_x.data(), strided_x.size())
                                            .subview(ksj::array::slice(0U, strided_x.size(), 2U)),
                                          ksj::array::VectorView<const double>(strided_y.data(), strided_y.size())
                                            .subview(ksj::array::slice(0U, strided_y.size(), 2U))));

  std::array<ksj::base::cf32, 4> complex_x{
    ksj::base::cf32{1.0F, 2.0F},
    ksj::base::cf32{-1.0F, -1.0F},
    ksj::base::cf32{3.0F, -1.0F},
    ksj::base::cf32{-1.0F, -1.0F},
  };
  std::array<ksj::base::cf32, 4> complex_y{
    ksj::base::cf32{2.0F, 1.0F},
    ksj::base::cf32{-1.0F, -1.0F},
    ksj::base::cf32{0.5F, 4.0F},
    ksj::base::cf32{-1.0F, -1.0F},
  };
  const auto complex_strided =
    ksj::linalg::dot(ksj::array::VectorView<const ksj::base::cf32>(complex_x.data(), complex_x.size())
                       .subview(ksj::array::slice(0U, complex_x.size(), 2U)),
                     ksj::array::VectorView<const ksj::base::cf32>(complex_y.data(), complex_y.size())
                       .subview(ksj::array::slice(0U, complex_y.size(), 2U)));
  EXPECT_NEAR(1.5F, complex_strided.real(), 1.0e-5F);
  EXPECT_NEAR(9.5F, complex_strided.imag(), 1.0e-5F);
}

TEST(KSpaceJetLinalg, IntelDotuSupportsComplexIppBackend) {
  auto lhs32 = ksj::array::make_pooled_vector<ksj::base::cf32>(3U);
  auto rhs32 = ksj::array::make_pooled_vector<ksj::base::cf32>(3U);
  lhs32(0U) = {1.0F, 2.0F};
  lhs32(1U) = {-3.0F, 0.5F};
  lhs32(2U) = {0.25F, -1.0F};
  rhs32(0U) = {2.0F, -1.0F};
  rhs32(1U) = {0.25F, 4.0F};
  rhs32(2U) = {-2.0F, 0.75F};

  ksj::base::cf32 output32{};
  ASSERT_TRUE(ksj::linalg::detail::intel::dotu(ksj::array::as_const_view(lhs32.view()),
                                               ksj::array::as_const_view(rhs32.view()), output32));
  ksj::base::cf32 expected32{};
  for (std::size_t index = 0U; index < lhs32.size(); ++index) {
    expected32 += lhs32(index) * rhs32(index);
  }
  EXPECT_NEAR(expected32.real(), output32.real(), kCf32Tolerance);
  EXPECT_NEAR(expected32.imag(), output32.imag(), kCf32Tolerance);

  auto lhs64 = ksj::array::make_pooled_vector<ksj::base::cf64>(2U);
  auto rhs64 = ksj::array::make_pooled_vector<ksj::base::cf64>(2U);
  lhs64(0U) = {1.0, 2.0};
  lhs64(1U) = {3.0, -1.0};
  rhs64(0U) = {2.0, 1.0};
  rhs64(1U) = {0.5, 4.0};

  ksj::base::cf64 output64{};
  ASSERT_TRUE(ksj::linalg::detail::intel::dotu(ksj::array::as_const_view(lhs64.view()),
                                               ksj::array::as_const_view(rhs64.view()), output64));
  ksj::base::cf64 expected64{};
  for (std::size_t index = 0U; index < lhs64.size(); ++index) {
    expected64 += lhs64(index) * rhs64(index);
  }
  EXPECT_NEAR(expected64.real(), output64.real(), 1.0e-12);
  EXPECT_NEAR(expected64.imag(), output64.imag(), 1.0e-12);
}

TEST(KSpaceJetLinalg, TransposeReturnsPooledMatrix) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  as_eigen(matrix) << 1, 2, 3, 4, 5, 6;

  auto output = ksj::linalg::transpose(matrix);

  EXPECT_EQ(3U, output.rows());
  EXPECT_EQ(2U, output.cols());
  EXPECT_EQ(1, output(0, 0));
  EXPECT_EQ(2, output(1, 0));
  EXPECT_EQ(3, output(2, 0));
  EXPECT_EQ(4, output(0, 1));
  EXPECT_EQ(5, output(1, 1));
  EXPECT_EQ(6, output(2, 1));
}

TEST(KSpaceJetLinalg, HermitianGramSupportsStridedRowMajorMatrixView) {
  std::array<ksj::base::cf32, 9> matrix_storage{
    ksj::base::cf32{1.0F, 1.0F}, ksj::base::cf32{3.0F, 0.0F},  ksj::base::cf32{},
    ksj::base::cf32{2.0F, 0.0F}, ksj::base::cf32{4.0F, -1.0F}, ksj::base::cf32{},
    ksj::base::cf32{0.0F, 1.0F}, ksj::base::cf32{5.0F, 2.0F},  ksj::base::cf32{},
  };
  std::array<ksj::base::cf32, 4> output_storage{};

  const auto input_view = ksj::array::MatrixView<const ksj::base::cf32>(matrix_storage.data(), 3U, 3U)
                            .subview(ksj::array::_, ksj::array::slice(0U, 2U));
  const auto output_view = ksj::array::MatrixView<ksj::base::cf32>(output_storage.data(), 2U, 2U);
  EXPECT_FALSE(ksj::linalg::detail::intel::hermitian_gram(input_view, output_view, 1.0F));

  // The public operation must fall back to the stride-aware implementation.
  ksj::linalg::hermitian_gram(input_view, output_view);

  EXPECT_FLOAT_EQ(7.0F, output_storage[0].real());
  EXPECT_FLOAT_EQ(0.0F, output_storage[0].imag());
  EXPECT_FLOAT_EQ(55.0F, output_storage[3].real());
  EXPECT_FLOAT_EQ(0.0F, output_storage[3].imag());
  EXPECT_FLOAT_EQ(13.0F, output_storage[1].real());
  EXPECT_FLOAT_EQ(-10.0F, output_storage[1].imag());
  EXPECT_FLOAT_EQ(13.0F, output_storage[2].real());
  EXPECT_FLOAT_EQ(10.0F, output_storage[2].imag());

  const auto diagonal_sum =
    ksj::linalg::diagonal_abs_sum(ksj::array::MatrixView<const ksj::base::cf32>(output_storage.data(), 2U, 2U));
  EXPECT_FLOAT_EQ(62.0F, diagonal_sum);

  ksj::linalg::add_to_diagonal(ksj::array::MatrixView<ksj::base::cf32>(output_storage.data(), 2U, 2U),
                               ksj::base::cf32{0.5F, 0.0F});
  EXPECT_FLOAT_EQ(7.5F, output_storage[0].real());
  EXPECT_FLOAT_EQ(55.5F, output_storage[3].real());
}

TEST(KSpaceJetLinalg, IntelHermitianGramMatchesReferenceForContiguousComplexMatrixAndScale) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf32>(4U, 3U);
  input(0U, 0U) = {1.0F, 2.0F};
  input(0U, 1U) = {-0.5F, 1.0F};
  input(0U, 2U) = {2.0F, -0.25F};
  input(1U, 0U) = {0.25F, -1.5F};
  input(1U, 1U) = {3.0F, 0.5F};
  input(1U, 2U) = {-2.0F, 1.0F};
  input(2U, 0U) = {2.5F, 0.0F};
  input(2U, 1U) = {1.25F, -0.75F};
  input(2U, 2U) = {0.5F, 2.0F};
  input(3U, 0U) = {-1.0F, 0.5F};
  input(3U, 1U) = {0.0F, -2.0F};
  input(3U, 2U) = {1.5F, 0.75F};

  constexpr float scale = 0.375F;
  auto reference = ksj::array::make_pooled_matrix<ksj::base::cf32>(3U, 3U);
  ksj::linalg::detail::eigen::hermitian_gram(ksj::array::as_const_view(input.view()), reference.view(), scale);

  auto direct_intel = ksj::array::make_pooled_matrix<ksj::base::cf32>(3U, 3U);
  ASSERT_TRUE(
    ksj::linalg::detail::intel::hermitian_gram(ksj::array::as_const_view(input.view()), direct_intel.view(), scale));

  // Armadillo 8 used a column-major CHERK for A.t() * A, then mirrored its
  // upper triangle.  Verify the row-major CBLAS call keeps that exact finite-
  // precision result, not merely the same mathematical result.
  std::array<ksj::base::cf32, 12U> column_major_input{};
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      column_major_input[row + input.rows() * col] = input(row, col);
    }
  }
  std::array<ksj::base::cf32, 9U> column_major_output{};
  cblas_cherk(CblasColMajor, CblasUpper, CblasConjTrans, 3, 4, scale, test_mkl_complex_cast(column_major_input.data()),
              4, 0.0F, test_mkl_complex_cast(column_major_output.data()), 3);
  for (std::size_t row = 0U; row < 3U; ++row) {
    column_major_output[row + 3U * row] = {column_major_output[row + 3U * row].real(), 0.0F};
    for (std::size_t col = row + 1U; col < 3U; ++col) {
      column_major_output[col + 3U * row] = std::conj(column_major_output[row + 3U * col]);
    }
  }
  for (std::size_t row = 0U; row < 3U; ++row) {
    for (std::size_t col = 0U; col < 3U; ++col) {
      EXPECT_EQ(column_major_output[row + 3U * col], direct_intel(row, col));
    }
  }

  auto dispatched = ksj::array::make_pooled_matrix<ksj::base::cf32>(3U, 3U);
  ksj::linalg::hermitian_gram(ksj::array::as_const_view(input.view()), dispatched.view(), scale);

  for (std::size_t row = 0U; row < reference.rows(); ++row) {
    for (std::size_t col = 0U; col < reference.cols(); ++col) {
      EXPECT_NEAR(reference(row, col).real(), direct_intel(row, col).real(), 2.0e-5F);
      EXPECT_NEAR(reference(row, col).imag(), direct_intel(row, col).imag(), 2.0e-5F);
      EXPECT_NEAR(direct_intel(row, col).real(), dispatched(row, col).real(), 1.0e-6F);
      EXPECT_NEAR(direct_intel(row, col).imag(), dispatched(row, col).imag(), 1.0e-6F);
    }
  }
  EXPECT_NEAR(0.0F, dispatched(0U, 0U).imag(), 1.0e-6F);
  EXPECT_NEAR(dispatched(0U, 2U).real(), dispatched(2U, 0U).real(), 1.0e-6F);
  EXPECT_NEAR(dispatched(0U, 2U).imag(), -dispatched(2U, 0U).imag(), 1.0e-6F);
}

TEST(KSpaceJetLinalg, HermitianGramValidatesDimensions) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf32>(3U, 2U);
  auto wrong_output = ksj::array::make_pooled_matrix<ksj::base::cf32>(3U, 3U);
  EXPECT_THROW(ksj::linalg::hermitian_gram(ksj::array::as_const_view(input.view()), wrong_output.view()),
               std::invalid_argument);

  std::array<ksj::base::cf32, 1U> unused{};
  const auto empty_input = ksj::array::MatrixView<const ksj::base::cf32>(unused.data(), 0U, 0U);
  const auto empty_output = ksj::array::MatrixView<ksj::base::cf32>(unused.data(), 0U, 0U);
  EXPECT_THROW(ksj::linalg::hermitian_gram(empty_input, empty_output), std::invalid_argument);
}

TEST(KSpaceJetLinalg, HermitianGramDispatchPolicyHandlesThresholdsWithoutOverflow) {
  EXPECT_FALSE(ksj::linalg::detail::hermitian_gram_ops_at_least(0U, 3U, 1U));
  EXPECT_FALSE(ksj::linalg::detail::hermitian_gram_ops_at_least(2U, 3U, 37U));
  EXPECT_TRUE(ksj::linalg::detail::hermitian_gram_ops_at_least(2U, 3U, 36U));
  EXPECT_TRUE(ksj::linalg::detail::hermitian_gram_ops_at_least(std::numeric_limits<std::size_t>::max(),
                                                               std::numeric_limits<std::size_t>::max(),
                                                               std::numeric_limits<std::size_t>::max()));
}

TEST(KSpaceJetLinalg, SupportsBlasStyleVectorOps) {
  auto x = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  auto y = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  x(0) = {1.0F, 2.0F};
  x(1) = {3.0F, 4.0F};
  y(0) = {10.0F, 0.0F};
  y(1) = {20.0F, 0.0F};

  const auto scaled = ksj::linalg::scale(x, 2.0F);
  EXPECT_FLOAT_EQ(2.0F, scaled(0).real());
  EXPECT_FLOAT_EQ(8.0F, scaled(1).imag());

  auto output_scaled = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ksj::linalg::scale(x, output_scaled, 2.0F);
  EXPECT_FLOAT_EQ(2.0F, output_scaled(0).real());
  EXPECT_FLOAT_EQ(8.0F, output_scaled(1).imag());

  const auto combined = ksj::linalg::axpy(0.5F, x, y);
  EXPECT_FLOAT_EQ(10.5F, combined(0).real());
  EXPECT_FLOAT_EQ(1.0F, combined(0).imag());
  EXPECT_FLOAT_EQ(21.5F, combined(1).real());
  EXPECT_FLOAT_EQ(2.0F, combined(1).imag());

  auto output_combined = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ksj::linalg::axpy(0.5F, x, y, output_combined);
  EXPECT_FLOAT_EQ(10.5F, output_combined(0).real());
  EXPECT_FLOAT_EQ(1.0F, output_combined(0).imag());
  EXPECT_FLOAT_EQ(21.5F, output_combined(1).real());
  EXPECT_FLOAT_EQ(2.0F, output_combined(1).imag());
}

TEST(KSpaceJetLinalg, SolvesInvertsAndComputesDeterminant) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  matrix(0, 0) = 4.0;
  matrix(1, 0) = 7.0;
  matrix(0, 1) = 2.0;
  matrix(1, 1) = 6.0;

  EXPECT_NEAR(10.0, ksj::linalg::determinant(matrix), 1.0e-12);

  const auto inverse = ksj::linalg::inverse(matrix);
  EXPECT_NEAR(0.6, inverse(0, 0), 1.0e-12);
  EXPECT_NEAR(-0.7, inverse(1, 0), 1.0e-12);
  EXPECT_NEAR(-0.2, inverse(0, 1), 1.0e-12);
  EXPECT_NEAR(0.4, inverse(1, 1), 1.0e-12);

  auto inverse_output = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::linalg::inverse(ksj::array::as_const_view(matrix.view()), inverse_output.view());
  EXPECT_NEAR(0.6, inverse_output(0, 0), 1.0e-12);
  EXPECT_NEAR(-0.7, inverse_output(1, 0), 1.0e-12);
  EXPECT_NEAR(-0.2, inverse_output(0, 1), 1.0e-12);
  EXPECT_NEAR(0.4, inverse_output(1, 1), 1.0e-12);

  auto rhs = ksj::array::make_pooled_vector<double>(2);
  rhs(0) = 6.0;
  rhs(1) = 13.0;
  const auto solution = ksj::linalg::solve(matrix, rhs);
  EXPECT_NEAR(1.0, solution(0), 1.0e-12);
  EXPECT_NEAR(1.0, solution(1), 1.0e-12);

  auto output_solution = ksj::array::make_pooled_vector<double>(2);
  ksj::linalg::solve(matrix, rhs, output_solution);
  EXPECT_NEAR(1.0, output_solution(0), 1.0e-12);
  EXPECT_NEAR(1.0, output_solution(1), 1.0e-12);
}

TEST(KSpaceJetLinalg, ComputesPseudoInverseFromViewAndPooledMatrix) {
  auto matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  matrix(0, 0) = 1.0;
  matrix(1, 0) = 1.0;
  matrix(2, 0) = 1.0;
  matrix(0, 1) = 0.0;
  matrix(1, 1) = 1.0;
  matrix(2, 1) = 2.0;

  auto output = ksj::array::make_pooled_matrix<double>(2, 3);
  ksj::linalg::pseudo_inverse(ksj::array::as_const_view(matrix.view()), output.view());

  EXPECT_EQ(2U, output.rows());
  EXPECT_EQ(3U, output.cols());
  const auto identity = ksj::linalg::matmul(output, matrix);
  EXPECT_NEAR(1.0, identity(0, 0), 1.0e-12);
  EXPECT_NEAR(0.0, identity(0, 1), 1.0e-12);
  EXPECT_NEAR(0.0, identity(1, 0), 1.0e-12);
  EXPECT_NEAR(1.0, identity(1, 1), 1.0e-12);

  const auto returned = ksj::linalg::pseudo_inverse(matrix);
  EXPECT_EQ(2U, returned.rows());
  EXPECT_EQ(3U, returned.cols());
  EXPECT_NEAR(0.0, (as_eigen(returned) - as_eigen(output)).norm(), 1.0e-12);

  auto wrong_shape = ksj::array::make_pooled_matrix<double>(3, 2);
  EXPECT_THROW(ksj::linalg::pseudo_inverse(ksj::array::as_const_view(matrix.view()), wrong_shape.view()),
               std::invalid_argument);
}

TEST(KSpaceJetLinalg, ComputesRankDeficientPseudoInverse) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  matrix(0, 0) = 1.0;
  matrix(0, 1) = 2.0;
  matrix(1, 0) = 2.0;
  matrix(1, 1) = 4.0;

  const auto pseudo_inverse = ksj::linalg::pseudo_inverse(matrix);
  const auto projected = ksj::linalg::matmul(ksj::linalg::matmul(matrix, pseudo_inverse), matrix);

  EXPECT_NEAR(matrix(0, 0), projected(0, 0), 1.0e-12);
  EXPECT_NEAR(matrix(0, 1), projected(0, 1), 1.0e-12);
  EXPECT_NEAR(matrix(1, 0), projected(1, 0), 1.0e-12);
  EXPECT_NEAR(matrix(1, 1), projected(1, 1), 1.0e-12);
}

TEST(KSpaceJetLinalg, ComputesComplexPseudoInverse) {
  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  matrix(0, 0) = ksj::base::cf32{1.0, 1.0};
  matrix(0, 1) = ksj::base::cf32{0.0, 2.0};
  matrix(1, 0) = ksj::base::cf32{2.0, -1.0};
  matrix(1, 1) = ksj::base::cf32{3.0, 0.5};

  const auto pseudo_inverse = ksj::linalg::pseudo_inverse(matrix);
  const auto identity = ksj::linalg::matmul(pseudo_inverse, matrix);

  EXPECT_NEAR(1.0F, identity(0, 0).real(), kCf32Tolerance);
  EXPECT_NEAR(0.0F, identity(0, 0).imag(), kCf32Tolerance);
  EXPECT_NEAR(0.0F, std::abs(identity(0, 1)), kCf32Tolerance);
  EXPECT_NEAR(0.0F, std::abs(identity(1, 0)), kCf32Tolerance);
  EXPECT_NEAR(1.0F, identity(1, 1).real(), kCf32Tolerance);
  EXPECT_NEAR(0.0F, identity(1, 1).imag(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, SolvesMatrixRightHandSides) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  matrix(0, 0) = 4.0;
  matrix(1, 0) = 7.0;
  matrix(0, 1) = 2.0;
  matrix(1, 1) = 6.0;

  auto expected = ksj::array::make_pooled_matrix<double>(2, 2);
  expected(0, 0) = 1.0;
  expected(1, 0) = 1.0;
  expected(0, 1) = 2.0;
  expected(1, 1) = 3.0;
  auto rhs = ksj::linalg::matmul(matrix, expected);

  const auto solution = ksj::linalg::solve(matrix, rhs);

  ASSERT_EQ(2U, solution.rows());
  ASSERT_EQ(2U, solution.cols());
  EXPECT_NEAR(expected(0, 0), solution(0, 0), 1.0e-12);
  EXPECT_NEAR(expected(1, 0), solution(1, 0), 1.0e-12);
  EXPECT_NEAR(expected(0, 1), solution(0, 1), 1.0e-12);
  EXPECT_NEAR(expected(1, 1), solution(1, 1), 1.0e-12);

  auto output_solution = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::linalg::solve(matrix, rhs, output_solution);
  EXPECT_NEAR(expected(0, 0), output_solution(0, 0), 1.0e-12);
  EXPECT_NEAR(expected(1, 0), output_solution(1, 0), 1.0e-12);
  EXPECT_NEAR(expected(0, 1), output_solution(0, 1), 1.0e-12);
  EXPECT_NEAR(expected(1, 1), output_solution(1, 1), 1.0e-12);
}

TEST(KSpaceJetLinalg, ComputesWhiteningMatrixFromCovariance) {
  auto diagonal_covariance = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(diagonal_covariance).setZero();
  diagonal_covariance(0, 0) = 4.0;
  diagonal_covariance(1, 1) = 9.0;
  const auto whitening = ksj::linalg::whitening_matrix_from_covariance(diagonal_covariance);
  EXPECT_NEAR(0.5, whitening(0, 0), 1.0e-12);
  EXPECT_NEAR(1.0 / 3.0, whitening(1, 1), 1.0e-12);
}

TEST(KSpaceJetLinalg, WhitensSamplesWithRightTransform) {
  auto samples = ksj::array::make_pooled_matrix<double>(2, 2);
  samples(0, 0) = 2.0;
  samples(1, 0) = 4.0;
  samples(0, 1) = 9.0;
  samples(1, 1) = 12.0;
  auto whitening = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(whitening).setZero();
  whitening(0, 0) = 0.5;
  whitening(1, 1) = 1.0 / 3.0;

  const auto output = ksj::linalg::whiten_samples(samples, whitening);

  EXPECT_NEAR(1.0, output(0, 0), 1.0e-12);
  EXPECT_NEAR(2.0, output(1, 0), 1.0e-12);
  EXPECT_NEAR(3.0, output(0, 1), 1.0e-12);
  EXPECT_NEAR(4.0, output(1, 1), 1.0e-12);
}

TEST(KSpaceJetLinalg, CalibratesAndAppliesCholeskyPrewhitenMatrix) {
  auto channel0 = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  auto channel1 = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  channel0(0) = {1.0F, 0.0F};
  channel0(1) = {-1.0F, 0.0F};
  channel0(2) = {1.0F, 0.0F};
  channel0(3) = {-1.0F, 0.0F};
  channel1(0) = {2.0F, 0.0F};
  channel1(1) = {2.0F, 0.0F};
  channel1(2) = {-2.0F, 0.0F};
  channel1(3) = {-2.0F, 0.0F};

  std::vector<ksj::array::VectorView<ksj::base::cf32>> channels{channel0.view(), channel1.view()};
  auto inverse_cholesky = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  auto scale_factors = std::vector<float>(2);

  const auto calibration = ksj::linalg::cholesky_prewhiten_calibration(
    std::span<ksj::array::VectorView<ksj::base::cf32>>(channels.data(), channels.size()), inverse_cholesky.view(),
    std::span<float>(scale_factors.data(), scale_factors.size()), channel0.size());

  ASSERT_EQ(ksj::linalg::PrewhitenCalibrationStatus::success, calibration.status);
  EXPECT_NEAR(2.5F, calibration.sigma_average, 1.0e-6F);
  EXPECT_NEAR(1.0F / std::sqrt(0.4F), inverse_cholesky(0, 0).real(), 1.0e-6F);
  EXPECT_NEAR(0.0F, inverse_cholesky(0, 1).real(), 1.0e-6F);
  EXPECT_NEAR(0.0F, inverse_cholesky(1, 0).real(), 1.0e-6F);
  EXPECT_NEAR(1.0F / std::sqrt(1.6F), inverse_cholesky(1, 1).real(), 1.0e-6F);
  EXPECT_NEAR(inverse_cholesky(0, 0).real(), scale_factors[0], 1.0e-6F);
  EXPECT_NEAR(inverse_cholesky(1, 1).real(), scale_factors[1], 1.0e-6F);

  as_eigen(inverse_cholesky).setZero();
  inverse_cholesky(0, 0) = {1.0F, 0.0F};
  inverse_cholesky(1, 0) = {0.5F, 0.0F};
  inverse_cholesky(1, 1) = {2.0F, 0.0F};
  channel0(0) = {2.0F, 0.0F};
  channel1(0) = {3.0F, 0.0F};
  ASSERT_TRUE(ksj::linalg::apply_cholesky_prewhiten(
    inverse_cholesky.view(), std::span<ksj::array::VectorView<ksj::base::cf32>>(channels.data(), channels.size()), 1U));
  EXPECT_NEAR(2.0F, channel0(0).real(), 1.0e-6F);
  EXPECT_NEAR(7.0F, channel1(0).real(), 1.0e-6F);
}

TEST(KSpaceJetLinalg, AppliesCholeskyPrewhitenToStridedChannelViews) {
  auto channel0 = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  auto channel1 = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  channel0(0) = {2.0F, 0.0F};
  channel0(1) = {-100.0F, 0.0F};
  channel0(2) = {4.0F, 0.0F};
  channel0(3) = {-200.0F, 0.0F};
  channel1(0) = {3.0F, 0.0F};
  channel1(1) = {-300.0F, 0.0F};
  channel1(2) = {5.0F, 0.0F};
  channel1(3) = {-400.0F, 0.0F};

  std::array<ksj::array::VectorView<ksj::base::cf32>, 2> channels{
    ksj::array::VectorView<ksj::base::cf32>(channel0.data(), channel0.size())
      .subview(ksj::array::slice(0U, channel0.size(), 2U)),
    ksj::array::VectorView<ksj::base::cf32>(channel1.data(), channel1.size())
      .subview(ksj::array::slice(0U, channel1.size(), 2U)),
  };
  auto inverse_cholesky = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  as_eigen(inverse_cholesky).setZero();
  inverse_cholesky(0, 0) = {1.0F, 0.0F};
  inverse_cholesky(1, 0) = {0.5F, 0.0F};
  inverse_cholesky(1, 1) = {2.0F, 0.0F};

  ASSERT_TRUE(ksj::linalg::apply_cholesky_prewhiten(
    inverse_cholesky.view(), std::span<ksj::array::VectorView<ksj::base::cf32>>(channels.data(), channels.size()), 2U));
  EXPECT_NEAR(2.0F, channel0(0).real(), 1.0e-6F);
  EXPECT_NEAR(4.0F, channel0(2).real(), 1.0e-6F);
  EXPECT_NEAR(7.0F, channel1(0).real(), 1.0e-6F);
  EXPECT_NEAR(12.0F, channel1(2).real(), 1.0e-6F);
  EXPECT_NEAR(-100.0F, channel0(1).real(), 1.0e-6F);
  EXPECT_NEAR(-400.0F, channel1(3).real(), 1.0e-6F);
}

TEST(KSpaceJetLinalg, SupportsCholeskyQrSvdAndSmallSolve) {
  auto spd = ksj::array::make_pooled_matrix<double>(2, 2);
  spd(0, 0) = 4.0;
  spd(1, 0) = 2.0;
  spd(0, 1) = 2.0;
  spd(1, 1) = 3.0;

  const auto lower = ksj::linalg::cholesky_lower(spd);
  EXPECT_NEAR(2.0, lower(0, 0), 1.0e-12);
  EXPECT_NEAR(1.0, lower(1, 0), 1.0e-12);
  EXPECT_NEAR(std::sqrt(2.0), lower(1, 1), 1.0e-12);

  auto rhs = ksj::array::make_pooled_vector<double>(2);
  rhs(0) = 6.0;
  rhs(1) = 5.0;
  const auto cholesky_solution = ksj::linalg::solve_cholesky(spd, rhs);
  EXPECT_NEAR(1.0, cholesky_solution(0), 1.0e-12);
  EXPECT_NEAR(1.0, cholesky_solution(1), 1.0e-12);

  auto expected_matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  expected_matrix(0, 0) = 1.0;
  expected_matrix(1, 0) = 1.0;
  expected_matrix(0, 1) = 2.0;
  expected_matrix(1, 1) = 3.0;
  const auto cholesky_rhs_matrix = ksj::linalg::matmul(spd, expected_matrix);
  const auto cholesky_matrix_solution = ksj::linalg::solve_cholesky(spd, cholesky_rhs_matrix);
  EXPECT_NEAR(1.0, cholesky_matrix_solution(0, 0), 1.0e-12);
  EXPECT_NEAR(1.0, cholesky_matrix_solution(1, 0), 1.0e-12);
  EXPECT_NEAR(2.0, cholesky_matrix_solution(0, 1), 1.0e-12);
  EXPECT_NEAR(3.0, cholesky_matrix_solution(1, 1), 1.0e-12);

  auto qr_matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  qr_matrix(0, 0) = 1.0;
  qr_matrix(1, 0) = 1.0;
  qr_matrix(2, 0) = 1.0;
  qr_matrix(0, 1) = 0.0;
  qr_matrix(1, 1) = 1.0;
  qr_matrix(2, 1) = 2.0;
  auto qr_rhs = ksj::array::make_pooled_vector<double>(3);
  qr_rhs(0) = 1.0;
  qr_rhs(1) = 3.0;
  qr_rhs(2) = 5.0;
  const auto qr_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs);
  EXPECT_NEAR(1.0, qr_solution(0), 1.0e-12);
  EXPECT_NEAR(2.0, qr_solution(1), 1.0e-12);

  auto qr_expected = ksj::array::make_pooled_matrix<double>(2, 2);
  qr_expected(0, 0) = 1.0;
  qr_expected(1, 0) = 2.0;
  qr_expected(0, 1) = 2.0;
  qr_expected(1, 1) = 3.0;
  const auto qr_rhs_matrix = ksj::linalg::matmul(qr_matrix, qr_expected);
  const auto qr_matrix_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs_matrix);
  EXPECT_NEAR(1.0, qr_matrix_solution(0, 0), 1.0e-12);
  EXPECT_NEAR(2.0, qr_matrix_solution(1, 0), 1.0e-12);
  EXPECT_NEAR(2.0, qr_matrix_solution(0, 1), 1.0e-12);
  EXPECT_NEAR(3.0, qr_matrix_solution(1, 1), 1.0e-12);

  auto diagonal = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(diagonal).setZero();
  diagonal(0, 0) = 3.0;
  diagonal(1, 1) = 2.0;
  const auto values = ksj::linalg::singular_values(diagonal);
  ASSERT_EQ(2U, values.size());
  EXPECT_NEAR(3.0, values(0), 1.0e-12);
  EXPECT_NEAR(2.0, values(1), 1.0e-12);

  const auto small_solution = ksj::linalg::solve_small(spd, rhs);
  EXPECT_NEAR(1.0, small_solution(0), 1.0e-12);
  EXPECT_NEAR(1.0, small_solution(1), 1.0e-12);
}

TEST(KSpaceJetLinalg, SupportsFullSvdAndEigenDecompositions) {
  auto diagonal = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(diagonal).setZero();
  diagonal(0, 0) = 3.0;
  diagonal(1, 1) = 2.0;

  const auto decomposition = ksj::linalg::svd(diagonal, ksj::linalg::SvdMode::full);

  ASSERT_EQ(2U, decomposition.u.rows());
  ASSERT_EQ(2U, decomposition.u.cols());
  ASSERT_EQ(2U, decomposition.singular_values.size());
  ASSERT_EQ(2U, decomposition.v_adjoint.rows());
  ASSERT_EQ(2U, decomposition.v_adjoint.cols());
  EXPECT_NEAR(3.0, decomposition.singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, decomposition.singular_values(1), 1.0e-12);
  EXPECT_NEAR(3.0,
              (as_eigen(decomposition.u) * as_eigen(decomposition.singular_values).asDiagonal() *
               as_eigen(decomposition.v_adjoint))(0, 0),
              1.0e-12);

  auto in_place_diagonal = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(in_place_diagonal).setZero();
  in_place_diagonal(0, 0) = 3.0;
  in_place_diagonal(1, 1) = 2.0;
  const auto in_place_decomposition = ksj::linalg::svd_in_place(in_place_diagonal, ksj::linalg::SvdMode::full);
  ASSERT_EQ(2U, in_place_decomposition.u.rows());
  ASSERT_EQ(2U, in_place_decomposition.u.cols());
  ASSERT_EQ(2U, in_place_decomposition.singular_values.size());
  ASSERT_EQ(2U, in_place_decomposition.v_adjoint.rows());
  ASSERT_EQ(2U, in_place_decomposition.v_adjoint.cols());
  EXPECT_NEAR(3.0, in_place_decomposition.singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, in_place_decomposition.singular_values(1), 1.0e-12);

  auto workspace_u = ksj::array::make_pooled_matrix<double>(2, 2);
  auto workspace_singular_values = ksj::array::make_pooled_vector<double>(2);
  auto workspace_v_adjoint = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::linalg::SvdWorkspace<double> svd_workspace;
  ASSERT_TRUE(
    ksj::linalg::full_svd(diagonal, workspace_u, workspace_singular_values, workspace_v_adjoint, svd_workspace));
  EXPECT_NEAR(3.0, workspace_singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, workspace_singular_values(1), 1.0e-12);
  const auto workspace_reconstructed =
    as_eigen(workspace_u) * as_eigen(workspace_singular_values).asDiagonal() * as_eigen(workspace_v_adjoint);
  EXPECT_NEAR(0.0, (workspace_reconstructed - as_eigen(diagonal)).norm(), 1.0e-12);
  EXPECT_EQ(2U, svd_workspace.matrix_work.rows());
  EXPECT_EQ(2U, svd_workspace.matrix_work.cols());

  auto wide = ksj::array::make_pooled_matrix<double>(2, 3);
  as_eigen(wide).setZero();
  wide(0, 0) = 4.0;
  wide(1, 1) = 2.0;
  wide(0, 2) = 0.25;

  auto left_vectors = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::linalg::left_singular_vectors(ksj::array::as_const_view(wide.view()), left_vectors.view());
  ASSERT_EQ(2U, left_vectors.rows());
  ASSERT_EQ(2U, left_vectors.cols());
  const auto left_identity = as_eigen(left_vectors).adjoint() * as_eigen(left_vectors);
  EXPECT_NEAR(1.0, left_identity(0, 0), 1.0e-12);
  EXPECT_NEAR(0.0, left_identity(0, 1), 1.0e-12);
  EXPECT_NEAR(0.0, left_identity(1, 0), 1.0e-12);
  EXPECT_NEAR(1.0, left_identity(1, 1), 1.0e-12);

  const auto wide_svd = ksj::linalg::svd(wide);
  EXPECT_NEAR(0.0, (as_eigen(wide_svd.u).cwiseAbs() - as_eigen(left_vectors).cwiseAbs()).norm(), 1.0e-12);

  auto complex_wide = ksj::array::make_pooled_matrix<std::complex<float>>(4, 32);
  for (std::size_t row = 0U; row < complex_wide.rows(); ++row) {
    for (std::size_t col = 0U; col < complex_wide.cols(); ++col) {
      const auto row_value = static_cast<float>(row + 1U);
      const auto col_value = static_cast<float>(col + 1U);
      const auto col_pattern = static_cast<float>(col % 7U);
      const auto row_pattern = static_cast<float>(row % 3U);
      complex_wide(row, col) = {
        row_value * 0.1F + col_pattern * 0.01F,
        col_value * 0.005F - row_pattern * 0.02F,
      };
    }
  }

  auto complex_left_vectors = ksj::array::make_pooled_matrix<std::complex<float>>(4, 4);
  ksj::linalg::left_singular_vectors(ksj::array::as_const_view(complex_wide.view()), complex_left_vectors.view());
  const auto complex_left_identity = as_eigen(complex_left_vectors).adjoint() * as_eigen(complex_left_vectors);
  for (Eigen::Index row = 0; row < complex_left_identity.rows(); ++row) {
    for (Eigen::Index col = 0; col < complex_left_identity.cols(); ++col) {
      const auto expected = row == col ? 1.0F : 0.0F;
      EXPECT_NEAR(expected, std::abs(complex_left_identity(row, col)), 1.0e-4F);
    }
  }
  const auto complex_gram = as_eigen(complex_wide) * as_eigen(complex_wide).adjoint();
  const auto complex_projected =
    as_eigen(complex_left_vectors).adjoint() * complex_gram * as_eigen(complex_left_vectors);
  for (Eigen::Index row = 0; row < complex_projected.rows(); ++row) {
    for (Eigen::Index col = 0; col < complex_projected.cols(); ++col) {
      if (row == col) {
        continue;
      }
      EXPECT_NEAR(0.0F, std::abs(complex_projected(row, col)), 1.0e-3F);
    }
  }
  for (Eigen::Index index = 1; index < complex_projected.rows(); ++index) {
    EXPECT_GE(complex_projected(index - 1, index - 1).real() + 1.0e-4F, complex_projected(index, index).real());
  }

  auto symmetric = ksj::array::make_pooled_matrix<double>(2, 2);
  symmetric(0, 0) = 2.0;
  symmetric(1, 0) = 1.0;
  symmetric(0, 1) = 1.0;
  symmetric(1, 1) = 2.0;
  const auto self_adjoint = ksj::linalg::self_adjoint_eigen_decomposition(symmetric);
  ASSERT_EQ(2U, self_adjoint.eigenvalues.size());
  EXPECT_NEAR(1.0, self_adjoint.eigenvalues(0), 1.0e-12);
  EXPECT_NEAR(3.0, self_adjoint.eigenvalues(1), 1.0e-12);

  auto svd_storage = ksj::array::make_pooled_matrix<double>(3, 4);
  as_eigen(svd_storage).setZero();
  auto svd_view = svd_storage.view().subview(ksj::array::slice(0, 2), ksj::array::slice(1, 3));
  svd_view(0, 0) = 5.0;
  svd_view(1, 1) = 1.5;
  const auto view_svd = ksj::linalg::svd(ksj::array::as_const_view(svd_view), ksj::linalg::SvdMode::full);
  ASSERT_EQ(2U, view_svd.u.rows());
  ASSERT_EQ(2U, view_svd.u.cols());
  ASSERT_EQ(2U, view_svd.singular_values.size());
  EXPECT_NEAR(5.0, view_svd.singular_values(0), 1.0e-12);
  EXPECT_NEAR(1.5, view_svd.singular_values(1), 1.0e-12);

  auto self_adjoint_storage = ksj::array::make_pooled_matrix<double>(3, 4);
  as_eigen(self_adjoint_storage).setZero();
  auto self_adjoint_view = self_adjoint_storage.view().subview(ksj::array::slice(1, 3), ksj::array::slice(1, 3));
  self_adjoint_view(0, 0) = 2.0;
  self_adjoint_view(1, 0) = 1.0;
  self_adjoint_view(0, 1) = 1.0;
  self_adjoint_view(1, 1) = 2.0;
  const auto view_self_adjoint =
    ksj::linalg::self_adjoint_eigen_decomposition(ksj::array::as_const_view(self_adjoint_view));
  ASSERT_EQ(2U, view_self_adjoint.eigenvalues.size());
  EXPECT_NEAR(1.0, view_self_adjoint.eigenvalues(0), 1.0e-12);
  EXPECT_NEAR(3.0, view_self_adjoint.eigenvalues(1), 1.0e-12);

  auto view_self_adjoint_values = ksj::array::make_pooled_vector<double>(2);
  auto view_self_adjoint_vectors = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::linalg::self_adjoint_eigen_decomposition(ksj::array::as_const_view(self_adjoint_view),
                                                view_self_adjoint_values.view(), view_self_adjoint_vectors.view());
  EXPECT_NEAR(1.0, view_self_adjoint_values(0), 1.0e-12);
  EXPECT_NEAR(3.0, view_self_adjoint_values(1), 1.0e-12);
  const auto output_view_identity = as_eigen(view_self_adjoint_vectors).adjoint() * as_eigen(view_self_adjoint_vectors);
  EXPECT_NEAR(1.0, output_view_identity(0, 0), 1.0e-12);
  EXPECT_NEAR(0.0, output_view_identity(0, 1), 1.0e-12);
  EXPECT_NEAR(0.0, output_view_identity(1, 0), 1.0e-12);
  EXPECT_NEAR(1.0, output_view_identity(1, 1), 1.0e-12);

  auto rotation = ksj::array::make_pooled_matrix<double>(2, 2);
  rotation(0, 0) = 0.0;
  rotation(1, 0) = 1.0;
  rotation(0, 1) = -1.0;
  rotation(1, 1) = 0.0;
  const auto general = ksj::linalg::eigen_decomposition(rotation);
  ASSERT_EQ(2U, general.eigenvalues.size());
  EXPECT_NEAR(0.0, general.eigenvalues(0).real() + general.eigenvalues(1).real(), 1.0e-12);
  EXPECT_NEAR(0.0, general.eigenvalues(0).imag() + general.eigenvalues(1).imag(), 1.0e-12);
  EXPECT_NEAR(1.0, std::abs(general.eigenvalues(0)), 1.0e-12);
  EXPECT_NEAR(1.0, std::abs(general.eigenvalues(1)), 1.0e-12);
}

TEST(KSpaceJetLinalg, IntelDecompositionCandidatesMatchReferenceSemantics) {
  auto diagonal = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(diagonal).setZero();
  diagonal(0, 0) = 3.0;
  diagonal(1, 1) = 2.0;

  auto u = ksj::array::make_pooled_matrix<double>(2, 2);
  auto singular_values = ksj::array::make_pooled_vector<double>(2);
  auto v_adjoint = ksj::array::make_pooled_matrix<double>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::svd(diagonal, u, singular_values, v_adjoint, true));
  EXPECT_NEAR(3.0, singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, singular_values(1), 1.0e-12);
  const auto reconstructed = as_eigen(u) * as_eigen(singular_values).asDiagonal() * as_eigen(v_adjoint);
  EXPECT_NEAR(3.0, reconstructed(0, 0), 1.0e-12);
  EXPECT_NEAR(0.0, reconstructed(1, 0), 1.0e-12);
  EXPECT_NEAR(0.0, reconstructed(0, 1), 1.0e-12);
  EXPECT_NEAR(2.0, reconstructed(1, 1), 1.0e-12);

  auto diagonal_work = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(diagonal_work).setZero();
  diagonal_work(0, 0) = 3.0;
  diagonal_work(1, 1) = 2.0;
  auto in_place_u = ksj::array::make_pooled_matrix<double>(2, 2);
  auto in_place_singular_values = ksj::array::make_pooled_vector<double>(2);
  auto in_place_v_adjoint = ksj::array::make_pooled_matrix<double>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::can_svd_in_place(diagonal_work.view()));
  ASSERT_TRUE(ksj::linalg::detail::intel::svd_in_place(diagonal_work.view(), in_place_u, in_place_singular_values,
                                                       in_place_v_adjoint, true));
  EXPECT_NEAR(3.0, in_place_singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, in_place_singular_values(1), 1.0e-12);

  auto workspace_u = ksj::array::make_pooled_matrix<double>(2, 2);
  auto workspace_singular_values = ksj::array::make_pooled_vector<double>(2);
  auto workspace_v_adjoint = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::linalg::SvdWorkspace<double> svd_workspace;
  svd_workspace.resize(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::svd(ksj::array::as_const_view(diagonal.view()), workspace_u.view(),
                                              workspace_singular_values.view(), workspace_v_adjoint.view(),
                                              svd_workspace, true));
  EXPECT_NEAR(3.0, workspace_singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, workspace_singular_values(1), 1.0e-12);

  auto symmetric = ksj::array::make_pooled_matrix<double>(2, 2);
  symmetric(0, 0) = 2.0;
  symmetric(1, 0) = 1.0;
  symmetric(0, 1) = 1.0;
  symmetric(1, 1) = 2.0;
  auto eigenvalues = ksj::array::make_pooled_vector<double>(2);
  auto eigenvectors = ksj::array::make_pooled_matrix<double>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::self_adjoint_eigen_decomposition(symmetric, eigenvalues, eigenvectors));
  EXPECT_NEAR(1.0, eigenvalues(0), 1.0e-12);
  EXPECT_NEAR(3.0, eigenvalues(1), 1.0e-12);
  const auto symmetric_residual =
    as_eigen(symmetric) * as_eigen(eigenvectors) - as_eigen(eigenvectors) * as_eigen(eigenvalues).asDiagonal();
  EXPECT_NEAR(0.0, symmetric_residual.norm(), 1.0e-12);

  auto rotation = ksj::array::make_pooled_matrix<double>(2, 2);
  rotation(0, 0) = 0.0;
  rotation(1, 0) = 1.0;
  rotation(0, 1) = -1.0;
  rotation(1, 1) = 0.0;
  auto general_values = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  auto general_vectors = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::eigen_decomposition(rotation, general_values, general_vectors));
  EXPECT_NEAR(0.0, general_values(0).real() + general_values(1).real(), 1.0e-12);
  EXPECT_NEAR(0.0, general_values(0).imag() + general_values(1).imag(), 1.0e-12);
  EXPECT_NEAR(1.0, std::abs(general_values(0)), 1.0e-12);
  EXPECT_NEAR(1.0, std::abs(general_values(1)), 1.0e-12);
  const auto complex_rotation = as_eigen(rotation).template cast<ksj::base::cf64>();
  const auto general_residual =
    complex_rotation * as_eigen(general_vectors) - as_eigen(general_vectors) * as_eigen(general_values).asDiagonal();
  EXPECT_NEAR(0.0, general_residual.norm(), 1.0e-12);
}

TEST(KSpaceJetLinalg, SupportsComplexDecompositions) {
  auto hermitian = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  hermitian(0, 0) = ksj::base::cf32{2.0, 0.0};
  hermitian(1, 0) = ksj::base::cf32{0.0, -1.0};
  hermitian(0, 1) = ksj::base::cf32{0.0, 1.0};
  hermitian(1, 1) = ksj::base::cf32{2.0, 0.0};

  const auto self_adjoint = ksj::linalg::self_adjoint_eigen_decomposition(hermitian);
  ASSERT_EQ(2U, self_adjoint.eigenvalues.size());
  EXPECT_NEAR(1.0F, self_adjoint.eigenvalues(0), kCf32Tolerance);
  EXPECT_NEAR(3.0F, self_adjoint.eigenvalues(1), kCf32Tolerance);

  const auto singular = ksj::linalg::singular_values(hermitian);
  ASSERT_EQ(2U, singular.size());
  EXPECT_NEAR(3.0F, singular(0), kCf32Tolerance);
  EXPECT_NEAR(1.0F, singular(1), kCf32Tolerance);

  const auto whitening = ksj::linalg::whitening_matrix_from_covariance(hermitian);
  const auto whitened_covariance = as_eigen(whitening).adjoint() * as_eigen(hermitian) * as_eigen(whitening);
  EXPECT_NEAR(
    0.0, (whitened_covariance - Eigen::Matrix<ksj::base::cf32, Eigen::Dynamic, Eigen::Dynamic>::Identity(2, 2)).norm(),
    kCf32Tolerance);

  const auto full = ksj::linalg::full_svd(hermitian);
  ASSERT_EQ(2U, full.u.rows());
  ASSERT_EQ(2U, full.u.cols());
  ASSERT_EQ(2U, full.singular_values.size());
  ASSERT_EQ(2U, full.v_adjoint.rows());
  ASSERT_EQ(2U, full.v_adjoint.cols());
  const auto reconstructed = as_eigen(full.u) * as_eigen(full.singular_values).asDiagonal() * as_eigen(full.v_adjoint);
  EXPECT_NEAR(0.0F, (reconstructed - as_eigen(hermitian)).norm(), kCf32Tolerance);

  auto hermitian_work = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  hermitian_work(0, 0) = hermitian(0, 0);
  hermitian_work(1, 0) = hermitian(1, 0);
  hermitian_work(0, 1) = hermitian(0, 1);
  hermitian_work(1, 1) = hermitian(1, 1);
  const auto in_place_full = ksj::linalg::full_svd_in_place(hermitian_work);
  ASSERT_EQ(2U, in_place_full.u.rows());
  ASSERT_EQ(2U, in_place_full.u.cols());
  ASSERT_EQ(2U, in_place_full.singular_values.size());
  ASSERT_EQ(2U, in_place_full.v_adjoint.rows());
  ASSERT_EQ(2U, in_place_full.v_adjoint.cols());
  const auto in_place_reconstructed = as_eigen(in_place_full.u) * as_eigen(in_place_full.singular_values).asDiagonal() *
                                      as_eigen(in_place_full.v_adjoint);
  EXPECT_NEAR(0.0F, (in_place_reconstructed - as_eigen(hermitian)).norm(), kCf32Tolerance);

  auto workspace_u = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  auto workspace_singular_values = ksj::array::make_pooled_vector<float>(2);
  auto workspace_v_adjoint = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ksj::linalg::SvdWorkspace<ksj::base::cf32> svd_workspace;
  ASSERT_TRUE(
    ksj::linalg::full_svd(hermitian, workspace_u, workspace_singular_values, workspace_v_adjoint, svd_workspace));
  const auto workspace_reconstructed =
    as_eigen(workspace_u) * as_eigen(workspace_singular_values).asDiagonal() * as_eigen(workspace_v_adjoint);
  EXPECT_NEAR(0.0F, (workspace_reconstructed - as_eigen(hermitian)).norm(), kCf32Tolerance);

  auto rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  rhs(0) = ksj::base::cf32{2.0, 1.0};
  rhs(1) = ksj::base::cf32{4.0, -2.0};
  const auto least_squares = ksj::linalg::solve_least_squares(hermitian, rhs, ksj::linalg::LeastSquaresSolver::svd);
  EXPECT_NEAR(2.0F, (as_eigen(hermitian) * as_eigen(least_squares))(0).real(), kCf32Tolerance);
  EXPECT_NEAR(1.0F, (as_eigen(hermitian) * as_eigen(least_squares))(0).imag(), kCf32Tolerance);
  EXPECT_NEAR(4.0F, (as_eigen(hermitian) * as_eigen(least_squares))(1).real(), kCf32Tolerance);
  EXPECT_NEAR(-2.0F, (as_eigen(hermitian) * as_eigen(least_squares))(1).imag(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, UsesOnlyCallerWorkspaceForBoundedHermitianCalibrationPrimitives) {
  using Complex = ksj::base::cf32;
  std::array<Complex, 4U> hermitian{
    Complex{2.0F, 0.0F},
    Complex{0.0F, -1.0F},
    Complex{0.0F, 1.0F},
    Complex{2.0F, 0.0F},
  };
  std::array<float, 2U> eigenvalues{};
  std::array<Complex, 4U> eigenvectors{};
  std::array<Complex, 4U> eigensolver_workspace{};
  const auto matrix = ksj::array::MatrixView<const Complex>{hermitian.data(), 2U, 2U};
  const auto values = ksj::array::VectorView<float>{eigenvalues.data(), eigenvalues.size()};
  const auto vectors = ksj::array::MatrixView<Complex>{eigenvectors.data(), 2U, 2U};
  const auto workspace = ksj::array::MatrixView<Complex>{eigensolver_workspace.data(), 2U, 2U};

  ASSERT_TRUE(ksj::linalg::self_adjoint_eigen_decomposition_with_workspace(matrix, values, vectors, workspace));
  EXPECT_NEAR(1.0F, eigenvalues[0U], kCf32Tolerance);
  EXPECT_NEAR(3.0F, eigenvalues[1U], kCf32Tolerance);
  for (std::size_t row = 0U; row < 2U; ++row) {
    for (std::size_t column = 0U; column < 2U; ++column) {
      Complex reconstructed{};
      for (std::size_t eigenvector = 0U; eigenvector < 2U; ++eigenvector) {
        reconstructed += eigenvectors[row * 2U + eigenvector] * eigenvalues[eigenvector] *
                         std::conj(eigenvectors[column * 2U + eigenvector]);
      }
      EXPECT_NEAR(hermitian[row * 2U + column].real(), reconstructed.real(), kCf32Tolerance);
      EXPECT_NEAR(hermitian[row * 2U + column].imag(), reconstructed.imag(), kCf32Tolerance);
    }
  }

  std::array<Complex, 4U> whitening{};
  const auto whitening_view = ksj::array::MatrixView<Complex>{whitening.data(), 2U, 2U};
  ksj::linalg::whitening_matrix_from_self_adjoint_eigen_with_workspace(
    ksj::array::VectorView<const float>{eigenvalues.data(), eigenvalues.size()},
    ksj::array::MatrixView<const Complex>{eigenvectors.data(), 2U, 2U}, whitening_view, 1.0e-6F);
  Complex whitened_covariance[4U]{};
  for (std::size_t row = 0U; row < 2U; ++row) {
    for (std::size_t column = 0U; column < 2U; ++column) {
      for (std::size_t left = 0U; left < 2U; ++left) {
        for (std::size_t right = 0U; right < 2U; ++right) {
          whitened_covariance[row * 2U + column] +=
            std::conj(whitening[left * 2U + row]) * hermitian[left * 2U + right] * whitening[right * 2U + column];
        }
      }
    }
  }
  EXPECT_NEAR(1.0F, whitened_covariance[0U].real(), 2.0e-4F);
  EXPECT_NEAR(1.0F, whitened_covariance[3U].real(), 2.0e-4F);
  EXPECT_NEAR(0.0F, std::abs(whitened_covariance[1U]), 2.0e-4F);
  EXPECT_NEAR(0.0F, std::abs(whitened_covariance[2U]), 2.0e-4F);

  std::array<Complex, 12U> channel_major_samples{
    Complex{1.0F, 0.0F}, Complex{2.0F, 0.0F}, Complex{3.0F, 0.0F},  Complex{4.0F, 0.0F},
    Complex{5.0F, 0.0F}, Complex{6.0F, 0.0F}, Complex{2.0F, 0.0F},  Complex{4.0F, 0.0F},
    Complex{6.0F, 0.0F}, Complex{8.0F, 0.0F}, Complex{10.0F, 0.0F}, Complex{12.0F, 0.0F},
  };
  std::array<Complex, 4U> covariance{};
  std::array<Complex, 2U> means{};
  ksj::stats::covariance_channel_major_with_workspace(
    ksj::array::MatrixView<const Complex>{channel_major_samples.data(), 2U, 6U},
    ksj::array::MatrixView<Complex>{covariance.data(), 2U, 2U},
    ksj::array::VectorView<Complex>{means.data(), means.size()}, ksj::stats::VarianceNormalization::population);
  EXPECT_NEAR(3.5F, means[0U].real(), kCf32Tolerance);
  EXPECT_NEAR(7.0F, means[1U].real(), kCf32Tolerance);
  EXPECT_NEAR(35.0F / 12.0F, covariance[0U].real(), kCf32Tolerance);
  EXPECT_NEAR(35.0F / 6.0F, covariance[1U].real(), kCf32Tolerance);
  EXPECT_NEAR(35.0F / 6.0F, covariance[2U].real(), kCf32Tolerance);
  EXPECT_NEAR(35.0F / 3.0F, covariance[3U].real(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, IntelComplexDecompositionCandidatesMatchReferenceSemantics) {
  auto diagonal = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  as_eigen(diagonal).setZero();
  diagonal(0, 0) = ksj::base::cf32{3.0, 0.0};
  diagonal(1, 1) = ksj::base::cf32{0.0, 2.0};

  auto u = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  auto singular_values = ksj::array::make_pooled_vector<float>(2);
  auto v_adjoint = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::svd(diagonal, u, singular_values, v_adjoint, true));
  EXPECT_NEAR(3.0, singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, singular_values(1), 1.0e-12);
  const auto reconstructed = as_eigen(u) * as_eigen(singular_values).asDiagonal() * as_eigen(v_adjoint);
  EXPECT_NEAR(0.0, (reconstructed - as_eigen(diagonal)).norm(), 1.0e-12);

  auto diagonal_work = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  as_eigen(diagonal_work).setZero();
  diagonal_work(0, 0) = ksj::base::cf32{3.0, 0.0};
  diagonal_work(1, 1) = ksj::base::cf32{0.0, 2.0};
  auto in_place_u = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  auto in_place_singular_values = ksj::array::make_pooled_vector<float>(2);
  auto in_place_v_adjoint = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::can_svd_in_place(diagonal_work.view()));
  ASSERT_TRUE(ksj::linalg::detail::intel::svd_in_place(diagonal_work.view(), in_place_u, in_place_singular_values,
                                                       in_place_v_adjoint, true));
  EXPECT_NEAR(3.0, in_place_singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, in_place_singular_values(1), 1.0e-12);

  auto workspace_u = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  auto workspace_singular_values = ksj::array::make_pooled_vector<float>(2);
  auto workspace_v_adjoint = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ksj::linalg::SvdWorkspace<ksj::base::cf32> svd_workspace;
  svd_workspace.resize(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::svd(ksj::array::as_const_view(diagonal.view()), workspace_u.view(),
                                              workspace_singular_values.view(), workspace_v_adjoint.view(),
                                              svd_workspace, true));
  EXPECT_NEAR(3.0, workspace_singular_values(0), 1.0e-12);
  EXPECT_NEAR(2.0, workspace_singular_values(1), 1.0e-12);

  auto hermitian = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  hermitian(0, 0) = ksj::base::cf32{2.0, 0.0};
  hermitian(1, 0) = ksj::base::cf32{0.0, -1.0};
  hermitian(0, 1) = ksj::base::cf32{0.0, 1.0};
  hermitian(1, 1) = ksj::base::cf32{2.0, 0.0};
  auto eigenvalues = ksj::array::make_pooled_vector<float>(2);
  auto eigenvectors = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::self_adjoint_eigen_decomposition(hermitian, eigenvalues, eigenvectors));
  EXPECT_NEAR(1.0, eigenvalues(0), 1.0e-12);
  EXPECT_NEAR(3.0, eigenvalues(1), 1.0e-12);
  const auto hermitian_residual =
    as_eigen(hermitian) * as_eigen(eigenvectors) - as_eigen(eigenvectors) * as_eigen(eigenvalues).asDiagonal();
  EXPECT_NEAR(0.0, hermitian_residual.norm(), 1.0e-12);

  auto general = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  as_eigen(general).setZero();
  general(0, 0) = ksj::base::cf32{0.0, 1.0};
  general(1, 1) = ksj::base::cf32{2.0, -1.0};
  auto general_values = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  auto general_vectors = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::eigen_decomposition(general, general_values, general_vectors));
  EXPECT_NEAR(
    0.0, std::abs((general_values(0) - ksj::base::cf32{0.0, 1.0}) * (general_values(1) - ksj::base::cf32{0.0, 1.0})),
    1.0e-12);
  EXPECT_NEAR(
    0.0, std::abs((general_values(0) - ksj::base::cf32{2.0, -1.0}) * (general_values(1) - ksj::base::cf32{2.0, -1.0})),
    1.0e-12);
  const auto general_residual =
    as_eigen(general) * as_eigen(general_vectors) - as_eigen(general_vectors) * as_eigen(general_values).asDiagonal();
  EXPECT_NEAR(0.0, general_residual.norm(), 1.0e-12);
}

TEST(KSpaceJetLinalg, IntelComplexSolveCandidatesMatchReferenceSemantics) {
  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  matrix(0, 0) = ksj::base::cf32{4.0, 1.0};
  matrix(1, 0) = ksj::base::cf32{2.0, 0.25};
  matrix(0, 1) = ksj::base::cf32{1.0, -0.5};
  matrix(1, 1) = ksj::base::cf32{3.0, -1.0};

  auto expected = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  expected(0) = ksj::base::cf32{1.0, -1.0};
  expected(1) = ksj::base::cf32{2.0, 0.5};
  auto rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  as_eigen(rhs) = as_eigen(matrix) * as_eigen(expected);

  auto lu_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_lu(matrix, rhs, lu_solution));
  EXPECT_NEAR(0.0F, (as_eigen(lu_solution) - as_eigen(expected)).norm(), kCf32Tolerance);

  auto output_lu_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ksj::linalg::solve(matrix, rhs, output_lu_solution);
  EXPECT_NEAR(0.0F, (as_eigen(output_lu_solution) - as_eigen(expected)).norm(), kCf32Tolerance);

  const auto view_lu_solution =
    ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()));
  EXPECT_NEAR(0.0F, (as_eigen(view_lu_solution) - as_eigen(expected)).norm(), kCf32Tolerance);

  const auto pooled_lu_solution = ksj::linalg::solve_lu(matrix, rhs);
  EXPECT_NEAR(0.0F, (as_eigen(pooled_lu_solution) - as_eigen(expected)).norm(), kCf32Tolerance);

  auto expected_matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  expected_matrix(0, 0) = ksj::base::cf32{1.0, -1.0};
  expected_matrix(1, 0) = ksj::base::cf32{2.0, 0.5};
  expected_matrix(0, 1) = ksj::base::cf32{-0.5, 1.0};
  expected_matrix(1, 1) = ksj::base::cf32{0.75, -0.25};
  auto rhs_matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  as_eigen(rhs_matrix) = as_eigen(matrix) * as_eigen(expected_matrix);

  auto lu_matrix_solution = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_lu(matrix, rhs_matrix, lu_matrix_solution));
  EXPECT_NEAR(0.0F, (as_eigen(lu_matrix_solution) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);

  auto output_lu_matrix_solution = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ksj::linalg::solve(matrix, rhs_matrix, output_lu_matrix_solution);
  EXPECT_NEAR(0.0F, (as_eigen(output_lu_matrix_solution) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);

  const auto view_lu_matrix_solution =
    ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs_matrix.view()));
  EXPECT_NEAR(0.0F, (as_eigen(view_lu_matrix_solution) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);

  const auto pooled_lu_matrix_solution = ksj::linalg::solve_lu(matrix, rhs_matrix);
  EXPECT_NEAR(0.0F, (as_eigen(pooled_lu_matrix_solution) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);

  auto hermitian = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  hermitian(0, 0) = ksj::base::cf32{4.0, 0.0};
  hermitian(1, 0) = ksj::base::cf32{1.0, -0.5};
  hermitian(0, 1) = ksj::base::cf32{1.0, 0.5};
  hermitian(1, 1) = ksj::base::cf32{3.0, 0.0};

  auto lower = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::cholesky_lower(hermitian, lower));
  EXPECT_NEAR(0.0F, (as_eigen(lower) * as_eigen(lower).adjoint() - as_eigen(hermitian)).norm(), kCf32Tolerance);

  as_eigen(rhs) = as_eigen(hermitian) * as_eigen(expected);
  auto cholesky_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_cholesky(hermitian, rhs, cholesky_solution));
  EXPECT_NEAR(0.0F, (as_eigen(cholesky_solution) - as_eigen(expected)).norm(), kCf32Tolerance);

  as_eigen(rhs_matrix) = as_eigen(hermitian) * as_eigen(expected_matrix);
  auto cholesky_matrix_solution = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_cholesky(hermitian, rhs_matrix, cholesky_matrix_solution));
  EXPECT_NEAR(0.0F, (as_eigen(cholesky_matrix_solution) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);

  auto qr_matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(3, 2);
  qr_matrix(0, 0) = ksj::base::cf32{1.0, 0.5};
  qr_matrix(1, 0) = ksj::base::cf32{0.5, -1.0};
  qr_matrix(2, 0) = ksj::base::cf32{2.0, 0.25};
  qr_matrix(0, 1) = ksj::base::cf32{-0.5, 1.0};
  qr_matrix(1, 1) = ksj::base::cf32{1.5, 0.25};
  qr_matrix(2, 1) = ksj::base::cf32{0.25, -0.75};

  auto qr_rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(3);
  as_eigen(qr_rhs) = as_eigen(qr_matrix) * as_eigen(expected);
  auto qr_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs, qr_solution));
  EXPECT_NEAR(0.0F, (as_eigen(qr_solution) - as_eigen(expected)).norm(), kCf32Tolerance);

  auto qr_rhs_matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(3, 2);
  as_eigen(qr_rhs_matrix) = as_eigen(qr_matrix) * as_eigen(expected_matrix);
  auto qr_matrix_solution = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs_matrix, qr_matrix_solution));
  EXPECT_NEAR(0.0F, (as_eigen(qr_matrix_solution) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, RefinedComplexSolveSupportsMultipleRightHandSidesAndReportsFailure) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(3U, 3U);
  matrix(0U, 0U) = {8.0F, 0.0F};
  matrix(0U, 1U) = {1.0F, -0.5F};
  matrix(0U, 2U) = {0.25F, 0.75F};
  matrix(1U, 0U) = {1.0F, 0.5F};
  matrix(1U, 1U) = {6.0F, 0.0F};
  matrix(1U, 2U) = {-0.5F, 0.25F};
  matrix(2U, 0U) = {0.25F, -0.75F};
  matrix(2U, 1U) = {-0.5F, -0.25F};
  matrix(2U, 2U) = {5.0F, 0.0F};

  auto expected = ksj::array::make_pooled_matrix<complex_type>(3U, 2U);
  expected(0U, 0U) = {1.0F, -0.5F};
  expected(0U, 1U) = {-0.25F, 1.5F};
  expected(1U, 0U) = {2.0F, 0.75F};
  expected(1U, 1U) = {0.5F, -1.0F};
  expected(2U, 0U) = {-1.0F, 0.25F};
  expected(2U, 1U) = {1.25F, 0.5F};
  auto rhs = ksj::linalg::matmul(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(expected.view()));

  auto output = ksj::array::make_pooled_matrix<complex_type>(3U, 2U);
  float reciprocal_condition{};
  ASSERT_TRUE(ksj::linalg::try_solve_refined(matrix, rhs, output, reciprocal_condition));
  EXPECT_GT(reciprocal_condition, 0.0F);
  EXPECT_NEAR(0.0F, (as_eigen(output) - as_eigen(expected)).norm(), 2.0e-5F);

  const auto allocated_output = ksj::linalg::solve_refined(matrix, rhs);
  EXPECT_NEAR(0.0F, (as_eigen(allocated_output) - as_eigen(expected)).norm(), 2.0e-5F);

  auto singular = ksj::array::make_pooled_matrix<complex_type>(2U, 2U);
  singular(0U, 0U) = {1.0F, 0.0F};
  singular(0U, 1U) = {2.0F, 0.0F};
  singular(1U, 0U) = {2.0F, 0.0F};
  singular(1U, 1U) = {4.0F, 0.0F};
  auto singular_rhs = ksj::array::make_pooled_matrix<complex_type>(2U, 1U);
  singular_rhs(0U, 0U) = {3.0F, 0.0F};
  singular_rhs(1U, 0U) = {6.0F, 0.0F};
  auto unchanged_output = ksj::array::make_pooled_matrix<complex_type>(2U, 1U);
  ksj::array::fill(unchanged_output.view(), complex_type{17.0F, -3.0F});
  reciprocal_condition = 1.0F;
  EXPECT_FALSE(ksj::linalg::try_solve_refined(singular, singular_rhs, unchanged_output, reciprocal_condition));
  EXPECT_EQ(complex_type(17.0F, -3.0F), unchanged_output(0U, 0U));
  EXPECT_EQ(complex_type(17.0F, -3.0F), unchanged_output(1U, 0U));
  EXPECT_THROW((void)ksj::linalg::solve_refined(singular, singular_rhs), std::runtime_error);

  auto wrong_rhs = ksj::array::make_pooled_matrix<complex_type>(4U, 1U);
  EXPECT_THROW((void)ksj::linalg::try_solve_refined(matrix, wrong_rhs, output, reciprocal_condition),
               std::invalid_argument);
}

TEST(KSpaceJetLinalg, ReusesCallerOwnedWorkspaceForComplexMatrixLuSolve) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(3, 3);
  matrix(0, 0) = {5.0F, 0.5F};
  matrix(0, 1) = {1.0F, -0.5F};
  matrix(0, 2) = {0.5F, 0.25F};
  matrix(1, 0) = {0.25F, 0.5F};
  matrix(1, 1) = {4.0F, -0.25F};
  matrix(1, 2) = {1.0F, 0.0F};
  matrix(2, 0) = {0.5F, -0.25F};
  matrix(2, 1) = {0.75F, 0.5F};
  matrix(2, 2) = {3.0F, 0.25F};

  auto expected = ksj::array::make_pooled_matrix<complex_type>(3, 2);
  expected(0, 0) = {1.0F, -1.0F};
  expected(0, 1) = {0.5F, 0.25F};
  expected(1, 0) = {2.0F, 0.5F};
  expected(1, 1) = {-1.0F, 0.75F};
  expected(2, 0) = {-0.5F, 1.0F};
  expected(2, 1) = {1.5F, -0.25F};
  auto rhs = ksj::linalg::matmul(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(expected.view()));
  auto output = ksj::array::make_pooled_matrix<complex_type>(3, 2);
  ksj::linalg::LuSolveWorkspace<complex_type> workspace;

  ASSERT_TRUE(ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace));
  EXPECT_NEAR(0.0F, (as_eigen(output) - as_eigen(expected)).norm(), kCf32Tolerance);

  const auto returned_output =
    ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()));
  EXPECT_NEAR(0.0F, (as_eigen(returned_output) - as_eigen(expected)).norm(), kCf32Tolerance);

  const auto* matrix_work = workspace.matrix_work.data();
  const auto* rhs_work = workspace.rhs_work.data();
  const auto* pivots = workspace.pivots.data();
  ASSERT_TRUE(ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace));
  EXPECT_EQ(matrix_work, workspace.matrix_work.data());
  EXPECT_EQ(rhs_work, workspace.rhs_work.data());
  EXPECT_EQ(pivots, workspace.pivots.data());
}

TEST(KSpaceJetLinalg, ReusesCallerOwnedWorkspaceForComplexVectorLuSolve) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(4, 4);
  as_eigen(matrix).setZero();
  matrix(0, 0) = {6.0F, 0.5F};
  matrix(0, 1) = {0.5F, -0.25F};
  matrix(1, 0) = {0.25F, 0.75F};
  matrix(1, 1) = {5.0F, -0.5F};
  matrix(1, 2) = {-0.5F, 0.25F};
  matrix(2, 1) = {1.0F, 0.0F};
  matrix(2, 2) = {4.0F, 0.25F};
  matrix(2, 3) = {0.25F, -0.5F};
  matrix(3, 0) = {-0.25F, 0.5F};
  matrix(3, 2) = {0.75F, -0.25F};
  matrix(3, 3) = {3.0F, 0.75F};

  auto expected = ksj::array::make_pooled_vector<complex_type>(4);
  expected(0) = {1.0F, -0.5F};
  expected(1) = {-0.25F, 1.5F};
  expected(2) = {2.0F, 0.75F};
  expected(3) = {-1.0F, 0.25F};
  auto rhs = ksj::array::make_pooled_vector<complex_type>(4);
  as_eigen(rhs) = as_eigen(matrix) * as_eigen(expected);

  auto output = ksj::array::make_pooled_vector<complex_type>(4);
  ksj::linalg::LuSolveWorkspace<complex_type> workspace;
  ASSERT_TRUE(ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace));
  EXPECT_NEAR(0.0F, (as_eigen(output) - as_eigen(expected)).norm(), kCf32Tolerance);

  const auto* matrix_work = workspace.matrix_work.data();
  const auto* rhs_work = workspace.rhs_work.data();
  const auto* pivots = workspace.pivots.data();
  ASSERT_TRUE(ksj::linalg::solve_lu(matrix, rhs, output, workspace));
  EXPECT_NEAR(0.0F, (as_eigen(output) - as_eigen(expected)).norm(), kCf32Tolerance);
  EXPECT_EQ(matrix_work, workspace.matrix_work.data());
  EXPECT_EQ(rhs_work, workspace.rhs_work.data());
  EXPECT_EQ(pivots, workspace.pivots.data());
}

TEST(KSpaceJetLinalg, ReusesCallerOwnedWorkspaceForComplexQrSolve) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(18, 16);
  as_eigen(matrix).setZero();
  for (std::size_t index = 0U; index < 16U; ++index) {
    matrix(index, index) = complex_type{1.0F, 0.0F};
  }

  auto expected = ksj::array::make_pooled_vector<complex_type>(16);
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    expected(index) = complex_type{static_cast<float>(index) * 0.25F, -static_cast<float>(index) * 0.125F};
  }
  auto rhs = ksj::array::make_pooled_vector<complex_type>(18);
  as_eigen(rhs) = as_eigen(matrix) * as_eigen(expected);

  auto output = ksj::array::make_pooled_vector<complex_type>(16);
  ksj::linalg::LeastSquaresQrWorkspace<complex_type> workspace;
  ASSERT_TRUE(ksj::linalg::solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace));
  EXPECT_NEAR(0.0F, (as_eigen(output) - as_eigen(expected)).norm(), kCf32Tolerance);

  const auto* matrix_work = workspace.matrix_work.data();
  const auto* rhs_work = workspace.rhs_vector_work.data();
  ASSERT_NE(nullptr, matrix_work);
  ASSERT_NE(nullptr, rhs_work);
  ASSERT_TRUE(ksj::linalg::solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace));
  EXPECT_EQ(matrix_work, workspace.matrix_work.data());
  EXPECT_EQ(rhs_work, workspace.rhs_vector_work.data());
}

TEST(KSpaceJetLinalg, SolvesComplexLeastSquaresIntoCallerOutputForNormalEquations) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(4, 2);
  matrix(0, 0) = {1.0F, 0.0F};
  matrix(0, 1) = {0.0F, 0.0F};
  matrix(1, 0) = {0.5F, 0.25F};
  matrix(1, 1) = {1.0F, -0.5F};
  matrix(2, 0) = {1.0F, -0.25F};
  matrix(2, 1) = {0.25F, 0.5F};
  matrix(3, 0) = {0.0F, 0.0F};
  matrix(3, 1) = {1.0F, 0.0F};

  auto expected_vector = ksj::array::make_pooled_vector<complex_type>(2);
  expected_vector(0) = {2.0F, -0.5F};
  expected_vector(1) = {-1.0F, 0.75F};
  auto rhs_vector = ksj::array::make_pooled_vector<complex_type>(4);
  as_eigen(rhs_vector) = as_eigen(matrix) * as_eigen(expected_vector);

  auto vector_output = ksj::array::make_pooled_vector<complex_type>(2);
  auto qr_workspace = ksj::linalg::LeastSquaresQrWorkspace<complex_type>{};
  auto svd_workspace = ksj::linalg::LeastSquaresSvdWorkspace<complex_type>{};
  const auto* vector_output_data = vector_output.data();
  ASSERT_TRUE(ksj::linalg::solve_least_squares(matrix, rhs_vector, vector_output, qr_workspace, svd_workspace,
                                               ksj::linalg::LeastSquaresSolver::normal_equations));
  EXPECT_EQ(vector_output_data, vector_output.data());
  EXPECT_NEAR(0.0F, (as_eigen(vector_output) - as_eigen(expected_vector)).norm(), kCf32Tolerance);

  auto expected_matrix = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  expected_matrix(0, 0) = {1.5F, -0.5F};
  expected_matrix(0, 1) = {0.25F, 1.0F};
  expected_matrix(1, 0) = {-0.75F, 0.125F};
  expected_matrix(1, 1) = {2.0F, -0.25F};
  auto rhs_matrix = ksj::array::make_pooled_matrix<complex_type>(4, 2);
  as_eigen(rhs_matrix) = as_eigen(matrix) * as_eigen(expected_matrix);

  auto matrix_output = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  const auto* matrix_output_data = matrix_output.data();
  ASSERT_TRUE(ksj::linalg::solve_least_squares(matrix, rhs_matrix, matrix_output, qr_workspace, svd_workspace,
                                               ksj::linalg::LeastSquaresSolver::normal_equations));
  EXPECT_EQ(matrix_output_data, matrix_output.data());
  EXPECT_NEAR(0.0F, (as_eigen(matrix_output) - as_eigen(expected_matrix)).norm(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, ReusesCallerOwnedWorkspaceForComplexInverse) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  matrix(0, 0) = {4.0F, 1.0F};
  matrix(0, 1) = {1.0F, -0.5F};
  matrix(1, 0) = {2.0F, 0.25F};
  matrix(1, 1) = {3.0F, -1.0F};
  auto output = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  ksj::linalg::LuFactorWorkspace<complex_type> workspace;

  ASSERT_TRUE(ksj::linalg::inverse(ksj::array::as_const_view(matrix.view()), output.view(), workspace));
  const auto identity = as_eigen(matrix) * as_eigen(output);
  EXPECT_NEAR(0.0F, (identity - Eigen::Matrix<complex_type, 2, 2>::Identity()).norm(), kCf32Tolerance);

  const auto* matrix_work = workspace.matrix_work.data();
  const auto* pivots = workspace.pivots.data();
  ASSERT_TRUE(ksj::linalg::inverse(ksj::array::as_const_view(matrix.view()), output.view(), workspace));
  EXPECT_EQ(matrix_work, workspace.matrix_work.data());
  EXPECT_EQ(pivots, workspace.pivots.data());
}

TEST(KSpaceJetLinalg, SupportsScaledComplexMatmulViewOutput) {
  using complex_type = ksj::base::cf32;
  auto lhs = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  auto rhs = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  lhs(0, 0) = {1.0F, 0.5F};
  lhs(0, 1) = {2.0F, -1.0F};
  lhs(1, 0) = {-0.5F, 1.0F};
  lhs(1, 1) = {3.0F, 0.25F};
  rhs(0, 0) = {0.5F, -1.0F};
  rhs(0, 1) = {1.0F, 2.0F};
  rhs(1, 0) = {2.0F, 0.5F};
  rhs(1, 1) = {-1.0F, 0.25F};
  auto output = ksj::array::make_pooled_matrix<complex_type>(2, 2);
  const complex_type alpha{1.0F, 1.0F};

  ksj::linalg::matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                      alpha);
  const auto expected = alpha * (as_eigen(lhs) * as_eigen(rhs));
  EXPECT_NEAR(0.0F, (as_eigen(output) - expected).norm(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, SupportsGeneralEigenViewOutputWithCallerOwnedWorkspace) {
  using complex_type = ksj::base::cf32;
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(3, 3);
  matrix.set_zero();
  matrix(0, 0) = {1.0F, 2.0F};
  matrix(1, 1) = {3.0F, -1.0F};
  matrix(2, 2) = {-2.0F, 0.5F};

  auto values = ksj::array::make_pooled_vector<complex_type>(3);
  auto vectors = ksj::array::make_pooled_matrix<complex_type>(3, 3);
  ksj::linalg::GeneralEigenWorkspace<complex_type> workspace;
  ASSERT_TRUE(ksj::linalg::eigen_decomposition(ksj::array::as_const_view(matrix.view()), values.view(), vectors.view(),
                                               workspace));

  const auto residual = as_eigen(matrix) * as_eigen(vectors) - as_eigen(vectors) * as_eigen(values).asDiagonal();
  EXPECT_NEAR(0.0F, residual.norm(), kCf32Tolerance);

  const auto returned = ksj::linalg::eigen_decomposition(ksj::array::as_const_view(matrix.view()));
  const auto returned_residual = as_eigen(matrix) * as_eigen(returned.eigenvectors) -
                                 as_eigen(returned.eigenvectors) * as_eigen(returned.eigenvalues).asDiagonal();
  EXPECT_NEAR(0.0F, returned_residual.norm(), kCf32Tolerance);

  const auto* matrix_work = workspace.matrix_work.data();
  const auto* values_work = workspace.eigenvalues_work.data();
  const auto* vectors_work = workspace.eigenvectors_work.data();
  ASSERT_TRUE(ksj::linalg::eigen_decomposition(ksj::array::as_const_view(matrix.view()), values.view(), vectors.view(),
                                               workspace));
  EXPECT_EQ(matrix_work, workspace.matrix_work.data());
  EXPECT_EQ(values_work, workspace.eigenvalues_work.data());
  EXPECT_EQ(vectors_work, workspace.eigenvectors_work.data());
}

TEST(KSpaceJetLinalg, SupportsRealGeneralEigenViewOutputWithCallerOwnedWorkspace) {
  auto rotation = ksj::array::make_pooled_matrix<double>(2, 2);
  rotation(0, 0) = 0.0;
  rotation(0, 1) = -1.0;
  rotation(1, 0) = 1.0;
  rotation(1, 1) = 0.0;
  auto values = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  auto vectors = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  ksj::linalg::GeneralEigenWorkspace<double> workspace;

  ASSERT_TRUE(ksj::linalg::eigen_decomposition(ksj::array::as_const_view(rotation.view()), values.view(),
                                               vectors.view(), workspace));
  EXPECT_NEAR(0.0, values(0).real() + values(1).real(), 1.0e-12);
  EXPECT_NEAR(0.0, values(0).imag() + values(1).imag(), 1.0e-12);
  const auto complex_rotation = as_eigen(rotation).cast<ksj::base::cf64>();
  const auto residual = complex_rotation * as_eigen(vectors) - as_eigen(vectors) * as_eigen(values).asDiagonal();
  EXPECT_NEAR(0.0, residual.norm(), 1.0e-12);
}

TEST(KSpaceJetLinalg, IntelWhiteningCandidateMatchesReferenceSemantics) {
  auto covariance = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  covariance(0, 0) = ksj::base::cf32{4.0, 0.0};
  covariance(1, 0) = ksj::base::cf32{1.0, -0.5};
  covariance(0, 1) = ksj::base::cf32{1.0, 0.5};
  covariance(1, 1) = ksj::base::cf32{3.0, 0.0};

  auto whitening = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::whitening_matrix_from_covariance(covariance, whitening));
  const auto whitened_covariance = as_eigen(whitening).adjoint() * as_eigen(covariance) * as_eigen(whitening);
  EXPECT_NEAR(
    0.0, (whitened_covariance - Eigen::Matrix<ksj::base::cf32, Eigen::Dynamic, Eigen::Dynamic>::Identity(2, 2)).norm(),
    kCf32Tolerance);

  const auto reference = ksj::linalg::detail::eigen::whitening_matrix_from_covariance(covariance, kCf32Tolerance);
  EXPECT_NEAR(0.0F, (as_eigen(whitening) - as_eigen(reference)).norm(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, IntelCovarianceWhitenAndLeastSquaresCandidatesMatchReferenceSemantics) {
  auto samples = ksj::array::make_pooled_matrix<ksj::base::cf32>(4, 2);
  samples(0, 0) = ksj::base::cf32{1.0, 0.5};
  samples(1, 0) = ksj::base::cf32{2.0, -0.25};
  samples(2, 0) = ksj::base::cf32{3.0, 0.75};
  samples(3, 0) = ksj::base::cf32{4.0, -0.5};
  samples(0, 1) = ksj::base::cf32{2.0, -1.0};
  samples(1, 1) = ksj::base::cf32{3.0, 0.5};
  samples(2, 1) = ksj::base::cf32{5.0, -0.25};
  samples(3, 1) = ksj::base::cf32{7.0, 0.75};

  auto reference_covariance = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  auto intel_covariance = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  ksj::stats::covariance(samples, reference_covariance);
  ASSERT_TRUE(ksj::linalg::detail::intel::covariance_centered_product(samples, intel_covariance, true));
  EXPECT_NEAR(0.0F, (as_eigen(intel_covariance) - as_eigen(reference_covariance)).norm(), kCf32Tolerance);

  auto whitening = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 2);
  as_eigen(whitening).setZero();
  whitening(0, 0) = ksj::base::cf32{0.5, 0.0};
  whitening(1, 1) = ksj::base::cf32{1.0F / 3.0F, 0.0F};
  auto reference_whitened = ksj::array::make_pooled_matrix<ksj::base::cf32>(4, 2);
  auto intel_whitened = ksj::array::make_pooled_matrix<ksj::base::cf32>(4, 2);
  ksj::linalg::detail::eigen::whiten_samples(samples, whitening, reference_whitened);
  ASSERT_TRUE(ksj::linalg::detail::intel::whiten_samples(samples, whitening, intel_whitened));
  EXPECT_NEAR(0.0F, (as_eigen(intel_whitened) - as_eigen(reference_whitened)).norm(), kCf32Tolerance);

  auto real_matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  real_matrix(0, 0) = 1.0;
  real_matrix(1, 0) = 1.0;
  real_matrix(2, 0) = 1.0;
  real_matrix(0, 1) = 0.0;
  real_matrix(1, 1) = 1.0;
  real_matrix(2, 1) = 2.0;
  auto expected = ksj::array::make_pooled_matrix<double>(2, 2);
  expected(0, 0) = 1.0;
  expected(1, 0) = 2.0;
  expected(0, 1) = 2.0;
  expected(1, 1) = 3.0;
  const auto real_rhs = ksj::linalg::matmul(real_matrix, expected);
  auto real_solution = ksj::array::make_pooled_matrix<double>(2, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_least_squares_svd(real_matrix, real_rhs, real_solution));
  EXPECT_NEAR(0.0, (as_eigen(real_solution) - as_eigen(expected)).norm(), 1.0e-12);
  ksj::linalg::LeastSquaresSvdWorkspace<double> real_svd_workspace;
  auto public_real_solution = ksj::array::make_pooled_matrix<double>(2, 2);
  ASSERT_TRUE(ksj::linalg::solve_least_squares_svd(real_matrix, real_rhs, public_real_solution, real_svd_workspace));
  EXPECT_NEAR(0.0, (as_eigen(public_real_solution) - as_eigen(expected)).norm(), 1.0e-12);
  auto real_rrqr_solution = ksj::array::make_pooled_matrix<double>(2, 2);
  ASSERT_TRUE(
    ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(real_matrix, real_rhs, real_rrqr_solution));
  EXPECT_NEAR(0.0, (as_eigen(real_rrqr_solution) - as_eigen(expected)).norm(), 1.0e-12);

  auto complex_matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(3, 2);
  complex_matrix(0, 0) = ksj::base::cf32{1.0, 0.5};
  complex_matrix(1, 0) = ksj::base::cf32{1.5, -0.25};
  complex_matrix(2, 0) = ksj::base::cf32{2.0, 0.75};
  complex_matrix(0, 1) = ksj::base::cf32{0.25, -0.5};
  complex_matrix(1, 1) = ksj::base::cf32{1.0, 0.25};
  complex_matrix(2, 1) = ksj::base::cf32{1.5, -0.75};
  auto complex_expected = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  complex_expected(0) = ksj::base::cf32{1.0, -1.0};
  complex_expected(1) = ksj::base::cf32{2.0, 0.5};
  auto complex_rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(3);
  as_eigen(complex_rhs) = as_eigen(complex_matrix) * as_eigen(complex_expected);
  auto complex_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_least_squares_svd(complex_matrix, complex_rhs, complex_solution));
  EXPECT_NEAR(0.0F, (as_eigen(complex_solution) - as_eigen(complex_expected)).norm(), kCf32Tolerance);
  ksj::linalg::LeastSquaresSvdWorkspace<ksj::base::cf32> complex_svd_workspace;
  auto public_complex_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(
    ksj::linalg::solve_least_squares_svd(complex_matrix, complex_rhs, public_complex_solution, complex_svd_workspace));
  EXPECT_NEAR(0.0F, (as_eigen(public_complex_solution) - as_eigen(complex_expected)).norm(), kCf32Tolerance);
  auto complex_rrqr_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(complex_matrix, complex_rhs,
                                                                                complex_rrqr_solution));
  EXPECT_NEAR(0.0F, (as_eigen(complex_rrqr_solution) - as_eigen(complex_expected)).norm(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, SupportsLeastSquaresSolverVariants) {
  auto matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  matrix(0, 0) = 1.0;
  matrix(1, 0) = 1.0;
  matrix(2, 0) = 1.0;
  matrix(0, 1) = 0.0;
  matrix(1, 1) = 1.0;
  matrix(2, 1) = 2.0;

  auto expected = ksj::array::make_pooled_matrix<double>(2, 2);
  expected(0, 0) = 1.0;
  expected(1, 0) = 2.0;
  expected(0, 1) = 2.0;
  expected(1, 1) = 3.0;
  const auto rhs_matrix = ksj::linalg::matmul(matrix, expected);

  for (const auto solver : {ksj::linalg::LeastSquaresSolver::qr, ksj::linalg::LeastSquaresSolver::rank_revealing_qr,
                            ksj::linalg::LeastSquaresSolver::svd, ksj::linalg::LeastSquaresSolver::normal_equations,
                            ksj::linalg::LeastSquaresSolver::normal_equations_cholesky}) {
    const auto solution = ksj::linalg::solve_least_squares(matrix, rhs_matrix, solver);
    EXPECT_NEAR(1.0, solution(0, 0), 1.0e-10);
    EXPECT_NEAR(2.0, solution(1, 0), 1.0e-10);
    EXPECT_NEAR(2.0, solution(0, 1), 1.0e-10);
    EXPECT_NEAR(3.0, solution(1, 1), 1.0e-10);
  }
}

TEST(KSpaceJetLinalg, RejectsRankDeficientNormalEquations) {
  auto matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  matrix(0, 0) = 1.0;
  matrix(1, 0) = 2.0;
  matrix(2, 0) = 3.0;
  matrix(0, 1) = 2.0;
  matrix(1, 1) = 4.0;
  matrix(2, 1) = 6.0;

  auto rhs = ksj::array::make_pooled_vector<double>(3);
  rhs(0) = 1.0;
  rhs(1) = 2.0;
  rhs(2) = 3.0;
  auto vector_output = ksj::array::make_pooled_vector<double>(2);
  vector_output(0) = -1.0;
  vector_output(1) = -1.0;

  auto qr_workspace = ksj::linalg::LeastSquaresQrWorkspace<double>{};
  auto svd_workspace = ksj::linalg::LeastSquaresSvdWorkspace<double>{};
  EXPECT_FALSE(ksj::linalg::solve_least_squares(matrix, rhs, vector_output, qr_workspace, svd_workspace,
                                                ksj::linalg::LeastSquaresSolver::normal_equations));
  EXPECT_DOUBLE_EQ(-1.0, vector_output(0));
  EXPECT_DOUBLE_EQ(-1.0, vector_output(1));

  auto rhs_matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  rhs_matrix(0, 0) = 1.0;
  rhs_matrix(1, 0) = 2.0;
  rhs_matrix(2, 0) = 3.0;
  rhs_matrix(0, 1) = -1.0;
  rhs_matrix(1, 1) = -2.0;
  rhs_matrix(2, 1) = -3.0;
  auto matrix_output = ksj::array::make_pooled_matrix<double>(2, 2);
  matrix_output(0, 0) = -1.0;
  matrix_output(1, 0) = -1.0;
  matrix_output(0, 1) = -1.0;
  matrix_output(1, 1) = -1.0;

  EXPECT_FALSE(ksj::linalg::solve_least_squares(matrix, rhs_matrix, matrix_output, qr_workspace, svd_workspace,
                                                ksj::linalg::LeastSquaresSolver::normal_equations));
  EXPECT_DOUBLE_EQ(-1.0, matrix_output(0, 0));
  EXPECT_DOUBLE_EQ(-1.0, matrix_output(1, 0));
  EXPECT_DOUBLE_EQ(-1.0, matrix_output(0, 1));
  EXPECT_DOUBLE_EQ(-1.0, matrix_output(1, 1));
}

TEST(KSpaceJetLinalg, SupportsRankDeficientLeastSquaresSolver) {
  auto matrix = ksj::array::make_pooled_matrix<double>(4, 3);
  matrix(0, 0) = 1.0;
  matrix(1, 0) = 2.0;
  matrix(2, 0) = 3.0;
  matrix(3, 0) = 4.0;
  matrix(0, 1) = 2.0;
  matrix(1, 1) = 4.0;
  matrix(2, 1) = 6.0;
  matrix(3, 1) = 8.0;
  matrix(0, 2) = 0.0;
  matrix(1, 2) = 1.0;
  matrix(2, 2) = 0.0;
  matrix(3, 2) = 1.0;

  auto expected = ksj::array::make_pooled_vector<double>(3);
  expected(0) = 1.0;
  expected(1) = -0.5;
  expected(2) = 2.0;
  auto rhs = ksj::array::make_pooled_vector<double>(4);
  as_eigen(rhs) = as_eigen(matrix) * as_eigen(expected);

  const auto solution =
    ksj::linalg::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  EXPECT_NEAR(0.0, (as_eigen(matrix) * as_eigen(solution) - as_eigen(rhs)).norm(), 1.0e-12);

  auto rhs_matrix = ksj::array::make_pooled_matrix<double>(4, 2);
  auto expected_matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  expected_matrix(0, 0) = 1.0;
  expected_matrix(1, 0) = -0.5;
  expected_matrix(2, 0) = 2.0;
  expected_matrix(0, 1) = -2.0;
  expected_matrix(1, 1) = 1.0;
  expected_matrix(2, 1) = 0.5;
  as_eigen(rhs_matrix) = as_eigen(matrix) * as_eigen(expected_matrix);

  const auto matrix_solution =
    ksj::linalg::solve_least_squares(matrix, rhs_matrix, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  EXPECT_NEAR(0.0, (as_eigen(matrix) * as_eigen(matrix_solution) - as_eigen(rhs_matrix)).norm(), 1.0e-12);

  auto intel_solution = ksj::array::make_pooled_matrix<double>(3, 2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs_matrix, intel_solution));
  EXPECT_NEAR(0.0, (as_eigen(matrix) * as_eigen(intel_solution) - as_eigen(rhs_matrix)).norm(), 1.0e-12);
}

TEST(KSpaceJetLinalg, SupportsComplexRankDeficientLeastSquaresSolver) {
  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(4, 2);
  matrix(0, 0) = ksj::base::cf32{1.0, 0.5};
  matrix(1, 0) = ksj::base::cf32{2.0, -0.25};
  matrix(2, 0) = ksj::base::cf32{3.0, 0.75};
  matrix(3, 0) = ksj::base::cf32{4.0, -0.5};
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    matrix(row, 1) = ksj::base::cf32{2.0, -1.0} * matrix(row, 0);
  }

  auto expected = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  expected(0) = ksj::base::cf32{1.0, -0.5};
  expected(1) = ksj::base::cf32{0.25, 0.75};
  auto rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  as_eigen(rhs) = as_eigen(matrix) * as_eigen(expected);

  const auto solution =
    ksj::linalg::solve_least_squares(matrix, rhs, ksj::linalg::LeastSquaresSolver::rank_revealing_qr);
  EXPECT_NEAR(0.0F, (as_eigen(matrix) * as_eigen(solution) - as_eigen(rhs)).norm(), kCf32Tolerance);

  auto intel_solution = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  ASSERT_TRUE(ksj::linalg::detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, intel_solution));
  EXPECT_NEAR(0.0F, (as_eigen(matrix) * as_eigen(intel_solution) - as_eigen(rhs)).norm(), kCf32Tolerance);
}

TEST(KSpaceJetLinalg, TransposeRotated180MatchesXformShape) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  matrix(0, 0) = 1;
  matrix(0, 1) = 2;
  matrix(0, 2) = 3;
  matrix(1, 0) = 4;
  matrix(1, 1) = 5;
  matrix(1, 2) = 6;

  const auto output = ksj::linalg::transpose_rotated_180(matrix);

  EXPECT_EQ(3U, output.rows());
  EXPECT_EQ(2U, output.cols());
  EXPECT_EQ(6, output(0, 0));
  EXPECT_EQ(5, output(1, 0));
  EXPECT_EQ(4, output(2, 0));
  EXPECT_EQ(3, output(0, 1));
  EXPECT_EQ(2, output(1, 1));
  EXPECT_EQ(1, output(2, 1));
}

TEST(KSpaceJetLinalg, RejectsDimensionMismatch) {
  auto lhs = ksj::array::make_pooled_matrix<double>(2, 3);
  auto rhs = ksj::array::make_pooled_matrix<double>(2, 3);
  EXPECT_THROW((void)ksj::linalg::matmul(lhs, rhs), std::invalid_argument);

  auto matrix = ksj::array::make_pooled_matrix<double>(2, 3);
  auto vector = ksj::array::make_pooled_vector<double>(2);
  EXPECT_THROW((void)ksj::linalg::gemv(matrix, vector), std::invalid_argument);

  auto output = ksj::array::make_pooled_vector<double>(3);
  EXPECT_THROW(ksj::linalg::scale(vector, output, 2.0), std::invalid_argument);
  EXPECT_THROW(ksj::linalg::axpy(2.0, vector, output, vector), std::invalid_argument);
  EXPECT_THROW(ksj::linalg::solve(lhs, vector, vector), std::invalid_argument);
  EXPECT_THROW((void)ksj::linalg::cholesky_lower(matrix), std::invalid_argument);
  EXPECT_THROW((void)ksj::linalg::solve_small(matrix, vector), std::invalid_argument);
  EXPECT_THROW((void)ksj::linalg::self_adjoint_eigen_decomposition(matrix), std::invalid_argument);
  EXPECT_THROW((void)ksj::linalg::eigen_decomposition(matrix), std::invalid_argument);
}

} // namespace
