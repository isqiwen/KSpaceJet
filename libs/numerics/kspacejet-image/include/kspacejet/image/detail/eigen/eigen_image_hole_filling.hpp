#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

template <typename T>
void fill_holes(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, HoleFillMode mode, T fill_value);

template <typename T>
void fill_holes(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, HoleFillMode mode,
                T fill_value);
} // namespace ksj::image::detail::eigen
