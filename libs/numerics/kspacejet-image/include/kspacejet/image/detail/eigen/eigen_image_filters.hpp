#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ksj::image::detail::eigen {

template <typename T>
void filter2d(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
              ksj::array::PooledImage<T>& output, BorderMode border_mode, FilterAnchor anchor);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> filter2d(const ksj::array::PooledImage<T>& input,
                                                  const ksj::array::PooledImage<T>& kernel, BorderMode border_mode,
                                                  FilterAnchor anchor);

template <typename T, typename KernelT>
void filter3d_replicate(ksj::array::CubeView<T> input_output, ksj::array::CubeView<KernelT> kernel);

template <typename T>
void filter2d_region(ksj::array::ImageView<const T> input, ksj::array::ImageView<const T> kernel,
                     ksj::array::ImageView<T> output, ksj::array::detail::NormalizedSlice rows,
                     ksj::array::detail::NormalizedSlice cols, BorderMode border_mode, FilterAnchor anchor);

template <typename T>
void box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_rows,
                std::size_t kernel_cols, BorderMode border_mode);

template <typename T>
void box_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_rows,
                std::size_t kernel_cols, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(const ksj::array::PooledImage<T>& input, std::size_t kernel_rows,
                                                    std::size_t kernel_cols, BorderMode border_mode);

[[nodiscard]] ksj::array::PooledVector<double> gaussian_kernel(std::size_t kernel_size, double sigma);

void gaussian_blur(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                   std::size_t kernel_size, double sigma, BorderMode border_mode);

void gaussian_blur(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                   std::size_t kernel_size, double sigma, BorderMode border_mode);

template <typename T>
void gaussian_blur(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_size,
                   double sigma, BorderMode border_mode);

template <typename T>
void gaussian_blur(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, std::size_t kernel_size,
                   double sigma, BorderMode border_mode);

template <typename T>
void gaussian_blur(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_size,
                   double sigma, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gaussian_blur(const ksj::array::PooledImage<T>& input, std::size_t kernel_size,
                                                       double sigma, BorderMode border_mode);

void bilateral_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                      std::size_t diameter, double sigma_color, double sigma_space, BorderMode border_mode);

template <typename T>
void bilateral_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t diameter,
                      double sigma_color, double sigma_space, BorderMode border_mode);

template <typename T>
void bilateral_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, std::size_t diameter,
                      double sigma_color, double sigma_space, BorderMode border_mode);

template <typename T>
void bilateral_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t diameter,
                      double sigma_color, double sigma_space, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> bilateral_filter(const ksj::array::PooledImage<T>& input, std::size_t diameter,
                                                          double sigma_color, double sigma_space,
                                                          BorderMode border_mode);

void median_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                   std::size_t kernel_size, BorderMode border_mode);

template <typename T>
void median_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, std::size_t kernel_size,
                   BorderMode border_mode);

template <typename T>
void median_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, std::size_t kernel_size,
                   BorderMode border_mode);

template <typename T>
void median_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, std::size_t kernel_size,
                   BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> median_filter(const ksj::array::PooledImage<T>& input, std::size_t kernel_size,
                                                       BorderMode border_mode);

void median3x3_interior_zero(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output);

void phase_quality_map3x3(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                          float threshold);

template <typename T>
void sobel_x(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, BorderMode border_mode);

template <typename T>
void sobel_x(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_x(const ksj::array::PooledImage<T>& input, BorderMode border_mode);

template <typename T>
void sobel_y(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, BorderMode border_mode);

template <typename T>
void sobel_y(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_y(const ksj::array::PooledImage<T>& input, BorderMode border_mode);

template <typename T>
void gradient_magnitude(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, BorderMode border_mode);

template <typename T>
void gradient_magnitude(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                        BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gradient_magnitude(const ksj::array::PooledImage<T>& input,
                                                            BorderMode border_mode);

template <typename T>
void laplacian(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, BorderMode border_mode);

template <typename T>
void laplacian(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, BorderMode border_mode);

template <typename T>
void laplacian(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> laplacian(const ksj::array::PooledImage<T>& input, BorderMode border_mode);

template <typename T>
void unsharp_mask(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, double amount,
                  std::size_t kernel_size, double sigma, BorderMode border_mode);

template <typename T>
void unsharp_mask(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, double amount,
                  std::size_t kernel_size, double sigma, BorderMode border_mode);

template <typename T>
void unsharp_mask(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, double amount,
                  std::size_t kernel_size, double sigma, BorderMode border_mode);

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unsharp_mask(const ksj::array::PooledImage<T>& input, double amount,
                                                      std::size_t kernel_size, double sigma, BorderMode border_mode);
} // namespace ksj::image::detail::eigen
