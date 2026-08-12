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

[[nodiscard]] bool align_ecc_euclidean(ksj::array::ImageView<const float> reference,
                                       ksj::array::ImageView<const float> moving, ksj::array::ImageView<float> aligned,
                                       const EccRegistrationOptions& options);
} // namespace ksj::image::detail::opencv
