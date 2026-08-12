#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>

namespace ksj::image::detail::intel {

[[nodiscard]] bool pad(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output, std::size_t top,
                       std::size_t bottom, std::size_t left, std::size_t right, BorderMode mode);

template <typename T>
[[nodiscard]] bool pad(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t, std::size_t,
                       std::size_t, BorderMode) {
  return false;
}

} // namespace ksj::image::detail::intel
