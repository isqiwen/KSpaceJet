#pragma once

/// Region growing, region extraction, and region-of-interest operations for dense images.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_regions.hpp"
#include "kspacejet/image/detail/intel/intel_image_regions.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>

namespace ksj::image {

template <typename T>
void pad(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t top,
         const std::size_t bottom, const std::size_t left, const std::size_t right,
         const BorderMode mode = BorderMode::constant, const T constant_value = T{}) {
  if (constant_value == T{} && detail::intel::pad(input, output, top, bottom, left, right, mode)) {
    return;
  }
  detail::eigen::pad(input, output, top, bottom, left, right, mode, constant_value);
}

template <typename T>
void pad(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t top,
         const std::size_t bottom, const std::size_t left, const std::size_t right,
         const BorderMode mode = BorderMode::constant, const T constant_value = T{}) {
  pad(ksj::array::as_const_view(input), output, top, bottom, left, right, mode, constant_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
pad(ksj::array::ImageView<const T> input, const std::size_t top, const std::size_t bottom, const std::size_t left,
    const std::size_t right, const BorderMode mode = BorderMode::constant, const T constant_value = T{}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows() + top + bottom, input.cols() + left + right);
  pad(input, output.view(), top, bottom, left, right, mode, constant_value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
pad(ksj::array::ImageView<T> input, const std::size_t top, const std::size_t bottom, const std::size_t left,
    const std::size_t right, const BorderMode mode = BorderMode::constant, const T constant_value = T{}) {
  return pad(ksj::array::as_const_view(input), top, bottom, left, right, mode, constant_value);
}

template <typename T>
void pad(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const std::size_t top,
         const std::size_t bottom, const std::size_t left, const std::size_t right,
         const BorderMode mode = BorderMode::constant, const T constant_value = T{}) {
  pad(ksj::array::as_const_view(input.view()), output.view(), top, bottom, left, right, mode, constant_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
pad(const ksj::array::PooledImage<T>& input, const std::size_t top, const std::size_t bottom, const std::size_t left,
    const std::size_t right, const BorderMode mode = BorderMode::constant, const T constant_value = T{}) {
  return pad(ksj::array::as_const_view(input.view()), top, bottom, left, right, mode, constant_value);
}

template <typename T>
void crop(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const std::size_t source_row,
          const std::size_t source_col) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    detail::eigen::crop(input, temp, source_row, source_col);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  detail::eigen::crop(input, output, source_row, source_col);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> crop(const ksj::array::PooledImage<T>& input, const std::size_t source_row,
                                              const std::size_t source_col, const std::size_t rows,
                                              const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  crop(input, output, source_row, source_col);
  return output;
}

template <typename T> void center_crop(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (detail::eigen::aliases_image_storage(input, output)) {
    auto temp = ksj::array::make_pooled_image<T>(output.rows(), output.cols());
    detail::eigen::center_crop(input, temp);
    detail::eigen::copy_image_storage(temp, output);
    return;
  }

  detail::eigen::center_crop(input, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> center_crop(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                     const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  center_crop(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> center_pad_or_crop(const ksj::array::PooledImage<T>& input,
                                                            const std::size_t rows, const std::size_t cols,
                                                            const T constant_value = T{}) {
  return detail::eigen::center_pad_or_crop(input, rows, cols, constant_value);
}

template <typename T>
void copy_roi(const ksj::array::PooledImage<T>& source, const std::size_t source_row, const std::size_t source_col,
              ksj::array::PooledImage<T>& destination, const std::size_t destination_row,
              const std::size_t destination_col, const std::size_t rows, const std::size_t cols) {
  if (detail::eigen::aliases_image_storage(source, destination)) {
    auto temp = crop(source, source_row, source_col, rows, cols);
    detail::eigen::copy_roi(temp, 0, 0, destination, destination_row, destination_col, rows, cols);
    return;
  }

  detail::eigen::copy_roi(source, source_row, source_col, destination, destination_row, destination_col, rows, cols);
}

} // namespace ksj::image
