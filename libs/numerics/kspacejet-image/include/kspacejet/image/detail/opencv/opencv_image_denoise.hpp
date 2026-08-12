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

[[nodiscard]] bool bm3d_denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input,
                                          ksj::array::ImageView<ksj::base::cf32> output, float sigma);

[[nodiscard]] inline bool bm3d_denoise_magnitude(const ksj::array::PooledImage<ksj::base::cf32>& input,
                                                 ksj::array::PooledImage<ksj::base::cf32>& output, const float sigma) {
  return bm3d_denoise_magnitude(ksj::array::as_const_view(input.view()), output.view(), sigma);
}
} // namespace ksj::image::detail::opencv
