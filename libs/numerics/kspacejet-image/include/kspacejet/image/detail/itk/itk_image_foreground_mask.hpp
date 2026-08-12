#pragma once

#include "kspacejet/image/foreground_mask.hpp"

namespace ksj::image::detail::itk {

[[nodiscard]] bool calculate_volume_foreground_mask(const VolumeForegroundMaskInput& input,
                                                    const VolumeForegroundMaskOutput& output, float threshold_index,
                                                    float normalization_lower, float normalization_upper);

} // namespace ksj::image::detail::itk
