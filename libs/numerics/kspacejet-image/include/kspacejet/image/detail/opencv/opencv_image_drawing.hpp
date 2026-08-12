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

[[nodiscard]] bool fill_bgr(ksj::array::CubeView<std::uint8_t> image, BgrColor color);
[[nodiscard]] bool draw_line(ksj::array::CubeView<std::uint8_t> image, ImagePoint start, ImagePoint end, BgrColor color,
                             int thickness);
[[nodiscard]] bool draw_circle(ksj::array::CubeView<std::uint8_t> image, ImagePoint center, int radius, BgrColor color,
                               int thickness);
[[nodiscard]] bool draw_polyline(ksj::array::CubeView<std::uint8_t> image, std::span<const ImagePoint> points,
                                 BgrColor color, int thickness);
[[nodiscard]] TextSize measure_text(std::string_view text, double font_scale, int thickness);
[[nodiscard]] bool draw_text(ksj::array::CubeView<std::uint8_t> image, std::string_view text, ImagePoint origin,
                             double font_scale, BgrColor color, int thickness);
} // namespace ksj::image::detail::opencv
