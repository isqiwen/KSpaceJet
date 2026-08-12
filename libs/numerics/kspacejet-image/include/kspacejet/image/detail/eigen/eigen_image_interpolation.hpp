#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

[[nodiscard]] double resize_scale(std::size_t input_size, std::size_t output_size) noexcept;
[[nodiscard]] std::size_t nearest_resize_index(double coordinate, std::size_t limit) noexcept;

template <typename T> void resize_nearest(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_nearest(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                        std::size_t cols);

template <typename T> void resize_linear(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_linear(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                       std::size_t cols);

[[nodiscard]] double cubic_resize_weight(double offset) noexcept;

template <typename T> void resize_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

void rotate_cubic_bspline_smooth(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 double angle_degrees);

void rotate_cubic_bspline_smooth(ksj::array::ImageView<const ksj::base::cf32> input,
                                 ksj::array::ImageView<ksj::base::cf32> output, double angle_degrees);

template <typename T>
void rotate_cubic_bspline_smooth(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                 double angle_degrees);

void rotate_cubic(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output, double angle_degrees);

void rotate_cubic(ksj::array::ImageView<const ksj::base::cf32> input, ksj::array::ImageView<ksj::base::cf32> output,
                  double angle_degrees);

template <typename T>
void rotate_cubic(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, double angle_degrees);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> rotate_cubic(const ksj::array::PooledImage<T>& input, double angle_degrees);

template <typename T>
void rotate_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, double angle_degrees);

[[nodiscard]] InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                               InterpolationAxis axis, float ratio);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_cubic(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                      std::size_t cols);

[[nodiscard]] double lanczos_sinc(double value) noexcept;
[[nodiscard]] double lanczos4_resize_weight(double offset) noexcept;

template <typename T> void resize_lanczos4(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_lanczos4(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                         std::size_t cols);

[[nodiscard]] double interval_overlap(double begin, double end, double cell_begin, double cell_end) noexcept;

template <typename T> void resize_area(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_area(const ksj::array::PooledImage<T>& input, std::size_t rows,
                                                     std::size_t cols);
} // namespace ksj::image::detail::eigen
