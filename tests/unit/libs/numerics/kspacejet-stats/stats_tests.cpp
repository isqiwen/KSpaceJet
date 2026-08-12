#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/stats.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

template <typename T> [[nodiscard]] ksj::array::VectorView<const T> const_view(const T* data, const std::size_t size) {
  return ksj::array::VectorView<const T>(data, size);
}

TEST(KSpaceJetStats, ComputesMeanAndVariance) {
  auto values = ksj::array::make_pooled_vector<double>(3);
  values(0) = 1.0;
  values(1) = 2.0;
  values(2) = 3.0;

  EXPECT_DOUBLE_EQ(6.0, ksj::stats::sum(values));
  EXPECT_DOUBLE_EQ(2.0, ksj::stats::mean(values));
  EXPECT_DOUBLE_EQ(2.0, ksj::stats::mean(values.view()));
  EXPECT_DOUBLE_EQ(1.0, ksj::stats::variance(values, ksj::stats::VarianceNormalization::sample));
  EXPECT_DOUBLE_EQ(1.0, ksj::stats::variance(values.view(), ksj::stats::VarianceNormalization::sample));
  EXPECT_NEAR(2.0 / 3.0, ksj::stats::variance(values, ksj::stats::VarianceNormalization::population), 1e-12);
}

TEST(KSpaceJetStats, ComputesRmseForRealDifferences) {
  const double diff[] = {3.0, 4.0};

  EXPECT_NEAR(std::sqrt(12.5), ksj::stats::rmse(const_view(diff, 2U)), 1.0e-12);
}

TEST(KSpaceJetStats, ComputesRmseBetweenComplexArrays) {
  const ksj::base::cf64 data[] = {{2.0, 1.0}, {3.0, -1.0}};
  const ksj::base::cf64 reference[] = {{1.0, 1.0}, {3.0, 1.0}};

  EXPECT_NEAR(std::sqrt(2.5), ksj::stats::rmse(const_view(data, 2U), const_view(reference, 2U)), 1.0e-12);
}

TEST(KSpaceJetStats, ComparesArraysWithExplicitPrecision) {
  const double data[] = {1.0, 2.0};
  const double reference[] = {1.0 + 1.0e-7, 2.0};

  EXPECT_TRUE(ksj::stats::equal(const_view(data, 2U), const_view(reference, 2U), 1.0e-6));
  EXPECT_FALSE(ksj::stats::equal(const_view(data, 2U), const_view(reference, 2U), 1.0e-8));
}

TEST(KSpaceJetStats, FindsExtremaIndexesAndValues) {
  const double data[] = {-2.0, 7.0, 4.0, -9.0};

  EXPECT_EQ(1, ksj::stats::max_index(const_view(data, 4U)));
  EXPECT_EQ(3, ksj::stats::min_index(const_view(data, 4U)));
  EXPECT_DOUBLE_EQ(7.0, ksj::stats::max_value(const_view(data, 4U)));
  EXPECT_DOUBLE_EQ(-9.0, ksj::stats::min_value(const_view(data, 4U)));
}

TEST(KSpaceJetStats, FindsComplexExtremaByMagnitude) {
  const ksj::base::cf64 data[] = {{1.0, 0.0}, {3.0, 4.0}, {2.0, 0.0}};

  EXPECT_EQ(1, ksj::stats::max_index(const_view(data, 3U)));
  EXPECT_EQ(0, ksj::stats::min_index(const_view(data, 3U)));
  EXPECT_EQ((ksj::base::cf64{3.0, 4.0}), ksj::stats::max_value(const_view(data, 3U)));
  EXPECT_EQ((ksj::base::cf64{1.0, 0.0}), ksj::stats::min_value(const_view(data, 3U)));
}

TEST(KSpaceJetStats, ComputesViewSums) {
  auto values = ksj::array::make_pooled_vector<float>(4);
  values(0) = -1.0F;
  values(1) = 2.0F;
  values(2) = -3.0F;
  values(3) = 4.0F;

  EXPECT_FLOAT_EQ(2.0F, ksj::stats::sum(values.view()));
  EXPECT_FLOAT_EQ(10.0F, ksj::stats::sum_abs(values.view()));
  EXPECT_FLOAT_EQ(10.0F, ksj::stats::sum_abs(values));

  const auto every_other = ksj::array::as_const_view(values.view()).subview(ksj::array::slice(0U, values.size(), 2U));
  EXPECT_FLOAT_EQ(-4.0F, ksj::stats::sum(every_other));
}

TEST(KSpaceJetStats, ComputesOtsuThresholdFromVectorView) {
  auto values = ksj::array::make_pooled_vector<float>(8);
  values(0) = 0.0F;
  values(1) = 0.0F;
  values(2) = 0.0F;
  values(3) = 0.0F;
  values(4) = 2.0F;
  values(5) = 2.0F;
  values(6) = 2.0F;
  values(7) = 2.0F;

  const auto threshold = ksj::stats::otsu_threshold(values.view(), 256U);

  EXPECT_GE(threshold, 0.0F);
  EXPECT_LT(threshold, 2.0F);
  EXPECT_FLOAT_EQ(threshold, ksj::stats::otsu_threshold(values, 256U));
}

TEST(KSpaceJetStats, OtsuThresholdReturnsConstantInputValue) {
  auto values = ksj::array::make_pooled_vector<float>(3);
  values(0) = 4.0F;
  values(1) = 4.0F;
  values(2) = 4.0F;

  EXPECT_FLOAT_EQ(4.0F, ksj::stats::otsu_threshold(values));
}

TEST(KSpaceJetStats, ComputesLinearFitFromVectorViews) {
  auto x = ksj::array::make_pooled_vector<double>(4);
  auto y = ksj::array::make_pooled_vector<double>(4);
  x(0) = 0.0;
  x(1) = 1.0;
  x(2) = 2.0;
  x(3) = 3.0;
  y(0) = 1.0;
  y(1) = 3.0;
  y(2) = 5.0;
  y(3) = 7.0;

  const auto fit = ksj::stats::linear_fit(x.view(), y.view());
  ASSERT_TRUE(fit.has_value());
  EXPECT_NEAR(2.0, fit->slope, 1.0e-12);
  EXPECT_NEAR(1.0, fit->intercept, 1.0e-12);

  const auto pooled_fit = ksj::stats::linear_fit(x, y);
  ASSERT_TRUE(pooled_fit.has_value());
  EXPECT_NEAR(fit->slope, pooled_fit->slope, 1.0e-12);
  EXPECT_NEAR(fit->intercept, pooled_fit->intercept, 1.0e-12);
}

TEST(KSpaceJetStats, LinearFitRejectsSingularInput) {
  auto x = ksj::array::make_pooled_vector<double>(3);
  auto y = ksj::array::make_pooled_vector<double>(3);
  x(0) = 2.0;
  x(1) = 2.0;
  x(2) = 2.0;
  y(0) = 1.0;
  y(1) = 2.0;
  y(2) = 3.0;

  EXPECT_FALSE(ksj::stats::linear_fit(x, y).has_value());
}

TEST(KSpaceJetStats, ComputesComplexViewSum) {
  auto values = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  values(0) = {1.0F, -2.0F};
  values(1) = {3.0F, 4.0F};

  const auto output = ksj::stats::sum(values.view());
  EXPECT_FLOAT_EQ(4.0F, output.real());
  EXPECT_FLOAT_EQ(2.0F, output.imag());
  EXPECT_NEAR(std::sqrt(5.0F) + 5.0F, ksj::stats::sum_abs(values.view()), 1.0e-6F);
}

TEST(KSpaceJetStats, ComputesCovariance) {
  auto lhs = ksj::array::make_pooled_vector<double>(3);
  auto rhs = ksj::array::make_pooled_vector<double>(3);
  lhs(0) = 1.0;
  lhs(1) = 2.0;
  lhs(2) = 3.0;
  rhs(0) = 2.0;
  rhs(1) = 4.0;
  rhs(2) = 6.0;

  EXPECT_DOUBLE_EQ(2.0, ksj::stats::covariance(lhs, rhs, ksj::stats::VarianceNormalization::sample));
}

TEST(KSpaceJetStats, ComputesMatrixCovariance) {
  auto samples = ksj::array::make_pooled_matrix<double>(3, 2);
  samples(0, 0) = 1.0;
  samples(1, 0) = 2.0;
  samples(2, 0) = 3.0;
  samples(0, 1) = 2.0;
  samples(1, 1) = 4.0;
  samples(2, 1) = 6.0;

  const auto covariance = ksj::stats::covariance(samples);

  ASSERT_EQ(2U, covariance.rows());
  ASSERT_EQ(2U, covariance.cols());
  EXPECT_NEAR(1.0, covariance(0, 0), 1.0e-12);
  EXPECT_NEAR(2.0, covariance(0, 1), 1.0e-12);
  EXPECT_NEAR(2.0, covariance(1, 0), 1.0e-12);
  EXPECT_NEAR(4.0, covariance(1, 1), 1.0e-12);

  auto output = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::stats::covariance(samples.view(), output.view());
  EXPECT_NEAR(covariance(0, 0), output(0, 0), 1.0e-12);
  EXPECT_NEAR(covariance(0, 1), output(0, 1), 1.0e-12);
  EXPECT_NEAR(covariance(1, 0), output(1, 0), 1.0e-12);
  EXPECT_NEAR(covariance(1, 1), output(1, 1), 1.0e-12);

  ksj::stats::covariance(samples, output);
  EXPECT_NEAR(covariance(0, 0), output(0, 0), 1.0e-12);
  EXPECT_NEAR(covariance(0, 1), output(0, 1), 1.0e-12);
  EXPECT_NEAR(covariance(1, 0), output(1, 0), 1.0e-12);
  EXPECT_NEAR(covariance(1, 1), output(1, 1), 1.0e-12);
}

TEST(KSpaceJetStats, ComputesComplexMatrixCovariance) {
  auto samples = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  samples(0, 0) = ksj::base::cf64{1.0, 1.0};
  samples(1, 0) = ksj::base::cf64{3.0, -1.0};
  samples(0, 1) = ksj::base::cf64{2.0, 0.0};
  samples(1, 1) = ksj::base::cf64{4.0, 0.0};

  const auto covariance = ksj::stats::covariance(samples);

  EXPECT_NEAR(4.0, covariance(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(0.0, covariance(0, 0).imag(), 1.0e-12);
  EXPECT_NEAR(2.0, covariance(0, 1).real(), 1.0e-12);
  EXPECT_NEAR(2.0, covariance(0, 1).imag(), 1.0e-12);
  EXPECT_NEAR(2.0, covariance(1, 0).real(), 1.0e-12);
  EXPECT_NEAR(-2.0, covariance(1, 0).imag(), 1.0e-12);
  EXPECT_NEAR(2.0, covariance(1, 1).real(), 1.0e-12);
  EXPECT_NEAR(0.0, covariance(1, 1).imag(), 1.0e-12);
}

TEST(KSpaceJetStats, ComputesRootSumOfSquaresForVectorsAndCubes) {
  auto vector = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  vector(0) = {3.0, 4.0};
  vector(1) = {0.0, 12.0};

  EXPECT_DOUBLE_EQ(169.0, ksj::stats::sum_of_squares(vector));
  EXPECT_DOUBLE_EQ(13.0, ksj::stats::root_sum_of_squares(vector));

  auto cube = ksj::array::make_pooled_cube<ksj::base::cf32>(2, 2, 2);
  cube(0, 0, 0) = {3.0F, 4.0F};
  cube(0, 0, 1) = {0.0F, 12.0F};
  cube(1, 0, 0) = {1.0F, 0.0F};
  cube(1, 0, 1) = {2.0F, 0.0F};
  cube(0, 1, 0) = {0.0F, 0.0F};
  cube(0, 1, 1) = {0.0F, 0.0F};
  cube(1, 1, 0) = {6.0F, 8.0F};
  cube(1, 1, 1) = {0.0F, 0.0F};

  const auto rss = ksj::stats::root_sum_of_squares_across(cube, ksj::array::Dim::dim2);

  ASSERT_EQ(2U, rss.rows());
  ASSERT_EQ(2U, rss.cols());
  EXPECT_FLOAT_EQ(13.0F, rss(0, 0));
  EXPECT_FLOAT_EQ(std::sqrt(5.0F), rss(1, 0));
  EXPECT_FLOAT_EQ(0.0F, rss(0, 1));
  EXPECT_FLOAT_EQ(10.0F, rss(1, 1));
}

TEST(KSpaceJetStats, ComputesMaximumMagnitudeAndVectorDistances) {
  auto lhs = ksj::array::make_pooled_vector<double>(3);
  auto rhs = ksj::array::make_pooled_vector<double>(3);
  lhs(0) = -1.0;
  lhs(1) = 2.0;
  lhs(2) = -3.0;
  rhs(0) = 1.0;
  rhs(1) = -2.0;
  rhs(2) = -1.0;

  EXPECT_DOUBLE_EQ(3.0, ksj::stats::max_abs(lhs));
  EXPECT_DOUBLE_EQ(8.0, ksj::stats::l1_distance(lhs, rhs));
  EXPECT_DOUBLE_EQ(std::sqrt(24.0), ksj::stats::l2_distance(lhs, rhs));
  EXPECT_DOUBLE_EQ(4.0, ksj::stats::linf_distance(lhs, rhs));
}

TEST(KSpaceJetStats, ComputesComplexMaximumMagnitudeAndVectorDistances) {
  auto lhs = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  auto rhs = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  lhs(0) = {3.0, 4.0};
  lhs(1) = {1.0, 1.0};
  rhs(0) = {0.0, 0.0};
  rhs(1) = {1.0, -1.0};

  EXPECT_DOUBLE_EQ(5.0, ksj::stats::max_abs(lhs));
  EXPECT_DOUBLE_EQ(7.0, ksj::stats::l1_distance(lhs, rhs));
  EXPECT_DOUBLE_EQ(std::sqrt(29.0), ksj::stats::l2_distance(lhs, rhs));
  EXPECT_DOUBLE_EQ(5.0, ksj::stats::linf_distance(lhs, rhs));
}

TEST(KSpaceJetStats, NormDistancesSupportEmptyAndStridedViews) {
  const auto empty = ksj::array::VectorView<const float>(nullptr, 0U);
  EXPECT_FLOAT_EQ(0.0F, ksj::stats::max_abs(empty));
  EXPECT_FLOAT_EQ(0.0F, ksj::stats::l1_distance(empty, empty));
  EXPECT_FLOAT_EQ(0.0F, ksj::stats::l2_distance(empty, empty));
  EXPECT_FLOAT_EQ(0.0F, ksj::stats::linf_distance(empty, empty));

  const float lhs_storage[] = {-1.0F, 99.0F, 2.0F, 99.0F, -3.0F};
  const float rhs_storage[] = {1.0F, 99.0F, -2.0F, 99.0F, -1.0F};
  const auto lhs = const_view(lhs_storage, 5U).subview(ksj::array::slice(0U, 5U, 2U));
  const auto rhs = const_view(rhs_storage, 5U).subview(ksj::array::slice(0U, 5U, 2U));

  EXPECT_FLOAT_EQ(3.0F, ksj::stats::max_abs(lhs));
  EXPECT_FLOAT_EQ(8.0F, ksj::stats::l1_distance(lhs, rhs));
  EXPECT_FLOAT_EQ(std::sqrt(24.0F), ksj::stats::l2_distance(lhs, rhs));
  EXPECT_FLOAT_EQ(4.0F, ksj::stats::linf_distance(lhs, rhs));
}

TEST(KSpaceJetStats, NormDistancesRejectDimensionMismatch) {
  const double lhs[] = {1.0, 2.0};
  const double rhs[] = {1.0};

  EXPECT_THROW(static_cast<void>(ksj::stats::l1_distance(const_view(lhs, 2U), const_view(rhs, 1U))),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(ksj::stats::l2_distance(const_view(lhs, 2U), const_view(rhs, 1U))),
               std::invalid_argument);
  EXPECT_THROW(static_cast<void>(ksj::stats::linf_distance(const_view(lhs, 2U), const_view(rhs, 1U))),
               std::invalid_argument);
}

TEST(KSpaceJetStats, ComputesCenteredMagnitudeAverageForPooledAndStridedCubeViews) {
  auto cube = ksj::array::make_pooled_cube<float>(3, 3, 2);
  for (std::size_t slice = 0; slice < cube.dim2(); ++slice) {
    for (std::size_t col = 0; col < cube.dim1(); ++col) {
      for (std::size_t row = 0; row < cube.dim0(); ++row) {
        cube(row, col, slice) = static_cast<float>(row + 10U * col + 100U * slice);
      }
    }
  }

  EXPECT_FLOAT_EQ(61.0F, ksj::stats::centered_magnitude_average(cube, 1U, 1U));

  std::array<float, 12> strided_storage{0.0F, 100.0F, 10.0F, 110.0F, -1.0F, -1.0F,
                                        1.0F, 101.0F, 11.0F, 111.0F, -1.0F, -1.0F};
  const auto strided_view = ksj::array::CubeView<const float>(strided_storage.data(), 2U, 3U, 2U)
                              .subview(ksj::array::_, ksj::array::slice(0U, 2U), ksj::array::_);

  EXPECT_FLOAT_EQ(55.5F, ksj::stats::centered_magnitude_average(strided_view, 2U, 2U));
}

TEST(KSpaceJetStats, ComputesSquaredL2NormAndDistanceForCubeViews) {
  auto lhs = ksj::array::make_pooled_cube<ksj::base::cf32>(2, 2, 2);
  auto rhs = ksj::array::make_pooled_cube<ksj::base::cf32>(2, 2, 2);
  for (std::size_t slice = 0; slice < lhs.dim2(); ++slice) {
    for (std::size_t col = 0; col < lhs.dim1(); ++col) {
      for (std::size_t row = 0; row < lhs.dim0(); ++row) {
        const auto value = static_cast<float>(row + 10U * col + 100U * slice);
        lhs(row, col, slice) = {value, value + 1.0F};
        rhs(row, col, slice) = {value - 1.0F, value + 3.0F};
      }
    }
  }

  EXPECT_FLOAT_EQ(90584.0F, ksj::stats::squared_l2_norm(lhs));
  EXPECT_FLOAT_EQ(40.0F, ksj::stats::squared_l2_distance(lhs, rhs));

  std::array<float, 12> strided_storage{1.0F, 2.0F, 3.0F, 4.0F, -1.0F, -1.0F, 5.0F, 6.0F, 7.0F, 8.0F, -1.0F, -1.0F};
  const auto strided_view = ksj::array::CubeView<const float>(strided_storage.data(), 2U, 3U, 2U)
                              .subview(ksj::array::_, ksj::array::slice(0U, 2U), ksj::array::_);

  EXPECT_FLOAT_EQ(204.0F, ksj::stats::squared_l2_norm(strided_view));
}

TEST(KSpaceJetStats, WritesSliceReductionsIntoExistingStorage) {
  auto cube = ksj::array::make_pooled_cube<ksj::base::cf32>(2, 2, 2);
  cube(0, 0, 0) = {3.0F, 4.0F};
  cube(0, 0, 1) = {0.0F, 12.0F};
  cube(1, 0, 0) = {1.0F, 0.0F};
  cube(1, 0, 1) = {2.0F, 0.0F};
  cube(0, 1, 0) = {0.0F, 0.0F};
  cube(0, 1, 1) = {0.0F, 0.0F};
  cube(1, 1, 0) = {6.0F, 8.0F};
  cube(1, 1, 1) = {0.0F, 0.0F};

  auto sos = ksj::array::make_pooled_matrix<float>(2, 2);
  ksj::stats::sum_of_squares_across(cube, sos, ksj::array::Dim::dim2);
  EXPECT_FLOAT_EQ(169.0F, sos(0, 0));
  EXPECT_FLOAT_EQ(5.0F, sos(1, 0));
  EXPECT_FLOAT_EQ(0.0F, sos(0, 1));
  EXPECT_FLOAT_EQ(100.0F, sos(1, 1));

  ksj::stats::root_sum_of_squares_across(cube, sos, ksj::array::Dim::dim2);
  EXPECT_FLOAT_EQ(13.0F, sos(0, 0));
  EXPECT_FLOAT_EQ(std::sqrt(5.0F), sos(1, 0));
  EXPECT_FLOAT_EQ(0.0F, sos(0, 1));
  EXPECT_FLOAT_EQ(10.0F, sos(1, 1));
}

TEST(KSpaceJetStats, RejectsSliceReductionOutputDimensionMismatch) {
  auto cube = ksj::array::make_pooled_cube<ksj::base::cf32>(2, 2, 2);
  auto wrong_rows = ksj::array::make_pooled_matrix<float>(3, 2);
  auto wrong_cols = ksj::array::make_pooled_matrix<float>(2, 3);

  EXPECT_THROW(ksj::stats::sum_of_squares_across(cube, wrong_rows, ksj::array::Dim::dim2), std::invalid_argument);
  EXPECT_THROW(ksj::stats::root_sum_of_squares_across(cube, wrong_cols, ksj::array::Dim::dim2), std::invalid_argument);
}

TEST(KSpaceJetStats, RejectsMatrixCovarianceDimensionMismatch) {
  auto samples = ksj::array::make_pooled_matrix<double>(2, 3);
  auto wrong_covariance = ksj::array::make_pooled_matrix<double>(2, 2);

  EXPECT_THROW(ksj::stats::covariance(samples, wrong_covariance), std::invalid_argument);
}

} // namespace
