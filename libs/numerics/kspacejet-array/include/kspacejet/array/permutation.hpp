#pragma once

/// Dimension permutation operations that materialize data in the requested row-major order.

#include "kspacejet/array/indexing.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ksj::array {

template <typename T> void reverse_in_place(VectorView<T> data) {
  if (data.is_contiguous()) {
    std::reverse(data.data(), data.data() + data.size());
    return;
  }
  for (std::size_t left = 0, right = data.size(); left < right && left < --right; ++left) {
    std::swap(data[left], data[right]);
  }
}

template <typename T> void rotate_left_in_place(VectorView<T> data, std::size_t offset) {
  if (data.empty()) {
    return;
  }

  offset %= data.size();
  if (offset == 0) {
    return;
  }

  if (data.is_contiguous()) {
    std::rotate(data.data(), data.data() + offset, data.data() + data.size());
    return;
  }

  reverse_in_place(data.subview(slice(0U, offset)));
  reverse_in_place(data.subview(slice(offset, data.size())));
  reverse_in_place(data);
}

} // namespace ksj::array
