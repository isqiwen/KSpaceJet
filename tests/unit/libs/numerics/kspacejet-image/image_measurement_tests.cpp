#include "kspacejet/image/image.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetImageMeasurements, ComputesFloatMeasurements) {
  auto image = ksj::array::make_pooled_image<float>(2U, 2U);
  image(0U, 0U) = 1.0F;
  image(0U, 1U) = -2.0F;
  image(1U, 0U) = 3.0F;
  image(1U, 1U) = -4.0F;

  EXPECT_DOUBLE_EQ(-0.5, ksj::image::mean(image));
  const auto mean_stddev = ksj::image::mean_stddev(image);
  EXPECT_DOUBLE_EQ(-0.5, mean_stddev.mean);
  EXPECT_NEAR(std::sqrt(7.25), mean_stddev.stddev, 1.0e-12);

  const auto [min_value, max_value] = ksj::image::minmax(image);
  EXPECT_FLOAT_EQ(-4.0F, min_value);
  EXPECT_FLOAT_EQ(3.0F, max_value);

  EXPECT_DOUBLE_EQ(10.0, ksj::image::norm_l1(image));
  EXPECT_NEAR(std::sqrt(30.0), ksj::image::norm_l2(image), 1.0e-6);
  EXPECT_DOUBLE_EQ(4.0, ksj::image::norm_inf(image));
}

TEST(KSpaceJetImageMeasurements, ComputesDiffNormsOnStridedViews) {
  std::array<float, 12> lhs_storage{1.0F, 2.0F, 99.0F, 3.0F, 4.0F, 99.0F, 5.0F, 6.0F, 99.0F, 7.0F, 8.0F, 99.0F};
  std::array<float, 12> rhs_storage{0.0F, 3.0F, 99.0F, 1.0F, 6.0F, 99.0F, 2.0F, 9.0F, 99.0F, 3.0F, 12.0F, 99.0F};
  const auto lhs =
    ksj::array::ImageView<const float>(lhs_storage.data(), 4U, 3U).subview(ksj::array::_, ksj::array::slice(0U, 2U));
  const auto rhs =
    ksj::array::ImageView<const float>(rhs_storage.data(), 4U, 3U).subview(ksj::array::_, ksj::array::slice(0U, 2U));

  EXPECT_DOUBLE_EQ(20.0, ksj::image::norm_diff_l1(lhs, rhs));
  EXPECT_DOUBLE_EQ(std::sqrt(60.0), ksj::image::norm_diff_l2(lhs, rhs));
  EXPECT_DOUBLE_EQ(4.0, ksj::image::norm_diff_inf(lhs, rhs));
}

TEST(KSpaceJetImageMeasurements, ComputesMeasurementsOnStridedViews) {
  std::array<float, 12> storage{1.0F, 2.0F, 99.0F, 3.0F, 4.0F, 99.0F, 5.0F, 6.0F, 99.0F, 7.0F, 8.0F, 99.0F};
  const auto image =
    ksj::array::ImageView<const float>(storage.data(), 4U, 3U).subview(ksj::array::_, ksj::array::slice(0U, 2U));

  EXPECT_DOUBLE_EQ(4.5, ksj::image::mean(image));
  const auto mean_stddev = ksj::image::mean_stddev(image);
  EXPECT_DOUBLE_EQ(4.5, mean_stddev.mean);
  EXPECT_DOUBLE_EQ(std::sqrt(5.25), mean_stddev.stddev);
  EXPECT_DOUBLE_EQ(36.0, ksj::image::norm_l1(image));
  EXPECT_NEAR(std::sqrt(204.0), ksj::image::norm_l2(image), 1.0e-6);
  EXPECT_DOUBLE_EQ(8.0, ksj::image::norm_inf(image));
}

TEST(KSpaceJetImageMeasurements, FallsBackForDoubleImages) {
  auto image = ksj::array::make_pooled_image<double>(2U, 2U);
  image(0U, 0U) = 1.0;
  image(0U, 1U) = 2.0;
  image(1U, 0U) = 3.0;
  image(1U, 1U) = 4.0;

  EXPECT_DOUBLE_EQ(2.5, ksj::image::mean(image));
  EXPECT_DOUBLE_EQ(10.0, ksj::image::norm_l1(image));
  EXPECT_DOUBLE_EQ(std::sqrt(30.0), ksj::image::norm_l2(image));
  EXPECT_DOUBLE_EQ(4.0, ksj::image::norm_inf(image));
}

TEST(KSpaceJetImageMeasurements, RejectsInvalidInputs) {
  auto image = ksj::array::make_pooled_image<float>(2U, 2U);
  auto wrong_shape = ksj::array::make_pooled_image<float>(2U, 3U);
  auto empty = ksj::array::ImageView<const float>(nullptr, 0U, 0U);

  EXPECT_THROW(static_cast<void>(ksj::image::mean(empty)), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(ksj::image::norm_diff_l1(image, wrong_shape)), std::invalid_argument);
}

} // namespace
