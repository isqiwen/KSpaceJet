#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> threshold(const ksj::array::PooledImage<T>& input, T threshold_value,
                                                   T low_value, T high_value);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> normalize_minmax(const ksj::array::PooledImage<T>& input);

template <typename T> [[nodiscard]] T otsu_threshold(const ksj::array::PooledImage<T>& input);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> otsu_mask(const ksj::array::PooledImage<T>& input, T low_value, T high_value);

[[nodiscard]] std::vector<int> multi_otsu_thresholds_scaled_inplace(ksj::array::ImageView<float> input,
                                                                    std::size_t class_count = 4U,
                                                                    std::size_t gray_levels = 256U);
} // namespace ksj::image::detail::eigen
