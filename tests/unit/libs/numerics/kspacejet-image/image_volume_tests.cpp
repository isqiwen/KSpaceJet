#include "kspacejet/base/types.hpp"
#include "kspacejet/image/image.hpp"
#include "kspacejet/image/volume.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

TEST(KSpaceJetImage, VolumeStoresSlicesContiguously) {
  ksj::image::Volume<int> volume;
  volume.resize(2, 3, 2);

  const int first_slice[] = {1, 2, 3, 4, 5, 6};
  const int second_slice[] = {7, 8, 9, 10, 11, 12};
  volume.set_slice(0, first_slice);
  volume.set_slice(1, second_slice);

  ASSERT_EQ(2U, volume.rows());
  ASSERT_EQ(3U, volume.columns());
  ASSERT_EQ(2U, volume.slices());
  ASSERT_EQ(6U, volume.slice_size());
  EXPECT_EQ(1, volume.slice_data(0)[0]);
  EXPECT_EQ(6, volume.slice_data(0)[5]);
  EXPECT_EQ(7, volume.slice_data(1)[0]);
  EXPECT_EQ(12, volume.slice_data(1)[5]);
}

TEST(KSpaceJetImage, VolumeViewReferencesPooledVolumeStorage) {
  ksj::image::Volume<int> volume;
  volume.resize(2, 3, 2);

  auto view = volume.view();
  ASSERT_EQ(2U, view.rows());
  ASSERT_EQ(3U, view.columns());
  ASSERT_EQ(2U, view.slices());
  ASSERT_EQ(6U, view.slice_stride());

  view(1, 2, 1) = 42;
  EXPECT_EQ(42, volume.slice_data(1)[5]);

  auto subview = view.subview(ksj::array::slice(1, 2), ksj::array::slice(1, 3), ksj::array::slice(1, 2));
  subview(0, 1, 0) = 7;
  EXPECT_EQ(7, view(1, 2, 1));
  EXPECT_EQ(7, volume.slice_data(1)[5]);

  const ksj::image::VolumeView<const int> const_view = view;
  EXPECT_EQ(7, const_view(1, 2, 1));
}

TEST(KSpaceJetImage, ResizesComplexVolumeCubicFromStridedRowMajorView) {
  constexpr std::size_t input_rows = 2U;
  constexpr std::size_t input_cols = 2U;
  constexpr std::size_t input_slices = 2U;
  constexpr std::size_t output_rows = 3U;
  constexpr std::size_t output_cols = 3U;
  constexpr std::size_t output_slices = 3U;
  constexpr std::size_t input_padded_cols = input_cols + 1U;
  constexpr std::size_t output_padded_cols = output_cols + 1U;
  constexpr std::size_t input_row_stride = input_padded_cols * input_slices;
  constexpr std::size_t output_row_stride = output_padded_cols * output_slices;
  const ksj::base::cf32 value{3.0F, -2.0F};

  std::vector<ksj::base::cf32> source(input_rows * input_row_stride, value);
  std::vector<ksj::base::cf32> destination(output_rows * output_row_stride);
  const auto input_parent =
    ksj::array::CubeView<const ksj::base::cf32>(source.data(), input_rows, input_padded_cols, input_slices);
  auto output_parent =
    ksj::array::CubeView<ksj::base::cf32>(destination.data(), output_rows, output_padded_cols, output_slices);
  const auto input = input_parent.subview(ksj::array::_, ksj::array::slice(0U, input_cols), ksj::array::_);
  const auto output = output_parent.subview(ksj::array::_, ksj::array::slice(0U, output_cols), ksj::array::_);

  const auto result = ksj::image::resize_volume_cubic(input, output);

  ASSERT_EQ(ksj::image::InterpolationStatus::success, result.status);
  for (std::size_t i0 = 0U; i0 < output.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < output.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < output.dim2(); ++i2) {
        EXPECT_NEAR(value.real(), output(i0, i1, i2).real(), 1.0e-5F);
        EXPECT_NEAR(value.imag(), output(i0, i1, i2).imag(), 1.0e-5F);
      }
    }
  }
}

TEST(KSpaceJetImage, ReusesWorkspaceForComplexVolumeCubicResize) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf32>(3, 3, 3);
  for (std::size_t i0 = 0U; i0 < input.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input.dim2(); ++i2) {
        input(i0, i1, i2) = ksj::base::cf32{static_cast<float>(i0 + 2U * i1), -static_cast<float>(i1 + 3U * i2)};
      }
    }
  }
  auto output = ksj::array::make_pooled_cube<ksj::base::cf32>(4, 4, 4);
  ksj::image::ResizeVolumeCubicWorkspace workspace;

  auto result = ksj::image::resize_volume_cubic(input, output, workspace);
  ASSERT_EQ(ksj::image::InterpolationStatus::success, result.status);
  const auto* real_input = workspace.real_input.data();
  const auto* imag_input = workspace.imag_input.data();
  const auto* real_output = workspace.real_output.data();
  const auto* imag_output = workspace.imag_output.data();
  const auto* work_buffer = workspace.work_buffer.data();
  ASSERT_NE(nullptr, real_input);
  ASSERT_NE(nullptr, imag_input);
  ASSERT_NE(nullptr, real_output);
  ASSERT_NE(nullptr, imag_output);
  ASSERT_NE(nullptr, work_buffer);

  result = ksj::image::resize_volume_cubic(input, output, workspace);
  ASSERT_EQ(ksj::image::InterpolationStatus::success, result.status);
  EXPECT_EQ(real_input, workspace.real_input.data());
  EXPECT_EQ(imag_input, workspace.imag_input.data());
  EXPECT_EQ(real_output, workspace.real_output.data());
  EXPECT_EQ(imag_output, workspace.imag_output.data());
  EXPECT_EQ(work_buffer, workspace.work_buffer.data());
}

} // namespace
