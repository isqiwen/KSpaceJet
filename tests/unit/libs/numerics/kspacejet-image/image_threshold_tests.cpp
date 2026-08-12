#include "../eigen_test_adapter.hpp"
#include "kspacejet/image/detail/intel/intel_image_thresholds.hpp"
#include "kspacejet/image/image.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(KSpaceJetImage, ThresholdsMatrix) {
  auto image = ksj::array::make_pooled_image<double>(2, 2);
  image(0, 0) = 0.1;
  image(0, 1) = 0.6;
  image(1, 0) = 0.4;
  image(1, 1) = 0.9;

  const auto output = ksj::image::threshold(image, 0.5, 0.0, 1.0);

  EXPECT_DOUBLE_EQ(0.0, output(0, 0));
  EXPECT_DOUBLE_EQ(1.0, output(0, 1));
  EXPECT_DOUBLE_EQ(0.0, output(1, 0));
  EXPECT_DOUBLE_EQ(1.0, output(1, 1));
}

TEST(KSpaceJetImage, ThresholdWritesIntoStridedView) {
  std::vector<double> input_storage(4U * 5U, 0.0);
  std::vector<double> output_storage(4U * 5U, -1.0);
  ksj::array::ImageView<double> input(input_storage.data(), 4U, 5U);
  ksj::array::ImageView<double> output(output_storage.data(), 4U, 5U);
  auto input_roi = input.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 4U));
  auto output_roi = output.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 4U));
  input_roi(0U, 0U) = 0.1;
  input_roi(0U, 1U) = 0.6;
  input_roi(0U, 2U) = 0.4;
  input_roi(1U, 0U) = 0.9;
  input_roi(1U, 1U) = 0.5;
  input_roi(1U, 2U) = 0.0;

  ksj::image::threshold(input_roi, output_roi, 0.5, 0.0, 1.0);

  EXPECT_DOUBLE_EQ(0.0, output_roi(0U, 0U));
  EXPECT_DOUBLE_EQ(1.0, output_roi(0U, 1U));
  EXPECT_DOUBLE_EQ(0.0, output_roi(0U, 2U));
  EXPECT_DOUBLE_EQ(1.0, output_roi(1U, 0U));
  EXPECT_DOUBLE_EQ(1.0, output_roi(1U, 1U));
  EXPECT_DOUBLE_EQ(0.0, output_roi(1U, 2U));
  EXPECT_DOUBLE_EQ(-1.0, output(0U, 0U));
  EXPECT_DOUBLE_EQ(-1.0, output(3U, 4U));
}

TEST(KSpaceJetImage, IntelThresholdMatchesPublicBoundarySemantics) {
  auto input = ksj::array::make_pooled_image<float>(2U, 3U);
  input(0U, 0U) = 0.1F;
  input(0U, 1U) = 0.5F;
  input(0U, 2U) = 0.9F;
  input(1U, 0U) = -1.0F;
  input(1U, 1U) = 0.49F;
  input(1U, 2U) = 0.51F;
  auto output = ksj::array::make_pooled_image<float>(2U, 3U);

  ASSERT_TRUE(
    ksj::image::detail::intel::threshold(ksj::array::as_const_view(input.view()), output.view(), 0.5F, -2.0F, 7.0F));

  EXPECT_FLOAT_EQ(-2.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(7.0F, output(0U, 1U));
  EXPECT_FLOAT_EQ(7.0F, output(0U, 2U));
  EXPECT_FLOAT_EQ(-2.0F, output(1U, 0U));
  EXPECT_FLOAT_EQ(-2.0F, output(1U, 1U));
  EXPECT_FLOAT_EQ(7.0F, output(1U, 2U));
}

TEST(KSpaceJetImage, NormalizesMinMax) {
  auto image = ksj::array::make_pooled_image<double>(2, 1);
  image(0, 0) = 2.0;
  image(1, 0) = 6.0;

  const auto output = ksj::image::normalize_minmax(image);

  EXPECT_DOUBLE_EQ(0.0, output(0, 0));
  EXPECT_DOUBLE_EQ(1.0, output(1, 0));
}

TEST(KSpaceJetImage, NormalizeMinMaxWritesIntoStridedView) {
  std::vector<float> input_storage(4U * 5U, 0.0F);
  std::vector<float> output_storage(4U * 5U, -1.0F);
  ksj::array::ImageView<float> input(input_storage.data(), 4U, 5U);
  ksj::array::ImageView<float> output(output_storage.data(), 4U, 5U);
  auto input_roi = input.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  auto output_roi = output.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  input_roi(0U, 0U) = 2.0F;
  input_roi(0U, 1U) = 4.0F;
  input_roi(1U, 0U) = 6.0F;
  input_roi(1U, 1U) = 10.0F;

  ksj::image::normalize_minmax(input_roi, output_roi);

  EXPECT_FLOAT_EQ(0.0F, output_roi(0U, 0U));
  EXPECT_FLOAT_EQ(0.25F, output_roi(0U, 1U));
  EXPECT_FLOAT_EQ(0.5F, output_roi(1U, 0U));
  EXPECT_FLOAT_EQ(1.0F, output_roi(1U, 1U));
  EXPECT_FLOAT_EQ(-1.0F, output(0U, 0U));
}

TEST(KSpaceJetImage, BuildsOtsuMask) {
  auto image = ksj::array::make_pooled_image<float>(2, 4);
  image(0, 0) = 0.0F;
  image(0, 1) = 0.0F;
  image(0, 2) = 0.0F;
  image(0, 3) = 0.0F;
  image(1, 0) = 2.0F;
  image(1, 1) = 2.0F;
  image(1, 2) = 2.0F;
  image(1, 3) = 2.0F;

  const auto threshold = ksj::image::otsu_threshold(image);
  EXPECT_GE(threshold, 0.0F);
  EXPECT_LT(threshold, 2.0F);

  const auto mask = ksj::image::otsu_mask(image);
  EXPECT_FLOAT_EQ(0.0F, mask(0, 0));
  EXPECT_FLOAT_EQ(0.0F, mask(0, 3));
  EXPECT_FLOAT_EQ(1.0F, mask(1, 0));
  EXPECT_FLOAT_EQ(1.0F, mask(1, 3));
}

TEST(KSpaceJetImage, OtsuMaskWritesIntoStridedView) {
  std::vector<float> input_storage(4U * 6U, 0.0F);
  std::vector<float> output_storage(4U * 6U, -1.0F);
  ksj::array::ImageView<float> input(input_storage.data(), 4U, 6U);
  ksj::array::ImageView<float> output(output_storage.data(), 4U, 6U);
  auto input_roi = input.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 5U));
  auto output_roi = output.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 5U));
  for (std::size_t col = 0U; col < input_roi.cols(); ++col) {
    input_roi(0U, col) = 0.0F;
    input_roi(1U, col) = 2.0F;
  }

  ksj::image::otsu_mask(input_roi, output_roi);

  EXPECT_FLOAT_EQ(0.0F, output_roi(0U, 0U));
  EXPECT_FLOAT_EQ(0.0F, output_roi(0U, 3U));
  EXPECT_FLOAT_EQ(1.0F, output_roi(1U, 0U));
  EXPECT_FLOAT_EQ(1.0F, output_roi(1U, 3U));
  EXPECT_FLOAT_EQ(-1.0F, output(0U, 0U));
}

TEST(KSpaceJetImage, ComputesReferenceMedian3x3InteriorAndZerosBorder) {
  auto input = ksj::array::make_pooled_image<float>(3, 3);
  input(0, 0) = 9.0F;
  input(0, 1) = 2.0F;
  input(0, 2) = 7.0F;
  input(1, 0) = 4.0F;
  input(1, 1) = 5.0F;
  input(1, 2) = 6.0F;
  input(2, 0) = 3.0F;
  input(2, 1) = 8.0F;
  input(2, 2) = 1.0F;
  auto output = ksj::array::make_pooled_image<float>(3, 3);

  ksj::image::median3x3_interior_zero(input, output);

  EXPECT_FLOAT_EQ(5.0F, output(1, 1));
  EXPECT_FLOAT_EQ(0.0F, output(0, 0));
  EXPECT_FLOAT_EQ(0.0F, output(0, 1));
  EXPECT_FLOAT_EQ(0.0F, output(2, 2));
}

TEST(KSpaceJetImage, ComputesReferencePhaseQualityMap3x3) {
  auto input = ksj::array::make_pooled_image<float>(3, 3);
  as_eigen(input).setZero();
  auto output = ksj::array::make_pooled_image<float>(3, 3);

  ksj::image::phase_quality_map3x3(input, output);

  EXPECT_FLOAT_EQ(1.0F, output(1, 1));
  EXPECT_FLOAT_EQ(0.0F, output(0, 0));
  EXPECT_FLOAT_EQ(0.0F, output(2, 2));
}

TEST(KSpaceJetImage, MultiOtsuScalesInputInPlace) {
  auto image = ksj::array::make_pooled_image<float>(2, 4);
  image(0, 0) = 0.0F;
  image(0, 1) = 1.0F;
  image(0, 2) = 1.0F;
  image(0, 3) = 5.0F;
  image(1, 0) = 5.0F;
  image(1, 1) = 10.0F;
  image(1, 2) = 10.0F;
  image(1, 3) = 10.0F;

  const auto thresholds = ksj::image::multi_otsu_thresholds_scaled_inplace(image);

  ASSERT_EQ(4U, thresholds.size());
  EXPECT_EQ(0, thresholds[0]);
  EXPECT_LE(thresholds[1], thresholds[2]);
  EXPECT_LE(thresholds[2], thresholds[3]);
  EXPECT_FLOAT_EQ(0.0F, image(0, 0));
  EXPECT_FLOAT_EQ(25.5F, image(0, 1));
  EXPECT_FLOAT_EQ(127.5F, image(0, 3));
  EXPECT_FLOAT_EQ(255.0F, image(1, 3));
}

} // namespace
