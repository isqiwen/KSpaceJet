#pragma once

/// Downsampling and decimation operations for regularly sampled dense arrays.

#include "kspacejet/array/views.hpp"

#include <cstddef>
#include <stdexcept>

namespace ksj::array {

enum class MatrixDecimationAxis {
  rows,
  cols,
};

template <typename T>
[[nodiscard]] MatrixView<T> decimate_blocks_in_place(MatrixView<T> view, const MatrixDecimationAxis axis,
                                                     const std::size_t blocks, const std::size_t points) {
  if (view.empty()) {
    return view;
  }
  if (blocks == 0U) {
    throw std::invalid_argument("matrix decimation block count must be positive");
  }

  if (axis == MatrixDecimationAxis::cols) {
    if (view.cols() % blocks != 0U) {
      throw std::invalid_argument("matrix decimation column count must be divisible by block count");
    }
    const std::size_t segment_length = view.cols() / blocks;
    if (points > segment_length) {
      throw std::invalid_argument("matrix decimation point count exceeds column segment length");
    }
    const std::size_t copy_length = segment_length - points;
    for (std::size_t row = 0U; row < view.rows(); ++row) {
      auto row_view = view.row(row);
      for (std::size_t block = 0U; block < blocks; ++block) {
        const std::size_t src_from = block * segment_length + points / 2U;
        const std::size_t dst_from = block * copy_length;
        for (std::size_t index = 0U; index < copy_length; ++index) {
          row_view[dst_from + index] = row_view[src_from + index];
        }
      }
    }
    return view.subview(_, slice(0U, blocks * copy_length));
  }

  if (view.rows() % blocks != 0U) {
    throw std::invalid_argument("matrix decimation row count must be divisible by block count");
  }
  const std::size_t segment_length = view.rows() / blocks;
  if (points > segment_length) {
    throw std::invalid_argument("matrix decimation point count exceeds row segment length");
  }
  const std::size_t copy_length = segment_length - points;
  for (std::size_t col = 0U; col < view.cols(); ++col) {
    auto col_view = view.col(col);
    for (std::size_t block = 0U; block < blocks; ++block) {
      const std::size_t src_from = block * segment_length + points / 2U;
      const std::size_t dst_from = block * copy_length;
      for (std::size_t index = 0U; index < copy_length; ++index) {
        col_view[dst_from + index] = col_view[src_from + index];
      }
    }
  }
  return view.subview(slice(0U, blocks * copy_length), _);
}

} // namespace ksj::array
