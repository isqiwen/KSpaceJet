#pragma once

/// Binary and labeled-image hole-filling operations with explicit connectivity semantics.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_hole_filling.hpp"
#include "kspacejet/image/types.hpp"

namespace ksj::image {

template <typename T>
void fill_holes(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                const HoleFillMode mode = HoleFillMode::exterior, const T fill_value = T{1}) {
  if (detail::aliases_image_view_storage(input, output) && input.rows() == output.rows() &&
      input.cols() == output.cols()) {
    auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
    detail::eigen::fill_holes(input, temp.view(), mode, fill_value);
    ksj::array::copy(temp.view(), output);
    return;
  }

  detail::eigen::fill_holes(input, output, mode, fill_value);
}

template <typename T>
void fill_holes(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const HoleFillMode mode = HoleFillMode::exterior, const T fill_value = T{1}) {
  fill_holes(ksj::array::as_const_view(input.view()), output.view(), mode, fill_value);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> fill_holes(ksj::array::ImageView<const T> input,
                                                    const HoleFillMode mode = HoleFillMode::exterior,
                                                    const T fill_value = T{1}) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  fill_holes(input, output.view(), mode, fill_value);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> fill_holes(const ksj::array::PooledImage<T>& input,
                                                    const HoleFillMode mode = HoleFillMode::exterior,
                                                    const T fill_value = T{1}) {
  return fill_holes(ksj::array::as_const_view(input.view()), mode, fill_value);
}

} // namespace ksj::image
