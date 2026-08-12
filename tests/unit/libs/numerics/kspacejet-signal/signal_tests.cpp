#include "../eigen_test_adapter.hpp"
#include "kspacejet/signal/signal.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_filters.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_convolution.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_phase.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_convolution.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_filters.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_windows.hpp"
#include "kspacejet/signal/detail/opencv/opencv_signal_convolution.hpp"

#include <array>
#include <complex>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetSignal, BuildsHannWindow) {
  const auto window = ksj::signal::window<double>(3, ksj::signal::WindowKind::hann);

  ASSERT_EQ(3U, window.size());
  EXPECT_NEAR(0.0, window(0), 1e-12);
  EXPECT_NEAR(1.0, window(1), 1e-12);
  EXPECT_NEAR(0.0, window(2), 1e-12);
}

TEST(KSpaceJetSignal, BuildsWindowIntoStridedView) {
  std::array<double, 6> storage{};
  auto output =
    ksj::array::VectorView<double>(storage.data(), storage.size()).subview(ksj::array::slice(0U, storage.size(), 2U));

  ksj::signal::window(output, ksj::signal::WindowKind::hann);

  EXPECT_NEAR(0.0, output(0), 1.0e-12);
  EXPECT_NEAR(1.0, output(1), 1.0e-12);
  EXPECT_NEAR(0.0, output(2), 1.0e-12);
  EXPECT_DOUBLE_EQ(0.0, storage[1]);
  EXPECT_DOUBLE_EQ(0.0, storage[3]);
  EXPECT_DOUBLE_EQ(0.0, storage[5]);
}

TEST(KSpaceJetSignal, ConvolvesFullSignal) {
  auto signal = ksj::array::make_pooled_vector<double>(2);
  auto kernel = ksj::array::make_pooled_vector<double>(2);
  signal(0) = 1.0;
  signal(1) = 2.0;
  kernel(0) = 3.0;
  kernel(1) = 4.0;

  const auto output = ksj::signal::convolve(signal, kernel);

  ASSERT_EQ(3U, output.size());
  EXPECT_DOUBLE_EQ(3.0, output(0));
  EXPECT_DOUBLE_EQ(10.0, output(1));
  EXPECT_DOUBLE_EQ(8.0, output(2));
}

TEST(KSpaceJetSignal, ConvolvesIntoViewOutput) {
  auto signal = ksj::array::make_pooled_vector<double>(2);
  auto kernel = ksj::array::make_pooled_vector<double>(2);
  signal(0) = 1.0;
  signal(1) = 2.0;
  kernel(0) = 3.0;
  kernel(1) = 4.0;

  std::array<double, 6> storage{};
  auto output =
    ksj::array::VectorView<double>(storage.data(), storage.size()).subview(ksj::array::slice(0U, storage.size(), 2U));

  ksj::signal::convolve(ksj::array::as_const_view(signal.view()), ksj::array::as_const_view(kernel.view()), output);

  EXPECT_DOUBLE_EQ(3.0, output(0));
  EXPECT_DOUBLE_EQ(10.0, output(1));
  EXPECT_DOUBLE_EQ(8.0, output(2));
  EXPECT_DOUBLE_EQ(0.0, storage[1]);
  EXPECT_DOUBLE_EQ(0.0, storage[3]);
  EXPECT_DOUBLE_EQ(0.0, storage[5]);
}

TEST(KSpaceJetSignal, Convolves2dFullMatrix) {
  auto input = ksj::array::make_pooled_matrix<double>(2, 2);
  input(0, 0) = 1.0;
  input(0, 1) = 2.0;
  input(1, 0) = 3.0;
  input(1, 1) = 4.0;

  auto kernel = ksj::array::make_pooled_matrix<double>(2, 3);
  kernel(0, 0) = 0.0;
  kernel(0, 1) = 1.0;
  kernel(0, 2) = 2.0;
  kernel(1, 0) = 3.0;
  kernel(1, 1) = 4.0;
  kernel(1, 2) = 5.0;

  const auto output = ksj::signal::convolve2d_full(input, kernel);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(4U, output.cols());
  EXPECT_DOUBLE_EQ(0.0, output(0, 0));
  EXPECT_DOUBLE_EQ(1.0, output(0, 1));
  EXPECT_DOUBLE_EQ(4.0, output(0, 2));
  EXPECT_DOUBLE_EQ(4.0, output(0, 3));
  EXPECT_DOUBLE_EQ(3.0, output(1, 0));
  EXPECT_DOUBLE_EQ(13.0, output(1, 1));
  EXPECT_DOUBLE_EQ(23.0, output(1, 2));
  EXPECT_DOUBLE_EQ(18.0, output(1, 3));
  EXPECT_DOUBLE_EQ(9.0, output(2, 0));
  EXPECT_DOUBLE_EQ(24.0, output(2, 1));
  EXPECT_DOUBLE_EQ(31.0, output(2, 2));
  EXPECT_DOUBLE_EQ(20.0, output(2, 3));
}

TEST(KSpaceJetSignal, Convolves2dFullIntoStridedOutput) {
  auto input = ksj::array::make_pooled_matrix<double>(2, 2);
  input(0, 0) = 1.0;
  input(0, 1) = 2.0;
  input(1, 0) = 3.0;
  input(1, 1) = 4.0;

  auto kernel = ksj::array::make_pooled_matrix<double>(1, 2);
  kernel(0, 0) = 5.0;
  kernel(0, 1) = 6.0;

  auto storage = ksj::array::make_pooled_matrix<double>(2, 6);
  storage.fill(-1.0);
  auto output = storage.subview(ksj::array::_, ksj::array::slice(0U, 6U, 2U));

  ksj::signal::convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()),
                               output);

  EXPECT_DOUBLE_EQ(5.0, output(0, 0));
  EXPECT_DOUBLE_EQ(16.0, output(0, 1));
  EXPECT_DOUBLE_EQ(12.0, output(0, 2));
  EXPECT_DOUBLE_EQ(15.0, output(1, 0));
  EXPECT_DOUBLE_EQ(38.0, output(1, 1));
  EXPECT_DOUBLE_EQ(24.0, output(1, 2));
  for (std::size_t row = 0U; row < storage.rows(); ++row) {
    for (std::size_t col = 1U; col < storage.cols(); col += 2U) {
      EXPECT_DOUBLE_EQ(-1.0, storage(row, col));
    }
  }
}

TEST(KSpaceJetSignal, Convolves2dFullComplexMatrix) {
  using complex_type = std::complex<float>;
  auto input = ksj::array::make_pooled_matrix<complex_type>(1, 2);
  input(0, 0) = complex_type{1.0F, 1.0F};
  input(0, 1) = complex_type{2.0F, -1.0F};

  auto kernel = ksj::array::make_pooled_matrix<complex_type>(2, 1);
  kernel(0, 0) = complex_type{3.0F, 0.0F};
  kernel(1, 0) = complex_type{0.0F, -1.0F};

  const auto output = ksj::signal::convolve2d_full(input, kernel);

  ASSERT_EQ(2U, output.rows());
  ASSERT_EQ(2U, output.cols());
  EXPECT_EQ((complex_type{3.0F, 3.0F}), output(0, 0));
  EXPECT_EQ((complex_type{6.0F, -3.0F}), output(0, 1));
  EXPECT_EQ((complex_type{1.0F, -1.0F}), output(1, 0));
  EXPECT_EQ((complex_type{-1.0F, -2.0F}), output(1, 1));
}

TEST(KSpaceJetSignal, AppliesCausalFirFilter) {
  auto input = ksj::array::make_pooled_vector<float>(4);
  auto taps = ksj::array::make_pooled_vector<float>(2);
  input(0) = 1.0F;
  input(1) = 2.0F;
  input(2) = 3.0F;
  input(3) = 4.0F;
  taps(0) = 0.5F;
  taps(1) = 1.0F;

  const auto output = ksj::signal::fir_filter(input, taps);

  ASSERT_EQ(input.size(), output.size());
  EXPECT_FLOAT_EQ(0.5F, output(0));
  EXPECT_FLOAT_EQ(2.0F, output(1));
  EXPECT_FLOAT_EQ(3.5F, output(2));
  EXPECT_FLOAT_EQ(5.0F, output(3));
}

TEST(KSpaceJetSignal, AppliesCausalIirFilter) {
  auto input = ksj::array::make_pooled_vector<float>(4);
  auto numerator = ksj::array::make_pooled_vector<float>(1);
  auto denominator = ksj::array::make_pooled_vector<float>(2);
  input.fill(1.0F);
  numerator(0) = 0.5F;
  denominator(0) = 1.0F;
  denominator(1) = -0.5F;

  const auto output = ksj::signal::iir_filter(input, numerator, denominator);

  ASSERT_EQ(input.size(), output.size());
  EXPECT_FLOAT_EQ(0.5F, output(0));
  EXPECT_FLOAT_EQ(0.75F, output(1));
  EXPECT_FLOAT_EQ(0.875F, output(2));
  EXPECT_FLOAT_EQ(0.9375F, output(3));
}

TEST(KSpaceJetSignal, IntelFirFilterMatchesReference) {
  auto input = ksj::array::make_pooled_vector<float>(256);
  auto taps = ksj::array::make_pooled_vector<float>(4);
  auto intel_output = ksj::array::make_pooled_vector<float>(input.size());
  auto reference_output = ksj::array::make_pooled_vector<float>(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = std::sin(static_cast<float>(index) * 0.125F);
  }
  taps(0) = 0.25F;
  taps(1) = -0.125F;
  taps(2) = 0.5F;
  taps(3) = 0.25F;

  ksj::signal::detail::eigen::fir_filter(ksj::array::as_const_view(input.view()),
                                         ksj::array::as_const_view(taps.view()), reference_output.view());
  ASSERT_TRUE(ksj::signal::detail::intel::fir_filter(ksj::array::as_const_view(input.view()),
                                                     ksj::array::as_const_view(taps.view()), intel_output.view()));

  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(reference_output(index), intel_output(index), 1.0e-5F);
  }
}

TEST(KSpaceJetSignal, IntelIirFilterMatchesReference) {
  auto input = ksj::array::make_pooled_vector<float>(256);
  auto numerator = ksj::array::make_pooled_vector<float>(3);
  auto denominator = ksj::array::make_pooled_vector<float>(3);
  auto intel_output = ksj::array::make_pooled_vector<float>(input.size());
  auto reference_output = ksj::array::make_pooled_vector<float>(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = std::cos(static_cast<float>(index) * 0.0625F);
  }
  numerator(0) = 0.2F;
  numerator(1) = 0.1F;
  numerator(2) = 0.05F;
  denominator(0) = 1.0F;
  denominator(1) = -0.25F;
  denominator(2) = 0.1F;

  ksj::signal::detail::eigen::iir_filter(ksj::array::as_const_view(input.view()),
                                         ksj::array::as_const_view(numerator.view()),
                                         ksj::array::as_const_view(denominator.view()), reference_output.view());
  ASSERT_TRUE(ksj::signal::detail::intel::iir_filter(
    ksj::array::as_const_view(input.view()), ksj::array::as_const_view(numerator.view()),
    ksj::array::as_const_view(denominator.view()), intel_output.view()));

  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(reference_output(index), intel_output(index), 1.0e-5F);
  }
}

TEST(KSpaceJetSignal, ReusesCallerOwnedWorkspacesForFilters) {
  auto input = ksj::array::make_pooled_vector<float>(512);
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = std::sin(static_cast<float>(index) * 0.03125F);
  }

  auto taps = ksj::array::make_pooled_vector<float>(16);
  for (std::size_t index = 0; index < taps.size(); ++index) {
    taps(index) = 1.0F / static_cast<float>(index + 1U);
  }
  auto fir_output = ksj::array::make_pooled_vector<float>(input.size());
  ksj::signal::FirFilterWorkspace<float> fir_workspace;
  ksj::signal::fir_filter(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(taps.view()),
                          fir_output.view(), fir_workspace);
  const auto* fir_spec = fir_workspace.spec_storage.data();
  const auto* fir_buffer = fir_workspace.buffer_storage.data();
  ASSERT_NE(nullptr, fir_spec);
  ASSERT_NE(nullptr, fir_buffer);
  ksj::signal::fir_filter(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(taps.view()),
                          fir_output.view(), fir_workspace);
  EXPECT_EQ(fir_spec, fir_workspace.spec_storage.data());
  EXPECT_EQ(fir_buffer, fir_workspace.buffer_storage.data());

  auto numerator = ksj::array::make_pooled_vector<float>(3);
  auto denominator = ksj::array::make_pooled_vector<float>(3);
  numerator(0) = 0.2F;
  numerator(1) = 0.1F;
  numerator(2) = 0.05F;
  denominator(0) = 1.0F;
  denominator(1) = -0.25F;
  denominator(2) = 0.1F;
  auto iir_output = ksj::array::make_pooled_vector<float>(input.size());
  ksj::signal::IirFilterWorkspace<float> iir_workspace;
  ksj::signal::iir_filter(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(numerator.view()),
                          ksj::array::as_const_view(denominator.view()), iir_output.view(), iir_workspace);
  const auto* iir_taps = iir_workspace.taps_storage.data();
  const auto* iir_state = iir_workspace.state_storage.data();
  ASSERT_NE(nullptr, iir_taps);
  ASSERT_NE(nullptr, iir_state);
  ksj::signal::iir_filter(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(numerator.view()),
                          ksj::array::as_const_view(denominator.view()), iir_output.view(), iir_workspace);
  EXPECT_EQ(iir_taps, iir_workspace.taps_storage.data());
  EXPECT_EQ(iir_state, iir_workspace.state_storage.data());

  auto median_output = ksj::array::make_pooled_vector<float>(input.size());
  ksj::signal::MedianFilterWorkspace<float> median_workspace;
  ksj::signal::median_filter(ksj::array::as_const_view(input.view()), median_output.view(), 3U, median_workspace,
                             ksj::signal::SignalBorderMode::causal_replicate);
  const auto* median_buffer = median_workspace.buffer_storage.data();
  ASSERT_NE(nullptr, median_buffer);
  ksj::signal::median_filter(ksj::array::as_const_view(input.view()), median_output.view(), 3U, median_workspace,
                             ksj::signal::SignalBorderMode::causal_replicate);
  EXPECT_EQ(median_buffer, median_workspace.buffer_storage.data());
}

TEST(KSpaceJetSignal, MedianFiltersVectorWithReplicatedEdges) {
  auto signal = ksj::array::make_pooled_vector<float>(5);
  signal(0) = 5.0F;
  signal(1) = 1.0F;
  signal(2) = 4.0F;
  signal(3) = 2.0F;
  signal(4) = 3.0F;

  const auto output = ksj::signal::median_filter(signal, 3);

  ASSERT_EQ(5U, output.size());
  EXPECT_FLOAT_EQ(5.0F, output(0));
  EXPECT_FLOAT_EQ(4.0F, output(1));
  EXPECT_FLOAT_EQ(2.0F, output(2));
  EXPECT_FLOAT_EQ(3.0F, output(3));
  EXPECT_FLOAT_EQ(3.0F, output(4));
}

TEST(KSpaceJetSignal, MedianFiltersVectorWithZeroPaddingInPlace) {
  auto signal = ksj::array::make_pooled_vector<float>(5);
  signal(0) = 5.0F;
  signal(1) = 1.0F;
  signal(2) = 4.0F;
  signal(3) = 2.0F;
  signal(4) = 3.0F;

  ksj::signal::median_filter_in_place(signal.view(), 3, ksj::signal::SignalBorderMode::zero);

  EXPECT_FLOAT_EQ(1.0F, signal(0));
  EXPECT_FLOAT_EQ(4.0F, signal(1));
  EXPECT_FLOAT_EQ(2.0F, signal(2));
  EXPECT_FLOAT_EQ(3.0F, signal(3));
  EXPECT_FLOAT_EQ(2.0F, signal(4));
}

TEST(KSpaceJetSignal, MedianFiltersVectorWithCausalReplicatedEdgesInPlace) {
  auto signal = ksj::array::make_pooled_vector<float>(5);
  signal(0) = 5.0F;
  signal(1) = 1.0F;
  signal(2) = 4.0F;
  signal(3) = 2.0F;
  signal(4) = 3.0F;

  ksj::signal::median_filter_in_place(signal.view(), 3, ksj::signal::SignalBorderMode::causal_replicate);

  EXPECT_FLOAT_EQ(5.0F, signal(0));
  EXPECT_FLOAT_EQ(5.0F, signal(1));
  EXPECT_FLOAT_EQ(4.0F, signal(2));
  EXPECT_FLOAT_EQ(2.0F, signal(3));
  EXPECT_FLOAT_EQ(3.0F, signal(4));
}

TEST(KSpaceJetSignal, MedianFiltersStridedVectorWithCausalReplicatedEdgesInPlace) {
  std::array<float, 10> storage{};
  auto signal =
    ksj::array::VectorView<float>(storage.data(), storage.size()).subview(ksj::array::slice(0U, storage.size(), 2U));
  signal(0) = 5.0F;
  signal(1) = 1.0F;
  signal(2) = 4.0F;
  signal(3) = 2.0F;
  signal(4) = 3.0F;

  ksj::signal::median_filter_in_place(signal, 3, ksj::signal::SignalBorderMode::causal_replicate);

  EXPECT_FLOAT_EQ(5.0F, signal(0));
  EXPECT_FLOAT_EQ(5.0F, signal(1));
  EXPECT_FLOAT_EQ(4.0F, signal(2));
  EXPECT_FLOAT_EQ(2.0F, signal(3));
  EXPECT_FLOAT_EQ(3.0F, signal(4));
  EXPECT_FLOAT_EQ(0.0F, storage[1]);
  EXPECT_FLOAT_EQ(0.0F, storage[3]);
  EXPECT_FLOAT_EQ(0.0F, storage[5]);
  EXPECT_FLOAT_EQ(0.0F, storage[7]);
  EXPECT_FLOAT_EQ(0.0F, storage[9]);
}

TEST(KSpaceJetSignal, ExtractsSlidingWindowMatrixFromCube) {
  auto cube = ksj::array::make_pooled_cube<double>(3, 3, 2);
  for (std::size_t i2 = 0; i2 < cube.dim2(); ++i2) {
    for (std::size_t i1 = 0; i1 < cube.dim1(); ++i1) {
      for (std::size_t i0 = 0; i0 < cube.dim0(); ++i0) {
        cube(i0, i1, i2) = static_cast<double>(i0 + 10U * i1 + 100U * i2);
      }
    }
  }

  const auto matrix = ksj::signal::extract_sliding_window_matrix(cube, 2, 2);

  ASSERT_EQ(4U, matrix.rows());
  ASSERT_EQ(8U, matrix.cols());
  EXPECT_DOUBLE_EQ(0.0, matrix(0, 0));
  EXPECT_DOUBLE_EQ(100.0, matrix(0, 1));
  EXPECT_DOUBLE_EQ(10.0, matrix(0, 2));
  EXPECT_DOUBLE_EQ(110.0, matrix(0, 3));
  EXPECT_DOUBLE_EQ(1.0, matrix(0, 4));
  EXPECT_DOUBLE_EQ(111.0, matrix(0, 7));
  EXPECT_DOUBLE_EQ(10.0, matrix(1, 0));
  EXPECT_DOUBLE_EQ(120.0, matrix(1, 3));
  EXPECT_DOUBLE_EQ(11.0, matrix(2, 2));
  EXPECT_DOUBLE_EQ(122.0, matrix(3, 7));
}

TEST(KSpaceJetSignal, BuildsNormalEquationWithoutIndex) {
  auto input = ksj::array::make_pooled_matrix<double>(3, 3);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      input(row, col) = static_cast<double>(row * 10U + col);
    }
  }

  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  auto rhs = ksj::array::make_pooled_matrix<double>(2, 1);
  auto indexes = ksj::array::make_pooled_vector<std::size_t>(2);

  ksj::signal::normal_equation_without_index(input, 1, matrix, rhs, indexes);

  EXPECT_EQ(0U, indexes(0));
  EXPECT_EQ(2U, indexes(1));
  EXPECT_DOUBLE_EQ(0.0, matrix(0, 0));
  EXPECT_DOUBLE_EQ(2.0, matrix(0, 1));
  EXPECT_DOUBLE_EQ(20.0, matrix(1, 0));
  EXPECT_DOUBLE_EQ(22.0, matrix(1, 1));
  EXPECT_DOUBLE_EQ(1.0, rhs(0, 0));
  EXPECT_DOUBLE_EQ(21.0, rhs(1, 0));
}

TEST(KSpaceJetSignal, ReturnsNormalEquationWithoutIndex) {
  auto input = ksj::array::make_pooled_matrix<double>(3, 3);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      input(row, col) = static_cast<double>(row * 10U + col);
    }
  }

  const auto result = ksj::signal::normal_equation_without_index(input, 1);

  ASSERT_EQ(2U, result.matrix.rows());
  ASSERT_EQ(2U, result.matrix.cols());
  ASSERT_EQ(2U, result.rhs.rows());
  ASSERT_EQ(1U, result.rhs.cols());
  ASSERT_EQ(2U, result.retained_indexes.size());
  EXPECT_EQ(0U, result.retained_indexes(0));
  EXPECT_EQ(2U, result.retained_indexes(1));
  EXPECT_DOUBLE_EQ(0.0, result.matrix(0, 0));
  EXPECT_DOUBLE_EQ(22.0, result.matrix(1, 1));
  EXPECT_DOUBLE_EQ(1.0, result.rhs(0, 0));
  EXPECT_DOUBLE_EQ(21.0, result.rhs(1, 0));
}

TEST(KSpaceJetSignal, ResamplesVectors) {
  auto signal = ksj::array::make_pooled_vector<double>(3);
  signal(0) = 0.0;
  signal(1) = 10.0;
  signal(2) = 20.0;

  const auto linear = ksj::signal::resample(signal, 5, ksj::signal::ResampleKernel::linear);
  ASSERT_EQ(5U, linear.size());
  EXPECT_DOUBLE_EQ(0.0, linear(0));
  EXPECT_DOUBLE_EQ(5.0, linear(1));
  EXPECT_DOUBLE_EQ(10.0, linear(2));
  EXPECT_DOUBLE_EQ(15.0, linear(3));
  EXPECT_DOUBLE_EQ(20.0, linear(4));

  const auto nearest = ksj::signal::resample(signal, 5, ksj::signal::ResampleKernel::nearest);
  EXPECT_DOUBLE_EQ(0.0, nearest(0));
  EXPECT_DOUBLE_EQ(10.0, nearest(1));
  EXPECT_DOUBLE_EQ(10.0, nearest(2));
  EXPECT_DOUBLE_EQ(20.0, nearest(3));
  EXPECT_DOUBLE_EQ(20.0, nearest(4));

  const auto cubic_same_size = ksj::signal::resample(signal, 3, ksj::signal::ResampleKernel::cubic);
  EXPECT_NEAR(0.0, cubic_same_size(0), 1.0e-12);
  EXPECT_NEAR(10.0, cubic_same_size(1), 1.0e-12);
  EXPECT_NEAR(20.0, cubic_same_size(2), 1.0e-12);

  const auto mitchell = ksj::signal::resample(signal, 5, ksj::signal::ResampleKernel::mitchell);
  EXPECT_GT(mitchell(1), 0.0);
  EXPECT_LT(mitchell(1), 10.0);
  EXPECT_NEAR(10.0, mitchell(2), 1.0e-12);

  const auto lanczos = ksj::signal::resample(signal, 5, ksj::signal::ResampleKernel::lanczos3);
  EXPECT_NEAR(0.0, lanczos(0), 1.0e-12);
  EXPECT_NEAR(10.0, lanczos(2), 1.0e-12);
  EXPECT_NEAR(20.0, lanczos(4), 1.0e-12);

  as_eigen(signal).setConstant(7.0);
  const auto cubic_constant = ksj::signal::resample(signal, 9, ksj::signal::ResampleKernel::cubic);
  for (std::size_t index = 0; index < cubic_constant.size(); ++index) {
    EXPECT_NEAR(7.0, cubic_constant(index), 1.0e-12);
  }

  const auto mitchell_constant = ksj::signal::resample(signal, 9, ksj::signal::ResampleKernel::mitchell);
  for (std::size_t index = 0; index < mitchell_constant.size(); ++index) {
    EXPECT_NEAR(7.0, mitchell_constant(index), 1.0e-12);
  }

  const auto lanczos_constant = ksj::signal::resample(signal, 9, ksj::signal::ResampleKernel::lanczos3);
  for (std::size_t index = 0; index < lanczos_constant.size(); ++index) {
    EXPECT_NEAR(7.0, lanczos_constant(index), 1.0e-12);
  }
}

TEST(KSpaceJetSignal, ResamplesIntoViewOutput) {
  auto signal = ksj::array::make_pooled_vector<double>(3);
  signal(0) = 0.0;
  signal(1) = 10.0;
  signal(2) = 20.0;

  std::array<double, 10> storage{};
  auto output =
    ksj::array::VectorView<double>(storage.data(), storage.size()).subview(ksj::array::slice(0U, storage.size(), 2U));

  ksj::signal::resample(ksj::array::as_const_view(signal.view()), output, ksj::signal::ResampleKernel::linear);

  EXPECT_DOUBLE_EQ(0.0, output(0));
  EXPECT_DOUBLE_EQ(5.0, output(1));
  EXPECT_DOUBLE_EQ(10.0, output(2));
  EXPECT_DOUBLE_EQ(15.0, output(3));
  EXPECT_DOUBLE_EQ(20.0, output(4));
  EXPECT_DOUBLE_EQ(0.0, storage[1]);
  EXPECT_DOUBLE_EQ(0.0, storage[3]);
  EXPECT_DOUBLE_EQ(0.0, storage[5]);
  EXPECT_DOUBLE_EQ(0.0, storage[7]);
  EXPECT_DOUBLE_EQ(0.0, storage[9]);
}

TEST(KSpaceJetSignal, BuildsAdditionalWindowFamilies) {
  const auto tukey = ksj::signal::tukey_window<double>(5, 0.5);
  ASSERT_EQ(5U, tukey.size());
  EXPECT_NEAR(0.0, tukey(0), 1.0e-12);
  EXPECT_NEAR(1.0, tukey(2), 1.0e-12);
  EXPECT_NEAR(0.0, tukey(4), 1.0e-12);

  const auto exponential = ksj::signal::exponential_window<double>(3, 1.0);
  EXPECT_GT(exponential(1), exponential(0));
  EXPECT_DOUBLE_EQ(exponential(0), exponential(2));

  const auto accelerated_exponential = ksj::signal::exponential_window<double>(128, 8.0);
  const auto accelerated_center = static_cast<double>(accelerated_exponential.size() - 1U) / 2.0;
  const auto accelerated_denom = static_cast<double>(accelerated_exponential.size() - 1U);
  for (const auto index : {0U, 31U, 63U, 64U, 127U}) {
    const auto normalized = (static_cast<double>(index) - accelerated_center) / accelerated_denom;
    EXPECT_NEAR(std::exp(-8.0 * normalized * normalized), accelerated_exponential(index), 1.0e-12);
  }

  const auto fermi = ksj::signal::fermi_window<double>(3, 1.0, 0.5);
  EXPECT_GT(fermi(1), fermi(0));
  EXPECT_DOUBLE_EQ(fermi(0), fermi(2));

  const auto quadratic_exponential = ksj::signal::quadratic_exponential_window<double>(5, 2.0, 4.0);
  ASSERT_EQ(5U, quadratic_exponential.size());
  for (std::size_t index = 0; index < quadratic_exponential.size(); ++index) {
    const auto offset = static_cast<double>(index) - 2.5;
    EXPECT_NEAR(std::exp(offset * offset * 0.04), quadratic_exponential(index), 1.0e-12);
  }
}

TEST(KSpaceJetSignal, BuildsGeneratorCompatibleFilters) {
  const auto triangle = ksj::signal::triangle_filter<double>(3, 0, 5);
  EXPECT_NEAR(1.0, triangle(0), 1.0e-12);
  EXPECT_NEAR(0.5, triangle(1), 1.0e-12);
  EXPECT_NEAR(0.0, triangle(2), 1.0e-12);

  const auto half_hamming = ksj::signal::half_hamming_filter<double>(3, 7, 3);
  EXPECT_NEAR(0.08, half_hamming(0), 1.0e-12);
  EXPECT_NEAR(0.54, half_hamming(1), 1.0e-12);
  EXPECT_NEAR(1.0, half_hamming(2), 1.0e-12);

  const auto hamming_bandpass = ksj::signal::hamming_bandpass_filter<double>(3, 7, 3);
  EXPECT_NEAR(0.08, hamming_bandpass(0), 1.0e-12);
  EXPECT_NEAR(1.0, hamming_bandpass(1), 1.0e-12);
  EXPECT_NEAR(0.08, hamming_bandpass(2), 1.0e-12);

  const auto dual_hamming = ksj::signal::dual_hamming_bandpass_filter<double>(6, 0, 6);
  EXPECT_NEAR(0.08, dual_hamming(0), 1.0e-12);
  EXPECT_NEAR(1.0, dual_hamming(1), 1.0e-12);
  EXPECT_NEAR(0.08, dual_hamming(2), 1.0e-12);
  EXPECT_NEAR(0.08, dual_hamming(3), 1.0e-12);
  EXPECT_NEAR(1.0, dual_hamming(4), 1.0e-12);
  EXPECT_NEAR(0.08, dual_hamming(5), 1.0e-12);

  const auto half_hann = ksj::signal::half_hann_filter<double>(3, 2, 3);
  EXPECT_NEAR(0.0, half_hann(0), 1.0e-12);
  EXPECT_NEAR(0.5, half_hann(1), 1.0e-12);
  EXPECT_NEAR(1.0, half_hann(2), 1.0e-12);

  const auto half_blackman = ksj::signal::half_blackman_filter<double>(3, 2, 3);
  EXPECT_NEAR(0.0, half_blackman(0), 1.0e-12);
  EXPECT_NEAR(0.34, half_blackman(1), 1.0e-12);
  EXPECT_NEAR(1.0, half_blackman(2), 1.0e-12);

  const auto hbrr = ksj::signal::hbrr_filter<double>(3, 0, 3);
  EXPECT_NEAR(0.0, hbrr(0), 1.0e-12);
  EXPECT_NEAR(1.0, hbrr(1), 1.0e-12);
  EXPECT_NEAR(0.0, hbrr(2), 1.0e-12);

  const auto tukey = ksj::signal::tukey_filter<double>(9, 0.5, 0, 9);
  EXPECT_NEAR(0.0, tukey(0), 1.0e-12);
  EXPECT_NEAR(0.5, tukey(1), 1.0e-12);
  EXPECT_NEAR(1.0, tukey(2), 1.0e-12);
  EXPECT_NEAR(1.0, tukey(6), 1.0e-12);
  EXPECT_NEAR(0.5, tukey(7), 1.0e-12);
  EXPECT_NEAR(0.0, tukey(8), 1.0e-12);

  const auto exponential = ksj::signal::exponential_filter<double>(4, 0, 4, 1.0, 2.0);
  EXPECT_NEAR(std::exp(-0.25), exponential(0), 1.0e-12);
  EXPECT_NEAR(std::exp(-0.0625), exponential(1), 1.0e-12);
  EXPECT_NEAR(1.0, exponential(2), 1.0e-12);
  EXPECT_NEAR(std::exp(-0.0625), exponential(3), 1.0e-12);

  const auto fermi = ksj::signal::fermi_filter<double>(4, 0, 4, 2.0, 1.0);
  EXPECT_NEAR(0.5, fermi(0), 1.0e-12);
  EXPECT_NEAR(1.0 / (1.0 + std::exp(-2.0)), fermi(2), 1.0e-12);

  const auto quadratic_exponential = ksj::signal::quadratic_exponential_filter<double>(5, 2.0, 4.0);
  EXPECT_NEAR(std::exp(0.25), quadratic_exponential(0), 1.0e-12);
  EXPECT_NEAR(std::exp(0.01), quadratic_exponential(2), 1.0e-12);

  const auto t2_linear = ksj::signal::t2_linear_filter<double>(3, 2, 0.5, 3);
  EXPECT_NEAR(0.5, t2_linear(0), 1.0e-12);
  EXPECT_NEAR(1.0, t2_linear(1), 1.0e-12);
  EXPECT_NEAR(1.5, t2_linear(2), 1.0e-12);

  const auto t2_exponential = ksj::signal::t2_exponential_filter<double>(3, 2, 5.0, 3, 10.0);
  EXPECT_NEAR(std::exp(0.5), t2_exponential(0), 1.0e-12);
  EXPECT_NEAR(1.0, t2_exponential(1), 1.0e-12);
  EXPECT_NEAR(std::exp(-0.5), t2_exponential(2), 1.0e-12);

  const auto denominator = ksj::signal::cosine_laplacian_denominator<double>(2, 3, 3);
  EXPECT_NEAR(0.0, denominator(0, 0), 1.0e-12);
  EXPECT_NEAR(-1.0, denominator(0, 1), 1.0e-12);
  EXPECT_NEAR(-1.0, denominator(1, 0), 1.0e-12);
}

TEST(KSpaceJetSignal, BuildsBandPassWindowFamilies) {
  const auto bandpass = ksj::signal::fermi_bandpass_window<double>(9, 1.0, 3.0, 0.25);

  EXPECT_LT(bandpass(4), 0.1);
  EXPECT_GT(bandpass(2), 0.8);
  EXPECT_GT(bandpass(6), 0.8);
  EXPECT_LT(bandpass(0), 0.1);
  EXPECT_DOUBLE_EQ(bandpass(2), bandpass(6));

  const auto dual = ksj::signal::dual_fermi_band_window<double>(9, 2.0, 0.5, 0.2);
  EXPECT_GT(dual(2), dual(4));
  EXPECT_GT(dual(6), dual(4));
  EXPECT_DOUBLE_EQ(dual(2), dual(6));

  EXPECT_THROW((void)ksj::signal::fermi_bandpass_window<double>(9, 3.0, 1.0, 0.25), std::invalid_argument);
  EXPECT_THROW((void)ksj::signal::dual_fermi_band_window<double>(9, 2.0, 0.5, 0.0), std::invalid_argument);
}

TEST(KSpaceJetSignal, BuildsFiltersIntoViewOutputs) {
  auto quadratic = ksj::array::make_pooled_vector<double>(5);
  ksj::signal::quadratic_exponential_filter(quadratic.view(), 2.0, 4.0);
  EXPECT_NEAR(std::exp(0.25), quadratic(0), 1.0e-12);
  EXPECT_NEAR(std::exp(0.01), quadratic(2), 1.0e-12);

  auto t2 = ksj::array::make_pooled_vector<double>(3);
  ksj::signal::t2_exponential_filter(t2.view(), 2, 5.0, 3, 10.0);
  EXPECT_NEAR(std::exp(0.5), t2(0), 1.0e-12);
  EXPECT_NEAR(1.0, t2(1), 1.0e-12);
  EXPECT_NEAR(std::exp(-0.5), t2(2), 1.0e-12);

  auto denominator = ksj::array::make_pooled_image<double>(2, 3);
  ksj::signal::cosine_laplacian_denominator(denominator.view(), 3);
  EXPECT_NEAR(0.0, denominator(0, 0), 1.0e-12);
  EXPECT_NEAR(-1.0, denominator(0, 1), 1.0e-12);
  EXPECT_NEAR(-1.0, denominator(1, 0), 1.0e-12);

  auto bandpass = ksj::array::make_pooled_vector<double>(9);
  ksj::signal::fermi_bandpass_window(bandpass.view(), 1.0, 3.0, 0.25);
  EXPECT_LT(bandpass(4), 0.1);
  EXPECT_GT(bandpass(2), 0.8);
}

TEST(KSpaceJetSignal, WrapsAndUnwrapsPhase) {
  auto phase = ksj::array::make_pooled_vector<double>(4);
  phase(0) = 0.0;
  phase(1) = 0.75 * std::numbers::pi;
  phase(2) = -0.75 * std::numbers::pi;
  phase(3) = -0.5 * std::numbers::pi;

  const auto unwrapped = ksj::signal::unwrap_phase(phase);
  EXPECT_NEAR(0.0, unwrapped(0), 1.0e-12);
  EXPECT_NEAR(0.75 * std::numbers::pi, unwrapped(1), 1.0e-12);
  EXPECT_NEAR(1.25 * std::numbers::pi, unwrapped(2), 1.0e-12);
  EXPECT_NEAR(1.5 * std::numbers::pi, unwrapped(3), 1.0e-12);

  const auto wrapped = ksj::signal::wrap_phase(unwrapped);
  EXPECT_NEAR(phase(0), wrapped(0), 1.0e-12);
  EXPECT_NEAR(phase(1), wrapped(1), 1.0e-12);
  EXPECT_NEAR(phase(2), wrapped(2), 1.0e-12);
  EXPECT_NEAR(phase(3), wrapped(3), 1.0e-12);
}

TEST(KSpaceJetSignal, WrapsAndUnwrapsPhaseIntoViewOutputs) {
  auto phase = ksj::array::make_pooled_vector<double>(4);
  phase(0) = 0.0;
  phase(1) = 0.75 * std::numbers::pi;
  phase(2) = -0.75 * std::numbers::pi;
  phase(3) = -0.5 * std::numbers::pi;

  std::array<double, 8> unwrap_storage{};
  auto unwrapped = ksj::array::VectorView<double>(unwrap_storage.data(), unwrap_storage.size())
                     .subview(ksj::array::slice(0U, unwrap_storage.size(), 2U));
  ksj::signal::unwrap_phase(ksj::array::as_const_view(phase.view()), unwrapped);

  EXPECT_NEAR(0.0, unwrapped(0), 1.0e-12);
  EXPECT_NEAR(0.75 * std::numbers::pi, unwrapped(1), 1.0e-12);
  EXPECT_NEAR(1.25 * std::numbers::pi, unwrapped(2), 1.0e-12);
  EXPECT_NEAR(1.5 * std::numbers::pi, unwrapped(3), 1.0e-12);
  EXPECT_DOUBLE_EQ(0.0, unwrap_storage[1]);
  EXPECT_DOUBLE_EQ(0.0, unwrap_storage[3]);
  EXPECT_DOUBLE_EQ(0.0, unwrap_storage[5]);
  EXPECT_DOUBLE_EQ(0.0, unwrap_storage[7]);

  auto wrapped = ksj::array::make_pooled_vector<double>(4);
  ksj::signal::wrap_phase(ksj::array::as_const_view(unwrapped), wrapped.view());
  for (std::size_t index = 0; index < phase.size(); ++index) {
    EXPECT_NEAR(phase(index), wrapped(index), 1.0e-12);
  }
}

TEST(KSpaceJetSignal, WrapsAndUnwrapsPhaseImages) {
  auto phase = ksj::array::make_pooled_image<double>(2, 3);
  phase(0, 0) = 0.0;
  phase(0, 1) = 0.75 * std::numbers::pi;
  phase(0, 2) = -0.75 * std::numbers::pi;
  phase(1, 0) = 0.75 * std::numbers::pi;
  phase(1, 1) = -0.5 * std::numbers::pi;
  phase(1, 2) = 0.0;

  const auto unwrapped = ksj::signal::unwrap_phase_2d(phase);
  EXPECT_NEAR(0.0, unwrapped(0, 0), 1.0e-12);
  EXPECT_NEAR(0.75 * std::numbers::pi, unwrapped(0, 1), 1.0e-12);
  EXPECT_NEAR(1.25 * std::numbers::pi, unwrapped(0, 2), 1.0e-12);
  EXPECT_NEAR(0.75 * std::numbers::pi, unwrapped(1, 0), 1.0e-12);
  EXPECT_NEAR(1.5 * std::numbers::pi, unwrapped(1, 1), 1.0e-12);
  EXPECT_NEAR(2.0 * std::numbers::pi, unwrapped(1, 2), 1.0e-12);

  const auto wrapped = ksj::signal::wrap_phase(unwrapped);
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      EXPECT_NEAR(phase(row, col), wrapped(row, col), 1.0e-12);
    }
  }
}

TEST(KSpaceJetSignal, WrapsAndUnwrapsPhaseImagesIntoViewOutputs) {
  auto phase = ksj::array::make_pooled_image<double>(2, 3);
  phase(0, 0) = 0.0;
  phase(0, 1) = 0.75 * std::numbers::pi;
  phase(0, 2) = -0.75 * std::numbers::pi;
  phase(1, 0) = 0.75 * std::numbers::pi;
  phase(1, 1) = -0.5 * std::numbers::pi;
  phase(1, 2) = 0.0;

  auto unwrapped = ksj::array::make_pooled_image<double>(2, 3);
  ksj::signal::unwrap_phase_2d(ksj::array::as_const_view(phase.view()), unwrapped.view());
  EXPECT_NEAR(1.25 * std::numbers::pi, unwrapped(0, 2), 1.0e-12);
  EXPECT_NEAR(2.0 * std::numbers::pi, unwrapped(1, 2), 1.0e-12);

  auto wrapped = ksj::array::make_pooled_image<double>(2, 3);
  ksj::signal::wrap_phase(ksj::array::as_const_view(unwrapped.view()), wrapped.view());
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      EXPECT_NEAR(phase(row, col), wrapped(row, col), 1.0e-12);
    }
  }
}

TEST(KSpaceJetSignal, UnwrapsPhaseWithLaplacianFft) {
  auto phase = ksj::array::make_pooled_image<double>(8, 8);
  double mean = 0.0;
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      const auto value =
        0.25 * std::sin(2.0 * std::numbers::pi * static_cast<double>(row) / static_cast<double>(phase.rows())) +
        0.15 * std::cos(2.0 * std::numbers::pi * static_cast<double>(col) / static_cast<double>(phase.cols()));
      phase(row, col) = value;
      mean += value;
    }
  }
  mean /= static_cast<double>(phase.size());

  const auto unwrapped = ksj::signal::unwrap_phase_laplacian_2d(phase);
  ASSERT_EQ(phase.rows(), unwrapped.rows());
  ASSERT_EQ(phase.cols(), unwrapped.cols());
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      EXPECT_NEAR(phase(row, col) - mean, unwrapped(row, col), 2.0e-3);
    }
  }
}

TEST(KSpaceJetSignal, LaplacianPhaseUnwrapKeepsZeroPhaseAtZero) {
  auto phase = ksj::array::make_pooled_image<float>(4, 5);
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      phase(row, col) = 0.0F;
    }
  }

  const auto unwrapped = ksj::signal::unwrap_phase_laplacian_2d(phase);
  ASSERT_EQ(4U, unwrapped.rows());
  ASSERT_EQ(5U, unwrapped.cols());
  for (std::size_t row = 0; row < unwrapped.rows(); ++row) {
    for (std::size_t col = 0; col < unwrapped.cols(); ++col) {
      EXPECT_NEAR(0.0F, unwrapped(row, col), 1.0e-6F);
    }
  }
}

TEST(KSpaceJetSignal, ComposesSeparableKernelAndCorrelates2d) {
  auto row_kernel = ksj::array::make_pooled_vector<double>(3);
  auto col_kernel = ksj::array::make_pooled_vector<double>(3);
  row_kernel(0) = 1.0;
  row_kernel(1) = 2.0;
  row_kernel(2) = 1.0;
  col_kernel(0) = 1.0;
  col_kernel(1) = 0.0;
  col_kernel(2) = -1.0;

  const auto kernel = ksj::signal::compose_separable_kernel(row_kernel, col_kernel);
  ASSERT_EQ(3U, kernel.rows());
  ASSERT_EQ(3U, kernel.cols());
  EXPECT_DOUBLE_EQ(2.0, kernel(0, 1));
  EXPECT_DOUBLE_EQ(0.0, kernel(1, 1));
  EXPECT_DOUBLE_EQ(-2.0, kernel(2, 1));

  auto image = ksj::array::make_pooled_image<double>(3, 3);
  for (std::size_t row = 0; row < 3U; ++row) {
    for (std::size_t col = 0; col < 3U; ++col) {
      image(row, col) = static_cast<double>(row * 10U + col);
    }
  }

  const auto output = ksj::signal::correlate2d_same(image, kernel);
  EXPECT_DOUBLE_EQ(-80.0, output(1, 1));

  auto output_view = ksj::array::make_pooled_image<double>(image.rows(), image.cols());
  ksj::signal::correlate2d_same(ksj::array::as_const_view(image.view()), ksj::array::as_const_view(kernel.view()),
                                output_view.view());
  EXPECT_DOUBLE_EQ(output(1, 1), output_view(1, 1));

  const auto separable = ksj::signal::correlate2d_same_separable(image, row_kernel, col_kernel);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_DOUBLE_EQ(output(row, col), separable(row, col));
    }
  }

  auto separable_view = ksj::array::make_pooled_image<double>(image.rows(), image.cols());
  auto scratch = ksj::array::make_pooled_image<double>(image.rows(), image.cols());
  ksj::signal::correlate2d_same_separable(
    ksj::array::as_const_view(image.view()), ksj::array::as_const_view(row_kernel.view()),
    ksj::array::as_const_view(col_kernel.view()), separable_view.view(), scratch.view());
  EXPECT_DOUBLE_EQ(output(1, 1), separable_view(1, 1));

  const auto public_separable = ksj::signal::correlate2d_same_separable(image, row_kernel, col_kernel);
  EXPECT_DOUBLE_EQ(-80.0, public_separable(1, 1));
}

TEST(KSpaceJetSignal, Convolve2dSameUsesPolicyPathForLargeKernels) {
  auto image = ksj::array::make_pooled_image<float>(64, 64);
  for (std::size_t row = 0U; row < image.rows(); ++row) {
    for (std::size_t col = 0U; col < image.cols(); ++col) {
      image(row, col) = std::sin(static_cast<float>(row) * 0.125F) + std::cos(static_cast<float>(col) * 0.0625F);
    }
  }

  auto kernel = ksj::array::make_pooled_image<float>(31, 31);
  for (std::size_t row = 0U; row < kernel.rows(); ++row) {
    for (std::size_t col = 0U; col < kernel.cols(); ++col) {
      kernel(row, col) = (row == col ? 0.01F : -0.001F);
    }
  }

  auto reference = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  ksj::signal::detail::eigen::convolve2d_same(ksj::array::as_const_view(image.view()),
                                              ksj::array::as_const_view(kernel.view()), reference.view());
  const auto output = ksj::signal::convolve2d_same(image, kernel);

  for (std::size_t row = 0U; row < output.rows(); ++row) {
    for (std::size_t col = 0U; col < output.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), output(row, col), 2.0e-4F);
    }
  }
}

TEST(KSpaceJetSignal, Convolve2dSameKeepsComplexInputsOnSupportedBackend) {
  using complex_type = std::complex<float>;
  auto image = ksj::array::make_pooled_image<complex_type>(64, 64);
  for (std::size_t row = 0U; row < image.rows(); ++row) {
    for (std::size_t col = 0U; col < image.cols(); ++col) {
      image(row, col) = complex_type{static_cast<float>(row) * 0.125F, static_cast<float>(col) * -0.0625F};
    }
  }

  auto kernel = ksj::array::make_pooled_image<complex_type>(31, 31);
  for (std::size_t row = 0U; row < kernel.rows(); ++row) {
    for (std::size_t col = 0U; col < kernel.cols(); ++col) {
      kernel(row, col) = complex_type{row == col ? 0.01F : -0.001F, row == col ? -0.002F : 0.0005F};
    }
  }

  auto reference = ksj::array::make_pooled_image<complex_type>(image.rows(), image.cols());
  ksj::signal::detail::eigen::convolve2d_same(ksj::array::as_const_view(image.view()),
                                              ksj::array::as_const_view(kernel.view()), reference.view());
  const auto output = ksj::signal::convolve2d_same(image, kernel);

  for (std::size_t row = 0U; row < output.rows(); ++row) {
    for (std::size_t col = 0U; col < output.cols(); ++col) {
      EXPECT_NEAR(0.0F, std::abs(reference(row, col) - output(row, col)), 1.0e-4F);
    }
  }
}

TEST(KSpaceJetSignal, OpenCvCorrelate2dMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(4, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * 5U + col);
    }
  }

  auto kernel = ksj::array::make_pooled_image<float>(3, 3);
  kernel(0, 0) = 0.0F;
  kernel(0, 1) = 1.0F;
  kernel(0, 2) = 0.0F;
  kernel(1, 0) = -1.0F;
  kernel(1, 1) = 2.0F;
  kernel(1, 2) = -1.0F;
  kernel(2, 0) = 0.0F;
  kernel(2, 1) = 1.0F;
  kernel(2, 2) = 0.0F;

  const auto reference = ksj::signal::detail::eigen::correlate2d_same(image, kernel);
  auto opencv = ksj::array::make_pooled_image<float>(4, 4);
  ASSERT_TRUE(ksj::signal::detail::opencv::correlate2d_same(ksj::array::as_const_view(image.view()),
                                                            ksj::array::as_const_view(kernel.view()), opencv.view()));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), opencv(row, col), 1.0e-5F);
    }
  }
}

TEST(KSpaceJetSignal, IppCorrelate2dMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(8, 7);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>((row * 11U + col * 5U) % 17U) * 0.25F - 1.5F;
    }
  }

  auto kernel = ksj::array::make_pooled_image<float>(5, 3);
  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      kernel(row, col) = static_cast<float>((row * 3U + col * 7U) % 13U) * 0.125F - 0.75F;
    }
  }

  const auto reference = ksj::signal::detail::eigen::correlate2d_same(image, kernel);
  auto ipp = ksj::array::make_pooled_image<float>(image.rows(), image.cols());
  ASSERT_TRUE(ksj::signal::detail::intel::correlate2d_same(ksj::array::as_const_view(image.view()),
                                                           ksj::array::as_const_view(kernel.view()), ipp.view()));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), ipp(row, col), 1.0e-4F);
    }
  }

  auto double_image = ksj::array::make_pooled_image<double>(8, 7);
  auto double_kernel = ksj::array::make_pooled_image<double>(5, 3);
  auto double_output = ksj::array::make_pooled_image<double>(8, 7);
  EXPECT_FALSE(ksj::signal::detail::intel::correlate2d_same(ksj::array::as_const_view(double_image.view()),
                                                            ksj::array::as_const_view(double_kernel.view()),
                                                            double_output.view()));
}

TEST(KSpaceJetSignal, IppConvolve2dFullMatchesReference) {
  auto input = ksj::array::make_pooled_matrix<float>(8, 7);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      input(row, col) = static_cast<float>((row * 11U + col * 5U) % 17U) * 0.25F - 1.5F;
    }
  }

  auto kernel = ksj::array::make_pooled_matrix<float>(5, 3);
  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      kernel(row, col) = static_cast<float>((row * 3U + col * 7U) % 13U) * 0.125F - 0.75F;
    }
  }

  const auto reference = ksj::signal::detail::eigen::convolve2d_full(input, kernel);
  auto ipp = ksj::array::make_pooled_matrix<float>(reference.rows(), reference.cols());
  ASSERT_TRUE(ksj::signal::detail::intel::convolve2d_full(ksj::array::as_const_view(input.view()),
                                                          ksj::array::as_const_view(kernel.view()), ipp.view()));

  for (std::size_t row = 0; row < reference.rows(); ++row) {
    for (std::size_t col = 0; col < reference.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), ipp(row, col), 1.0e-4F);
    }
  }

  auto double_input = ksj::array::make_pooled_matrix<double>(8, 7);
  auto double_kernel = ksj::array::make_pooled_matrix<double>(5, 3);
  auto double_output = ksj::array::make_pooled_matrix<double>(12, 9);
  EXPECT_FALSE(ksj::signal::detail::intel::convolve2d_full(ksj::array::as_const_view(double_input.view()),
                                                           ksj::array::as_const_view(double_kernel.view()),
                                                           double_output.view()));
}

TEST(KSpaceJetSignal, OpenCvSeparableCorrelate2dMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(4, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>(row * 5U + col);
    }
  }

  auto row_kernel = ksj::array::make_pooled_vector<float>(3);
  auto col_kernel = ksj::array::make_pooled_vector<float>(3);
  row_kernel(0) = 1.0F;
  row_kernel(1) = 2.0F;
  row_kernel(2) = 1.0F;
  col_kernel(0) = 1.0F;
  col_kernel(1) = 0.0F;
  col_kernel(2) = -1.0F;

  const auto reference = ksj::signal::detail::eigen::correlate2d_same_separable(image, row_kernel, col_kernel);
  auto opencv = ksj::array::make_pooled_image<float>(4, 4);
  ASSERT_TRUE(ksj::signal::detail::opencv::correlate2d_same_separable(
    ksj::array::as_const_view(image.view()), ksj::array::as_const_view(row_kernel.view()),
    ksj::array::as_const_view(col_kernel.view()), opencv.view()));

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), opencv(row, col), 1.0e-5F);
    }
  }
}

TEST(KSpaceJetSignal, FftCorrelate2dMatchesReferenceForLargeKernel) {
  auto image = ksj::array::make_pooled_image<double>(5, 4);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<double>((row * 7U + col * 5U) % 11U) - 3.0;
    }
  }

  auto kernel = ksj::array::make_pooled_image<double>(4, 3);
  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      kernel(row, col) = static_cast<double>((row * 3U + col * 2U) % 7U) * 0.25 - 0.5;
    }
  }

  const auto reference = ksj::signal::detail::eigen::correlate2d_same(image, kernel);
  const auto fft = ksj::signal::correlate2d_same_fft(image, kernel);

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), fft(row, col), 1.0e-10);
    }
  }
  const auto fft_again = ksj::signal::correlate2d_same_fft(image, kernel);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), fft_again(row, col), 1.0e-10);
    }
  }
}

TEST(KSpaceJetSignal, DefaultLargeKernelCorrelate2dPolicyMatchesReference) {
  auto image = ksj::array::make_pooled_image<double>(32, 32);
  auto kernel = ksj::array::make_pooled_image<double>(31, 31);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<double>((row * 11U + col * 7U) % 23U) * 0.125;
    }
  }
  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      kernel(row, col) = static_cast<double>((row * 5U + col * 3U) % 17U) * 0.01;
    }
  }

  const auto reference = ksj::signal::detail::eigen::correlate2d_same(image, kernel);
  const auto output = ksj::signal::correlate2d_same(image, kernel);

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), output(row, col), 1.0e-8);
    }
  }
}

TEST(KSpaceJetSignal, DefaultFloatLargeKernelCorrelate2dPolicyMatchesReference) {
  auto image = ksj::array::make_pooled_image<float>(32, 32);
  auto kernel = ksj::array::make_pooled_image<float>(31, 31);
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<float>((row * 11U + col * 7U) % 23U) * 0.125F;
    }
  }
  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      kernel(row, col) = static_cast<float>((row * 5U + col * 3U) % 17U) * 0.01F;
    }
  }

  const auto reference = ksj::signal::detail::eigen::correlate2d_same(image, kernel);
  const auto output = ksj::signal::correlate2d_same(image, kernel);

  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(reference(row, col), output(row, col), 2.0e-4F);
    }
  }
}

TEST(KSpaceJetSignal, RejectsInvalidSignalDimensions) {
  auto vector = ksj::array::make_pooled_vector<double>(2);
  auto empty = ksj::array::make_pooled_vector<double>(0);
  EXPECT_THROW((void)ksj::signal::resample(empty, 2), std::invalid_argument);
  EXPECT_THROW((void)ksj::signal::resample(vector, 2, static_cast<ksj::signal::ResampleKernel>(99)),
               std::invalid_argument);

  auto phase = ksj::array::make_pooled_image<double>(2, 2);
  EXPECT_THROW((void)ksj::signal::unwrap_phase_2d(phase, -1.0), std::invalid_argument);

  auto kernel = ksj::array::make_pooled_image<double>(3, 3);
  auto empty_image = ksj::array::make_pooled_image<double>(0, 0);
  EXPECT_THROW((void)ksj::signal::correlate2d_same(empty_image, kernel), std::invalid_argument);

  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  auto matrix_kernel = ksj::array::make_pooled_matrix<double>(2, 2);
  auto wrong_full_output = ksj::array::make_pooled_matrix<double>(2, 2);
  EXPECT_THROW(ksj::signal::convolve2d_full(ksj::array::as_const_view(matrix.view()),
                                            ksj::array::as_const_view(matrix_kernel.view()), wrong_full_output.view()),
               std::invalid_argument);

  auto row_kernel = ksj::array::make_pooled_vector<double>(0);
  auto col_kernel = ksj::array::make_pooled_vector<double>(3);
  EXPECT_THROW((void)ksj::signal::correlate2d_same_separable(phase, row_kernel, col_kernel), std::invalid_argument);

  EXPECT_THROW((void)ksj::signal::correlate2d_same_fft(empty_image, kernel), std::invalid_argument);
}

} // namespace
