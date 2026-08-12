#include "../eigen_test_adapter.hpp"
#include "kspacejet/image/image.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

TEST(KSpaceJetImage, AppliesMorphologyKernels) {
  auto image = ksj::array::make_pooled_image<int>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 1;

  const auto dilated = ksj::image::dilate(image, 3, ksj::image::BorderMode::constant);
  EXPECT_EQ(1, dilated(0, 0));
  EXPECT_EQ(1, dilated(2, 2));

  const auto eroded = ksj::image::erode(dilated, 3, ksj::image::BorderMode::constant);
  EXPECT_EQ(1, eroded(1, 1));
  EXPECT_EQ(0, eroded(0, 0));

  auto filled = ksj::array::make_pooled_image<int>(3, 3);
  as_eigen(filled).setOnes();
  filled(1, 1) = 0;
  const auto closed = ksj::image::morph_close(filled, 3, ksj::image::BorderMode::replicate);
  EXPECT_EQ(1, closed(1, 1));
}

TEST(KSpaceJetImage, FillsBinaryHoles) {
  auto image = ksj::array::make_pooled_image<int>(5, 5);
  as_eigen(image).setOnes();
  image(2, 2) = 0;
  image(0, 2) = 0;

  const auto filled = ksj::image::fill_holes(image);
  EXPECT_EQ(1, filled(2, 2));
  EXPECT_EQ(0, filled(0, 2));

  ksj::image::fill_holes(image, image);
  EXPECT_EQ(1, image(2, 2));
  EXPECT_EQ(0, image(0, 2));
}

TEST(KSpaceJetImage, FillsCornerPairHoles) {
  auto image = ksj::array::make_pooled_image<int>(5, 5);
  as_eigen(image).setOnes();
  image(0, 0) = 0;
  image(4, 4) = 0;
  image(0, 2) = 0;
  image(2, 2) = 0;

  const auto filled = ksj::image::fill_holes(image, ksj::image::HoleFillMode::corner_pair);
  EXPECT_EQ(0, filled(0, 0));
  EXPECT_EQ(0, filled(4, 4));
  EXPECT_EQ(1, filled(0, 2));
  EXPECT_EQ(1, filled(2, 2));
}

TEST(KSpaceJetImage, WritesMorphologyKernels) {
  auto image = ksj::array::make_pooled_image<int>(3, 3);
  as_eigen(image).setZero();
  image(1, 1) = 1;
  auto output = ksj::array::make_pooled_image<int>(3, 3);

  ksj::image::dilate(image, output, 3, ksj::image::BorderMode::constant);
  EXPECT_EQ(1, output(0, 0));
  EXPECT_EQ(1, output(2, 2));

  ksj::image::erode(output, output, 3, ksj::image::BorderMode::constant);
  EXPECT_EQ(1, output(1, 1));
  EXPECT_EQ(0, output(0, 0));

  auto filled = ksj::array::make_pooled_image<int>(3, 3);
  as_eigen(filled).setOnes();
  filled(1, 1) = 0;
  ksj::image::morph_close(filled, filled, 3, ksj::image::BorderMode::replicate);
  EXPECT_EQ(1, filled(1, 1));

  as_eigen(filled).setOnes();
  filled(1, 1) = 0;
  ksj::image::morph_close(filled.view(), filled.view(), 3, ksj::image::BorderMode::replicate);
  EXPECT_EQ(1, filled(1, 1));
}

TEST(KSpaceJetImage, AppliesPooledMorphologyPrimitiveForwards) {
  auto image = ksj::array::make_pooled_image<int>(5, 5);
  as_eigen(image).setZero();
  image(2, 2) = 1;
  auto output = ksj::array::make_pooled_image<int>(5, 5);

  ksj::image::dilate_cross_value(image, output, 1);
  EXPECT_EQ(0, output(0, 0));
  EXPECT_EQ(1, output(1, 2));
  EXPECT_EQ(1, output(2, 1));
  EXPECT_EQ(1, output(2, 2));
  EXPECT_EQ(1, output(2, 3));
  EXPECT_EQ(1, output(3, 2));

  auto kernel = ksj::array::make_pooled_image<int>(3, 3);
  as_eigen(kernel).setZero();
  kernel(1, 1) = 1;
  as_eigen(output).setZero();
  ksj::image::dilate_mask(image, kernel, output, 1);
  EXPECT_EQ(1, output(2, 2));

  auto full_kernel = ksj::array::make_pooled_image<int>(3, 3);
  as_eigen(full_kernel).setOnes();
  auto inplace_dilate = ksj::array::make_pooled_image<int>(5, 5);
  as_eigen(inplace_dilate).setZero();
  inplace_dilate(2, 2) = 1;
  auto morph_scratch = ksj::array::make_pooled_image<int>(0, 0);
  ksj::image::dilate_mask_in_place(inplace_dilate, full_kernel, morph_scratch, 1);
  EXPECT_EQ(0, inplace_dilate(0, 0));
  EXPECT_EQ(0, inplace_dilate(1, 4));
  EXPECT_EQ(1, inplace_dilate(1, 1));
  EXPECT_EQ(1, inplace_dilate(3, 3));
  EXPECT_EQ(0, inplace_dilate(4, 4));
  EXPECT_EQ(5U, morph_scratch.rows());
  EXPECT_EQ(5U, morph_scratch.cols());

  auto inplace_erode = ksj::array::make_pooled_image<int>(7, 7);
  as_eigen(inplace_erode).setOnes();
  inplace_erode(3, 3) = 0;
  ksj::image::erode_mask_in_place(inplace_erode, full_kernel, morph_scratch, 0);
  EXPECT_EQ(0, inplace_erode(0, 0));
  EXPECT_EQ(1, inplace_erode(1, 1));
  EXPECT_EQ(0, inplace_erode(2, 2));
  EXPECT_EQ(0, inplace_erode(4, 4));
  EXPECT_EQ(1, inplace_erode(5, 5));
  EXPECT_EQ(0, inplace_erode(6, 6));
  EXPECT_EQ(7U, morph_scratch.rows());
  EXPECT_EQ(7U, morph_scratch.cols());

  ksj::image::keep_largest_component(output);
  EXPECT_EQ(1, output(2, 2));
  EXPECT_EQ(0, output(0, 0));
}

TEST(KSpaceJetImage, RejectsInvalidOutputArguments) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  auto wrong_shape = ksj::array::make_pooled_image<double>(2, 2);

  EXPECT_THROW(ksj::image::box_filter(image, wrong_shape, 3, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(ksj::image::crop(image, 2, 2, 3, 3)), std::invalid_argument);
  auto too_large_crop = ksj::array::make_pooled_image<double>(4, 4);
  EXPECT_THROW(ksj::image::center_crop(image, too_large_crop), std::invalid_argument);
  auto wrong_labels = ksj::array::make_pooled_image<ksj::image::ConnectedComponentLabel>(2, 2);
  EXPECT_THROW(ksj::image::connected_components(image, wrong_labels, nullptr, ksj::image::Connectivity::four),
               std::invalid_argument);
  auto region_mask = ksj::array::make_pooled_image<ksj::image::RegionGrowMaskValue>(2, 2);
  EXPECT_THROW(ksj::image::region_grow(image, region_mask, 0, 0, 0.0, 1.0, ksj::image::Connectivity::four),
               std::invalid_argument);
  auto valid_region_mask = ksj::array::make_pooled_image<ksj::image::RegionGrowMaskValue>(3, 3);
  EXPECT_THROW(ksj::image::region_grow(image, valid_region_mask, 3, 0, 0.0, 1.0, ksj::image::Connectivity::four),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::region_grow(image, valid_region_mask, 0, 0, 2.0, 1.0, ksj::image::Connectivity::four),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::resize(image, image, static_cast<ksj::image::ResizeMethod>(99)), std::invalid_argument);
  auto empty = ksj::array::make_pooled_image<double>(0, 3);
  auto scalar_output = ksj::array::make_pooled_image<double>(1, 1);
  EXPECT_THROW(ksj::image::resize_area(empty, scalar_output), std::invalid_argument);
  EXPECT_THROW(ksj::image::box_filter(image, image, 0, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(ksj::image::gaussian_blur(image, image, 4, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::gaussian_blur(image, image, 3, 0.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::bilateral_filter(image, wrong_shape, 3, 0.1, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::bilateral_filter(image, image, 4, 0.1, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::bilateral_filter(image, image, 3, 0.0, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::median_filter(image, wrong_shape, 3, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::median_filter(image, image, 2, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(ksj::image::sobel_x(image, wrong_shape, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(ksj::image::sobel_y(image, wrong_shape, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(ksj::image::gradient_magnitude(image, wrong_shape, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::laplacian(image, wrong_shape, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(ksj::image::unsharp_mask(image, wrong_shape, 1.0, 3, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::unsharp_mask(image, image, -1.0, 3, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::unsharp_mask(image, image, 1.0, 4, 1.0, ksj::image::BorderMode::replicate),
               std::invalid_argument);
  EXPECT_THROW(ksj::image::dilate(image, wrong_shape, 3, ksj::image::BorderMode::replicate), std::invalid_argument);
  EXPECT_THROW(ksj::image::erode(image, image, 0, ksj::image::BorderMode::replicate), std::invalid_argument);
}

} // namespace
