#include "../eigen_test_adapter.hpp"
#include "kspacejet/array/array.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetArrayPooledEigen, MatrixUsesRowMajorPooledMemory) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 3);

  ASSERT_NE(nullptr, matrix.data());
  EXPECT_EQ(2U, matrix.rows());
  EXPECT_EQ(3U, matrix.cols());
  EXPECT_EQ(6U, matrix.size());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(matrix.data()) % 64U);

  matrix(0, 0) = 1.0;
  matrix(1, 0) = 2.0;
  matrix(0, 1) = 3.0;
  matrix(1, 1) = 4.0;
  matrix(0, 2) = 5.0;
  matrix(1, 2) = 6.0;

  EXPECT_DOUBLE_EQ(1.0, matrix.data()[0]);
  EXPECT_DOUBLE_EQ(3.0, matrix.data()[1]);
  EXPECT_DOUBLE_EQ(5.0, matrix.data()[2]);
  EXPECT_DOUBLE_EQ(6.0, matrix.data()[5]);
}

TEST(KSpaceJetArrayPooledEigen, MatrixExposesEigenExpressionsWithoutCopies) {
  auto lhs = ksj::array::make_pooled_matrix<double>(2, 2);
  auto rhs = ksj::array::make_pooled_matrix<double>(2, 2);
  auto output = ksj::array::make_pooled_matrix<double>(2, 2);

  as_eigen(lhs) << 1.0, 2.0, 3.0, 4.0;
  as_eigen(rhs) << 5.0, 6.0, 7.0, 8.0;
  // Keep this as an Eigen expression assignment without using a small dynamic GEMM expression:
  // GCC 14 can warn inside Eigen's product evaluator even though these pooled buffers are non-null.
  as_eigen(output).array() = as_eigen(lhs).array() + 2.0 * as_eigen(rhs).array();

  EXPECT_DOUBLE_EQ(11.0, output(0, 0));
  EXPECT_DOUBLE_EQ(17.0, output(1, 0));
  EXPECT_DOUBLE_EQ(14.0, output(0, 1));
  EXPECT_DOUBLE_EQ(20.0, output(1, 1));
}

TEST(KSpaceJetArrayPooledEigen, MatrixViewExposesRowsColumnsAndSubview) {
  auto matrix = ksj::array::make_pooled_matrix<int>(3, 4);
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<int>(row * 10U + col);
    }
  }

  auto view = ksj::array::matrix_view(matrix);
  auto row = view.row(1U);
  auto col = view.col(2U);
  row(3U) = 99;
  col(2U) = 88;

  EXPECT_EQ(4U, row.size());
  EXPECT_EQ(1U, row.stride());
  EXPECT_EQ(3U, col.size());
  EXPECT_EQ(4U, col.stride());
  EXPECT_EQ(1U, view.col_stride());
  EXPECT_EQ(99, matrix(1, 3));
  EXPECT_EQ(88, matrix(2, 2));

  auto roi = view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  EXPECT_EQ(2U, roi.rows());
  EXPECT_EQ(2U, roi.cols());
  EXPECT_EQ(4U, roi.row_stride());
  EXPECT_EQ(1U, roi.col_stride());
  EXPECT_EQ(11, roi(0, 0));
  EXPECT_EQ(88, roi(1, 1));
  EXPECT_EQ(3U, view.rows());
  EXPECT_EQ(4U, view.cols());

  auto nested = roi.subview(ksj::array::slice(1U, 2U), ksj::array::slice(0U, 2U));
  EXPECT_EQ(1U, nested.rows());
  EXPECT_EQ(2U, nested.cols());
  EXPECT_EQ(21, nested(0, 0));
  EXPECT_EQ(88, nested(0, 1));
  EXPECT_EQ(2U, roi.rows());
  EXPECT_EQ(2U, roi.cols());

  auto tagged_roi = view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  auto tagged_row = view.subview(1U, ksj::array::slice(1U, 4U));
  auto tagged_col = view.subview(ksj::array::slice(0U, 3U), 2U);
  EXPECT_EQ(2U, tagged_roi.rows());
  EXPECT_EQ(2U, tagged_roi.cols());
  EXPECT_EQ(11, tagged_roi(0U, 0U));
  EXPECT_EQ(3U, tagged_row.size());
  EXPECT_EQ(11, tagged_row(0U));
  EXPECT_EQ(99, tagged_row(2U));
  EXPECT_EQ(3U, tagged_col.size());
  EXPECT_EQ(2, tagged_col(0U));
  EXPECT_EQ(88, tagged_col(2U));
  EXPECT_EQ(99, view.subview(1U, 3U));

  EXPECT_TRUE(view.subview(ksj::array::slice(0U, 0U), ksj::array::slice(0U, 1U)).empty());
  EXPECT_THROW((void)view.subview(ksj::array::slice(2U, 4U), ksj::array::slice(0U, 1U)), std::out_of_range);
}

} // namespace
