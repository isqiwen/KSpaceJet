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
[[nodiscard]] T sample_with_border(const ksj::array::PooledImage<T>& input, long row, long col, const BorderMode mode,
                                   const T& constant_value) {
  const auto rows = static_cast<long>(input.rows());
  const auto cols = static_cast<long>(input.cols());
  if (row >= 0 && row < rows && col >= 0 && col < cols) {
    return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
  }

  if (mode == BorderMode::constant || rows == 0 || cols == 0) {
    return constant_value;
  }

  if (mode == BorderMode::replicate) {
    row = std::clamp(row, 0L, rows - 1L);
    col = std::clamp(col, 0L, cols - 1L);
    return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
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
    row = reflect101_index(row, rows);
    col = reflect101_index(col, cols);
    return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
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

  row = reflect_index(row, rows);
  col = reflect_index(col, cols);
  return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
}

template <typename T>
[[nodiscard]] T sample_with_border(const ksj::array::ImageView<const T> input, long row, long col,
                                   const BorderMode mode, const T& constant_value) {
  const auto rows = static_cast<long>(input.rows());
  const auto cols = static_cast<long>(input.cols());
  if (row >= 0 && row < rows && col >= 0 && col < cols) {
    return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
  }

  if (mode == BorderMode::constant || rows == 0 || cols == 0) {
    return constant_value;
  }

  if (mode == BorderMode::replicate) {
    row = std::clamp(row, 0L, rows - 1L);
    col = std::clamp(col, 0L, cols - 1L);
    return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
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
    row = reflect101_index(row, rows);
    col = reflect101_index(col, cols);
    return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
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

  row = reflect_index(row, rows);
  col = reflect_index(col, cols);
  return input(static_cast<std::size_t>(row), static_cast<std::size_t>(col));
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> pad(const ksj::array::PooledImage<T>& input, const std::size_t top,
                                             const std::size_t bottom, const std::size_t left, const std::size_t right,
                                             const BorderMode mode, const T& constant_value) {
  auto output = ksj::array::make_pooled_image<T>(input.rows() + top + bottom, input.cols() + left + right);
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const auto src_row = static_cast<long>(row) - static_cast<long>(top);
      const auto src_col = static_cast<long>(col) - static_cast<long>(left);
      output(row, col) = sample_with_border(input, src_row, src_col, mode, constant_value);
    }
  }
  return output;
}

template <typename T>
void pad(const ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const std::size_t top,
         const std::size_t bottom, const std::size_t left, const std::size_t right, const BorderMode mode,
         const T& constant_value) {
  if (output.rows() != input.rows() + top + bottom || output.cols() != input.cols() + left + right) {
    throw std::invalid_argument("pad output dimension mismatch");
  }

  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const auto src_row = static_cast<long>(row) - static_cast<long>(top);
      const auto src_col = static_cast<long>(col) - static_cast<long>(left);
      output(row, col) = sample_with_border(input, src_row, src_col, mode, constant_value);
    }
  }
}

void validate_region(const std::size_t container_rows, const std::size_t container_cols, const std::size_t row,
                     const std::size_t col, const std::size_t rows, const std::size_t cols,
                     const char* operation_name) {
  if (row > container_rows || col > container_cols || rows > container_rows - row || cols > container_cols - col) {
    throw std::invalid_argument(std::string(operation_name) + " region is outside image bounds");
  }
}

template <typename T>
void crop(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output, const std::size_t source_row,
          const std::size_t source_col) {
  validate_region(input.rows(), input.cols(), source_row, source_col, output.rows(), output.cols(), "crop");
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = input(source_row + row, source_col + col);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> crop(const ksj::array::PooledImage<T>& input, const std::size_t source_row,
                                              const std::size_t source_col, const std::size_t rows,
                                              const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::crop(input, output, source_row, source_col);
  return output;
}

template <typename T> void center_crop(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (output.rows() > input.rows() || output.cols() > input.cols()) {
    throw std::invalid_argument("center_crop output dimensions must not exceed input dimensions");
  }
  const auto source_row = (input.rows() - output.rows()) / 2U;
  const auto source_col = (input.cols() - output.cols()) / 2U;
  ::ksj::image::detail::eigen::crop(input, output, source_row, source_col);
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> center_crop(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                     const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::center_crop(input, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> center_pad_or_crop(const ksj::array::PooledImage<T>& input,
                                                            const std::size_t rows, const std::size_t cols,
                                                            const T& constant_value) {
  const auto padded_rows = std::max(input.rows(), rows);
  const auto padded_cols = std::max(input.cols(), cols);
  const auto row_padding = padded_rows - input.rows();
  const auto col_padding = padded_cols - input.cols();
  auto padded =
    ::ksj::image::detail::eigen::pad(input, row_padding / 2U, row_padding - row_padding / 2U, col_padding / 2U,
                                     col_padding - col_padding / 2U, BorderMode::constant, constant_value);
  return ::ksj::image::detail::eigen::center_crop(padded, rows, cols);
}

template <typename T>
void copy_roi(const ksj::array::PooledImage<T>& source, const std::size_t source_row, const std::size_t source_col,
              ksj::array::PooledImage<T>& destination, const std::size_t destination_row,
              const std::size_t destination_col, const std::size_t rows, const std::size_t cols) {
  validate_region(source.rows(), source.cols(), source_row, source_col, rows, cols, "copy_roi source");
  validate_region(destination.rows(), destination.cols(), destination_row, destination_col, rows, cols,
                  "copy_roi destination");
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t col = 0; col < cols; ++col) {
      destination(destination_row + row, destination_col + col) = source(source_row + row, source_col + col);
    }
  }
}

#define KSJ_IMAGE_INSTANTIATE_REGIONS(T)                                                                               \
  template T sample_with_border<T>(const ksj::array::PooledImage<T>&, long, long, BorderMode, const T&);               \
  template T sample_with_border<T>(ksj::array::ImageView<const T>, long, long, BorderMode, const T&);                  \
  template ksj::array::PooledImage<T> pad<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t, std::size_t, \
                                             std::size_t, BorderMode, const T&);                                       \
  template void pad<T>(ksj::array::ImageView<const T>, ksj::array::ImageView<T>, std::size_t, std::size_t,             \
                       std::size_t, std::size_t, BorderMode, const T&);                                                \
  template void crop<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&, std::size_t, std::size_t);     \
  template ksj::array::PooledImage<T> crop<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t,             \
                                              std::size_t, std::size_t);                                               \
  template void center_crop<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                        \
  template ksj::array::PooledImage<T> center_crop<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t);     \
  template ksj::array::PooledImage<T> center_pad_or_crop<T>(const ksj::array::PooledImage<T>&, std::size_t,            \
                                                            std::size_t, const T&);                                    \
  template void copy_roi<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t, ksj::array::PooledImage<T>&,  \
                            std::size_t, std::size_t, std::size_t, std::size_t)

KSJ_IMAGE_INSTANTIATE_REGIONS(float);
KSJ_IMAGE_INSTANTIATE_REGIONS(double);
KSJ_IMAGE_INSTANTIATE_REGIONS(int);
KSJ_IMAGE_INSTANTIATE_REGIONS(std::int16_t);
KSJ_IMAGE_INSTANTIATE_REGIONS(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_REGIONS(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_REGIONS(char);
#undef KSJ_IMAGE_INSTANTIATE_REGIONS

} // namespace ksj::image::detail::eigen
