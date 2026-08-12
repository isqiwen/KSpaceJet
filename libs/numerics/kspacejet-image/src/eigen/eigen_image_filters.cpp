#include "kspacejet/base/types.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/image/detail/eigen/eigen_image_basic.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_thresholds.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_hole_filling.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_regions.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_components.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_interpolation.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_filters.hpp"
#include "kspacejet/image/detail/eigen/eigen_image_morphology.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Eigen/Core>

namespace ksj::image::detail::eigen {
using ksj::array::detail::eigen_adapter::as_eigen;

template <typename T>
void filter2d(const ksj::array::PooledImage<T>& input, const ksj::array::PooledImage<T>& kernel,
              ksj::array::PooledImage<T>& output, const BorderMode border_mode, const FilterAnchor anchor) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("filter2d output dimension mismatch");
  }
  if (kernel.empty()) {
    throw std::invalid_argument("filter2d kernel must not be empty");
  }
  if (input.empty()) {
    return;
  }

  const auto anchor_row = anchor == FilterAnchor::top_left ? 0U : kernel.rows() / 2U;
  const auto anchor_col = anchor == FilterAnchor::top_left ? 0U : kernel.cols() / 2U;
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      double sum = 0.0;
      for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
        const auto source_row = static_cast<long>(row) + static_cast<long>(kernel_row) - static_cast<long>(anchor_row);
        for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
          const auto source_col =
            static_cast<long>(col) + static_cast<long>(kernel_col) - static_cast<long>(anchor_col);
          sum += static_cast<double>(sample_with_border(input, source_row, source_col, border_mode, T{})) *
                 static_cast<double>(kernel(kernel_row, kernel_col));
        }
      }
      output(row, col) = static_cast<T>(sum);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> filter2d(const ksj::array::PooledImage<T>& input,
                                                  const ksj::array::PooledImage<T>& kernel,
                                                  const BorderMode border_mode, const FilterAnchor anchor) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::filter2d(input, kernel, output, border_mode, anchor);
  return output;
}

template <typename T>
[[nodiscard]] T sample_cube_replicate(const ksj::array::CubeView<T> input, long i0, long i1, long i2) {
  i0 = std::clamp(i0, 0L, static_cast<long>(input.dim0()) - 1L);
  i1 = std::clamp(i1, 0L, static_cast<long>(input.dim1()) - 1L);
  i2 = std::clamp(i2, 0L, static_cast<long>(input.dim2()) - 1L);
  return input(static_cast<std::size_t>(i0), static_cast<std::size_t>(i1), static_cast<std::size_t>(i2));
}

template <typename T, typename KernelT>
void filter3d_replicate(ksj::array::CubeView<T> input_output, const ksj::array::CubeView<KernelT> kernel) {
  if (kernel.empty()) {
    throw std::invalid_argument("filter3d_replicate kernel must not be empty");
  }
  if (input_output.empty()) {
    return;
  }

  using value_type = std::remove_const_t<T>;
  using kernel_type = std::remove_const_t<KernelT>;
  using sum_type = decltype(std::declval<value_type>() * std::declval<kernel_type>());

  const auto source = ksj::array::make_pooled_cube(ksj::array::as_const_view(input_output));
  const auto anchor_dim0 = static_cast<long>(kernel.dim0() / 2U);
  const auto anchor_dim1 = static_cast<long>(kernel.dim1() / 2U);
  const auto anchor_dim2 = static_cast<long>(kernel.dim2() / 2U);

  for (std::size_t i0 = 0U; i0 < input_output.dim0(); ++i0) {
    for (std::size_t i1 = 0U; i1 < input_output.dim1(); ++i1) {
      for (std::size_t i2 = 0U; i2 < input_output.dim2(); ++i2) {
        sum_type sum{};
        for (std::size_t kernel_i0 = 0U; kernel_i0 < kernel.dim0(); ++kernel_i0) {
          const auto source_i0 = static_cast<long>(i0) + static_cast<long>(kernel_i0) - anchor_dim0;
          for (std::size_t kernel_i1 = 0U; kernel_i1 < kernel.dim1(); ++kernel_i1) {
            const auto source_i1 = static_cast<long>(i1) + static_cast<long>(kernel_i1) - anchor_dim1;
            for (std::size_t kernel_i2 = 0U; kernel_i2 < kernel.dim2(); ++kernel_i2) {
              const auto source_i2 = static_cast<long>(i2) + static_cast<long>(kernel_i2) - anchor_dim2;
              sum += sample_cube_replicate(source.view(), source_i0, source_i1, source_i2) *
                     kernel(kernel_i0, kernel_i1, kernel_i2);
            }
          }
        }
        input_output(i0, i1, i2) = static_cast<value_type>(sum);
      }
    }
  }
}

template <typename T>
void filter2d_region(const ksj::array::ImageView<const T> input, const ksj::array::ImageView<const T> kernel,
                     ksj::array::ImageView<T> output, const ksj::array::detail::NormalizedSlice rows,
                     const ksj::array::detail::NormalizedSlice cols, const BorderMode border_mode,
                     const FilterAnchor anchor) {
  if (rows.step != 1U || cols.step != 1U) {
    throw std::invalid_argument("filter2d_region does not support strided regions");
  }
  validate_region(input.rows(), input.cols(), rows.start, cols.start, rows.count, cols.count, "filter2d_region input");
  validate_region(output.rows(), output.cols(), rows.start, cols.start, rows.count, cols.count,
                  "filter2d_region output");
  if (kernel.empty()) {
    throw std::invalid_argument("filter2d_region kernel must not be empty");
  }
  if (rows.count == 0 || cols.count == 0) {
    return;
  }

  const auto anchor_row = anchor == FilterAnchor::top_left ? 0U : kernel.rows() / 2U;
  const auto anchor_col = anchor == FilterAnchor::top_left ? 0U : kernel.cols() / 2U;
  for (std::size_t row = rows.start; row < rows.start + rows.count; ++row) {
    for (std::size_t col = cols.start; col < cols.start + cols.count; ++col) {
      double sum = 0.0;
      for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
        const auto source_row = static_cast<long>(row) + static_cast<long>(kernel_row) - static_cast<long>(anchor_row);
        for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
          const auto source_col =
            static_cast<long>(col) + static_cast<long>(kernel_col) - static_cast<long>(anchor_col);
          sum += static_cast<double>(sample_with_border(input, source_row, source_col, border_mode, T{})) *
                 static_cast<double>(kernel(kernel_row, kernel_col));
        }
      }
      output(row, col) = static_cast<T>(sum);
    }
  }
}

template <typename T>
void box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode);

template <typename T>
void box_filter(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode) {
  if (kernel_rows == 0 || kernel_cols == 0) {
    throw std::invalid_argument("box_filter kernel dimensions must be positive");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("box_filter output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const auto row_before = static_cast<long>(kernel_rows / 2U);
  const auto row_after = static_cast<long>(kernel_rows - kernel_rows / 2U - 1U);
  const auto col_before = static_cast<long>(kernel_cols / 2U);
  const auto col_after = static_cast<long>(kernel_cols - kernel_cols / 2U - 1U);
  const auto divisor = static_cast<double>(kernel_rows * kernel_cols);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      T sum{};
      for (long dy = -row_before; dy <= row_after; ++dy) {
        for (long dx = -col_before; dx <= col_after; ++dx) {
          sum += sample_with_border(input, static_cast<long>(row) + dy, static_cast<long>(col) + dx, border_mode, T{});
        }
      }
      output(row, col) = static_cast<T>(static_cast<double>(sum) / divisor);
    }
  }
}

template <typename T>
void box_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::box_filter(input.view(), output.view(), kernel_rows, kernel_cols, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> box_filter(const ksj::array::PooledImage<T>& input,
                                                    const std::size_t kernel_rows, const std::size_t kernel_cols,
                                                    const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::box_filter(input, output, kernel_rows, kernel_cols, border_mode);
  return output;
}

[[nodiscard]] ksj::array::PooledVector<double> gaussian_kernel(const std::size_t kernel_size, const double sigma) {
  if (kernel_size == 0 || kernel_size % 2U == 0) {
    throw std::invalid_argument("gaussian_blur kernel size must be a positive odd value");
  }
  if (sigma <= 0.0) {
    throw std::invalid_argument("gaussian_blur sigma must be positive");
  }

  auto kernel = ksj::array::make_pooled_vector<double>(kernel_size);
  const auto radius = static_cast<long>(kernel_size / 2U);
  const auto sigma_scale = 2.0 * sigma * sigma;
  double total = 0.0;
  for (std::size_t index = 0; index < kernel_size; ++index) {
    const auto offset = static_cast<long>(index) - radius;
    const auto value = std::exp(-static_cast<double>(offset * offset) / sigma_scale);
    kernel(index) = value;
    total += value;
  }
  for (std::size_t index = 0; index < kernel.size(); ++index) {
    kernel(index) /= total;
  }
  return kernel;
}

[[nodiscard]] inline double sample_buffer_with_border(ksj::array::ImageView<const double> values, long row, long col,
                                                      const BorderMode mode) {
  const auto rows = values.rows();
  const auto cols = values.cols();
  const auto row_count = static_cast<long>(rows);
  const auto col_count = static_cast<long>(cols);
  if (row >= 0 && row < row_count && col >= 0 && col < col_count) {
    return values(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
  }

  if (mode == BorderMode::constant || rows == 0 || cols == 0) {
    return 0.0;
  }

  if (mode == BorderMode::replicate) {
    row = std::clamp(row, 0L, row_count - 1L);
    col = std::clamp(col, 0L, col_count - 1L);
    return values(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
  }

  if (mode == BorderMode::reflect101) {
    const auto reflect101_index = [](long index, const long size) {
      if (size <= 1) {
        return 0L;
      }
      while (index < 0 || index >= size) {
        index = index < 0 ? -index : 2L * size - index - 2L;
      }
      return index;
    };
    row = reflect101_index(row, row_count);
    col = reflect101_index(col, col_count);
    return values(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
  }

  const auto reflect_index = [](long index, const long size) {
    if (size <= 1) {
      return 0L;
    }
    while (index < 0 || index >= size) {
      index = index < 0 ? -index - 1L : 2L * size - index - 1L;
    }
    return index;
  };

  row = reflect_index(row, row_count);
  col = reflect_index(col, col_count);
  return values(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
}

template <typename T>
void gaussian_blur(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size,
                   const double sigma, const BorderMode border_mode) {
  const auto kernel = gaussian_kernel(kernel_size, sigma);
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("gaussian_blur output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const auto radius = static_cast<long>(kernel_size / 2U);
  auto temp = ksj::array::make_pooled_image<double>(input.rows(), input.cols());
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      double sum = 0.0;
      for (long dx = -radius; dx <= radius; ++dx) {
        sum += static_cast<double>(
                 sample_with_border(input, static_cast<long>(row), static_cast<long>(col) + dx, border_mode, T{})) *
               kernel(static_cast<std::size_t>(dx + radius));
      }
      temp(row, col) = sum;
    }
  }

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      double sum = 0.0;
      for (long dy = -radius; dy <= radius; ++dy) {
        sum += sample_buffer_with_border(ksj::array::as_const_view(temp.view()), static_cast<long>(row) + dy,
                                         static_cast<long>(col), border_mode) *
               kernel(static_cast<std::size_t>(dy + radius));
      }
      output(row, col) = static_cast<T>(sum);
    }
  }
}

template <typename T>
void gaussian_blur(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size,
                   const double sigma, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::gaussian_blur(ksj::array::as_const_view(input), output, kernel_size, sigma, border_mode);
}

template <typename T>
void gaussian_blur(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                   const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::gaussian_blur(input.view(), output.view(), kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gaussian_blur(const ksj::array::PooledImage<T>& input,
                                                       const std::size_t kernel_size, const double sigma,
                                                       const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::gaussian_blur(input.view(), output.view(), kernel_size, sigma, border_mode);
  return output;
}

template <typename T>
void bilateral_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t diameter,
                      const double sigma_color, const double sigma_space, const BorderMode border_mode) {
  if (diameter == 0 || diameter % 2U == 0) {
    throw std::invalid_argument("bilateral_filter diameter must be a positive odd value");
  }
  if (sigma_color <= 0.0 || sigma_space <= 0.0) {
    throw std::invalid_argument("bilateral_filter sigmas must be positive");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("bilateral_filter output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const auto radius = static_cast<long>(diameter / 2U);
  const auto radius2 = radius * radius;
  const auto spatial_scale = 2.0 * sigma_space * sigma_space;
  const auto range_scale = 2.0 * sigma_color * sigma_color;
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      const auto center = static_cast<double>(input(row, col));
      double weighted_sum = 0.0;
      double weight_sum = 0.0;
      for (long dy = -radius; dy <= radius; ++dy) {
        for (long dx = -radius; dx <= radius; ++dx) {
          const auto spatial_distance2 = dy * dy + dx * dx;
          if (spatial_distance2 > radius2) {
            continue;
          }
          const auto sample = static_cast<double>(
            sample_with_border(input, static_cast<long>(row) + dy, static_cast<long>(col) + dx, border_mode, T{}));
          const auto spatial_weight = std::exp(-static_cast<double>(spatial_distance2) / spatial_scale);
          const auto range_delta = sample - center;
          const auto range_weight = std::exp(-(range_delta * range_delta) / range_scale);
          const auto weight = spatial_weight * range_weight;
          weighted_sum += sample * weight;
          weight_sum += weight;
        }
      }
      output(row, col) = static_cast<T>(weight_sum > 0.0 ? weighted_sum / weight_sum : center);
    }
  }
}

template <typename T>
void bilateral_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t diameter,
                      const double sigma_color, const double sigma_space, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::bilateral_filter(ksj::array::as_const_view(input), output, diameter, sigma_color,
                                                sigma_space, border_mode);
}

template <typename T>
void bilateral_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                      const std::size_t diameter, const double sigma_color, const double sigma_space,
                      const BorderMode border_mode) {
  ::ksj::image::detail::eigen::bilateral_filter(input.view(), output.view(), diameter, sigma_color, sigma_space,
                                                border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> bilateral_filter(const ksj::array::PooledImage<T>& input,
                                                          const std::size_t diameter, const double sigma_color,
                                                          const double sigma_space, const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::bilateral_filter(input.view(), output.view(), diameter, sigma_color, sigma_space,
                                                border_mode);
  return output;
}

template <typename T>
void median_filter(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size,
                   const BorderMode border_mode) {
  if (kernel_size == 0 || kernel_size % 2U == 0) {
    throw std::invalid_argument("median_filter kernel size must be a positive odd value");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("median_filter output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const auto radius = static_cast<long>(kernel_size / 2U);
  auto window = ksj::array::make_pooled_vector<T>(kernel_size * kernel_size);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      std::size_t window_size = 0U;
      for (long dy = -radius; dy <= radius; ++dy) {
        for (long dx = -radius; dx <= radius; ++dx) {
          window(window_size++) =
            sample_with_border(input, static_cast<long>(row) + dy, static_cast<long>(col) + dx, border_mode, T{});
        }
      }
      const auto median_index = window.data() + static_cast<std::ptrdiff_t>(window_size / 2U);
      std::nth_element(window.data(), median_index, window.data() + window_size);
      output(row, col) = *median_index;
    }
  }
}

template <typename T>
void median_filter(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const std::size_t kernel_size,
                   const BorderMode border_mode) {
  ::ksj::image::detail::eigen::median_filter(ksj::array::as_const_view(input), output, kernel_size, border_mode);
}

template <typename T>
void median_filter(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                   const std::size_t kernel_size, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::median_filter(input.view(), output.view(), kernel_size, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> median_filter(const ksj::array::PooledImage<T>& input,
                                                       const std::size_t kernel_size, const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::median_filter(input.view(), output.view(), kernel_size, border_mode);
  return output;
}

void median3x3_interior_zero(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("median3x3_interior_zero output dimension mismatch");
  }

  ksj::array::fill(output, 0.0F);
  if (input.rows() < 3U || input.cols() < 3U) {
    return;
  }

  std::array<float, 9U> window{};
  for (std::size_t row = 1U; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1U; col + 1U < input.cols(); ++col) {
      window[0] = input(row, col);
      window[1] = input(row - 1U, col);
      window[2] = input(row + 1U, col);
      window[3] = input(row, col - 1U);
      window[4] = input(row - 1U, col - 1U);
      window[5] = input(row + 1U, col - 1U);
      window[6] = input(row, col + 1U);
      window[7] = input(row - 1U, col + 1U);
      window[8] = input(row + 1U, col + 1U);
      std::nth_element(window.begin(), window.begin() + 4, window.end());
      output(row, col) = window[4];
    }
  }
}

void phase_quality_map3x3(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                          const float threshold) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("phase_quality_map3x3 output dimension mismatch");
  }

  ksj::array::fill(output, 0.0F);
  if (input.rows() < 3U || input.cols() < 3U) {
    return;
  }

  for (std::size_t row = 1U; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1U; col + 1U < input.cols(); ++col) {
      float cos_sum = 0.0F;
      float sin_sum = 0.0F;
      for (std::size_t sample_row = row - 1U; sample_row <= row + 1U; ++sample_row) {
        for (std::size_t sample_col = col - 1U; sample_col <= col + 1U; ++sample_col) {
          cos_sum += std::cos(input(sample_row, sample_col));
          sin_sum += std::sin(input(sample_row, sample_col));
        }
      }
      const auto quality = std::sqrt(cos_sum * cos_sum + sin_sum * sin_sum) / 81.0F;
      output(row, col) = quality < threshold ? 0.0F : 1.0F;
    }
  }
}

template <typename T>
void sobel_components_at(const ksj::array::ImageView<const T> input, const std::size_t row, const std::size_t col,
                         const BorderMode border_mode, double& gx, double& gy) {
  const auto top_left = static_cast<double>(
    sample_with_border(input, static_cast<long>(row) - 1L, static_cast<long>(col) - 1L, border_mode, T{}));
  const auto top = static_cast<double>(
    sample_with_border(input, static_cast<long>(row) - 1L, static_cast<long>(col), border_mode, T{}));
  const auto top_right = static_cast<double>(
    sample_with_border(input, static_cast<long>(row) - 1L, static_cast<long>(col) + 1L, border_mode, T{}));
  const auto left = static_cast<double>(
    sample_with_border(input, static_cast<long>(row), static_cast<long>(col) - 1L, border_mode, T{}));
  const auto right = static_cast<double>(
    sample_with_border(input, static_cast<long>(row), static_cast<long>(col) + 1L, border_mode, T{}));
  const auto bottom_left = static_cast<double>(
    sample_with_border(input, static_cast<long>(row) + 1L, static_cast<long>(col) - 1L, border_mode, T{}));
  const auto bottom = static_cast<double>(
    sample_with_border(input, static_cast<long>(row) + 1L, static_cast<long>(col), border_mode, T{}));
  const auto bottom_right = static_cast<double>(
    sample_with_border(input, static_cast<long>(row) + 1L, static_cast<long>(col) + 1L, border_mode, T{}));

  gx = -top_left + top_right - 2.0 * left + 2.0 * right - bottom_left + bottom_right;
  gy = -top_left - 2.0 * top - top_right + bottom_left + 2.0 * bottom + bottom_right;
}

template <typename T>
void sobel_x(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
             const BorderMode border_mode) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("sobel_x output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      double gx = 0.0;
      double gy = 0.0;
      sobel_components_at(input, row, col, border_mode, gx, gy);
      (void)gy;
      output(row, col) = static_cast<T>(gx);
    }
  }
}

template <typename T>
void sobel_x(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
             const BorderMode border_mode) {
  ::ksj::image::detail::eigen::sobel_x(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_x(const ksj::array::PooledImage<T>& input,
                                                 const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::sobel_x(input, output, border_mode);
  return output;
}

template <typename T>
void sobel_y(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
             const BorderMode border_mode) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("sobel_y output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      double gx = 0.0;
      double gy = 0.0;
      sobel_components_at(input, row, col, border_mode, gx, gy);
      (void)gx;
      output(row, col) = static_cast<T>(gy);
    }
  }
}

template <typename T>
void sobel_y(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
             const BorderMode border_mode) {
  ::ksj::image::detail::eigen::sobel_y(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> sobel_y(const ksj::array::PooledImage<T>& input,
                                                 const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::sobel_y(input, output, border_mode);
  return output;
}

template <typename T>
void gradient_magnitude(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                        const BorderMode border_mode) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("gradient_magnitude output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      double gx = 0.0;
      double gy = 0.0;
      sobel_components_at(input, row, col, border_mode, gx, gy);
      output(row, col) = static_cast<T>(std::hypot(gx, gy));
    }
  }
}

template <typename T>
void gradient_magnitude(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                        const BorderMode border_mode) {
  ::ksj::image::detail::eigen::gradient_magnitude(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> gradient_magnitude(const ksj::array::PooledImage<T>& input,
                                                            const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::gradient_magnitude(input, output, border_mode);
  return output;
}

template <typename T>
void laplacian(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const BorderMode border_mode) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("laplacian output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      const auto center = static_cast<double>(input(row, col));
      const auto top = static_cast<double>(
        sample_with_border(input, static_cast<long>(row) - 1L, static_cast<long>(col), border_mode, T{}));
      const auto bottom = static_cast<double>(
        sample_with_border(input, static_cast<long>(row) + 1L, static_cast<long>(col), border_mode, T{}));
      const auto left = static_cast<double>(
        sample_with_border(input, static_cast<long>(row), static_cast<long>(col) - 1L, border_mode, T{}));
      const auto right = static_cast<double>(
        sample_with_border(input, static_cast<long>(row), static_cast<long>(col) + 1L, border_mode, T{}));
      output(row, col) = static_cast<T>(top + bottom + left + right - 4.0 * center);
    }
  }
}

template <typename T>
void laplacian(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::laplacian(ksj::array::as_const_view(input), output, border_mode);
}

template <typename T>
void laplacian(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
               const BorderMode border_mode) {
  ::ksj::image::detail::eigen::laplacian(input.view(), output.view(), border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> laplacian(const ksj::array::PooledImage<T>& input,
                                                   const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::laplacian(input.view(), output.view(), border_mode);
  return output;
}

template <typename T>
void unsharp_mask(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const double amount,
                  const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  if (amount < 0.0) {
    throw std::invalid_argument("unsharp_mask amount must be non-negative");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("unsharp_mask output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  auto blurred = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::gaussian_blur(input, blurred.view(), kernel_size, sigma, border_mode);
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      const auto value = static_cast<double>(input(row, col));
      output(row, col) = static_cast<T>(value + amount * (value - static_cast<double>(blurred(row, col))));
    }
  }
}

template <typename T>
void unsharp_mask(ksj::array::ImageView<T> input, ksj::array::ImageView<T> output, const double amount,
                  const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::unsharp_mask(ksj::array::as_const_view(input), output, amount, kernel_size, sigma,
                                            border_mode);
}

template <typename T>
void unsharp_mask(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const double amount,
                  const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::unsharp_mask(input.view(), output.view(), amount, kernel_size, sigma, border_mode);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> unsharp_mask(const ksj::array::PooledImage<T>& input, const double amount,
                                                      const std::size_t kernel_size, const double sigma,
                                                      const BorderMode border_mode) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::unsharp_mask(input.view(), output.view(), amount, kernel_size, sigma, border_mode);
  return output;
}

void gaussian_blur(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                   const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::gaussian_blur<float>(input, output, kernel_size, sigma, border_mode);
}

void gaussian_blur(ksj::array::ImageView<const double> input, ksj::array::ImageView<double> output,
                   const std::size_t kernel_size, const double sigma, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::gaussian_blur<double>(input, output, kernel_size, sigma, border_mode);
}

void bilateral_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                      const std::size_t diameter, const double sigma_color, const double sigma_space,
                      const BorderMode border_mode) {
  ::ksj::image::detail::eigen::bilateral_filter<float>(input, output, diameter, sigma_color, sigma_space, border_mode);
}

void median_filter(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                   const std::size_t kernel_size, const BorderMode border_mode) {
  ::ksj::image::detail::eigen::median_filter<float>(input, output, kernel_size, border_mode);
}

#define KSJ_IMAGE_INSTANTIATE_FILTERS(T)                                                                               \
  template void filter2d<T>(const ksj::array::PooledImage<T>&, const ksj::array::PooledImage<T>&,                      \
                            ksj::array::PooledImage<T>&, BorderMode, FilterAnchor);                                    \
  template ksj::array::PooledImage<T> filter2d<T>(const ksj::array::PooledImage<T>&,                                   \
                                                  const ksj::array::PooledImage<T>&, BorderMode, FilterAnchor);        \
  template void filter2d_region<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<const T>,                     \
                                   ksj::array::ImageView<T>, ksj::array::detail::NormalizedSlice,                      \
                                   ksj::array::detail::NormalizedSlice, BorderMode, FilterAnchor);                     \
  template void box_filter<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,      \
                              BorderMode);                                                                             \
  template void box_filter<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t,             \
                              std::size_t, BorderMode);                                                                \
  template ksj::array::PooledImage<T> box_filter<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t,       \
                                                    BorderMode);                                                       \
  template void gaussian_blur<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, double,        \
                                 BorderMode);                                                                          \
  template void gaussian_blur<T>(ksj::array::ImageView<T>, ksj::array::ImageView<T>, std::size_t, double, BorderMode); \
  template void gaussian_blur<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t, double,  \
                                 BorderMode);                                                                          \
  template ksj::array::PooledImage<T> gaussian_blur<T>(const ksj::array::PooledImage<T>&, std::size_t, double,         \
                                                       BorderMode);                                                    \
  template void bilateral_filter<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, double,     \
                                    double, BorderMode);                                                               \
  template void bilateral_filter<T>(ksj::array::ImageView<T>, ksj::array::ImageView<T>, std::size_t, double, double,   \
                                    BorderMode);                                                                       \
  template void bilateral_filter<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t,       \
                                    double, double, BorderMode);                                                       \
  template ksj::array::PooledImage<T> bilateral_filter<T>(const ksj::array::PooledImage<T>&, std::size_t, double,      \
                                                          double, BorderMode);                                         \
  template void median_filter<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, BorderMode);   \
  template void median_filter<T>(ksj::array::ImageView<T>, ksj::array::ImageView<T>, std::size_t, BorderMode);         \
  template void median_filter<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t,          \
                                 BorderMode);                                                                          \
  template ksj::array::PooledImage<T> median_filter<T>(const ksj::array::PooledImage<T>&, std::size_t, BorderMode)
#define KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(T)                                                                    \
  template void sobel_x<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode);                      \
  template void sobel_x<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, BorderMode);                \
  template ksj::array::PooledImage<T> sobel_x<T>(const ksj::array::PooledImage<T>&, BorderMode);                       \
  template void sobel_y<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode);                      \
  template void sobel_y<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, BorderMode);                \
  template ksj::array::PooledImage<T> sobel_y<T>(const ksj::array::PooledImage<T>&, BorderMode);                       \
  template void gradient_magnitude<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode);           \
  template void gradient_magnitude<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, BorderMode);     \
  template ksj::array::PooledImage<T> gradient_magnitude<T>(const ksj::array::PooledImage<T>&, BorderMode);            \
  template void laplacian<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, BorderMode);                    \
  template void laplacian<T>(ksj::array::ImageView<T>, ksj::array::ImageView<T>, BorderMode);                          \
  template void laplacian<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, BorderMode);              \
  template ksj::array::PooledImage<T> laplacian<T>(const ksj::array::PooledImage<T>&, BorderMode);                     \
  template void unsharp_mask<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, double, std::size_t, double, \
                                BorderMode);                                                                           \
  template void unsharp_mask<T>(ksj::array::ImageView<T>, ksj::array::ImageView<T>, double, std::size_t, double,       \
                                BorderMode);                                                                           \
  template void unsharp_mask<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, double, std::size_t,   \
                                double, BorderMode);                                                                   \
  template ksj::array::PooledImage<T> unsharp_mask<T>(const ksj::array::PooledImage<T>&, double, std::size_t, double,  \
                                                      BorderMode)

KSJ_IMAGE_INSTANTIATE_FILTERS(float);
KSJ_IMAGE_INSTANTIATE_FILTERS(double);
KSJ_IMAGE_INSTANTIATE_FILTERS(int);
KSJ_IMAGE_INSTANTIATE_FILTERS(std::int16_t);
KSJ_IMAGE_INSTANTIATE_FILTERS(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_FILTERS(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_FILTERS(char);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(float);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(double);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(int);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(std::int16_t);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS(char);

template void filter3d_replicate<double, double>(ksj::array::CubeView<double>, ksj::array::CubeView<double>);
template void filter3d_replicate<float, float>(ksj::array::CubeView<float>, ksj::array::CubeView<float>);
template void filter3d_replicate<ksj::base::cf64, const double>(ksj::array::CubeView<ksj::base::cf64>,
                                                                ksj::array::CubeView<const double>);
#undef KSJ_IMAGE_INSTANTIATE_DERIVATIVE_FILTERS
#undef KSJ_IMAGE_INSTANTIATE_FILTERS

} // namespace ksj::image::detail::eigen
