#include "kspacejet/base/types.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_regions.hpp"
#include "kspacejet/image/detail/intel/intel_image_regions.hpp"
#include "kspacejet/image/image.hpp"

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetImage, RotatesFloatImageByZeroDegrees) {
  auto image = ksj::array::make_pooled_image<float>(2, 3);
  image(0, 0) = 1.0F;
  image(0, 1) = 2.0F;
  image(0, 2) = 3.0F;
  image(1, 0) = 4.0F;
  image(1, 1) = 5.0F;
  image(1, 2) = 6.0F;

  const auto rotated = ksj::image::rotate_cubic(image, 0.0);

  ASSERT_EQ(image.rows(), rotated.rows());
  ASSERT_EQ(image.cols(), rotated.cols());
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(image(row, col), rotated(row, col), 1.0e-5F);
    }
  }
}

TEST(KSpaceJetImage, RotatesComplexImageByZeroDegrees) {
  auto image = ksj::array::make_pooled_image<ksj::base::cf32>(2, 2);
  image(0, 0) = {1.0F, 2.0F};
  image(0, 1) = {3.0F, 4.0F};
  image(1, 0) = {5.0F, 6.0F};
  image(1, 1) = {7.0F, 8.0F};

  const auto rotated = ksj::image::rotate_cubic(image, 0.0);

  ASSERT_EQ(image.rows(), rotated.rows());
  ASSERT_EQ(image.cols(), rotated.cols());
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(image(row, col).real(), rotated(row, col).real(), 1.0e-5F);
      EXPECT_NEAR(image(row, col).imag(), rotated(row, col).imag(), 1.0e-5F);
    }
  }
}

TEST(KSpaceJetImage, CubicBsplineSmoothRotateSmoothsComplexImageAtZeroDegrees) {
  auto image = ksj::array::make_pooled_image<ksj::base::cf32>(5, 5);
  ksj::array::fill(image.view(), ksj::base::cf32{});
  image(2, 2) = {36.0F, 72.0F};

  const auto rotated = ksj::image::rotate_cubic_bspline_smooth(image, 0.0);

  for (std::size_t row = 0; row < rotated.rows(); ++row) {
    for (std::size_t col = 0; col < rotated.cols(); ++col) {
      float expected_real = 0.0F;
      if (row >= 1U && row <= 3U && col >= 1U && col <= 3U) {
        const int row_weight = row == 2U ? 4 : 1;
        const int col_weight = col == 2U ? 4 : 1;
        expected_real = static_cast<float>(row_weight * col_weight);
      }
      EXPECT_NEAR(rotated(row, col).real(), expected_real, 1.0e-5F);
      EXPECT_NEAR(rotated(row, col).imag(), 2.0F * expected_real, 1.0e-5F);
    }
  }
}

TEST(KSpaceJetImage, CubicBsplineSmoothRotateReplicatesBoundaryAtZeroDegrees) {
  auto image = ksj::array::make_pooled_image<float>(5, 5);
  ksj::array::fill(image.view(), 0.0F);
  image(0, 0) = 36.0F;

  const auto rotated = ksj::image::rotate_cubic_bspline_smooth(image, 0.0);

  EXPECT_NEAR(rotated(0, 0), 25.0F, 1.0e-5F);
  EXPECT_NEAR(rotated(0, 1), 5.0F, 1.0e-5F);
  EXPECT_NEAR(rotated(1, 0), 5.0F, 1.0e-5F);
  EXPECT_NEAR(rotated(1, 1), 1.0F, 1.0e-5F);
  EXPECT_NEAR(rotated(0, 2), 0.0F, 1.0e-5F);
  EXPECT_NEAR(rotated(2, 0), 0.0F, 1.0e-5F);
}

TEST(KSpaceJetImage, CubicBsplineSmoothRotateUsesPortableFallbackForDoubleImages) {
  auto image = ksj::array::make_pooled_image<double>(5, 5);
  ksj::array::fill(image.view(), 0.0);
  image(2, 2) = 36.0;

  const auto rotated = ksj::image::rotate_cubic_bspline_smooth(image, 0.0);

  EXPECT_NEAR(rotated(2, 2), 16.0, 1.0e-12);
  EXPECT_NEAR(rotated(2, 1), 4.0, 1.0e-12);
  EXPECT_NEAR(rotated(1, 2), 4.0, 1.0e-12);
  EXPECT_NEAR(rotated(1, 1), 1.0, 1.0e-12);
  EXPECT_NEAR(rotated(0, 0), 0.0, 1.0e-12);
}

TEST(KSpaceJetImage, PadsWithReplicatedBorder) {
  auto image = ksj::array::make_pooled_image<int>(2, 2);
  image(0, 0) = 1;
  image(0, 1) = 2;
  image(1, 0) = 3;
  image(1, 1) = 4;

  const auto output = ksj::image::pad(image, 1, 1, 1, 1, ksj::image::BorderMode::replicate);

  ASSERT_EQ(4U, output.rows());
  ASSERT_EQ(4U, output.cols());
  EXPECT_EQ(1, output(0, 0));
  EXPECT_EQ(1, output(1, 1));
  EXPECT_EQ(4, output(3, 3));
}

TEST(KSpaceJetImage, PadsImageViewsWithReflect101Border) {
  auto image = ksj::array::make_pooled_image<int>(2, 3);
  image(0, 0) = 1;
  image(0, 1) = 2;
  image(0, 2) = 3;
  image(1, 0) = 4;
  image(1, 1) = 5;
  image(1, 2) = 6;
  auto output = ksj::array::make_pooled_image<int>(4, 5);

  ksj::image::pad(ksj::array::as_const_view(image.view()), output.view(), 0, 2, 0, 2,
                  ksj::image::BorderMode::reflect101);

  ASSERT_EQ(4U, output.rows());
  ASSERT_EQ(5U, output.cols());
  EXPECT_EQ(1, output(0, 0));
  EXPECT_EQ(3, output(0, 2));
  EXPECT_EQ(2, output(0, 3));
  EXPECT_EQ(1, output(0, 4));
  EXPECT_EQ(4, output(1, 0));
  EXPECT_EQ(6, output(1, 2));
  EXPECT_EQ(5, output(1, 3));
  EXPECT_EQ(4, output(1, 4));
  EXPECT_EQ(1, output(2, 0));
  EXPECT_EQ(3, output(2, 2));
  EXPECT_EQ(2, output(2, 3));
  EXPECT_EQ(1, output(2, 4));
}

TEST(KSpaceJetImage, IntelPadReplicateMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(2, 3);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * 10U + col + 1U);
    }
  }
  auto reference = ksj::array::make_pooled_image<float>(5, 6);
  auto intel = ksj::array::make_pooled_image<float>(5, 6);

  ksj::image::detail::eigen::pad(ksj::array::as_const_view(image.view()), reference.view(), 1, 2, 2, 1,
                                 ksj::image::BorderMode::replicate, 0.0F);
  ASSERT_TRUE(ksj::image::detail::intel::pad(ksj::array::as_const_view(image.view()), intel.view(), 1, 2, 2, 1,
                                             ksj::image::BorderMode::replicate));

  for (std::size_t row = 0; row < reference.rows(); ++row) {
    for (std::size_t col = 0; col < reference.cols(); ++col) {
      EXPECT_FLOAT_EQ(reference(row, col), intel(row, col));
    }
  }
}

TEST(KSpaceJetImage, IntelPadConstantZeroMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(2, 3);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * 10U + col + 1U);
    }
  }
  auto reference = ksj::array::make_pooled_image<float>(5, 6);
  auto intel = ksj::array::make_pooled_image<float>(5, 6);

  ksj::image::detail::eigen::pad(ksj::array::as_const_view(image.view()), reference.view(), 1, 2, 2, 1,
                                 ksj::image::BorderMode::constant, 0.0F);
  ASSERT_TRUE(ksj::image::detail::intel::pad(ksj::array::as_const_view(image.view()), intel.view(), 1, 2, 2, 1,
                                             ksj::image::BorderMode::constant));

  for (std::size_t row = 0; row < reference.rows(); ++row) {
    for (std::size_t col = 0; col < reference.cols(); ++col) {
      EXPECT_FLOAT_EQ(reference(row, col), intel(row, col));
    }
  }
}

TEST(KSpaceJetImage, IntelPadMirrorDoesNotClaimReflectSemantics) {
  auto image = ksj::array::make_pooled_image<float>(2, 3);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * 10U + col + 1U);
    }
  }
  auto intel = ksj::array::make_pooled_image<float>(5, 6);

  EXPECT_FALSE(ksj::image::detail::intel::pad(ksj::array::as_const_view(image.view()), intel.view(), 1, 2, 2, 1,
                                              ksj::image::BorderMode::reflect));
}

TEST(KSpaceJetImage, CropsImageRegions) {
  auto image = ksj::array::make_pooled_image<int>(4, 5);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col);
    }
  }

  const auto cropped = ksj::image::crop(image, 1, 2, 2, 3);

  ASSERT_EQ(2U, cropped.rows());
  ASSERT_EQ(3U, cropped.cols());
  EXPECT_EQ(12, cropped(0, 0));
  EXPECT_EQ(14, cropped(0, 2));
  EXPECT_EQ(22, cropped(1, 0));
}

TEST(KSpaceJetImage, WritesCenterCrop) {
  auto image = ksj::array::make_pooled_image<int>(5, 5);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col);
    }
  }
  auto output = ksj::array::make_pooled_image<int>(3, 3);

  ksj::image::center_crop(image, output);

  EXPECT_EQ(11, output(0, 0));
  EXPECT_EQ(22, output(1, 1));
  EXPECT_EQ(33, output(2, 2));
}

TEST(KSpaceJetImage, CenterPadsOrCropsToRequestedCanvas) {
  auto image = ksj::array::make_pooled_image<int>(2, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col + 1U);
    }
  }

  const auto output = ksj::image::center_pad_or_crop(image, 3, 2);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(2U, output.cols());
  EXPECT_EQ(2, output(0, 0));
  EXPECT_EQ(3, output(0, 1));
  EXPECT_EQ(12, output(1, 0));
  EXPECT_EQ(13, output(1, 1));
  EXPECT_EQ(0, output(2, 0));
  EXPECT_EQ(0, output(2, 1));
}

TEST(KSpaceJetImage, CopiesRoiWithOverlapProtection) {
  auto image = ksj::array::make_pooled_image<int>(4, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col);
    }
  }

  ksj::image::copy_roi(image, 0, 0, image, 1, 1, 2, 2);

  EXPECT_EQ(0, image(1, 1));
  EXPECT_EQ(1, image(1, 2));
  EXPECT_EQ(10, image(2, 1));
  EXPECT_EQ(11, image(2, 2));
}

} // namespace
