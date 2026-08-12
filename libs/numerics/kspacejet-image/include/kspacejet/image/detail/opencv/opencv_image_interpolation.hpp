#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::image::detail::opencv {

[[nodiscard]] InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                               InterpolationAxis axis, float ratio);
} // namespace ksj::image::detail::opencv
