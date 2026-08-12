#include "../eigen_test_adapter.hpp"
#include "kspacejet/numerics/numerics.hpp"

#include "kspacejet/base/types.hpp"

#include <cmath>
#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetNumericsHeader, AggregatesDomainModules) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  as_eigen(matrix).setIdentity();

  EXPECT_EQ(2U, matrix.rows());
  EXPECT_EQ(2U, matrix.cols());
  EXPECT_DOUBLE_EQ(1.0, matrix(0, 0));
  EXPECT_EQ(ksj::fft::Direction::forward, ksj::fft::Direction::forward);
  EXPECT_EQ(ksj::signal::ResampleKernel::linear, ksj::signal::ResampleKernel::linear);
  EXPECT_EQ(ksj::image::BorderMode::reflect, ksj::image::BorderMode::reflect);
  EXPECT_EQ(ksj::stats::VarianceNormalization::sample, ksj::stats::VarianceNormalization::sample);
  EXPECT_EQ(ksj::optimization::LeastSquaresMethod::qr, ksj::optimization::LeastSquaresMethod::qr);
  EXPECT_EQ(ksj::sparse::StorageFormat::csr, ksj::sparse::StorageFormat::csr);

  auto vector = ksj::array::make_pooled_vector<double>(3);
  vector(0) = 1.0;
  vector(1) = 2.0;
  vector(2) = 3.0;
  EXPECT_DOUBLE_EQ(2.0, ksj::stats::mean(vector));
}

TEST(KSpaceJetNumericsScalarMath, ComputesAbsoluteValueForScalarAndComplex) {
  EXPECT_EQ(7, ksj::numerics::abs_value(-7));
  EXPECT_DOUBLE_EQ(5.0, ksj::numerics::abs_value(ksj::base::cf64{3.0, 4.0}));
}

TEST(KSpaceJetNumericsScalarMath, HandlesModuloEdgeCases) {
  EXPECT_EQ(3, ksj::numerics::floor_mod(-1, 4));
  EXPECT_EQ(5, ksj::numerics::floor_mod(5, 0));
  EXPECT_DOUBLE_EQ(0.5, ksj::numerics::floor_mod(4.5, 2.0));
}

} // namespace
