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

[[nodiscard]] bool write_bgr_image(ksj::array::CubeView<const std::uint8_t> image, const std::string& path);
[[nodiscard]] bool write_jpeg(ksj::array::CubeView<const std::uint8_t> image, const std::string& path);
[[nodiscard]] bool write_image(ksj::array::ImageView<const std::uint8_t> image, const std::string& path);
} // namespace ksj::image::detail::opencv
