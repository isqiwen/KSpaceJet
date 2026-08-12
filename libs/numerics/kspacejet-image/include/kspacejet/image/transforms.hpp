#pragma once

/// Geometric image transforms including flips, rotations, and coordinate-based resampling.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/intel/intel_image_interpolation.hpp"
#include "kspacejet/image/types.hpp"

#include <cmath>
#include <stdexcept>

namespace ksj::image {

// Compatibility path for the former IPP affine rotation used by QRotateCCW.
// Its B-spline kernel uses B=1,C=0, so a zero-degree transform intentionally
// applies separable [1,4,1]/6 smoothing instead of acting as an identity.
template <typename T>
void rotate_cubic_bspline_smooth(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                 const double angle_degrees) {
  detail::validate_image_shape(input, output, "rotate_cubic_bspline_smooth output dimension mismatch");
  if (!std::isfinite(angle_degrees)) {
    throw std::invalid_argument("rotate_cubic_bspline_smooth angle must be finite");
  }
  if (input.empty()) {
    return;
  }

  if (detail::aliases_image_view_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    rotate_cubic_bspline_smooth(input, temp.view(), angle_degrees);
    ksj::array::copy(temp.view(), output);
    return;
  }

  const auto result = detail::intel::rotate_cubic_bspline_smooth(input, output, angle_degrees);
  if (result.status == InterpolationStatus::success) {
    return;
  }

  detail::eigen::rotate_cubic_bspline_smooth(input, output, angle_degrees);
}

template <typename T>
void rotate_cubic_bspline_smooth(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                                 const double angle_degrees) {
  rotate_cubic_bspline_smooth(ksj::array::as_const_view(input.view()), output.view(), angle_degrees);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> rotate_cubic_bspline_smooth(ksj::array::ImageView<const T> input,
                                                                     const double angle_degrees) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  rotate_cubic_bspline_smooth(input, output.view(), angle_degrees);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> rotate_cubic_bspline_smooth(const ksj::array::PooledImage<T>& input,
                                                                     const double angle_degrees) {
  return rotate_cubic_bspline_smooth(ksj::array::as_const_view(input.view()), angle_degrees);
}

template <typename T>
void rotate_cubic(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const double angle_degrees) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    detail::eigen::rotate_cubic(input, temp.view(), angle_degrees);
    ksj::array::copy(temp.view(), output);
    return;
  }

  detail::eigen::rotate_cubic(input, output, angle_degrees);
}

template <typename T>
void rotate_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                  const double angle_degrees) {
  rotate_cubic(ksj::array::as_const_view(input.view()), output.view(), angle_degrees);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> rotate_cubic(ksj::array::ImageView<const T> input,
                                                      const double angle_degrees) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  rotate_cubic(input, output.view(), angle_degrees);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> rotate_cubic(const ksj::array::PooledImage<T>& input,
                                                      const double angle_degrees) {
  return rotate_cubic(ksj::array::as_const_view(input.view()), angle_degrees);
}

} // namespace ksj::image
