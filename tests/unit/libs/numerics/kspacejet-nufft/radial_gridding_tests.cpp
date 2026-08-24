#include "kspacejet/base/types.hpp"
#include "kspacejet/nufft/nufft.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {

TEST(KSpaceJetRadialGridding, AnalyticRampUsesRadianPerPixelCoordinates) {
  std::array<double, 6U> trajectory_storage{
    0.0, 0.0, 3.0 / 5.0, 4.0 / 5.0, -std::numbers::pi_v<double>, 0.0,
  };
  std::array<double, 3U> density_storage{};
  const auto trajectory = ksj::array::MatrixView<double>(trajectory_storage.data(), 3U, 2U);
  const auto density = ksj::array::VectorView<double>(density_storage.data(), density_storage.size());

  ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density);

  EXPECT_DOUBLE_EQ(0.0, density(0U));
  EXPECT_DOUBLE_EQ(1.0, density(1U));
  EXPECT_DOUBLE_EQ(std::numbers::pi_v<double>, density(2U));
}

TEST(KSpaceJetRadialGridding, MatchesWeightedDirectNudftForCartesianBinRadialSpokes) {
  constexpr std::size_t kRows = 4U;
  constexpr std::size_t kCols = 4U;
  constexpr std::size_t kSamples = 5U;
  constexpr std::size_t kPixels = kRows * kCols;

  const auto grid = ksj::nufft::Grid2D{kRows, kCols, 1.5, 0.5};
  std::array<ksj::base::cf64, kSamples> samples_storage{
    ksj::base::cf64{1.0, -0.5}, ksj::base::cf64{-2.0, 0.25},   ksj::base::cf64{0.5, 3.0},
    ksj::base::cf64{1.5, -1.0}, ksj::base::cf64{-0.75, -0.25},
  };
  std::array<double, kSamples * 2U> trajectory_storage{
    -std::numbers::pi_v<double> / 2.0,
    0.0,
    0.0,
    0.0,
    std::numbers::pi_v<double> / 2.0,
    0.0,
    0.0,
    -std::numbers::pi_v<double> / 2.0,
    0.0,
    std::numbers::pi_v<double> / 2.0,
  };
  std::array<double, kSamples> density_storage{};
  std::array<ksj::base::cf64, kPixels> image_storage{};
  std::array<ksj::base::cf64, kPixels> fft_intermediate_storage{};
  std::array<ksj::base::cf64, kCols> fft_source_storage{};
  std::array<ksj::base::cf64, kCols> fft_destination_storage{};
  std::array<ksj::base::cf64, kSamples> weighted_samples_storage{};
  std::array<ksj::base::cf64, kPixels> direct_image_storage{};

  const auto samples = ksj::array::VectorView<ksj::base::cf64>(samples_storage.data(), samples_storage.size());
  const auto trajectory = ksj::array::MatrixView<double>(trajectory_storage.data(), kSamples, 2U);
  const auto density = ksj::array::VectorView<double>(density_storage.data(), density_storage.size());
  const auto image = ksj::array::MatrixView<ksj::base::cf64>(image_storage.data(), kRows, kCols);
  const auto fft_intermediate = ksj::array::MatrixView<ksj::base::cf64>(fft_intermediate_storage.data(), kRows, kCols);
  const auto fft_source = ksj::array::VectorView<ksj::base::cf64>(fft_source_storage.data(), fft_source_storage.size());
  const auto fft_destination =
    ksj::array::VectorView<ksj::base::cf64>(fft_destination_storage.data(), fft_destination_storage.size());
  const auto workspace = ksj::nufft::RadialGridding2Workspace<double>{fft_intermediate, fft_source, fft_destination};

  ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density);
  ksj::nufft::radial_linear_gridding2_adjoint(grid, samples, trajectory, density, image, workspace);

  for (std::size_t sample = 0U; sample < kSamples; ++sample) {
    weighted_samples_storage[sample] = samples_storage[sample] * density_storage[sample];
  }
  const auto weighted_samples =
    ksj::array::VectorView<ksj::base::cf64>(weighted_samples_storage.data(), weighted_samples_storage.size());
  const auto direct_image = ksj::array::MatrixView<ksj::base::cf64>(direct_image_storage.data(), kRows, kCols);
  ksj::nufft::direct_nudft2_adjoint(grid, ksj::array::as_const_view(weighted_samples),
                                    ksj::array::as_const_view(trajectory), direct_image);

  for (std::size_t row = 0U; row < kRows; ++row) {
    for (std::size_t col = 0U; col < kCols; ++col) {
      EXPECT_NEAR(direct_image(row, col).real(), image(row, col).real(), 1.0e-10);
      EXPECT_NEAR(direct_image(row, col).imag(), image(row, col).imag(), 1.0e-10);
    }
  }
}

TEST(KSpaceJetRadialGridding, LinearlyDistributesOffBinSample) {
  constexpr std::size_t kRows = 2U;
  constexpr std::size_t kCols = 2U;
  std::array<ksj::base::cf64, 1U> samples_storage{ksj::base::cf64{2.0 / std::numbers::pi_v<double>, 0.0}};
  std::array<double, 2U> trajectory_storage{std::numbers::pi_v<double> / 2.0, 0.0};
  std::array<double, 1U> density_storage{};
  std::array<ksj::base::cf64, kRows * kCols> image_storage{};
  std::array<ksj::base::cf64, kRows * kCols> fft_intermediate_storage{};
  std::array<ksj::base::cf64, kCols> fft_source_storage{};
  std::array<ksj::base::cf64, kCols> fft_destination_storage{};

  const auto samples = ksj::array::VectorView<ksj::base::cf64>(samples_storage.data(), samples_storage.size());
  const auto trajectory = ksj::array::MatrixView<double>(trajectory_storage.data(), 1U, 2U);
  const auto density = ksj::array::VectorView<double>(density_storage.data(), density_storage.size());
  const auto image = ksj::array::MatrixView<ksj::base::cf64>(image_storage.data(), kRows, kCols);
  const auto workspace = ksj::nufft::RadialGridding2Workspace<double>{
    ksj::array::MatrixView<ksj::base::cf64>(fft_intermediate_storage.data(), kRows, kCols),
    ksj::array::VectorView<ksj::base::cf64>(fft_source_storage.data(), fft_source_storage.size()),
    ksj::array::VectorView<ksj::base::cf64>(fft_destination_storage.data(), fft_destination_storage.size()),
  };

  ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density);
  ksj::nufft::radial_linear_gridding2_adjoint({kRows, kCols}, samples, trajectory, density, image, workspace);

  EXPECT_NEAR(1.0, image(0U, 0U).real(), 1.0e-12);
  EXPECT_NEAR(1.0, image(0U, 1U).real(), 1.0e-12);
  EXPECT_NEAR(0.0, image(1U, 0U).real(), 1.0e-12);
  EXPECT_NEAR(0.0, image(1U, 1U).real(), 1.0e-12);
  for (std::size_t row = 0U; row < kRows; ++row) {
    for (std::size_t col = 0U; col < kCols; ++col) {
      EXPECT_NEAR(0.0, image(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetRadialGridding, IsByteIdenticalAcrossRepeatedCallerWorkspaceExecutions) {
  constexpr std::size_t kRows = 3U;
  constexpr std::size_t kCols = 4U;
  constexpr std::size_t kPixels = kRows * kCols;
  std::array<ksj::base::cf32, 3U> samples_storage{
    ksj::base::cf32{0.5F, 1.0F},
    ksj::base::cf32{-1.0F, 0.25F},
    ksj::base::cf32{2.0F, -0.75F},
  };
  std::array<float, 6U> trajectory_storage{
    -std::numbers::pi_v<float> / 3.0F, 0.0F, 0.0F, std::numbers::pi_v<float> / 2.0F, std::numbers::pi_v<float> / 4.0F,
    -std::numbers::pi_v<float> / 3.0F,
  };
  std::array<float, 3U> density_storage{};
  std::array<ksj::base::cf32, kPixels> first_image_storage{};
  std::array<ksj::base::cf32, kPixels> second_image_storage{};
  std::array<ksj::base::cf32, kPixels> fft_intermediate_storage{};
  std::array<ksj::base::cf32, kCols> fft_source_storage{};
  std::array<ksj::base::cf32, kCols> fft_destination_storage{};

  const auto samples = ksj::array::VectorView<ksj::base::cf32>(samples_storage.data(), samples_storage.size());
  const auto trajectory = ksj::array::MatrixView<float>(trajectory_storage.data(), 3U, 2U);
  const auto density = ksj::array::VectorView<float>(density_storage.data(), density_storage.size());
  const auto workspace = ksj::nufft::RadialGridding2Workspace<float>{
    ksj::array::MatrixView<ksj::base::cf32>(fft_intermediate_storage.data(), kRows, kCols),
    ksj::array::VectorView<ksj::base::cf32>(fft_source_storage.data(), fft_source_storage.size()),
    ksj::array::VectorView<ksj::base::cf32>(fft_destination_storage.data(), fft_destination_storage.size()),
  };

  ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density);
  ksj::nufft::radial_linear_gridding2_adjoint(
    {kRows, kCols}, samples, trajectory, density,
    ksj::array::MatrixView<ksj::base::cf32>(first_image_storage.data(), kRows, kCols), workspace);
  ksj::nufft::radial_linear_gridding2_adjoint(
    {kRows, kCols}, samples, trajectory, density,
    ksj::array::MatrixView<ksj::base::cf32>(second_image_storage.data(), kRows, kCols), workspace);

  EXPECT_EQ(0, std::memcmp(first_image_storage.data(), second_image_storage.data(), sizeof(first_image_storage)));
}

TEST(KSpaceJetRadialGridding, RejectsRampShapeNonfiniteOutOfRangeAndAlias) {
  std::array<double, 4U> trajectory_storage{0.0, 0.0, 0.5, -0.25};
  std::array<double, 2U> density_storage{};
  const auto trajectory = ksj::array::MatrixView<double>(trajectory_storage.data(), 2U, 2U);
  const auto density = ksj::array::VectorView<double>(density_storage.data(), density_storage.size());

  EXPECT_THROW(
    ksj::nufft::radial_analytic_ramp_dcf2(ksj::array::MatrixView<double>(trajectory_storage.data(), 2U, 1U), density),
    std::invalid_argument);
  EXPECT_THROW(
    ksj::nufft::radial_analytic_ramp_dcf2(trajectory, ksj::array::VectorView<double>(density_storage.data(), 1U)),
    std::invalid_argument);

  trajectory_storage[0U] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density), std::invalid_argument);
  trajectory_storage[0U] = std::numbers::pi_v<double> + 0.001;
  EXPECT_THROW(ksj::nufft::radial_analytic_ramp_dcf2(trajectory, density), std::invalid_argument);
  trajectory_storage[0U] = 0.0;
  EXPECT_THROW(
    ksj::nufft::radial_analytic_ramp_dcf2(trajectory, ksj::array::VectorView<double>(trajectory_storage.data(), 2U)),
    std::invalid_argument);
}

TEST(KSpaceJetRadialGridding, RejectsBadInputsAndCallerWorkspace) {
  constexpr std::size_t kRows = 2U;
  constexpr std::size_t kCols = 2U;
  std::array<ksj::base::cf64, 2U> samples_storage{ksj::base::cf64{1.0, 0.0}, ksj::base::cf64{0.5, -0.25}};
  std::array<double, 4U> trajectory_storage{0.0, 0.0, std::numbers::pi_v<double> / 2.0, 0.0};
  std::array<double, 2U> density_storage{1.0, 1.0};
  std::array<ksj::base::cf64, kRows * kCols> image_storage{};
  std::array<ksj::base::cf64, kRows * kCols> intermediate_storage{};
  std::array<ksj::base::cf64, kCols> source_storage{};
  std::array<ksj::base::cf64, kCols> destination_storage{};

  const auto samples = ksj::array::VectorView<ksj::base::cf64>(samples_storage.data(), samples_storage.size());
  const auto trajectory = ksj::array::MatrixView<double>(trajectory_storage.data(), 2U, 2U);
  const auto density = ksj::array::VectorView<double>(density_storage.data(), density_storage.size());
  const auto image = ksj::array::MatrixView<ksj::base::cf64>(image_storage.data(), kRows, kCols);
  const auto intermediate = ksj::array::MatrixView<ksj::base::cf64>(intermediate_storage.data(), kRows, kCols);
  const auto source = ksj::array::VectorView<ksj::base::cf64>(source_storage.data(), source_storage.size());
  const auto destination =
    ksj::array::VectorView<ksj::base::cf64>(destination_storage.data(), destination_storage.size());
  const auto workspace = ksj::nufft::RadialGridding2Workspace<double>{intermediate, source, destination};
  const auto run = [&] {
    ksj::nufft::radial_linear_gridding2_adjoint({kRows, kCols}, samples, trajectory, density, image, workspace);
  };

  EXPECT_THROW(ksj::nufft::radial_linear_gridding2_adjoint(
                 {kRows, kCols}, samples, trajectory, density, image,
                 {intermediate, ksj::array::VectorView<ksj::base::cf64>(source_storage.data(), 1U), destination}),
               std::invalid_argument);
  EXPECT_THROW(ksj::nufft::radial_linear_gridding2_adjoint({kRows, kCols}, samples, trajectory, density, image,
                                                           {image, source, destination}),
               std::invalid_argument);
  EXPECT_THROW(ksj::nufft::radial_linear_gridding2_adjoint(
                 {kRows, kCols}, ksj::array::VectorView<ksj::base::cf64>(image_storage.data(), 2U), trajectory, density,
                 image, workspace),
               std::invalid_argument);

  samples_storage[0U] = {std::numeric_limits<double>::quiet_NaN(), 0.0};
  EXPECT_THROW(run(), std::invalid_argument);
  samples_storage[0U] = {1.0, 0.0};
  trajectory_storage[0U] = std::numbers::pi_v<double> + 0.001;
  EXPECT_THROW(run(), std::invalid_argument);
  trajectory_storage[0U] = 0.0;
  density_storage[0U] = -1.0;
  EXPECT_THROW(run(), std::invalid_argument);
}

} // namespace
