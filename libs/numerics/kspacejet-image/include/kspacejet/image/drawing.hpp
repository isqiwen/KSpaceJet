#pragma once

/// Explicit in-place drawing primitives for raster images and volume slices.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_drawing.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace ksj::image {

inline void fill_bgr(ksj::array::CubeView<std::uint8_t> image, const BgrColor color) {
  if (!detail::opencv::fill_bgr(image, color)) {
    throw std::runtime_error("fill_bgr OpenCV backend failed");
  }
}

inline void fill_bgr(ksj::array::PooledCube<std::uint8_t>& image, const BgrColor color) {
  fill_bgr(image.view(), color);
}

[[nodiscard]] inline ksj::array::PooledCube<std::uint8_t> make_bgr_image(const std::size_t rows, const std::size_t cols,
                                                                         const BgrColor color = {0U, 0U, 0U}) {
  auto image = ksj::array::make_pooled_cube<std::uint8_t>(rows, cols, 3U);
  fill_bgr(image, color);
  return image;
}

inline void draw_line(ksj::array::CubeView<std::uint8_t> image, const ImagePoint start, const ImagePoint end,
                      const BgrColor color, const int thickness = 1) {
  if (!detail::opencv::draw_line(image, start, end, color, thickness)) {
    throw std::runtime_error("draw_line OpenCV backend failed");
  }
}

inline void draw_line(ksj::array::PooledCube<std::uint8_t>& image, const ImagePoint start, const ImagePoint end,
                      const BgrColor color, const int thickness = 1) {
  draw_line(image.view(), start, end, color, thickness);
}

inline void draw_circle(ksj::array::CubeView<std::uint8_t> image, const ImagePoint center, const int radius,
                        const BgrColor color, const int thickness = 1) {
  if (!detail::opencv::draw_circle(image, center, radius, color, thickness)) {
    throw std::runtime_error("draw_circle OpenCV backend failed");
  }
}

inline void draw_circle(ksj::array::PooledCube<std::uint8_t>& image, const ImagePoint center, const int radius,
                        const BgrColor color, const int thickness = 1) {
  draw_circle(image.view(), center, radius, color, thickness);
}

inline void draw_polyline(ksj::array::CubeView<std::uint8_t> image, std::span<const ImagePoint> points,
                          const BgrColor color, const int thickness = 1) {
  if (!detail::opencv::draw_polyline(image, points, color, thickness)) {
    throw std::runtime_error("draw_polyline OpenCV backend failed");
  }
}

inline void draw_polyline(ksj::array::PooledCube<std::uint8_t>& image, std::span<const ImagePoint> points,
                          const BgrColor color, const int thickness = 1) {
  draw_polyline(image.view(), points, color, thickness);
}

[[nodiscard]] inline TextSize measure_text(std::string_view text, const double font_scale, const int thickness = 1) {
  return detail::opencv::measure_text(text, font_scale, thickness);
}

inline void draw_text(ksj::array::CubeView<std::uint8_t> image, std::string_view text, const ImagePoint origin,
                      const double font_scale, const BgrColor color, const int thickness = 1) {
  if (!detail::opencv::draw_text(image, text, origin, font_scale, color, thickness)) {
    throw std::runtime_error("draw_text OpenCV backend failed");
  }
}

inline void draw_text(ksj::array::PooledCube<std::uint8_t>& image, std::string_view text, const ImagePoint origin,
                      const double font_scale, const BgrColor color, const int thickness = 1) {
  draw_text(image.view(), text, origin, font_scale, color, thickness);
}

} // namespace ksj::image
