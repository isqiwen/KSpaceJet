#pragma once

/// Image import/export boundaries that materialize external data into KSpaceJet row-major owners.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_io.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ksj::image {

inline void write_bgr_image(ksj::array::CubeView<const std::uint8_t> image, const std::string& path) {
  if (!detail::opencv::write_bgr_image(image, path)) {
    throw std::runtime_error("write_bgr_image OpenCV backend failed");
  }
}

inline void write_bgr_image(const ksj::array::PooledCube<std::uint8_t>& image, const std::string& path) {
  write_bgr_image(image.view(), path);
}

inline void write_jpeg(ksj::array::CubeView<const std::uint8_t> image, const std::string& path) {
  write_bgr_image(image, path);
}

inline void write_jpeg(const ksj::array::PooledCube<std::uint8_t>& image, const std::string& path) {
  write_bgr_image(image, path);
}

inline void write_image(ksj::array::ImageView<const std::uint8_t> image, const std::string& path) {
  if (!detail::opencv::write_image(image, path)) {
    throw std::runtime_error("write_image OpenCV backend failed");
  }
}

inline void write_image(const ksj::array::PooledImage<std::uint8_t>& image, const std::string& path) {
  write_image(image.view(), path);
}

} // namespace ksj::image
