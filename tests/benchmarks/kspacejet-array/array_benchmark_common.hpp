#pragma once

#include "benchmark_common.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/array/detail/array_policy.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

namespace ksj::benchmarks::array_benchmarks {

inline constexpr std::size_t kMaxArrayBenchmarkElements = 1024U * 1024U;

struct Shape2D {
  std::size_t rows{};
  std::size_t cols{};
};

struct Shape3D {
  std::size_t dim0{};
  std::size_t dim1{};
  std::size_t dim2{};
};

struct Shape4D {
  std::size_t dim0{};
  std::size_t dim1{};
  std::size_t dim2{};
  std::size_t dim3{};
};

[[nodiscard]] inline constexpr bool policy_range_contains(const std::size_t size, const std::size_t minimum,
                                                          const std::size_t maximum) noexcept {
  return minimum != ksj::array::detail::ArrayDispatchPolicy::disabled_backend_min_elements && size >= minimum &&
         size <= maximum;
}

[[nodiscard]] inline constexpr std::optional<ksj::array::detail::VectorElementwiseOperation>
vector_elementwise_operation(const std::string_view operation) noexcept {
  using Operation = ksj::array::detail::VectorElementwiseOperation;
  if (operation == "vector_add" || operation == "matrix_add" || operation == "image_add" || operation == "cube_add" ||
      operation == "array4d_add" || operation == "complex_vector_add") {
    return Operation::add;
  }
  if (operation == "vector_subtract" || operation == "complex_vector_subtract") {
    return Operation::subtract;
  }
  if (operation == "vector_multiply" || operation == "complex_vector_multiply") {
    return Operation::multiply;
  }
  if (operation == "vector_divide" || operation == "complex_vector_divide") {
    return Operation::divide;
  }
  if (operation == "complex_vector_add_scalar") {
    return Operation::add_scalar;
  }
  if (operation == "vector_subtract_scalar") {
    return Operation::subtract_scalar;
  }
  if (operation == "vector_scalar_subtract") {
    return Operation::scalar_subtract;
  }
  if (operation == "vector_scale" || operation == "complex_vector_scale") {
    return Operation::scale;
  }
  if (operation == "vector_divide_scalar") {
    return Operation::divide_scalar;
  }
  if (operation == "vector_scalar_divide") {
    return Operation::scalar_divide;
  }
  if (operation == "vector_negate") {
    return Operation::negate;
  }
  if (operation == "vector_absolute") {
    return Operation::absolute;
  }
  if (operation == "vector_square") {
    return Operation::square;
  }
  if (operation == "vector_sqrt") {
    return Operation::sqrt;
  }
  if (operation == "vector_inverse") {
    return Operation::inverse;
  }
  if (operation == "vector_rsqrt") {
    return Operation::inverse_sqrt;
  }
  if (operation == "vector_exp") {
    return Operation::exp;
  }
  if (operation == "vector_log") {
    return Operation::log;
  }
  if (operation == "vector_hypot") {
    return Operation::hypot;
  }
  if (operation == "vector_minimum") {
    return Operation::minimum;
  }
  if (operation == "vector_maximum") {
    return Operation::maximum;
  }
  if (operation == "vector_clamp") {
    return Operation::clamp;
  }
  return std::nullopt;
}

[[nodiscard]] inline constexpr bool uses_small_direct_vector_loop(const std::string_view operation) noexcept {
  return operation == "vector_add" || operation == "vector_subtract" || operation == "vector_multiply" ||
         operation == "vector_divide" || operation == "vector_scale" || operation == "vector_subtract_scalar" ||
         operation == "vector_scalar_subtract" || operation == "vector_divide_scalar" ||
         operation == "vector_scalar_divide" || operation == "vector_negate" || operation == "vector_absolute" ||
         operation == "vector_square" || operation == "vector_minimum" || operation == "vector_maximum" ||
         operation == "vector_clamp" || operation == "complex_vector_add" || operation == "complex_vector_subtract" ||
         operation == "complex_vector_multiply" || operation == "complex_vector_divide" ||
         operation == "complex_vector_add_scalar" || operation == "complex_vector_scale";
}

template <typename T>
[[nodiscard]] std::string_view selected_elementwise_backend(const std::string_view operation, const std::size_t size,
                                                            const bool vector_overload = true) noexcept {
  using value_type = std::remove_cv_t<T>;
  using Operation = ksj::array::detail::VectorElementwiseOperation;

  if (operation == "vector_fill") {
    if (policy_range_contains(size, ksj::array::detail::intel_vector_fill_min_elements<value_type>(),
                              ksj::array::detail::intel_vector_fill_max_elements<value_type>())) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_vector_fill_min_elements<value_type>()) {
      return "eigen_detail";
    }
    return "manual_std_fill";
  }
  if (operation == "vector_copy") {
    if (size >= ksj::array::detail::intel_vector_copy_min_elements<value_type>() &&
        ksj::array::detail::intel_vector_copy_size_enabled<value_type>(size)) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_vector_copy_min_elements<value_type>() &&
        ksj::array::detail::eigen_vector_copy_size_enabled<value_type>(size)) {
      return "eigen_detail";
    }
    return "manual_std_copy";
  }
  if (operation == "vector_sum") {
    if (ksj::array::detail::intel_vector_sum_size_enabled<value_type>(size)) {
      return "intel_detail";
    }
    if (ksj::array::detail::eigen_vector_sum_size_enabled<value_type>(size)) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  if (operation == "vector_min" || operation == "vector_max") {
    return ksj::array::detail::intel_vector_reduction_minmax_size_enabled<value_type>(size) ? "intel_detail"
                                                                                            : "manual_loop";
  }

  const auto policy_operation = vector_elementwise_operation(operation);
  if (!policy_operation.has_value()) {
    return {};
  }

  if (operation == "vector_divide_scalar" &&
      (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) &&
      size > ksj::array::detail::small_direct_vector_elementwise_max_elements<value_type>() && size <= 1024U) {
    return "intel_detail";
  }
  if (vector_overload && *policy_operation == Operation::sqrt &&
      (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) && size <= 16U) {
    return "manual_loop";
  }
  if (vector_overload && *policy_operation == Operation::log && std::is_same_v<value_type, float> && size <= 16U) {
    return "manual_loop";
  }
  const auto direct_max = *policy_operation == Operation::divide
                            ? ksj::array::detail::direct_vector_divide_max_elements<value_type>()
                            : ksj::array::detail::small_direct_vector_elementwise_max_elements<value_type>();
  if (vector_overload && uses_small_direct_vector_loop(operation) &&
      ksj::array::detail::small_direct_vector_elementwise_enabled<value_type>() && size <= direct_max) {
    return "manual_loop";
  }
  if (!vector_overload && *policy_operation == Operation::add &&
      !ksj::array::detail::intel_multidimensional_add_size_enabled<value_type>(size)) {
    return "manual_loop";
  }

  if (*policy_operation == Operation::divide &&
      policy_range_contains(size, ksj::array::detail::mkl_vml_vector_divide_min_elements<value_type>(),
                            ksj::array::detail::mkl_vml_vector_divide_max_elements<value_type>()) &&
      ksj::array::detail::mkl_vml_vector_divide_size_enabled<value_type>(size)) {
    return "mkl_vml_detail";
  }
  if (policy_range_contains(
        size, ksj::array::detail::mkl_vml_vector_unary_elementwise_min_elements<value_type>(*policy_operation),
        ksj::array::detail::mkl_vml_vector_unary_elementwise_max_elements<value_type>(*policy_operation)) &&
      ksj::array::detail::mkl_vml_vector_elementwise_size_enabled<value_type>(*policy_operation, size)) {
    return vector_overload ? "mkl_vml_detail" : "flatten_mkl_vml_detail";
  }

  // hypot keeps std::hypot as its fallback because sqrt(x*x + y*y) is not equivalent for extreme values.
  if (*policy_operation == Operation::hypot) {
    return "manual_loop";
  }

  const auto intel_min = ksj::array::detail::intel_vector_elementwise_min_elements<value_type>(*policy_operation);
  const auto intel_max = ksj::array::detail::intel_vector_elementwise_max_elements<value_type>(*policy_operation);
  if (policy_range_contains(size, intel_min, intel_max) &&
      ksj::array::detail::intel_vector_elementwise_size_enabled<value_type>(*policy_operation, size)) {
    return vector_overload ? "intel_detail" : "flatten_intel_detail";
  }

  const auto eigen_min = ksj::array::detail::eigen_vector_elementwise_min_elements<value_type>(*policy_operation);
  const auto eigen_max = ksj::array::detail::eigen_vector_elementwise_max_elements<value_type>(*policy_operation);
  if (policy_range_contains(size, eigen_min, eigen_max) &&
      ksj::array::detail::eigen_vector_elementwise_size_enabled<value_type>(*policy_operation, size)) {
    return vector_overload ? "eigen_detail" : "flatten_eigen_detail";
  }

  return "manual_loop";
}

template <typename T>
[[nodiscard]] std::string_view selected_complex_backend(const std::string_view operation,
                                                        const std::size_t size) noexcept {
  using complex_type = std::complex<T>;
  using Policy = ksj::array::detail::ArrayDispatchPolicy;

  if (operation == "complex_vector_abs") {
    if (policy_range_contains(size, ksj::array::detail::mkl_vml_complex_magnitude_min_elements<complex_type>(),
                              ksj::array::detail::mkl_vml_complex_magnitude_max_elements<complex_type>())) {
      return "mkl_vml_detail";
    }
    const auto intel_min = std::is_same_v<T, float> ? Policy::intel_complex_magnitude_float_min_elements
                                                    : Policy::intel_complex_magnitude_double_min_elements;
    if (size >= intel_min) {
      return "intel_detail";
    }
    const auto eigen_min = std::is_same_v<T, float> ? Policy::eigen_complex_magnitude_float_min_elements
                                                    : Policy::eigen_complex_magnitude_double_min_elements;
    return size >= eigen_min ? "eigen_detail" : "manual_loop";
  }
  if (operation == "complex_vector_real" || operation == "complex_vector_imag") {
    const auto component_operation = operation == "complex_vector_real"
                                       ? ksj::array::detail::ComplexComponentOperation::real
                                       : ksj::array::detail::ComplexComponentOperation::imag;
    if (size >= ksj::array::detail::intel_complex_component_min_elements<complex_type>() &&
        ksj::array::detail::intel_complex_component_size_enabled<complex_type>(component_operation, size)) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_complex_component_min_elements<complex_type>()) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  if (operation == "complex_vector_split") {
    if (policy_range_contains(size, ksj::array::detail::intel_complex_split_min_elements<complex_type>(),
                              ksj::array::detail::intel_complex_split_max_elements<complex_type>()) &&
        ksj::array::detail::intel_complex_split_size_enabled<complex_type>(size)) {
      return "intel_detail";
    }
    if (policy_range_contains(size, ksj::array::detail::eigen_complex_split_min_elements<complex_type>(),
                              ksj::array::detail::eigen_complex_split_max_elements<complex_type>())) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  if (operation == "complex_vector_from_real_imag") {
    if (size >= ksj::array::detail::intel_complex_from_real_imag_min_elements<complex_type>()) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_complex_from_real_imag_min_elements<complex_type>()) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  if (operation == "complex_vector_conjugate") {
    if (policy_range_contains(size, ksj::array::detail::mkl_vml_complex_conjugate_min_elements<complex_type>(),
                              ksj::array::detail::mkl_vml_complex_conjugate_max_elements<complex_type>()) &&
        ksj::array::detail::mkl_vml_complex_conjugate_size_enabled<complex_type>(size)) {
      return "mkl_vml_detail";
    }
    if (size >= ksj::array::detail::intel_complex_conjugate_min_elements<complex_type>()) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_complex_conjugate_min_elements<complex_type>()) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  if (operation == "complex_vector_rectangular_to_polar_split" ||
      operation == "complex_vector_polar_to_rectangular_split") {
    if (size >= ksj::array::detail::intel_complex_polar_min_elements<complex_type>()) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_complex_polar_min_elements<complex_type>()) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  if (operation == "complex_vector_multiply_conjugate") {
    if (size >= ksj::array::detail::intel_complex_multiply_conjugate_min_elements<complex_type>()) {
      return "intel_detail";
    }
    if (size >= ksj::array::detail::eigen_complex_multiply_conjugate_min_elements<complex_type>() &&
        ksj::array::detail::eigen_complex_multiply_conjugate_size_enabled<complex_type>(size)) {
      return "eigen_detail";
    }
    return "manual_loop";
  }
  return selected_elementwise_backend<complex_type>(operation, size);
}

[[nodiscard]] inline constexpr bool uses_vml_enhanced_performance_accuracy(const std::string_view operation) noexcept {
  return operation == "vector_sqrt" || operation == "vector_exp" || operation == "vector_log" ||
         operation == "complex_vector_abs" || operation == "complex_vector_divide";
}

template <typename T>
[[nodiscard]] constexpr double vml_relative_tolerance(const std::string_view operation,
                                                      const std::string_view effective_backend) noexcept {
  if (effective_backend != "mkl_vml_detail" || !uses_vml_enhanced_performance_accuracy(operation)) {
    return -1.0;
  }
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return 2.5e-4;
  } else {
    return 2.0e-9;
  }
}

template <typename T>
[[nodiscard]] double benchmark_relative_tolerance(const std::string_view operation, const std::size_t size,
                                                  const std::string_view effective_backend) noexcept {
  auto tolerance = vml_relative_tolerance<T>(operation, effective_backend);
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    if (operation == "vector_sum") {
      const auto accumulation_tolerance =
        64.0 * static_cast<double>(std::numeric_limits<float>::epsilon()) * std::sqrt(static_cast<double>(size));
      tolerance = std::max(tolerance, std::max(1.0e-5, accumulation_tolerance));
    } else if (operation == "complex_vector_squared_norm") {
      tolerance = std::max(tolerance, 1.0e-4);
    }
  } else if (operation == "complex_vector_squared_norm") {
    tolerance = std::max(tolerance, 2.0e-9);
  }
  return tolerance;
}

[[nodiscard]] inline std::size_t largest_divisor_at_most(const std::size_t value, const std::size_t limit) noexcept {
  const auto capped_limit = std::min(value, limit);
  for (std::size_t candidate = capped_limit; candidate > 1U; --candidate) {
    if (value % candidate == 0U) {
      return candidate;
    }
  }
  return 1U;
}

[[nodiscard]] inline std::size_t integer_sqrt_floor(const std::size_t value) noexcept {
  std::size_t root = 1U;
  while ((root + 1U) <= value / (root + 1U)) {
    ++root;
  }
  return root;
}

[[nodiscard]] inline Shape2D element_count_shape2d(const std::size_t element_count) noexcept {
  const auto rows = largest_divisor_at_most(element_count, integer_sqrt_floor(element_count));
  return {rows, element_count / rows};
}

[[nodiscard]] inline Shape3D element_count_shape3d(const std::size_t element_count) noexcept {
  const auto dim2 = largest_divisor_at_most(element_count, 16U);
  const auto plane = element_count / dim2;
  const auto shape2d = element_count_shape2d(plane);
  return {shape2d.rows, shape2d.cols, dim2};
}

[[nodiscard]] inline Shape4D element_count_shape4d(const std::size_t element_count) noexcept {
  const auto dim0 = largest_divisor_at_most(element_count, 8U);
  const auto volume = element_count / dim0;
  const auto shape3d = element_count_shape3d(volume);
  return {dim0, shape3d.dim0, shape3d.dim1, shape3d.dim2};
}

[[nodiscard]] inline Shape4D calibration_shape4d(const std::size_t element_count) noexcept {
  for (std::size_t dim0 = std::min<std::size_t>(8U, element_count); dim0 > 0U; --dim0) {
    if (element_count % dim0 != 0U) {
      continue;
    }
    const auto volume = element_count / dim0;
    for (std::size_t dim3 = std::min<std::size_t>(16U, volume); dim3 > 0U; --dim3) {
      if (volume % dim3 != 0U) {
        continue;
      }
      const auto plane = volume / dim3;
      const auto shape2d = element_count_shape2d(plane);
      if (shape2d.rows >= 3U && shape2d.cols >= 3U && dim3 >= 3U) {
        return {dim0, shape2d.rows, shape2d.cols, dim3};
      }
    }
  }
  return element_count_shape4d(element_count);
}

template <typename T> using EigenVector = Eigen::Matrix<T, Eigen::Dynamic, 1, Eigen::ColMajor>;

template <typename T> using EigenMatrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::ColMajor>;

template <typename T> using EigenImage = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

template <typename T> void fill_eigen_vector(EigenVector<T>& vector) {
  for (Eigen::Index i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(static_cast<double>((static_cast<std::size_t>(i) % 251U) + 1U) * 0.125);
  }
}

template <typename T> void fill_eigen_matrix(EigenMatrix<T>& matrix) {
  for (Eigen::Index col = 0; col < matrix.cols(); ++col) {
    for (Eigen::Index row = 0; row < matrix.rows(); ++row) {
      matrix(row, col) = static_cast<T>(static_cast<double>((static_cast<std::size_t>(row) % 251U) + 1U) * 0.25 +
                                        static_cast<double>((static_cast<std::size_t>(col) % 127U) + 1U) * 0.125);
    }
  }
}

template <typename T> void fill_eigen_image(EigenImage<T>& image) {
  for (Eigen::Index row = 0; row < image.rows(); ++row) {
    for (Eigen::Index col = 0; col < image.cols(); ++col) {
      image(row, col) = static_cast<T>(
        static_cast<double>((static_cast<std::size_t>(row) * 17U + static_cast<std::size_t>(col) * 31U) % 101U) /
        100.0);
    }
  }
}

template <typename T> void fill_complex_image(ksj::array::PooledImage<std::complex<T>>& image) {
  for (std::size_t row = 0; row < image.rows(); ++row) {
    for (std::size_t col = 0; col < image.cols(); ++col) {
      const auto real = static_cast<T>(static_cast<double>((row * 17U + col * 31U) % 251U) * 0.125);
      const auto imag = static_cast<T>(static_cast<double>((row * 13U + col * 19U) % 127U) * 0.0625);
      image(row, col) = {real, imag};
    }
  }
}

template <typename T>
[[nodiscard]] double checksum_complex_image(const ksj::array::PooledImage<std::complex<T>>& image) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < image.size(); ++index) {
    checksum += static_cast<double>(image.data()[index].real() + image.data()[index].imag());
  }
  return checksum;
}

void run_basic_benchmarks_float(const ksj::benchmarks::Config& config);
void run_basic_benchmarks_double(const ksj::benchmarks::Config& config);

void run_complex_benchmarks_float(const ksj::benchmarks::Config& config);
void run_complex_benchmarks_double(const ksj::benchmarks::Config& config);

void run_difference_benchmarks_complex_float(const ksj::benchmarks::Config& config);
void run_channel_volume_benchmarks_complex_float(const ksj::benchmarks::Config& config);
void run_calibration_benchmarks_complex_float(const ksj::benchmarks::Config& config);

} // namespace ksj::benchmarks::array_benchmarks
