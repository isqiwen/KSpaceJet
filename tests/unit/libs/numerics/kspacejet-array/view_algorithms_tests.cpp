#include "../eigen_test_adapter.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/array/detail/intel/intel_array_elementwise.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetArrayPooledEigen, MatrixViewSupportsMultiInputTransformAndAccumulateIf) {
  auto first = ksj::array::make_pooled_matrix<int>(2, 3);
  auto second = ksj::array::make_pooled_matrix<int>(2, 3);
  auto third = ksj::array::make_pooled_matrix<int>(2, 3);
  auto fourth = ksj::array::make_pooled_matrix<int>(2, 3);
  auto fifth = ksj::array::make_pooled_matrix<int>(2, 3);
  auto output = ksj::array::make_pooled_matrix<int>(2, 3);

  for (std::size_t row = 0; row < first.rows(); ++row) {
    for (std::size_t col = 0; col < first.cols(); ++col) {
      const auto value = static_cast<int>(row * first.cols() + col + 1U);
      first(row, col) = value;
      second(row, col) = value * 10;
      third(row, col) = value * 100;
      fourth(row, col) = value * 1000;
      fifth(row, col) = value * 10000;
    }
  }

  ksj::array::transform(first.view(), second.view(), third.view(), output.view(),
                        [](const int a, const int b, const int c) {
                          return a + b + c;
                        });
  EXPECT_EQ(111, output(0U, 0U));
  EXPECT_EQ(666, output(1U, 2U));

  ksj::array::transform(first, second, third, fourth, output, [](const int a, const int b, const int c, const int d) {
    return a + b + c + d;
  });
  EXPECT_EQ(1111, output(0U, 0U));
  EXPECT_EQ(6666, output(1U, 2U));

  EXPECT_EQ(19998, ksj::array::accumulate_if(output.view(), 0, [](const int value) {
              return value > 3000;
            }));
  EXPECT_EQ(9999, ksj::array::accumulate_if(output, 0, [](const int value) {
              return value % 2 != 0;
            }));

  ksj::array::transform(first.view(), second.view(), third.view(), fourth.view(), fifth.view(), output.view(),
                        [](const int a, const int b, const int c, const int d, const int e) {
                          return a + b + c + d + e;
                        });
  EXPECT_EQ(11111, output(0U, 0U));
  EXPECT_EQ(66666, output(1U, 2U));

  ksj::array::transform(first, second, third, fourth, fifth, output,
                        [](const int a, const int b, const int c, const int d, const int e) {
                          return e - d - c - b - a;
                        });
  EXPECT_EQ(8889, output(0U, 0U));
  EXPECT_EQ(53334, output(1U, 2U));

  auto roi_first = ksj::array::make_pooled_matrix<int>(2, 5);
  auto roi_second = ksj::array::make_pooled_matrix<int>(2, 5);
  auto roi_third = ksj::array::make_pooled_matrix<int>(2, 5);
  auto roi_fourth = ksj::array::make_pooled_matrix<int>(2, 5);
  auto roi_fifth = ksj::array::make_pooled_matrix<int>(2, 5);
  auto roi_output = ksj::array::make_pooled_matrix<int>(2, 5);
  for (std::size_t row = 0; row < roi_first.rows(); ++row) {
    for (std::size_t col = 0; col < roi_first.cols(); ++col) {
      const auto value = static_cast<int>(row * roi_first.cols() + col);
      roi_first(row, col) = value;
      roi_second(row, col) = value + 10;
      roi_third(row, col) = value + 20;
      roi_fourth(row, col) = value + 30;
      roi_fifth(row, col) = value + 40;
    }
  }

  const auto roi_cols = ksj::array::slice(1U, 4U);
  const auto first_roi = roi_first.view().subview(ksj::array::_, roi_cols);
  const auto second_roi = roi_second.view().subview(ksj::array::_, roi_cols);
  const auto third_roi = roi_third.view().subview(ksj::array::_, roi_cols);
  const auto fourth_roi = roi_fourth.view().subview(ksj::array::_, roi_cols);
  const auto fifth_roi = roi_fifth.view().subview(ksj::array::_, roi_cols);
  const auto output_roi = roi_output.view().subview(ksj::array::_, roi_cols);
  ASSERT_FALSE(first_roi.is_contiguous());
  ASSERT_EQ(1U, first_roi.col_stride());

  ksj::array::transform(first_roi, second_roi, third_roi, output_roi, [](const int a, const int b, const int c) {
    return a + b + c;
  });
  EXPECT_EQ(33, roi_output(0U, 1U));
  EXPECT_EQ(54, roi_output(1U, 3U));

  ksj::array::transform(first_roi, second_roi, third_roi, fourth_roi, output_roi,
                        [](const int a, const int b, const int c, const int d) {
                          return a + b + c + d;
                        });
  EXPECT_EQ(64, roi_output(0U, 1U));
  EXPECT_EQ(92, roi_output(1U, 3U));

  ksj::array::transform(first_roi, second_roi, third_roi, fourth_roi, fifth_roi, output_roi,
                        [](const int a, const int b, const int c, const int d, const int e) {
                          return a + b + c + d + e;
                        });
  EXPECT_EQ(105, roi_output(0U, 1U));
  EXPECT_EQ(140, roi_output(1U, 3U));
}

TEST(KSpaceJetArrayPooledEigen, CommonArrayApisSupportDimShapeIndexedZipAndSelection) {
  auto matrix = ksj::array::make_pooled_matrix<int>(3U, 4U);
  ksj::array::transform_indexed(matrix.view(), matrix.view(), [](const ksj::array::Index<2U> index, const int) {
    return static_cast<int>(index[0U] * 10U + index[1U]);
  });

  EXPECT_EQ(3U, matrix.shape()[0U]);
  EXPECT_EQ(4U, matrix.extent(ksj::array::Dim::dim1));
  EXPECT_EQ(12, matrix(1U, 2U));

  std::size_t linear_index_sum = 0U;
  ksj::array::for_each_linear_indexed(matrix.view(), [&linear_index_sum](const std::size_t index, const int) {
    linear_index_sum += index;
  });
  EXPECT_EQ(66U, linear_index_sum);

  auto ones = ksj::array::make_pooled_matrix<int>(3U, 4U);
  ones.set_constant(1);
  ksj::array::for_each_zip(matrix.view(), ones.view(), [](int& value, const int one) {
    value += one;
  });
  EXPECT_EQ(13, matrix(1U, 2U));

  ksj::array::for_each_zip(matrix.view().subview(ksj::array::_, ksj::array::slice(1U, 3U)),
                           ones.view().subview(ksj::array::_, ksj::array::slice(1U, 3U)),
                           [](int& value, const int one) {
                             value += one * 2;
                           });
  EXPECT_EQ(15, matrix(1U, 2U));

  std::size_t block_count = 0U;
  std::size_t element_count = 0U;
  ksj::array::for_each_contiguous_block(matrix.view().subview(ksj::array::_, ksj::array::slice(1U, 3U)),
                                        [&](const std::span<int> block) {
                                          ++block_count;
                                          element_count += block.size();
                                        });
  EXPECT_EQ(3U, block_count);
  EXPECT_EQ(6U, element_count);

  const auto flattened = ksj::array::flatten_view(matrix.view());
  EXPECT_EQ(matrix.size(), flattened.size());
  EXPECT_EQ(matrix(2U, 3U), flattened(11U));
  EXPECT_THROW((void)ksj::array::flatten_view(matrix.view().subview(ksj::array::_, ksj::array::slice(0U, 4U, 2U))),
               std::invalid_argument);

  const auto reshaped = ksj::array::reshape_view(matrix.view(), ksj::array::Shape<3U>(2U, 2U, 3U));
  EXPECT_EQ(2U, reshaped.dim0());
  EXPECT_EQ(3U, reshaped.dim2());
  EXPECT_EQ(matrix(1U, 2U), reshaped(1U, 0U, 0U));

  auto mask = ksj::array::make_pooled_matrix<int>(3U, 4U);
  ksj::array::transform_indexed(mask.view(), mask.view(), [](const ksj::array::Index<2U> index, const int) {
    return index[1U] % 2U == 0U ? 1 : 0;
  });
  auto selected = ksj::array::make_pooled_matrix<int>(3U, 4U);
  ksj::array::where(mask.view(), matrix.view(), ones.view(), selected.view());
  EXPECT_EQ(matrix(1U, 2U), selected(1U, 2U));
  EXPECT_EQ(1, selected(1U, 3U));

  auto clamped = ksj::array::make_pooled_matrix<int>(3U, 4U);
  ksj::array::clip(matrix.view(), clamped.view(), 5, 15);
  EXPECT_EQ(5, clamped(0U, 0U));
  EXPECT_EQ(15, clamped(2U, 3U));

  auto real_values = ksj::array::make_pooled_vector<float>(4U);
  real_values(0U) = -2.0F;
  real_values(1U) = 0.5F;
  real_values(2U) = 3.0F;
  real_values(3U) = 8.0F;
  auto real_clamped = ksj::array::make_pooled_vector<float>(4U);
  ksj::array::clamp(real_values.view(), real_clamped.view(), 0.0F, 4.0F);
  EXPECT_FLOAT_EQ(0.0F, real_clamped(0U));
  EXPECT_FLOAT_EQ(0.5F, real_clamped(1U));
  EXPECT_FLOAT_EQ(3.0F, real_clamped(2U));
  EXPECT_FLOAT_EQ(4.0F, real_clamped(3U));

  auto floats = ksj::array::make_pooled_vector<float>(3U);
  floats(0U) = 1.0F;
  floats(1U) = std::numeric_limits<float>::quiet_NaN();
  floats(2U) = std::numeric_limits<float>::infinity();
  auto finite = ksj::array::make_pooled_vector<int>(3U);
  ksj::array::isfinite(floats.view(), finite.view());
  EXPECT_EQ(1, finite(0U));
  EXPECT_EQ(0, finite(1U));
  EXPECT_EQ(0, finite(2U));
  auto no_nan = ksj::array::make_pooled_vector<float>(3U);
  ksj::array::replace_nan(floats.view(), no_nan.view(), -1.0F);
  EXPECT_FLOAT_EQ(-1.0F, no_nan(1U));
}

TEST(KSpaceJetArrayPooledEigen, ElementwiseMathApisSupportVectorAndGenericViews) {
  auto input = ksj::array::make_pooled_vector<float>(4U);
  input(0U) = 1.0F;
  input(1U) = 4.0F;
  input(2U) = 9.0F;
  input(3U) = 16.0F;
  auto rhs = ksj::array::make_pooled_vector<float>(4U);
  rhs(0U) = 2.0F;
  rhs(1U) = 3.0F;
  rhs(2U) = 8.0F;
  rhs(3U) = 20.0F;
  auto output = ksj::array::make_pooled_vector<float>(4U);

  ksj::array::subtract_scalar(input.view(), 1.0F, output.view());
  EXPECT_FLOAT_EQ(0.0F, output(0U));
  EXPECT_FLOAT_EQ(15.0F, output(3U));

  ksj::array::scalar_subtract(input.view(), 20.0F, output.view());
  EXPECT_FLOAT_EQ(19.0F, output(0U));
  EXPECT_FLOAT_EQ(4.0F, output(3U));

  ksj::array::divide_scalar(input.view(), 2.0F, output.view());
  EXPECT_FLOAT_EQ(0.5F, output(0U));
  EXPECT_FLOAT_EQ(8.0F, output(3U));

  ksj::array::scalar_divide(input.view(), 16.0F, output.view());
  EXPECT_FLOAT_EQ(16.0F, output(0U));
  EXPECT_FLOAT_EQ(1.0F, output(3U));

  ksj::array::negate(input.view(), output.view());
  EXPECT_FLOAT_EQ(-1.0F, output(0U));
  EXPECT_FLOAT_EQ(-16.0F, output(3U));

  ksj::array::sqrt(input.view(), output.view());
  EXPECT_FLOAT_EQ(1.0F, output(0U));
  EXPECT_FLOAT_EQ(4.0F, output(3U));

  ksj::array::square(input.view(), output.view());
  EXPECT_FLOAT_EQ(1.0F, output(0U));
  EXPECT_FLOAT_EQ(256.0F, output(3U));

  ksj::array::minimum(input.view(), rhs.view(), output.view());
  EXPECT_FLOAT_EQ(1.0F, output(0U));
  EXPECT_FLOAT_EQ(16.0F, output(3U));

  ksj::array::maximum(input.view(), rhs.view(), output.view());
  EXPECT_FLOAT_EQ(2.0F, output(0U));
  EXPECT_FLOAT_EQ(20.0F, output(3U));

  auto matrix = ksj::array::make_pooled_matrix<double>(2U, 2U);
  matrix(0U, 0U) = 1.0;
  matrix(0U, 1U) = -2.0;
  matrix(1U, 0U) = 3.0;
  matrix(1U, 1U) = -4.0;
  auto abs_matrix = ksj::array::make_pooled_matrix<double>(2U, 2U);
  ksj::array::absolute(matrix.view(), abs_matrix.view());
  EXPECT_DOUBLE_EQ(2.0, abs_matrix(0U, 1U));
  EXPECT_DOUBLE_EQ(4.0, abs_matrix(1U, 1U));

  auto complex_input = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  complex_input(0U) = {3.0F, 4.0F};
  complex_input(1U) = {5.0F, 12.0F};
  auto magnitude = ksj::array::make_pooled_vector<float>(2U);
  ksj::array::absolute(complex_input.view(), magnitude.view());
  EXPECT_FLOAT_EQ(5.0F, magnitude(0U));
  EXPECT_FLOAT_EQ(13.0F, magnitude(1U));
}

TEST(KSpaceJetArrayPooledEigen, RealVectorDivideCoversMklVmlPolicyRanges) {
  auto lhs_float = ksj::array::make_pooled_vector<float>(4096U);
  auto rhs_float = ksj::array::make_pooled_vector<float>(4096U);
  auto output_float = ksj::array::make_pooled_vector<float>(4096U);
  for (std::size_t index = 0U; index < lhs_float.size(); ++index) {
    lhs_float(index) = static_cast<float>((index % 257U) + 1U) * 0.5F;
    rhs_float(index) = static_cast<float>((index % 17U) + 3U) * 0.25F;
  }

  ksj::array::divide(lhs_float.view(), rhs_float.view(), output_float.view());
  for (std::size_t index = 0U; index < lhs_float.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_NEAR(lhs_float(index) / rhs_float(index), output_float(index), 1.0e-6F);
  }

  auto lhs_double = ksj::array::make_pooled_vector<double>(1024U);
  auto rhs_double = ksj::array::make_pooled_vector<double>(1024U);
  auto output_double = ksj::array::make_pooled_vector<double>(1024U);
  for (std::size_t index = 0U; index < lhs_double.size(); ++index) {
    lhs_double(index) = static_cast<double>((index % 257U) + 1U) * 0.5;
    rhs_double(index) = static_cast<double>((index % 17U) + 3U) * 0.25;
  }

  ksj::array::divide(lhs_double.view(), rhs_double.view(), output_double.view());
  for (std::size_t index = 0U; index < lhs_double.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_NEAR(lhs_double(index) / rhs_double(index), output_double(index), 1.0e-12);
  }
}

TEST(KSpaceJetArrayPooledEigen, GenericTakeScatterAndDimReductionsUseRowMajorLinearSemantics) {
  auto cube = ksj::array::make_pooled_cube<int>(2U, 3U, 4U);
  ksj::array::transform_linear_indexed(cube.view(), cube.view(), [](const std::size_t index, const int) {
    return static_cast<int>(index);
  });

  auto indices = ksj::array::make_pooled_vector<std::size_t>(3U);
  indices(0U) = 0U;
  indices(1U) = 5U;
  indices(2U) = 23U;
  const auto taken = ksj::array::take(cube.view(), indices.view());
  EXPECT_EQ(0, taken(0U));
  EXPECT_EQ(5, taken(1U));
  EXPECT_EQ(23, taken(2U));

  auto scattered = ksj::array::make_pooled_cube<int>(2U, 3U, 4U);
  scattered.set_zero();
  ksj::array::scatter(taken.view(), indices.view(), scattered.view());
  EXPECT_EQ(5, scattered(0U, 1U, 1U));
  EXPECT_EQ(23, scattered(1U, 2U, 3U));

  const auto sum_dim2 = ksj::array::sum(cube.view(), ksj::array::Dim::dim2);
  EXPECT_EQ(6, sum_dim2(0U, 0U));
  EXPECT_EQ(86, sum_dim2(1U, 2U));

  const auto min_dim1 = ksj::array::min(cube.view(), ksj::array::Dim::dim1);
  const auto max_dim0 = ksj::array::max(cube.view(), ksj::array::Dim::dim0);
  EXPECT_EQ(0, min_dim1(0U, 0U));
  EXPECT_EQ(20, max_dim0(2U, 0U));

  const auto argmax_dim2 = ksj::array::argmax(cube.view(), ksj::array::Dim::dim2);
  EXPECT_EQ(3U, argmax_dim2(1U, 2U));
}

TEST(KSpaceJetArrayPooledEigen, ReductionPrimitivesCoverMagnitudeDistanceCloseAndCubeNormAcross) {
  auto lhs = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  lhs(0U) = {3.0F, 4.0F};
  lhs(1U) = {1.0F, -1.0F};
  rhs(0U) = {1.0F, 4.0F};
  rhs(1U) = {1.0F, 1.0F};

  EXPECT_NEAR(5.0F + std::sqrt(2.0F), ksj::array::sum_abs(lhs.view()), 1.0e-6F);
  EXPECT_NEAR(8.0F, ksj::array::squared_distance(lhs.view(), rhs.view()), 1.0e-6F);
  EXPECT_TRUE(ksj::array::all_close(lhs.view(), rhs.view(), 2.1F));
  EXPECT_FALSE(ksj::array::all_close(lhs.view(), rhs.view(), 1.9F));

  auto cube = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 2U, 3U);
  for (std::size_t i0 = 0U; i0 < cube.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < cube.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < cube.dim2(); ++i2) {
        cube(i0, i1, i2) = {static_cast<float>(i0 + 1U), static_cast<float>(i1 + i2)};
      }
    }
  }

  auto output = ksj::array::make_pooled_matrix<float>(2U, 2U);
  ksj::array::squared_norm_across(cube.view().subview(ksj::array::_, ksj::array::_, ksj::array::slice(0U, 3U, 2U)),
                                  output.view(), ksj::array::Dim::dim2);
  EXPECT_FLOAT_EQ(6.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(12.0F, output(0U, 1U));
  EXPECT_FLOAT_EQ(12.0F, output(1U, 0U));
  EXPECT_FLOAT_EQ(18.0F, output(1U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, VectorViewFindsTrueRunBounds) {
  auto input = ksj::array::make_pooled_vector<float>(10U);
  const std::array<float, 10U> values{0.0F, 2.0F, 3.0F, 0.0F, 4.0F, 5.0F, 6.0F, 0.0F, 7.0F, 0.0F};
  ksj::array::copy(ksj::array::VectorView<const float>(values.data(), values.size()), input.view());

  const auto bounds = ksj::array::find_true_run_bounds<int>(
    input.view(),
    [](const float value) {
      return value > 1.0F;
    },
    2U);

  ASSERT_EQ(4U, bounds.size());
  EXPECT_EQ(1, bounds(0U));
  EXPECT_EQ(2, bounds(1U));
  EXPECT_EQ(4, bounds(2U));
  EXPECT_EQ(6, bounds(3U));
}

TEST(KSpaceJetArrayPooledEigen, CubeViewForwardDifferenceUsesZeroBoundary) {
  auto input = ksj::array::make_pooled_cube<int>(2U, 3U, 2U);
  auto output = ksj::array::make_pooled_cube<int>(2U, 3U, 2U);
  for (std::size_t row = 0U; row < input.dim0(); ++row) {
    for (std::size_t col = 0U; col < input.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
        input(row, col, slice) = static_cast<int>(row * 100U + col * 10U + slice);
      }
    }
  }

  ksj::array::forward_difference(input, output, 0U, ksj::array::DifferenceBoundary::zero);
  EXPECT_EQ(100, output(0U, 1U, 1U));
  EXPECT_EQ(0, output(1U, 1U, 1U));

  ksj::array::forward_difference(input.view(), output.view(), 1U, ksj::array::DifferenceBoundary::zero);
  EXPECT_EQ(10, output(1U, 0U, 1U));
  EXPECT_EQ(0, output(1U, 2U, 1U));

  ksj::array::forward_difference(input.view(), output.view(), 2U, ksj::array::DifferenceBoundary::zero);
  EXPECT_EQ(1, output(1U, 2U, 0U));
  EXPECT_EQ(0, output(1U, 2U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, CubeViewAdjointForwardDifferenceUsesZeroBoundary) {
  auto input = ksj::array::make_pooled_cube<int>(3U, 1U, 1U);
  auto output = ksj::array::make_pooled_cube<int>(3U, 1U, 1U);
  input(0U, 0U, 0U) = 2;
  input(1U, 0U, 0U) = 5;
  input(2U, 0U, 0U) = 11;

  ksj::array::adjoint_forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::zero);

  EXPECT_EQ(-2, output(0U, 0U, 0U));
  EXPECT_EQ(-3, output(1U, 0U, 0U));
  EXPECT_EQ(5, output(2U, 0U, 0U));
}

TEST(KSpaceJetArrayPooledEigen, CubeViewDifferenceUsesPeriodicBoundary) {
  auto input = ksj::array::make_pooled_cube<int>(3U, 1U, 1U);
  auto output = ksj::array::make_pooled_cube<int>(3U, 1U, 1U);
  input(0U, 0U, 0U) = 2;
  input(1U, 0U, 0U) = 5;
  input(2U, 0U, 0U) = 11;

  ksj::array::forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::periodic);
  EXPECT_EQ(3, output(0U, 0U, 0U));
  EXPECT_EQ(6, output(1U, 0U, 0U));
  EXPECT_EQ(-9, output(2U, 0U, 0U));

  ksj::array::adjoint_forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::periodic);
  EXPECT_EQ(9, output(0U, 0U, 0U));
  EXPECT_EQ(-3, output(1U, 0U, 0U));
  EXPECT_EQ(-6, output(2U, 0U, 0U));
}

TEST(KSpaceJetArrayPooledEigen, Array4DViewRawFactoryUsesRowMajorStrides) {
  std::array<int, 2U * 3U * 4U * 5U> values{};
  std::iota(values.begin(), values.end(), 0);

  const auto view = ksj::array::array4d_view(values.data(), 2U, 3U, 4U, 5U);

  EXPECT_EQ(60U, view.dim0_stride());
  EXPECT_EQ(20U, view.dim1_stride());
  EXPECT_EQ(5U, view.dim2_stride());
  EXPECT_EQ(1U, view.dim3_stride());
  EXPECT_EQ(0, view(0U, 0U, 0U, 0U));
  EXPECT_EQ(119, view(1U, 2U, 3U, 4U));
}

TEST(KSpaceJetArrayPooledEigen, Array4DViewDifferenceUsesPeriodicAndZeroBoundaries) {
  auto input = ksj::array::make_pooled_array4d<int>(2U, 2U, 3U, 2U);
  auto output = ksj::array::make_pooled_array4d<int>(2U, 2U, 3U, 2U);
  for (std::size_t echo = 0U; echo < input.dim0(); ++echo) {
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          input(echo, row, col, slice) = static_cast<int>(echo * 1000U + row * 100U + col * 10U + slice);
        }
      }
    }
  }

  ksj::array::forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::periodic);
  EXPECT_EQ(1000, output(0U, 1U, 2U, 1U));
  EXPECT_EQ(-1000, output(1U, 1U, 2U, 1U));

  ksj::array::adjoint_forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::periodic);
  EXPECT_EQ(1000, output(0U, 1U, 2U, 1U));
  EXPECT_EQ(-1000, output(1U, 1U, 2U, 1U));

  ksj::array::forward_difference(input.view(), output.view(), 3U, ksj::array::DifferenceBoundary::zero);
  EXPECT_EQ(1, output(1U, 1U, 2U, 0U));
  EXPECT_EQ(0, output(1U, 1U, 2U, 1U));

  ksj::array::adjoint_forward_difference(input.view(), output.view(), 3U, ksj::array::DifferenceBoundary::zero);
  EXPECT_EQ(-1120, output(1U, 1U, 2U, 0U));
  EXPECT_EQ(1120, output(1U, 1U, 2U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, CopyChannelEchoVolumeExtractsRowMajorEcho) {
  constexpr std::size_t channels = 2U;
  constexpr std::size_t echoes = 3U;
  constexpr std::size_t rows = 2U;
  constexpr std::size_t cols = 3U;
  constexpr std::size_t slices = 2U;
  constexpr std::size_t volume_size = rows * cols * slices;
  std::array<int, channels * echoes * volume_size> source{};
  for (std::size_t channel = 0U; channel < channels; ++channel) {
    for (std::size_t echo = 0U; echo < echoes; ++echo) {
      for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t col = 0U; col < cols; ++col) {
          for (std::size_t slice = 0U; slice < slices; ++slice) {
            const auto source_index =
              channel * echoes * volume_size + echo * volume_size + (row * cols + col) * slices + slice;
            source[source_index] = static_cast<int>(channel * 10000U + echo * 1000U + row * 100U + col * 10U + slice);
          }
        }
      }
    }
  }

  auto output = ksj::array::make_pooled_array4d<int>(channels, rows, cols, slices);
  const auto source_view = ksj::array::Array4DView<const int>(source.data(), channels, echoes, rows, cols * slices);
  ksj::array::copy_block_split(source_view, ksj::array::Dim::dim1, 1U, ksj::array::Dim::dim3, output.view());

  EXPECT_EQ(1000, output(0U, 0U, 0U, 0U));
  EXPECT_EQ(1121, output(0U, 1U, 2U, 1U));
  EXPECT_EQ(11000, output(1U, 0U, 0U, 0U));
  EXPECT_EQ(11121, output(1U, 1U, 2U, 1U));
  EXPECT_THROW(
    ksj::array::copy_block_split(source_view, ksj::array::Dim::dim1, echoes, ksj::array::Dim::dim3, output.view()),
    std::out_of_range);
}

TEST(KSpaceJetArrayPooledEigen, ChannelLinePresenceMaskBroadcastsDetectedLines) {
  auto channels = ksj::array::make_pooled_array4d<ksj::base::cf32>(2U, 2U, 3U, 2U);
  auto mask = ksj::array::make_pooled_cube<float>(2U, 3U, 2U);
  channels.set_zero();

  channels(1U, 0U, 2U, 1U) = {0.25F, 0.0F};
  channels(0U, 1U, 0U, 0U) = {0.02F, 0.0F};

  ksj::array::broadcast_abs_presence_mask(channels.view(), mask.view(), ksj::array::Dim::dim1, 0.1F);

  EXPECT_EQ(0.0F, mask(0U, 0U, 0U));
  EXPECT_EQ(1.0F, mask(0U, 0U, 1U));
  EXPECT_EQ(1.0F, mask(0U, 1U, 1U));
  EXPECT_EQ(1.0F, mask(0U, 2U, 1U));
  EXPECT_EQ(0.0F, mask(1U, 0U, 0U));
  EXPECT_EQ(0.0F, mask(1U, 2U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, ChannelVolumeOperationsUseSeparateInputAndOutput) {
  auto channels = ksj::array::make_pooled_array4d<ksj::base::cf32>(2U, 2U, 2U, 2U);
  auto volume = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 2U, 2U);
  auto real_volume = ksj::array::make_pooled_cube<float>(2U, 2U, 2U);
  auto weighted = ksj::array::make_pooled_array4d<ksj::base::cf32>(2U, 2U, 2U, 2U);
  auto combined = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 2U, 2U);

  for (std::size_t index = 0U; index < channels.size(); ++index) {
    channels[index] = {static_cast<float>(index + 1U), static_cast<float>(index % 3U)};
  }
  for (std::size_t index = 0U; index < volume.size(); ++index) {
    volume[index] = {static_cast<float>(index + 1U), 0.0F};
    real_volume[index] = static_cast<float>(index + 2U);
  }

  ksj::array::multiply_cube_by_array4d(volume.view(), channels.view(), weighted.view());
  EXPECT_EQ(volume(1U, 0U, 1U) * channels(0U, 1U, 0U, 1U), weighted(0U, 1U, 0U, 1U));

  auto masked = ksj::array::make_pooled_array4d<ksj::base::cf32>(2U, 2U, 2U, 2U);
  ksj::array::multiply_array4d_by_cube(channels.view(), real_volume.view(), masked.view());
  EXPECT_EQ(channels(1U, 0U, 1U, 0U) * real_volume(0U, 1U, 0U), masked(1U, 0U, 1U, 0U));

  ksj::array::reduce_conjugate_product(channels.view(), channels.view(), combined.view(), ksj::array::Dim::dim0);
  const auto expected_norm = std::norm(channels(0U, 1U, 1U, 0U)) + std::norm(channels(1U, 1U, 1U, 0U));
  EXPECT_NEAR(expected_norm, combined(1U, 1U, 0U).real(), 1.0e-5F);
  EXPECT_NEAR(0.0F, combined(1U, 1U, 0U).imag(), 1.0e-5F);

  auto corrected = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 2U, 2U);
  ksj::array::multiply_cube_by_abs_sum_squared(volume.view(), channels.view(), corrected.view(), ksj::array::Dim::dim0);
  const auto magnitude_sum = std::abs(channels(0U, 0U, 1U, 1U)) + std::abs(channels(1U, 0U, 1U, 1U));
  EXPECT_EQ(volume(0U, 1U, 1U) * magnitude_sum * magnitude_sum, corrected(0U, 1U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, ComplexSoftThresholdShrinksByMagnitude) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf32>(4);
  input(0U) = {3.0F, 4.0F};
  input(1U) = {0.1F, 0.0F};
  input(2U) = {1.0F, 0.0F};
  input(3U) = {};

  auto output = ksj::array::make_pooled_vector<ksj::base::cf32>(input.size());
  ksj::array::soft_threshold(ksj::array::as_const_view(input.view()), output.view(), 1.0F, 0.2F);

  EXPECT_NEAR(2.4F, output(0U).real(), 1.0e-6F);
  EXPECT_NEAR(3.2F, output(0U).imag(), 1.0e-6F);
  EXPECT_EQ(ksj::base::cf32{}, output(1U));
  EXPECT_EQ(ksj::base::cf32{}, output(2U));
  EXPECT_EQ(ksj::base::cf32{}, output(3U));

  const auto returned = ksj::array::soft_threshold(input, 1.0F, 0.2F);
  EXPECT_NEAR(output(0U).real(), returned(0U).real(), 1.0e-6F);
  EXPECT_NEAR(output(0U).imag(), returned(0U).imag(), 1.0e-6F);
}

TEST(KSpaceJetArrayPooledEigen, MatrixViewComputesForwardDifferenceStatsWithStridedRows) {
  std::array<double, 8> values{1.0, 4.0, 7.0, -1.0, 3.0, 8.0, 11.0, -1.0};
  std::array<int, 8> mask{0, 1, 1, -1, 1, 1, 0, -1};
  const auto matrix =
    ksj::array::MatrixView<const double>(values.data(), 2U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U));
  const auto mask_view =
    ksj::array::MatrixView<const int>(mask.data(), 2U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U));

  const auto row_stats =
    ksj::array::forward_difference_stats_below_threshold(matrix, mask_view, ksj::array::MatrixDifferenceAxis::row, 5.0);
  const auto column_stats = ksj::array::forward_difference_stats_below_threshold(
    matrix, mask_view, ksj::array::MatrixDifferenceAxis::column, 5.0);

  EXPECT_DOUBLE_EQ(6.0, row_stats.sum);
  EXPECT_EQ(2U, row_stats.count);
  EXPECT_DOUBLE_EQ(3.0, row_stats.mean());
  EXPECT_DOUBLE_EQ(6.0, column_stats.sum);
  EXPECT_EQ(2U, column_stats.count);
}

TEST(KSpaceJetArrayPooledEigen, ViewEigenAdaptersBorrowStridedStorage) {
  auto vector = ksj::array::make_pooled_vector<double>(5);
  for (std::size_t index = 0; index < vector.size(); ++index) {
    vector(index) = static_cast<double>(index + 1U);
  }

  auto even_indices = vector.view().subview(ksj::array::slice(0U, vector.size(), 2U));
  auto eigen_vector = as_eigen(even_indices);
  EXPECT_DOUBLE_EQ(9.0, eigen_vector.sum());
  eigen_vector(1) = 20.0;
  EXPECT_DOUBLE_EQ(20.0, vector(2U));

  auto matrix = ksj::array::make_pooled_matrix<double>(3, 4);
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<double>(row * 10U + col);
    }
  }

  auto middle = matrix.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  auto eigen_matrix = as_eigen(middle);
  EXPECT_DOUBLE_EQ(11.0, eigen_matrix(0, 0));
  EXPECT_DOUBLE_EQ(22.0, eigen_matrix(1, 1));
  eigen_matrix(0, 1) = 77.0;
  EXPECT_DOUBLE_EQ(77.0, matrix(1U, 2U));

  const auto& const_matrix = matrix;
  const auto const_middle = const_matrix.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  const auto const_eigen_matrix = as_eigen(const_middle);
  EXPECT_DOUBLE_EQ(77.0, const_eigen_matrix(0, 1));
}

TEST(KSpaceJetArrayPooledEigen, AsConstViewBorrowsStorageAndPreservesStrides) {
  auto vector = ksj::array::make_pooled_vector<int>(5);
  auto vector_line = vector.view().subview(ksj::array::slice(0U, vector.size(), 2U));
  const auto const_vector_line = ksj::array::as_const_view(vector_line);
  EXPECT_EQ(vector_line.data(), const_vector_line.data());
  EXPECT_EQ(3U, const_vector_line.size());
  EXPECT_EQ(2U, const_vector_line.stride());

  auto matrix = ksj::array::make_pooled_matrix<int>(3, 4);
  auto matrix_roi = matrix.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  const auto const_matrix_roi = ksj::array::as_const_view(matrix_roi);
  EXPECT_EQ(matrix_roi.data(), const_matrix_roi.data());
  EXPECT_EQ(2U, const_matrix_roi.rows());
  EXPECT_EQ(2U, const_matrix_roi.cols());
  EXPECT_EQ(4U, const_matrix_roi.row_stride());
  EXPECT_EQ(1U, const_matrix_roi.col_stride());

  auto image = ksj::array::make_pooled_image<int>(3, 4);
  auto image_roi = image.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  const auto const_image_roi = ksj::array::as_const_view(image_roi);
  EXPECT_EQ(image_roi.data(), const_image_roi.data());
  EXPECT_EQ(2U, const_image_roi.height());
  EXPECT_EQ(2U, const_image_roi.width());
  EXPECT_EQ(4U, const_image_roi.row_stride());

  auto cube = ksj::array::make_pooled_cube<int>(2, 3, 4);
  auto cube_roi = cube.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  const auto const_cube_roi = ksj::array::as_const_view(cube_roi);
  EXPECT_EQ(cube_roi.data(), const_cube_roi.data());
  EXPECT_EQ(1U, const_cube_roi.dim0());
  EXPECT_EQ(2U, const_cube_roi.dim1());
  EXPECT_EQ(2U, const_cube_roi.dim2());
  EXPECT_EQ(12U, const_cube_roi.dim0_stride());
  EXPECT_EQ(4U, const_cube_roi.dim1_stride());
  EXPECT_EQ(1U, const_cube_roi.dim2_stride());

  auto array4d = ksj::array::make_pooled_array4d<int>(2, 3, 4, 5);
  auto array4d_roi = array4d.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U),
                                     ksj::array::slice(1U, 4U));
  const auto const_array4d_roi = ksj::array::as_const_view(array4d_roi);
  EXPECT_EQ(array4d_roi.data(), const_array4d_roi.data());
  EXPECT_EQ(1U, const_array4d_roi.dim0());
  EXPECT_EQ(2U, const_array4d_roi.dim1());
  EXPECT_EQ(2U, const_array4d_roi.dim2());
  EXPECT_EQ(3U, const_array4d_roi.dim3());
  EXPECT_EQ(60U, const_array4d_roi.dim0_stride());
  EXPECT_EQ(20U, const_array4d_roi.dim1_stride());
  EXPECT_EQ(5U, const_array4d_roi.dim2_stride());
  EXPECT_EQ(1U, const_array4d_roi.dim3_stride());
}

TEST(KSpaceJetArrayPooledEigen, PooledContainersExposeCommonContiguousAccessors) {
  auto vector = ksj::array::make_pooled_vector<int>(4);
  for (std::size_t index = 0; index < vector.size(); ++index) {
    vector[index] = static_cast<int>(index + 1U);
  }
  auto* vector_data = vector.data();
  const auto vector_capacity = vector.capacity();
  EXPECT_EQ(4U, vector.extent());
  EXPECT_EQ(1U, vector.stride());
  EXPECT_EQ(4U * sizeof(int), vector.size_bytes());
  EXPECT_GE(vector_capacity, vector.size());
  EXPECT_GE(vector.capacity_bytes(), vector.size_bytes());
  EXPECT_TRUE(vector.is_contiguous());
  EXPECT_EQ(1, vector.front());
  EXPECT_EQ(4, vector.back());
  EXPECT_EQ(10, std::accumulate(vector.begin(), vector.end(), 0));
  EXPECT_THROW((void)vector(4U), std::out_of_range);
  vector.resize(2U);
  EXPECT_EQ(2U, vector.size());
  EXPECT_EQ(vector_data, vector.data());
  EXPECT_EQ(vector_capacity, vector.capacity());
  EXPECT_EQ(2U, vector.buffer().size());
  vector.clear();
  EXPECT_TRUE(vector.empty());
  EXPECT_EQ(0U, vector.buffer().size());
  EXPECT_EQ(vector_data, vector.data());
  EXPECT_EQ(vector_capacity, vector.capacity());
  vector.release();
  EXPECT_EQ(0U, vector.capacity());

  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    matrix[index] = static_cast<int>(index + 1U);
  }
  auto* matrix_data = matrix.data();
  const auto matrix_capacity = matrix.capacity();
  EXPECT_EQ(2U, matrix.extent(0U));
  EXPECT_EQ(3U, matrix.extent(1U));
  EXPECT_EQ(3U, matrix.row_stride());
  EXPECT_EQ(1U, matrix.col_stride());
  EXPECT_EQ(3U, matrix.stride(0U));
  EXPECT_EQ(1U, matrix.stride(1U));
  EXPECT_EQ(6U * sizeof(int), matrix.size_bytes());
  EXPECT_GE(matrix_capacity, matrix.size());
  EXPECT_GE(matrix.capacity_bytes(), matrix.size_bytes());
  EXPECT_TRUE(matrix.is_contiguous());
  EXPECT_EQ(1, matrix.front());
  EXPECT_EQ(6, matrix.back());
  EXPECT_EQ(6, matrix(1U, 2U));
  EXPECT_EQ(21, std::accumulate(matrix.begin(), matrix.end(), 0));
  EXPECT_THROW((void)matrix(2U, 0U), std::out_of_range);
  const auto matrix_view = matrix.view();
  EXPECT_TRUE(matrix_view.is_contiguous());
  EXPECT_EQ(6, matrix_view[5U]);
  const auto matrix_roi = matrix.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U));
  EXPECT_FALSE(matrix_roi.is_contiguous());
  EXPECT_EQ(2, matrix_roi.front());
  EXPECT_EQ(6, matrix_roi.back());
  matrix.resize(1U, 2U);
  EXPECT_EQ(1U, matrix.rows());
  EXPECT_EQ(2U, matrix.cols());
  EXPECT_EQ(matrix_data, matrix.data());
  EXPECT_EQ(matrix_capacity, matrix.capacity());
  EXPECT_EQ(2U, matrix.buffer().size());
  matrix.release();
  EXPECT_TRUE(matrix.empty());
  EXPECT_EQ(0U, matrix.capacity());

  auto image = ksj::array::make_pooled_image<int>(2, 3);
  for (std::size_t index = 0; index < image.size(); ++index) {
    image[index] = static_cast<int>(index + 10U);
  }
  auto* image_data = image.data();
  const auto image_capacity = image.capacity();
  EXPECT_EQ(2U, image.extent(0U));
  EXPECT_EQ(3U, image.extent(1U));
  EXPECT_EQ(3U, image.row_stride());
  EXPECT_EQ(1U, image.col_stride());
  EXPECT_EQ(3U, image.stride(0U));
  EXPECT_EQ(1U, image.stride(1U));
  EXPECT_EQ(6U * sizeof(int), image.size_bytes());
  EXPECT_GE(image_capacity, image.size());
  EXPECT_GE(image.capacity_bytes(), image.size_bytes());
  EXPECT_TRUE(image.is_contiguous());
  EXPECT_EQ(10, image.front());
  EXPECT_EQ(15, image.back());
  EXPECT_EQ(13, image(1U, 0U));
  EXPECT_EQ(75, std::accumulate(image.begin(), image.end(), 0));
  EXPECT_THROW((void)image(2U, 0U), std::out_of_range);
  const auto image_view = image.view();
  EXPECT_TRUE(image_view.is_contiguous());
  EXPECT_EQ(15, image_view[5U]);
  const auto image_roi = image.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U));
  EXPECT_FALSE(image_roi.is_contiguous());
  EXPECT_EQ(11, image_roi.front());
  EXPECT_EQ(15, image_roi.back());
  image.resize(1U, 2U);
  EXPECT_EQ(1U, image.height());
  EXPECT_EQ(2U, image.width());
  EXPECT_EQ(image_data, image.data());
  EXPECT_EQ(image_capacity, image.capacity());
  EXPECT_EQ(2U, image.buffer().size());
  image.clear();
  EXPECT_TRUE(image.empty());
  EXPECT_EQ(0U, image.buffer().size());
  EXPECT_EQ(image_data, image.data());
  EXPECT_EQ(image_capacity, image.capacity());
  image.release();
  EXPECT_EQ(0U, image.capacity());

  auto cube = ksj::array::make_pooled_cube<int>(2, 3, 4);
  for (std::size_t index = 0; index < cube.size(); ++index) {
    cube[index] = static_cast<int>(index);
  }
  auto* cube_data = cube.data();
  const auto cube_capacity = cube.capacity();
  EXPECT_EQ(2U, cube.extent(0U));
  EXPECT_EQ(3U, cube.extent(1U));
  EXPECT_EQ(4U, cube.extent(2U));
  EXPECT_EQ(12U, cube.dim0_stride());
  EXPECT_EQ(4U, cube.dim1_stride());
  EXPECT_EQ(1U, cube.dim2_stride());
  EXPECT_EQ(12U, cube.stride(0U));
  EXPECT_EQ(4U, cube.stride(1U));
  EXPECT_EQ(1U, cube.stride(2U));
  EXPECT_EQ(24U * sizeof(int), cube.size_bytes());
  EXPECT_GE(cube_capacity, cube.size());
  EXPECT_GE(cube.capacity_bytes(), cube.size_bytes());
  EXPECT_TRUE(cube.is_contiguous());
  EXPECT_EQ(0, cube.front());
  EXPECT_EQ(23, cube.back());
  EXPECT_EQ(23, cube(1U, 2U, 3U));
  EXPECT_EQ(276, std::accumulate(cube.begin(), cube.end(), 0));
  EXPECT_THROW((void)cube(2U, 0U, 0U), std::out_of_range);
  const auto cube_view = cube.view();
  EXPECT_TRUE(cube_view.is_contiguous());
  EXPECT_EQ(23, cube_view[23U]);
  const auto cube_roi = cube.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  EXPECT_FALSE(cube_roi.is_contiguous());
  EXPECT_EQ(5, cube_roi.front());
  EXPECT_EQ(22, cube_roi.back());
  cube.resize(1U, 2U, 3U);
  EXPECT_EQ(1U, cube.dim0());
  EXPECT_EQ(2U, cube.dim1());
  EXPECT_EQ(3U, cube.dim2());
  EXPECT_EQ(cube_data, cube.data());
  EXPECT_EQ(cube_capacity, cube.capacity());
  EXPECT_EQ(6U, cube.buffer().size());
  cube.release();
  EXPECT_TRUE(cube.empty());
  EXPECT_EQ(0U, cube.capacity());

  auto array4d = ksj::array::make_pooled_array4d<int>(2, 3, 4, 5);
  for (std::size_t index = 0; index < array4d.size(); ++index) {
    array4d[index] = static_cast<int>(index);
  }
  auto* array4d_data = array4d.data();
  const auto array4d_capacity = array4d.capacity();
  EXPECT_EQ(2U, array4d.extent(0U));
  EXPECT_EQ(3U, array4d.extent(1U));
  EXPECT_EQ(4U, array4d.extent(2U));
  EXPECT_EQ(5U, array4d.extent(3U));
  EXPECT_EQ(60U, array4d.dim0_stride());
  EXPECT_EQ(20U, array4d.dim1_stride());
  EXPECT_EQ(5U, array4d.dim2_stride());
  EXPECT_EQ(1U, array4d.dim3_stride());
  EXPECT_EQ(60U, array4d.stride(0U));
  EXPECT_EQ(20U, array4d.stride(1U));
  EXPECT_EQ(5U, array4d.stride(2U));
  EXPECT_EQ(1U, array4d.stride(3U));
  EXPECT_EQ(120U * sizeof(int), array4d.size_bytes());
  EXPECT_GE(array4d_capacity, array4d.size());
  EXPECT_GE(array4d.capacity_bytes(), array4d.size_bytes());
  EXPECT_TRUE(array4d.is_contiguous());
  EXPECT_EQ(0, array4d.front());
  EXPECT_EQ(119, array4d.back());
  EXPECT_EQ(119, array4d(1U, 2U, 3U, 4U));
  EXPECT_EQ(7140, std::accumulate(array4d.begin(), array4d.end(), 0));
  EXPECT_THROW((void)array4d(2U, 0U, 0U, 0U), std::out_of_range);
  const auto array4d_view = array4d.view();
  EXPECT_TRUE(array4d_view.is_contiguous());
  EXPECT_EQ(119, array4d_view[119U]);
  const auto array4d_roi = array4d.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U),
                                           ksj::array::slice(2U, 4U), ksj::array::slice(1U, 4U));
  EXPECT_FALSE(array4d_roi.is_contiguous());
  EXPECT_EQ(31, array4d_roi.front());
  EXPECT_EQ(118, array4d_roi.back());
  array4d.resize(1U, 2U, 3U, 4U);
  EXPECT_EQ(1U, array4d.dim0());
  EXPECT_EQ(2U, array4d.dim1());
  EXPECT_EQ(3U, array4d.dim2());
  EXPECT_EQ(4U, array4d.dim3());
  EXPECT_EQ(array4d_data, array4d.data());
  EXPECT_EQ(array4d_capacity, array4d.capacity());
  EXPECT_EQ(24U, array4d.buffer().size());
  array4d.clear();
  EXPECT_TRUE(array4d.empty());
  EXPECT_EQ(0U, array4d.buffer().size());
  EXPECT_EQ(array4d_data, array4d.data());
  EXPECT_EQ(array4d_capacity, array4d.capacity());
  array4d.release();
  EXPECT_EQ(0U, array4d.capacity());
}

TEST(KSpaceJetArrayPooledEigen, ViewsExposeCommonLogicalAccessors) {
  std::array<int, 24> values{};

  auto vector = ksj::array::VectorView<int>(values.data(), values.size()).subview(ksj::array::slice(0U, 6U, 2U));
  vector[0U] = 1;
  vector[1U] = 2;
  vector[2U] = 3;
  EXPECT_EQ(3U, vector.extent());
  EXPECT_EQ(2U, vector.stride());
  EXPECT_EQ(3U * sizeof(int), vector.size_bytes());
  EXPECT_FALSE(vector.is_contiguous());
  EXPECT_EQ(1, vector.front());
  EXPECT_EQ(3, vector.back());
  EXPECT_EQ(2, values[2U]);
  EXPECT_THROW((void)vector(3U), std::out_of_range);

  auto matrix =
    ksj::array::MatrixView<int>(values.data(), 2U, 6U).subview(ksj::array::_, ksj::array::slice(1U, 6U, 2U));
  matrix[0U] = 10;
  matrix[1U] = 11;
  matrix[5U] = 15;
  EXPECT_EQ(2U, matrix.extent(0U));
  EXPECT_EQ(3U, matrix.extent(1U));
  EXPECT_EQ(6U, matrix.stride(0U));
  EXPECT_EQ(2U, matrix.stride(1U));
  EXPECT_EQ(6U * sizeof(int), matrix.size_bytes());
  EXPECT_FALSE(matrix.is_contiguous());
  EXPECT_EQ(10, matrix.front());
  EXPECT_EQ(15, matrix.back());
  EXPECT_EQ(15, values[11U]);
  EXPECT_THROW((void)matrix(2U, 0U), std::out_of_range);

  auto image = ksj::array::ImageView<int>(values.data(), 2U, 6U).subview(ksj::array::_, ksj::array::slice(1U, 4U));
  image[0U] = 20;
  image[1U] = 21;
  image[5U] = 25;
  EXPECT_EQ(2U, image.extent(0U));
  EXPECT_EQ(3U, image.extent(1U));
  EXPECT_EQ(6U, image.stride(0U));
  EXPECT_EQ(1U, image.stride(1U));
  EXPECT_EQ(6U * sizeof(int), image.size_bytes());
  EXPECT_FALSE(image.is_contiguous());
  EXPECT_EQ(20, image.front());
  EXPECT_EQ(25, image.back());
  EXPECT_EQ(25, values[9U]);
  EXPECT_THROW((void)image(2U, 0U), std::out_of_range);

  auto cube = ksj::array::CubeView<int>(values.data(), 2U, 3U, 4U)
                .subview(ksj::array::_, ksj::array::_, ksj::array::slice(0U, 4U, 2U));
  cube[0U] = 30;
  cube[1U] = 31;
  cube[11U] = 41;
  EXPECT_EQ(2U, cube.extent(0U));
  EXPECT_EQ(3U, cube.extent(1U));
  EXPECT_EQ(2U, cube.extent(2U));
  EXPECT_EQ(12U, cube.stride(0U));
  EXPECT_EQ(4U, cube.stride(1U));
  EXPECT_EQ(2U, cube.stride(2U));
  EXPECT_EQ(12U * sizeof(int), cube.size_bytes());
  EXPECT_FALSE(cube.is_contiguous());
  EXPECT_EQ(30, cube.front());
  EXPECT_EQ(41, cube.back());
  EXPECT_EQ(41, values[22U]);
  EXPECT_THROW((void)cube(2U, 0U, 0U), std::out_of_range);

  auto array4d = ksj::array::Array4DView<int>(values.data(), 2U, 2U, 2U, 3U)
                   .subview(ksj::array::_, ksj::array::_, ksj::array::_, ksj::array::slice(0U, 2U));
  array4d[0U] = 50;
  array4d[1U] = 51;
  array4d[15U] = 65;
  EXPECT_EQ(2U, array4d.extent(0U));
  EXPECT_EQ(2U, array4d.extent(1U));
  EXPECT_EQ(2U, array4d.extent(2U));
  EXPECT_EQ(2U, array4d.extent(3U));
  EXPECT_EQ(12U, array4d.stride(0U));
  EXPECT_EQ(6U, array4d.stride(1U));
  EXPECT_EQ(3U, array4d.stride(2U));
  EXPECT_EQ(1U, array4d.stride(3U));
  EXPECT_EQ(16U * sizeof(int), array4d.size_bytes());
  EXPECT_FALSE(array4d.is_contiguous());
  EXPECT_EQ(50, array4d.front());
  EXPECT_EQ(65, array4d.back());
  EXPECT_EQ(65, values[22U]);
  EXPECT_THROW((void)array4d(2U, 0U, 0U, 0U), std::out_of_range);
}

TEST(KSpaceJetArrayPooledEigen, PacksOnlyNonContiguousViewsIntoScratch) {
  auto vector = ksj::array::make_pooled_vector<int>(6);
  for (std::size_t index = 0; index < vector.size(); ++index) {
    vector(index) = static_cast<int>(index + 1U);
  }

  ksj::array::PooledVector<int> vector_scratch;
  const auto contiguous_vector = ksj::array::pack_contiguous(ksj::array::as_const_view(vector.view()), vector_scratch);
  EXPECT_EQ(vector.data(), contiguous_vector.data());
  EXPECT_TRUE(contiguous_vector.is_contiguous());
  EXPECT_EQ(0U, vector_scratch.size());

  const auto strided_vector =
    ksj::array::as_const_view(vector.view().subview(ksj::array::slice(1U, vector.size(), 2U)));
  const auto packed_vector = ksj::array::pack_contiguous(strided_vector, vector_scratch);
  EXPECT_EQ(vector_scratch.data(), packed_vector.data());
  EXPECT_TRUE(packed_vector.is_contiguous());
  EXPECT_EQ(3U, packed_vector.size());
  EXPECT_EQ(2, packed_vector(0U));
  EXPECT_EQ(4, packed_vector(1U));
  EXPECT_EQ(6, packed_vector(2U));

  auto matrix = ksj::array::make_pooled_matrix<int>(3, 4);
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    matrix[index] = static_cast<int>(index);
  }

  ksj::array::PooledMatrix<int> matrix_scratch;
  const auto matrix_roi =
    ksj::array::as_const_view(matrix.view().subview(ksj::array::slice(0U, 3U), ksj::array::slice(1U, 3U)));
  const auto packed_matrix = ksj::array::pack_contiguous(matrix_roi, matrix_scratch);
  EXPECT_EQ(matrix_scratch.data(), packed_matrix.data());
  EXPECT_TRUE(packed_matrix.is_contiguous());
  EXPECT_EQ(3U, packed_matrix.rows());
  EXPECT_EQ(2U, packed_matrix.cols());
  EXPECT_EQ(matrix(0U, 1U), packed_matrix(0U, 0U));
  EXPECT_EQ(matrix(2U, 2U), packed_matrix(2U, 1U));

  auto image = ksj::array::make_pooled_image<int>(3, 4);
  for (std::size_t index = 0; index < image.size(); ++index) {
    image[index] = static_cast<int>(index + 10U);
  }

  ksj::array::PooledImage<int> image_scratch;
  const auto image_roi =
    ksj::array::as_const_view(image.view().subview(ksj::array::slice(0U, 3U), ksj::array::slice(1U, 3U)));
  const auto packed_image = ksj::array::pack_contiguous(image_roi, image_scratch);
  EXPECT_EQ(image_scratch.data(), packed_image.data());
  EXPECT_TRUE(packed_image.is_contiguous());
  EXPECT_EQ(3U, packed_image.rows());
  EXPECT_EQ(2U, packed_image.cols());
  EXPECT_EQ(image(0U, 1U), packed_image(0U, 0U));
  EXPECT_EQ(image(2U, 2U), packed_image(2U, 1U));

  auto cube = ksj::array::make_pooled_cube<int>(2, 3, 4);
  for (std::size_t index = 0; index < cube.size(); ++index) {
    cube[index] = static_cast<int>(index + 20U);
  }

  ksj::array::PooledCube<int> cube_scratch;
  const auto cube_roi = ksj::array::as_const_view(
    cube.view().subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U)));
  const auto packed_cube = ksj::array::pack_contiguous(cube_roi, cube_scratch);
  EXPECT_EQ(cube_scratch.data(), packed_cube.data());
  EXPECT_TRUE(packed_cube.is_contiguous());
  EXPECT_EQ(2U, packed_cube.dim0());
  EXPECT_EQ(2U, packed_cube.dim1());
  EXPECT_EQ(2U, packed_cube.dim2());
  EXPECT_EQ(cube(0U, 1U, 1U), packed_cube(0U, 0U, 0U));
  EXPECT_EQ(cube(1U, 2U, 2U), packed_cube(1U, 1U, 1U));

  auto array4d = ksj::array::make_pooled_array4d<int>(2, 3, 4, 5);
  for (std::size_t index = 0; index < array4d.size(); ++index) {
    array4d[index] = static_cast<int>(index + 30U);
  }

  ksj::array::PooledArray4D<int> array4d_scratch;
  const auto array4d_roi = ksj::array::as_const_view(array4d.view().subview(
    ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(2U, 4U), ksj::array::slice(1U, 4U)));
  const auto packed_array4d = ksj::array::pack_contiguous(array4d_roi, array4d_scratch);
  EXPECT_EQ(array4d_scratch.data(), packed_array4d.data());
  EXPECT_TRUE(packed_array4d.is_contiguous());
  EXPECT_EQ(2U, packed_array4d.dim0());
  EXPECT_EQ(2U, packed_array4d.dim1());
  EXPECT_EQ(2U, packed_array4d.dim2());
  EXPECT_EQ(3U, packed_array4d.dim3());
  EXPECT_EQ(array4d(0U, 1U, 2U, 1U), packed_array4d(0U, 0U, 0U, 0U));
  EXPECT_EQ(array4d(1U, 2U, 3U, 3U), packed_array4d(1U, 1U, 1U, 2U));
}

TEST(KSpaceJetArrayPooledEigen, PooledAlgorithmOverloadsForwardToViews) {
  auto vector = ksj::array::make_pooled_vector<int>(3);
  ksj::array::fill(vector, 2);
  EXPECT_EQ(6, ksj::array::accumulate(vector, 0));

  auto vector_copy = ksj::array::make_pooled_vector<int>(3);
  ksj::array::copy(vector, vector_copy);
  ksj::array::transform(vector_copy, vector_copy, [](const int value) {
    return value + 1;
  });
  EXPECT_EQ(3, vector_copy(0U));
  EXPECT_EQ(9, ksj::array::accumulate(vector_copy, 0));

  const auto* max_value = ksj::array::max_element(static_cast<const ksj::array::PooledVector<int>&>(vector_copy));
  ASSERT_NE(nullptr, max_value);
  EXPECT_EQ(3, *max_value);

  auto matrix = ksj::array::make_pooled_matrix<int>(1, 2);
  matrix(0U, 0U) = 7;
  matrix(0U, 1U) = 8;
  auto padded = ksj::array::make_pooled_matrix<int>(3, 4);
  ksj::array::copy_centered(matrix, padded, -1);
  EXPECT_EQ(7, padded(1U, 1U));
  EXPECT_EQ(8, padded(1U, 2U));
  EXPECT_EQ(-1, padded(0U, 0U));

  const auto returned_padded = ksj::array::copy_centered(matrix, 3U, 4U, -1);
  EXPECT_EQ(7, returned_padded(1U, 1U));
  EXPECT_EQ(8, returned_padded(1U, 2U));
  EXPECT_EQ(-1, returned_padded(0U, 0U));

  auto cube = ksj::array::make_pooled_cube<int>(1, 2, 3);
  ksj::array::fill(cube, 4);
  EXPECT_EQ(24, ksj::array::accumulate(cube, 0));

  auto array4d = ksj::array::make_pooled_array4d<int>(1, 2, 3, 4);
  ksj::array::fill(array4d, 5);
  EXPECT_EQ(120, ksj::array::accumulate(array4d, 0));
}

TEST(KSpaceJetArrayPooledEigen, PooledContainersExposeMemberAlgorithms) {
  auto vector = ksj::array::make_pooled_vector<int>(3);
  vector.fill(2).transform_in_place([](const int value) {
    return value + 1;
  });
  EXPECT_EQ(9, ksj::array::accumulate(vector, 0));
  vector.set_zero();
  EXPECT_EQ(0, ksj::array::accumulate(vector, 0));

  auto vector_source = ksj::array::make_pooled_vector<int>(2);
  vector_source(0U) = 5;
  vector_source(1U) = 6;
  vector.assign(vector_source);
  EXPECT_EQ(2U, vector.size());
  EXPECT_EQ(11, ksj::array::accumulate(vector, 0));

  auto matrix_source = ksj::array::make_pooled_matrix<int>(2, 2);
  matrix_source.fill(4);
  auto matrix = ksj::array::make_pooled_matrix<int>(1, 1);
  matrix.assign(matrix_source).transform_in_place([](const int value) {
    return value * 2;
  });
  EXPECT_EQ(2U, matrix.rows());
  EXPECT_EQ(2U, matrix.cols());
  EXPECT_EQ(8, matrix(1U, 1U));
  matrix.set_zero();
  EXPECT_EQ(0, ksj::array::accumulate(matrix, 0));

  auto image_source = ksj::array::make_pooled_image<int>(2, 2);
  image_source.fill(3);
  auto image = ksj::array::make_pooled_image<int>(1, 1);
  image.assign(image_source).transform_in_place([](const int value) {
    return value + 4;
  });
  EXPECT_EQ(2U, image.height());
  EXPECT_EQ(2U, image.width());
  EXPECT_EQ(7, image(1U, 1U));
  image.set_zero();
  EXPECT_EQ(0, ksj::array::accumulate(image, 0));

  auto cube_source = ksj::array::make_pooled_cube<int>(1, 2, 3);
  cube_source.fill(2);
  auto cube = ksj::array::make_pooled_cube<int>(1, 1, 1);
  cube.assign(cube_source).transform_in_place([](const int value) {
    return value * 5;
  });
  EXPECT_EQ(1U, cube.dim0());
  EXPECT_EQ(2U, cube.dim1());
  EXPECT_EQ(3U, cube.dim2());
  EXPECT_EQ(10, cube(0U, 1U, 2U));
  cube.set_zero();
  EXPECT_EQ(0, ksj::array::accumulate(cube, 0));

  auto array4d_source = ksj::array::make_pooled_array4d<int>(1, 2, 2, 3);
  array4d_source.fill(3);
  auto array4d = ksj::array::make_pooled_array4d<int>(1, 1, 1, 1);
  array4d.assign(array4d_source).transform_in_place([](const int value) {
    return value + 7;
  });
  EXPECT_EQ(1U, array4d.dim0());
  EXPECT_EQ(2U, array4d.dim1());
  EXPECT_EQ(2U, array4d.dim2());
  EXPECT_EQ(3U, array4d.dim3());
  EXPECT_EQ(10, array4d(0U, 1U, 1U, 2U));
  array4d.set_zero();
  EXPECT_EQ(0, ksj::array::accumulate(array4d, 0));
}

TEST(KSpaceJetArrayPooledEigen, PooledContainersExposeInitializationHelpers) {
  auto vector = ksj::array::PooledVector<int>::ones(4U);
  EXPECT_EQ(4, ksj::array::accumulate(vector, 0));
  vector.set_constant(3).set_linspace(2, 8);
  EXPECT_EQ(2, vector(0U));
  EXPECT_EQ(4, vector(1U));
  EXPECT_EQ(6, vector(2U));
  EXPECT_EQ(8, vector(3U));

  const auto vector_zeros = ksj::array::PooledVector<int>::zeros(3U);
  EXPECT_EQ(0, ksj::array::accumulate(vector_zeros, 0));
  const auto vector_linspace = ksj::array::PooledVector<double>::linspace(3U, 1.0, 2.0);
  EXPECT_DOUBLE_EQ(1.0, vector_linspace(0U));
  EXPECT_DOUBLE_EQ(1.5, vector_linspace(1U));
  EXPECT_DOUBLE_EQ(2.0, vector_linspace(2U));

  auto matrix = ksj::array::PooledMatrix<int>::constant(2U, 3U, 9);
  EXPECT_EQ(54, ksj::array::accumulate(matrix, 0));
  matrix.set_identity();
  EXPECT_EQ(1, matrix(0U, 0U));
  EXPECT_EQ(0, matrix(0U, 1U));
  EXPECT_EQ(1, matrix(1U, 1U));
  EXPECT_EQ(0, matrix(1U, 2U));

  const auto matrix_ones = ksj::array::PooledMatrix<int>::ones(2U, 3U);
  EXPECT_EQ(6, ksj::array::accumulate(matrix_ones, 0));
  const auto matrix_identity = ksj::array::PooledMatrix<int>::identity(2U, 3U);
  EXPECT_EQ(1, matrix_identity(0U, 0U));
  EXPECT_EQ(1, matrix_identity(1U, 1U));
  EXPECT_EQ(0, matrix_identity(0U, 2U));
  const auto matrix_eye = ksj::array::PooledMatrix<int>::eye(3U, 2U);
  EXPECT_EQ(1, matrix_eye(0U, 0U));
  EXPECT_EQ(1, matrix_eye(1U, 1U));
  EXPECT_EQ(0, matrix_eye(2U, 1U));
  const auto matrix_linspace = ksj::array::PooledMatrix<int>::linspace(2U, 3U, 1, 6);
  EXPECT_EQ(1, matrix_linspace(0U, 0U));
  EXPECT_EQ(2, matrix_linspace(0U, 1U));
  EXPECT_EQ(6, matrix_linspace(1U, 2U));

  auto image = ksj::array::PooledImage<int>::zeros(2U, 2U);
  image.set_ones();
  EXPECT_EQ(4, ksj::array::accumulate(image, 0));
  const auto image_linspace = ksj::array::PooledImage<int>::linspace(2U, 2U, 1, 4);
  EXPECT_EQ(1, image_linspace(0U, 0U));
  EXPECT_EQ(4, image_linspace(1U, 1U));

  auto cube = ksj::array::PooledCube<int>::constant(1U, 2U, 3U, 2);
  EXPECT_EQ(12, ksj::array::accumulate(cube, 0));
  cube.set_linspace(1, 6);
  EXPECT_EQ(1, cube(0U, 0U, 0U));
  EXPECT_EQ(6, cube(0U, 1U, 2U));

  const auto array4d = ksj::array::PooledArray4D<int>::linspace(1U, 2U, 2U, 2U, 1, 8);
  EXPECT_EQ(1, array4d(0U, 0U, 0U, 0U));
  EXPECT_EQ(8, array4d(0U, 1U, 1U, 1U));
  const auto array4d_ones = ksj::array::PooledArray4D<int>::ones(1U, 2U, 2U, 2U);
  EXPECT_EQ(8, ksj::array::accumulate(array4d_ones, 0));

  const auto free_full_vector = ksj::array::full_vector<int>(3U, 7);
  EXPECT_EQ(21, ksj::array::accumulate(free_full_vector, 0));
  const auto free_zero_vector = ksj::array::zeros_vector<int>(3U);
  EXPECT_EQ(0, ksj::array::accumulate(free_zero_vector, 0));
  const auto free_linspace_vector = ksj::array::linspace_vector<int>(3U, 2, 6);
  EXPECT_EQ(4, free_linspace_vector(1U));

  const auto free_full_matrix = ksj::array::full_matrix<int>(2U, 2U, 4);
  EXPECT_EQ(16, ksj::array::accumulate(free_full_matrix, 0));
  const auto free_identity_matrix = ksj::array::identity_matrix<int>(2U, 3U);
  EXPECT_EQ(1, free_identity_matrix(1U, 1U));
  EXPECT_EQ(0, free_identity_matrix(1U, 2U));
  const auto free_ones_image = ksj::array::ones_image<int>(2U, 3U);
  EXPECT_EQ(6, ksj::array::accumulate(free_ones_image, 0));
  const auto free_linspace_cube = ksj::array::linspace_cube<int>(1U, 2U, 3U, 1, 6);
  EXPECT_EQ(6, free_linspace_cube(0U, 1U, 2U));
  const auto free_full_array4d = ksj::array::full_array4d<int>(1U, 2U, 2U, 2U, 3);
  EXPECT_EQ(24, ksj::array::accumulate(free_full_array4d, 0));

  std::mt19937 generator{123U};
  const auto random_vector = ksj::array::PooledVector<int>::uniform_random(16U, -2, 2, generator);
  for (const auto value : random_vector) {
    EXPECT_GE(value, -2);
    EXPECT_LE(value, 2);
  }

  auto random_matrix = ksj::array::PooledMatrix<double>::uniform_random(2U, 3U, -1.0, 1.0, generator);
  EXPECT_EQ(2U, random_matrix.rows());
  EXPECT_EQ(3U, random_matrix.cols());
  for (const auto value : random_matrix) {
    EXPECT_GE(value, -1.0);
    EXPECT_LE(value, 1.0);
  }

  ksj::array::fill_uniform_random(random_matrix, 2.0, 3.0, generator);
  for (const auto value : random_matrix) {
    EXPECT_GE(value, 2.0);
    EXPECT_LE(value, 3.0);
  }
  EXPECT_THROW(ksj::array::fill_uniform_random(random_matrix.view(), 3.0, 2.0, generator), std::invalid_argument);

  const auto random_image = ksj::array::PooledImage<int>::uniform_random(2U, 3U, 4, 9, generator);
  for (const auto value : random_image) {
    EXPECT_GE(value, 4);
    EXPECT_LE(value, 9);
  }

  const auto random_cube = ksj::array::PooledCube<float>::uniform_random(1U, 2U, 3U, -0.5F, 0.5F, generator);
  for (const auto value : random_cube) {
    EXPECT_GE(value, -0.5F);
    EXPECT_LE(value, 0.5F);
  }

  const auto random_array4d = ksj::array::PooledArray4D<int>::uniform_random(1U, 2U, 2U, 2U, 10, 20, generator);
  for (const auto value : random_array4d) {
    EXPECT_GE(value, 10);
    EXPECT_LE(value, 20);
  }

  const auto random_complex = ksj::array::PooledVector<ksj::base::cf32>::uniform_random(4U, -1.0F, 1.0F, generator);
  for (const auto value : random_complex) {
    EXPECT_GE(value.real(), -1.0F);
    EXPECT_LE(value.real(), 1.0F);
    EXPECT_GE(value.imag(), -1.0F);
    EXPECT_LE(value.imag(), 1.0F);
  }

  const auto random_vector_without_external_generator = ksj::array::PooledVector<int>::uniform_random(8U, 0, 3);
  for (const auto value : random_vector_without_external_generator) {
    EXPECT_GE(value, 0);
    EXPECT_LE(value, 3);
  }

  auto random_matrix_without_external_generator = ksj::array::make_pooled_matrix<float>(2U, 2U);
  random_matrix_without_external_generator.set_uniform_random(-2.0F, -1.0F);
  for (const auto value : random_matrix_without_external_generator) {
    EXPECT_GE(value, -2.0F);
    EXPECT_LE(value, -1.0F);
  }

  ksj::array::fill_uniform_random(random_matrix_without_external_generator.view(), 5.0F, 6.0F);
  for (const auto value : random_matrix_without_external_generator) {
    EXPECT_GE(value, 5.0F);
    EXPECT_LE(value, 6.0F);
  }

  const auto free_random_vector = ksj::array::uniform_random_vector<int>(4U, 1, 3, generator);
  EXPECT_EQ(4U, free_random_vector.size());
  const auto free_random_matrix = ksj::array::uniform_random_matrix<float>(2U, 2U, -1.0F, 1.0F, generator);
  EXPECT_EQ(2U, free_random_matrix.rows());
  EXPECT_EQ(2U, free_random_matrix.cols());
  const auto free_random_image = ksj::array::uniform_random_image<int>(2U, 1U, 0, 5, generator);
  EXPECT_EQ(2U, free_random_image.size());
  const auto free_random_cube = ksj::array::uniform_random_cube<float>(1U, 1U, 2U, 0.0F, 1.0F, generator);
  EXPECT_EQ(2U, free_random_cube.size());
  const auto free_random_array4d = ksj::array::uniform_random_array4d<int>(1U, 1U, 1U, 2U, 0, 9, generator);
  EXPECT_EQ(2U, free_random_array4d.size());
}

TEST(KSpaceJetArrayPooledEigen, PooledContainersExposeMemberViewsAndSubviews) {
  auto vector = ksj::array::make_pooled_vector<int>(4);
  for (std::size_t index = 0; index < vector.size(); ++index) {
    vector(index) = static_cast<int>(index);
  }
  auto vector_view = vector.view();
  vector.subview(ksj::array::slice(1U, 3U))(1U) = 20;
  EXPECT_EQ(vector.data(), vector_view.data());
  EXPECT_EQ(4U, vector_view.size());
  EXPECT_EQ(20, vector(2U));
  const auto& const_vector = vector;
  EXPECT_EQ(20, const_vector.subview(ksj::array::slice(2U, 3U))(0U));

  auto matrix = ksj::array::make_pooled_matrix<int>(3, 4);
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<int>(row * 10U + col);
    }
  }
  auto matrix_view = matrix.view();
  matrix.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U))(1U, 1U) = 88;
  matrix.row(0U)(3U) = 33;
  matrix.col(1U)(2U) = 77;
  EXPECT_EQ(matrix.data(), matrix_view.data());
  EXPECT_EQ(3U, matrix_view.rows());
  EXPECT_EQ(4U, matrix_view.cols());
  EXPECT_EQ(4U, matrix_view.row_stride());
  EXPECT_EQ(1U, matrix_view.col_stride());
  EXPECT_EQ(33, matrix(0U, 3U));
  EXPECT_EQ(77, matrix(2U, 1U));
  EXPECT_EQ(88, matrix(2U, 2U));
  const auto& const_matrix = matrix;
  EXPECT_EQ(33, const_matrix.row(0U)(3U));
  EXPECT_EQ(77, const_matrix.col(1U)(2U));
  EXPECT_EQ(88, const_matrix.subview(ksj::array::slice(2U, 3U), ksj::array::slice(2U, 3U))(0U, 0U));
  EXPECT_EQ(33, matrix.subview(0U, ksj::array::slice(3U, 4U))(0U));
  EXPECT_EQ(77, matrix.subview(ksj::array::slice(2U, 3U), 1U)(0U));

  auto image = ksj::array::make_pooled_image<int>(2, 3);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col);
    }
  }
  auto image_view = image.view();
  image.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U))(0U, 1U) = 99;
  image.row(0U)(2U) = 44;
  image.col(1U)(1U) = 55;
  EXPECT_EQ(image.data(), image_view.data());
  EXPECT_EQ(2U, image_view.height());
  EXPECT_EQ(3U, image_view.width());
  EXPECT_EQ(3U, image_view.row_stride_elements());
  EXPECT_EQ(44, image(0U, 2U));
  EXPECT_EQ(55, image(1U, 1U));
  EXPECT_EQ(99, image(1U, 2U));
  const auto& const_image = image;
  EXPECT_EQ(44, const_image.row(0U)(2U));
  EXPECT_EQ(55, const_image.col(1U)(1U));
  EXPECT_EQ(99, const_image.subview(ksj::array::slice(1U, 2U), ksj::array::slice(2U, 3U))(0U, 0U));
  EXPECT_EQ(44, image.subview(0U, ksj::array::slice(2U, 3U))(0U));
  EXPECT_EQ(55, image.subview(ksj::array::slice(1U, 2U), 1U)(0U));

  auto cube = ksj::array::make_pooled_cube<int>(2, 3, 4);
  ksj::array::fill(cube.view(), 1);
  auto cube_view = cube.view();
  cube.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U))(0U, 1U, 1U) = 7;
  cube.subview(0U, ksj::array::_, ksj::array::_)(2U, 3U) = 8;
  cube.subview(ksj::array::_, 1U, ksj::array::_)(1U, 2U) = 9;
  cube.subview(ksj::array::_, ksj::array::_, 0U)(1U, 2U) = 10;
  EXPECT_EQ(cube.data(), cube_view.data());
  EXPECT_EQ(2U, cube_view.dim0());
  EXPECT_EQ(3U, cube_view.dim1());
  EXPECT_EQ(4U, cube_view.dim2());
  EXPECT_EQ(12U, cube_view.dim0_stride());
  EXPECT_EQ(4U, cube_view.dim1_stride());
  EXPECT_EQ(1U, cube_view.dim2_stride());
  EXPECT_EQ(8, cube(0U, 2U, 3U));
  EXPECT_EQ(9, cube(1U, 1U, 2U));
  EXPECT_EQ(10, cube(1U, 2U, 0U));
  EXPECT_EQ(7, cube(1U, 2U, 2U));
  const auto& const_cube = cube;
  EXPECT_EQ(8, const_cube.subview(0U, ksj::array::_, ksj::array::_)(2U, 3U));
  EXPECT_EQ(9, const_cube.subview(ksj::array::_, 1U, ksj::array::_)(1U, 2U));
  EXPECT_EQ(10, const_cube.subview(ksj::array::_, ksj::array::_, 0U)(1U, 2U));
  EXPECT_EQ(
    7, const_cube.subview(ksj::array::slice(1U, 2U), ksj::array::slice(2U, 3U), ksj::array::slice(2U, 3U))(0U, 0U, 0U));
  EXPECT_EQ(7, cube.subview(1U, 2U, ksj::array::slice(2U, 3U))(0U));
}

TEST(KSpaceJetArrayPooledEigen, PooledContainersSupportSwapReserveAndExplicitReshape) {
  auto vector = ksj::array::PooledVector<int>::linspace(6U, 1, 6);
  const auto old_vector_capacity = vector.capacity();
  vector.reserve(old_vector_capacity + 8U);
  EXPECT_GE(vector.capacity(), old_vector_capacity + 8U);
  EXPECT_EQ(6U, vector.size());
  EXPECT_EQ(1, vector[0]);
  EXPECT_EQ(6, vector[5]);

  auto as_matrix = vector.reshape_view(2U, 3U);
  EXPECT_EQ(2U, as_matrix.rows());
  EXPECT_EQ(3U, as_matrix.cols());
  EXPECT_EQ(5, as_matrix(1U, 1U));
  as_matrix(1U, 2U) = 60;
  EXPECT_EQ(60, vector[5]);

  auto as_cube = vector.reshape_view(1U, 2U, 3U);
  EXPECT_EQ(1U, as_cube.dim0());
  EXPECT_EQ(2U, as_cube.dim1());
  EXPECT_EQ(3U, as_cube.dim2());
  EXPECT_EQ(60, as_cube(0U, 1U, 2U));
  EXPECT_THROW((void)vector.reshape_view(4U, 2U), std::invalid_argument);

  auto matrix = ksj::array::PooledMatrix<int>::linspace(2U, 3U, 1, 6);
  matrix.reserve(3U, 4U);
  EXPECT_GE(matrix.capacity(), 12U);
  EXPECT_EQ(2U, matrix.rows());
  EXPECT_EQ(3U, matrix.cols());
  EXPECT_EQ(6, matrix(1U, 2U));
  matrix.reshape(3U, 2U);
  EXPECT_EQ(3U, matrix.rows());
  EXPECT_EQ(2U, matrix.cols());
  EXPECT_EQ(4, matrix(1U, 1U));
  EXPECT_THROW(matrix.reshape(4U, 2U), std::invalid_argument);

  auto matrix_line = matrix.reshape_view(6U);
  matrix_line(5U) = 90;
  EXPECT_EQ(90, matrix(2U, 1U));
  auto matrix_cube = matrix.reshape_view(1U, 3U, 2U);
  EXPECT_EQ(90, matrix_cube(0U, 2U, 1U));

  auto image = ksj::array::PooledImage<int>::linspace(2U, 3U, 1, 6);
  image.reshape(3U, 2U);
  EXPECT_EQ(3U, image.rows());
  EXPECT_EQ(2U, image.cols());
  EXPECT_EQ(6, image(2U, 1U));

  auto cube = ksj::array::PooledCube<int>::linspace(2U, 3U, 4U, 1, 24);
  cube.reserve(3U, 3U, 4U);
  EXPECT_GE(cube.capacity(), 36U);
  EXPECT_EQ(2U, cube.dim0());
  EXPECT_EQ(3U, cube.dim1());
  EXPECT_EQ(4U, cube.dim2());
  EXPECT_EQ(24, cube(1U, 2U, 3U));
  cube.reshape(4U, 3U, 2U);
  EXPECT_EQ(4U, cube.dim0());
  EXPECT_EQ(3U, cube.dim1());
  EXPECT_EQ(2U, cube.dim2());
  EXPECT_EQ(24, cube(3U, 2U, 1U));
  auto cube_as_matrix = cube.reshape_view(6U, 4U);
  cube_as_matrix(5U, 3U) = 120;
  EXPECT_EQ(120, cube(3U, 2U, 1U));

  auto array4d = ksj::array::PooledArray4D<int>::linspace(2U, 3U, 2U, 2U, 1, 24);
  array4d.reserve(2U, 3U, 2U, 4U);
  EXPECT_GE(array4d.capacity(), 48U);
  EXPECT_EQ(24, array4d(1U, 2U, 1U, 1U));
  array4d.reshape(3U, 2U, 2U, 2U);
  EXPECT_EQ(3U, array4d.dim0());
  EXPECT_EQ(2U, array4d.dim1());
  EXPECT_EQ(2U, array4d.dim2());
  EXPECT_EQ(2U, array4d.dim3());
  EXPECT_EQ(24, array4d(2U, 1U, 1U, 1U));
  auto array4d_cube = array4d.reshape_view(4U, 3U, 2U);
  EXPECT_EQ(24, array4d_cube(3U, 2U, 1U));
  EXPECT_THROW(array4d.reshape(2U, 2U, 2U, 2U), std::invalid_argument);

  auto lhs = ksj::array::PooledVector<int>::linspace(3U, 1, 3);
  auto rhs = ksj::array::PooledVector<int>::linspace(2U, 9, 10);
  swap(lhs, rhs);
  EXPECT_EQ(2U, lhs.size());
  EXPECT_EQ(9, lhs[0]);
  EXPECT_EQ(3U, rhs.size());
  EXPECT_EQ(3, rhs[2]);
}

TEST(KSpaceJetArrayPooledEigen, PooledFactoriesMaterializeViewsAsNewOwners) {
  auto vector = ksj::array::make_pooled_vector<int>(6);
  for (std::size_t index = 0; index < vector.size(); ++index) {
    vector(index) = static_cast<int>(index + 1U);
  }
  auto vector_copy = ksj::array::make_pooled_vector(vector.view().subview(ksj::array::slice(0U, vector.size(), 2U)));
  vector(0U) = 99;
  ASSERT_EQ(3U, vector_copy.size());
  EXPECT_EQ(1, vector_copy(0U));
  EXPECT_EQ(3, vector_copy(1U));
  EXPECT_EQ(5, vector_copy(2U));
  EXPECT_EQ(1U, vector_copy.view().stride());

  auto matrix = ksj::array::make_pooled_matrix<int>(3, 4);
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<int>(row * 10U + col);
    }
  }
  auto matrix_copy =
    ksj::array::make_pooled_matrix(matrix.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U)));
  matrix(1U, 1U) = 99;
  ASSERT_EQ(2U, matrix_copy.rows());
  ASSERT_EQ(2U, matrix_copy.cols());
  EXPECT_EQ(11, matrix_copy(0U, 0U));
  EXPECT_EQ(12, matrix_copy(0U, 1U));
  EXPECT_EQ(21, matrix_copy(1U, 0U));
  EXPECT_EQ(22, matrix_copy(1U, 1U));
  EXPECT_EQ(11, matrix_copy.data()[0]);
  EXPECT_EQ(22, matrix_copy.data()[3]);

  auto image = ksj::array::make_pooled_image<int>(3, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 100U + col);
    }
  }
  auto image_copy = ksj::array::make_pooled_image(image.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U)));
  image(1U, 1U) = 999;
  ASSERT_EQ(2U, image_copy.height());
  ASSERT_EQ(2U, image_copy.width());
  EXPECT_EQ(101, image_copy(0U, 0U));
  EXPECT_EQ(102, image_copy(0U, 1U));
  EXPECT_EQ(201, image_copy(1U, 0U));
  EXPECT_EQ(202, image_copy(1U, 1U));
  EXPECT_EQ(2U, image_copy.row_stride_elements());

  auto cube = ksj::array::make_pooled_cube<int>(2, 3, 4);
  for (std::size_t row = 0; row < cube.dim0(); ++row) {
    for (std::size_t col = 0; col < cube.dim1(); ++col) {
      for (std::size_t slice = 0; slice < cube.dim2(); ++slice) {
        cube(row, col, slice) = static_cast<int>(row * 100U + col * 10U + slice);
      }
    }
  }
  auto cube_copy = ksj::array::make_pooled_cube(
    cube.subview(ksj::array::slice(1U, 2U), ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U)));
  cube(1U, 1U, 1U) = 999;
  ASSERT_EQ(1U, cube_copy.dim0());
  ASSERT_EQ(2U, cube_copy.dim1());
  ASSERT_EQ(2U, cube_copy.dim2());
  EXPECT_EQ(111, cube_copy(0U, 0U, 0U));
  EXPECT_EQ(112, cube_copy(0U, 0U, 1U));
  EXPECT_EQ(121, cube_copy(0U, 1U, 0U));
  EXPECT_EQ(122, cube_copy(0U, 1U, 1U));
  EXPECT_EQ(111, cube_copy.data()[0]);
  EXPECT_EQ(122, cube_copy.data()[3]);

  const auto owner_copy = ksj::array::make_pooled_image(image_copy);
  image_copy(0U, 0U) = -1;
  EXPECT_EQ(101, owner_copy(0U, 0U));
}

TEST(KSpaceJetArrayPooledEigen, ViewAlgorithmsOperateOnVectorAndMatrixViews) {
  auto matrix = ksj::array::make_pooled_matrix<int>(3, 4);
  auto output = ksj::array::make_pooled_matrix<int>(3, 4);
  auto view = ksj::array::matrix_view(matrix);
  auto output_view = ksj::array::matrix_view(output);

  ksj::array::fill(view, 1);
  EXPECT_EQ(12, ksj::array::accumulate(view, 0));

  auto middle = view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  ksj::array::fill(middle, 5);
  EXPECT_EQ(28, ksj::array::accumulate(view, 0));

  ksj::array::transform(view, output_view, [](const int value) {
    return value * 2;
  });
  EXPECT_EQ(10, output(1, 1));
  EXPECT_EQ(2, output(0, 0));

  ksj::array::transform(view, output_view, output_view, [](const int lhs, const int rhs) {
    return lhs + rhs;
  });
  EXPECT_EQ(15, output(1, 1));
  EXPECT_EQ(3, output(0, 0));

  int visited = 0;
  ksj::array::for_each(view.col(2U), [&visited](int& value) {
    ++visited;
    value += 10;
  });
  EXPECT_EQ(3, visited);
  EXPECT_EQ(15, matrix(1, 2));

  auto* minimum = ksj::array::min_element(view);
  auto* maximum = ksj::array::max_element(view);
  ASSERT_NE(nullptr, minimum);
  ASSERT_NE(nullptr, maximum);
  EXPECT_EQ(1, *minimum);
  EXPECT_EQ(15, *maximum);

  auto copy_target = ksj::array::make_pooled_matrix<int>(2, 2);
  ksj::array::copy(middle, ksj::array::matrix_view(copy_target));
  EXPECT_EQ(5, copy_target(0, 0));
  EXPECT_EQ(15, copy_target(0, 1));
  EXPECT_EQ(15, copy_target(1, 1));
}

TEST(KSpaceJetArrayPooledEigen, CopyHandlesOverlappingVectorViews) {
  auto vector = ksj::array::make_pooled_vector<int>(6U);
  for (std::size_t index = 0U; index < vector.size(); ++index) {
    vector(index) = static_cast<int>(index);
  }

  ksj::array::copy(vector.view().subview(ksj::array::slice(2U, 6U)), vector.view().subview(ksj::array::slice(0U, 4U)));
  EXPECT_EQ(2, vector(0U));
  EXPECT_EQ(3, vector(1U));
  EXPECT_EQ(4, vector(2U));
  EXPECT_EQ(5, vector(3U));

  for (std::size_t index = 0U; index < vector.size(); ++index) {
    vector(index) = static_cast<int>(index);
  }

  ksj::array::copy(vector.view().subview(ksj::array::slice(0U, 4U)), vector.view().subview(ksj::array::slice(2U, 6U)));
  EXPECT_EQ(0, vector(2U));
  EXPECT_EQ(1, vector(3U));
  EXPECT_EQ(2, vector(4U));
  EXPECT_EQ(3, vector(5U));
}

TEST(KSpaceJetArrayPooledEigen, CopyHandlesOverlappingStridedMatrixViews) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2U, 4U);
  for (std::size_t index = 0U; index < matrix.size(); ++index) {
    matrix[index] = static_cast<int>(index + 1U);
  }

  ksj::array::copy(matrix.view().subview(ksj::array::_, ksj::array::slice(0U, 3U)),
                   matrix.view().subview(ksj::array::_, ksj::array::slice(1U, 4U)));

  EXPECT_EQ(1, matrix(0U, 0U));
  EXPECT_EQ(1, matrix(0U, 1U));
  EXPECT_EQ(2, matrix(0U, 2U));
  EXPECT_EQ(3, matrix(0U, 3U));
  EXPECT_EQ(5, matrix(1U, 0U));
  EXPECT_EQ(5, matrix(1U, 1U));
  EXPECT_EQ(6, matrix(1U, 2U));
  EXPECT_EQ(7, matrix(1U, 3U));
}

TEST(KSpaceJetArrayPooledEigen, StridedVectorViewBorrowsEveryNthElement) {
  auto vector = ksj::array::make_pooled_vector<int>(8U);
  for (std::size_t index = 0U; index < vector.size(); ++index) {
    vector[index] = static_cast<int>(index);
  }

  auto every_other = vector.view().subview(ksj::array::slice(0U, vector.size(), 2U));
  EXPECT_EQ(vector.data(), every_other.data());
  EXPECT_EQ(4U, every_other.size());
  EXPECT_EQ(2U, every_other.stride());
  EXPECT_EQ(0, every_other(0U));
  EXPECT_EQ(2, every_other(1U));
  EXPECT_EQ(6, every_other(3U));

  auto shifted = vector.view().subview(ksj::array::slice(1U, 7U, 2U));
  shifted(1U) = 42;
  EXPECT_EQ(42, vector(3U));

  auto pooled_view = vector.subview(ksj::array::slice(0U, 6U, 3U));
  EXPECT_EQ(vector.data(), pooled_view.data());
  EXPECT_EQ(3U, pooled_view.stride());

  EXPECT_TRUE(vector.view().subview(ksj::array::slice(vector.size(), vector.size(), 3U)).empty());
  EXPECT_THROW((void)vector.view().subview(ksj::array::slice(0U, 2U, 0U)), std::invalid_argument);
  EXPECT_THROW((void)vector.view().subview(ksj::array::slice(2U, 10U, 2U)), std::out_of_range);
}

TEST(KSpaceJetArrayPooledEigen, CopyCenteredPadsAndCropsMatrixViewsWithStrides) {
  int row_major_storage_with_padding[] = {1, 2, -9, -9, 10, 20, -9, -9};
  const auto strided_view = ksj::array::MatrixView<const int>(row_major_storage_with_padding, 2U, 4U)
                              .subview(ksj::array::_, ksj::array::slice(0U, 2U));

  auto padded = ksj::array::make_pooled_matrix<int>(4, 5);
  ksj::array::copy_centered(strided_view, padded.view(), -1);
  const auto returned_padded = ksj::array::copy_centered(strided_view, 4U, 5U, -1);

  EXPECT_EQ(-1, padded(0U, 0U));
  EXPECT_EQ(1, padded(1U, 1U));
  EXPECT_EQ(2, padded(1U, 2U));
  EXPECT_EQ(10, padded(2U, 1U));
  EXPECT_EQ(20, padded(2U, 2U));
  EXPECT_EQ(-1, padded(3U, 4U));
  EXPECT_EQ(1, returned_padded(1U, 1U));
  EXPECT_EQ(20, returned_padded(2U, 2U));
  EXPECT_EQ(-1, returned_padded(3U, 4U));

  auto cropped = ksj::array::make_pooled_matrix<int>(1, 2);
  ksj::array::copy_centered(padded.view(), cropped.view());
  const auto returned_cropped = ksj::array::copy_centered(padded.view(), 1U, 2U);

  EXPECT_EQ(10, cropped(0U, 0U));
  EXPECT_EQ(20, cropped(0U, 1U));
  EXPECT_EQ(10, returned_cropped(0U, 0U));
  EXPECT_EQ(20, returned_cropped(0U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, CopyCenteredPadsImageViews) {
  auto image = ksj::array::make_pooled_image<int>(1U, 2U);
  image(0U, 0U) = 3;
  image(0U, 1U) = 4;

  const auto padded = ksj::array::copy_centered(ksj::array::as_const_view(image.view()), 3U, 4U, -1);

  EXPECT_EQ(-1, padded(0U, 0U));
  EXPECT_EQ(3, padded(1U, 1U));
  EXPECT_EQ(4, padded(1U, 2U));
  EXPECT_EQ(-1, padded(2U, 3U));
}

TEST(KSpaceJetArrayPooledEigen, CopyCenteredPadsCubeViews) {
  auto cube = ksj::array::make_pooled_cube<int>(1, 2, 1);
  cube(0U, 0U, 0U) = 7;
  cube(0U, 1U, 0U) = 8;

  auto padded = ksj::array::make_pooled_cube<int>(3, 4, 2);
  ksj::array::copy_centered(cube.view(), padded.view(), -1);
  const auto returned_padded = ksj::array::copy_centered(cube, 3U, 4U, 2U, -1);

  EXPECT_EQ(-1, padded(0U, 0U, 0U));
  EXPECT_EQ(7, padded(1U, 1U, 1U));
  EXPECT_EQ(8, padded(1U, 2U, 1U));
  EXPECT_EQ(-1, padded(2U, 3U, 1U));
  EXPECT_EQ(7, returned_padded(1U, 1U, 1U));
  EXPECT_EQ(8, returned_padded(1U, 2U, 1U));
  EXPECT_EQ(-1, returned_padded(2U, 3U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, CopyCenteredPadsArray4DViewsWithLowerAlignment) {
  auto input = ksj::array::make_pooled_array4d<int>(1U, 1U, 2U, 1U);
  input(0U, 0U, 0U, 0U) = 7;
  input(0U, 0U, 1U, 0U) = 8;

  auto padded = ksj::array::make_pooled_array4d<int>(1U, 4U, 5U, 2U);
  ksj::array::copy_centered(input.view(), padded.view(), -1, ksj::array::CenteredCopyAlignment::lower);
  const auto returned_padded =
    ksj::array::copy_centered(input.view(), 1U, 4U, 5U, 2U, -1, ksj::array::CenteredCopyAlignment::lower);

  EXPECT_EQ(-1, padded(0U, 0U, 0U, 0U));
  EXPECT_EQ(7, padded(0U, 1U, 1U, 0U));
  EXPECT_EQ(8, padded(0U, 1U, 2U, 0U));
  EXPECT_EQ(-1, padded(0U, 2U, 2U, 0U));
  EXPECT_EQ(7, returned_padded(0U, 1U, 1U, 0U));
  EXPECT_EQ(8, returned_padded(0U, 1U, 2U, 0U));
  EXPECT_EQ(-1, returned_padded(0U, 2U, 2U, 0U));
}

TEST(KSpaceJetArrayPooledEigen, ExtractChannelVolumePatchesBuildsRowsInRowMajorPatchOrder) {
  auto input = ksj::array::make_pooled_array4d<int>(2U, 2U, 3U, 2U);
  for (std::size_t channel = 0U; channel < input.dim0(); ++channel) {
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          input(channel, row, col, slice) = static_cast<int>(100U * channel + 10U * row + 2U * col + slice);
        }
      }
    }
  }

  auto output = ksj::array::make_pooled_matrix<int>(4U, 8U);
  ksj::array::extract_sliding_patch_matrix(input.view(), 2U, 2U, 1U, output.view());

  EXPECT_EQ(0, output(0U, 0U));
  EXPECT_EQ(2, output(0U, 1U));
  EXPECT_EQ(10, output(0U, 2U));
  EXPECT_EQ(12, output(0U, 3U));
  EXPECT_EQ(100, output(0U, 4U));
  EXPECT_EQ(102, output(0U, 5U));
  EXPECT_EQ(110, output(0U, 6U));
  EXPECT_EQ(112, output(0U, 7U));

  EXPECT_EQ(1, output(1U, 0U));
  EXPECT_EQ(3, output(1U, 1U));
  EXPECT_EQ(11, output(1U, 2U));
  EXPECT_EQ(13, output(1U, 3U));

  EXPECT_EQ(2, output(2U, 0U));
  EXPECT_EQ(4, output(2U, 1U));
  EXPECT_EQ(12, output(2U, 2U));
  EXPECT_EQ(14, output(2U, 3U));
}

TEST(KSpaceJetArrayPooledEigen, RawCubeViewUsesRowMajorLayout) {
  std::array<int, 12> storage{};
  auto view = ksj::array::cube_view(storage.data(), 2U, 3U, 2U);

  view(1U, 2U, 1U) = 42;

  EXPECT_EQ(42, storage[(1U * 3U + 2U) * 2U + 1U]);
  EXPECT_EQ(6U, view.dim0_stride());
  EXPECT_EQ(2U, view.dim1_stride());
  EXPECT_EQ(1U, view.dim2_stride());
  EXPECT_TRUE(view.is_contiguous());
}

TEST(KSpaceJetArrayPooledEigen, ElementwiseTransformsVectorsAndMatrices) {
  auto vector = ksj::array::make_pooled_vector<int>(3);
  vector(0) = 1;
  vector(1) = 2;
  vector(2) = 3;

  const auto doubled = ksj::array::transform(vector, [](const int value) {
    return value * 2;
  });
  EXPECT_EQ(2, doubled(0));
  EXPECT_EQ(6, doubled(2));

  const auto reversed = ksj::array::reverse(vector);
  EXPECT_EQ(3, reversed(0));
  EXPECT_EQ(1, reversed(2));

  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  matrix(0, 0) = 1;
  matrix(0, 1) = 2;
  matrix(0, 2) = 3;
  matrix(1, 0) = 4;
  matrix(1, 1) = 5;
  matrix(1, 2) = 6;

  const auto row_reversed = ksj::array::reverse_rows(matrix);
  EXPECT_EQ(4, row_reversed(0, 0));
  EXPECT_EQ(6, row_reversed(0, 2));
  EXPECT_EQ(1, row_reversed(1, 0));
  EXPECT_EQ(3, row_reversed(1, 2));

  const auto col_reversed = ksj::array::reverse_cols(matrix);
  EXPECT_EQ(3, col_reversed(0, 0));
  EXPECT_EQ(1, col_reversed(0, 2));
  EXPECT_EQ(6, col_reversed(1, 0));
  EXPECT_EQ(4, col_reversed(1, 2));

  const auto rotated = ksj::array::rotate_180(matrix);
  EXPECT_EQ(6, rotated(0, 0));
  EXPECT_EQ(1, rotated(1, 2));

  const auto transposed = ksj::array::transpose(matrix);
  EXPECT_EQ(3U, transposed.rows());
  EXPECT_EQ(2U, transposed.cols());
  EXPECT_EQ(1, transposed(0, 0));
  EXPECT_EQ(4, transposed(0, 1));
  EXPECT_EQ(3, transposed(2, 0));

  auto transposed_view_output = ksj::array::make_pooled_matrix<int>(3, 2);
  ksj::array::transpose(matrix.view(), transposed_view_output.view());
  EXPECT_EQ(1, transposed_view_output(0, 0));
  EXPECT_EQ(4, transposed_view_output(0, 1));
  EXPECT_EQ(3, transposed_view_output(2, 0));

  const auto transformed = ksj::array::transpose_rotated_180(matrix);
  EXPECT_EQ(3U, transformed.rows());
  EXPECT_EQ(2U, transformed.cols());
  EXPECT_EQ(6, transformed(0, 0));
  EXPECT_EQ(1, transformed(2, 1));

  auto transformed_view_output = ksj::array::make_pooled_matrix<int>(3, 2);
  ksj::array::transpose_rotated_180(matrix.view(), transformed_view_output.view());
  EXPECT_EQ(6, transformed_view_output(0, 0));
  EXPECT_EQ(1, transformed_view_output(2, 1));

  auto image = ksj::array::make_pooled_image<int>(2, 3);
  image(0, 0) = 1;
  image(0, 1) = 2;
  image(0, 2) = 3;
  image(1, 0) = 4;
  image(1, 1) = 5;
  image(1, 2) = 6;

  const auto image_row_reversed = ksj::array::reverse_rows(image);
  EXPECT_EQ(4, image_row_reversed(0, 0));
  EXPECT_EQ(3, image_row_reversed(1, 2));

  const auto image_col_reversed = ksj::array::reverse_cols(image);
  EXPECT_EQ(3, image_col_reversed(0, 0));
  EXPECT_EQ(4, image_col_reversed(1, 2));

  const auto image_rotated = ksj::array::rotate_180(image);
  EXPECT_EQ(6, image_rotated(0, 0));
  EXPECT_EQ(1, image_rotated(1, 2));

  const auto image_transposed = ksj::array::transpose(image);
  EXPECT_EQ(3U, image_transposed.rows());
  EXPECT_EQ(2U, image_transposed.cols());
  EXPECT_EQ(1, image_transposed(0, 0));
  EXPECT_EQ(4, image_transposed(0, 1));

  auto image_transformed_view_output = ksj::array::make_pooled_image<int>(3, 2);
  ksj::array::transpose_rotated_180(image.view(), image_transformed_view_output.view());
  EXPECT_EQ(6, image_transformed_view_output(0, 0));
  EXPECT_EQ(1, image_transformed_view_output(2, 1));

  auto cube = ksj::array::make_pooled_cube<int>(1, 2, 3);
  for (std::size_t index = 0; index < cube.size(); ++index) {
    cube[index] = static_cast<int>(index + 1U);
  }
  const auto cube_copy = ksj::array::copy(cube);
  EXPECT_EQ(6, cube_copy(0U, 1U, 2U));
  const auto cube_doubled = ksj::array::transform(cube, [](const int value) {
    return value * 2;
  });
  EXPECT_EQ(12, cube_doubled(0U, 1U, 2U));
  const auto cube_sum = ksj::array::transform(cube, cube_doubled, [](const int lhs, const int rhs) {
    return lhs + rhs;
  });
  EXPECT_EQ(18, cube_sum(0U, 1U, 2U));

  auto array4d = ksj::array::make_pooled_array4d<int>(1, 2, 2, 3);
  for (std::size_t index = 0; index < array4d.size(); ++index) {
    array4d[index] = static_cast<int>(index + 1U);
  }
  const auto array4d_copy = ksj::array::copy(array4d);
  EXPECT_EQ(12, array4d_copy(0U, 1U, 1U, 2U));
  const auto array4d_doubled = ksj::array::transform(array4d, [](const int value) {
    return value * 2;
  });
  EXPECT_EQ(24, array4d_doubled(0U, 1U, 1U, 2U));
  const auto array4d_sum = ksj::array::transform(array4d, array4d_doubled, [](const int lhs, const int rhs) {
    return lhs + rhs;
  });
  EXPECT_EQ(36, array4d_sum(0U, 1U, 1U, 2U));
}

TEST(KSpaceJetArrayPooledEigen, ComplexOpsExposeMagnitudePhaseAndPolarForms) {
  auto values = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  values(0) = {3.0, 4.0};
  values(1) = {0.0, -2.0};

  const auto magnitudes = ksj::array::magnitude(values);
  EXPECT_DOUBLE_EQ(5.0, magnitudes(0));
  EXPECT_DOUBLE_EQ(2.0, magnitudes(1));

  const auto member_magnitudes = values.magnitude();
  EXPECT_DOUBLE_EQ(5.0, member_magnitudes(0));
  EXPECT_DOUBLE_EQ(2.0, values.abs()(1));
  EXPECT_DOUBLE_EQ(5.0, values.modulus()(0));

  const auto square_magnitudes = ksj::array::squared_magnitude(values);
  EXPECT_DOUBLE_EQ(25.0, square_magnitudes(0));
  EXPECT_DOUBLE_EQ(4.0, square_magnitudes(1));
  EXPECT_DOUBLE_EQ(25.0, values.squared_magnitude()(0));

  const auto phases = ksj::array::phase(values);
  EXPECT_NEAR(std::atan2(4.0, 3.0), phases(0), 1e-12);
  EXPECT_NEAR(-std::numbers::pi / 2.0, phases(1), 1e-12);
  EXPECT_NEAR(std::atan2(4.0, 3.0), values.phase()(0), 1e-12);

  const auto conjugated = ksj::array::conjugate(values);
  EXPECT_DOUBLE_EQ(-4.0, conjugated(0).imag());
  EXPECT_DOUBLE_EQ(-4.0, values.conjugate()(0).imag());
  EXPECT_DOUBLE_EQ(3.0, values.real()(0));
  EXPECT_DOUBLE_EQ(4.0, values.imag()(0));

  auto real_parts = ksj::array::make_pooled_vector<double>(values.size());
  auto imag_parts = ksj::array::make_pooled_vector<double>(values.size());
  ksj::array::split_complex(values.view(), real_parts.view(), imag_parts.view());
  EXPECT_DOUBLE_EQ(3.0, real_parts(0));
  EXPECT_DOUBLE_EQ(4.0, imag_parts(0));
  EXPECT_DOUBLE_EQ(0.0, real_parts(1));
  EXPECT_DOUBLE_EQ(-2.0, imag_parts(1));

  const auto split = ksj::array::split_complex(values);
  EXPECT_DOUBLE_EQ(3.0, split.real(0));
  EXPECT_DOUBLE_EQ(4.0, split.imag(0));
  EXPECT_DOUBLE_EQ(0.0, split.real(1));
  EXPECT_DOUBLE_EQ(-2.0, split.imag(1));

  auto rebuilt = ksj::array::make_pooled_vector<ksj::base::cf64>(values.size());
  ksj::array::complex_from_real_imag(real_parts.view(), imag_parts.view(), rebuilt.view());
  EXPECT_DOUBLE_EQ(values(0).real(), rebuilt(0).real());
  EXPECT_DOUBLE_EQ(values(0).imag(), rebuilt(0).imag());
  EXPECT_DOUBLE_EQ(values(1).real(), rebuilt(1).real());
  EXPECT_DOUBLE_EQ(values(1).imag(), rebuilt(1).imag());

  const auto rebuilt_return = ksj::array::complex_from_real_imag(split.real, split.imag);
  EXPECT_DOUBLE_EQ(values(0).real(), rebuilt_return(0).real());
  EXPECT_DOUBLE_EQ(values(0).imag(), rebuilt_return(0).imag());
  EXPECT_DOUBLE_EQ(values(1).real(), rebuilt_return(1).real());
  EXPECT_DOUBLE_EQ(values(1).imag(), rebuilt_return(1).imag());

  auto in_place_conjugated = ksj::array::copy(values);
  ksj::array::conjugate(in_place_conjugated.view(), in_place_conjugated.view());
  EXPECT_DOUBLE_EQ(3.0, in_place_conjugated(0).real());
  EXPECT_DOUBLE_EQ(-4.0, in_place_conjugated(0).imag());

  auto polar_magnitudes = ksj::array::make_pooled_vector<double>(values.size());
  auto polar_phases = ksj::array::make_pooled_vector<double>(values.size());
  ksj::array::rectangular_to_polar(values.view(), polar_magnitudes.view(), polar_phases.view());
  EXPECT_DOUBLE_EQ(5.0, polar_magnitudes(0));
  EXPECT_NEAR(std::atan2(4.0, 3.0), polar_phases(0), 1e-12);
  EXPECT_DOUBLE_EQ(2.0, polar_magnitudes(1));
  EXPECT_NEAR(-std::numbers::pi / 2.0, polar_phases(1), 1e-12);

  const auto polar_components = ksj::array::rectangular_to_polar_components(values);
  EXPECT_DOUBLE_EQ(5.0, polar_components.magnitude(0));
  EXPECT_NEAR(std::atan2(4.0, 3.0), polar_components.phase(0), 1e-12);
  EXPECT_DOUBLE_EQ(2.0, polar_components.magnitude(1));
  EXPECT_NEAR(-std::numbers::pi / 2.0, polar_components.phase(1), 1e-12);

  auto rectangular_from_split = ksj::array::make_pooled_vector<ksj::base::cf64>(values.size());
  ksj::array::polar_to_rectangular(polar_magnitudes.view(), polar_phases.view(), rectangular_from_split.view());
  EXPECT_NEAR(values(0).real(), rectangular_from_split(0).real(), 1e-12);
  EXPECT_NEAR(values(0).imag(), rectangular_from_split(0).imag(), 1e-12);
  EXPECT_NEAR(values(1).real(), rectangular_from_split(1).real(), 1e-12);
  EXPECT_NEAR(values(1).imag(), rectangular_from_split(1).imag(), 1e-12);

  const auto rectangular_from_components =
    ksj::array::polar_to_rectangular(polar_components.magnitude, polar_components.phase);
  EXPECT_NEAR(values(0).real(), rectangular_from_components(0).real(), 1e-12);
  EXPECT_NEAR(values(0).imag(), rectangular_from_components(0).imag(), 1e-12);
  EXPECT_NEAR(values(1).real(), rectangular_from_components(1).real(), 1e-12);
  EXPECT_NEAR(values(1).imag(), rectangular_from_components(1).imag(), 1e-12);

  auto rhs = ksj::array::make_pooled_vector<ksj::base::cf64>(values.size());
  rhs(0) = {1.0, 2.0};
  rhs(1) = {0.0, -3.0};
  const auto conjugate_product = ksj::array::multiply_conjugate(values.view(), rhs.view());
  EXPECT_DOUBLE_EQ(11.0, conjugate_product(0).real());
  EXPECT_DOUBLE_EQ(-2.0, conjugate_product(0).imag());
  EXPECT_DOUBLE_EQ(6.0, conjugate_product(1).real());
  EXPECT_DOUBLE_EQ(0.0, conjugate_product(1).imag());

  const auto polar = ksj::array::rectangular_to_polar(values);
  const auto rectangular = ksj::array::polar_to_rectangular(polar);
  EXPECT_NEAR(values(0).real(), rectangular(0).real(), 1e-12);
  EXPECT_NEAR(values(0).imag(), rectangular(0).imag(), 1e-12);
}

TEST(KSpaceJetArrayPooledEigen, ComplexViewOpsWriteExplicitOutputsForHigherDimensions) {
  auto cube = ksj::array::make_pooled_cube<ksj::base::cf64>(1U, 2U, 2U);
  cube(0U, 0U, 0U) = {3.0, 4.0};
  cube(0U, 0U, 1U) = {0.0, -2.0};
  cube(0U, 1U, 0U) = {-1.0, 0.0};
  cube(0U, 1U, 1U) = {1.0, 1.0};

  auto magnitudes = ksj::array::make_pooled_cube<double>(cube.dim0(), cube.dim1(), cube.dim2());
  ksj::array::magnitude(cube.view(), magnitudes.view());
  EXPECT_DOUBLE_EQ(5.0, magnitudes(0U, 0U, 0U));
  EXPECT_DOUBLE_EQ(2.0, magnitudes(0U, 0U, 1U));
  EXPECT_DOUBLE_EQ(std::sqrt(2.0), magnitudes(0U, 1U, 1U));

  auto phases = cube.phase();
  EXPECT_NEAR(-std::numbers::pi / 2.0, phases(0U, 0U, 1U), 1.0e-12);

  const auto cube_split = ksj::array::split_complex(cube);
  EXPECT_DOUBLE_EQ(3.0, cube_split.real(0U, 0U, 0U));
  EXPECT_DOUBLE_EQ(4.0, cube_split.imag(0U, 0U, 0U));
  EXPECT_DOUBLE_EQ(0.0, cube_split.real(0U, 0U, 1U));
  EXPECT_DOUBLE_EQ(-2.0, cube_split.imag(0U, 0U, 1U));

  const auto cube_rebuilt = ksj::array::complex_from_real_imag(cube_split.real, cube_split.imag);
  EXPECT_DOUBLE_EQ(cube(0U, 0U, 0U).real(), cube_rebuilt(0U, 0U, 0U).real());
  EXPECT_DOUBLE_EQ(cube(0U, 0U, 0U).imag(), cube_rebuilt(0U, 0U, 0U).imag());
  EXPECT_DOUBLE_EQ(cube(0U, 0U, 1U).real(), cube_rebuilt(0U, 0U, 1U).real());
  EXPECT_DOUBLE_EQ(cube(0U, 0U, 1U).imag(), cube_rebuilt(0U, 0U, 1U).imag());

  const auto cube_polar = ksj::array::rectangular_to_polar_components(cube);
  EXPECT_DOUBLE_EQ(5.0, cube_polar.magnitude(0U, 0U, 0U));
  EXPECT_NEAR(std::atan2(4.0, 3.0), cube_polar.phase(0U, 0U, 0U), 1.0e-12);

  const auto cube_rectangular = ksj::array::polar_to_rectangular(cube_polar.magnitude, cube_polar.phase);
  EXPECT_NEAR(cube(0U, 0U, 0U).real(), cube_rectangular(0U, 0U, 0U).real(), 1.0e-12);
  EXPECT_NEAR(cube(0U, 0U, 0U).imag(), cube_rectangular(0U, 0U, 0U).imag(), 1.0e-12);

  const auto cube_phasor = ksj::array::unit_phasor(cube_polar.phase);
  EXPECT_NEAR(1.0, std::abs(cube_phasor(0U, 0U, 0U)), 1.0e-12);

  auto conjugated = cube.conjugate();
  EXPECT_DOUBLE_EQ(-4.0, conjugated(0U, 0U, 0U).imag());
  EXPECT_DOUBLE_EQ(-1.0, conjugated(0U, 1U, 1U).imag());

  auto array4d = ksj::array::make_pooled_array4d<ksj::base::cf64>(1U, 1U, 1U, 2U);
  array4d(0U, 0U, 0U, 0U) = {5.0, 12.0};
  array4d(0U, 0U, 0U, 1U) = {-2.0, 3.0};
  const auto array4d_magnitudes = ksj::array::magnitude(array4d.view());
  EXPECT_DOUBLE_EQ(13.0, array4d_magnitudes(0U, 0U, 0U, 0U));
  EXPECT_DOUBLE_EQ(13.0, array4d.squared_magnitude()(0U, 0U, 0U, 1U));

  const auto array4d_polar = ksj::array::rectangular_to_polar_components(array4d);
  const auto array4d_rectangular = ksj::array::polar_to_rectangular(array4d_polar.magnitude, array4d_polar.phase);
  EXPECT_NEAR(array4d(0U, 0U, 0U, 0U).real(), array4d_rectangular(0U, 0U, 0U, 0U).real(), 1.0e-12);
  EXPECT_NEAR(array4d(0U, 0U, 0U, 0U).imag(), array4d_rectangular(0U, 0U, 0U, 0U).imag(), 1.0e-12);

  const auto array4d_split = ksj::array::split_complex(array4d);
  const auto array4d_rebuilt = ksj::array::complex_from_real_imag(array4d_split.real, array4d_split.imag);
  EXPECT_DOUBLE_EQ(array4d(0U, 0U, 0U, 1U).real(), array4d_rebuilt(0U, 0U, 0U, 1U).real());
  EXPECT_DOUBLE_EQ(array4d(0U, 0U, 0U, 1U).imag(), array4d_rebuilt(0U, 0U, 0U, 1U).imag());
}

TEST(KSpaceJetArrayPooledEigen, ReflectsComplexCubeAboutReferencePhasor) {
  auto values = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 2, 1);
  auto reflector = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 2, 1);
  auto output = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 2, 1);
  values(0, 0, 0) = {1.0, 2.0};
  values(0, 1, 0) = {3.0, 4.0};
  reflector(0, 0, 0) = {1.0, 0.0};
  reflector(0, 1, 0) = {0.0, 2.0};

  ksj::array::reflect_complex_about(values, reflector, output);

  EXPECT_DOUBLE_EQ(1.0, output(0, 0, 0).real());
  EXPECT_DOUBLE_EQ(-2.0, output(0, 0, 0).imag());
  EXPECT_DOUBLE_EQ(-3.0, output(0, 1, 0).real());
  EXPECT_DOUBLE_EQ(4.0, output(0, 1, 0).imag());
}

TEST(KSpaceJetArrayPooledEigen, NormalizesComplexPhaseForMatrixAndCubeViews) {
  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 2);
  matrix(0, 0) = {3.0, 4.0};
  matrix(0, 1) = {0.0, -2.0};

  ksj::array::normalize_complex_phase_in_place(matrix);

  EXPECT_NEAR(0.6, matrix(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(0.8, matrix(0, 0).imag(), 1.0e-12);
  EXPECT_NEAR(0.0, matrix(0, 1).real(), 1.0e-12);
  EXPECT_NEAR(-1.0, matrix(0, 1).imag(), 1.0e-12);

  auto cube = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 1, 2);
  cube(0, 0, 0) = {5.0, 0.0};
  cube(0, 0, 1) = {0.0, 7.0};
  auto normalized = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 1, 2);
  ksj::array::normalize_complex_phase(cube, normalized);

  EXPECT_NEAR(1.0, normalized(0, 0, 0).real(), 1.0e-12);
  EXPECT_NEAR(0.0, normalized(0, 0, 0).imag(), 1.0e-12);
  EXPECT_NEAR(0.0, normalized(0, 0, 1).real(), 1.0e-12);
  EXPECT_NEAR(1.0, normalized(0, 0, 1).imag(), 1.0e-12);
}

TEST(KSpaceJetArrayPooledEigen, ComputesAbsolutePhaseDifferenceForComplexCubes) {
  auto lhs = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 1, 2);
  auto rhs = ksj::array::make_pooled_cube<ksj::base::cf64>(1, 1, 2);
  lhs(0, 0, 0) = {0.0, 1.0};
  rhs(0, 0, 0) = {1.0, 0.0};
  lhs(0, 0, 1) = {-1.0, 0.0};
  rhs(0, 0, 1) = {0.0, -1.0};

  const auto output = ksj::array::absolute_phase_difference(lhs, rhs);

  EXPECT_NEAR(std::numbers::pi / 2.0, output(0, 0, 0), 1.0e-12);
  EXPECT_NEAR(std::numbers::pi / 2.0, output(0, 0, 1), 1.0e-12);
}

TEST(KSpaceJetArrayPooledEigen, CopiesWhereMaskEqualsAndFindsArgminAcrossSliceGroups) {
  auto mask_storage = ksj::array::make_pooled_cube<int>(2U, 2U, 2U);
  auto input_storage = ksj::array::make_pooled_cube<double>(2U, 2U, 2U);
  auto output_storage = ksj::array::make_pooled_cube<double>(2U, 2U, 2U);

  auto mask = mask_storage.view();
  auto input = input_storage.view();
  auto output = output_storage.view();

  for (std::size_t slice = 0U; slice < mask.dim2(); ++slice) {
    for (std::size_t col = 0U; col < mask.dim1(); ++col) {
      for (std::size_t row = 0U; row < mask.dim0(); ++row) {
        input(row, col, slice) = static_cast<double>(100U * slice + 10U * col + row);
        mask(row, col, slice) = (row == 1U || slice == 1U) ? 7 : 3;
        output(row, col, slice) = -1.0;
      }
    }
  }

  ksj::array::copy_where_equal(mask, 7, ksj::array::as_const_view(input), output);

  EXPECT_DOUBLE_EQ(-1.0, output(0U, 0U, 0U));
  EXPECT_DOUBLE_EQ(1.0, output(1U, 0U, 0U));
  EXPECT_DOUBLE_EQ(110.0, output(0U, 1U, 1U));
  EXPECT_DOUBLE_EQ(111.0, output(1U, 1U, 1U));
  auto other_mask_storage = ksj::array::make_pooled_cube(mask);
  other_mask_storage(0U, 0U, 0U) = 99;
  other_mask_storage(1U, 1U, 1U) = 99;
  const auto other_mask = ksj::array::as_const_view(other_mask_storage.view());
  EXPECT_EQ(2U, ksj::array::count_not_equal(mask, other_mask));

  auto grouped = ksj::array::make_pooled_cube<double>(1U, 1U, 8U);
  grouped(0U, 0U, 0U) = 8.0;
  grouped(0U, 0U, 1U) = 4.0;
  grouped(0U, 0U, 2U) = 7.0;
  grouped(0U, 0U, 3U) = 5.0;
  grouped(0U, 0U, 4U) = 6.0;
  grouped(0U, 0U, 5U) = 3.0;
  grouped(0U, 0U, 6U) = 9.0;
  grouped(0U, 0U, 7U) = 2.0;

  const auto argmin = ksj::array::argmin_groups<int>(grouped, 4U, ksj::array::Dim::dim2);

  ASSERT_EQ(2U, argmin.dim2());
  EXPECT_EQ(2, argmin(0U, 0U, 0U));
  EXPECT_EQ(3, argmin(0U, 0U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, GathersScattersAndFillsRowMajorLinearCubeIndices) {
  auto cube = ksj::array::make_pooled_cube<int>(2U, 3U, 2U);
  for (std::size_t slice = 0U; slice < cube.dim2(); ++slice) {
    for (std::size_t col = 0U; col < cube.dim1(); ++col) {
      for (std::size_t row = 0U; row < cube.dim0(); ++row) {
        cube(row, col, slice) = static_cast<int>(100U * slice + 10U * col + row);
      }
    }
  }

  auto indices = ksj::array::make_pooled_vector<std::size_t>(3U);
  indices(0U) = 0U;
  indices(1U) = 3U;
  indices(2U) = 10U;

  auto gathered = ksj::array::gather_linear_indices(cube.view(), indices.view());

  EXPECT_EQ(0, gathered(0U));
  EXPECT_EQ(110, gathered(1U));
  EXPECT_EQ(21, gathered(2U));

  auto replacement = ksj::array::make_pooled_vector<int>(3U);
  replacement(0U) = -1;
  replacement(1U) = -2;
  replacement(2U) = -3;
  ksj::array::scatter_linear_indices(replacement.view(), indices.view(), cube.view());

  EXPECT_EQ(-1, cube(0U, 0U, 0U));
  EXPECT_EQ(-2, cube(0U, 1U, 1U));
  EXPECT_EQ(-3, cube(1U, 2U, 0U));

  ksj::array::fill_linear_indices(cube.view(), indices.view(), 7);

  EXPECT_EQ(7, cube(0U, 0U, 0U));
  EXPECT_EQ(7, cube(0U, 1U, 1U));
  EXPECT_EQ(7, cube(1U, 2U, 0U));
}

TEST(KSpaceJetArrayPooledEigen, ReductionsWorkOnViewsAndPooledContainers) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2U, 4U);
  for (std::size_t row = 0U; row < matrix.rows(); ++row) {
    for (std::size_t col = 0U; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<int>(row * matrix.cols() + col + 1U);
    }
  }

  const auto middle = matrix.subview(ksj::array::slice(0U, 2U), ksj::array::slice(1U, 3U));
  EXPECT_EQ(18, ksj::array::sum(middle));
  EXPECT_DOUBLE_EQ(4.5, ksj::array::mean(middle));
  EXPECT_EQ(2, ksj::array::min(middle));
  EXPECT_EQ(7, ksj::array::max(middle));
  EXPECT_EQ((std::pair{2, 7}), ksj::array::minmax(middle));
  EXPECT_EQ(4U, ksj::array::count_nonzero(middle));
  EXPECT_EQ(118, ksj::array::sum(middle, 100));

  matrix(1U, 2U) = 0;
  EXPECT_EQ(3U, ksj::array::count_nonzero(middle));
  EXPECT_FALSE(ksj::array::all_of(middle, [](const int value) {
    return value > 0;
  }));
  EXPECT_TRUE(ksj::array::any_of(middle, [](const int value) {
    return value == 0;
  }));

  EXPECT_EQ(155, matrix.squared_norm());
  EXPECT_DOUBLE_EQ(std::sqrt(155.0), matrix.norm());
  EXPECT_DOUBLE_EQ(29.0 / 8.0, matrix.mean());

  auto complex_values = ksj::array::make_pooled_vector<ksj::base::cf64>(2U);
  complex_values(0U) = {3.0, 4.0};
  complex_values(1U) = {1.0, 2.0};
  EXPECT_EQ((ksj::base::cf64{2.0, 3.0}), complex_values.mean());
  EXPECT_DOUBLE_EQ(30.0, complex_values.squared_norm());
  EXPECT_DOUBLE_EQ(std::sqrt(30.0), complex_values.norm());
}

TEST(KSpaceJetArrayPooledEigen, ElementwiseWrappersUseExplicitOutput) {
  auto lhs = ksj::array::make_pooled_matrix<int>(2U, 2U);
  auto rhs = ksj::array::make_pooled_matrix<int>(2U, 2U);
  auto third = ksj::array::make_pooled_matrix<int>(2U, 2U);
  auto fourth = ksj::array::make_pooled_matrix<int>(2U, 2U);
  auto output = ksj::array::make_pooled_matrix<int>(2U, 2U);

  lhs(0U, 0U) = 1;
  lhs(0U, 1U) = 2;
  lhs(1U, 0U) = 3;
  lhs(1U, 1U) = 4;
  rhs(0U, 0U) = 10;
  rhs(0U, 1U) = 20;
  rhs(1U, 0U) = 30;
  rhs(1U, 1U) = 40;
  third(0U, 0U) = 100;
  third(0U, 1U) = 200;
  third(1U, 0U) = 300;
  third(1U, 1U) = 400;
  fourth(0U, 0U) = 1000;
  fourth(0U, 1U) = 2000;
  fourth(1U, 0U) = 3000;
  fourth(1U, 1U) = 4000;

  ksj::array::add(lhs.view(), rhs.view(), output.view());
  EXPECT_EQ(11, output(0U, 0U));
  EXPECT_EQ(44, output(1U, 1U));

  ksj::array::subtract(rhs, lhs, output);
  EXPECT_EQ(9, output(0U, 0U));
  EXPECT_EQ(36, output(1U, 1U));

  ksj::array::add_subtract(rhs.view(), lhs.view(), lhs.view(), output.view());
  EXPECT_EQ(10, output(0U, 0U));
  EXPECT_EQ(40, output(1U, 1U));

  ksj::array::multiply(lhs.view(), rhs.view(), output.view());
  EXPECT_EQ(10, output(0U, 0U));
  EXPECT_EQ(160, output(1U, 1U));

  ksj::array::multiply_accumulate(lhs.view(), rhs.view(), output.view());
  EXPECT_EQ(20, output(0U, 0U));
  EXPECT_EQ(320, output(1U, 1U));

  ksj::array::scale(lhs.view(), 3, output.view());
  EXPECT_EQ(3, output(0U, 0U));
  EXPECT_EQ(12, output(1U, 1U));

  ksj::array::scale_add(lhs.view(), 2, rhs.view(), output.view());
  EXPECT_EQ(12, output(0U, 0U));
  EXPECT_EQ(48, output(1U, 1U));

  ksj::array::linear_combination(lhs, 2, rhs, 3, output);
  EXPECT_EQ(32, output(0U, 0U));
  EXPECT_EQ(128, output(1U, 1U));

  ksj::array::linear_combination(lhs.view(), 2, rhs.view(), 3, third.view(), 4, fourth.view(), 5, output.view());
  EXPECT_EQ(5432, output(0U, 0U));
  EXPECT_EQ(21728, output(1U, 1U));

  ksj::array::cwise_min(lhs, rhs, output);
  EXPECT_EQ(1, output(0U, 0U));
  EXPECT_EQ(4, output(1U, 1U));

  ksj::array::add_scalar(lhs.view(), 5, output.view());
  EXPECT_EQ(6, output(0U, 0U));
  EXPECT_EQ(9, output(1U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, IntelElementwiseMultiplySupportsInPlaceOutput) {
  auto real_values = ksj::array::make_pooled_vector<ksj::base::f32>(4U);
  auto real_scale = ksj::array::make_pooled_vector<ksj::base::f32>(4U);
  real_values(0U) = 1.0F;
  real_values(1U) = 2.0F;
  real_values(2U) = 3.0F;
  real_values(3U) = 4.0F;
  real_scale(0U) = 2.0F;
  real_scale(1U) = 3.0F;
  real_scale(2U) = 4.0F;
  real_scale(3U) = 5.0F;

  ASSERT_TRUE(ksj::array::detail::intel::multiply(ksj::array::as_const_view(real_values.view()),
                                                  ksj::array::as_const_view(real_scale.view()), real_values.view()));
  EXPECT_FLOAT_EQ(2.0F, real_values(0U));
  EXPECT_FLOAT_EQ(6.0F, real_values(1U));
  EXPECT_FLOAT_EQ(12.0F, real_values(2U));
  EXPECT_FLOAT_EQ(20.0F, real_values(3U));

  auto complex_values = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto complex_scale = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  complex_values(0U) = {1.0F, 2.0F};
  complex_values(1U) = {3.0F, -1.0F};
  complex_scale(0U) = {2.0F, 0.0F};
  complex_scale(1U) = {0.0F, 2.0F};

  ASSERT_TRUE(ksj::array::detail::intel::multiply(ksj::array::as_const_view(complex_values.view()),
                                                  ksj::array::as_const_view(complex_scale.view()),
                                                  complex_values.view()));
  EXPECT_FLOAT_EQ(2.0F, complex_values(0U).real());
  EXPECT_FLOAT_EQ(4.0F, complex_values(0U).imag());
  EXPECT_FLOAT_EQ(2.0F, complex_values(1U).real());
  EXPECT_FLOAT_EQ(6.0F, complex_values(1U).imag());
}

TEST(KSpaceJetArrayPooledEigen, MultiplyAccumulateSupportsRealAndComplexVectors) {
  auto real_lhs = ksj::array::make_pooled_vector<ksj::base::f32>(3U);
  auto real_rhs = ksj::array::make_pooled_vector<ksj::base::f32>(3U);
  auto real_output = ksj::array::make_pooled_vector<ksj::base::f32>(3U);
  real_lhs(0U) = 1.0F;
  real_lhs(1U) = 2.0F;
  real_lhs(2U) = 3.0F;
  real_rhs(0U) = 4.0F;
  real_rhs(1U) = 5.0F;
  real_rhs(2U) = 6.0F;
  real_output(0U) = 10.0F;
  real_output(1U) = 20.0F;
  real_output(2U) = 30.0F;

  ksj::array::multiply_accumulate(real_lhs.view(), real_rhs.view(), real_output.view());

  EXPECT_FLOAT_EQ(14.0F, real_output(0U));
  EXPECT_FLOAT_EQ(30.0F, real_output(1U));
  EXPECT_FLOAT_EQ(48.0F, real_output(2U));

  auto complex_lhs = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto complex_rhs = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto complex_output = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  complex_lhs(0U) = {1.0F, 2.0F};
  complex_lhs(1U) = {3.0F, -1.0F};
  complex_rhs(0U) = {2.0F, 0.5F};
  complex_rhs(1U) = {-1.0F, 4.0F};
  complex_output(0U) = {10.0F, 20.0F};
  complex_output(1U) = {30.0F, 40.0F};

  ksj::array::multiply_accumulate(complex_lhs.view(), complex_rhs.view(), complex_output.view());

  EXPECT_FLOAT_EQ(11.0F, complex_output(0U).real());
  EXPECT_FLOAT_EQ(24.5F, complex_output(0U).imag());
  EXPECT_FLOAT_EQ(31.0F, complex_output(1U).real());
  EXPECT_FLOAT_EQ(53.0F, complex_output(1U).imag());
}

TEST(KSpaceJetArrayPooledEigen, RealInverseRsqrtAndHypotSupportViewAndReturningForms) {
  auto input = ksj::array::make_pooled_vector<float>(512U);
  auto rhs = ksj::array::make_pooled_vector<float>(input.size());
  auto output = ksj::array::make_pooled_vector<float>(input.size());
  for (std::size_t index = 0U; index < input.size(); ++index) {
    input(index) = 0.25F + static_cast<float>(index % 17U) * 0.125F;
    rhs(index) = 0.5F + static_cast<float>(index % 11U) * 0.25F;
  }

  ksj::array::inverse(input.view(), output.view());
  EXPECT_NEAR(1.0F / input(7U), output(7U), 1.0e-6F);
  ksj::array::rsqrt(input.view(), output.view());
  EXPECT_NEAR(1.0F / std::sqrt(input(7U)), output(7U), 1.0e-6F);
  ksj::array::hypot(input.view(), rhs.view(), output.view());
  EXPECT_NEAR(std::hypot(input(7U), rhs(7U)), output(7U), 1.0e-6F);

  const auto inverse = ksj::array::inverse(input);
  const auto rsqrt = ksj::array::rsqrt(input);
  const auto inverse_sqrt = ksj::array::inverse_sqrt(input);
  const auto hypot = ksj::array::hypot(input, rhs);
  EXPECT_NEAR(1.0F / input(256U), inverse(256U), 1.0e-6F);
  EXPECT_NEAR(1.0F / std::sqrt(input(256U)), rsqrt(256U), 1.0e-6F);
  EXPECT_NEAR(rsqrt(256U), inverse_sqrt(256U), 0.0F);
  EXPECT_NEAR(std::hypot(input(256U), rhs(256U)), hypot(256U), 1.0e-6F);
}

TEST(KSpaceJetArrayPooledEigen, RealInverseRsqrtAndHypotPreserveNonContiguousMatrixSemantics) {
  auto input = ksj::array::make_pooled_matrix<double>(3U, 4U);
  auto rhs = ksj::array::make_pooled_matrix<double>(3U, 4U);
  auto output = ksj::array::make_pooled_matrix<double>(3U, 4U);
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      input(row, col) = 0.25 + static_cast<double>(row + col) * 0.125;
      rhs(row, col) = 0.5 + static_cast<double>(row + 2U * col) * 0.25;
    }
  }

  const auto cols = ksj::array::slice(0U, input.cols(), 2U);
  const auto input_view = input.subview(ksj::array::slice(0U, input.rows()), cols);
  const auto rhs_view = rhs.subview(ksj::array::slice(0U, rhs.rows()), cols);
  const auto output_view = output.subview(ksj::array::slice(0U, output.rows()), cols);
  ksj::array::hypot(input_view, rhs_view, output_view);
  EXPECT_NEAR(std::hypot(input(2U, 2U), rhs(2U, 2U)), output(2U, 2U), 1.0e-12);
}

TEST(KSpaceJetArrayPooledEigen, MultiplyAccumulateSupportsExactInputOutputAlias) {
  auto values = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  auto scale = ksj::array::make_pooled_vector<ksj::base::cf32>(2U);
  values(0U) = {1.0F, 2.0F};
  values(1U) = {3.0F, -1.0F};
  scale(0U) = {2.0F, 0.0F};
  scale(1U) = {0.0F, 2.0F};

  ksj::array::multiply_accumulate(values.view(), scale.view(), values.view());

  EXPECT_FLOAT_EQ(3.0F, values(0U).real());
  EXPECT_FLOAT_EQ(6.0F, values(0U).imag());
  EXPECT_FLOAT_EQ(5.0F, values(1U).real());
  EXPECT_FLOAT_EQ(5.0F, values(1U).imag());
}

TEST(KSpaceJetArrayPooledEigen, ComplexUnitPhasorNormalizesMagnitudeAndPreservesZeros) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 2U, 1U);
  auto output = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 2U, 1U);

  input(0U, 0U, 0U) = {3.0F, 4.0F};
  input(0U, 1U, 0U) = {0.0F, 0.0F};
  input(1U, 0U, 0U) = {1.0e-8F, 0.0F};
  input(1U, 1U, 0U) = {-5.0F, 0.0F};

  ksj::array::complex_unit_phasor(input.view(), output.view(), 1.0e-6F);

  EXPECT_NEAR(0.6F, output(0U, 0U, 0U).real(), 1.0e-6F);
  EXPECT_NEAR(0.8F, output(0U, 0U, 0U).imag(), 1.0e-6F);
  EXPECT_EQ(ksj::base::cf32{}, output(0U, 1U, 0U));
  EXPECT_EQ(ksj::base::cf32{}, output(1U, 0U, 0U));
  EXPECT_NEAR(-1.0F, output(1U, 1U, 0U).real(), 1.0e-6F);
  EXPECT_NEAR(0.0F, output(1U, 1U, 0U).imag(), 1.0e-6F);
}

TEST(KSpaceJetArrayPooledEigen, ComplexConjugateProductScaledSupportsExplicitAndAccumulatingForms) {
  auto phase = ksj::array::make_pooled_cube<ksj::base::cf32>(1U, 2U, 2U);
  auto signal = ksj::array::make_pooled_cube<ksj::base::cf32>(1U, 2U, 2U);
  auto output = ksj::array::make_pooled_cube<ksj::base::cf32>(1U, 2U, 2U);
  auto accumulator = ksj::array::make_pooled_cube<ksj::base::cf32>(1U, 2U, 2U);

  for (std::size_t slice = 0U; slice < phase.dim2(); ++slice) {
    for (std::size_t col = 0U; col < phase.dim1(); ++col) {
      const auto value = static_cast<float>(1U + col + 2U * slice);
      phase(0U, col, slice) = {value, value + 1.0F};
      signal(0U, col, slice) = {value + 2.0F, -value};
      accumulator(0U, col, slice) = {10.0F + value, -5.0F};
    }
  }

  ksj::array::conjugate_product_scaled(phase.view(), signal.view(), 0.25F, output.view());
  ksj::array::accumulate_conjugate_product_scaled(accumulator.view(), phase.view(), signal.view(), 0.25F);

  for (std::size_t slice = 0U; slice < phase.dim2(); ++slice) {
    for (std::size_t col = 0U; col < phase.dim1(); ++col) {
      const auto expected = std::conj(phase(0U, col, slice)) * signal(0U, col, slice) * 0.25F;
      EXPECT_NEAR(expected.real(), output(0U, col, slice).real(), 1.0e-6F);
      EXPECT_NEAR(expected.imag(), output(0U, col, slice).imag(), 1.0e-6F);
      EXPECT_NEAR((ksj::base::cf32{10.0F + static_cast<float>(1U + col + 2U * slice), -5.0F} + expected).real(),
                  accumulator(0U, col, slice).real(), 1.0e-6F);
      EXPECT_NEAR((ksj::base::cf32{10.0F + static_cast<float>(1U + col + 2U * slice), -5.0F} + expected).imag(),
                  accumulator(0U, col, slice).imag(), 1.0e-6F);
    }
  }
}

TEST(KSpaceJetArrayPooledEigen, FusedComplexPhasorAndConjugateProductMatchesSeparateOperationsOnCubeSubview) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf32>(3U, 4U, 2U);
  auto signal = ksj::array::make_pooled_cube<ksj::base::cf32>(3U, 4U, 2U);
  auto phasor = ksj::array::make_pooled_cube<ksj::base::cf32>(3U, 4U, 2U);
  auto accumulator = ksj::array::make_pooled_cube<ksj::base::cf32>(3U, 4U, 2U);
  auto expected_phasor = ksj::array::make_pooled_cube<ksj::base::cf32>(3U, 4U, 2U);
  auto expected_accumulator = ksj::array::make_pooled_cube<ksj::base::cf32>(3U, 4U, 2U);

  for (std::size_t row = 0U; row < input.dim0(); ++row) {
    for (std::size_t col = 0U; col < input.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
        const auto value = static_cast<float>(1U + row + 3U * col + 7U * slice);
        input(row, col, slice) = {value, value + 0.5F};
        signal(row, col, slice) = {value - 2.0F, 0.25F * value};
        phasor(row, col, slice) = {};
        accumulator(row, col, slice) = {value, -value};
        expected_phasor(row, col, slice) = {};
        expected_accumulator(row, col, slice) = accumulator(row, col, slice);
      }
    }
  }

  const auto roi = ksj::array::slice(1U, 3U);
  const auto cols = ksj::array::slice(1U, 3U);
  const auto slices = ksj::array::slice(0U, 2U);
  auto input_roi = input.subview(roi, cols, slices);
  auto signal_roi = signal.subview(roi, cols, slices);
  auto phasor_roi = phasor.subview(roi, cols, slices);
  auto accumulator_roi = accumulator.subview(roi, cols, slices);
  auto expected_phasor_roi = expected_phasor.subview(roi, cols, slices);
  auto expected_accumulator_roi = expected_accumulator.subview(roi, cols, slices);

  ksj::array::complex_unit_phasor(input_roi, expected_phasor_roi, 1.0e-6F);
  ksj::array::accumulate_conjugate_product_scaled(expected_accumulator_roi, expected_phasor_roi, signal_roi, 0.5F);
  ksj::array::complex_unit_phasor_and_accumulate_conjugate_product(input_roi, phasor_roi, accumulator_roi, signal_roi,
                                                                   1.0e-6F, 0.5F);

  for (std::size_t row = 1U; row < 3U; ++row) {
    for (std::size_t col = 1U; col < 3U; ++col) {
      for (std::size_t slice = 0U; slice < 2U; ++slice) {
        EXPECT_NEAR(expected_phasor(row, col, slice).real(), phasor(row, col, slice).real(), 1.0e-6F);
        EXPECT_NEAR(expected_phasor(row, col, slice).imag(), phasor(row, col, slice).imag(), 1.0e-6F);
        EXPECT_NEAR(expected_accumulator(row, col, slice).real(), accumulator(row, col, slice).real(), 1.0e-6F);
        EXPECT_NEAR(expected_accumulator(row, col, slice).imag(), accumulator(row, col, slice).imag(), 1.0e-6F);
      }
    }
  }
}

TEST(KSpaceJetArrayPooledEigen, FusedComplexPhasorAndDividedConjugateProductMatchesSeparateOperationsOnContiguousCube) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 3U, 2U);
  auto signal = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 3U, 2U);
  auto phasor = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 3U, 2U);
  auto accumulator = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 3U, 2U);
  auto expected_phasor = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 3U, 2U);
  auto expected_accumulator = ksj::array::make_pooled_cube<ksj::base::cf32>(2U, 3U, 2U);

  for (std::size_t row = 0U; row < input.dim0(); ++row) {
    for (std::size_t col = 0U; col < input.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
        const auto value = static_cast<float>(1U + row + 5U * col + 11U * slice);
        input(row, col, slice) = {value + 0.25F, -0.5F * value};
        signal(row, col, slice) = {0.75F * value, value - 3.0F};
        accumulator(row, col, slice) = {value, -0.25F * value};
        expected_accumulator(row, col, slice) = accumulator(row, col, slice);
      }
    }
  }
  input(1U, 2U, 1U) = {std::numeric_limits<float>::max() * 0.25F, -std::numeric_limits<float>::max() * 0.25F};

  constexpr auto epsilon = 1.0e-6F;
  constexpr auto divisor = 3.0F;
  for (std::size_t row = 0U; row < input.dim0(); ++row) {
    for (std::size_t col = 0U; col < input.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
        const auto value = input(row, col, slice);
        const auto magnitude = std::abs(value);
        ksj::base::cf32 unit{};
        if (magnitude > epsilon) {
          unit = value / magnitude;
        }
        expected_phasor(row, col, slice) = unit;
        expected_accumulator(row, col, slice) += (std::conj(unit) / divisor) * signal(row, col, slice);
      }
    }
  }

  ksj::array::complex_unit_phasor_and_accumulate_conjugate_product_divided(
    ksj::array::as_const_view(input.view()), phasor.view(), accumulator.view(),
    ksj::array::as_const_view(signal.view()), epsilon, divisor);

  for (std::size_t row = 0U; row < input.dim0(); ++row) {
    for (std::size_t col = 0U; col < input.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
        EXPECT_NEAR(expected_phasor(row, col, slice).real(), phasor(row, col, slice).real(), 1.0e-6F);
        EXPECT_NEAR(expected_phasor(row, col, slice).imag(), phasor(row, col, slice).imag(), 1.0e-6F);
        EXPECT_NEAR(expected_accumulator(row, col, slice).real(), accumulator(row, col, slice).real(), 1.0e-5F);
        EXPECT_NEAR(expected_accumulator(row, col, slice).imag(), accumulator(row, col, slice).imag(), 1.0e-5F);
      }
    }
  }
}

} // namespace
