#include "../eigen_test_adapter.hpp"
#include "kspacejet/image/image.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(KSpaceJetImage, FindsConnectedComponentsWithStats) {
  auto mask = ksj::array::make_pooled_image<int>(4, 5);
  as_eigen(mask).setZero();
  mask(0, 0) = 1;
  mask(0, 1) = 1;
  mask(1, 0) = 1;
  mask(2, 3) = 1;
  mask(3, 3) = 1;

  const auto result = ksj::image::connected_components(mask, ksj::image::Connectivity::four);

  ASSERT_EQ(4U, result.labels.rows());
  ASSERT_EQ(5U, result.labels.cols());
  ASSERT_EQ(2U, result.stats.size());
  EXPECT_EQ(1, result.labels(0, 0));
  EXPECT_EQ(1, result.labels(0, 1));
  EXPECT_EQ(1, result.labels(1, 0));
  EXPECT_EQ(0, result.labels(1, 1));
  EXPECT_EQ(2, result.labels(2, 3));
  EXPECT_EQ(2, result.labels(3, 3));
  EXPECT_EQ(3U, result.stats[0].area);
  EXPECT_EQ(0U, result.stats[0].min_row);
  EXPECT_EQ(1U, result.stats[0].max_row);
  EXPECT_NEAR(1.0 / 3.0, result.stats[0].centroid_col, 1.0e-12);
  EXPECT_EQ(2U, result.stats[1].area);
}

TEST(KSpaceJetImage, SupportsEightConnectedComponents) {
  auto mask = ksj::array::make_pooled_image<int>(2, 2);
  as_eigen(mask).setZero();
  mask(0, 0) = 1;
  mask(1, 1) = 1;

  const auto four = ksj::image::connected_components(mask, ksj::image::Connectivity::four);
  const auto eight = ksj::image::connected_components(mask, ksj::image::Connectivity::eight);

  ASSERT_EQ(2U, four.stats.size());
  ASSERT_EQ(1U, eight.stats.size());
  EXPECT_EQ(eight.labels(0, 0), eight.labels(1, 1));
}

TEST(KSpaceJetImage, WritesConnectedComponents) {
  auto mask = ksj::array::make_pooled_image<float>(3, 3);
  as_eigen(mask).setZero();
  mask(0, 0) = 1.0F;
  mask(2, 2) = 1.0F;
  auto labels = ksj::array::make_pooled_image<ksj::image::ConnectedComponentLabel>(3, 3);
  std::vector<ksj::image::ConnectedComponentStats> stats;

  const auto component_count = ksj::image::connected_components(mask, labels, &stats, ksj::image::Connectivity::four);

  EXPECT_EQ(2U, component_count);
  ASSERT_EQ(2U, stats.size());
  EXPECT_EQ(1, labels(0, 0));
  EXPECT_EQ(2, labels(2, 2));
  EXPECT_EQ(0, labels(1, 1));
}

TEST(KSpaceJetImage, WritesConnectedComponentsIntoStridedView) {
  std::vector<int> mask_storage(5U * 6U, 0);
  auto at = [&mask_storage](const std::size_t row, const std::size_t col) -> int& {
    return mask_storage[row * 6U + col];
  };
  at(1, 2) = 1;
  at(1, 3) = 1;
  at(2, 2) = 1;
  at(3, 4) = 1;

  std::vector<ksj::image::ConnectedComponentLabel> label_storage(5U * 6U, -1);
  ksj::array::ImageView<const int> mask(mask_storage.data(), 5U, 6U);
  ksj::array::ImageView<ksj::image::ConnectedComponentLabel> labels(label_storage.data(), 5U, 6U);
  auto mask_roi = mask.subview(ksj::array::slice(1U, 4U), ksj::array::slice(2U, 5U));
  auto labels_roi = labels.subview(ksj::array::slice(1U, 4U), ksj::array::slice(2U, 5U));
  std::vector<ksj::image::ConnectedComponentStats> stats;

  const auto component_count =
    ksj::image::connected_components(mask_roi, labels_roi, &stats, ksj::image::Connectivity::four);

  EXPECT_EQ(2U, component_count);
  ASSERT_EQ(2U, stats.size());
  EXPECT_EQ(1, labels(1, 2));
  EXPECT_EQ(1, labels(1, 3));
  EXPECT_EQ(1, labels(2, 2));
  EXPECT_EQ(2, labels(3, 4));
  EXPECT_EQ(0, labels(2, 3));
  EXPECT_EQ(-1, labels(0, 0));
  EXPECT_EQ(3U, stats[0].area);
  EXPECT_EQ(0U, stats[0].min_row);
  EXPECT_EQ(0U, stats[0].min_col);
  EXPECT_EQ(1U, stats[0].max_row);
  EXPECT_EQ(1U, stats[0].max_col);
}

TEST(KSpaceJetImage, GrowsRegionFromSeedWithinThresholds) {
  auto image = ksj::array::make_pooled_image<int>(3, 4);
  as_eigen(image).setZero();
  image(0, 1) = 5;
  image(0, 2) = 5;
  image(1, 1) = 5;
  image(1, 2) = 6;
  image(2, 2) = 6;
  image(2, 3) = 6;

  auto mask = ksj::array::make_pooled_image<ksj::image::RegionGrowMaskValue>(3, 4);
  const auto area = ksj::image::region_grow(image, mask, 0, 1, 5, 6, ksj::image::Connectivity::four);

  EXPECT_EQ(6U, area);
  EXPECT_EQ(1, mask(0, 1));
  EXPECT_EQ(1, mask(2, 3));
  EXPECT_EQ(0, mask(0, 0));
}

TEST(KSpaceJetImage, RegionGrowWritesIntoStridedView) {
  std::vector<int> input_storage(5U * 6U, 0);
  std::vector<ksj::image::RegionGrowMaskValue> mask_storage(5U * 6U, 9);
  ksj::array::ImageView<int> input(input_storage.data(), 5U, 6U);
  ksj::array::ImageView<ksj::image::RegionGrowMaskValue> mask(mask_storage.data(), 5U, 6U);
  auto input_roi = input.subview(ksj::array::slice(1U, 4U), ksj::array::slice(1U, 5U));
  auto mask_roi = mask.subview(ksj::array::slice(1U, 4U), ksj::array::slice(1U, 5U));
  input_roi(0U, 0U) = 5;
  input_roi(0U, 1U) = 5;
  input_roi(1U, 1U) = 6;
  input_roi(2U, 1U) = 6;
  input_roi(2U, 2U) = 6;
  input_roi(2U, 3U) = 6;

  const auto area = ksj::image::region_grow(input_roi, mask_roi, 0U, 0U, 5, 6, ksj::image::Connectivity::four);

  EXPECT_EQ(6U, area);
  EXPECT_EQ(1, mask_roi(0U, 0U));
  EXPECT_EQ(1, mask_roi(2U, 3U));
  EXPECT_EQ(0, mask_roi(1U, 0U));
  EXPECT_EQ(9, mask(0U, 0U));
  EXPECT_EQ(9, mask(4U, 5U));
}

TEST(KSpaceJetImage, RegionGrowHonorsConnectivityAndInPlaceMask) {
  auto image = ksj::array::make_pooled_image<int>(2, 2);
  as_eigen(image).setZero();
  image(0, 0) = 1;
  image(1, 1) = 1;

  const auto four = ksj::image::region_grow(image, 0, 0, 1, 1, ksj::image::Connectivity::four);
  const auto eight = ksj::image::region_grow(image, 0, 0, 1, 1, ksj::image::Connectivity::eight);
  EXPECT_EQ(1, four(0, 0));
  EXPECT_EQ(0, four(1, 1));
  EXPECT_EQ(1, eight(1, 1));

  auto mask_as_input = ksj::array::make_pooled_image<ksj::image::RegionGrowMaskValue>(2, 2);
  as_eigen(mask_as_input).setOnes();
  const auto area =
    ksj::image::region_grow(mask_as_input, mask_as_input, 0, 0, static_cast<ksj::image::RegionGrowMaskValue>(1),
                            static_cast<ksj::image::RegionGrowMaskValue>(1), ksj::image::Connectivity::four);
  EXPECT_EQ(4U, area);
  EXPECT_EQ(1, mask_as_input(1, 1));
}

} // namespace
