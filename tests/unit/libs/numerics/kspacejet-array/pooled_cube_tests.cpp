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

TEST(KSpaceJetArrayPooledEigen, CubeViewExposesSubviewAndAlgorithms) {
  auto cube = ksj::array::make_pooled_cube<int>(2, 3, 4);
  auto output = ksj::array::make_pooled_cube<int>(2, 3, 4);
  auto view = ksj::array::cube_view(cube);
  auto output_view = ksj::array::cube_view(output);

  EXPECT_EQ(2U, view.dim0());
  EXPECT_EQ(3U, view.dim1());
  EXPECT_EQ(4U, view.dim2());
  EXPECT_EQ(12U, view.dim0_stride());
  EXPECT_EQ(4U, view.dim1_stride());
  EXPECT_EQ(1U, view.dim2_stride());
  EXPECT_EQ(3U, view.subview(0U, ksj::array::_, ksj::array::_).rows());
  EXPECT_EQ(4U, view.subview(0U, ksj::array::_, ksj::array::_).cols());
  EXPECT_EQ(2U, view.subview(ksj::array::_, 1U, ksj::array::_).rows());
  EXPECT_EQ(4U, view.subview(ksj::array::_, 1U, ksj::array::_).cols());
  EXPECT_EQ(2U, view.subview(ksj::array::_, ksj::array::_, 2U).rows());
  EXPECT_EQ(3U, view.subview(ksj::array::_, ksj::array::_, 2U).cols());
  EXPECT_EQ(12U, view.subview(ksj::array::_, ksj::array::_, 2U).row_stride());
  EXPECT_EQ(4U, view.subview(ksj::array::_, ksj::array::_, 2U).col_stride());

  ksj::array::fill(view, 1);
  EXPECT_EQ(24, ksj::array::accumulate(view, 0));

  ksj::array::fill(view.subview(ksj::array::_, ksj::array::_, 3U), 9);
  EXPECT_EQ(9, cube(0U, 0U, 3U));
  EXPECT_EQ(9, cube(1U, 2U, 3U));
  EXPECT_EQ(54, ksj::array::accumulate(view.subview(ksj::array::_, ksj::array::_, 3U), 0));

  auto stepped = view.subview(ksj::array::_, ksj::array::slice(0U, 3U, 2U), 3U);
  EXPECT_EQ(2U, stepped.rows());
  EXPECT_EQ(2U, stepped.cols());
  EXPECT_EQ(12U, stepped.row_stride());
  EXPECT_EQ(8U, stepped.col_stride());
  EXPECT_EQ(9, stepped(0U, 0U));
  EXPECT_EQ(9, stepped(1U, 1U));

  auto roi = view.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  ksj::array::fill(roi, 5);
  EXPECT_EQ(88, ksj::array::accumulate(view, 0));
  EXPECT_EQ(5, cube(1, 1, 1));
  EXPECT_EQ(5, cube(1, 2, 2));
  EXPECT_EQ(1, cube(0, 1, 1));

  ksj::array::transform(view, output_view, [](const int value) {
    return value * 2;
  });
  EXPECT_EQ(10, output(1, 1, 1));
  EXPECT_EQ(2, output(0, 0, 0));
  EXPECT_EQ(18, output(0, 0, 3));

  ksj::array::transform(view, output_view, output_view, [](const int lhs, const int rhs) {
    return lhs + rhs;
  });
  EXPECT_EQ(15, output(1, 1, 1));
  EXPECT_EQ(3, output(0, 0, 0));
  EXPECT_EQ(27, output(0, 0, 3));

  int visited = 0;
  ksj::array::for_each(roi, [&visited](int& value) {
    ++visited;
    value += 1;
  });
  EXPECT_EQ(4, visited);
  EXPECT_EQ(6, cube(1, 1, 1));

  auto* minimum = ksj::array::min_element(view);
  auto* maximum = ksj::array::max_element(view);
  ASSERT_NE(nullptr, minimum);
  ASSERT_NE(nullptr, maximum);
  EXPECT_EQ(1, *minimum);
  EXPECT_EQ(9, *maximum);

  auto copy_target = ksj::array::make_pooled_cube<int>(1, 2, 2);
  ksj::array::copy(roi, ksj::array::cube_view(copy_target));
  EXPECT_EQ(6, copy_target(0, 0, 0));
  EXPECT_EQ(6, copy_target(0, 1, 1));

  auto product_lhs = ksj::array::make_pooled_cube<int>(2, 2, 3);
  auto product_rhs = ksj::array::make_pooled_cube<int>(2, 2, 3);
  auto product_output = ksj::array::make_pooled_matrix<int>(2, 2);
  for (std::size_t row = 0; row < product_lhs.dim0(); ++row) {
    for (std::size_t col = 0; col < product_lhs.dim1(); ++col) {
      for (std::size_t slice = 0; slice < product_lhs.dim2(); ++slice) {
        product_lhs(row, col, slice) = static_cast<int>(row + 10U * col + 100U * slice);
        product_rhs(row, col, slice) = 1;
      }
    }
  }
  ksj::array::sum_product_across(product_lhs, product_rhs, product_output, ksj::array::Dim::dim2);
  EXPECT_EQ(300, product_output(0U, 0U));
  EXPECT_EQ(303, product_output(1U, 0U));
  EXPECT_EQ(330, product_output(0U, 1U));
  EXPECT_EQ(333, product_output(1U, 1U));

  auto row_major_indexes = ksj::array::make_pooled_vector<std::size_t>(2U);
  const auto row_major_count = ksj::array::find_linear_indices(
    product_lhs,
    [](const int value) {
      return value == 1 || value == 110;
    },
    row_major_indexes);
  ASSERT_EQ(2U, row_major_count);
  EXPECT_EQ(4U, row_major_indexes(0U));
  EXPECT_EQ(6U, row_major_indexes(1U));

  auto tagged_cube = view.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  auto fixed_row = view.subview(1U, ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  auto fixed_col = view.subview(ksj::array::slice(0U, 2U), 1U, ksj::array::slice(1U, 3U));
  auto fixed_slice = view.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), 2U);
  EXPECT_EQ(2U, tagged_cube.dim0());
  EXPECT_EQ(2U, tagged_cube.dim1());
  EXPECT_EQ(2U, tagged_cube.dim2());
  EXPECT_EQ(2U, fixed_row.rows());
  EXPECT_EQ(2U, fixed_row.cols());
  EXPECT_EQ(6, fixed_row(0U, 0U));
  EXPECT_EQ(2U, fixed_col.rows());
  EXPECT_EQ(2U, fixed_col.cols());
  EXPECT_EQ(6, fixed_col(1U, 0U));
  EXPECT_EQ(2U, fixed_slice.rows());
  EXPECT_EQ(2U, fixed_slice.cols());
  EXPECT_EQ(6, fixed_slice(1U, 0U));

  auto slice_line = view.subview(1U, 1U, ksj::array::slice(1U, 3U));
  auto col_line = view.subview(1U, ksj::array::slice(1U, 3U), 2U);
  auto row_line = view.subview(ksj::array::slice(0U, 2U), 1U, 2U);
  EXPECT_EQ(2U, slice_line.size());
  EXPECT_EQ(6, slice_line(0U));
  EXPECT_EQ(6, slice_line(1U));
  EXPECT_EQ(2U, col_line.size());
  EXPECT_EQ(6, col_line(0U));
  EXPECT_EQ(6, col_line(1U));
  EXPECT_EQ(2U, row_line.size());
  EXPECT_EQ(1, row_line(0U));
  EXPECT_EQ(6, row_line(1U));
  EXPECT_EQ(6, view.subview(1U, 1U, 2U));

  const auto& const_cube = cube;
  const auto const_view = ksj::array::cube_view(const_cube);
  EXPECT_EQ(6, const_view(1U, 1U, 1U));
  EXPECT_TRUE(view.subview(ksj::array::slice(0U, 0U), ksj::array::slice(0U, 1U), ksj::array::slice(0U, 1U)).empty());
  EXPECT_THROW((void)view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(0U, 1U), ksj::array::slice(0U, 1U)),
               std::out_of_range);
}

TEST(KSpaceJetArrayPooledEigen, CubeUsesRowMajorPooledMemory) {
  auto cube = ksj::array::make_pooled_cube<float>(2, 3, 4);

  ASSERT_NE(nullptr, cube.data());
  EXPECT_EQ(2U, cube.dim0());
  EXPECT_EQ(3U, cube.dim1());
  EXPECT_EQ(4U, cube.dim2());
  EXPECT_EQ(24U, cube.size());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(cube.data()) % 64U);

  cube(0, 0, 0) = 1.0F;
  cube(1, 0, 0) = 2.0F;
  cube(0, 1, 0) = 3.0F;
  cube(1, 2, 3) = 24.0F;

  EXPECT_FLOAT_EQ(1.0F, cube.data()[0]);
  EXPECT_FLOAT_EQ(3.0F, cube.data()[4]);
  EXPECT_FLOAT_EQ(2.0F, cube.data()[12]);
  EXPECT_FLOAT_EQ(24.0F, cube.data()[23]);
}

TEST(KSpaceJetArrayPooledEigen, CubeExposesEigenTensorMapWithoutCopies) {
  auto cube = ksj::array::make_pooled_cube<double>(2, 2, 2);
  as_eigen(cube).setConstant(2.5);

  EXPECT_DOUBLE_EQ(2.5, cube(0, 0, 0));
  EXPECT_DOUBLE_EQ(2.5, cube(1, 1, 1));

  as_eigen(cube)(1, 0, 1) = 7.0;
  EXPECT_DOUBLE_EQ(7.0, cube(1, 0, 1));

  const auto& const_cube = cube;
  EXPECT_DOUBLE_EQ(7.0, as_eigen(const_cube)(1, 0, 1));
}

} // namespace
