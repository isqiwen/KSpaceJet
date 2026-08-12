#pragma once

/// Foreground-mask estimation and cleanup operations for image and volume inputs.

#include "kspacejet/image/volume_view.hpp"

namespace ksj::image {

struct VolumeForegroundMaskInput {
  VolumeView<const float> mask_source;
  VolumeView<const float> normalization_source;
};

struct VolumeForegroundMaskOutput {
  VolumeView<float> mask;
  VolumeView<float> normalized_volume;
};

[[nodiscard]] bool calculate_volume_foreground_mask(const VolumeForegroundMaskInput& input,
                                                    const VolumeForegroundMaskOutput& output, float threshold_index,
                                                    float normalization_lower, float normalization_upper);

} // namespace ksj::image
