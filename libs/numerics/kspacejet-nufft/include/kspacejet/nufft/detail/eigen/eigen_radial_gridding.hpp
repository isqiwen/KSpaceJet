#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/nufft/radial_gridding.hpp"

namespace ksj::nufft::detail::eigen {

void radial_analytic_ramp_dcf2(ksj::array::MatrixView<const float> trajectory_radians_per_pixel,
                               ksj::array::VectorView<float> density_compensation);

void radial_analytic_ramp_dcf2(ksj::array::MatrixView<const double> trajectory_radians_per_pixel,
                               ksj::array::VectorView<double> density_compensation);

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

} // namespace ksj::nufft::detail::eigen
