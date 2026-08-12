#pragma once

/// Explicit conversion helpers at external-layout boundaries; KSpaceJet owners remain row-major.

#include "kspacejet/array/pooled_image.hpp"
#include "kspacejet/array/pooled_matrix.hpp"
#include "kspacejet/memory/allocation_properties.hpp"

#include <cstddef>
#include <utility>

namespace ksj::array {

template <typename T>
[[nodiscard]] PooledMatrix<T> copy_as_matrix(const PooledImage<T>& image,
                                             ksj::memory::AllocationProperties properties = {}) {
  auto matrix = make_pooled_matrix<T>(image.height(), image.width(), std::move(properties));
  for (std::size_t row = 0U; row < image.height(); ++row) {
    for (std::size_t col = 0U; col < image.width(); ++col) {
      matrix(row, col) = image(row, col);
    }
  }
  return matrix;
}

template <typename T>
[[nodiscard]] PooledImage<T> copy_as_image(const PooledMatrix<T>& matrix,
                                           ksj::memory::AllocationProperties properties = {}) {
  auto image = make_pooled_image<T>(matrix.rows(), matrix.cols(), std::move(properties));
  for (std::size_t row = 0U; row < matrix.rows(); ++row) {
    for (std::size_t col = 0U; col < matrix.cols(); ++col) {
      image(row, col) = matrix(row, col);
    }
  }
  return image;
}

} // namespace ksj::array
