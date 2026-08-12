#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

template <typename T>
void dilate(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_rows,
            std::size_t kernel_cols, BorderMode border_mode,
            StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void dilate(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_rows,
            std::size_t kernel_cols, BorderMode border_mode,
            StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate(const ksj::array::PooledImage<T>& input, std::size_t kernel_rows,
                                                std::size_t kernel_cols, BorderMode border_mode,
                                                StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void erode(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_rows,
           std::size_t kernel_cols, BorderMode border_mode,
           StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void erode(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_rows,
           std::size_t kernel_cols, BorderMode border_mode,
           StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode(const ksj::array::PooledImage<T>& input, std::size_t kernel_rows,
                                               std::size_t kernel_cols, BorderMode border_mode,
                                               StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void morph_open(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_rows,
                std::size_t kernel_cols, BorderMode border_mode,
                StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void morph_open(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_rows,
                std::size_t kernel_cols, BorderMode border_mode,
                StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> morph_open(const ksj::array::PooledImage<T>& input, std::size_t kernel_rows,
                                                    std::size_t kernel_cols, BorderMode border_mode,
                                                    StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void morph_close(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_rows,
                 std::size_t kernel_cols, BorderMode border_mode,
                 StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void morph_close(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_rows,
                 std::size_t kernel_cols, BorderMode border_mode,
                 StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_close(const ksj::array::PooledImage<T>& input, std::size_t kernel_rows, std::size_t kernel_cols,
            BorderMode border_mode, StructuringElementShape shape = StructuringElementShape::rectangle);

template <typename T>
void dilate_cross_value(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, T value);

template <typename T>
void erode_cross_value(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, T value, T background);

template <typename T>
void dilate_threshold_cross(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, T center_threshold,
                            T neighbor_threshold, T value);

template <typename T>
void erode_threshold_cross(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, T center_threshold,
                           T neighbor_threshold, T value);

[[nodiscard]] ksj::array::PooledImage<int> disk_structuring_element(std::size_t radius);

template <typename T>
void dilate_mask(ksj::array::ImageView<const T> input, ksj::array::ImageView<const int> kernel,
                 ksj::array::ImageView<T> output, T value);

template <typename T>
void erode_mask(ksj::array::ImageView<const T> input, ksj::array::ImageView<const int> kernel,
                ksj::array::ImageView<T> output, T background);

void keep_largest_component(ksj::array::ImageView<int> mask, Connectivity connectivity);
} // namespace ksj::image::detail::eigen
