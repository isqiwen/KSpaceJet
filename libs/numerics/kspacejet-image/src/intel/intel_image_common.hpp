#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/image/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include <ipp/ippi.h>

namespace ksj::image::detail::intel {

template <typename T> inline constexpr bool ipp_image_scalar_v = std::is_same_v<T, float>;

[[nodiscard]] inline bool valid_float_roi(ksj::array::ImageView<const float> input,
                                          ksj::array::ImageView<float> output) noexcept {
  return input.rows() == output.rows() && input.cols() == output.cols() && input.rows() > 0U && input.cols() > 0U &&
         input.rows() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
         input.row_stride_bytes() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
         output.row_stride_bytes() <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] inline IppiSize roi_size(ksj::array::ImageView<const float> image) noexcept {
  return IppiSize{static_cast<int>(image.cols()), static_cast<int>(image.rows())};
}

[[nodiscard]] inline IppiSize roi_size(ksj::array::ImageView<float> image) noexcept {
  return IppiSize{static_cast<int>(image.cols()), static_cast<int>(image.rows())};
}

[[nodiscard]] inline bool fits_int(const std::size_t value) noexcept {
  return value <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] inline IppiSize image_size(const std::size_t rows, const std::size_t cols) noexcept {
  return IppiSize{static_cast<int>(cols), static_cast<int>(rows)};
}

[[nodiscard]] inline IppiBorderType border_type(const BorderMode mode) noexcept {
  switch (mode) {
    case BorderMode::constant:
      return ippBorderConst;
    case BorderMode::replicate:
      return ippBorderRepl;
    case BorderMode::reflect:
      return ippBorderMirrorR;
    case BorderMode::reflect101:
      return ippBorderMirror;
  }
  return ippBorderRepl;
}

} // namespace ksj::image::detail::intel
