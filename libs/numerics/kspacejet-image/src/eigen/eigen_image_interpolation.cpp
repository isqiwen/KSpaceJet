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

[[nodiscard]] double resize_scale(const std::size_t input_size, const std::size_t output_size) noexcept {
  return output_size > 1U ? static_cast<double>(input_size - 1U) / static_cast<double>(output_size - 1U) : 0.0;
}

[[nodiscard]] std::size_t nearest_resize_index(const double coordinate, const std::size_t limit) noexcept {
  const auto rounded = static_cast<std::size_t>(std::floor(coordinate + 0.5));
  return std::min(rounded, limit - 1U);
}

template <typename T> void resize_nearest(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (output.empty()) {
    return;
  }
  if (input.empty()) {
    throw std::invalid_argument("resize_nearest input must not be empty");
  }

  const double row_scale = resize_scale(input.rows(), output.rows());
  const double col_scale = resize_scale(input.cols(), output.cols());
  for (std::size_t row = 0; row < output.rows(); ++row) {
    const auto src_row = nearest_resize_index(static_cast<double>(row) * row_scale, input.rows());
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const auto src_col = nearest_resize_index(static_cast<double>(col) * col_scale, input.cols());
      output(row, col) = input(src_row, src_col);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_nearest(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                        const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::resize_nearest(input, output);
  return output;
}

template <typename T> void resize_linear(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (output.empty()) {
    return;
  }
  if (input.empty()) {
    throw std::invalid_argument("resize_linear input must not be empty");
  }

  if (output.rows() == 1 && output.cols() == 1) {
    output(0, 0) = input(0, 0);
    return;
  }

  const double row_scale = resize_scale(input.rows(), output.rows());
  const double col_scale = resize_scale(input.cols(), output.cols());
  for (std::size_t row = 0; row < output.rows(); ++row) {
    const double src_row = static_cast<double>(row) * row_scale;
    const auto row0 = static_cast<std::size_t>(std::floor(src_row));
    const auto row1 = std::min(row0 + 1U, input.rows() - 1U);
    const double row_weight = src_row - static_cast<double>(row0);
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const double src_col = static_cast<double>(col) * col_scale;
      const auto col0 = static_cast<std::size_t>(std::floor(src_col));
      const auto col1 = std::min(col0 + 1U, input.cols() - 1U);
      const double col_weight = src_col - static_cast<double>(col0);

      const auto top = static_cast<double>(input(row0, col0)) * (1.0 - col_weight) +
                       static_cast<double>(input(row0, col1)) * col_weight;
      const auto bottom = static_cast<double>(input(row1, col0)) * (1.0 - col_weight) +
                          static_cast<double>(input(row1, col1)) * col_weight;
      output(row, col) = static_cast<T>(top * (1.0 - row_weight) + bottom * row_weight);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_linear(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                       const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::resize_linear(input, output);
  return output;
}

[[nodiscard]] double cubic_resize_weight(double offset) noexcept {
  constexpr double a = -0.75;
  offset = std::abs(offset);
  if (offset < 1.0) {
    return ((a + 2.0) * offset - (a + 3.0)) * offset * offset + 1.0;
  }
  if (offset < 2.0) {
    return (((a * offset - 5.0 * a) * offset + 8.0 * a) * offset) - 4.0 * a;
  }
  return 0.0;
}

template <typename T>
[[nodiscard]] T sample_cubic_replicate(const ksj::array::PooledImage<T>& input, const double row, const double col) {
  const auto base_row = static_cast<long>(std::floor(row));
  const auto base_col = static_cast<long>(std::floor(col));
  double sum = 0.0;
  for (long dy = -1L; dy <= 2L; ++dy) {
    const auto row_weight = cubic_resize_weight(row - static_cast<double>(base_row + dy));
    for (long dx = -1L; dx <= 2L; ++dx) {
      const auto col_weight = cubic_resize_weight(col - static_cast<double>(base_col + dx));
      sum += static_cast<double>(sample_with_border(input, base_row + dy, base_col + dx, BorderMode::replicate, T{})) *
             row_weight * col_weight;
    }
  }
  return static_cast<T>(sum);
}

template <typename T> void resize_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (output.empty()) {
    return;
  }
  if (input.empty()) {
    throw std::invalid_argument("resize_cubic input must not be empty");
  }

  const double row_scale = resize_scale(input.rows(), output.rows());
  const double col_scale = resize_scale(input.cols(), output.cols());
  for (std::size_t row = 0; row < output.rows(); ++row) {
    const double src_row = static_cast<double>(row) * row_scale;
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const double src_col = static_cast<double>(col) * col_scale;
      output(row, col) = sample_cubic_replicate(input, src_row, src_col);
    }
  }
}

[[nodiscard]] inline double interpolation_coordinate(const std::size_t index, const double factor,
                                                     const double shift) noexcept {
  return (static_cast<double>(index) - shift) / factor;
}

[[nodiscard]] inline float sample_cubic_zero(ksj::array::MatrixView<float> input, const double row, const double col) {
  const auto base_row = static_cast<long>(std::floor(row));
  const auto base_col = static_cast<long>(std::floor(col));
  double sum = 0.0;
  for (long dy = -1L; dy <= 2L; ++dy) {
    const auto source_row = base_row + dy;
    if (source_row < 0 || source_row >= static_cast<long>(input.rows())) {
      continue;
    }
    const auto row_weight = cubic_resize_weight(row - static_cast<double>(source_row));
    for (long dx = -1L; dx <= 2L; ++dx) {
      const auto source_col = base_col + dx;
      if (source_col < 0 || source_col >= static_cast<long>(input.cols())) {
        continue;
      }
      const auto col_weight = cubic_resize_weight(col - static_cast<double>(source_col));
      sum += static_cast<double>(input(static_cast<std::size_t>(source_row), static_cast<std::size_t>(source_col))) *
             row_weight * col_weight;
    }
  }
  return static_cast<float>(sum);
}

template <typename T> struct is_std_complex : std::false_type {};
template <typename T> struct is_std_complex<std::complex<T>> : std::true_type {};

template <typename T> inline constexpr bool is_std_complex_v = is_std_complex<std::remove_cv_t<T>>::value;

template <typename T> using cubic_accumulator_t = std::conditional_t<is_std_complex_v<T>, ksj::base::cf64, double>;

template <typename T> [[nodiscard]] cubic_accumulator_t<T> cubic_accumulator_value(const T& value) {
  if constexpr (is_std_complex_v<T>) {
    return {static_cast<double>(value.real()), static_cast<double>(value.imag())};
  } else {
    return static_cast<double>(value);
  }
}

template <typename T> [[nodiscard]] T cubic_accumulator_cast(const cubic_accumulator_t<T>& value) {
  if constexpr (is_std_complex_v<T>) {
    using scalar_type = typename std::remove_cv_t<T>::value_type;
    return T{static_cast<scalar_type>(value.real()), static_cast<scalar_type>(value.imag())};
  } else {
    return static_cast<T>(value);
  }
}

[[nodiscard]] inline double cubic_bspline_weight(const double distance) noexcept {
  const double absolute_distance = std::abs(distance);
  if (absolute_distance < 1.0) {
    return (3.0 * absolute_distance * absolute_distance * absolute_distance -
            6.0 * absolute_distance * absolute_distance + 4.0) /
           6.0;
  }
  if (absolute_distance < 2.0) {
    const double remaining = 2.0 - absolute_distance;
    return remaining * remaining * remaining / 6.0;
  }
  return 0.0;
}

template <typename T>
[[nodiscard]] T sample_cubic_bspline_replicate(ksj::array::ImageView<const T> input, const double row,
                                               const double col) {
  const auto base_row = static_cast<long>(std::floor(row));
  const auto base_col = static_cast<long>(std::floor(col));
  const auto max_row = static_cast<long>(input.rows() - 1U);
  const auto max_col = static_cast<long>(input.cols() - 1U);
  cubic_accumulator_t<T> sum{};
  for (long dy = -1L; dy <= 2L; ++dy) {
    const auto unclamped_row = base_row + dy;
    const auto source_row = std::clamp(unclamped_row, 0L, max_row);
    const auto row_weight = cubic_bspline_weight(row - static_cast<double>(unclamped_row));
    for (long dx = -1L; dx <= 2L; ++dx) {
      const auto unclamped_col = base_col + dx;
      const auto source_col = std::clamp(unclamped_col, 0L, max_col);
      const auto col_weight = cubic_bspline_weight(col - static_cast<double>(unclamped_col));
      sum +=
        cubic_accumulator_value(input(static_cast<std::size_t>(source_row), static_cast<std::size_t>(source_col))) *
        row_weight * col_weight;
    }
  }
  return cubic_accumulator_cast<T>(sum);
}

template <typename T>
void rotate_cubic_bspline_smooth(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output,
                                 const double angle_degrees) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("rotate_cubic_bspline_smooth output dimension mismatch");
  }
  if (!std::isfinite(angle_degrees)) {
    throw std::invalid_argument("rotate_cubic_bspline_smooth angle must be finite");
  }
  if (input.empty()) {
    return;
  }

  constexpr double degrees_to_radians = 3.141592653589793238462643383279502884 / 180.0;
  const double angle_radians = angle_degrees * degrees_to_radians;
  const double cos_angle = std::cos(angle_radians);
  const double sin_angle = std::sin(angle_radians);
  const double center_col = static_cast<double>(input.cols()) / 2.0;
  const double center_row = static_cast<double>(input.rows()) / 2.0;

  for (std::size_t row = 0; row < output.rows(); ++row) {
    const double y = static_cast<double>(row) - center_row;
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const double x = static_cast<double>(col) - center_col;
      const double source_col = cos_angle * x + sin_angle * y + center_col;
      const double source_row = -sin_angle * x + cos_angle * y + center_row;
      if (source_row < 0.0 || source_col < 0.0 || source_row >= static_cast<double>(input.rows()) ||
          source_col >= static_cast<double>(input.cols())) {
        output(row, col) = T{};
      } else {
        output(row, col) = sample_cubic_bspline_replicate(input, source_row, source_col);
      }
    }
  }
}

template <typename T>
[[nodiscard]] T sample_cubic_constant(ksj::array::ImageView<const T> input, const double row, const double col) {
  const auto base_row = static_cast<long>(std::floor(row));
  const auto base_col = static_cast<long>(std::floor(col));
  cubic_accumulator_t<T> sum{};
  for (long dy = -1L; dy <= 2L; ++dy) {
    const auto source_row = base_row + dy;
    if (source_row < 0 || source_row >= static_cast<long>(input.rows())) {
      continue;
    }
    const auto row_weight = cubic_resize_weight(row - static_cast<double>(source_row));
    for (long dx = -1L; dx <= 2L; ++dx) {
      const auto source_col = base_col + dx;
      if (source_col < 0 || source_col >= static_cast<long>(input.cols())) {
        continue;
      }
      const auto col_weight = cubic_resize_weight(col - static_cast<double>(source_col));
      sum +=
        cubic_accumulator_value(input(static_cast<std::size_t>(source_row), static_cast<std::size_t>(source_col))) *
        row_weight * col_weight;
    }
  }
  return cubic_accumulator_cast<T>(sum);
}

template <typename T>
void rotate_cubic(ksj::array::ImageView<const T> input, ksj::array::ImageView<T> output, const double angle_degrees) {
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("rotate_cubic output dimension mismatch");
  }
  if (input.empty()) {
    return;
  }

  constexpr double degrees_to_radians = 3.141592653589793238462643383279502884 / 180.0;
  const double angle_radians = angle_degrees * degrees_to_radians;
  const double cos_angle = std::cos(angle_radians);
  const double sin_angle = std::sin(angle_radians);
  const double center_col = static_cast<double>(input.cols()) / 2.0;
  const double center_row = static_cast<double>(input.rows()) / 2.0;

  for (std::size_t row = 0; row < output.rows(); ++row) {
    const double y = static_cast<double>(row) - center_row;
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const double x = static_cast<double>(col) - center_col;
      const double source_col = cos_angle * x + sin_angle * y + center_col;
      const double source_row = -sin_angle * x + cos_angle * y + center_row;
      output(row, col) = sample_cubic_constant(input, source_row, source_col);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> rotate_cubic(const ksj::array::PooledImage<T>& input,
                                                      const double angle_degrees) {
  auto output = ksj::array::make_pooled_image<T>(input.rows(), input.cols());
  ::ksj::image::detail::eigen::rotate_cubic(ksj::array::as_const_view(input.view()), output.view(), angle_degrees);
  return output;
}

template <typename T>
void rotate_cubic(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output,
                  const double angle_degrees) {
  rotate_cubic(ksj::array::as_const_view(input.view()), output.view(), angle_degrees);
}

[[nodiscard]] InterpolationResult cubic_interpolate_2d_inplace(ksj::array::MatrixView<ksj::base::cf32> matrix,
                                                               InterpolationAxis axis, float ratio) {
  if (matrix.empty()) {
    return {InterpolationStatus::empty_input, 0};
  }

  if (ratio <= 0.0F) {
    return {InterpolationStatus::backend_error, 0};
  }

  if (ratio == 1.0F) {
    return {InterpolationStatus::success, 0};
  }

  const auto rows = matrix.rows();
  const auto cols = matrix.cols();
  auto source_buffer = ksj::array::make_pooled_matrix<float>(rows, cols);
  auto source = source_buffer.view();
  ksj::array::transform(matrix, source, [](const auto& value) {
    const auto real = value.real();
    return real < 0.0F ? 0.0F : real;
  });

  const auto row_factor = (axis == InterpolationAxis::column) ? static_cast<double>(ratio) : 1.0;
  const auto col_factor = (axis == InterpolationAxis::row) ? static_cast<double>(ratio) : 1.0;
  const auto row_shift = (axis == InterpolationAxis::column)
                           ? ((1.0 - static_cast<double>(ratio)) * ((static_cast<double>(rows) + 1.0) / 2.0))
                           : 0.0;
  const auto col_shift = (axis == InterpolationAxis::row)
                           ? ((1.0 - static_cast<double>(ratio)) * ((static_cast<double>(cols) + 1.0) / 2.0))
                           : 0.0;

  for (std::size_t row = 0; row < rows; ++row) {
    const auto source_row = interpolation_coordinate(row, row_factor, row_shift);
    for (std::size_t col = 0; col < cols; ++col) {
      const auto source_col = interpolation_coordinate(col, col_factor, col_shift);
      const auto value = sample_cubic_zero(source, source_row, source_col);
      matrix(row, col) = ksj::base::cf32(value < 0.0F ? 0.0F : value, 0.0F);
    }
  }

  return {InterpolationStatus::success, 0};
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_cubic(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                      const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::resize_cubic(input, output);
  return output;
}

[[nodiscard]] double lanczos_sinc(const double value) noexcept {
  constexpr double pi = 3.141592653589793238462643383279502884;
  const auto absolute = std::abs(value);
  if (absolute < 1.0e-12) {
    return 1.0;
  }
  const auto scaled = pi * value;
  return std::sin(scaled) / scaled;
}

[[nodiscard]] double lanczos4_resize_weight(const double offset) noexcept {
  constexpr double radius = 4.0;
  const auto absolute = std::abs(offset);
  if (absolute >= radius) {
    return 0.0;
  }
  return lanczos_sinc(offset) * lanczos_sinc(offset / radius);
}

template <typename T>
[[nodiscard]] T sample_lanczos4_replicate(const ksj::array::PooledImage<T>& input, const double row, const double col) {
  const auto base_row = static_cast<long>(std::floor(row));
  const auto base_col = static_cast<long>(std::floor(col));
  double row_weights[8]{};
  double col_weights[8]{};
  double row_weight_sum = 0.0;
  double col_weight_sum = 0.0;
  for (long index = 0; index < 8L; ++index) {
    const auto row_offset = row - static_cast<double>(base_row + index - 3L);
    const auto col_offset = col - static_cast<double>(base_col + index - 3L);
    row_weights[index] = lanczos4_resize_weight(row_offset);
    col_weights[index] = lanczos4_resize_weight(col_offset);
    row_weight_sum += row_weights[index];
    col_weight_sum += col_weights[index];
  }

  if (row_weight_sum != 0.0) {
    for (auto& weight : row_weights) {
      weight /= row_weight_sum;
    }
  }
  if (col_weight_sum != 0.0) {
    for (auto& weight : col_weights) {
      weight /= col_weight_sum;
    }
  }

  double sum = 0.0;
  for (long dy = 0; dy < 8L; ++dy) {
    const auto source_row = base_row + dy - 3L;
    for (long dx = 0; dx < 8L; ++dx) {
      const auto source_col = base_col + dx - 3L;
      sum += static_cast<double>(sample_with_border(input, source_row, source_col, BorderMode::replicate, T{})) *
             row_weights[dy] * col_weights[dx];
    }
  }
  return static_cast<T>(sum);
}

template <typename T>
void resize_lanczos4(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (output.empty()) {
    return;
  }
  if (input.empty()) {
    throw std::invalid_argument("resize_lanczos4 input must not be empty");
  }

  const double row_scale = resize_scale(input.rows(), output.rows());
  const double col_scale = resize_scale(input.cols(), output.cols());
  for (std::size_t row = 0; row < output.rows(); ++row) {
    const double src_row = static_cast<double>(row) * row_scale;
    for (std::size_t col = 0; col < output.cols(); ++col) {
      const double src_col = static_cast<double>(col) * col_scale;
      output(row, col) = sample_lanczos4_replicate(input, src_row, src_col);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_lanczos4(const ksj::array::PooledImage<T>& input,
                                                         const std::size_t rows, const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::resize_lanczos4(input, output);
  return output;
}

[[nodiscard]] double interval_overlap(const double begin, const double end, const double cell_begin,
                                      const double cell_end) noexcept {
  return std::max(0.0, std::min(end, cell_end) - std::max(begin, cell_begin));
}

template <typename T> void resize_area(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  if (output.empty()) {
    return;
  }
  if (input.empty()) {
    throw std::invalid_argument("resize_area input must not be empty");
  }

  const double row_scale = static_cast<double>(input.rows()) / static_cast<double>(output.rows());
  const double col_scale = static_cast<double>(input.cols()) / static_cast<double>(output.cols());
  const double output_pixel_area = row_scale * col_scale;

  for (std::size_t row = 0; row < output.rows(); ++row) {
    const double source_row_begin = static_cast<double>(row) * row_scale;
    const double source_row_end = static_cast<double>(row + 1U) * row_scale;
    const auto first_source_row = static_cast<std::size_t>(std::floor(source_row_begin));
    const auto last_source_row = std::min(static_cast<std::size_t>(std::ceil(source_row_end)), input.rows());

    for (std::size_t col = 0; col < output.cols(); ++col) {
      const double source_col_begin = static_cast<double>(col) * col_scale;
      const double source_col_end = static_cast<double>(col + 1U) * col_scale;
      const auto first_source_col = static_cast<std::size_t>(std::floor(source_col_begin));
      const auto last_source_col = std::min(static_cast<std::size_t>(std::ceil(source_col_end)), input.cols());

      double weighted_sum = 0.0;
      for (std::size_t source_row = first_source_row; source_row < last_source_row; ++source_row) {
        const double row_weight = interval_overlap(source_row_begin, source_row_end, static_cast<double>(source_row),
                                                   static_cast<double>(source_row + 1U));
        if (row_weight == 0.0) {
          continue;
        }
        for (std::size_t source_col = first_source_col; source_col < last_source_col; ++source_col) {
          const double col_weight = interval_overlap(source_col_begin, source_col_end, static_cast<double>(source_col),
                                                     static_cast<double>(source_col + 1U));
          if (col_weight == 0.0) {
            continue;
          }
          weighted_sum += static_cast<double>(input(source_row, source_col)) * row_weight * col_weight;
        }
      }
      output(row, col) = static_cast<T>(weighted_sum / output_pixel_area);
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::PooledImage<T> resize_area(const ksj::array::PooledImage<T>& input, const std::size_t rows,
                                                     const std::size_t cols) {
  auto output = ksj::array::make_pooled_image<T>(rows, cols);
  ::ksj::image::detail::eigen::resize_area(input, output);
  return output;
}

void rotate_cubic_bspline_smooth(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                                 const double angle_degrees) {
  ::ksj::image::detail::eigen::rotate_cubic_bspline_smooth<float>(input, output, angle_degrees);
}

void rotate_cubic_bspline_smooth(ksj::array::ImageView<const ksj::base::cf32> input,
                                 ksj::array::ImageView<ksj::base::cf32> output, const double angle_degrees) {
  ::ksj::image::detail::eigen::rotate_cubic_bspline_smooth<ksj::base::cf32>(input, output, angle_degrees);
}

void rotate_cubic(ksj::array::ImageView<const float> input, ksj::array::ImageView<float> output,
                  const double angle_degrees) {
  ::ksj::image::detail::eigen::rotate_cubic<float>(input, output, angle_degrees);
}

void rotate_cubic(ksj::array::ImageView<const ksj::base::cf32> input, ksj::array::ImageView<ksj::base::cf32> output,
                  const double angle_degrees) {
  ::ksj::image::detail::eigen::rotate_cubic<ksj::base::cf32>(input, output, angle_degrees);
}

#define KSJ_IMAGE_INSTANTIATE_RESIZE(T)                                                                                \
  template void resize_nearest<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                     \
  template ksj::array::PooledImage<T> resize_nearest<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t);  \
  template void resize_linear<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                      \
  template ksj::array::PooledImage<T> resize_linear<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t);   \
  template void resize_cubic<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                       \
  template ksj::array::PooledImage<T> resize_cubic<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t);    \
  template void resize_lanczos4<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                    \
  template ksj::array::PooledImage<T> resize_lanczos4<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t); \
  template void resize_area<T>(const ksj::array::PooledImage<T>&, ksj::array::PooledImage<T>&);                        \
  template ksj::array::PooledImage<T> resize_area<T>(const ksj::array::PooledImage<T>&, std::size_t, std::size_t)

KSJ_IMAGE_INSTANTIATE_RESIZE(float);
KSJ_IMAGE_INSTANTIATE_RESIZE(double);
KSJ_IMAGE_INSTANTIATE_RESIZE(int);
KSJ_IMAGE_INSTANTIATE_RESIZE(std::int16_t);
KSJ_IMAGE_INSTANTIATE_RESIZE(std::uint8_t);
KSJ_IMAGE_INSTANTIATE_RESIZE(std::uint16_t);
KSJ_IMAGE_INSTANTIATE_RESIZE(char);

template void rotate_cubic_bspline_smooth<double>(ksj::array::ImageView<const double>, ksj::array::ImageView<double>,
                                                  double);
template void rotate_cubic_bspline_smooth<ksj::base::cf32>(ksj::array::ImageView<const ksj::base::cf32>,
                                                           ksj::array::ImageView<ksj::base::cf32>, double);
template void rotate_cubic_bspline_smooth<ksj::base::cf64>(ksj::array::ImageView<const ksj::base::cf64>,
                                                           ksj::array::ImageView<ksj::base::cf64>, double);
template void rotate_cubic<double>(ksj::array::ImageView<const double>, ksj::array::ImageView<double>, double);
template void rotate_cubic<ksj::base::cf32>(ksj::array::ImageView<const ksj::base::cf32>,
                                            ksj::array::ImageView<ksj::base::cf32>, double);
template void rotate_cubic<ksj::base::cf64>(ksj::array::ImageView<const ksj::base::cf64>,
                                            ksj::array::ImageView<ksj::base::cf64>, double);
template ksj::array::PooledImage<ksj::base::cf32>
rotate_cubic<ksj::base::cf32>(const ksj::array::PooledImage<ksj::base::cf32>&, double);
template void rotate_cubic<ksj::base::cf32>(const ksj::array::PooledImage<ksj::base::cf32>&,
                                            ksj::array::PooledImage<ksj::base::cf32>&, double);
#undef KSJ_IMAGE_INSTANTIATE_RESIZE

} // namespace ksj::image::detail::eigen
