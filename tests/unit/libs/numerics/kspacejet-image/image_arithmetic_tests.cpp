#include "kspacejet/image/image.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetImageArithmetic, AppliesFloatCwiseOperationsOnStridedImageViews) {
  std::array<float, 12> lhs_storage{1.0F, 2.0F, 3.0F, -1.0F, 4.0F, 5.0F, 6.0F, -1.0F, 7.0F, 8.0F, 9.0F, -1.0F};
  std::array<float, 12> rhs_storage{9.0F, 8.0F, 7.0F, -1.0F, 6.0F, 5.0F, 4.0F, -1.0F, 3.0F, 2.0F, 1.0F, -1.0F};
  std::array<float, 12> output_storage{};
  const auto lhs =
    ksj::array::ImageView<const float>(lhs_storage.data(), 3U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U));
  const auto rhs =
    ksj::array::ImageView<const float>(rhs_storage.data(), 3U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U));
  auto output =
    ksj::array::ImageView<float>(output_storage.data(), 3U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U));

  ksj::image::cwise_add(lhs, rhs, output);
  EXPECT_FLOAT_EQ(10.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(10.0F, output(2U, 2U));

  ksj::image::cwise_subtract(lhs, rhs, output);
  EXPECT_FLOAT_EQ(-8.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(8.0F, output(2U, 2U));

  ksj::image::cwise_multiply(lhs, rhs, output);
  EXPECT_FLOAT_EQ(9.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(9.0F, output(2U, 2U));

  ksj::image::cwise_divide(lhs, rhs, output);
  EXPECT_NEAR(1.0F / 9.0F, output(0U, 0U), 1.0e-6F);
  EXPECT_FLOAT_EQ(9.0F, output(2U, 2U));

  ksj::image::cwise_add_scalar(lhs, 2.0F, output);
  EXPECT_FLOAT_EQ(3.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(11.0F, output(2U, 2U));

  ksj::image::cwise_subtract_scalar(lhs, 2.0F, output);
  EXPECT_FLOAT_EQ(-1.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(7.0F, output(2U, 2U));

  ksj::image::cwise_multiply_scalar(lhs, 2.0F, output);
  EXPECT_FLOAT_EQ(2.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(18.0F, output(2U, 2U));

  ksj::image::cwise_divide_scalar(lhs, 2.0F, output);
  EXPECT_FLOAT_EQ(0.5F, output(0U, 0U));
  EXPECT_FLOAT_EQ(4.5F, output(2U, 2U));

  std::array<float, 12> signed_storage{-1.0F, 2.0F,   -3.0F, -99.0F, 4.0F, -5.0F,
                                       6.0F,  -99.0F, 7.0F,  -8.0F,  9.0F, -99.0F};
  const auto signed_input =
    ksj::array::ImageView<const float>(signed_storage.data(), 3U, 4U).subview(ksj::array::_, ksj::array::slice(0U, 3U));
  ksj::image::cwise_abs(signed_input, output);
  EXPECT_FLOAT_EQ(1.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(8.0F, output(2U, 1U));

  ksj::image::cwise_square(lhs, output);
  EXPECT_FLOAT_EQ(1.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(81.0F, output(2U, 2U));

  ksj::image::cwise_sqrt(lhs, output);
  EXPECT_FLOAT_EQ(1.0F, output(0U, 0U));
  EXPECT_FLOAT_EQ(3.0F, output(2U, 2U));
}

TEST(KSpaceJetImageArithmetic, ReturnsImagesFromCwiseOperations) {
  auto lhs = ksj::array::make_pooled_image<float>(2U, 2U);
  auto rhs = ksj::array::make_pooled_image<float>(2U, 2U);
  lhs(0U, 0U) = 1.0F;
  lhs(0U, 1U) = 4.0F;
  lhs(1U, 0U) = 9.0F;
  lhs(1U, 1U) = 16.0F;
  rhs(0U, 0U) = 2.0F;
  rhs(0U, 1U) = 2.0F;
  rhs(1U, 0U) = 3.0F;
  rhs(1U, 1U) = 4.0F;

  const auto sum = ksj::image::cwise_add(lhs, rhs);
  const auto product = ksj::image::cwise_multiply(lhs, rhs);
  const auto scaled = ksj::image::cwise_divide_scalar(lhs, 4.0F);
  const auto root = ksj::image::cwise_sqrt(lhs);

  ASSERT_EQ(2U, sum.rows());
  ASSERT_EQ(2U, sum.cols());
  EXPECT_FLOAT_EQ(3.0F, sum(0U, 0U));
  EXPECT_FLOAT_EQ(20.0F, sum(1U, 1U));
  EXPECT_FLOAT_EQ(27.0F, product(1U, 0U));
  EXPECT_FLOAT_EQ(4.0F, scaled(1U, 1U));
  EXPECT_FLOAT_EQ(3.0F, root(1U, 0U));
}

TEST(KSpaceJetImageArithmetic, CopiesAndFillsFloatAndComplexImages) {
  auto float_input = ksj::array::make_pooled_image<float>(2U, 2U);
  auto float_output = ksj::array::make_pooled_image<float>(2U, 2U);
  float_input(0U, 0U) = 1.0F;
  float_input(0U, 1U) = 2.0F;
  float_input(1U, 0U) = 3.0F;
  float_input(1U, 1U) = 4.0F;

  ksj::image::copy(ksj::array::as_const_view(float_input.view()), float_output.view());
  EXPECT_FLOAT_EQ(1.0F, float_output(0U, 0U));
  EXPECT_FLOAT_EQ(4.0F, float_output(1U, 1U));

  const auto copied = ksj::image::copy(float_input);
  EXPECT_FLOAT_EQ(2.0F, copied(0U, 1U));
  EXPECT_FLOAT_EQ(3.0F, copied(1U, 0U));

  ksj::image::fill(float_output.view(), 7.0F);
  EXPECT_FLOAT_EQ(7.0F, float_output(0U, 1U));
  EXPECT_FLOAT_EQ(7.0F, float_output(1U, 0U));

  auto complex_input = ksj::array::make_pooled_image<ksj::base::cf32>(2U, 2U);
  auto complex_output = ksj::array::make_pooled_image<ksj::base::cf32>(2U, 2U);
  complex_input(0U, 0U) = {1.0F, -1.0F};
  complex_input(0U, 1U) = {2.0F, -2.0F};
  complex_input(1U, 0U) = {3.0F, -3.0F};
  complex_input(1U, 1U) = {4.0F, -4.0F};

  ksj::image::copy(ksj::array::as_const_view(complex_input.view()), complex_output.view());
  EXPECT_FLOAT_EQ(1.0F, complex_output(0U, 0U).real());
  EXPECT_FLOAT_EQ(-4.0F, complex_output(1U, 1U).imag());

  ksj::image::fill(complex_output.view(), ksj::base::cf32{5.0F, 6.0F});
  EXPECT_FLOAT_EQ(5.0F, complex_output(0U, 1U).real());
  EXPECT_FLOAT_EQ(6.0F, complex_output(1U, 0U).imag());
}

TEST(KSpaceJetImageArithmetic, FillsIntegerImages) {
  auto bytes = ksj::array::make_pooled_image<std::uint8_t>(2U, 3U);
  auto words = ksj::array::make_pooled_image<std::uint16_t>(2U, 3U);

  ksj::image::fill(bytes.view(), 17);
  ksj::image::fill(words.view(), 513);

  EXPECT_EQ(17U, static_cast<unsigned>(bytes(0U, 0U)));
  EXPECT_EQ(17U, static_cast<unsigned>(bytes(1U, 2U)));
  EXPECT_EQ(513U, static_cast<unsigned>(words(0U, 1U)));
  EXPECT_EQ(513U, static_cast<unsigned>(words(1U, 2U)));
}

} // namespace
