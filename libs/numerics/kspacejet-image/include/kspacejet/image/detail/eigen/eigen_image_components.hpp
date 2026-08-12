#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

void validate_connectivity(Connectivity connectivity);

template <typename T>
std::size_t connected_components(ksj::array::ImageView<const T> input,
                                 ksj::array::ImageView<ConnectedComponentLabel> labels,
                                 std::vector<ConnectedComponentStats>* stats, Connectivity connectivity);

template <typename T>
std::size_t connected_components(const ksj::array::PooledImage<T>& input,
                                 ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                                 std::vector<ConnectedComponentStats>* stats, Connectivity connectivity);

template <typename T>
std::size_t connected_components_alias_safe(ksj::array::ImageView<const T> input,
                                            ksj::array::ImageView<ConnectedComponentLabel> labels,
                                            std::vector<ConnectedComponentStats>* stats, Connectivity connectivity);

template <typename T>
[[nodiscard]] ConnectedComponentsResult connected_components(const ksj::array::PooledImage<T>& input,
                                                             Connectivity connectivity);

template <typename T>
std::size_t region_grow(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<RegionGrowMaskValue>& mask,
                        std::size_t seed_row, std::size_t seed_col, T lower_threshold, T upper_threshold,
                        Connectivity connectivity);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<RegionGrowMaskValue>
region_grow(const ksj::array::PooledImage<T>& input, std::size_t seed_row, std::size_t seed_col, T lower_threshold,
            T upper_threshold, Connectivity connectivity);
} // namespace ksj::image::detail::eigen
