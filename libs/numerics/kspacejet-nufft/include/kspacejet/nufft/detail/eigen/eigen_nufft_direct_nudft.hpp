#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/nufft/types.hpp"

namespace ksj::nufft::detail::eigen {

void direct_nudft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::VectorView<ksj::base::cf32> output);

void direct_nudft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::VectorView<ksj::base::cf64> output);

void direct_nudft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                           ksj::array::MatrixView<const float> trajectory,
                           ksj::array::MatrixView<ksj::base::cf32> image);

void direct_nudft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                           ksj::array::MatrixView<const double> trajectory,
                           ksj::array::MatrixView<ksj::base::cf64> image);

} // namespace ksj::nufft::detail::eigen
