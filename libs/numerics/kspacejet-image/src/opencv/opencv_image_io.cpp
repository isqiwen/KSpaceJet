#include "kspacejet/image/detail/opencv/opencv_image_io.hpp"
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

[[nodiscard]] inline bool write_bgr_image(ksj::array::CubeView<const std::uint8_t> image, const std::string& path) {
  if (!valid_bgr_image(image)) {
    return false;
  }
  return cv::imwrite(path, as_bgr_opencv(image));
}

[[nodiscard]] inline bool write_jpeg(ksj::array::CubeView<const std::uint8_t> image, const std::string& path) {
  return write_bgr_image(image, path);
}

[[nodiscard]] inline bool write_image(ksj::array::ImageView<const std::uint8_t> image, const std::string& path) {
  if (image.empty() || !fits_cv_size(image.rows(), image.cols())) {
    return false;
  }
  return cv::imwrite(path, as_opencv(image));
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool write_bgr_image(ksj::array::CubeView<const std::uint8_t> image, const std::string& path) {
  return detail::opencv_impl::write_bgr_image(image, path);
}

bool write_jpeg(ksj::array::CubeView<const std::uint8_t> image, const std::string& path) {
  return detail::opencv_impl::write_jpeg(image, path);
}

bool write_image(ksj::array::ImageView<const std::uint8_t> image, const std::string& path) {
  return detail::opencv_impl::write_image(image, path);
}
} // namespace detail::opencv
} // namespace ksj::image
