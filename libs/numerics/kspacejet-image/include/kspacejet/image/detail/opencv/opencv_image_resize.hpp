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

[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<double>& input,
                                  ksj::array::PooledImage<double>& output);
template <typename T>
[[nodiscard]] bool resize_nearest(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

[[nodiscard]] bool resize_linear(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_linear(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output);
template <typename T> [[nodiscard]] bool resize_linear(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

[[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output);
template <typename T> [[nodiscard]] bool resize_cubic(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

[[nodiscard]] bool resize_area(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_area(const ksj::array::PooledImage<double>& input, ksj::array::PooledImage<double>& output);
template <typename T> [[nodiscard]] bool resize_area(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}

[[nodiscard]] bool resize_lanczos4(const ksj::array::PooledImage<float>& input, ksj::array::PooledImage<float>& output);
[[nodiscard]] bool resize_lanczos4(const ksj::array::PooledImage<double>& input,
                                   ksj::array::PooledImage<double>& output);
template <typename T>
[[nodiscard]] bool resize_lanczos4(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&) {
  return false;
}
} // namespace ksj::image::detail::opencv
