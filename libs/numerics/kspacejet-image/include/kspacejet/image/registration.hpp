#pragma once

/// Image registration entry points and transform-estimation configuration contracts.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_registration.hpp"
#include "kspacejet/image/types.hpp"

namespace ksj::image {

[[nodiscard]] inline bool align_ecc_euclidean(ksj::array::ImageView<const float> reference,
                                              ksj::array::ImageView<const float> moving,
                                              ksj::array::ImageView<float> aligned,
                                              const EccRegistrationOptions& options = {}) {
  return detail::opencv::align_ecc_euclidean(reference, moving, aligned, options);
}

[[nodiscard]] inline bool align_ecc_euclidean(const ksj::array::PooledImage<float>& reference,
                                              const ksj::array::PooledImage<float>& moving,
                                              ksj::array::PooledImage<float>& aligned,
                                              const EccRegistrationOptions& options = {}) {
  return align_ecc_euclidean(reference.view(), moving.view(), aligned.view(), options);
}

} // namespace ksj::image
