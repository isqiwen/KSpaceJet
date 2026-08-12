#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

template <typename T>
[[nodiscard]] T sample_with_border(const ksj::array::PooledImage<T>& input, long row, long col, BorderMode mode,
                                   const T& constant_value);

template <typename T>
[[nodiscard]] T sample_with_border(ksj::array::ImageView<const T> input, long row, long col, BorderMode mode,
                                   const T& constant_value);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> pad(const ksj::array::PooledImage<T>& input, std::size_t top,
                                             std::size_t bottom, std::size_t left, std::size_t right, BorderMode mode,
                                             const T& constant_value);

template <typename T>
void pad(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t top, std::size_t bottom,
         std::size_t left, std::size_t right, BorderMode mode, const T& constant_value);

void validate_region(std::size_t container_rows, std::size_t container_cols, std::size_t row, std::size_t col,
                     std::size_t rows, std::size_t cols, const char* operation_name);

template <typename T>
void crop(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t source_row,
          std::size_t source_col);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> crop(const ksj::array::PooledImage<T>& input, std::size_t source_row,
                                              std::size_t source_col, std::size_t rows, std::size_t cols);

template <typename T> void center_crop(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> center_crop(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                     std::size_t cols);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> center_pad_or_crop(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                            std::size_t cols, const T& constant_value = T{});

template <typename T>
void copy_roi(const ksj::array::PooledImage<T>& source, std::size_t source_row, std::size_t source_col,
              ksj::array::PooledImage<T>& destination, std::size_t destination_row, std::size_t destination_col,
              std::size_t rows, std::size_t cols);
} // namespace ksj::image::detail::eigen
