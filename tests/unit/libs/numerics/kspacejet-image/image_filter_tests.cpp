#include "../eigen_test_adapter.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_filters.hpp"
#include "kspacejet/image/detail/intel/intel_image_filters.hpp"
#include "kspacejet/image/image.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

TEST(KSpaceJetImage, FiltersWithTopLeftAnchor) {
  auto image = ksj::array::make_pooled_image<float>(2, 2);
  image(0, 0) = 1.0F;
  image(0, 1) = 2.0F;
  image(1, 0) = 3.0F;
  image(1, 1) = 4.0F;
  auto kernel = ksj::array::make_pooled_image<float>(2, 2);
  as_eigen(kernel).setOnes();

  const auto output =
    ksj::image::filter2d(image, kernel, ksj::image::BorderMode::constant, ksj::image::FilterAnchor::top_left);

  EXPECT_FLOAT_EQ(10.0F, output(0, 0));
  EXPECT_FLOAT_EQ(6.0F, output(0, 1));
  EXPECT_FLOAT_EQ(7.0F, output(1, 0));
  EXPECT_FLOAT_EQ(4.0F, output(1, 1));
}

TEST(KSpaceJetImage, FiltersVolumeInPlaceWithReplicatedBorder) {
  constexpr std::size_t rows = 2U;
  constexpr std::size_t cols = 2U;
  constexpr std::size_t slices = 2U;
  constexpr std::size_t physical_cols = cols + 1U;
  std::vector<double> storage(rows * physical_cols * slices);
  const auto index = [=](const std::size_t row, const std::size_t col, const std::size_t slice) {
    return (row * physical_cols + col) * slices + slice;
  };
  for (std::size_t row = 0U; row < rows; ++row) {
    for (std::size_t col = 0U; col < cols; ++col) {
      for (std::size_t slice = 0U; slice < slices; ++slice) {
        storage[index(row, col, slice)] = static_cast<double>(row + 10U * col + 100U * slice);
      }
    }
  }

  auto padded_volume = ksj::array::CubeView<double>(storage.data(), rows, physical_cols, slices);
  auto volume = padded_volume.subview(ksj::array::_, ksj::array::slice(0U, cols), ksj::array::_);
  auto kernel = ksj::array::make_pooled_cube<double>(3U, 1U, 1U);
  ksj::array::fill(kernel.view(), 1.0);

  ksj::image::filter3d_replicate_in_place(volume, kernel.view());

  EXPECT_DOUBLE_EQ(1.0, volume(0, 0, 0));
  EXPECT_DOUBLE_EQ(2.0, volume(1, 0, 0));
  EXPECT_DOUBLE_EQ(31.0, volume(0, 1, 0));
  EXPECT_DOUBLE_EQ(32.0, volume(1, 1, 0));
  EXPECT_DOUBLE_EQ(301.0, volume(0, 0, 1));
  EXPECT_DOUBLE_EQ(302.0, volume(1, 0, 1));
  EXPECT_DOUBLE_EQ(331.0, volume(0, 1, 1));
  EXPECT_DOUBLE_EQ(332.0, volume(1, 1, 1));
  EXPECT_DOUBLE_EQ(301.0, storage[index(0, 0, 1)]);
}

TEST(KSpaceJetImage, AppliesBoxFilter) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 9.0;

  const auto output = ksj::image::box_filter(image, 3, ksj::image::BorderMode::constant);

  EXPECT_DOUBLE_EQ(1.0, output(1, 1));
  EXPECT_DOUBLE_EQ(1.0, output(0, 0));

  const auto view_output =
    ksj::image::box_filter(ksj::array::as_const_view(image.view()), 3, ksj::image::BorderMode::constant);
  EXPECT_DOUBLE_EQ(1.0, view_output(1, 1));
  EXPECT_DOUBLE_EQ(1.0, view_output(0, 0));
}

TEST(KSpaceJetImage, WritesBoxFilter) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 9.0;
  auto output = ksj::array::make_pooled_image<double>(3, 3);

  ksj::image::box_filter(image, output, 3, ksj::image::BorderMode::constant);

  EXPECT_DOUBLE_EQ(1.0, output(1, 1));
  EXPECT_DOUBLE_EQ(1.0, output(0, 0));

  ksj::array::fill(output.view(), 0.0);
  ksj::image::box_filter(ksj::array::as_const_view(image.view()), output.view(), 3, ksj::image::BorderMode::constant);
  EXPECT_DOUBLE_EQ(1.0, output(1, 1));
  EXPECT_DOUBLE_EQ(1.0, output(0, 0));

  ksj::image::box_filter(image.view(), image.view(), 3, ksj::image::BorderMode::constant);
  EXPECT_DOUBLE_EQ(1.0, image(1, 1));
  EXPECT_DOUBLE_EQ(1.0, image(0, 0));
}

TEST(KSpaceJetImage, IntelBoxFilterMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(5U, 6U);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>((row * 11U + col * 3U) % 17U);
    }
  }
  auto reference = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  auto intel = ksj::array::make_pooled_image<float>(image.rows(), image.cols());

  ksj::image::detail::eigen::box_filter(ksj::array::as_const_view(image.view()), reference.view(), 3U, 3U,
                                        ksj::image::BorderMode::replicate);
  ASSERT_TRUE(ksj::image::detail::intel::box_filter(ksj::array::as_const_view(image.view()), intel.view(), 3U, 3U,
                                                    ksj::image::BorderMode::replicate));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), intel(row, col), 1.0e-5F);
    }
  }
}

TEST(KSpaceJetImage, AppliesGaussianBlur) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 1.0;

  const auto output = ksj::image::gaussian_blur(image, 3, 1.0, ksj::image::BorderMode::constant);

  const auto side_weight = std::exp(-0.5);
  const auto denom = 1.0 + 2.0 * side_weight;
  const auto center_weight = 1.0 / (denom * denom);
  const auto edge_weight = side_weight / (denom * denom);
  const auto corner_weight = side_weight * side_weight / (denom * denom);
  EXPECT_NEAR(center_weight, output(1, 1), 1.0e-12);
  EXPECT_NEAR(edge_weight, output(0, 1), 1.0e-12);
  EXPECT_NEAR(corner_weight, output(0, 0), 1.0e-12);
}

TEST(KSpaceJetImage, WritesGaussianBlur) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 1.0;
  auto output = ksj::array::make_pooled_image<double>(3, 3);

  ksj::image::gaussian_blur(image, output, 3, 1.0, ksj::image::BorderMode::constant);

  EXPECT_GT(output(1, 1), output(0, 1));
  EXPECT_GT(output(0, 1), output(0, 0));

  ksj::image::gaussian_blur(image, image, 3, 1.0, ksj::image::BorderMode::constant);
  EXPECT_NEAR(output(1, 1), image(1, 1), 1.0e-12);
}

TEST(KSpaceJetImage, IntelGaussianBlurMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(5, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * 5U + col * 2U) * 0.25F;
    }
  }
  auto reference = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  auto intel = ksj::array::make_pooled_image<float>(image.rows(), image.cols());

  ksj::image::detail::eigen::gaussian_blur(image.view(), reference.view(), 3, 1.0, ksj::image::BorderMode::replicate);
  ASSERT_TRUE(ksj::image::detail::intel::gaussian_blur(ksj::array::as_const_view(image.view()), intel.view(), 3, 1.0,
                                                       ksj::image::BorderMode::replicate));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), intel(row, col), 1.0e-4F);
    }
  }
}

TEST(KSpaceJetImage, AppliesBilateralFilter) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 1.0;

  const auto edge_preserving = ksj::image::bilateral_filter(image, 3, 0.1, 1.0, ksj::image::BorderMode::constant);
  EXPECT_GT(edge_preserving(1, 1), 0.99);
  EXPECT_LT(edge_preserving(0, 1), 0.01);

  const auto smooth = ksj::image::bilateral_filter(image, 3, 10.0, 1.0, ksj::image::BorderMode::constant);
  EXPECT_LT(smooth(1, 1), edge_preserving(1, 1));
  EXPECT_GT(smooth(0, 1), edge_preserving(0, 1));
}

TEST(KSpaceJetImage, WritesBilateralFilter) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 1.0;
  auto output = ksj::array::make_pooled_image<double>(3, 3);

  ksj::image::bilateral_filter(image, output, 3, 0.1, 1.0, ksj::image::BorderMode::constant);
  EXPECT_GT(output(1, 1), 0.99);

  ksj::image::bilateral_filter(image, image, 3, 0.1, 1.0, ksj::image::BorderMode::constant);
  EXPECT_NEAR(output(1, 1), image(1, 1), 1.0e-12);
}

TEST(KSpaceJetImage, OpenCvBilateralFilterMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(5, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>((row * 7U + col * 3U) % 11U) * 0.125F;
    }
  }

  auto reference = ksj::array::make_pooled_image<float>(5, 4);
  auto opencv = ksj::array::make_pooled_image<float>(5, 4);
  ksj::image::detail::eigen::bilateral_filter(image, reference, 5, 0.5, 1.4, ksj::image::BorderMode::replicate);
  ASSERT_TRUE(
    ksj::image::detail::opencv::bilateral_filter(image, opencv, 5, 0.5, 1.4, ksj::image::BorderMode::replicate));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), opencv(row, col), 1.0e-4F);
    }
  }
}

TEST(KSpaceJetImage, IntelMedianFilterMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(5U, 6U);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>((row * 7U + col * 5U + row * col) % 23U);
    }
  }
  auto reference = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  auto intel = ksj::array::make_pooled_image<float>(image.rows(), image.cols());

  ksj::image::detail::eigen::median_filter(ksj::array::as_const_view(image.view()), reference.view(), 3U,
                                           ksj::image::BorderMode::replicate);
  ASSERT_TRUE(ksj::image::detail::intel::median_filter(ksj::array::as_const_view(image.view()), intel.view(), 3U,
                                                       ksj::image::BorderMode::replicate));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_FLOAT_EQ(reference(row, col), intel(row, col));
    }
  }
}

TEST(KSpaceJetImage, AppliesMedianFilter) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setOnes();
  image(1, 1) = 99.0;

  const auto output = ksj::image::median_filter(image, 3, ksj::image::BorderMode::replicate);

  EXPECT_DOUBLE_EQ(1.0, output(1, 1));
  EXPECT_DOUBLE_EQ(1.0, output(0, 0));
}

TEST(KSpaceJetImage, WritesMedianFilter) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setOnes();
  image(1, 1) = 99.0;
  auto output = ksj::array::make_pooled_image<double>(3, 3);

  ksj::image::median_filter(image, output, 3, ksj::image::BorderMode::replicate);

  EXPECT_DOUBLE_EQ(1.0, output(1, 1));

  ksj::image::median_filter(image, image, 3, ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(1.0, image(1, 1));
}

TEST(KSpaceJetImage, AppliesSobelOperators) {
  auto horizontal = ksj::array::make_pooled_image<double>(3, 3);
  auto vertical = ksj::array::make_pooled_image<double>(3, 3);
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      horizontal(row, col) = static_cast<double>(col);
      vertical(row, col) = static_cast<double>(row);
    }
  }

  const auto dx = ksj::image::sobel_x(horizontal, ksj::image::BorderMode::replicate);
  const auto dy = ksj::image::sobel_y(horizontal, ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, dx(1, 1));
  EXPECT_DOUBLE_EQ(0.0, dy(1, 1));

  const auto vertical_dy = ksj::image::sobel_y(vertical, ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, vertical_dy(1, 1));
}

TEST(KSpaceJetImage, WritesSobelOperators) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      image(row, col) = static_cast<double>(col);
    }
  }
  auto output = ksj::array::make_pooled_image<double>(3, 3);

  ksj::image::sobel_x(image, output, ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, output(1, 1));

  ksj::image::sobel_x(image, image, ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, image(1, 1));
}

TEST(KSpaceJetImage, IntelSobelMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(5U, 6U);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * row + 2U * col);
    }
  }
  auto reference_x = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  auto reference_y = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  auto intel_x = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  auto intel_y = ksj::array::make_pooled_image<float>(image.rows(), image.cols());

  ksj::image::detail::eigen::sobel_x(ksj::array::as_const_view(image.view()), reference_x.view(),
                                     ksj::image::BorderMode::replicate);
  ksj::image::detail::eigen::sobel_y(ksj::array::as_const_view(image.view()), reference_y.view(),
                                     ksj::image::BorderMode::replicate);
  ASSERT_TRUE(ksj::image::detail::intel::sobel_x(ksj::array::as_const_view(image.view()), intel_x.view(),
                                                 ksj::image::BorderMode::replicate));
  ASSERT_TRUE(ksj::image::detail::intel::sobel_y(ksj::array::as_const_view(image.view()), intel_y.view(),
                                                 ksj::image::BorderMode::replicate));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference_x(row, col), intel_x(row, col), 1.0e-5F);
      EXPECT_NEAR(reference_y(row, col), intel_y(row, col), 1.0e-5F);
    }
  }
}

TEST(KSpaceJetImage, ComputesGradientMagnitude) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      image(row, col) = static_cast<double>(col);
      image(row, col) = static_cast<double>(col);
    }
  }

  const auto magnitude = ksj::image::gradient_magnitude(image, ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, magnitude(1, 1));

  auto view_output = ksj::array::make_pooled_image<double>(3, 3);
  ksj::image::gradient_magnitude(ksj::array::as_const_view(image.view()), view_output.view(),
                                 ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, view_output(1, 1));

  ksj::image::gradient_magnitude(image.view(), image.view(), ksj::image::BorderMode::replicate);
  EXPECT_DOUBLE_EQ(8.0, image(1, 1));
}

TEST(KSpaceJetImage, AppliesLaplacianAndUnsharpMask) {
  auto impulse = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(impulse).setZero();
  impulse(1, 1) = 1.0;

  const auto laplacian = ksj::image::laplacian(impulse, ksj::image::BorderMode::constant);
  EXPECT_DOUBLE_EQ(-4.0, laplacian(1, 1));
  EXPECT_DOUBLE_EQ(1.0, laplacian(0, 1));
  EXPECT_DOUBLE_EQ(0.0, laplacian(0, 0));

  ksj::image::laplacian(impulse, impulse, ksj::image::BorderMode::constant);
  EXPECT_DOUBLE_EQ(-4.0, impulse(1, 1));
  EXPECT_DOUBLE_EQ(1.0, impulse(0, 1));

  auto sharp_input = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(sharp_input).setZero();
  sharp_input(1, 1) = 1.0;
  const auto sharpened = ksj::image::unsharp_mask(sharp_input, 1.0, 3, 1.0, ksj::image::BorderMode::constant);
  EXPECT_GT(sharpened(1, 1), 1.0);
  EXPECT_LT(sharpened(0, 1), 0.0);

  ksj::image::unsharp_mask(sharp_input, sharp_input, 1.0, 3, 1.0, ksj::image::BorderMode::constant);
  EXPECT_GT(sharp_input(1, 1), 1.0);
  EXPECT_LT(sharp_input(0, 1), 0.0);
}

} // namespace
