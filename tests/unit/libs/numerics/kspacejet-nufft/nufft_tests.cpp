#include "kspacejet/base/types.hpp"
#include "kspacejet/nufft/nufft.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {

TEST(KSpaceJetNufft, DirectForwardUsesRowMajorPooledMatrix) {
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf64>(2U, 2U);
  image(0U, 0U) = {1.0, 0.0};
  image(0U, 1U) = {2.0, 0.0};
  image(1U, 0U) = {3.0, 0.0};
  image(1U, 1U) = {4.0, 0.0};

  auto trajectory = ksj::array::make_pooled_matrix<double>(2U, 2U);
  trajectory(0U, 0U) = 0.0;
  trajectory(0U, 1U) = 0.0;
  trajectory(1U, 0U) = std::numbers::pi;
  trajectory(1U, 1U) = 0.0;

  const auto output = ksj::nufft::direct_nudft2_forward({2U, 2U}, image, trajectory);

  ASSERT_EQ(2U, output.size());
  EXPECT_NEAR(10.0, output(0U).real(), 1.0e-12);
  EXPECT_NEAR(0.0, output(0U).imag(), 1.0e-12);
  EXPECT_NEAR(-4.0, output(1U).real(), 1.0e-12);
  EXPECT_NEAR(0.0, output(1U).imag(), 1.0e-12);
}

TEST(KSpaceJetNufft, DirectForwardWritesToViewOutput) {
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf32>(1U, 2U);
  image(0U, 0U) = {1.0F, 0.0F};
  image(0U, 1U) = {2.0F, 0.0F};

  auto trajectory = ksj::array::make_pooled_matrix<float>(1U, 2U);
  trajectory(0U, 0U) = 0.0F;
  trajectory(0U, 1U) = 0.0F;

  auto output = ksj::array::make_pooled_vector<ksj::base::cf32>(1U);
  ksj::nufft::direct_nudft2_forward({1U, 2U}, image.view(), trajectory.view(), output.view());

  EXPECT_NEAR(3.0F, output(0U).real(), 1.0e-6F);
  EXPECT_NEAR(0.0F, output(0U).imag(), 1.0e-6F);
}

TEST(KSpaceJetNufft, DirectAdjointWritesAllGridPoints) {
  auto samples = ksj::array::make_pooled_vector<ksj::base::cf64>(1U);
  samples(0U) = {2.0, -1.0};

  auto trajectory = ksj::array::make_pooled_matrix<double>(1U, 2U);
  trajectory(0U, 0U) = 0.0;
  trajectory(0U, 1U) = 0.0;

  const auto image = ksj::nufft::direct_nudft2_adjoint({2U, 3U}, samples, trajectory);

  ASSERT_EQ(2U, image.rows());
  ASSERT_EQ(3U, image.cols());
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      EXPECT_NEAR(2.0, image(row, col).real(), 1.0e-12);
      EXPECT_NEAR(-1.0, image(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetNufft, NufftForwardEigenBackendMatchesDirectNudft) {
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf64>(2U, 2U);
  image(0U, 0U) = {1.0, 0.0};
  image(0U, 1U) = {2.0, -1.0};
  image(1U, 0U) = {-3.0, 4.0};
  image(1U, 1U) = {5.0, 0.5};

  auto trajectory = ksj::array::make_pooled_matrix<double>(2U, 2U);
  trajectory(0U, 0U) = 0.0;
  trajectory(0U, 1U) = 0.0;
  trajectory(1U, 0U) = std::numbers::pi / 2.0;
  trajectory(1U, 1U) = std::numbers::pi / 3.0;

  const auto direct = ksj::nufft::direct_nudft2_forward({2U, 2U}, image, trajectory);
  const auto routed = ksj::nufft::nufft2_forward({2U, 2U}, image, trajectory, {ksj::nufft::Backend::eigen});

  ASSERT_EQ(direct.size(), routed.size());
  for (std::size_t index = 0; index < direct.size(); ++index) {
    EXPECT_NEAR(direct(index).real(), routed(index).real(), 1.0e-12);
    EXPECT_NEAR(direct(index).imag(), routed(index).imag(), 1.0e-12);
  }
}

TEST(KSpaceJetNufft, NufftForwardEigenWorkspaceBackendMatchesDirectNudft) {
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf64>(2U, 2U);
  image(0U, 0U) = {1.0, 0.0};
  image(0U, 1U) = {2.0, -1.0};
  image(1U, 0U) = {-3.0, 4.0};
  image(1U, 1U) = {5.0, 0.5};

  auto trajectory = ksj::array::make_pooled_matrix<double>(2U, 2U);
  trajectory(0U, 0U) = 0.0;
  trajectory(0U, 1U) = 0.0;
  trajectory(1U, 0U) = std::numbers::pi / 2.0;
  trajectory(1U, 1U) = std::numbers::pi / 3.0;

  auto routed = ksj::array::make_pooled_vector<ksj::base::cf64>(trajectory.rows());
  auto workspace = ksj::nufft::Nufft2Workspace<double>{};
  ksj::nufft::nufft2_forward({2U, 2U}, image.view(), trajectory.view(), routed.view(), workspace,
                             {ksj::nufft::Backend::eigen});

  const auto direct = ksj::nufft::direct_nudft2_forward({2U, 2U}, image, trajectory);
  ASSERT_EQ(direct.size(), routed.size());
  for (std::size_t index = 0; index < direct.size(); ++index) {
    EXPECT_NEAR(direct(index).real(), routed(index).real(), 1.0e-12);
    EXPECT_NEAR(direct(index).imag(), routed(index).imag(), 1.0e-12);
  }
}

TEST(KSpaceJetNufft, NufftAdjointEigenBackendMatchesDirectNudft) {
  auto samples = ksj::array::make_pooled_vector<ksj::base::cf64>(2U);
  samples(0U) = {1.0, 2.0};
  samples(1U) = {-3.0, 0.5};

  auto trajectory = ksj::array::make_pooled_matrix<double>(2U, 2U);
  trajectory(0U, 0U) = 0.0;
  trajectory(0U, 1U) = 0.0;
  trajectory(1U, 0U) = std::numbers::pi / 2.0;
  trajectory(1U, 1U) = std::numbers::pi / 3.0;

  const auto direct = ksj::nufft::direct_nudft2_adjoint({2U, 2U}, samples, trajectory);
  const auto routed = ksj::nufft::nufft2_adjoint({2U, 2U}, samples, trajectory, {ksj::nufft::Backend::eigen});

  ASSERT_EQ(direct.rows(), routed.rows());
  ASSERT_EQ(direct.cols(), routed.cols());
  for (std::size_t row = 0; row < direct.rows(); ++row) {
    for (std::size_t col = 0; col < direct.cols(); ++col) {
      EXPECT_NEAR(direct(row, col).real(), routed(row, col).real(), 1.0e-12);
      EXPECT_NEAR(direct(row, col).imag(), routed(row, col).imag(), 1.0e-12);
    }
  }
}

TEST(KSpaceJetNufft, ExplicitBartBackendRejectsUnsupportedDoublePath) {
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf64>(1U, 1U);
  auto trajectory = ksj::array::make_pooled_matrix<double>(1U, 2U);
  auto output = ksj::array::make_pooled_vector<ksj::base::cf64>(1U);

  EXPECT_THROW(
    ksj::nufft::nufft2_forward({1U, 1U}, image.view(), trajectory.view(), output.view(), {ksj::nufft::Backend::bart}),
    std::runtime_error);
}

TEST(KSpaceJetNufft, BartBackendRunsForwardSmokeWhenAvailable) {
  if (!ksj::nufft::bart_backend_available()) {
    GTEST_SKIP() << "BART backend is not available in this build";
  }

  auto image = ksj::array::make_pooled_matrix<ksj::base::cf32>(1U, 1U);
  image(0U, 0U) = {1.0F, 0.0F};

  auto trajectory = ksj::array::make_pooled_matrix<float>(1U, 2U);
  trajectory(0U, 0U) = 0.0F;
  trajectory(0U, 1U) = 0.0F;

  auto output = ksj::array::make_pooled_vector<ksj::base::cf32>(1U);
  EXPECT_NO_THROW(ksj::nufft::nufft2_forward({1U, 1U}, image.view(), trajectory.view(), output.view(),
                                             {ksj::nufft::Backend::bart, true}));
  EXPECT_TRUE(std::isfinite(output(0U).real()));
  EXPECT_TRUE(std::isfinite(output(0U).imag()));
}

TEST(KSpaceJetNufft, BartDirectDftMatchesEigenForNonzeroTrajectoryWhenAvailable) {
  if (!ksj::nufft::bart_backend_available()) {
    GTEST_SKIP() << "BART backend is not available in this build";
  }

  const auto grid = ksj::nufft::Grid2D{4U, 3U, 1.5, -0.5};
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf32>(grid.rows, grid.cols);
  for (std::size_t row = 0U; row < image.rows(); ++row) {
    for (std::size_t col = 0U; col < image.cols(); ++col) {
      image(row, col) = {static_cast<float>(row * 2U + col + 1U) / 7.0F,
                         static_cast<float>(static_cast<int>((row + col) % 5U) - 2) / 11.0F};
    }
  }

  auto trajectory = ksj::array::make_pooled_matrix<float>(3U, 2U);
  trajectory(0U, 0U) = 0.0F;
  trajectory(0U, 1U) = 0.0F;
  trajectory(1U, 0U) = std::numbers::pi_v<float> / 2.0F;
  trajectory(1U, 1U) = std::numbers::pi_v<float> / 3.0F;
  trajectory(2U, 0U) = -std::numbers::pi_v<float> / 4.0F;
  trajectory(2U, 1U) = std::numbers::pi_v<float> * 2.0F / 5.0F;

  const auto eigen_forward = ksj::nufft::direct_nudft2_forward(grid, image, trajectory);
  const auto bart_forward = ksj::nufft::nufft2_forward(grid, image, trajectory, {ksj::nufft::Backend::bart, true});
  for (std::size_t index = 0U; index < eigen_forward.size(); ++index) {
    EXPECT_NEAR(eigen_forward(index).real(), bart_forward(index).real(), 1.0e-4F);
    EXPECT_NEAR(eigen_forward(index).imag(), bart_forward(index).imag(), 1.0e-4F);
  }

  auto samples = ksj::array::make_pooled_vector<ksj::base::cf32>(3U);
  samples(0U) = {1.0F, 2.0F};
  samples(1U) = {-3.0F, 0.5F};
  samples(2U) = {0.75F, -1.25F};
  const auto eigen_adjoint = ksj::nufft::direct_nudft2_adjoint(grid, samples, trajectory);
  const auto bart_adjoint = ksj::nufft::nufft2_adjoint(grid, samples, trajectory, {ksj::nufft::Backend::bart, true});
  for (std::size_t row = 0U; row < eigen_adjoint.rows(); ++row) {
    for (std::size_t col = 0U; col < eigen_adjoint.cols(); ++col) {
      EXPECT_NEAR(eigen_adjoint(row, col).real(), bart_adjoint(row, col).real(), 1.0e-4F);
      EXPECT_NEAR(eigen_adjoint(row, col).imag(), bart_adjoint(row, col).imag(), 1.0e-4F);
    }
  }
}

TEST(KSpaceJetNufft, BartForwardReusesCallerOwnedWorkspaceWhenAvailable) {
  if (!ksj::nufft::bart_backend_available()) {
    GTEST_SKIP() << "BART backend is not available in this build";
  }

  auto image = ksj::array::make_pooled_matrix<ksj::base::cf32>(1U, 1U);
  image(0U, 0U) = {1.0F, 0.0F};

  auto trajectory = ksj::array::make_pooled_matrix<float>(2U, 2U);
  trajectory(0U, 0U) = 0.0F;
  trajectory(0U, 1U) = 0.0F;
  trajectory(1U, 0U) = 0.25F;
  trajectory(1U, 1U) = -0.125F;

  auto output_storage = ksj::array::make_pooled_matrix<ksj::base::cf32>(2U, 2U);
  auto output = output_storage.col(0U);
  auto workspace = ksj::nufft::Nufft2Workspace<float>{};

  ksj::nufft::nufft2_forward({1U, 1U}, image.view(), trajectory.view(), output, workspace,
                             {ksj::nufft::Backend::bart, true});

  ASSERT_NE(nullptr, workspace.bart_trajectory.data());
  ASSERT_NE(nullptr, workspace.output_buffer.data());
  ASSERT_NE(nullptr, workspace.bart_plan.get());
  const auto* trajectory_buffer = workspace.bart_trajectory.data();
  const auto* output_buffer = workspace.output_buffer.data();
  const auto* plan = workspace.bart_plan.get();

  ksj::nufft::nufft2_forward({1U, 1U}, image.view(), trajectory.view(), output, workspace,
                             {ksj::nufft::Backend::bart, true});

  EXPECT_EQ(trajectory_buffer, workspace.bart_trajectory.data());
  EXPECT_EQ(output_buffer, workspace.output_buffer.data());
  EXPECT_EQ(plan, workspace.bart_plan.get());
  for (std::size_t index = 0; index < output.size(); ++index) {
    EXPECT_TRUE(std::isfinite(output(index).real()));
    EXPECT_TRUE(std::isfinite(output(index).imag()));
  }
}

TEST(KSpaceJetNufft, RejectsMismatchedDimensions) {
  auto image = ksj::array::make_pooled_matrix<ksj::base::cf64>(2U, 2U);
  auto trajectory = ksj::array::make_pooled_matrix<double>(1U, 3U);
  auto output = ksj::array::make_pooled_vector<ksj::base::cf64>(1U);

  EXPECT_THROW(ksj::nufft::direct_nudft2_forward({2U, 2U}, image.view(), trajectory.view(), output.view()),
               std::invalid_argument);
}

} // namespace
