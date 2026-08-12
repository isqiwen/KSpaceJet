#include "../eigen_test_adapter.hpp"
#include "kspacejet/array/array.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace {

template <typename T>
concept HasImageShapeAccessors = requires(const T& value) {
  value.height();
  value.width();
  value.row_stride_elements();
  value.row_stride_bytes();
};

static_assert(!std::is_same_v<ksj::array::PooledMatrix<float>, ksj::array::PooledImage<float>>);
static_assert(!std::is_same_v<ksj::array::MatrixView<float>, ksj::array::ImageView<float>>);
static_assert(!HasImageShapeAccessors<ksj::array::PooledMatrix<float>>);
static_assert(HasImageShapeAccessors<ksj::array::PooledImage<float>>);

TEST(KSpaceJetArrayPooledEigen, ImageUsesRowMajorPooledMemory) {
  auto image = ksj::array::make_pooled_image<float>(2, 3);

  ASSERT_NE(nullptr, image.data());
  EXPECT_EQ(2U, image.rows());
  EXPECT_EQ(3U, image.cols());
  EXPECT_EQ(2U, image.height());
  EXPECT_EQ(3U, image.width());
  EXPECT_EQ(6U, image.size());
  EXPECT_EQ(3U, image.row_stride_elements());
  EXPECT_EQ(3U * sizeof(float), image.row_stride_bytes());
  EXPECT_EQ(0U, reinterpret_cast<std::uintptr_t>(image.data()) % 64U);

  image(0, 0) = 1.0F;
  image(0, 1) = 2.0F;
  image(0, 2) = 3.0F;
  image(1, 0) = 4.0F;
  image(1, 1) = 5.0F;
  image(1, 2) = 6.0F;

  EXPECT_FLOAT_EQ(1.0F, image.data()[0]);
  EXPECT_FLOAT_EQ(2.0F, image.data()[1]);
  EXPECT_FLOAT_EQ(3.0F, image.data()[2]);
  EXPECT_FLOAT_EQ(4.0F, image.data()[3]);
  EXPECT_FLOAT_EQ(6.0F, image.data()[5]);
}

TEST(KSpaceJetArrayPooledEigen, ImageExposesRowMajorEigenMapWithoutCopies) {
  auto image = ksj::array::make_pooled_image<double>(2, 2);
  as_eigen(image) << 1.0, 2.0, 3.0, 4.0;

  EXPECT_DOUBLE_EQ(1.0, image(0, 0));
  EXPECT_DOUBLE_EQ(2.0, image(0, 1));
  EXPECT_DOUBLE_EQ(3.0, image(1, 0));
  EXPECT_DOUBLE_EQ(4.0, image(1, 1));
}

TEST(KSpaceJetArrayPooledEigen, ImageViewReferencesUnderlyingImage) {
  auto image = ksj::array::make_pooled_image<int>(3, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<int>(row * 10U + col);
    }
  }

  auto view = ksj::array::image_view(image);
  auto middle = view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  middle(0U, 0U) = 99;

  EXPECT_EQ(3U, view.rows());
  EXPECT_EQ(4U, view.cols());
  EXPECT_EQ(4U, view.row_stride());
  EXPECT_EQ(4U, view.row_stride_elements());
  EXPECT_EQ(4U * sizeof(int), view.row_stride_bytes());
  EXPECT_EQ(2U, middle.rows());
  EXPECT_EQ(2U, middle.cols());
  EXPECT_EQ(99, image(1U, 1U));

  ksj::array::fill(middle, 7);
  EXPECT_EQ(7, image(1U, 1U));
  EXPECT_EQ(7, image(2U, 2U));
  EXPECT_EQ(100, ksj::array::accumulate(view, 0));

  auto* minimum = ksj::array::min_element(view);
  auto* maximum = ksj::array::max_element(view);
  ASSERT_NE(nullptr, minimum);
  ASSERT_NE(nullptr, maximum);
  EXPECT_EQ(0, *minimum);
  EXPECT_EQ(23, *maximum);

  auto output = ksj::array::make_pooled_image<int>(2, 2);
  ksj::array::copy(middle, ksj::array::image_view(output));
  EXPECT_EQ(7, output(0U, 0U));
  EXPECT_EQ(7, output(1U, 1U));

  auto tagged_roi = view.subview(ksj::array::slice(1U, 3U), ksj::array::slice(1U, 3U));
  auto tagged_row = view.subview(1U, ksj::array::slice(1U, 3U));
  auto tagged_col = view.subview(ksj::array::slice(1U, 3U), 2U);
  EXPECT_EQ(2U, tagged_roi.height());
  EXPECT_EQ(2U, tagged_roi.width());
  EXPECT_EQ(7, tagged_roi(0U, 0U));
  EXPECT_EQ(2U, tagged_row.size());
  EXPECT_EQ(7, tagged_row(0U));
  EXPECT_EQ(7, tagged_row(1U));
  EXPECT_EQ(2U, tagged_col.size());
  EXPECT_EQ(7, tagged_col(0U));
  EXPECT_EQ(7, tagged_col(1U));
  EXPECT_EQ(7, view.subview(1U, 1U));
}

TEST(KSpaceJetArrayPooledEigen, MatrixAndImageViewsBorrowCompatibleRowMajorMemory) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  matrix(0, 0) = 1;
  matrix(0, 1) = 2;
  matrix(0, 2) = 3;
  matrix(1, 0) = 4;
  matrix(1, 1) = 5;
  matrix(1, 2) = 6;

  auto image = ksj::array::image_view(matrix.view());
  static_assert(std::is_same_v<decltype(image), ksj::array::ImageView<int>>);
  EXPECT_EQ(matrix.data(), image.data());
  EXPECT_EQ(matrix.rows(), image.height());
  EXPECT_EQ(matrix.cols(), image.width());
  EXPECT_EQ(matrix.row_stride(), image.row_stride());

  image(1, 2) = 42;
  EXPECT_EQ(42, matrix(1, 2));

  auto matrix_view = ksj::array::matrix_view(image);
  static_assert(std::is_same_v<decltype(matrix_view), ksj::array::MatrixView<int>>);
  EXPECT_EQ(image.data(), matrix_view.data());
  EXPECT_EQ(image.rows(), matrix_view.rows());
  EXPECT_EQ(image.cols(), matrix_view.cols());
  EXPECT_EQ(image.row_stride(), matrix_view.row_stride());
  EXPECT_EQ(1U, matrix_view.col_stride());
}

TEST(KSpaceJetArrayPooledEigen, PooledMatrixAndImageAdaptersBorrowMemory) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  auto image = ksj::array::make_pooled_image<int>(2, 3);

  matrix(1, 2) = 7;
  image(0, 1) = 11;

  auto image_from_matrix = ksj::array::image_view(matrix);
  auto const_image_from_matrix = ksj::array::image_view(std::as_const(matrix));
  auto matrix_from_image = ksj::array::matrix_view(image);
  auto const_matrix_from_image = ksj::array::matrix_view(std::as_const(image));

  EXPECT_EQ(matrix.data(), image_from_matrix.data());
  EXPECT_EQ(matrix.data(), const_image_from_matrix.data());
  EXPECT_EQ(image.data(), matrix_from_image.data());
  EXPECT_EQ(image.data(), const_matrix_from_image.data());
  EXPECT_EQ(matrix.rows(), image_from_matrix.rows());
  EXPECT_EQ(matrix.cols(), image_from_matrix.cols());
  EXPECT_EQ(matrix.row_stride(), image_from_matrix.row_stride());
  EXPECT_EQ(image.rows(), matrix_from_image.rows());
  EXPECT_EQ(image.cols(), matrix_from_image.cols());
  EXPECT_EQ(image.row_stride(), matrix_from_image.row_stride());
  EXPECT_EQ(1U, matrix_from_image.col_stride());

  image_from_matrix(1, 2) = 13;
  matrix_from_image(0, 1) = 17;
  EXPECT_EQ(13, matrix(1, 2));
  EXPECT_EQ(17, image(0, 1));
}

TEST(KSpaceJetArrayPooledEigen, MatrixViewToImageViewRejectsNonUnitColumnStride) {
  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  const auto non_image_like = matrix.subview(ksj::array::_, ksj::array::slice(0U, matrix.cols(), 2U));
  EXPECT_THROW((void)ksj::array::image_view(non_image_like), std::invalid_argument);
}

TEST(KSpaceJetArrayPooledEigen, CopyAsMatrixMaterializesImageAsRowMajorMatrix) {
  auto image = ksj::array::make_pooled_image<double>(2, 3);
  image(0, 0) = 1.0;
  image(0, 1) = 2.0;
  image(0, 2) = 3.0;
  image(1, 0) = 4.0;
  image(1, 1) = 5.0;
  image(1, 2) = 6.0;

  auto matrix = ksj::array::copy_as_matrix(image);

  EXPECT_EQ(2U, matrix.rows());
  EXPECT_EQ(3U, matrix.cols());
  EXPECT_DOUBLE_EQ(image(0, 0), matrix(0, 0));
  EXPECT_DOUBLE_EQ(image(0, 1), matrix(0, 1));
  EXPECT_DOUBLE_EQ(image(0, 2), matrix(0, 2));
  EXPECT_DOUBLE_EQ(image(1, 0), matrix(1, 0));
  EXPECT_DOUBLE_EQ(image(1, 1), matrix(1, 1));
  EXPECT_DOUBLE_EQ(image(1, 2), matrix(1, 2));
  EXPECT_DOUBLE_EQ(1.0, matrix.data()[0]);
  EXPECT_DOUBLE_EQ(2.0, matrix.data()[1]);
  EXPECT_DOUBLE_EQ(3.0, matrix.data()[2]);
  EXPECT_DOUBLE_EQ(6.0, matrix.data()[5]);
}

TEST(KSpaceJetArrayPooledEigen, CopyAsImageMaterializesMatrixAsRowMajorImage) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 3);
  matrix(0, 0) = 1.0;
  matrix(0, 1) = 2.0;
  matrix(0, 2) = 3.0;
  matrix(1, 0) = 4.0;
  matrix(1, 1) = 5.0;
  matrix(1, 2) = 6.0;

  auto image = ksj::array::copy_as_image(matrix);

  EXPECT_EQ(2U, image.height());
  EXPECT_EQ(3U, image.width());
  EXPECT_DOUBLE_EQ(matrix(0, 0), image(0, 0));
  EXPECT_DOUBLE_EQ(matrix(0, 1), image(0, 1));
  EXPECT_DOUBLE_EQ(matrix(0, 2), image(0, 2));
  EXPECT_DOUBLE_EQ(matrix(1, 0), image(1, 0));
  EXPECT_DOUBLE_EQ(matrix(1, 1), image(1, 1));
  EXPECT_DOUBLE_EQ(matrix(1, 2), image(1, 2));
  EXPECT_DOUBLE_EQ(1.0, image.data()[0]);
  EXPECT_DOUBLE_EQ(2.0, image.data()[1]);
  EXPECT_DOUBLE_EQ(3.0, image.data()[2]);
  EXPECT_DOUBLE_EQ(6.0, image.data()[5]);
}

} // namespace
