#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

template <typename T>
[[nodiscard]] bool aliases_image_storage(const ksj::array::PooledImage<T>& input,
                                         const ksj::array::PooledImage<T>& output) noexcept;

template <typename T>
void copy_image_storage(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T> void copy_image_view(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output);

template <typename T> void copy_image_view(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output);
} // namespace ksj::image::detail::eigen
