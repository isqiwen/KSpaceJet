#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/image/types.hpp"
#include "kspacejet/image/workspace.hpp"

namespace ksj::image::detail::intel {

[[nodiscard]] InterpolationResult resize_volume_cubic(ksj::array::CubeView<const ksj::base::cf32> input,
                                                      ksj::array::CubeView<ksj::base::cf32> output);
[[nodiscard]] InterpolationResult resize_volume_cubic(ksj::array::CubeView<const ksj::base::cf32> input,
                                                      ksj::array::CubeView<ksj::base::cf32> output,
                                                      ResizeVolumeCubicWorkspace& workspace);

} // namespace ksj::image::detail::intel
