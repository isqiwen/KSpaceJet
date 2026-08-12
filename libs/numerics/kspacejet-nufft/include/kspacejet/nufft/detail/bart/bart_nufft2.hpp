#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/nufft/types.hpp"
#include "kspacejet/nufft/workspace.hpp"

namespace ksj::nufft::detail::bart {

[[nodiscard]] bool available() noexcept;

[[nodiscard]] bool nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                                  ksj::array::MatrixView<const float> trajectory,
                                  ksj::array::VectorView<ksj::base::cf32> output, bool direct_dft);

[[nodiscard]] bool nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                                  ksj::array::MatrixView<const float> trajectory,
                                  ksj::array::VectorView<ksj::base::cf32> output, Nufft2Workspace<float>& workspace,
                                  bool direct_dft);

[[nodiscard]] bool nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                                  ksj::array::MatrixView<const double> trajectory,
                                  ksj::array::VectorView<ksj::base::cf64> output, bool direct_dft);

[[nodiscard]] bool nufft2_forward(Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                                  ksj::array::MatrixView<const double> trajectory,
                                  ksj::array::VectorView<ksj::base::cf64> output, Nufft2Workspace<double>& workspace,
                                  bool direct_dft);

[[nodiscard]] bool nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                                  ksj::array::MatrixView<const float> trajectory,
                                  ksj::array::MatrixView<ksj::base::cf32> image, bool direct_dft);

[[nodiscard]] bool nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                                  ksj::array::MatrixView<const float> trajectory,
                                  ksj::array::MatrixView<ksj::base::cf32> image, Nufft2Workspace<float>& workspace,
                                  bool direct_dft);

[[nodiscard]] bool nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                                  ksj::array::MatrixView<const double> trajectory,
                                  ksj::array::MatrixView<ksj::base::cf64> image, bool direct_dft);

[[nodiscard]] bool nufft2_adjoint(Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                                  ksj::array::MatrixView<const double> trajectory,
                                  ksj::array::MatrixView<ksj::base::cf64> image, Nufft2Workspace<double>& workspace,
                                  bool direct_dft);

} // namespace ksj::nufft::detail::bart
