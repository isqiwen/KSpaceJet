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

template <typename T, typename Compare>
void morph_reduce(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                  const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                  Compare compare, const StructuringElementShape shape) {
  if (kernel_rows == 0 || kernel_cols == 0) {
    throw std::invalid_argument("morphology kernel dimensions must be positive");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("morphology output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  const auto row_before = static_cast<long>(kernel_rows / 2U);
  const auto row_after = static_cast<long>(kernel_rows - kernel_rows / 2U - 1U);
  const auto col_before = static_cast<long>(kernel_cols / 2U);
  const auto col_after = static_cast<long>(kernel_cols - kernel_cols / 2U - 1U);
  const auto row_radius = static_cast<double>(kernel_rows - 1U) / 2.0;
  const auto col_radius = static_cast<double>(kernel_cols - 1U) / 2.0;
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      T selected{};
      bool selected_initialized = false;
      for (long dy = -row_before; dy <= row_after; ++dy) {
        for (long dx = -col_before; dx <= col_after; ++dx) {
          if (shape == StructuringElementShape::ellipse) {
            const auto normalized_row = row_radius == 0.0 ? 0.0 : static_cast<double>(dy) / row_radius;
            const auto normalized_col = col_radius == 0.0 ? 0.0 : static_cast<double>(dx) / col_radius;
            if (normalized_row * normalized_row + normalized_col * normalized_col > 1.0) {
              continue;
            }
          }
          const auto candidate =
            sample_with_border(input, static_cast<long>(row) + dy, static_cast<long>(col) + dx, border_mode, T{});
          if (!selected_initialized || compare(selected, candidate)) {
            selected = candidate;
            selected_initialized = true;
          }
        }
      }
      output(row, col) = selected;
    }
  }
}

template <typename T, typename Compare>
void morph_reduce(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                  const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                  Compare compare, const StructuringElementShape shape) {
  ::ksj::image::detail::eigen::morph_reduce(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, compare,
                                            shape);
}

template <typename T, typename Compare>
[[nodiscard]] ksj::array::PooledImage<T>
morph_reduce(const ksj::array::PooledImage<T>& input, const std::size_t kernel_rows, const std::size_t kernel_cols,
             const BorderMode border_mode, Compare compare, const StructuringElementShape shape) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::morph_reduce(input, output, kernel_rows, kernel_cols, border_mode, compare, shape);
  return output;
}

template <typename T>
void dilate(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_rows,
            const std::size_t kernel_cols, const BorderMode border_mode, const StructuringElementShape shape) {
  morph_reduce(
    input, output, kernel_rows, kernel_cols, border_mode,
    [](const T& current, const T& candidate) {
      return current < candidate;
    },
    shape);
}

template <typename T>
void dilate(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const std::size_t kernel_rows,
            const std::size_t kernel_cols, const BorderMode border_mode, const StructuringElementShape shape) {
  ::ksj::image::detail::eigen::dilate(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> dilate(const ksj::array::PooledImage<T>& input, const std::size_t kernel_rows,
                                                const std::size_t kernel_cols, const BorderMode border_mode,
                                                const StructuringElementShape shape) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::dilate(input, output, kernel_rows, kernel_cols, border_mode, shape);
  return output;
}

template <typename T>
void erode(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t kernel_rows,
           const std::size_t kernel_cols, const BorderMode border_mode, const StructuringElementShape shape) {
  morph_reduce(
    input, output, kernel_rows, kernel_cols, border_mode,
    [](const T& current, const T& candidate) {
      return candidate < current;
    },
    shape);
}

template <typename T>
void erode(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const std::size_t kernel_rows,
           const std::size_t kernel_cols, const BorderMode border_mode, const StructuringElementShape shape) {
  ::ksj::image::detail::eigen::erode(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> erode(const ksj::array::PooledImage<T>& input, const std::size_t kernel_rows,
                                               const std::size_t kernel_cols, const BorderMode border_mode,
                                               const StructuringElementShape shape) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::erode(input, output, kernel_rows, kernel_cols, border_mode, shape);
  return output;
}

template <typename T>
void morph_open(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                const StructuringElementShape shape) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("morphology output dimension mismatch");
  }
  auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::erode(input, temp.view(), kernel_rows, kernel_cols, border_mode, shape);
  ::ksj::image::detail::eigen::dilate(ksj::array::as_const_view(temp.view()), output, kernel_rows, kernel_cols,
                                      border_mode, shape);
}

template <typename T>
void morph_open(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                const StructuringElementShape shape) {
  ::ksj::image::detail::eigen::morph_open(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> morph_open(const ksj::array::PooledImage<T>& input,
                                                    const std::size_t kernel_rows, const std::size_t kernel_cols,
                                                    const BorderMode border_mode, const StructuringElementShape shape) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::morph_open(input, output, kernel_rows, kernel_cols, border_mode, shape);
  return output;
}

template <typename T>
void morph_close(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                 const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                 const StructuringElementShape shape) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("morphology output dimension mismatch");
  }
  auto temp = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::dilate(input, temp.view(), kernel_rows, kernel_cols, border_mode, shape);
  ::ksj::image::detail::eigen::erode(ksj::array::as_const_view(temp.view()), output, kernel_rows, kernel_cols,
                                     border_mode, shape);
}

template <typename T>
void morph_close(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                 const std::size_t kernel_rows, const std::size_t kernel_cols, const BorderMode border_mode,
                 const StructuringElementShape shape) {
  ::ksj::image::detail::eigen::morph_close(input.view(), output.view(), kernel_rows, kernel_cols, border_mode, shape);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T>
morph_close(const ksj::array::PooledImage<T>& input, const std::size_t kernel_rows, const std::size_t kernel_cols,
            const BorderMode border_mode, const StructuringElementShape shape) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::morph_close(input, output, kernel_rows, kernel_cols, border_mode, shape);
  return output;
}

template <typename T>
void dilate_cross_value(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T value) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("dilate_cross_value output dimension mismatch");
  }
  ksj::array::copy(input, output);
  if (input.rows() < 3U || input.cols() < 3U) {
    return;
  }

  for (std::size_t row = 1U; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1U; col + 1U < input.cols(); ++col) {
      if (input(row, col) != value && (input(row - 1U, col) == value || input(row + 1U, col) == value ||
                                       input(row, col - 1U) == value || input(row, col + 1U) == value)) {
        output(row, col) = value;
      }
    }
  }
}

template <typename T>
void erode_cross_value(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const T value,
                       const T background) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("erode_cross_value output dimension mismatch");
  }
  ksj::array::copy(input, output);
  if (input.rows() < 3U || input.cols() < 3U) {
    return;
  }

  for (std::size_t row = 1U; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1U; col + 1U < input.cols(); ++col) {
      if (input(row, col) == value && (input(row - 1U, col) != value || input(row + 1U, col) != value ||
                                       input(row, col - 1U) != value || input(row, col + 1U) != value)) {
        output(row, col) = background;
      }
    }
  }
}

template <typename T>
void dilate_threshold_cross(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                            const T center_threshold, const T neighbor_threshold, const T value) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("dilate_threshold_cross output dimension mismatch");
  }
  ksj::array::copy(input, output);
  if (input.rows() < 3U || input.cols() < 3U) {
    return;
  }

  for (std::size_t row = 1U; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1U; col + 1U < input.cols(); ++col) {
      if (input(row, col) < center_threshold &&
          (input(row - 1U, col) > neighbor_threshold || input(row + 1U, col) > neighbor_threshold ||
           input(row, col - 1U) > neighbor_threshold || input(row, col + 1U) > neighbor_threshold)) {
        output(row, col) = value;
      }
    }
  }
}

template <typename T>
void erode_threshold_cross(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                           const T center_threshold, const T neighbor_threshold, const T value) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("erode_threshold_cross output dimension mismatch");
  }
  ksj::array::copy(input, output);
  if (input.rows() < 3U || input.cols() < 3U) {
    return;
  }

  for (std::size_t row = 1U; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1U; col + 1U < input.cols(); ++col) {
      if (input(row, col) > center_threshold &&
          (input(row - 1U, col) < neighbor_threshold || input(row + 1U, col) < neighbor_threshold ||
           input(row, col - 1U) < neighbor_threshold || input(row, col + 1U) < neighbor_threshold)) {
        output(row, col) = value;
      }
    }
  }
}

[[nodiscard]] ksj::array::PooledImage<int> disk_structuring_element(const std::size_t radius) {
  const auto size = radius * 2U + 1U;
  auto output = ksj::array::make_pooled_image<int>(size, size);
  for (std::size_t row = 0U; row < size; ++row) {
    for (std::size_t col = 0U; col < size; ++col) {
      const auto y = static_cast<double>(row) - static_cast<double>(radius);
      const auto x = static_cast<double>(col) - static_cast<double>(radius);
      output(row, col) = std::sqrt(y * y + x * x) > static_cast<double>(radius) ? 0 : 1;
    }
  }
  return output;
}

template <typename T>
void dilate_mask(const ksj::array::ImageView<const T> input, const ksj::array::ImageView<const int> kernel,
                 ksj::array::ImageView<T> output, const T value) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("dilate_mask output dimension mismatch");
  }
  if (kernel.empty()) {
    throw std::invalid_argument("dilate_mask kernel must not be empty");
  }

  ksj::array::copy(input, output);
  const auto kernel_center_row = static_cast<long>(kernel.rows() / 2U);
  const auto kernel_center_col = static_cast<long>(kernel.cols() / 2U);
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      bool hit = false;
      for (std::size_t kernel_row = 0U; kernel_row < kernel.rows() && !hit; ++kernel_row) {
        for (std::size_t kernel_col = 0U; kernel_col < kernel.cols(); ++kernel_col) {
          if (kernel(kernel_row, kernel_col) == 0) {
            continue;
          }
          const auto source_row = static_cast<long>(row) + static_cast<long>(kernel_row) - kernel_center_row;
          const auto source_col = static_cast<long>(col) + static_cast<long>(kernel_col) - kernel_center_col;
          if (source_row >= 0 && source_row < static_cast<long>(input.rows()) && source_col >= 0 &&
              source_col < static_cast<long>(input.cols()) &&
              input(static_cast<std::size_t>(source_row), static_cast<std::size_t>(source_col)) != T{}) {
            output(row, col) = value;
            hit = true;
            break;
          }
        }
      }
    }
  }
}

template <typename T>
void erode_mask(const ksj::array::ImageView<const T> input, const ksj::array::ImageView<const int> kernel,
                ksj::array::ImageView<T> output, const T background) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("erode_mask output dimension mismatch");
  }
  if (kernel.empty()) {
    throw std::invalid_argument("erode_mask kernel must not be empty");
  }

  ksj::array::copy(input, output);
  const auto kernel_center_row = static_cast<long>(kernel.rows() / 2U);
  const auto kernel_center_col = static_cast<long>(kernel.cols() / 2U);
  for (std::size_t row = 0U; row < input.rows(); ++row) {
    for (std::size_t col = 0U; col < input.cols(); ++col) {
      bool clear = false;
      for (std::size_t kernel_row = 0U; kernel_row < kernel.rows() && !clear; ++kernel_row) {
        for (std::size_t kernel_col = 0U; kernel_col < kernel.cols(); ++kernel_col) {
          if (kernel(kernel_row, kernel_col) == 0) {
            continue;
          }
          const auto source_row = static_cast<long>(row) + static_cast<long>(kernel_row) - kernel_center_row;
          const auto source_col = static_cast<long>(col) + static_cast<long>(kernel_col) - kernel_center_col;
          if (source_row < 0 || source_row >= static_cast<long>(input.rows()) || source_col < 0 ||
              source_col >= static_cast<long>(input.cols()) ||
              input(static_cast<std::size_t>(source_row), static_cast<std::size_t>(source_col)) == T{}) {
            output(row, col) = background;
            clear = true;
            break;
          }
        }
      }
    }
  }
}

void keep_largest_component(ksj::array::ImageView<int> mask, const Connectivity connectivity) {
  if (mask.empty()) {
    return;
  }

  auto foreground = ksj::array::make_pooled_image<char>(mask.rows(), mask.cols());
  auto input = foreground.view();
  for (std::size_t row = 0U; row < mask.rows(); ++row) {
    for (std::size_t col = 0U; col < mask.cols(); ++col) {
      input(row, col) = static_cast<char>(mask(row, col) == 1);
    }
  }

  auto labels = ksj::array::make_pooled_image<ConnectedComponentLabel>(mask.rows(), mask.cols());
  std::vector<ConnectedComponentStats> stats;
  ::ksj::image::detail::eigen::connected_components(ksj::array::as_const_view(input), labels.view(), &stats,
                                                    connectivity);
  if (stats.empty()) {
    ksj::array::fill(mask, 0);
    return;
  }

  const auto largest = std::max_element(stats.begin(), stats.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.area < rhs.area;
  });
  const auto largest_label = largest->label;
  for (std::size_t row = 0U; row < mask.rows(); ++row) {
    for (std::size_t col = 0U; col < mask.cols(); ++col) {
      if (labels(row, col) != largest_label) {
        mask(row, col) = 0;
      }
    }
  }
}

#define KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(T)                                                                            \
  template void dilate<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,          \
                          BorderMode, StructuringElementShape);                                                        \
  template void dilate<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t, std::size_t,    \
                          BorderMode, StructuringElementShape);                                                        \
  template ksj::array::PooledImage<T> dilate<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t,           \
                                                BorderMode, StructuringElementShape);                                  \
  template void erode<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,           \
                         BorderMode, StructuringElementShape);                                                         \
  template void erode<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t, std::size_t,     \
                         BorderMode, StructuringElementShape);                                                         \
  template ksj::array::PooledImage<T> erode<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t,            \
                                               BorderMode, StructuringElementShape);                                   \
  template void morph_open<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,      \
                              BorderMode, StructuringElementShape);                                                    \
  template void morph_open<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t,             \
                              std::size_t, BorderMode, StructuringElementShape);                                       \
  template ksj::array::PooledImage<T> morph_open<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t,       \
                                                    BorderMode, StructuringElementShape);                              \
  template void morph_close<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,     \
                               BorderMode, StructuringElementShape);                                                   \
  template void morph_close<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t,            \
                               std::size_t, BorderMode, StructuringElementShape);                                      \
  template ksj::array::PooledImage<T> morph_close<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t,      \
                                                     BorderMode, StructuringElementShape);                             \
  template void dilate_cross_value<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, T);                    \
  template void erode_cross_value<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, T, T);                  \
  template void dilate_threshold_cross<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, T, T, T);          \
  template void erode_threshold_cross<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, T, T, T);           \
  template void dilate_mask<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<const int>,                       \
                               ksj::array::ImageView<T>, T);                                                           \
  template void erode_mask<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<const int>,                        \
                              ksj::array::ImageView<T>, T)

KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(float);
KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(double);
KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(int);
KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(std::int16_t);
KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_MORPHOLOGY(char);
#undef KSJ_IMAGE_INSTANTIATE_MORPHOLOGY

} // namespace ksj::image::detail::eigen
