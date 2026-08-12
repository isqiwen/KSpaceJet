#include "../eigen_test_adapter.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/intel/intel_image_interpolation.hpp"
#include "kspacejet/image/image.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(KSpaceJetImage, ResizesLinearly) {
  auto image = ksj::array::make_pooled_image<double>(2, 2);
  image(0, 0) = 0.0;
  image(0, 1) = 10.0;
  image(1, 0) = 20.0;
  image(1, 1) = 30.0;

  const auto output = ksj::image::resize_linear(image, 3, 3);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(3U, output.cols());
  EXPECT_DOUBLE_EQ(0.0, output(0, 0));
  EXPECT_DOUBLE_EQ(15.0, output(1, 1));
  EXPECT_DOUBLE_EQ(30.0, output(2, 2));
}

TEST(KSpaceJetImage, WritesResizeLinearly) {
  auto image = ksj::array::make_pooled_image<double>(2, 2);
  image(0, 0) = 0.0;
  image(0, 1) = 10.0;
  image(1, 0) = 20.0;
  image(1, 1) = 30.0;
  auto output = ksj::array::make_pooled_image<double>(3, 3);

  ksj::image::resize_linear(image, output);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(3U, output.cols());
  EXPECT_DOUBLE_EQ(0.0, output(0, 0));
  EXPECT_DOUBLE_EQ(0.0, output(0, 0));
  EXPECT_DOUBLE_EQ(15.0, output(1, 1));
  EXPECT_DOUBLE_EQ(30.0, output(2, 2));

  ksj::image::resize_linear(output, output);
  EXPECT_DOUBLE_EQ(15.0, output(1, 1));
}

TEST(KSpaceJetImage, IntelResizeLinearPreservesConstantImage) {
  auto image = ksj::array::make_pooled_image<float>(4, 5);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = 7.25F;
    }
  }
  auto intel = ksj::array::make_pooled_image<float>(7, 6);

  ASSERT_TRUE(ksj::image::detail::intel::resize_linear(image, intel));

  for (std::size_t row = 0; row < intel.rows(); ++row) {
    for (std::size_t col = 0; col < intel.cols(); ++col) {
      EXPECT_NEAR(7.25F, intel(row, col), 1.0e-4F);
    }
  }
}

TEST(KSpaceJetImage, IntelResizeNearestPreservesConstantImage) {
  auto image = ksj::array::make_pooled_image<float>(4, 5);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = 3.5F;
    }
  }
  auto intel = ksj::array::make_pooled_image<float>(7, 6);

  ASSERT_TRUE(ksj::image::detail::intel::resize_nearest(image, intel));

  for (std::size_t row = 0; row < intel.rows(); ++row) {
    for (std::size_t col = 0; col < intel.cols(); ++col) {
      EXPECT_FLOAT_EQ(3.5F, intel(row, col));
    }
  }
}

TEST(KSpaceJetImage, IntelResizeCubicPreservesConstantImage) {
  auto image = ksj::array::make_pooled_image<float>(4, 5);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = 11.0F;
    }
  }
  auto intel = ksj::array::make_pooled_image<float>(7, 6);

  ASSERT_TRUE(ksj::image::detail::intel::resize_cubic(image, intel));

  for (std::size_t row = 0; row < intel.rows(); ++row) {
    for (std::size_t col = 0; col < intel.cols(); ++col) {
      EXPECT_NEAR(11.0F, intel(row, col), 1.0e-4F);
    }
  }
}

TEST(KSpaceJetImage, ResizesNearest) {
  auto image = ksj::array::make_pooled_image<int>(3, 3);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col);
    }
  }

  const auto output = ksj::image::resize_nearest(image, 2, 2);

  ASSERT_EQ(2U, output.rows());
  ASSERT_EQ(2U, output.cols());
  EXPECT_EQ(0, output(0, 0));
  EXPECT_EQ(2, output(0, 1));
  EXPECT_EQ(20, output(1, 0));
  EXPECT_EQ(22, output(1, 1));
}

TEST(KSpaceJetImage, WritesResizeCubic) {
  auto image = ksj::array::make_pooled_image<double>(2, 2);
  as_eigen(image).setConstant(7.0);
  auto output = ksj::array::make_pooled_image<double>(4, 4);

  ksj::image::resize_cubic(image, output);

  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      EXPECT_NEAR(7.0, output(row, col), 1.0e-12);
    }
  }

  ksj::image::resize_cubic(image, image);
  EXPECT_NEAR(7.0, image(0, 0), 1.0e-12);
}

TEST(KSpaceJetImage, CubicInterpolatesComplexMatrixInPlace) {
  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf32>(2U, 2U);
  as_eigen(matrix).setConstant({7.0F, 3.0F});

  const auto result = ksj::image::cubic_interpolate_2d_inplace(matrix, ksj::image::InterpolationAxis::row, 0.5F);

  EXPECT_EQ(ksj::image::InterpolationStatus::success, result.status);
  bool saw_positive_value = false;
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      EXPECT_GE(matrix(row, col).real(), 0.0F);
      EXPECT_FLOAT_EQ(0.0F, matrix(row, col).imag());
      saw_positive_value = saw_positive_value || matrix(row, col).real() > 0.0F;
    }
  }
  EXPECT_TRUE(saw_positive_value);
}

TEST(KSpaceJetImage, CubicInterpolationClampsNegativeOutput) {
  std::vector<ksj::base::cf32> values(4, {-2.0F, 1.0F});
  auto matrix = ksj::array::MatrixView<ksj::base::cf32>(values.data(), 2U, 2U);

  const auto result = ksj::image::cubic_interpolate_2d_inplace(matrix, ksj::image::InterpolationAxis::column, 0.5F);

  EXPECT_EQ(ksj::image::InterpolationStatus::success, result.status);
  for (const auto& value : values) {
    EXPECT_FLOAT_EQ(0.0F, value.real());
    EXPECT_FLOAT_EQ(0.0F, value.imag());
  }
}

TEST(KSpaceJetImage, CubicInterpolationReportsEmptyInput) {
  auto matrix = ksj::array::MatrixView<ksj::base::cf32>(nullptr, 0U, 0U);

  const auto result = ksj::image::cubic_interpolate_2d_inplace(matrix, ksj::image::InterpolationAxis::row, 0.5F);

  EXPECT_EQ(ksj::image::InterpolationStatus::empty_input, result.status);
}

TEST(KSpaceJetImage, CubicInterpolationBackendsSupportSameStatusContract) {
  const std::vector<ksj::base::cf32> input = {
    {1.0F, 1.0F},  {2.0F, 1.0F},  {3.0F, 1.0F},  {4.0F, 1.0F},  {5.0F, 1.0F},  {6.0F, 1.0F},
    {7.0F, 1.0F},  {8.0F, 1.0F},  {9.0F, 1.0F},  {10.0F, 1.0F}, {11.0F, 1.0F}, {12.0F, 1.0F},
    {13.0F, 1.0F}, {14.0F, 1.0F}, {15.0F, 1.0F}, {16.0F, 1.0F},
  };

  const auto expect_backend_contract = [](std::vector<ksj::base::cf32> values, const auto& backend) {
    auto matrix = ksj::array::MatrixView<ksj::base::cf32>(values.data(), 4U, 4U);
    const auto result = backend(matrix);

    EXPECT_EQ(ksj::image::InterpolationStatus::success, result.status);
    bool saw_positive_value = false;
    for (const auto& value : values) {
      EXPECT_GE(value.real(), 0.0F);
      EXPECT_FLOAT_EQ(0.0F, value.imag());
      saw_positive_value = saw_positive_value || value.real() > 0.0F;
    }
    EXPECT_TRUE(saw_positive_value);
  };

  expect_backend_contract(input, [](auto matrix) {
    return ksj::image::detail::intel::cubic_interpolate_2d_inplace(matrix, ksj::image::InterpolationAxis::row, 0.75F);
  });
  expect_backend_contract(input, [](auto matrix) {
    return ksj::image::detail::opencv::cubic_interpolate_2d_inplace(matrix, ksj::image::InterpolationAxis::row, 0.75F);
  });
  expect_backend_contract(input, [](auto matrix) {
    return ksj::image::detail::eigen::cubic_interpolate_2d_inplace(matrix, ksj::image::InterpolationAxis::row, 0.75F);
  });
}

TEST(KSpaceJetImage, WritesResizeLanczos4) {
  auto image = ksj::array::make_pooled_image<double>(3, 3);
  as_eigen(image).setConstant(5.0);
  auto output = ksj::array::make_pooled_image<double>(6, 6);

  ksj::image::resize_lanczos4(image, output);

  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      EXPECT_NEAR(5.0, output(row, col), 1.0e-12);
    }
  }

  ksj::image::resize_lanczos4(image, image);
  EXPECT_NEAR(5.0, image(1, 1), 1.0e-12);
}

TEST(KSpaceJetImage, ResizesAreaByAveragingSourceCoverage) {
  auto image = ksj::array::make_pooled_image<double>(4, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<double>(row * image.cols() + col);
    }
  }

  const auto expected = ksj::image::resize_area(image, 2, 2);

  ASSERT_EQ(2U, expected.rows());
  ASSERT_EQ(2U, expected.cols());
  EXPECT_DOUBLE_EQ(2.5, expected(0, 0));
  EXPECT_DOUBLE_EQ(4.5, expected(0, 1));
  EXPECT_DOUBLE_EQ(10.5, expected(1, 0));
  EXPECT_DOUBLE_EQ(12.5, expected(1, 1));

  auto area_output = ksj::array::make_pooled_image<double>(2, 2);
  ksj::image::resize_area(image, area_output);
  EXPECT_DOUBLE_EQ(expected(0, 0), area_output(0, 0));
  EXPECT_DOUBLE_EQ(expected(1, 1), area_output(1, 1));

  const auto dispatched = ksj::image::resize(image, 2, 2, ksj::image::ResizeMethod::area);
  EXPECT_DOUBLE_EQ(expected(0, 1), dispatched(0, 1));

  ksj::image::resize_area(image, image);
  EXPECT_DOUBLE_EQ(0.0, image(0, 0));
  EXPECT_DOUBLE_EQ(15.0, image(3, 3));
}

TEST(KSpaceJetImage, DispatchesResizeMethods) {
  auto image = ksj::array::make_pooled_image<double>(2, 2);
  image(0, 0) = 0.0;
  image(0, 1) = 10.0;
  image(1, 0) = 20.0;
  image(1, 1) = 30.0;

  const auto output = ksj::image::resize(image, 3, 3, ksj::image::ResizeMethod::linear);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(3U, output.cols());
  EXPECT_DOUBLE_EQ(15.0, output(1, 1));

  const auto lanczos = ksj::image::resize(image, 3, 3, ksj::image::ResizeMethod::lanczos4);
  EXPECT_NEAR(15.0, lanczos(1, 1), 1.0e-12);
}

} // namespace
