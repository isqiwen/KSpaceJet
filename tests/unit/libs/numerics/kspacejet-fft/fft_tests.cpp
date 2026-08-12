#include "../eigen_test_adapter.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/fft/fft.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace {

void expect_cube_near(const ksj::array::PooledCube<ksj::base::cf64>& expected,
                      const ksj::array::PooledCube<ksj::base::cf64>& actual, const double tolerance = 1.0e-12) {
  ASSERT_EQ(expected.dim0(), actual.dim0());
  ASSERT_EQ(expected.dim1(), actual.dim1());
  ASSERT_EQ(expected.dim2(), actual.dim2());
  for (std::size_t dim0 = 0U; dim0 < expected.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < expected.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < expected.dim2(); ++dim2) {
        EXPECT_NEAR(expected(dim0, dim1, dim2).real(), actual(dim0, dim1, dim2).real(), tolerance);
        EXPECT_NEAR(expected(dim0, dim1, dim2).imag(), actual(dim0, dim1, dim2).imag(), tolerance);
      }
    }
  }
}

TEST(KSpaceJetFft, SelectsMklForLargeFreeDoubleFftOnly) {
  EXPECT_FALSE(ksj::fft::detail::prefer_intel_fft<float>(128U * 1024U - 1U));
  EXPECT_TRUE(ksj::fft::detail::prefer_intel_fft<float>(128U * 1024U));
  EXPECT_FALSE(ksj::fft::detail::prefer_intel_fft<double>(32U * 1024U - 1U));
  EXPECT_TRUE(ksj::fft::detail::prefer_intel_fft<double>(32U * 1024U));
}

TEST(KSpaceJetFft, ComputesImpulseSpectrum) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  as_eigen(input).setZero();
  input(0) = {1.0, 0.0};

  const auto output = ksj::fft::fft(input);

  ASSERT_EQ(4U, output.size());
  for (std::size_t i = 0; i < output.size(); ++i) {
    EXPECT_NEAR(1.0, output(i).real(), 1e-12);
    EXPECT_NEAR(0.0, output(i).imag(), 1e-12);
  }
}

TEST(KSpaceJetFft, ReturnsVectorFftFromViewInput) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  input(0) = {1.0, 0.0};
  input(1) = {2.0, 0.0};
  input(2) = {3.0, 0.0};
  input(3) = {4.0, 0.0};

  const auto spectrum = ksj::fft::fft(ksj::array::as_const_view(input.view()));
  const auto restored = ksj::fft::ifft(ksj::array::as_const_view(spectrum.view()));

  ASSERT_EQ(input.size(), spectrum.size());
  ASSERT_EQ(input.size(), restored.size());
  for (std::size_t index = 0U; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), restored(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), restored(index).imag(), 1.0e-12);
  }
}

TEST(KSpaceJetFft, TransformsStridedVectorViewsThroughEigenFallback) {
  constexpr auto sentinel = ksj::base::cf64{-99.0, 7.0};
  std::array<ksj::base::cf64, 8> input_storage{};
  std::array<ksj::base::cf64, 8> spectrum_storage{};
  std::array<ksj::base::cf64, 8> restored_storage{};
  spectrum_storage.fill(sentinel);
  restored_storage.fill(sentinel);

  auto input = ksj::array::VectorView<ksj::base::cf64>(input_storage.data(), input_storage.size())
                 .subview(ksj::array::slice(0U, input_storage.size(), 2U));
  auto spectrum = ksj::array::VectorView<ksj::base::cf64>(spectrum_storage.data(), spectrum_storage.size())
                    .subview(ksj::array::slice(0U, spectrum_storage.size(), 2U));
  auto restored = ksj::array::VectorView<ksj::base::cf64>(restored_storage.data(), restored_storage.size())
                    .subview(ksj::array::slice(0U, restored_storage.size(), 2U));
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = {static_cast<double>(index + 1U), 0.0};
  }

  ksj::fft::fft(ksj::array::as_const_view(input), spectrum);
  EXPECT_NEAR(10.0, spectrum(0).real(), 1.0e-12);
  EXPECT_NEAR(0.0, spectrum(0).imag(), 1.0e-12);
  EXPECT_NEAR(-2.0, spectrum(1).real(), 1.0e-12);
  EXPECT_NEAR(2.0, spectrum(1).imag(), 1.0e-12);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_EQ(sentinel, spectrum_storage[index * 2U + 1U]);
  }

  ksj::fft::fft(ksj::array::as_const_view(spectrum), restored, ksj::fft::Direction::inverse,
                ksj::fft::Normalization::inverse);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), restored(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), restored(index).imag(), 1.0e-12);
    EXPECT_EQ(sentinel, restored_storage[index * 2U + 1U]);
  }
}

TEST(KSpaceJetFft, InverseNormalizationRestoresInput) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  input(0) = {1.0, 0.0};
  input(1) = {2.0, 0.0};
  input(2) = {3.0, 0.0};
  input(3) = {4.0, 0.0};

  const auto spectrum = ksj::fft::fft(input);
  const auto restored = ksj::fft::ifft(spectrum);

  for (std::size_t i = 0; i < input.size(); ++i) {
    EXPECT_NEAR(input(i).real(), restored(i).real(), 1e-12);
    EXPECT_NEAR(input(i).imag(), restored(i).imag(), 1e-12);
  }
}

TEST(KSpaceJetFft, WritesFftIntoExistingStorage) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  as_eigen(input).setZero();
  input(0) = {1.0, 0.0};

  auto output = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  ksj::fft::fft(input, output);

  for (std::size_t i = 0; i < output.size(); ++i) {
    EXPECT_NEAR(1.0, output(i).real(), 1.0e-12);
    EXPECT_NEAR(0.0, output(i).imag(), 1.0e-12);
  }

  ksj::fft::ifft(output, output);
  EXPECT_NEAR(1.0, output(0).real(), 1.0e-12);
  EXPECT_NEAR(0.0, output(0).imag(), 1.0e-12);
  for (std::size_t i = 1; i < output.size(); ++i) {
    EXPECT_NEAR(0.0, output(i).real(), 1.0e-12);
    EXPECT_NEAR(0.0, output(i).imag(), 1.0e-12);
  }
}

TEST(KSpaceJetFft, Uses1dPlanForCachedDimensions) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  input(0) = {1.0, 0.0};
  input(1) = {2.0, 0.5};
  input(2) = {3.0, -1.0};
  input(3) = {4.0, 2.0};
  auto spectrum = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  ksj::fft::Fft1Plan<double> forward_plan(4);

  forward_plan.execute(input, spectrum);

  EXPECT_EQ(4U, forward_plan.size());
  EXPECT_EQ(ksj::fft::Direction::forward, forward_plan.direction());
  EXPECT_EQ(ksj::fft::Normalization::none, forward_plan.normalization());

  auto restored = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  ksj::fft::Fft1Plan<double> inverse_plan(4, ksj::fft::Direction::inverse, ksj::fft::Normalization::inverse);
  inverse_plan.execute(spectrum, restored);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), restored(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), restored(index).imag(), 1.0e-12);
  }

  auto inplace_spectrum = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  ksj::array::copy(spectrum.view(), inplace_spectrum.view());
  inverse_plan.execute_in_place(inplace_spectrum.view());
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), inplace_spectrum(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), inplace_spectrum(index).imag(), 1.0e-12);
  }

  auto strided_spectrum_storage = ksj::array::make_pooled_vector<ksj::base::cf64>(8);
  ksj::array::fill(strided_spectrum_storage.view(), ksj::base::cf64{-99.0, 7.0});
  for (std::size_t index = 0; index < spectrum.size(); ++index) {
    strided_spectrum_storage(index * 2U) = spectrum(index);
  }
  auto strided_spectrum =
    strided_spectrum_storage.view().subview(ksj::array::slice(0U, strided_spectrum_storage.size(), 2U));
  auto inverse_scratch = ksj::array::make_pooled_vector<ksj::base::cf64>(0);
  inverse_plan.execute_in_place(strided_spectrum, inverse_scratch);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), strided_spectrum(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), strided_spectrum(index).imag(), 1.0e-12);
    EXPECT_NEAR(-99.0, strided_spectrum_storage(index * 2U + 1U).real(), 1.0e-12);
    EXPECT_NEAR(7.0, strided_spectrum_storage(index * 2U + 1U).imag(), 1.0e-12);
  }

  inverse_plan.execute(spectrum, spectrum);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), spectrum(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), spectrum(index).imag(), 1.0e-12);
  }

  auto short_vector = ksj::array::make_pooled_vector<ksj::base::cf64>(3);
  EXPECT_THROW(forward_plan.execute(short_vector, restored), std::invalid_argument);
}

TEST(KSpaceJetFft, OrthonormalForwardPlanUsesOrthonormalScalingForFftAndDftLengths) {
  for (const std::size_t size : {4U, 5U}) {
    auto input = ksj::array::make_pooled_vector<ksj::base::cf32>(size);
    ksj::array::fill(input.view(), ksj::base::cf32{});
    input(0) = {1.0F, 0.0F};
    auto output = ksj::array::make_pooled_vector<ksj::base::cf32>(size);

    ksj::fft::OrthonormalForwardFft1Plan plan(size);

    EXPECT_EQ(size, plan.size());
    ASSERT_NO_THROW(plan.execute(ksj::array::as_const_view(input.view()), output.view()));

    const auto expected = 1.0F / std::sqrt(static_cast<float>(size));
    for (std::size_t index = 0; index < output.size(); ++index) {
      EXPECT_NEAR(expected, output(index).real(), 1.0e-6F);
      EXPECT_NEAR(0.0F, output(index).imag(), 1.0e-6F);
    }
  }
}

TEST(KSpaceJetFft, ExecutorMatchesPolicySelectedFreeFftExactly) {
  constexpr std::size_t kSize = 568U;
  auto input = ksj::array::make_pooled_vector<ksj::base::cf32>(kSize);
  for (std::size_t index = 0U; index < kSize; ++index) {
    input(index) = {static_cast<float>((index * 17U) % 101U) / 37.0F, static_cast<float>((index * 29U) % 89U) / 41.0F};
  }

  ksj::fft::PolicyFft1Executor<float> executor;
  for (const auto direction : {ksj::fft::Direction::inverse, ksj::fft::Direction::forward}) {
    const auto normalization =
      direction == ksj::fft::Direction::forward ? ksj::fft::Normalization::none : ksj::fft::Normalization::inverse;
    auto expected = ksj::array::make_pooled_vector(input.view());
    auto actual = ksj::array::make_pooled_vector(input.view());
    ksj::fft::fft_inplace(expected.view(), direction, normalization, true, true);
    executor.execute_inplace(actual.view(), direction, normalization, true, true);

    for (std::size_t index = 0U; index < kSize; ++index) {
      EXPECT_EQ(expected(index), actual(index));
    }
  }
}

TEST(KSpaceJetFft, CachedExecutorMatchesPolicyExecutorForShiftedFloatFft) {
  constexpr std::size_t kSize = 512U;
  auto input = ksj::array::make_pooled_vector<ksj::base::cf32>(kSize);
  for (std::size_t index = 0U; index < kSize; ++index) {
    input(index) = {static_cast<float>((index * 17U) % 101U) / 131.0F,
                    static_cast<float>((index * 29U) % 89U) / 137.0F};
  }

  for (const auto direction : {ksj::fft::Direction::forward, ksj::fft::Direction::inverse}) {
    const auto normalization =
      direction == ksj::fft::Direction::forward ? ksj::fft::Normalization::none : ksj::fft::Normalization::inverse;
    auto expected = ksj::array::make_pooled_vector(input.view());
    auto actual = ksj::array::make_pooled_vector(input.view());
    ksj::fft::PolicyFft1Executor<float> policy_executor;
    ksj::fft::Fft1Executor<float> cached_executor;

    policy_executor.execute_inplace(expected.view(), direction, normalization, true, true);
    cached_executor.execute_inplace(actual.view(), direction, normalization, true, true);

    for (std::size_t index = 0U; index < kSize; ++index) {
      const auto tolerance = 3.0e-5F * std::max(1.0F, std::abs(expected(index)));
      EXPECT_LE(std::abs(expected(index) - actual(index)), tolerance) << "index=" << index;
    }
  }
}

TEST(KSpaceJetFft, Writes1dSegmentedFft) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  input(0) = {1.0, 0.0};
  input(1) = {0.0, 0.0};
  input(2) = {2.0, 0.0};
  input(3) = {0.0, 0.0};
  auto output = ksj::array::make_pooled_vector<ksj::base::cf64>(4);

  ksj::fft::fft_segmented(input, output, 2);

  EXPECT_NEAR(1.0, output(0).real(), 1.0e-12);
  EXPECT_NEAR(1.0, output(1).real(), 1.0e-12);
  EXPECT_NEAR(2.0, output(2).real(), 1.0e-12);
  EXPECT_NEAR(2.0, output(3).real(), 1.0e-12);

  ksj::fft::ifft_segmented(output, output, 2);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), output(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), output(index).imag(), 1.0e-12);
  }

  const auto public_output = ksj::fft::fft_segmented(input, 2);
  EXPECT_NEAR(2.0, public_output(2).real(), 1.0e-12);
}

TEST(KSpaceJetFft, Shifted1dSegmentedFftRoundTrips) {
  auto input = ksj::array::make_pooled_vector<ksj::base::cf64>(6);
  input(0) = {1.0, 0.0};
  input(1) = {2.0, -1.0};
  input(2) = {-0.5, 0.25};
  input(3) = {3.0, 0.5};
  input(4) = {0.0, -2.0};
  input(5) = {1.5, 1.0};
  auto spectrum = ksj::array::make_pooled_vector<ksj::base::cf64>(6);
  auto restored = ksj::array::make_pooled_vector<ksj::base::cf64>(6);

  ksj::fft::fft_segmented(input, spectrum, 2, ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal, true,
                          true);
  ksj::fft::fft_segmented(spectrum, restored, 2, ksj::fft::Direction::inverse, ksj::fft::Normalization::orthonormal,
                          true, true);

  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_NEAR(input(index).real(), restored(index).real(), 1.0e-12);
    EXPECT_NEAR(input(index).imag(), restored(index).imag(), 1.0e-12);
  }
}

TEST(KSpaceJetFft, ShiftsOddAndEvenVectors) {
  auto odd = ksj::array::make_pooled_vector<int>(5);
  for (std::size_t index = 0; index < odd.size(); ++index) {
    odd(index) = static_cast<int>(index);
  }

  const auto shifted_odd = ksj::fft::fftshift(odd);
  EXPECT_EQ(3, shifted_odd(0));
  EXPECT_EQ(4, shifted_odd(1));
  EXPECT_EQ(0, shifted_odd(2));
  EXPECT_EQ(1, shifted_odd(3));
  EXPECT_EQ(2, shifted_odd(4));

  const auto restored_odd = ksj::fft::ifftshift(shifted_odd);
  for (std::size_t index = 0; index < odd.size(); ++index) {
    EXPECT_EQ(odd(index), restored_odd(index));
  }

  auto even = ksj::array::make_pooled_vector<int>(4);
  for (std::size_t index = 0; index < even.size(); ++index) {
    even(index) = static_cast<int>(index);
  }
  const auto shifted_even = ksj::fft::fftshift(even);
  EXPECT_EQ(2, shifted_even(0));
  EXPECT_EQ(3, shifted_even(1));
  EXPECT_EQ(0, shifted_even(2));
  EXPECT_EQ(1, shifted_even(3));
}

TEST(KSpaceJetFft, WritesVectorShiftsIntoExistingStorage) {
  auto input = ksj::array::make_pooled_vector<int>(5);
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = static_cast<int>(index);
  }

  auto output = ksj::array::make_pooled_vector<int>(5);
  ksj::fft::fftshift(input, output);
  EXPECT_EQ(3, output(0));
  EXPECT_EQ(4, output(1));
  EXPECT_EQ(0, output(2));
  EXPECT_EQ(1, output(3));
  EXPECT_EQ(2, output(4));

  ksj::fft::ifftshift(output, output);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_EQ(input(index), output(index));
  }
}

TEST(KSpaceJetFft, ShiftsStridedVectorViews) {
  std::array<int, 10> input_storage{};
  std::array<int, 10> output_storage{};
  auto input = ksj::array::VectorView<int>(input_storage.data(), input_storage.size())
                 .subview(ksj::array::slice(0U, input_storage.size(), 2U));
  auto output = ksj::array::VectorView<int>(output_storage.data(), output_storage.size())
                  .subview(ksj::array::slice(0U, output_storage.size(), 2U));
  for (std::size_t index = 0; index < input.size(); ++index) {
    input(index) = static_cast<int>(index);
  }

  ksj::fft::fftshift(ksj::array::as_const_view(input), output);
  EXPECT_EQ(3, output(0));
  EXPECT_EQ(4, output(1));
  EXPECT_EQ(0, output(2));
  EXPECT_EQ(1, output(3));
  EXPECT_EQ(2, output(4));

  ksj::fft::ifftshift_in_place(output);
  for (std::size_t index = 0; index < input.size(); ++index) {
    EXPECT_EQ(input(index), output(index));
  }
}

TEST(KSpaceJetFft, ShiftsContiguousVectorViewInPlace) {
  std::vector<int> odd{0, 1, 2, 3, 4};
  ksj::fft::fftshift_in_place(ksj::array::VectorView<int>(odd.data(), odd.size()));
  EXPECT_EQ((std::vector<int>{3, 4, 0, 1, 2}), odd);

  ksj::fft::ifftshift_in_place(ksj::array::VectorView<int>(odd.data(), odd.size()));
  EXPECT_EQ((std::vector<int>{0, 1, 2, 3, 4}), odd);

  std::vector<int> even{0, 1, 2, 3};
  ksj::fft::fftshift_in_place(ksj::array::VectorView<int>(even.data(), even.size()));
  EXPECT_EQ((std::vector<int>{2, 3, 0, 1}), even);

  ksj::fft::ifftshift_in_place(ksj::array::VectorView<int>(even.data(), even.size()));
  EXPECT_EQ((std::vector<int>{0, 1, 2, 3}), even);
}

TEST(KSpaceJetFft, ShiftsMatricesAcrossBothAxes) {
  auto matrix = ksj::array::make_pooled_matrix<int>(3, 3);
  int value = 0;
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = value++;
    }
  }

  const auto shifted = ksj::fft::fftshift(matrix);
  EXPECT_EQ(8, shifted(0, 0));
  EXPECT_EQ(6, shifted(0, 1));
  EXPECT_EQ(7, shifted(0, 2));
  EXPECT_EQ(2, shifted(1, 0));

  const auto restored = ksj::fft::ifftshift(shifted);
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      EXPECT_EQ(matrix(row, col), restored(row, col));
    }
  }
}

TEST(KSpaceJetFft, WritesMatrixShiftsIntoExistingStorage) {
  auto matrix = ksj::array::make_pooled_matrix<int>(3, 3);
  int value = 0;
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = value++;
    }
  }

  auto output = ksj::array::make_pooled_matrix<int>(3, 3);
  ksj::fft::fftshift(matrix, output);
  EXPECT_EQ(8, output(0, 0));
  EXPECT_EQ(6, output(0, 1));
  EXPECT_EQ(7, output(0, 2));
  EXPECT_EQ(2, output(1, 0));

  ksj::fft::ifftshift(output, output);
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      EXPECT_EQ(matrix(row, col), output(row, col));
    }
  }
}

TEST(KSpaceJetFft, ShiftsStridedRowMajorMatrixView) {
  constexpr std::size_t rows = 3;
  constexpr std::size_t cols = 3;
  constexpr std::size_t physical_cols = cols + 2U;
  std::vector<int> storage(rows * physical_cols);
  auto view = ksj::array::MatrixView<int>(storage.data(), rows, physical_cols)
                .subview(ksj::array::_, ksj::array::slice(0U, cols));
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      view(row, col) = static_cast<int>(row * 10U + col);
    }
  }

  ksj::fft::fftshift_in_place(view);
  EXPECT_EQ(22, view(0, 0));
  EXPECT_EQ(20, view(0, 1));
  EXPECT_EQ(21, view(0, 2));
  EXPECT_EQ(2, view(1, 0));
  EXPECT_EQ(0, view(1, 1));
  EXPECT_EQ(1, view(1, 2));
  EXPECT_EQ(12, view(2, 0));
  EXPECT_EQ(10, view(2, 1));
  EXPECT_EQ(11, view(2, 2));

  ksj::fft::ifftshift_in_place(view);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      EXPECT_EQ(static_cast<int>(row * 10U + col), view(row, col));
    }
  }
}

TEST(KSpaceJetFft, ShiftsCubesAcrossAllAxes) {
  auto cube = ksj::array::make_pooled_cube<int>(3, 2, 3);
  int value = 0;
  for (std::size_t row = 0U; row < cube.dim0(); ++row) {
    for (std::size_t col = 0U; col < cube.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < cube.dim2(); ++slice) {
        cube(row, col, slice) = value++;
      }
    }
  }

  const auto shifted = ksj::fft::fftshift(cube);
  for (std::size_t row = 0U; row < cube.dim0(); ++row) {
    for (std::size_t col = 0U; col < cube.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < cube.dim2(); ++slice) {
        EXPECT_EQ(cube((row + 2U) % cube.dim0(), (col + 1U) % cube.dim1(), (slice + 2U) % cube.dim2()),
                  shifted(row, col, slice));
      }
    }
  }

  auto raw_shifted = ksj::array::make_pooled_cube<int>(cube.dim0(), cube.dim1(), cube.dim2());
  ksj::array::copy(cube.view(), raw_shifted.view());
  ksj::fft::fftshift_in_place(raw_shifted.view());
  for (std::size_t row = 0U; row < cube.dim0(); ++row) {
    for (std::size_t col = 0U; col < cube.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < cube.dim2(); ++slice) {
        EXPECT_EQ(shifted(row, col, slice), raw_shifted(row, col, slice));
      }
    }
  }

  ksj::fft::ifftshift_in_place(raw_shifted.view());
  for (std::size_t row = 0U; row < cube.dim0(); ++row) {
    for (std::size_t col = 0U; col < cube.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < cube.dim2(); ++slice) {
        EXPECT_EQ(cube(row, col, slice), raw_shifted(row, col, slice));
      }
    }
  }
}

TEST(KSpaceJetFft, RejectsShiftOutputDimensionMismatch) {
  auto vector = ksj::array::make_pooled_vector<int>(4);
  auto short_vector = ksj::array::make_pooled_vector<int>(3);
  EXPECT_THROW(ksj::fft::fftshift(vector, short_vector), std::invalid_argument);
  EXPECT_THROW(ksj::fft::ifftshift(vector, short_vector), std::invalid_argument);
  auto vector_view = ksj::array::VectorView<int>(vector.data(), vector.size());
  auto short_vector_view = ksj::array::VectorView<int>(short_vector.data(), short_vector.size());
  EXPECT_THROW(ksj::fft::fftshift(vector_view, short_vector_view), std::invalid_argument);
  EXPECT_THROW(ksj::fft::ifftshift(vector_view, short_vector_view), std::invalid_argument);

  auto matrix = ksj::array::make_pooled_matrix<int>(2, 3);
  auto wrong_rows = ksj::array::make_pooled_matrix<int>(3, 3);
  auto wrong_cols = ksj::array::make_pooled_matrix<int>(2, 2);
  EXPECT_THROW(ksj::fft::fftshift(matrix, wrong_rows), std::invalid_argument);
  EXPECT_THROW(ksj::fft::ifftshift(matrix, wrong_cols), std::invalid_argument);
}

TEST(KSpaceJetFft, RejectsFftOutputDimensionMismatch) {
  auto vector = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  auto short_vector = ksj::array::make_pooled_vector<ksj::base::cf64>(3);
  EXPECT_THROW(ksj::fft::fft(vector, short_vector), std::invalid_argument);
  EXPECT_THROW(ksj::fft::ifft(vector, short_vector), std::invalid_argument);

  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  auto wrong_rows = ksj::array::make_pooled_matrix<ksj::base::cf64>(3, 3);
  auto wrong_cols = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  EXPECT_THROW(ksj::fft::fft2(matrix, wrong_rows), std::invalid_argument);
  EXPECT_THROW(ksj::fft::ifft2(matrix, wrong_cols), std::invalid_argument);
}

TEST(KSpaceJetFft, RejectsSegmentedFftDimensionMismatch) {
  auto vector = ksj::array::make_pooled_vector<ksj::base::cf64>(5);
  auto vector_output = ksj::array::make_pooled_vector<ksj::base::cf64>(5);
  auto short_vector = ksj::array::make_pooled_vector<ksj::base::cf64>(4);
  EXPECT_THROW(ksj::fft::fft_segmented(vector, vector_output, 0), std::invalid_argument);
  EXPECT_THROW(ksj::fft::fft_segmented(vector, vector_output, 2), std::invalid_argument);
  EXPECT_THROW(ksj::fft::fft_segmented(vector, short_vector, 1), std::invalid_argument);

  auto matrix = ksj::array::make_pooled_matrix<ksj::base::cf64>(3, 4);
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(3, 4);
  auto wrong_output = ksj::array::make_pooled_matrix<ksj::base::cf64>(4, 4);
  EXPECT_THROW(ksj::fft::fft_segmented(matrix, output, ksj::array::Dim::dim1, 0), std::invalid_argument);
  EXPECT_THROW(ksj::fft::fft_segmented(matrix, output, ksj::array::Dim::dim0, 2), std::invalid_argument);
  EXPECT_THROW(ksj::fft::fft_segmented(matrix, wrong_output, ksj::array::Dim::dim1, 1), std::invalid_argument);
}

TEST(KSpaceJetFft, Computes2dImpulseSpectrum) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};

  const auto output = ksj::fft::fft2(input);
  const auto view_output = ksj::fft::fft2(ksj::array::as_const_view(input.view()));

  ASSERT_EQ(2U, output.rows());
  ASSERT_EQ(3U, output.cols());
  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      EXPECT_NEAR(1.0, output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(0.0, output(row, col).imag(), 1.0e-12);
      EXPECT_NEAR(output(row, col).real(), view_output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(output(row, col).imag(), view_output(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, Writes2dFftIntoExistingStorage) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};

  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  ksj::fft::fft2(input, output);

  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      EXPECT_NEAR(1.0, output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(0.0, output(row, col).imag(), 1.0e-12);
    }
  }

  ksj::fft::ifft2(output, output);
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      EXPECT_NEAR(input(row, col).real(), output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(input(row, col).imag(), output(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, WritesMatrixFftAlongSelectedDim) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 4);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};
  input(1, 0) = {2.0, 0.0};
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 4);

  ksj::fft::fft(input, output, ksj::array::Dim::dim1);

  for (std::size_t col = 0U; col < output.cols(); ++col) {
    EXPECT_NEAR(1.0, output(0, col).real(), 1.0e-12);
    EXPECT_NEAR(0.0, output(0, col).imag(), 1.0e-12);
    EXPECT_NEAR(2.0, output(1, col).real(), 1.0e-12);
    EXPECT_NEAR(0.0, output(1, col).imag(), 1.0e-12);
  }

  ksj::fft::ifft(output, output, ksj::array::Dim::dim1);
  for (std::size_t col = 0U; col < input.cols(); ++col) {
    for (std::size_t row = 0U; row < input.rows(); ++row) {
      EXPECT_NEAR(input(row, col).real(), output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(input(row, col).imag(), output(row, col).imag(), 1.0e-12);
    }
  }

  const auto dim0_output = ksj::fft::fft(input, ksj::array::Dim::dim0);
  EXPECT_NEAR(3.0, dim0_output(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(-1.0, dim0_output(1, 0).real(), 1.0e-12);

  const auto view_output = ksj::fft::fft(ksj::array::as_const_view(input.view()), ksj::array::Dim::dim1);
  for (std::size_t col = 0U; col < view_output.cols(); ++col) {
    EXPECT_NEAR(1.0, view_output(0, col).real(), 1.0e-12);
    EXPECT_NEAR(2.0, view_output(1, col).real(), 1.0e-12);
  }
}

TEST(KSpaceJetFft, WritesCubeFftAlongEachSelectedDim) {
  constexpr std::array axes{
    ksj::array::Dim::dim0,
    ksj::array::Dim::dim1,
    ksj::array::Dim::dim2,
  };

  for (const auto axis : axes) {
    auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 4U);
    ksj::array::fill(input.view(), ksj::base::cf64{});
    input(1U, 1U, 1U) = {1.0, 0.0};
    auto spectrum = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 4U);

    ksj::fft::fft(input, spectrum, axis);

    const auto length = input.view().extent(axis);
    for (std::size_t dim0 = 0U; dim0 < input.dim0(); ++dim0) {
      for (std::size_t dim1 = 0U; dim1 < input.dim1(); ++dim1) {
        for (std::size_t dim2 = 0U; dim2 < input.dim2(); ++dim2) {
          const auto line_matches = (axis == ksj::array::Dim::dim0 && dim1 == 1U && dim2 == 1U) ||
                                    (axis == ksj::array::Dim::dim1 && dim0 == 1U && dim2 == 1U) ||
                                    (axis == ksj::array::Dim::dim2 && dim0 == 1U && dim1 == 1U);
          const auto transformed_index = axis == ksj::array::Dim::dim0   ? dim0
                                         : axis == ksj::array::Dim::dim1 ? dim1
                                                                         : dim2;
          const auto phase =
            -2.0 * std::numbers::pi_v<double> * static_cast<double>(transformed_index) / static_cast<double>(length);
          const auto expected = line_matches ? std::polar(1.0, phase) : ksj::base::cf64{};
          EXPECT_NEAR(expected.real(), spectrum(dim0, dim1, dim2).real(), 1.0e-12);
          EXPECT_NEAR(expected.imag(), spectrum(dim0, dim1, dim2).imag(), 1.0e-12);
        }
      }
    }

    const auto returned_spectrum = ksj::fft::fft(ksj::array::as_const_view(input.view()), axis);
    expect_cube_near(spectrum, returned_spectrum);

    auto restored = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 4U);
    ksj::fft::ifft(spectrum, restored, axis);
    expect_cube_near(input, restored);

    auto aliased = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 4U);
    ksj::array::copy(input.view(), aliased.view());
    ksj::fft::fft(aliased, aliased, axis);
    expect_cube_near(spectrum, aliased);
  }
}

TEST(KSpaceJetFft, CubeAxisInplaceAndExecutorMatchOutOfPlaceTransform) {
  constexpr std::array axes{
    ksj::array::Dim::dim0,
    ksj::array::Dim::dim1,
    ksj::array::Dim::dim2,
  };
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(3U, 4U, 5U);
  for (std::size_t dim0 = 0U; dim0 < input.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < input.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < input.dim2(); ++dim2) {
        input(dim0, dim1, dim2) = {
          static_cast<double>(dim0 * 11U + dim1 * 7U + dim2 * 3U) / 13.0,
          static_cast<double>(dim0 * 5U + dim1 * 17U + dim2 * 19U) / 23.0,
        };
      }
    }
  }

  ksj::fft::Fft1Executor<double> executor;
  for (const auto axis : axes) {
    auto expected = ksj::array::make_pooled_cube<ksj::base::cf64>(input.dim0(), input.dim1(), input.dim2());
    ksj::fft::fft(input, expected, axis, ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal);

    auto inplace = ksj::array::make_pooled_cube<ksj::base::cf64>(input.dim0(), input.dim1(), input.dim2());
    ksj::array::copy(input.view(), inplace.view());
    ksj::fft::fft_inplace(inplace.view(), axis, ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal);
    expect_cube_near(expected, inplace, 1.0e-10);

    auto executor_output = ksj::array::make_pooled_cube<ksj::base::cf64>(input.dim0(), input.dim1(), input.dim2());
    ksj::array::copy(input.view(), executor_output.view());
    executor.execute_inplace(executor_output.view(), axis, ksj::fft::Direction::forward,
                             ksj::fft::Normalization::orthonormal);
    expect_cube_near(expected, executor_output, 1.0e-10);

    ksj::fft::ifft_inplace(inplace.view(), axis, ksj::fft::Normalization::orthonormal);
    expect_cube_near(input, inplace, 1.0e-10);
  }

  EXPECT_THROW(ksj::fft::fft_inplace(input.view(), ksj::array::Dim::dim3), std::invalid_argument);
}

TEST(KSpaceJetFft, Fft2ExecutorMatchesRepeatedRowMajorIntelFftExactly) {
  constexpr std::size_t kRows = 32U;
  constexpr std::size_t kCols = 32U;
  auto input_storage = ksj::array::make_pooled_vector<ksj::base::cf32>(kRows * kCols);
  auto expected_storage = ksj::array::make_pooled_vector<ksj::base::cf32>(kRows * kCols);
  auto actual_storage = ksj::array::make_pooled_vector<ksj::base::cf32>(kRows * kCols);
  auto input = ksj::array::MatrixView<ksj::base::cf32>(input_storage.data(), kRows, kCols);
  auto expected = ksj::array::MatrixView<ksj::base::cf32>(expected_storage.data(), kRows, kCols);
  auto actual = ksj::array::MatrixView<ksj::base::cf32>(actual_storage.data(), kRows, kCols);
  ksj::fft::Fft2Executor<float> executor;

  for (std::size_t trial = 0U; trial < 3U; ++trial) {
    for (std::size_t row = 0U; row < kRows; ++row) {
      for (std::size_t col = 0U; col < kCols; ++col) {
        input(row, col) = {static_cast<float>((row * 17U + col * 11U + trial) % 97U) / 31.0F,
                           static_cast<float>((row * 7U + col * 19U + trial) % 89U) / 43.0F};
      }
    }
    ksj::fft::fft2(input, expected, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
    executor.execute(input, actual, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
    for (std::size_t index = 0U; index < expected_storage.size(); ++index) {
      EXPECT_EQ(expected_storage(index), actual_storage(index));
    }
  }
}

TEST(KSpaceJetFft, Fft2ExecutorInplaceMatchesFft2Inplace) {
  constexpr std::size_t kRows = 16U;
  constexpr std::size_t kCols = 20U;
  auto expected = ksj::array::make_pooled_matrix<ksj::base::cf32>(kRows, kCols);
  auto actual = ksj::array::make_pooled_matrix<ksj::base::cf32>(kRows, kCols);
  ksj::fft::Fft2Executor<float> executor;

  for (std::size_t trial = 0U; trial < 3U; ++trial) {
    for (std::size_t row = 0U; row < kRows; ++row) {
      for (std::size_t col = 0U; col < kCols; ++col) {
        const auto value = ksj::base::cf32{static_cast<float>((row * 13U + col * 5U + trial) % 83U) / 19.0F,
                                           static_cast<float>((row * 3U + col * 17U + trial) % 71U) / 23.0F};
        expected(row, col) = value;
        actual(row, col) = value;
      }
    }

    ksj::fft::fft2_inplace(expected.view(), ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal);
    executor.execute_inplace(actual.view(), ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal);
    for (std::size_t row = 0U; row < kRows; ++row) {
      for (std::size_t col = 0U; col < kCols; ++col) {
        EXPECT_NEAR(expected(row, col).real(), actual(row, col).real(), 1.0e-5F);
        EXPECT_NEAR(expected(row, col).imag(), actual(row, col).imag(), 1.0e-5F);
      }
    }
  }
}

TEST(KSpaceJetFft, CenteredFft2ExecutorMatchesShiftedFft2InplaceForRowMajorAndStridedViews) {
  struct Shape {
    std::size_t rows;
    std::size_t cols;
    std::size_t physical_cols;
  };

  constexpr std::array<Shape, 2U> kShapes{{{5U, 7U, 7U}, {6U, 8U, 11U}}};
  constexpr std::array<bool, 2U> kShiftFlags{false, true};
  constexpr std::array<ksj::fft::Direction, 2U> kDirections{ksj::fft::Direction::forward, ksj::fft::Direction::inverse};
  ksj::fft::CenteredFft2Executor<float> executor;

  for (const auto shape : kShapes) {
    for (const auto direction : kDirections) {
      for (const auto preshift : kShiftFlags) {
        for (const auto postshift : kShiftFlags) {
          auto expected_storage = ksj::array::make_pooled_vector<ksj::base::cf32>(shape.rows * shape.physical_cols);
          auto actual_storage = ksj::array::make_pooled_vector<ksj::base::cf32>(shape.rows * shape.physical_cols);
          auto expected_full =
            ksj::array::MatrixView<ksj::base::cf32>(expected_storage.data(), shape.rows, shape.physical_cols);
          auto actual_full =
            ksj::array::MatrixView<ksj::base::cf32>(actual_storage.data(), shape.rows, shape.physical_cols);
          auto expected = expected_full.subview(ksj::array::_, ksj::array::slice(0U, shape.cols));
          auto actual = actual_full.subview(ksj::array::_, ksj::array::slice(0U, shape.cols));

          for (std::size_t index = 0U; index < expected_storage.size(); ++index) {
            expected_storage(index) = {-91.0F, 13.0F};
            actual_storage(index) = expected_storage(index);
          }
          for (std::size_t row = 0U; row < shape.rows; ++row) {
            for (std::size_t col = 0U; col < shape.cols; ++col) {
              const auto value = ksj::base::cf32{static_cast<float>((row * 17U + col * 11U + 3U) % 97U) / 29.0F,
                                                 static_cast<float>((row * 5U + col * 19U + 7U) % 89U) / 31.0F};
              expected(row, col) = value;
              actual(row, col) = value;
            }
          }

          ksj::fft::fft2_inplace(expected, direction, ksj::fft::Normalization::orthonormal, preshift, postshift);
          executor.execute_inplace(actual, direction, preshift, postshift);
          for (std::size_t row = 0U; row < shape.rows; ++row) {
            for (std::size_t col = 0U; col < shape.cols; ++col) {
              EXPECT_NEAR(expected(row, col).real(), actual(row, col).real(), 1.0e-5F);
              EXPECT_NEAR(expected(row, col).imag(), actual(row, col).imag(), 1.0e-5F);
            }
            for (std::size_t col = shape.cols; col < shape.physical_cols; ++col) {
              EXPECT_EQ(expected_full(row, col), actual_full(row, col));
            }
          }
        }
      }
    }
  }
}

TEST(KSpaceJetFft, WritesShifted2dFftIntoStridedMatrixView) {
  constexpr std::size_t rows = 2;
  constexpr std::size_t cols = 3;
  constexpr std::size_t physical_cols = cols + 2U;
  std::vector<ksj::base::cf64> storage(rows * physical_cols);
  auto view = ksj::array::MatrixView<ksj::base::cf64>(storage.data(), rows, physical_cols)
                .subview(ksj::array::_, ksj::array::slice(0U, cols));
  auto reference_input = ksj::array::make_pooled_matrix<ksj::base::cf64>(rows, cols);
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      const auto value = ksj::base::cf64{static_cast<double>(row * 10U + col), static_cast<double>(col)};
      view(row, col) = value;
      reference_input(row, col) = value;
    }
  }

  ksj::fft::ifftshift(reference_input, reference_input);
  auto expected = ksj::array::make_pooled_matrix<ksj::base::cf64>(rows, cols);
  ksj::fft::fft2(reference_input, expected, ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal);
  ksj::fft::fftshift(expected, expected);

  ksj::fft::fft2_inplace(view, ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal, true, true);

  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      EXPECT_NEAR(expected(row, col).real(), view(row, col).real(), 1.0e-12);
      EXPECT_NEAR(expected(row, col).imag(), view(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, Writes2dSegmentedFftAcrossDim1Segments) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 4);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};
  input(0, 2) = {2.0, 0.0};
  input(1, 0) = {3.0, 0.0};
  input(1, 2) = {4.0, 0.0};
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 4);

  ksj::fft::fft_segmented(input, output, ksj::array::Dim::dim1, 2);

  EXPECT_NEAR(1.0, output(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(1.0, output(0, 1).real(), 1.0e-12);
  EXPECT_NEAR(2.0, output(0, 2).real(), 1.0e-12);
  EXPECT_NEAR(2.0, output(0, 3).real(), 1.0e-12);
  EXPECT_NEAR(3.0, output(1, 0).real(), 1.0e-12);
  EXPECT_NEAR(4.0, output(1, 2).real(), 1.0e-12);

  ksj::fft::ifft_segmented(output, output, ksj::array::Dim::dim1, 2);
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      EXPECT_NEAR(input(row, col).real(), output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(input(row, col).imag(), output(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, Writes2dSegmentedFftAcrossDim0Segments) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(4, 2);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};
  input(2, 0) = {2.0, 0.0};
  input(0, 1) = {3.0, 0.0};
  input(2, 1) = {4.0, 0.0};

  const auto output = ksj::fft::fft_segmented(input, ksj::array::Dim::dim0, 2);

  EXPECT_NEAR(1.0, output(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(1.0, output(1, 0).real(), 1.0e-12);
  EXPECT_NEAR(2.0, output(2, 0).real(), 1.0e-12);
  EXPECT_NEAR(2.0, output(3, 0).real(), 1.0e-12);
  EXPECT_NEAR(3.0, output(0, 1).real(), 1.0e-12);
  EXPECT_NEAR(4.0, output(2, 1).real(), 1.0e-12);
}

TEST(KSpaceJetFft, Shifted2dSegmentedFftRoundTripsAcrossDim1Segments) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 6);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      input(row, col) = {static_cast<double>(row * input.cols() + col + 1U), static_cast<double>(row) - 0.5};
    }
  }
  auto spectrum = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 6);
  auto restored = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 6);

  ksj::fft::fft_segmented(input, spectrum, ksj::array::Dim::dim1, 2, ksj::fft::Direction::forward,
                          ksj::fft::Normalization::orthonormal, true, true);
  ksj::fft::fft_segmented(spectrum, restored, ksj::array::Dim::dim1, 2, ksj::fft::Direction::inverse,
                          ksj::fft::Normalization::orthonormal, true, true);

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      EXPECT_NEAR(input(row, col).real(), restored(row, col).real(), 1.0e-12);
      EXPECT_NEAR(input(row, col).imag(), restored(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, Writes2dFft) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);

  ksj::fft::fft2(input, output);

  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      EXPECT_NEAR(1.0, output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(0.0, output(row, col).imag(), 1.0e-12);
    }
  }

  ksj::fft::ifft2(output, output);
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      EXPECT_NEAR(input(row, col).real(), output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(input(row, col).imag(), output(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, WritesBatched2dFftAcrossCubeSlices) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);
  as_eigen(input).setZero();
  input(0, 0, 0) = {1.0, 0.0};
  input(0, 0, 1) = {2.0, 0.0};
  auto output = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);

  ksj::fft::fft2_batch(input, output);
  const auto public_output = ksj::fft::fft2_batch(input);

  for (std::size_t slice = 0; slice < output.dim2(); ++slice) {
    const double expected = static_cast<double>(slice + 1U);
    for (std::size_t col = 0; col < output.dim1(); ++col) {
      for (std::size_t row = 0; row < output.dim0(); ++row) {
        EXPECT_NEAR(expected, output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(0.0, output(row, col, slice).imag(), 1.0e-12);
        EXPECT_NEAR(output(row, col, slice).real(), public_output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(output(row, col, slice).imag(), public_output(row, col, slice).imag(), 1.0e-12);
      }
    }
  }

  ksj::fft::ifft2_batch(output, output);
  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        EXPECT_NEAR(input(row, col, slice).real(), output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(input(row, col, slice).imag(), output(row, col, slice).imag(), 1.0e-12);
      }
    }
  }
}

TEST(KSpaceJetFft, Computes3dImpulseSpectrum) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 3, 2);
  as_eigen(input).setZero();
  input(0, 0, 0) = {1.0, 0.0};

  const auto output = ksj::fft::fft3(input);
  const auto view_output = ksj::fft::fft3(ksj::array::as_const_view(input.view()));

  ASSERT_EQ(2U, output.dim0());
  ASSERT_EQ(3U, output.dim1());
  ASSERT_EQ(2U, output.dim2());
  for (std::size_t slice = 0; slice < output.dim2(); ++slice) {
    for (std::size_t col = 0; col < output.dim1(); ++col) {
      for (std::size_t row = 0; row < output.dim0(); ++row) {
        EXPECT_NEAR(1.0, output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(0.0, output(row, col, slice).imag(), 1.0e-12);
        EXPECT_NEAR(output(row, col, slice).real(), view_output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(output(row, col, slice).imag(), view_output(row, col, slice).imag(), 1.0e-12);
      }
    }
  }
}

TEST(KSpaceJetFft, ReturnsCentered3dFftFromViewInput) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);
  input(0, 0, 0) = {1.0, 0.0};
  input(1, 0, 0) = {2.0, 0.5};
  input(0, 1, 0) = {3.0, -1.0};
  input(1, 1, 0) = {4.0, 2.0};
  input(0, 0, 1) = {5.0, -0.5};
  input(1, 0, 1) = {6.0, 1.5};
  input(0, 1, 1) = {7.0, -2.0};
  input(1, 1, 1) = {8.0, 3.0};

  const auto spectrum = ksj::fft::fft3_centered(ksj::array::as_const_view(input.view()), ksj::fft::Direction::forward,
                                                ksj::fft::Normalization::orthonormal);
  const auto restored =
    ksj::fft::ifft3_centered(ksj::array::as_const_view(spectrum.view()), ksj::fft::Normalization::orthonormal);

  ASSERT_EQ(input.dim0(), restored.dim0());
  ASSERT_EQ(input.dim1(), restored.dim1());
  ASSERT_EQ(input.dim2(), restored.dim2());
  for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0U; col < input.dim1(); ++col) {
      for (std::size_t row = 0U; row < input.dim0(); ++row) {
        EXPECT_NEAR(input(row, col, slice).real(), restored(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(input(row, col, slice).imag(), restored(row, col, slice).imag(), 1.0e-12);
      }
    }
  }
}

TEST(KSpaceJetFft, Writes3dFftWithPlan) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);
  input(0, 0, 0) = {1.0, 0.0};
  input(1, 0, 0) = {2.0, 0.5};
  input(0, 1, 0) = {3.0, -1.0};
  input(1, 1, 0) = {4.0, 2.0};
  input(0, 0, 1) = {5.0, -0.5};
  input(1, 0, 1) = {6.0, 1.5};
  input(0, 1, 1) = {7.0, -2.0};
  input(1, 1, 1) = {8.0, 3.0};
  auto output = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);

  ksj::fft::fft3(input, output);

  ksj::fft::ifft3(output, output);
  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        EXPECT_NEAR(input(row, col, slice).real(), output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(input(row, col, slice).imag(), output(row, col, slice).imag(), 1.0e-12);
      }
    }
  }

  ksj::fft::Fft3Plan<double> plan(2, 2, 2);
  plan.execute(input, output);
  EXPECT_EQ(2U, plan.rows());
  EXPECT_EQ(2U, plan.cols());
  EXPECT_EQ(2U, plan.slices());
  EXPECT_TRUE(plan.has_cached_descriptor());

  auto wrong_shape = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 3);
  EXPECT_THROW(plan.execute(wrong_shape, output), std::invalid_argument);
}

TEST(KSpaceJetFft, Writes3dFftThroughCubeViews) {
  auto input_storage = ksj::array::make_pooled_vector<ksj::base::cf64>(12);
  auto output_storage = ksj::array::make_pooled_vector<ksj::base::cf64>(12);
  auto input_view = ksj::array::CubeView<ksj::base::cf64>(input_storage.data(), 2U, 3U, 2U);
  auto output_view = ksj::array::CubeView<ksj::base::cf64>(output_storage.data(), 2U, 3U, 2U);
  auto reference_input = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 2U);

  for (std::size_t row = 0U; row < input_view.dim0(); ++row) {
    for (std::size_t col = 0U; col < input_view.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input_view.dim2(); ++slice) {
        const auto value =
          ksj::base::cf64{static_cast<double>(1U + row + 3U * col + 7U * slice), static_cast<double>(row) - 0.5};
        input_view(row, col, slice) = value;
        reference_input(row, col, slice) = value;
      }
    }
  }

  auto reference = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 2U);
  ksj::fft::fft3(reference_input, reference);
  ksj::fft::fft3(ksj::array::as_const_view(input_view), output_view);

  for (std::size_t row = 0U; row < output_view.dim0(); ++row) {
    for (std::size_t col = 0U; col < output_view.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < output_view.dim2(); ++slice) {
        EXPECT_NEAR(reference(row, col, slice).real(), output_view(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(reference(row, col, slice).imag(), output_view(row, col, slice).imag(), 1.0e-12);
      }
    }
  }

  ksj::fft::ifft3(ksj::array::as_const_view(output_view), output_view);
  for (std::size_t row = 0U; row < input_view.dim0(); ++row) {
    for (std::size_t col = 0U; col < input_view.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < input_view.dim2(); ++slice) {
        EXPECT_NEAR(input_view(row, col, slice).real(), output_view(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(input_view(row, col, slice).imag(), output_view(row, col, slice).imag(), 1.0e-12);
      }
    }
  }
}

TEST(KSpaceJetFft, Writes3dFftThroughRowMajorBuffers) {
  auto input_storage = ksj::array::make_pooled_vector<ksj::base::cf64>(12);
  auto output_storage = ksj::array::make_pooled_vector<ksj::base::cf64>(12);
  auto reference_input = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 2U);

  for (std::size_t row = 0U; row < reference_input.dim0(); ++row) {
    for (std::size_t col = 0U; col < reference_input.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < reference_input.dim2(); ++slice) {
        const auto index = (row * reference_input.dim1() + col) * reference_input.dim2() + slice;
        const auto value =
          ksj::base::cf64{static_cast<double>(2U + row * 5U + col * 3U + slice), static_cast<double>(slice) - 0.25};
        input_storage(index) = value;
        reference_input(row, col, slice) = value;
      }
    }
  }

  auto reference = ksj::array::make_pooled_cube<ksj::base::cf64>(2U, 3U, 2U);
  ksj::fft::fft3(reference_input, reference);
  auto input_view = ksj::array::cube_view(input_storage.data(), 2U, 3U, 2U);
  auto output_view = ksj::array::cube_view(output_storage.data(), 2U, 3U, 2U);
  ksj::fft::fft3(ksj::array::as_const_view(input_view), output_view);

  for (std::size_t row = 0U; row < reference.dim0(); ++row) {
    for (std::size_t col = 0U; col < reference.dim1(); ++col) {
      for (std::size_t slice = 0U; slice < reference.dim2(); ++slice) {
        const auto index = (row * reference.dim1() + col) * reference.dim2() + slice;
        EXPECT_NEAR(reference(row, col, slice).real(), output_storage(index).real(), 1.0e-12);
        EXPECT_NEAR(reference(row, col, slice).imag(), output_storage(index).imag(), 1.0e-12);
      }
    }
  }
}

TEST(KSpaceJetFft, WritesBatched3dFftAcrossArray4dVolumes) {
  auto input = ksj::array::make_pooled_array4d<ksj::base::cf64>(2, 2, 2, 2);
  as_eigen(input).setZero();
  input(0, 0, 0, 0) = {1.0, 0.0};
  input(0, 0, 0, 1) = {2.0, 0.0};

  auto output = ksj::array::make_pooled_array4d<ksj::base::cf64>(2, 2, 2, 2);
  ksj::fft::fft3_batch(input, output);
  const auto public_output = ksj::fft::fft3_batch(input);

  for (std::size_t batch = 0; batch < output.dim3(); ++batch) {
    const double expected = static_cast<double>(batch + 1U);
    for (std::size_t slice = 0; slice < output.dim2(); ++slice) {
      for (std::size_t col = 0; col < output.dim1(); ++col) {
        for (std::size_t row = 0; row < output.dim0(); ++row) {
          EXPECT_NEAR(expected, output(row, col, slice, batch).real(), 1.0e-12);
          EXPECT_NEAR(0.0, output(row, col, slice, batch).imag(), 1.0e-12);
          EXPECT_NEAR(output(row, col, slice, batch).real(), public_output(row, col, slice, batch).real(), 1.0e-12);
          EXPECT_NEAR(output(row, col, slice, batch).imag(), public_output(row, col, slice, batch).imag(), 1.0e-12);
        }
      }
    }
  }

  ksj::fft::ifft3_batch(output, output);
  for (std::size_t batch = 0; batch < input.dim3(); ++batch) {
    for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
      for (std::size_t col = 0; col < input.dim1(); ++col) {
        for (std::size_t row = 0; row < input.dim0(); ++row) {
          EXPECT_NEAR(input(row, col, slice, batch).real(), output(row, col, slice, batch).real(), 1.0e-12);
          EXPECT_NEAR(input(row, col, slice, batch).imag(), output(row, col, slice, batch).imag(), 1.0e-12);
        }
      }
    }
  }

  ksj::fft::Fft3Plan<double> plan(2, 2, 2);
  plan.execute_batch(input, output);
  EXPECT_TRUE(plan.has_cached_descriptor());
  for (std::size_t batch = 0; batch < output.dim3(); ++batch) {
    const double expected = static_cast<double>(batch + 1U);
    for (std::size_t slice = 0; slice < output.dim2(); ++slice) {
      for (std::size_t col = 0; col < output.dim1(); ++col) {
        for (std::size_t row = 0; row < output.dim0(); ++row) {
          EXPECT_NEAR(expected, output(row, col, slice, batch).real(), 1.0e-12);
          EXPECT_NEAR(0.0, output(row, col, slice, batch).imag(), 1.0e-12);
        }
      }
    }
  }

  auto wrong_shape = ksj::array::make_pooled_array4d<ksj::base::cf64>(2, 2, 3, 2);
  EXPECT_THROW(plan.execute_batch(wrong_shape, output), std::invalid_argument);
  auto wrong_batch = ksj::array::make_pooled_array4d<ksj::base::cf64>(2, 2, 2, 3);
  EXPECT_THROW(plan.execute_batch(input, wrong_batch), std::invalid_argument);
}

TEST(KSpaceJetFft, WritesVolumeBatch3dFftThroughBatchFirstArray4dViews) {
  auto input = ksj::array::make_pooled_array4d<ksj::base::cf64>(2U, 2U, 3U, 2U);
  auto output = ksj::array::make_pooled_array4d<ksj::base::cf64>(2U, 2U, 3U, 2U);

  for (std::size_t batch = 0U; batch < input.dim0(); ++batch) {
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          input(batch, row, col, slice) =
            ksj::base::cf64{static_cast<double>(1U + batch * 17U + row * 5U + col * 3U + slice),
                            static_cast<double>(batch + slice) * -0.25};
        }
      }
    }
  }

  ksj::fft::fft3_volume_batch(ksj::array::as_const_view(input.view()), output.view());
  const auto public_output = ksj::fft::fft3_volume_batch(ksj::array::as_const_view(input.view()));

  for (std::size_t batch = 0U; batch < input.dim0(); ++batch) {
    auto reference_input = ksj::array::make_pooled_cube<ksj::base::cf64>(input.dim1(), input.dim2(), input.dim3());
    auto reference_output = ksj::array::make_pooled_cube<ksj::base::cf64>(input.dim1(), input.dim2(), input.dim3());
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          reference_input(row, col, slice) = input(batch, row, col, slice);
        }
      }
    }

    ksj::fft::fft3(reference_input, reference_output);
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          EXPECT_NEAR(reference_output(row, col, slice).real(), output(batch, row, col, slice).real(), 1.0e-12);
          EXPECT_NEAR(reference_output(row, col, slice).imag(), output(batch, row, col, slice).imag(), 1.0e-12);
          EXPECT_NEAR(output(batch, row, col, slice).real(), public_output(batch, row, col, slice).real(), 1.0e-12);
          EXPECT_NEAR(output(batch, row, col, slice).imag(), public_output(batch, row, col, slice).imag(), 1.0e-12);
        }
      }
    }
  }
}

TEST(KSpaceJetFft, CenteredVolumeBatch3dFftRestoresBatchFirstArray4dViews) {
  auto input = ksj::array::make_pooled_array4d<ksj::base::cf64>(2U, 2U, 4U, 2U);
  auto original = ksj::array::make_pooled_array4d<ksj::base::cf64>(2U, 2U, 4U, 2U);
  auto kspace = ksj::array::make_pooled_array4d<ksj::base::cf64>(2U, 2U, 4U, 2U);
  auto restored = ksj::array::make_pooled_array4d<ksj::base::cf64>(2U, 2U, 4U, 2U);

  for (std::size_t batch = 0U; batch < input.dim0(); ++batch) {
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          const auto value = ksj::base::cf64{static_cast<double>(2U + batch * 11U + row * 7U + col * 5U + slice * 3U),
                                             static_cast<double>(row + col + slice) * 0.125};
          input(batch, row, col, slice) = value;
          original(batch, row, col, slice) = value;
        }
      }
    }
  }

  ksj::fft::fft3_centered_volume_batch_in_place_input(input.view(), kspace.view(), ksj::fft::Direction::forward,
                                                      ksj::fft::Normalization::none);
  ksj::fft::ifft3_centered_volume_batch_in_place_input(kspace.view(), restored.view(),
                                                       ksj::fft::Normalization::inverse);

  for (std::size_t batch = 0U; batch < input.dim0(); ++batch) {
    for (std::size_t row = 0U; row < input.dim1(); ++row) {
      for (std::size_t col = 0U; col < input.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < input.dim3(); ++slice) {
          EXPECT_NEAR(original(batch, row, col, slice).real(), restored(batch, row, col, slice).real(), 1.0e-12);
          EXPECT_NEAR(original(batch, row, col, slice).imag(), restored(batch, row, col, slice).imag(), 1.0e-12);
        }
      }
    }
  }
}

TEST(KSpaceJetFft, Fft3PlanDescriptorMatchesReferenceForRowMajorCube) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 3, 4);
  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        input(row, col, slice) = {static_cast<double>((row * 7U + col * 11U + slice * 13U) % 19U) * 0.25,
                                  static_cast<double>((row * 5U + col * 3U + slice * 17U) % 23U) * -0.125};
      }
    }
  }

  auto reference = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 3, 4);
  auto planned = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 3, 4);
  ksj::fft::fft3(input, reference);
  ksj::fft::Fft3Plan<double> plan(2, 3, 4);
  plan.execute(input, planned);

  EXPECT_TRUE(plan.has_cached_descriptor());
  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        EXPECT_NEAR(reference(row, col, slice).real(), planned(row, col, slice).real(), 1.0e-10);
        EXPECT_NEAR(reference(row, col, slice).imag(), planned(row, col, slice).imag(), 1.0e-10);
      }
    }
  }

  ksj::fft::Fft3Plan<double> inverse_plan(2, 3, 4, ksj::fft::Direction::inverse, ksj::fft::Normalization::inverse);
  inverse_plan.execute(planned, planned);
  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        EXPECT_NEAR(input(row, col, slice).real(), planned(row, col, slice).real(), 1.0e-10);
        EXPECT_NEAR(input(row, col, slice).imag(), planned(row, col, slice).imag(), 1.0e-10);
      }
    }
  }
}

TEST(KSpaceJetFft, Default3dPolicyMatchesReference) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(4, 4, 4);
  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        input(row, col, slice) = {static_cast<double>((row * 7U + col * 11U + slice * 13U) % 19U) * 0.25,
                                  static_cast<double>((row * 5U + col * 3U + slice * 17U) % 23U) * -0.125};
      }
    }
  }

  auto reference = ksj::array::make_pooled_cube<ksj::base::cf64>(4, 4, 4);
  auto output = ksj::array::make_pooled_cube<ksj::base::cf64>(4, 4, 4);
  ksj::fft::fft3(input, reference);
  ksj::fft::fft3(input, output);

  for (std::size_t slice = 0; slice < input.dim2(); ++slice) {
    for (std::size_t col = 0; col < input.dim1(); ++col) {
      for (std::size_t row = 0; row < input.dim0(); ++row) {
        EXPECT_NEAR(reference(row, col, slice).real(), output(row, col, slice).real(), 1.0e-10);
        EXPECT_NEAR(reference(row, col, slice).imag(), output(row, col, slice).imag(), 1.0e-10);
      }
    }
  }
}

TEST(KSpaceJetFft, Uses2dPlanForCachedDimensions) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  as_eigen(input).setZero();
  input(0, 0) = {1.0, 0.0};
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  ksj::fft::Fft2Plan<double> plan(2, 2);

  plan.execute(input, output);

  EXPECT_EQ(2U, plan.rows());
  EXPECT_EQ(2U, plan.cols());
  EXPECT_TRUE(plan.has_cached_descriptor());
  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      EXPECT_NEAR(1.0, output(row, col).real(), 1.0e-12);
      EXPECT_NEAR(0.0, output(row, col).imag(), 1.0e-12);
    }
  }

  auto wrong_shape = ksj::array::make_pooled_matrix<ksj::base::cf64>(3, 2);
  EXPECT_THROW(plan.execute(wrong_shape, output), std::invalid_argument);
}

TEST(KSpaceJetFft, Fft2PlanExecutesRowMajorCubeSlices) {
  auto input = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);
  as_eigen(input).setZero();
  input(0, 0, 0) = {1.0, 0.0};
  input(0, 0, 1) = {2.0, 0.0};
  auto output = ksj::array::make_pooled_cube<ksj::base::cf64>(2, 2, 2);
  ksj::fft::Fft2Plan<double> plan(2, 2);

  plan.execute_batch(input, output);

  EXPECT_TRUE(plan.has_cached_descriptor());
  for (std::size_t slice = 0; slice < output.dim2(); ++slice) {
    const double expected = static_cast<double>(slice + 1U);
    for (std::size_t col = 0; col < output.dim1(); ++col) {
      for (std::size_t row = 0; row < output.dim0(); ++row) {
        EXPECT_NEAR(expected, output(row, col, slice).real(), 1.0e-12);
        EXPECT_NEAR(0.0, output(row, col, slice).imag(), 1.0e-12);
      }
    }
  }
}

TEST(KSpaceJetFft, Fft2PlanDescriptorMatchesReferenceForRowMajorMatrix) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  input(0, 0) = {1.0, 0.25};
  input(1, 0) = {2.0, -0.5};
  input(0, 1) = {3.0, 1.25};
  input(1, 1) = {4.0, -1.5};
  input(0, 2) = {5.0, 2.25};
  input(1, 2) = {6.0, -2.5};
  auto reference = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);
  auto planned = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 3);

  ksj::fft::fft2(input, reference);
  ksj::fft::Fft2Plan<double> plan(2, 3);
  plan.execute(input, planned);

  EXPECT_TRUE(plan.has_cached_descriptor());
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      EXPECT_NEAR(reference(row, col).real(), planned(row, col).real(), 1.0e-12);
      EXPECT_NEAR(reference(row, col).imag(), planned(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetFft, Default2dPolicyMatchesReference) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(16, 16);
  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      input(row, col) = {static_cast<double>((row * 7U + col * 11U) % 17U) * 0.25,
                         static_cast<double>((row * 5U + col * 13U) % 19U) * -0.125};
    }
  }
  auto reference = ksj::array::make_pooled_matrix<ksj::base::cf64>(16, 16);
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf64>(16, 16);

  ksj::fft::fft2(input, reference);
  ksj::fft::fft2(input, output);

  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      EXPECT_NEAR(reference(row, col).real(), output(row, col).real(), 1.0e-10);
      EXPECT_NEAR(reference(row, col).imag(), output(row, col).imag(), 1.0e-10);
    }
  }
}

TEST(KSpaceJetFft, ComputesFftConvolve2dFull) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  input(0, 0) = {1.0, 0.0};
  input(0, 1) = {2.0, 0.0};
  input(1, 0) = {3.0, 0.0};
  input(1, 1) = {4.0, 0.0};

  auto kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  kernel(0, 0) = {1.0, 0.0};
  kernel(0, 1) = {2.0, 0.0};
  kernel(1, 0) = {3.0, 0.0};
  kernel(1, 1) = {4.0, 0.0};

  auto output = ksj::fft::convolve2d_full_fft(input, kernel);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(3U, output.cols());
  const double expected[3][3] = {{1.0, 4.0, 4.0}, {6.0, 20.0, 16.0}, {9.0, 24.0, 16.0}};
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      EXPECT_NEAR(expected[row][col], output(row, col).real(), 1.0e-10);
      EXPECT_NEAR(0.0, output(row, col).imag(), 1.0e-10);
    }
  }
}

TEST(KSpaceJetFft, ComputesFftConvolve2dSameAndValidCrops) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  input(0, 0) = {1.0, 0.0};
  input(0, 1) = {2.0, 0.0};
  input(1, 0) = {3.0, 0.0};
  input(1, 1) = {4.0, 0.0};

  auto kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  kernel(0, 0) = {1.0, 0.0};
  kernel(0, 1) = {2.0, 0.0};
  kernel(1, 0) = {3.0, 0.0};
  kernel(1, 1) = {4.0, 0.0};

  const auto same = ksj::fft::convolve2d_same_fft(input, kernel);
  ASSERT_EQ(2U, same.rows());
  ASSERT_EQ(2U, same.cols());
  const double expected_same[2][2] = {{1.0, 4.0}, {6.0, 20.0}};
  for (std::size_t row = 0; row < same.rows(); ++row) {
    for (std::size_t col = 0; col < same.cols(); ++col) {
      EXPECT_NEAR(expected_same[row][col], same(row, col).real(), 1.0e-10);
      EXPECT_NEAR(0.0, same(row, col).imag(), 1.0e-10);
    }
  }

  const auto valid = ksj::fft::convolve2d_valid_fft(input, kernel);
  ASSERT_EQ(1U, valid.rows());
  ASSERT_EQ(1U, valid.cols());
  EXPECT_NEAR(20.0, valid(0, 0).real(), 1.0e-10);
  EXPECT_NEAR(0.0, valid(0, 0).imag(), 1.0e-10);
}

TEST(KSpaceJetFft, ComputesFftCorrelate2dFullWithConjugatedKernel) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  input(0, 0) = {1.0, 0.0};
  input(0, 1) = {2.0, 0.0};
  input(1, 0) = {3.0, 0.0};
  input(1, 1) = {4.0, 0.0};

  auto kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  kernel(0, 0) = {1.0, 0.0};
  kernel(0, 1) = {2.0, 0.0};
  kernel(1, 0) = {3.0, 0.0};
  kernel(1, 1) = {4.0, 0.0};

  auto output = ksj::fft::correlate2d_full_fft(input, kernel);

  ASSERT_EQ(3U, output.rows());
  ASSERT_EQ(3U, output.cols());
  const double expected[3][3] = {{4.0, 11.0, 6.0}, {14.0, 30.0, 14.0}, {6.0, 11.0, 4.0}};
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      EXPECT_NEAR(expected[row][col], output(row, col).real(), 1.0e-10);
      EXPECT_NEAR(0.0, output(row, col).imag(), 1.0e-10);
    }
  }

  auto scalar_input = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 1);
  auto scalar_kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 1);
  scalar_input(0, 0) = {1.0, 0.0};
  scalar_kernel(0, 0) = {2.0, 3.0};
  const auto scalar_output = ksj::fft::correlate2d_full_fft(scalar_input, scalar_kernel);
  EXPECT_NEAR(2.0, scalar_output(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(-3.0, scalar_output(0, 0).imag(), 1.0e-12);
}

TEST(KSpaceJetFft, ComputesFftCorrelate2dSameAndValidCrops) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  input(0, 0) = {1.0, 0.0};
  input(0, 1) = {2.0, 0.0};
  input(1, 0) = {3.0, 0.0};
  input(1, 1) = {4.0, 0.0};

  auto kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  kernel(0, 0) = {1.0, 0.0};
  kernel(0, 1) = {2.0, 0.0};
  kernel(1, 0) = {3.0, 0.0};
  kernel(1, 1) = {4.0, 0.0};

  const auto same = ksj::fft::correlate2d_same_fft(input, kernel);
  ASSERT_EQ(2U, same.rows());
  ASSERT_EQ(2U, same.cols());
  const double expected_same[2][2] = {{4.0, 11.0}, {14.0, 30.0}};
  for (std::size_t row = 0; row < same.rows(); ++row) {
    for (std::size_t col = 0; col < same.cols(); ++col) {
      EXPECT_NEAR(expected_same[row][col], same(row, col).real(), 1.0e-10);
      EXPECT_NEAR(0.0, same(row, col).imag(), 1.0e-10);
    }
  }

  const auto valid = ksj::fft::correlate2d_valid_fft(input, kernel);
  ASSERT_EQ(1U, valid.rows());
  ASSERT_EQ(1U, valid.cols());
  EXPECT_NEAR(30.0, valid(0, 0).real(), 1.0e-10);
  EXPECT_NEAR(0.0, valid(0, 0).imag(), 1.0e-10);
}

TEST(KSpaceJetFft, WritesFftConvolutionWithAliasProtection) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 1);
  auto kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 1);
  input(0, 0) = {2.0, 1.0};
  kernel(0, 0) = {3.0, -2.0};

  ksj::fft::convolve2d_full_fft(input, kernel, input);

  EXPECT_NEAR(8.0, input(0, 0).real(), 1.0e-12);
  EXPECT_NEAR(-1.0, input(0, 0).imag(), 1.0e-12);

  auto wrong_output = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 2);
  EXPECT_THROW(ksj::fft::convolve2d_full_fft(input, kernel, wrong_output), std::invalid_argument);

  auto empty = ksj::array::make_pooled_matrix<ksj::base::cf64>(0, 0);
  auto scalar_output = ksj::array::make_pooled_matrix<ksj::base::cf64>(1, 1);
  EXPECT_THROW(ksj::fft::correlate2d_full_fft(empty, kernel, scalar_output), std::invalid_argument);

  auto large_kernel = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  EXPECT_THROW(static_cast<void>(ksj::fft::convolve2d_valid_fft(input, large_kernel)), std::invalid_argument);
  EXPECT_THROW(ksj::fft::correlate2d_same_fft(input, kernel, wrong_output), std::invalid_argument);
}

TEST(KSpaceJetFft, Inverse2dNormalizationRestoresInput) {
  auto input = ksj::array::make_pooled_matrix<ksj::base::cf64>(2, 2);
  input(0, 0) = {1.0, 0.0};
  input(1, 0) = {2.0, 0.5};
  input(0, 1) = {3.0, -1.0};
  input(1, 1) = {4.0, 2.0};

  const auto spectrum = ksj::fft::fft2(input);
  const auto restored = ksj::fft::ifft2(spectrum);

  for (std::size_t col = 0; col < input.cols(); ++col) {
    for (std::size_t row = 0; row < input.rows(); ++row) {
      EXPECT_NEAR(input(row, col).real(), restored(row, col).real(), 1.0e-12);
      EXPECT_NEAR(input(row, col).imag(), restored(row, col).imag(), 1.0e-12);
    }
  }
}

} // namespace
