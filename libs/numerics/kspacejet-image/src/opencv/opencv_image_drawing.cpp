#include "kspacejet/image/detail/opencv/opencv_image_drawing.hpp"
#include "kspacejet/base/types.hpp"
#include "opencv_image_common.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::image::detail::opencv_impl {

[[nodiscard]] inline bool fill_bgr(ksj::array::CubeView<std::uint8_t> image, const BgrColor color) {
  if (!valid_bgr_image(image)) {
    return false;
  }
  as_bgr_opencv(image).setTo(as_opencv_scalar(color));
  return true;
}

[[nodiscard]] inline bool draw_line(ksj::array::CubeView<std::uint8_t> image, const ImagePoint start,
                                    const ImagePoint end, const BgrColor color, const int thickness) {
  if (!valid_bgr_image(image) || thickness == 0) {
    return false;
  }
  cv::line(as_bgr_opencv(image), cv::Point(start.x, start.y), cv::Point(end.x, end.y), as_opencv_scalar(color),
           thickness, cv::LINE_AA);
  return true;
}

[[nodiscard]] inline bool draw_circle(ksj::array::CubeView<std::uint8_t> image, const ImagePoint center,
                                      const int radius, const BgrColor color, const int thickness) {
  if (!valid_bgr_image(image) || radius < 0 || thickness == 0) {
    return false;
  }
  cv::circle(as_bgr_opencv(image), cv::Point(center.x, center.y), radius, as_opencv_scalar(color), thickness,
             cv::LINE_AA);
  return true;
}

[[nodiscard]] inline bool draw_polyline(ksj::array::CubeView<std::uint8_t> image, std::span<const ImagePoint> points,
                                        const BgrColor color, const int thickness) {
  if (!valid_bgr_image(image) || thickness == 0) {
    return false;
  }
  if (points.size() < 2U) {
    return true;
  }

  auto canvas = as_bgr_opencv(image);
  const auto line_color = as_opencv_scalar(color);
  for (std::size_t i = 1U; i < points.size(); ++i) {
    cv::line(canvas, cv::Point(points[i - 1U].x, points[i - 1U].y), cv::Point(points[i].x, points[i].y), line_color,
             thickness, cv::LINE_AA);
  }
  return true;
}

[[nodiscard]] inline TextSize measure_text(std::string_view text, const double font_scale, const int thickness) {
  int baseline = 0;
  const auto size = cv::getTextSize(std::string(text), cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
  return {size.width, size.height, baseline};
}

[[nodiscard]] inline bool draw_text(ksj::array::CubeView<std::uint8_t> image, std::string_view text,
                                    const ImagePoint origin, const double font_scale, const BgrColor color,
                                    const int thickness) {
  if (!valid_bgr_image(image) || font_scale <= 0.0 || thickness <= 0) {
    return false;
  }
  cv::putText(as_bgr_opencv(image), std::string(text), cv::Point(origin.x, origin.y), cv::FONT_HERSHEY_SIMPLEX,
              font_scale, as_opencv_scalar(color), thickness, cv::LINE_AA);
  return true;
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool fill_bgr(ksj::array::CubeView<std::uint8_t> image, const BgrColor color) {
  return detail::opencv_impl::fill_bgr(image, color);
}

bool draw_line(ksj::array::CubeView<std::uint8_t> image, const ImagePoint start, const ImagePoint end,
               const BgrColor color, const int thickness) {
  return detail::opencv_impl::draw_line(image, start, end, color, thickness);
}

bool draw_circle(ksj::array::CubeView<std::uint8_t> image, const ImagePoint center, const int radius,
                 const BgrColor color, const int thickness) {
  return detail::opencv_impl::draw_circle(image, center, radius, color, thickness);
}

bool draw_polyline(ksj::array::CubeView<std::uint8_t> image, std::span<const ImagePoint> points, const BgrColor color,
                   const int thickness) {
  return detail::opencv_impl::draw_polyline(image, points, color, thickness);
}

TextSize measure_text(std::string_view text, const double font_scale, const int thickness) {
  return detail::opencv_impl::measure_text(text, font_scale, thickness);
}

bool draw_text(ksj::array::CubeView<std::uint8_t> image, std::string_view text, const ImagePoint origin,
               const double font_scale, const BgrColor color, const int thickness) {
  return detail::opencv_impl::draw_text(image, text, origin, font_scale, color, thickness);
}
} // namespace detail::opencv
} // namespace ksj::image
