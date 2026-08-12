#pragma once

/// Image resizing and resampling operations with explicit interpolation and output-shape semantics.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/image_policy.hpp"
#include "kspacejet/image/detail/intel/intel_image_interpolation.hpp"
#include "kspacejet/image/detail/opencv/opencv_image_resize.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <stdexcept>

namespace ksj::image {

template <typename T> void resize_nearest(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T> void resize_linear(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T> void resize_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T> void resize_area(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T> void resize_lanczos4(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T>
void resize(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, ResizeMethod method);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_nearest(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                        const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  resize_nearest(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_linear(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                       const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  resize_linear(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_cubic(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                      const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  resize_cubic(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_area(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                     const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  resize_area(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_lanczos4(const ksj::array::PooledImage<T>& input,
                                                         const std::size_t rows, const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  resize_lanczos4(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                const std::size_t cols, const ResizeMethod method) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  resize(input, output, method);
  return output;
}

template <typename T> void resize_nearest(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    resize_nearest(input, temp);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  if (detail::prefer_intel_resize_nearest<T>(input.size(), output.size()) &&
      detail::intel::resize_nearest(input, output)) {
    return;
  }

  if (detail::prefer_opencv_resize_nearest<T>(input.size(), output.size()) &&
      detail::opencv::resize_nearest(input, output)) {
    return;
  }

  detail::eigen::resize_nearest(input, output);
}

template <typename T> void resize_linear(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    resize_linear(input, temp);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  if (detail::prefer_intel_resize_linear<T>(input.size(), output.size()) &&
      detail::intel::resize_linear(input, output)) {
    return;
  }

  if (detail::prefer_opencv_resize_linear<T>(input.size(), output.size()) &&
      detail::opencv::resize_linear(input, output)) {
    return;
  }

  detail::eigen::resize_linear(input, output);
}

template <typename T> void resize_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    resize_cubic(input, temp);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  if (detail::prefer_intel_resize_cubic<T>(input.size(), output.size()) && detail::intel::resize_cubic(input, output)) {
    return;
  }

  if (detail::prefer_opencv_resize_cubic<T>(input.size(), output.size()) &&
      detail::opencv::resize_cubic(input, output)) {
    return;
  }

  detail::eigen::resize_cubic(input, output);
}

template <typename T> void resize_area(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    resize_area(input, temp);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  if (detail::prefer_opencv_resize_area<T>(input.size(), output.size()) && detail::opencv::resize_area(input, output)) {
    return;
  }

  detail::eigen::resize_area(input, output);
}

template <typename T>
void resize_lanczos4(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    resize_lanczos4(input, temp);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  if (detail::prefer_opencv_resize_lanczos4<T>(input.size(), output.size()) &&
      detail::opencv::resize_lanczos4(input, output)) {
    return;
  }

  detail::eigen::resize_lanczos4(input, output);
}

template <typename T>
void resize(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const ResizeMethod method) {
  switch (method) {
    case ResizeMethod::nearest:
      resize_nearest(input, output);
      return;
    case ResizeMethod::linear:
      resize_linear(input, output);
      return;
    case ResizeMethod::cubic:
      resize_cubic(input, output);
      return;
    case ResizeMethod::area:
      resize_area(input, output);
      return;
    case ResizeMethod::lanczos4:
      resize_lanczos4(input, output);
      return;
  }
  throw std::invalid_argument("resize method is invalid");
}

} // namespace ksj::image
