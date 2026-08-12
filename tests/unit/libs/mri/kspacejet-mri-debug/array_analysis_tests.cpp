#include "kspacejet/mri/debug/array_analysis.hpp"
#include "kspacejet/mri/debug/debug_event.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

TEST(KSpaceJetMriDebugArrayAnalysis, DescribesContiguousAndStridedLayouts) {
  auto image = ksj::array::make_pooled_image<float>(2, 3);

  const auto image_layout = ksj::mri::debug::describe_layout(image);
  EXPECT_EQ("image", image_layout.kind);
  EXPECT_EQ(2U, image_layout.rank);
  EXPECT_EQ((std::vector<std::size_t>{2U, 3U}), image_layout.extents);
  EXPECT_EQ((std::vector<std::size_t>{3U, 1U}), image_layout.strides);
  EXPECT_TRUE(image_layout.contiguous);
  EXPECT_NE(std::string::npos, image_layout.contiguity_note.find("contiguous"));

  const auto column = image.view().col(1U);
  const auto column_layout = ksj::mri::debug::describe_layout(column);
  EXPECT_EQ("vector", column_layout.kind);
  EXPECT_EQ((std::vector<std::size_t>{2U}), column_layout.extents);
  EXPECT_EQ((std::vector<std::size_t>{3U}), column_layout.strides);
  EXPECT_FALSE(column_layout.contiguous);
  EXPECT_NE(std::string::npos, column_layout.contiguity_note.find("axis 0"));
}

TEST(KSpaceJetMriDebugArrayAnalysis, SummarizesRealAndComplexArrays) {
  auto matrix = ksj::array::make_pooled_matrix<float>(2, 2);
  matrix(0, 0) = 1.0F;
  matrix(0, 1) = 2.0F;
  matrix(1, 0) = 3.0F;
  matrix(1, 1) = 4.0F;

  const auto summary = ksj::mri::debug::summarize_array(matrix);
  EXPECT_FALSE(summary.complex_values);
  EXPECT_EQ(4U, summary.finite_count);
  EXPECT_EQ(0U, summary.nan_count);
  EXPECT_EQ(0U, summary.inf_count);
  EXPECT_EQ(0U, summary.zero_count);
  EXPECT_DOUBLE_EQ(1.0, summary.min_real);
  EXPECT_DOUBLE_EQ(4.0, summary.max_real);
  EXPECT_DOUBLE_EQ(2.5, summary.mean_real);
  EXPECT_DOUBLE_EQ(1.0, summary.min_abs);
  EXPECT_DOUBLE_EQ(4.0, summary.max_abs);

  auto complex_vector = ksj::array::make_pooled_vector<ksj::base::cf32>(2);
  complex_vector(0) = {1.0F, 0.0F};
  complex_vector(1) = {0.0F, 1.0F};

  const auto complex_summary = ksj::mri::debug::summarize_array(complex_vector);
  EXPECT_TRUE(complex_summary.complex_values);
  EXPECT_EQ(2U, complex_summary.finite_count);
  EXPECT_DOUBLE_EQ(1.0, complex_summary.min_abs);
  EXPECT_DOUBLE_EQ(1.0, complex_summary.max_abs);
  EXPECT_NEAR(0.0, complex_summary.min_phase, 1.0e-6);
  EXPECT_NEAR(std::atan2(1.0, 0.0), complex_summary.max_phase, 1.0e-6);
}

TEST(KSpaceJetMriDebugArrayAnalysis, ComparesArraysAndReportsIntegerDeltaHistogram) {
  auto lhs = ksj::array::make_pooled_vector<std::uint16_t>(4);
  auto rhs = ksj::array::make_pooled_vector<std::uint16_t>(4);
  lhs(0) = 1U;
  lhs(1) = 2U;
  lhs(2) = 3U;
  lhs(3) = 4U;
  rhs(0) = 1U;
  rhs(1) = 3U;
  rhs(2) = 2U;
  rhs(3) = 260U;

  const auto comparison = ksj::mri::debug::compare_arrays(lhs, rhs);
  EXPECT_TRUE(comparison.same_shape);
  EXPECT_EQ(4U, comparison.compared_count);
  EXPECT_EQ(3U, comparison.exact_mismatch_count);
  EXPECT_EQ(3U, comparison.tolerance_mismatch_count);
  EXPECT_DOUBLE_EQ(256.0, comparison.max_abs_diff);
  EXPECT_EQ(3U, comparison.max_abs_diff_linear_index);
  ASSERT_EQ(3U, comparison.integer_delta_histogram.size());
  EXPECT_EQ(-1, comparison.integer_delta_histogram[0].delta);
  EXPECT_EQ(1U, comparison.integer_delta_histogram[0].count);
  EXPECT_EQ(1, comparison.integer_delta_histogram[1].delta);
  EXPECT_EQ(1U, comparison.integer_delta_histogram[1].count);
  EXPECT_EQ(256, comparison.integer_delta_histogram[2].delta);
  EXPECT_EQ(1U, comparison.integer_delta_histogram[2].count);
}

TEST(KSpaceJetMriDebugEvent, EscapesJsonStrings) {
  EXPECT_EQ("a\\\\b\\\"c\\n", ksj::mri::debug::json_escape("a\\b\"c\n"));
}
