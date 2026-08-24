#include "kspacejet/base/types.hpp"
#include "kspacejet/nufft/nufft.hpp"

#include "kspacejet/nufft/detail/bart/bart_nufft2.hpp"
#include "kspacejet/nufft/detail/eigen/eigen_nufft_direct_nudft.hpp"
#include "kspacejet/nufft/detail/eigen/eigen_radial_gridding.hpp"
#include "kspacejet/nufft/detail/nufft_policy.hpp"

#include <stdexcept>

namespace ksj::nufft {

namespace {

void throw_unavailable_bart() {
  throw std::runtime_error("BART NUFFT backend is unavailable for this build or scalar type");
}

template <typename T, typename ImageView, typename TrajectoryView, typename OutputView>
void dispatch_forward(const Grid2D grid, ImageView image, TrajectoryView trajectory, OutputView output,
                      const Nufft2Options options) {
  if (detail::prefer_bart_nufft2(options.backend) &&
      detail::bart::nufft2_forward(grid, image, trajectory, output, options.direct_dft)) {
    return;
  }
  if (detail::require_bart_nufft2(options.backend)) {
    throw_unavailable_bart();
  }
  detail::eigen::direct_nudft2_forward(grid, image, trajectory, output);
}

template <typename T, typename ImageView, typename TrajectoryView, typename OutputView>
void dispatch_forward(const Grid2D grid, ImageView image, TrajectoryView trajectory, OutputView output,
                      Nufft2Workspace<T>& workspace, const Nufft2Options options) {
  if (detail::prefer_bart_nufft2(options.backend) &&
      detail::bart::nufft2_forward(grid, image, trajectory, output, workspace, options.direct_dft)) {
    return;
  }
  if (detail::require_bart_nufft2(options.backend)) {
    throw_unavailable_bart();
  }
  detail::eigen::direct_nudft2_forward(grid, image, trajectory, output);
}

template <typename T, typename SampleView, typename TrajectoryView, typename ImageView>
void dispatch_adjoint(const Grid2D grid, SampleView samples, TrajectoryView trajectory, ImageView image,
                      const Nufft2Options options) {
  if (detail::prefer_bart_nufft2(options.backend) &&
      detail::bart::nufft2_adjoint(grid, samples, trajectory, image, options.direct_dft)) {
    return;
  }
  if (detail::require_bart_nufft2(options.backend)) {
    throw_unavailable_bart();
  }
  detail::eigen::direct_nudft2_adjoint(grid, samples, trajectory, image);
}

template <typename T, typename SampleView, typename TrajectoryView, typename ImageView>
void dispatch_adjoint(const Grid2D grid, SampleView samples, TrajectoryView trajectory, ImageView image,
                      Nufft2Workspace<T>& workspace, const Nufft2Options options) {
  if (detail::prefer_bart_nufft2(options.backend) &&
      detail::bart::nufft2_adjoint(grid, samples, trajectory, image, workspace, options.direct_dft)) {
    return;
  }
  if (detail::require_bart_nufft2(options.backend)) {
    throw_unavailable_bart();
  }
  detail::eigen::direct_nudft2_adjoint(grid, samples, trajectory, image);
}

} // namespace

bool bart_backend_available() noexcept {
  return detail::bart::available();
}

void direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::VectorView<ksj::base::cf32> output) {
  detail::eigen::direct_nudft2_forward(grid, image, trajectory, output);
}

void direct_nudft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::VectorView<ksj::base::cf64> output) {
  detail::eigen::direct_nudft2_forward(grid, image, trajectory, output);
}

void direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::MatrixView<ksj::base::cf32> image) {
  detail::eigen::direct_nudft2_adjoint(grid, samples, trajectory, image);
}

void direct_nudft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::MatrixView<ksj::base::cf64> image) {
  detail::eigen::direct_nudft2_adjoint(grid, samples, trajectory, image);
}

void radial_analytic_ramp_dcf2(const ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                               const ksj::array::VectorView<float> density_compensation) {
  detail::eigen::radial_analytic_ramp_dcf2(trajectory_radians_per_pixel, density_compensation);
}

void radial_analytic_ramp_dcf2(const ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                               const ksj::array::VectorView<double> density_compensation) {
  detail::eigen::radial_analytic_ramp_dcf2(trajectory_radians_per_pixel, density_compensation);
}

void radial_linear_gridding2_adjoint(const Grid2D grid, const ksj::array::VectorView<const ksj::base::cf32> samples,
                                     const ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                                     const ksj::array::VectorView<const float> density_compensation,
                                     const ksj::array::MatrixView<ksj::base::cf32> image,
                                     const RadialGridding2Workspace<float> workspace) {
  detail::eigen::radial_linear_gridding2_adjoint(grid, samples, trajectory_radians_per_pixel, density_compensation,
                                                 image, workspace);
}

void radial_linear_gridding2_adjoint(const Grid2D grid, const ksj::array::VectorView<const ksj::base::cf64> samples,
                                     const ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                                     const ksj::array::VectorView<const double> density_compensation,
                                     const ksj::array::MatrixView<ksj::base::cf64> image,
                                     const RadialGridding2Workspace<double> workspace) {
  detail::eigen::radial_linear_gridding2_adjoint(grid, samples, trajectory_radians_per_pixel, density_compensation,
                                                 image, workspace);
}

void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                    const Nufft2Options options) {
  dispatch_forward<float>(grid, image, trajectory, output, options);
}

void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                    const Nufft2Options options) {
  dispatch_forward<double>(grid, image, trajectory, output, options);
}

void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                    Nufft2Workspace<float>& workspace, const Nufft2Options options) {
  dispatch_forward<float>(grid, image, trajectory, output, workspace, options);
}

void nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                    Nufft2Workspace<double>& workspace, const Nufft2Options options) {
  dispatch_forward<double>(grid, image, trajectory, output, workspace, options);
}

void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                    const Nufft2Options options) {
  dispatch_adjoint<float>(grid, samples, trajectory, image, options);
}

void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                    const Nufft2Options options) {
  dispatch_adjoint<double>(grid, samples, trajectory, image, options);
}

void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                    Nufft2Workspace<float>& workspace, const Nufft2Options options) {
  dispatch_adjoint<float>(grid, samples, trajectory, image, workspace, options);
}

void nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                    Nufft2Workspace<double>& workspace, const Nufft2Options options) {
  dispatch_adjoint<double>(grid, samples, trajectory, image, workspace, options);
}

} // namespace ksj::nufft
