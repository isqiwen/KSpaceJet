#pragma once

/// Image denoising algorithms and their strength, neighborhood, and output semantics.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_denoise.hpp"
#include "kspacejet/image/types.hpp"

#include <stdexcept>

namespace ksj::image {

inline void bm3d_denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input,
                                   ksj::array::ImageView<ksj::base::cf32> output, const float sigma) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<ksj::base::cf32>(input.rows(), input.cols());
    if (!detail::opencv::bm3d_denoise_magnitude(input, temp.view(), sigma)) {
      throw std::runtime_error("bm3d_denoise_magnitude OpenCV backend failed");
    }
    ksj::array::copy(temp.view(), output);
    return;
  }

  if (!detail::opencv::bm3d_denoise_magnitude(input, output, sigma)) {
    throw std::runtime_error("bm3d_denoise_magnitude OpenCV backend failed");
  }
}

inline void bm3d_denoise_magnitude(const ksj::array::PooledImage<ksj::base::cf32>& input,
                                   ksj::array::PooledImage<ksj::base::cf32>& output, const float sigma) {
  bm3d_denoise_magnitude(ksj::array::as_const_view(input.view()), output.view(), sigma);
}

[[nodiscard]] inline ksj::array::PooledImage<ksj::base::cf32>
bm3d_denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input, const float sigma) {
  auto output = ksj::array::make_pooled_image<ksj::base::cf32>(input.rows(), input.cols());
  bm3d_denoise_magnitude(input, output.view(), sigma);
  return output;
}

[[nodiscard]] inline ksj::array::PooledImage<ksj::base::cf32>
bm3d_denoise_magnitude(const ksj::array::PooledImage<ksj::base::cf32>& input, const float sigma) {
  return bm3d_denoise_magnitude(ksj::array::as_const_view(input.view()), sigma);
}

void denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input,
                       ksj::array::ImageView<ksj::base::cf32> output, const MagnitudeDenoiseParameters& parameters);

inline void denoise_magnitude(const ksj::array::PooledImage<ksj::base::cf32>& input,
                              ksj::array::PooledImage<ksj::base::cf32>& output,
                              const MagnitudeDenoiseParameters& parameters) {
  denoise_magnitude(ksj::array::as_const_view(input.view()), output.view(), parameters);
}

[[nodiscard]] inline ksj::array::PooledImage<ksj::base::cf32>
denoise_magnitude(ksj::array::ImageView<const ksj::base::cf32> input, const MagnitudeDenoiseParameters& parameters) {
  auto output = ksj::array::make_pooled_image<ksj::base::cf32>(input.rows(), input.cols());
  denoise_magnitude(input, output.view(), parameters);
  return output;
}

[[nodiscard]] inline ksj::array::PooledImage<ksj::base::cf32>
denoise_magnitude(const ksj::array::PooledImage<ksj::base::cf32>& input, const MagnitudeDenoiseParameters& parameters) {
  return denoise_magnitude(ksj::array::as_const_view(input.view()), parameters);
}

} // namespace ksj::image
