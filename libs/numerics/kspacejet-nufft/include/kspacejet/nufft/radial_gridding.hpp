#pragma once

/// Bounded two-dimensional radial linear gridding and analytic density-compensation primitives.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/nufft/types.hpp"

#include <complex>

namespace ksj::nufft {

/// Caller-owned scratch required by radial_linear_gridding2_adjoint.
///
/// `fft_intermediate` must have exactly the reconstruction grid shape. Both
/// line buffers must contain at least max(grid.rows, grid.cols) contiguous
/// complex elements. The destination image is itself used as the gridded
/// k-space accumulator before an allocation-free separable inverse transform
/// (radix-2 FFT for power-of-two axes, direct DFT otherwise), so it cannot
/// alias any member of this workspace or any input view.
template <typename T> struct RadialGridding2Workspace {
  ksj::array::MatrixView<std::complex<T>> fft_intermediate{};
  ksj::array::VectorView<std::complex<T>> fft_source{};
  ksj::array::VectorView<std::complex<T>> fft_destination{};
};

/// Writes the unnormalised analytic radial-ramp DCF `hypot(k_row, k_col)`.
///
/// Trajectory rows are `[k_row, k_col]` in radians per pixel and each
/// coordinate must be finite and in the inclusive Nyquist interval [-pi, pi].
/// No angular, dwell-time, or acquisition-specific normalisation is implied.
void radial_analytic_ramp_dcf2(ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                               ksj::array::VectorView<float> density_compensation);

void radial_analytic_ramp_dcf2(ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                               ksj::array::VectorView<double> density_compensation);

inline void radial_analytic_ramp_dcf2(const ksj::array::MatrixView<float> trajectory_radians_per_pixel,
                                      const ksj::array::VectorView<float> density_compensation) {
  radial_analytic_ramp_dcf2(ksj::array::as_const_view(trajectory_radians_per_pixel), density_compensation);
}

inline void radial_analytic_ramp_dcf2(const ksj::array::MatrixView<double> trajectory_radians_per_pixel,
                                      const ksj::array::VectorView<double> density_compensation) {
  radial_analytic_ramp_dcf2(ksj::array::as_const_view(trajectory_radians_per_pixel), density_compensation);
}

/// Applies a weighted adjoint through periodic 2-by-2 linear Cartesian gridding.
///
/// The input trajectory is measured in radians per pixel in the same centered
/// coordinate convention as direct_nudft2_adjoint. `image` is overwritten: it
/// is cleared, filled as unshifted Cartesian k-space, and transformed by an
/// unnormalised inverse transform. Therefore Cartesian-bin samples reproduce
/// the weighted direct NUDFT adjoint, while off-bin samples use the documented
/// linear interpolation approximation. All writable storage, including every
/// transform buffer, is supplied by the caller; this function has no
/// allocating overload or hidden FFT-plan state.
void radial_linear_gridding2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                                     ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                                     ksj::array::VectorView<const float> density_compensation,
                                     ksj::array::MatrixView<ksj::base::cf32> image,
                                     RadialGridding2Workspace<float> workspace);

void radial_linear_gridding2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                                     ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                                     ksj::array::VectorView<const double> density_compensation,
                                     ksj::array::MatrixView<ksj::base::cf64> image,
                                     RadialGridding2Workspace<double> workspace);

inline void radial_linear_gridding2_adjoint(const Grid2D grid, const ksj::array::VectorView<ksj::base::cf32> samples,
                                            const ksj::array::MatrixView<float> trajectory_radians_per_pixel,
                                            const ksj::array::VectorView<float> density_compensation,
                                            const ksj::array::MatrixView<ksj::base::cf32> image,
                                            const RadialGridding2Workspace<float> workspace) {
  radial_linear_gridding2_adjoint(grid, ksj::array::as_const_view(samples),
                                  ksj::array::as_const_view(trajectory_radians_per_pixel),
                                  ksj::array::as_const_view(density_compensation), image, workspace);
}

inline void radial_linear_gridding2_adjoint(const Grid2D grid, const ksj::array::VectorView<ksj::base::cf64> samples,
                                            const ksj::array::MatrixView<double> trajectory_radians_per_pixel,
                                            const ksj::array::VectorView<double> density_compensation,
                                            const ksj::array::MatrixView<ksj::base::cf64> image,
                                            const RadialGridding2Workspace<double> workspace) {
  radial_linear_gridding2_adjoint(grid, ksj::array::as_const_view(samples),
                                  ksj::array::as_const_view(trajectory_radians_per_pixel),
                                  ksj::array::as_const_view(density_compensation), image, workspace);
}

} // namespace ksj::nufft
