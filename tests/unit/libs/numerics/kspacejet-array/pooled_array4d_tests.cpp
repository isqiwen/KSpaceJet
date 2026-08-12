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

TEST(KSpaceJetArrayPooledEigen, Array4DUsesRowMajorPooledMemory) {
  auto data = ksj::array::make_pooled_array4d<float>(2, 3, 4, 5);

  ASSERT_NE(nullptr, data.data());
  EXPECT_EQ(2U, data.dim0());
  EXPECT_EQ(3U, data.dim1());
  EXPECT_EQ(4U, data.dim2());
  EXPECT_EQ(5U, data.dim3());
  EXPECT_EQ(120U, data.size());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(data.data()) % 64U);

  data(0, 0, 0, 0) = 1.0F;
  data(1, 0, 0, 0) = 2.0F;
  data(0, 1, 0, 0) = 3.0F;
  data(1, 2, 3, 4) = 120.0F;

  EXPECT_FLOAT_EQ(1.0F, data.data()[0]);
  EXPECT_FLOAT_EQ(3.0F, data.data()[20]);
  EXPECT_FLOAT_EQ(2.0F, data.data()[60]);
  EXPECT_FLOAT_EQ(120.0F, data.data()[119]);
}

TEST(KSpaceJetArrayPooledEigen, Array4DViewExposesSubviewAlgorithmsAndFactory) {
  auto data = ksj::array::make_pooled_array4d<int>(2, 3, 4, 5);
  auto output = ksj::array::make_pooled_array4d<int>(2, 3, 4, 5);
  auto view = ksj::array::array4d_view(data);
  auto output_view = output.view();

  EXPECT_EQ(2U, view.dim0());
  EXPECT_EQ(3U, view.dim1());
  EXPECT_EQ(4U, view.dim2());
  EXPECT_EQ(5U, view.dim3());
  EXPECT_EQ(60U, view.dim0_stride());
  EXPECT_EQ(20U, view.dim1_stride());
  EXPECT_EQ(5U, view.dim2_stride());
  EXPECT_EQ(1U, view.dim3_stride());
  EXPECT_EQ(4U, view.extent(2U));
  EXPECT_EQ(20U, view.stride(1U));

  ksj::array::fill(view, 1);
  EXPECT_EQ(120, ksj::array::accumulate(view, 0));

  auto roi = view.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(2U, 4U),
                          ksj::array::slice(1U, 4U));
  ksj::array::fill(roi, 5);
  EXPECT_EQ(1U, roi.dim0());
  EXPECT_EQ(2U, roi.dim1());
  EXPECT_EQ(2U, roi.dim2());
  EXPECT_EQ(3U, roi.dim3());
  EXPECT_EQ(5, data(1U, 1U, 2U, 1U));
  EXPECT_EQ(5, data(1U, 2U, 3U, 3U));
  EXPECT_EQ(1, data(0U, 1U, 2U, 1U));

  auto selected_dim0 =
    view.subview(1U, ksj::array::slice(1U, 3U), ksj::array::slice(2U, 4U), ksj::array::slice(1U, 4U));
  auto selected_dim1 =
    view.subview(ksj::array::slice(0U, 2U), 1U, ksj::array::slice(2U, 4U), ksj::array::slice(1U, 4U));
  auto selected_dim2 =
    view.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), 2U, ksj::array::slice(1U, 4U));
  auto selected_dim3 =
    view.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(2U, 4U), 1U);
  EXPECT_EQ(2U, selected_dim0.dim0());
  EXPECT_EQ(2U, selected_dim0.dim1());
  EXPECT_EQ(3U, selected_dim0.dim2());
  EXPECT_EQ(5, selected_dim0(0U, 0U, 0U));
  EXPECT_EQ(2U, selected_dim1.dim0());
  EXPECT_EQ(2U, selected_dim1.dim1());
  EXPECT_EQ(3U, selected_dim1.dim2());
  EXPECT_EQ(5, selected_dim1(1U, 0U, 0U));
  EXPECT_EQ(2U, selected_dim2.dim0());
  EXPECT_EQ(2U, selected_dim2.dim1());
  EXPECT_EQ(3U, selected_dim2.dim2());
  EXPECT_EQ(5, selected_dim2(1U, 0U, 0U));
  EXPECT_EQ(2U, selected_dim3.dim0());
  EXPECT_EQ(2U, selected_dim3.dim1());
  EXPECT_EQ(2U, selected_dim3.dim2());
  EXPECT_EQ(5, selected_dim3(1U, 0U, 0U));

  auto fixed_01 = view.subview(1U, 1U, ksj::array::slice(2U, 4U), ksj::array::slice(1U, 4U));
  auto fixed_02 = view.subview(1U, ksj::array::slice(1U, 3U), 2U, ksj::array::slice(1U, 4U));
  auto fixed_03 = view.subview(1U, ksj::array::slice(1U, 3U), ksj::array::slice(2U, 4U), 1U);
  auto fixed_12 = view.subview(ksj::array::slice(0U, 2U), 1U, 2U, ksj::array::slice(1U, 4U));
  auto fixed_13 = view.subview(ksj::array::slice(0U, 2U), 1U, ksj::array::slice(2U, 4U), 1U);
  auto fixed_23 = view.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), 2U, 1U);
  EXPECT_EQ(2U, fixed_01.rows());
  EXPECT_EQ(3U, fixed_01.cols());
  EXPECT_EQ(5, fixed_01(0U, 0U));
  EXPECT_EQ(2U, fixed_02.rows());
  EXPECT_EQ(3U, fixed_02.cols());
  EXPECT_EQ(5, fixed_02(0U, 0U));
  EXPECT_EQ(2U, fixed_03.rows());
  EXPECT_EQ(2U, fixed_03.cols());
  EXPECT_EQ(5, fixed_03(0U, 0U));
  EXPECT_EQ(2U, fixed_12.rows());
  EXPECT_EQ(3U, fixed_12.cols());
  EXPECT_EQ(5, fixed_12(1U, 0U));
  EXPECT_EQ(2U, fixed_13.rows());
  EXPECT_EQ(2U, fixed_13.cols());
  EXPECT_EQ(5, fixed_13(1U, 0U));
  EXPECT_EQ(2U, fixed_23.rows());
  EXPECT_EQ(2U, fixed_23.cols());
  EXPECT_EQ(5, fixed_23(1U, 0U));

  auto line_dim3 = view.subview(1U, 1U, 2U, ksj::array::slice(1U, 4U));
  auto line_dim2 = view.subview(1U, 1U, ksj::array::slice(2U, 4U), 1U);
  auto line_dim1 = view.subview(1U, ksj::array::slice(1U, 3U), 2U, 1U);
  auto line_dim0 = view.subview(ksj::array::slice(0U, 2U), 1U, 2U, 1U);
  EXPECT_EQ(3U, line_dim3.size());
  EXPECT_EQ(5, line_dim3(0U));
  EXPECT_EQ(2U, line_dim2.size());
  EXPECT_EQ(5, line_dim2(0U));
  EXPECT_EQ(2U, line_dim1.size());
  EXPECT_EQ(5, line_dim1(0U));
  EXPECT_EQ(2U, line_dim0.size());
  EXPECT_EQ(1, line_dim0(0U));
  EXPECT_EQ(5, line_dim0(1U));
  EXPECT_EQ(5, view.subview(1U, 1U, 2U, 1U));
  EXPECT_EQ(5, data.subview(1U, 1U, 2U, ksj::array::slice(1U, 2U))(0U));

  ksj::array::transform(view, output_view, [](const int value) {
    return value * 2;
  });
  EXPECT_EQ(10, output(1U, 1U, 2U, 1U));
  EXPECT_EQ(2, output(0U, 0U, 0U, 0U));

  ksj::array::transform(view, output_view, output_view, [](const int lhs, const int rhs) {
    return lhs + rhs;
  });
  EXPECT_EQ(15, output(1U, 1U, 2U, 1U));
  EXPECT_EQ(3, output(0U, 0U, 0U, 0U));

  int visited = 0;
  ksj::array::for_each(roi, [&visited](int& value) {
    ++visited;
    value += 1;
  });
  EXPECT_EQ(12, visited);
  EXPECT_EQ(6, data(1U, 1U, 2U, 1U));

  auto* minimum = ksj::array::min_element(view);
  auto* maximum = ksj::array::max_element(view);
  ASSERT_NE(nullptr, minimum);
  ASSERT_NE(nullptr, maximum);
  EXPECT_EQ(1, *minimum);
  EXPECT_EQ(6, *maximum);

  auto copy_target = ksj::array::make_pooled_array4d<int>(1, 2, 2, 3);
  ksj::array::copy(roi, copy_target.view());
  EXPECT_EQ(6, copy_target(0U, 0U, 0U, 0U));
  EXPECT_EQ(6, copy_target(0U, 1U, 1U, 2U));

  auto materialized = ksj::array::make_pooled_array4d(roi);
  data(1U, 1U, 2U, 1U) = 99;
  EXPECT_EQ(6, materialized(0U, 0U, 0U, 0U));
  EXPECT_EQ(6, materialized(0U, 1U, 1U, 2U));
  EXPECT_EQ(12U, materialized.view().dim0_stride());
  EXPECT_EQ(6U, materialized.view().dim1_stride());
  EXPECT_EQ(3U, materialized.view().dim2_stride());
  EXPECT_EQ(1U, materialized.view().dim3_stride());

  const auto& const_data = data;
  const auto const_view = ksj::array::array4d_view(const_data);
  EXPECT_EQ(99, const_view(1U, 1U, 2U, 1U));
  EXPECT_TRUE(view
                .subview(ksj::array::slice(0U, 0U), ksj::array::slice(0U, 1U), ksj::array::slice(0U, 1U),
                         ksj::array::slice(0U, 1U))
                .empty());
  EXPECT_THROW((void)view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(0U, 1U), ksj::array::slice(0U, 1U),
                                  ksj::array::slice(0U, 1U)),
               std::out_of_range);
}

TEST(KSpaceJetArrayPooledEigen, Array4DExposesEigenTensorMapWithoutCopies) {
  auto data = ksj::array::make_pooled_array4d<double>(2, 2, 2, 2);
  as_eigen(data).setConstant(3.5);

  EXPECT_DOUBLE_EQ(3.5, data(0, 0, 0, 0));
  EXPECT_DOUBLE_EQ(3.5, data(1, 1, 1, 1));

  as_eigen(data)(1, 0, 1, 0) = 9.0;
  EXPECT_DOUBLE_EQ(9.0, data(1, 0, 1, 0));

  const auto& const_data = data;
  EXPECT_DOUBLE_EQ(9.0, as_eigen(const_data)(1, 0, 1, 0));

  const auto copy = ksj::array::make_pooled_array4d(data);
  data(1, 0, 1, 0) = 12.0;
  EXPECT_EQ(2U, copy.dim0());
  EXPECT_EQ(2U, copy.dim1());
  EXPECT_EQ(2U, copy.dim2());
  EXPECT_EQ(2U, copy.dim3());
  EXPECT_DOUBLE_EQ(9.0, copy(1, 0, 1, 0));
}

} // namespace
