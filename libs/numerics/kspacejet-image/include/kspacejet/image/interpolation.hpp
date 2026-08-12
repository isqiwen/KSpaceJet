#pragma once

/// Image interpolation kernels and sampling APIs for continuous-coordinate evaluation.

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/image_policy.hpp"
#include "kspacejet/image/detail/intel/intel_image_interpolation.hpp"
#include "kspacejet/image/detail/intel/intel_image_volume.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_interpolation.hpp"
#include "kspacejet/image/types.hpp"
#include "kspacejet/image/workspace.hpp"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace ksj::image {

[[nodiscard]] inline InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                                      InterpolationAxis axis, float ratio) {
  if (matrix.empty()) {
    return {InterpolationStatus::empty_input, 0};
  }

  if (ratio <= 0.0F || !std::isfinite(ratio)) {
    return {InterpolationStatus::backend_error, 0};
  }

  if (ratio == 1.0F) {
    return {InterpolationStatus::success, 0};
  }

  if (detail::prefer_intel_cubic_interpolate_2d(matrix.size())) {
    const auto result = detail::intel::cubic_interpolate_2d_inplace(matrix, axis, ratio);
    if (result.status != InterpolationStatus::backend_error) {
      return result;
    }
  }

  if (detail::prefer_opencv_cubic_interpolate_2d(matrix.size())) {
    const auto result = detail::opencv::cubic_interpolate_2d_inplace(matrix, axis, ratio);
    if (result.status != InterpolationStatus::backend_error) {
      return result;
    }
  }

  return detail::eigen::cubic_interpolate_2d_inplace(matrix, axis, ratio);
}

[[nodiscard]] inline InterpolationResult cubic_interpolate_2d_inplace(ksj::array::PooledMatrix<ksj::base::cf32>& matrix,
                                                                      InterpolationAxis axis, float ratio) {
  return cubic_interpolate_2d_inplace(matrix.view(), axis, ratio);
}

[[nodiscard]] inline InterpolationResult resize_volume_cubic(ksj::array::CubeView<const ksj::base::cf32> input,
                                                             ksj::array::CubeView<ksj::base::cf32> output,
                                                             ResizeVolumeCubicWorkspace& workspace) {
  return detail::intel::resize_volume_cubic(input, output, workspace);
}

[[nodiscard]] inline InterpolationResult resize_volume_cubic(ksj::array::CubeView<const ksj::base::cf32> input,
                                                             ksj::array::CubeView<ksj::base::cf32> output) {
  ResizeVolumeCubicWorkspace workspace;
  return resize_volume_cubic(input, output, workspace);
}

[[nodiscard]] inline InterpolationResult resize_volume_cubic(const ksj::array::PooledCube<ksj::base::cf32>& input,
                                                             ksj::array::PooledCube<ksj::base::cf32>& output,
                                                             ResizeVolumeCubicWorkspace& workspace) {
  return resize_volume_cubic(input.view(), output.view(), workspace);
}

[[nodiscard]] inline InterpolationResult resize_volume_cubic(const ksj::array::PooledCube<ksj::base::cf32>& input,
                                                             ksj::array::PooledCube<ksj::base::cf32>& output) {
  return resize_volume_cubic(input.view(), output.view());
}

[[nodiscard]] inline ksj::array::PooledCube<ksj::base::cf32>
resize_volume_cubic(const ksj::array::PooledCube<ksj::base::cf32>& input, const std::size_t rows,
                    const std::size_t cols, const std::size_t slices) {
  auto output = ksj::array::make_pooled_cube<ksj::base::cf32>(rows, cols, slices);
  const auto result = resize_volume_cubic(input, output);
  if (result.status != InterpolationStatus::success) {
    throw std::runtime_error("resize_volume_cubic backend failed");
  }
  return output;
}

} // namespace ksj::image
