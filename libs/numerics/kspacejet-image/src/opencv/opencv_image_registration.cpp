#include "kspacejet/image/detail/opencv/opencv_image_registration.hpp"
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

[[nodiscard]] inline bool align_ecc_euclidean(ksj::array::ImageView<const float> reference,
                                              ksj::array::ImageView<const float> moving,
                                              ksj::array::ImageView<float> aligned,
                                              const EccRegistrationOptions& options) {
  if (reference.empty() || moving.empty() || aligned.empty() || reference.rows() != moving.rows() ||
      reference.cols() != moving.cols() || reference.rows() != aligned.rows() || reference.cols() != aligned.cols() ||
      options.iterations <= 0 || options.epsilon <= 0.0 || !fits_cv_size(reference.rows(), reference.cols())) {
    return false;
  }

  try {
    cv::Mat warp_matrix = cv::Mat::eye(2, 3, CV_32F);
    const cv::TermCriteria criteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, options.iterations,
                                    options.epsilon);
    cv::findTransformECC(as_opencv(reference), as_opencv(moving), warp_matrix, cv::MOTION_EUCLIDEAN, criteria);
    cv::warpAffine(as_opencv(moving), as_opencv(aligned), warp_matrix,
                   cv::Size(static_cast<int>(reference.cols()), static_cast<int>(reference.rows())),
                   cv::INTER_LINEAR + cv::WARP_INVERSE_MAP);
  } catch (const cv::Exception&) {
    return false;
  }

  return true;
}
} // namespace ksj::image::detail::opencv_impl

namespace ksj::image {
namespace detail::opencv {

bool align_ecc_euclidean(ksj::array::ImageView<const float> reference, ksj::array::ImageView<const float> moving,
                         ksj::array::ImageView<float> aligned, const EccRegistrationOptions& options) {
  return detail::opencv_impl::align_ecc_euclidean(reference, moving, aligned, options);
}
} // namespace detail::opencv
} // namespace ksj::image
