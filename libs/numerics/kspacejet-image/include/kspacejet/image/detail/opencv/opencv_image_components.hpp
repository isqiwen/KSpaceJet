#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::image::detail::opencv {

[[nodiscard]] bool connected_components(const ksj::array::PooledImage<float>& input,
                                        ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                                        std::vector<ConnectedComponentStats>* stats, Connectivity connectivity,
                                        std::size_t& component_count);
[[nodiscard]] bool connected_components(const ksj::array::PooledImage<double>& input,
                                        ksj::array::PooledImage<ConnectedComponentLabel>& labels,
                                        std::vector<ConnectedComponentStats>* stats, Connectivity connectivity,
                                        std::size_t& component_count);
template <typename T>
[[nodiscard]] bool connected_components(const ksj::array::PooledImage<T>&,
                                        ksj::array::PooledImage<ConnectedComponentLabel>&,
                                        std::vector<ConnectedComponentStats>*, Connectivity, std::size_t&) {
  return false;
}
} // namespace ksj::image::detail::opencv
