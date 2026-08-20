#pragma once

#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/scalar_traits.hpp"
#include "kspacejet/base/types.hpp"

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::array::detail {

struct ArrayDispatchPolicy {
  // Tuned by docs/benchmark_reports/2026-07-24/kspacejet-numerics/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  static constexpr std::size_t disabled_backend_min_elements = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t enabled_backend_max_elements = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t backend_block_min_elements = 256U;
  static constexpr std::size_t backend_block_max_count = 4096U;
  static constexpr std::size_t backend_short_block_max_count = 16U;

  // IPP remains the default for contiguous complex magnitude/phase except for benchmark-selected VML magnitude ranges.
  static constexpr std::size_t intel_complex_magnitude_float_min_elements = 1U;
  static constexpr std::size_t intel_complex_magnitude_double_min_elements = 1U;
  static constexpr std::size_t mkl_vml_complex_magnitude_float_min_elements = 262144U;
  static constexpr std::size_t mkl_vml_complex_magnitude_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_complex_magnitude_double_min_elements = 64U;
  static constexpr std::size_t mkl_vml_complex_magnitude_double_max_elements = 65536U;
  static constexpr std::size_t intel_complex_phase_float_min_elements = 1U;
  static constexpr std::size_t intel_complex_phase_double_min_elements = 1U;
  static constexpr std::size_t intel_complex_component_float_min_elements = 1024U;
  static constexpr std::size_t intel_complex_component_double_min_elements = 512U;
  static constexpr std::size_t intel_complex_split_float_min_elements = 1024U;
  static constexpr std::size_t intel_complex_split_float_max_elements = 65536U;
  static constexpr std::size_t intel_complex_split_double_min_elements = 1024U;
  static constexpr std::size_t intel_complex_split_double_max_elements = 262144U;
  static constexpr std::size_t intel_complex_from_real_imag_float_min_elements = 65536U;
  static constexpr std::size_t intel_complex_from_real_imag_double_min_elements = 262144U;
  static constexpr std::size_t intel_complex_conjugate_float_min_elements = 128U;
  static constexpr std::size_t intel_complex_conjugate_double_min_elements = 128U;
  static constexpr std::size_t mkl_vml_complex_conjugate_float_min_elements = 512U;
  static constexpr std::size_t mkl_vml_complex_conjugate_float_max_elements = 1024U;
  static constexpr std::size_t mkl_vml_complex_conjugate_double_min_elements = 256U;
  static constexpr std::size_t mkl_vml_complex_conjugate_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t intel_complex_polar_float_min_elements = 1U;
  static constexpr std::size_t intel_complex_polar_double_min_elements = 1U;
  static constexpr std::size_t intel_complex_multiply_conjugate_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_complex_multiply_conjugate_double_min_elements = disabled_backend_min_elements;

  // Eigen remains as the base backend fallback when a future build disables IPP or the view shape is unsupported.
  static constexpr std::size_t eigen_complex_component_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_component_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_split_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_split_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_complex_split_double_min_elements = 4096U;
  static constexpr std::size_t eigen_complex_split_double_max_elements = 65536U;
  static constexpr std::size_t eigen_complex_from_real_imag_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_from_real_imag_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_magnitude_float_min_elements = 1U;
  static constexpr std::size_t eigen_complex_magnitude_double_min_elements = 1U;
  static constexpr std::size_t eigen_complex_phase_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_phase_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_conjugate_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_conjugate_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_rectangular_to_polar_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_rectangular_to_polar_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_polar_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_polar_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_complex_multiply_conjugate_float_min_elements = 32U;
  static constexpr std::size_t eigen_complex_multiply_conjugate_double_min_elements = 64U;

  // Dimwise Array4D/Cube primitives are hot in PICS SBI/CG paths. The contiguous detail kernel is the default
  // candidate for KSpaceJet row-major Pooled views; benchmark rows keep manual/public/detail visible before tightening
  // thresholds.
  static constexpr std::size_t eigen_array4d_cube_multiply_min_elements = 1U;
  static constexpr std::size_t eigen_reduce_conjugate_product_min_elements = 1U;
  static constexpr std::size_t eigen_cube_abs_sum_squared_min_elements = 1U;
  static constexpr std::size_t eigen_sliding_patch_matrix_min_elements = 1U;

  static constexpr std::size_t intel_vector_fill_float_min_elements = 512U;
  static constexpr std::size_t intel_vector_fill_float_max_elements = 4096U;
  static constexpr std::size_t intel_vector_fill_double_min_elements = 256U;
  static constexpr std::size_t intel_vector_fill_double_max_elements = 262144U;
  static constexpr std::size_t intel_vector_fill_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_fill_complex_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t intel_vector_fill_complex_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_fill_complex_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_vector_fill_float_min_elements = 65536U;
  static constexpr std::size_t eigen_vector_fill_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_fill_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_fill_complex_double_min_elements = disabled_backend_min_elements;

  static constexpr std::size_t intel_vector_copy_float_min_elements = 16384U;
  static constexpr std::size_t intel_vector_copy_double_min_elements = 16384U;
  static constexpr std::size_t intel_vector_copy_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_copy_complex_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_copy_float_min_elements = 65536U;
  static constexpr std::size_t eigen_vector_copy_double_min_elements = 4096U;
  static constexpr std::size_t eigen_vector_copy_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_copy_complex_double_min_elements = disabled_backend_min_elements;

  static constexpr std::size_t intel_vector_elementwise_float_min_elements = 256U;
  static constexpr std::size_t intel_vector_elementwise_double_min_elements = 128U;
  static constexpr std::size_t intel_vector_elementwise_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_elementwise_complex_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_elementwise_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_elementwise_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_elementwise_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_elementwise_complex_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_multidimensional_add_float_min_elements = 512U;
  static constexpr std::size_t intel_multidimensional_add_float_max_elements = 262144U;
  static constexpr std::size_t intel_multidimensional_add_double_min_elements = 256U;
  static constexpr std::size_t intel_multidimensional_add_double_max_elements = 65536U;

  static constexpr std::size_t intel_vector_scalar_elementwise_float_min_elements = 1U;
  static constexpr std::size_t intel_vector_scalar_elementwise_double_min_elements = 1U;
  static constexpr std::size_t intel_vector_scalar_elementwise_complex_float_min_elements =
    disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_scalar_elementwise_complex_double_min_elements =
    disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_scalar_elementwise_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_scalar_elementwise_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_scalar_elementwise_complex_float_min_elements =
    disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_scalar_elementwise_complex_double_min_elements =
    disabled_backend_min_elements;

  static constexpr std::size_t intel_vector_unary_elementwise_float_min_elements = 256U;
  static constexpr std::size_t intel_vector_unary_elementwise_double_min_elements = 128U;
  static constexpr std::size_t eigen_vector_unary_elementwise_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_unary_elementwise_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_inverse_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_inverse_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_inverse_sqrt_float_min_elements = 1U;
  static constexpr std::size_t eigen_vector_inverse_sqrt_double_min_elements = 1U;

  static constexpr std::size_t intel_vector_minmax_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_minmax_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_minmax_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_minmax_double_min_elements = disabled_backend_min_elements;

  // Extended vector elementwise policy is intentionally operation-specific. The benchmark matrix in
  // tests/benchmarks/kspacejet-array compares manual/public/eigen/intel rows; only operations with a stable backend win
  // are enabled here.
  static constexpr std::size_t intel_vector_sqrt_float_min_elements = 64U;
  static constexpr std::size_t intel_vector_sqrt_double_min_elements = 1U;
  static constexpr std::size_t intel_vector_exp_float_min_elements = 1U;
  static constexpr std::size_t intel_vector_exp_double_min_elements = 1U;
  static constexpr std::size_t intel_vector_log_float_min_elements = 1U;
  static constexpr std::size_t intel_vector_log_double_min_elements = 1U;
  static constexpr std::size_t intel_vector_minimum_float_min_elements = 1U;
  static constexpr std::size_t intel_vector_minimum_double_min_elements = 1U;
  static constexpr std::size_t intel_vector_maximum_float_min_elements = 1024U;
  static constexpr std::size_t intel_vector_maximum_double_min_elements = 1U;
  static constexpr std::size_t intel_vector_clamp_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vector_clamp_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_clamp_float_min_elements = 4096U;
  static constexpr std::size_t eigen_vector_clamp_double_min_elements = disabled_backend_min_elements;

  // Tuned by docs/benchmark_reports/2026-07-23/kspacejet-array/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  // MKL VML is benchmark-selected for expensive real unary math where it beats the IPP elementwise backend on the
  // benchmark host. Keep narrow windows for operations that lose again at very large sizes.
  static constexpr std::size_t mkl_vml_vector_sqrt_float_min_elements = 512U;
  static constexpr std::size_t mkl_vml_vector_sqrt_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_sqrt_double_min_elements = 1U;
  static constexpr std::size_t mkl_vml_vector_sqrt_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_exp_float_min_elements = 256U;
  static constexpr std::size_t mkl_vml_vector_exp_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_exp_double_min_elements = 64U;
  static constexpr std::size_t mkl_vml_vector_exp_double_max_elements = 65536U;
  static constexpr std::size_t mkl_vml_vector_log_float_min_elements = 64U;
  static constexpr std::size_t mkl_vml_vector_log_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_log_double_min_elements = 1U;
  static constexpr std::size_t mkl_vml_vector_log_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_divide_float_min_elements = 512U;
  static constexpr std::size_t mkl_vml_vector_divide_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_divide_double_min_elements = 256U;
  static constexpr std::size_t mkl_vml_vector_divide_double_max_elements = 65536U;
  static constexpr std::size_t mkl_vml_vector_inverse_float_min_elements = 256U;
  static constexpr std::size_t mkl_vml_vector_inverse_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_inverse_double_min_elements = 128U;
  static constexpr std::size_t mkl_vml_vector_inverse_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_inverse_sqrt_float_min_elements = 64U;
  static constexpr std::size_t mkl_vml_vector_inverse_sqrt_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_inverse_sqrt_double_min_elements = 32U;
  static constexpr std::size_t mkl_vml_vector_inverse_sqrt_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_hypot_float_min_elements = 1U;
  static constexpr std::size_t mkl_vml_vector_hypot_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t mkl_vml_vector_hypot_double_min_elements = 1U;
  static constexpr std::size_t mkl_vml_vector_hypot_double_max_elements = enabled_backend_max_elements;

  // Complex elementwise policy is intentionally operation-specific. Tuned by
  // docs/benchmark_reports/2026-07-23/kspacejet-array/xeon-silver-4410y-avx512-linux/benchmark_report.md. Benchmark
  // results show IPP is the clear winner for complex subtract/multiply/scale and complex-double divide. MKL VML wins
  // complex-float divide from 128 elements and selected complex-double magnitude/conjugate ranges on the benchmark
  // host.
  static constexpr std::size_t intel_vector_add_complex_float_min_elements = 128U;
  static constexpr std::size_t intel_vector_add_complex_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t intel_vector_add_complex_double_min_elements = 64U;
  static constexpr std::size_t intel_vector_add_complex_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_vector_add_complex_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_add_complex_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_vector_add_complex_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_add_complex_double_max_elements = enabled_backend_max_elements;

  static constexpr std::size_t intel_vector_add_scalar_complex_float_min_elements = 128U;
  static constexpr std::size_t intel_vector_add_scalar_complex_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t intel_vector_add_scalar_complex_double_min_elements = 64U;
  static constexpr std::size_t intel_vector_add_scalar_complex_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_vector_add_scalar_complex_float_min_elements = 1U;
  static constexpr std::size_t eigen_vector_add_scalar_complex_float_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_vector_add_scalar_complex_double_min_elements = 1U;
  static constexpr std::size_t eigen_vector_add_scalar_complex_double_max_elements = 65535U;

  static constexpr std::size_t intel_vector_complex_arithmetic_min_elements = 1U;
  static constexpr std::size_t intel_vector_complex_arithmetic_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t intel_vector_subtract_complex_float_min_elements = 128U;
  static constexpr std::size_t intel_vector_subtract_complex_double_min_elements = 64U;
  static constexpr std::size_t mkl_vml_vector_divide_complex_float_min_elements = 64U;
  static constexpr std::size_t mkl_vml_vector_divide_complex_float_max_elements = 65536U;
  static constexpr std::size_t mkl_vml_vector_divide_complex_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t mkl_vml_vector_divide_complex_double_max_elements = enabled_backend_max_elements;
  static constexpr std::size_t eigen_vector_complex_arithmetic_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t eigen_vector_complex_arithmetic_max_elements = enabled_backend_max_elements;

  static constexpr std::size_t intel_vector_sum_float_min_elements = 1024U;
  static constexpr std::size_t intel_vector_sum_float_max_elements = 262144U;
  static constexpr std::size_t intel_vector_sum_double_min_elements = 128U;
  static constexpr std::size_t intel_vector_sum_double_max_elements = 262144U;
  static constexpr std::size_t eigen_vector_sum_float_min_elements = 128U;
  static constexpr std::size_t eigen_vector_sum_float_max_elements = 512U;
  static constexpr std::size_t eigen_vector_sum_float_large_min_elements = 1048576U;
  static constexpr std::size_t eigen_vector_sum_double_large_min_elements = 1048576U;
  static constexpr std::size_t intel_vector_reduction_minmax_float_min_elements = 64U;
  static constexpr std::size_t intel_vector_reduction_minmax_double_min_elements = 64U;
};

enum class VectorElementwiseOperation {
  add,
  subtract,
  multiply,
  divide,
  add_scalar,
  subtract_scalar,
  scalar_subtract,
  scale,
  divide_scalar,
  scalar_divide,
  negate,
  absolute,
  square,
  sqrt,
  inverse,
  inverse_sqrt,
  exp,
  log,
  hypot,
  minimum,
  maximum,
  clamp,
};

enum class ComplexComponentOperation {
  real,
  imag,
};

template <typename InputView, typename OutputView>
[[nodiscard]] bool views_are_contiguous(const InputView& input, const OutputView& output) noexcept {
  return input.is_contiguous() && output.is_contiguous();
}

template <typename... Views> [[nodiscard]] bool all_views_are_contiguous(const Views&... views) noexcept {
  return (... && views.is_contiguous());
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_fill_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_fill_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_fill_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::intel_vector_fill_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::intel_vector_fill_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_fill_max_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_fill_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_fill_double_max_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::intel_vector_fill_complex_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::intel_vector_fill_complex_double_max_elements;
  } else {
    return ArrayDispatchPolicy::enabled_backend_max_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_fill_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_fill_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_fill_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::eigen_vector_fill_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::eigen_vector_fill_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_copy_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_copy_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_copy_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::intel_vector_copy_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::intel_vector_copy_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_copy_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_copy_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_copy_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::eigen_vector_copy_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::eigen_vector_copy_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr bool intel_vector_copy_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return (element_count >= 16384U && element_count <= 32768U) || element_count >= 1048576U;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return element_count >= 16384U;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool eigen_vector_copy_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return element_count >= 65536U && element_count <= 131071U;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return element_count >= 4096U && element_count <= 8192U;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_elementwise_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_elementwise_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_elementwise_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::intel_vector_elementwise_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::intel_vector_elementwise_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_elementwise_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_elementwise_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_elementwise_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::eigen_vector_elementwise_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::eigen_vector_elementwise_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_scalar_elementwise_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_scalar_elementwise_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_scalar_elementwise_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::intel_vector_scalar_elementwise_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::intel_vector_scalar_elementwise_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_scalar_elementwise_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_scalar_elementwise_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_scalar_elementwise_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::eigen_vector_scalar_elementwise_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::eigen_vector_scalar_elementwise_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_unary_elementwise_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_unary_elementwise_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_unary_elementwise_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_unary_elementwise_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_unary_elementwise_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_unary_elementwise_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_minmax_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_minmax_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_minmax_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_sqrt_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_sqrt_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_sqrt_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_exp_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_exp_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_exp_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_log_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_log_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_log_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_minimum_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_minimum_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_minimum_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_maximum_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_maximum_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_maximum_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_vector_clamp_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_vector_clamp_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_vector_clamp_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_minmax_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_minmax_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_minmax_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_vector_clamp_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_vector_clamp_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_vector_clamp_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr std::size_t
intel_vector_elementwise_min_elements(const VectorElementwiseOperation operation) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::intel_vector_add_complex_float_min_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::intel_vector_add_scalar_complex_float_min_elements;
      case VectorElementwiseOperation::subtract:
        return ArrayDispatchPolicy::intel_vector_subtract_complex_float_min_elements;
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::intel_vector_complex_arithmetic_min_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::disabled_backend_min_elements;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::intel_vector_add_complex_double_min_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::intel_vector_add_scalar_complex_double_min_elements;
      case VectorElementwiseOperation::subtract:
        return ArrayDispatchPolicy::intel_vector_subtract_complex_double_min_elements;
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::intel_vector_complex_arithmetic_min_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::disabled_backend_min_elements;
      default:
        break;
    }
  } else {
    switch (operation) {
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
        return intel_vector_scalar_elementwise_min_elements<value_type>();
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
        return intel_vector_unary_elementwise_min_elements<value_type>();
      case VectorElementwiseOperation::sqrt:
        return intel_vector_sqrt_min_elements<value_type>();
      case VectorElementwiseOperation::exp:
        return intel_vector_exp_min_elements<value_type>();
      case VectorElementwiseOperation::log:
        return intel_vector_log_min_elements<value_type>();
      case VectorElementwiseOperation::minimum:
        return intel_vector_minimum_min_elements<value_type>();
      case VectorElementwiseOperation::maximum:
        return intel_vector_maximum_min_elements<value_type>();
      case VectorElementwiseOperation::clamp:
        return intel_vector_clamp_min_elements<value_type>();
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
        return intel_vector_elementwise_min_elements<value_type>();
      default:
        break;
    }
  }
  return ArrayDispatchPolicy::disabled_backend_min_elements;
}

template <typename T>
[[nodiscard]] constexpr bool intel_vector_elementwise_size_enabled(const VectorElementwiseOperation operation,
                                                                   const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    switch (operation) {
      case VectorElementwiseOperation::add_scalar:
        return true;
      case VectorElementwiseOperation::add:
        return element_count < 2048U || element_count >= 8192U;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return true;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return false;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    switch (operation) {
      case VectorElementwiseOperation::add_scalar:
        return element_count <= 1024U || element_count >= 65536U;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return true;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return false;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, float>) {
    switch (operation) {
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::square:
        return element_count <= 4096U || element_count >= 262144U;
      case VectorElementwiseOperation::absolute:
        return element_count <= 8192U;
      case VectorElementwiseOperation::divide_scalar:
        return (element_count >= 512U && element_count <= 4096U) ||
               (element_count >= 262144U && element_count <= 524288U) || element_count >= 1048576U;
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::clamp:
        return false;
      case VectorElementwiseOperation::minimum:
        return (element_count >= 1024U && element_count <= 65536U) || element_count >= 1048576U;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::maximum:
        return true;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, double>) {
    switch (operation) {
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
        return element_count <= 1024U || element_count >= 262144U;
      case VectorElementwiseOperation::divide_scalar:
        return (element_count >= 256U && element_count <= 1024U) ||
               (element_count >= 8192U && element_count <= 32768U) ||
               (element_count >= 131072U && element_count <= 524288U);
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::square:
        return (element_count >= 128U && element_count <= 1024U) || element_count >= 65536U;
      case VectorElementwiseOperation::absolute:
        return element_count <= 1024U || element_count >= 262144U;
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::clamp:
        return false;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
        return true;
      default:
        break;
    }
  }
  return true;
}

template <typename T>
[[nodiscard]] constexpr bool intel_multidimensional_add_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return element_count >= ArrayDispatchPolicy::intel_multidimensional_add_float_min_elements &&
           element_count <= ArrayDispatchPolicy::intel_multidimensional_add_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return element_count >= ArrayDispatchPolicy::intel_multidimensional_add_double_min_elements &&
           element_count <= ArrayDispatchPolicy::intel_multidimensional_add_double_max_elements;
  } else {
    return true;
  }
}

template <typename T>
[[nodiscard]] constexpr bool eigen_vector_elementwise_size_enabled(const VectorElementwiseOperation operation,
                                                                   const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    switch (operation) {
      case VectorElementwiseOperation::add_scalar:
        return element_count >= 4096U && element_count <= 65536U;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return true;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return false;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    switch (operation) {
      case VectorElementwiseOperation::add_scalar:
        return element_count >= 4096U && element_count <= 16384U;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return true;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return false;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, float>) {
    switch (operation) {
      case VectorElementwiseOperation::clamp:
        return element_count >= 512U;
      case VectorElementwiseOperation::scalar_divide:
        return element_count >= 32768U;
      case VectorElementwiseOperation::scalar_subtract:
        return element_count >= 8192U;
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
        return false;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
        return true;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, double>) {
    switch (operation) {
      case VectorElementwiseOperation::scalar_divide:
        return element_count >= 131072U;
      case VectorElementwiseOperation::clamp:
        return element_count >= 8192U;
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
        return false;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
        return true;
      default:
        break;
    }
  }
  return true;
}

template <typename T>
[[nodiscard]] constexpr std::size_t
intel_vector_elementwise_max_elements(const VectorElementwiseOperation operation) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::intel_vector_add_complex_float_max_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::intel_vector_add_scalar_complex_float_max_elements;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::intel_vector_complex_arithmetic_max_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::enabled_backend_max_elements;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::intel_vector_add_complex_double_max_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::intel_vector_add_scalar_complex_double_max_elements;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::intel_vector_complex_arithmetic_max_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::enabled_backend_max_elements;
      default:
        break;
    }
  }
  return ArrayDispatchPolicy::enabled_backend_max_elements;
}

template <typename T> [[nodiscard]] constexpr std::size_t mkl_vml_vector_divide_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::f32>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::f64>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_complex_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t mkl_vml_vector_divide_max_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::f32>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::f64>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_double_max_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_complex_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    return ArrayDispatchPolicy::mkl_vml_vector_divide_complex_double_max_elements;
  } else {
    return ArrayDispatchPolicy::enabled_backend_max_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr std::size_t
mkl_vml_vector_unary_elementwise_min_elements(const VectorElementwiseOperation operation) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    switch (operation) {
      case VectorElementwiseOperation::sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_sqrt_float_min_elements;
      case VectorElementwiseOperation::inverse:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_float_min_elements;
      case VectorElementwiseOperation::inverse_sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_sqrt_float_min_elements;
      case VectorElementwiseOperation::exp:
        return ArrayDispatchPolicy::mkl_vml_vector_exp_float_min_elements;
      case VectorElementwiseOperation::log:
        return ArrayDispatchPolicy::mkl_vml_vector_log_float_min_elements;
      case VectorElementwiseOperation::hypot:
        return ArrayDispatchPolicy::mkl_vml_vector_hypot_float_min_elements;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::disabled_backend_min_elements;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, double>) {
    switch (operation) {
      case VectorElementwiseOperation::sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_sqrt_double_min_elements;
      case VectorElementwiseOperation::inverse:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_double_min_elements;
      case VectorElementwiseOperation::inverse_sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_sqrt_double_min_elements;
      case VectorElementwiseOperation::exp:
        return ArrayDispatchPolicy::mkl_vml_vector_exp_double_min_elements;
      case VectorElementwiseOperation::log:
        return ArrayDispatchPolicy::mkl_vml_vector_log_double_min_elements;
      case VectorElementwiseOperation::hypot:
        return ArrayDispatchPolicy::mkl_vml_vector_hypot_double_min_elements;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::disabled_backend_min_elements;
      default:
        break;
    }
  }
  return ArrayDispatchPolicy::disabled_backend_min_elements;
}

template <typename T>
[[nodiscard]] constexpr std::size_t
mkl_vml_vector_unary_elementwise_max_elements(const VectorElementwiseOperation operation) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    switch (operation) {
      case VectorElementwiseOperation::sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_sqrt_float_max_elements;
      case VectorElementwiseOperation::inverse:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_float_max_elements;
      case VectorElementwiseOperation::inverse_sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_sqrt_float_max_elements;
      case VectorElementwiseOperation::exp:
        return ArrayDispatchPolicy::mkl_vml_vector_exp_float_max_elements;
      case VectorElementwiseOperation::log:
        return ArrayDispatchPolicy::mkl_vml_vector_log_float_max_elements;
      case VectorElementwiseOperation::hypot:
        return ArrayDispatchPolicy::mkl_vml_vector_hypot_float_max_elements;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::enabled_backend_max_elements;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, double>) {
    switch (operation) {
      case VectorElementwiseOperation::sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_sqrt_double_max_elements;
      case VectorElementwiseOperation::inverse:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_double_max_elements;
      case VectorElementwiseOperation::inverse_sqrt:
        return ArrayDispatchPolicy::mkl_vml_vector_inverse_sqrt_double_max_elements;
      case VectorElementwiseOperation::exp:
        return ArrayDispatchPolicy::mkl_vml_vector_exp_double_max_elements;
      case VectorElementwiseOperation::log:
        return ArrayDispatchPolicy::mkl_vml_vector_log_double_max_elements;
      case VectorElementwiseOperation::hypot:
        return ArrayDispatchPolicy::mkl_vml_vector_hypot_double_max_elements;
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::enabled_backend_max_elements;
      default:
        break;
    }
  }
  return ArrayDispatchPolicy::enabled_backend_max_elements;
}

template <typename T>
[[nodiscard]] constexpr bool mkl_vml_vector_elementwise_size_enabled(const VectorElementwiseOperation operation,
                                                                     const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    if (operation == VectorElementwiseOperation::sqrt) {
      return element_count <= 65536U || element_count >= 1048576U;
    }
  }
  return true;
}

template <typename T>
[[nodiscard]] constexpr bool mkl_vml_vector_divide_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return element_count <= 65536U || element_count >= 262144U;
  }
  return true;
}

template <typename T>
[[nodiscard]] constexpr std::size_t
eigen_vector_elementwise_min_elements(const VectorElementwiseOperation operation) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::eigen_vector_add_complex_float_min_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::eigen_vector_add_scalar_complex_float_min_elements;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::eigen_vector_complex_arithmetic_min_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::disabled_backend_min_elements;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::eigen_vector_add_complex_double_min_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::eigen_vector_add_scalar_complex_double_min_elements;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::eigen_vector_complex_arithmetic_min_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::disabled_backend_min_elements;
      default:
        break;
    }
  } else {
    switch (operation) {
      case VectorElementwiseOperation::add_scalar:
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::scale:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
        return eigen_vector_scalar_elementwise_min_elements<value_type>();
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
        return eigen_vector_unary_elementwise_min_elements<value_type>();
      case VectorElementwiseOperation::inverse:
        if constexpr (std::is_same_v<value_type, float>) {
          return ArrayDispatchPolicy::eigen_vector_inverse_float_min_elements;
        } else {
          return ArrayDispatchPolicy::eigen_vector_inverse_double_min_elements;
        }
      case VectorElementwiseOperation::inverse_sqrt:
        if constexpr (std::is_same_v<value_type, float>) {
          return ArrayDispatchPolicy::eigen_vector_inverse_sqrt_float_min_elements;
        } else {
          return ArrayDispatchPolicy::eigen_vector_inverse_sqrt_double_min_elements;
        }
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
        return eigen_vector_minmax_min_elements<value_type>();
      case VectorElementwiseOperation::clamp:
        return eigen_vector_clamp_min_elements<value_type>();
      case VectorElementwiseOperation::add:
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
        return eigen_vector_elementwise_min_elements<value_type>();
      default:
        break;
    }
    return eigen_vector_scalar_elementwise_min_elements<value_type>();
  }
  return ArrayDispatchPolicy::disabled_backend_min_elements;
}

template <typename T>
[[nodiscard]] constexpr std::size_t
eigen_vector_elementwise_max_elements(const VectorElementwiseOperation operation) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, ksj::base::cf32>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::eigen_vector_add_complex_float_max_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::eigen_vector_add_scalar_complex_float_max_elements;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::eigen_vector_complex_arithmetic_max_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::enabled_backend_max_elements;
      default:
        break;
    }
  } else if constexpr (std::is_same_v<value_type, ksj::base::cf64>) {
    switch (operation) {
      case VectorElementwiseOperation::add:
        return ArrayDispatchPolicy::eigen_vector_add_complex_double_max_elements;
      case VectorElementwiseOperation::add_scalar:
        return ArrayDispatchPolicy::eigen_vector_add_scalar_complex_double_max_elements;
      case VectorElementwiseOperation::subtract:
      case VectorElementwiseOperation::multiply:
      case VectorElementwiseOperation::divide:
      case VectorElementwiseOperation::scale:
        return ArrayDispatchPolicy::eigen_vector_complex_arithmetic_max_elements;
      case VectorElementwiseOperation::subtract_scalar:
      case VectorElementwiseOperation::scalar_subtract:
      case VectorElementwiseOperation::divide_scalar:
      case VectorElementwiseOperation::scalar_divide:
      case VectorElementwiseOperation::negate:
      case VectorElementwiseOperation::absolute:
      case VectorElementwiseOperation::square:
      case VectorElementwiseOperation::sqrt:
      case VectorElementwiseOperation::exp:
      case VectorElementwiseOperation::log:
      case VectorElementwiseOperation::minimum:
      case VectorElementwiseOperation::maximum:
      case VectorElementwiseOperation::clamp:
        return ArrayDispatchPolicy::enabled_backend_max_elements;
      default:
        break;
    }
  }
  return ArrayDispatchPolicy::enabled_backend_max_elements;
}

template <typename T>
[[nodiscard]] constexpr bool intel_vector_sum_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return element_count >= ArrayDispatchPolicy::intel_vector_sum_float_min_elements &&
           element_count <= ArrayDispatchPolicy::intel_vector_sum_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return element_count >= ArrayDispatchPolicy::intel_vector_sum_double_min_elements &&
           element_count <= ArrayDispatchPolicy::intel_vector_sum_double_max_elements;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool eigen_vector_sum_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return (element_count >= ArrayDispatchPolicy::eigen_vector_sum_float_min_elements &&
            element_count <= ArrayDispatchPolicy::eigen_vector_sum_float_max_elements) ||
           element_count >= ArrayDispatchPolicy::eigen_vector_sum_float_large_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return element_count >= ArrayDispatchPolicy::eigen_vector_sum_double_large_min_elements;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool intel_vector_reduction_minmax_size_enabled(const std::size_t element_count) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return element_count >= ArrayDispatchPolicy::intel_vector_reduction_minmax_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return element_count >= ArrayDispatchPolicy::intel_vector_reduction_minmax_double_min_elements;
  } else {
    return false;
  }
}

template <typename View> [[nodiscard]] bool prefer_intel_vector_fill(const View& output) noexcept {
  const auto min_elements = intel_vector_fill_min_elements<typename View::value_type>();
  const auto max_elements = intel_vector_fill_max_elements<typename View::value_type>();
  return output.is_contiguous() && min_elements != ArrayDispatchPolicy::disabled_backend_min_elements &&
         output.size() >= min_elements && output.size() <= max_elements;
}

template <typename View> [[nodiscard]] bool prefer_eigen_vector_fill(const View& output) noexcept {
  return output.is_contiguous() && output.size() >= eigen_vector_fill_min_elements<typename View::value_type>();
}

template <typename InputView, typename OutputView> [[nodiscard]] bool same_value_type_views() noexcept {
  return std::is_same_v<std::remove_cv_t<typename InputView::value_type>,
                        std::remove_cv_t<typename OutputView::value_type>>;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_vector_copy(const InputView& input, const OutputView& output) noexcept {
  using value_type = typename OutputView::value_type;
  return same_value_type_views<InputView, OutputView>() && views_are_contiguous(input, output) &&
         input.size() >= intel_vector_copy_min_elements<value_type>() &&
         intel_vector_copy_size_enabled<value_type>(input.size());
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_vector_copy(const InputView& input, const OutputView& output) noexcept {
  using value_type = typename OutputView::value_type;
  return same_value_type_views<InputView, OutputView>() && views_are_contiguous(input, output) &&
         input.size() >= eigen_vector_copy_min_elements<value_type>() &&
         eigen_vector_copy_size_enabled<value_type>(input.size());
}

template <typename LhsView, typename RhsView, typename OutputView> [[nodiscard]] bool same_value_type_views() noexcept {
  return std::is_same_v<std::remove_cv_t<typename LhsView::value_type>,
                        std::remove_cv_t<typename RhsView::value_type>> &&
         std::is_same_v<std::remove_cv_t<typename LhsView::value_type>,
                        std::remove_cv_t<typename OutputView::value_type>>;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_intel_vector_elementwise_for_elements(const VectorElementwiseOperation operation,
                                                                const std::size_t element_count, const LhsView& lhs,
                                                                const RhsView& rhs, const OutputView& output) noexcept {
  using value_type = typename OutputView::value_type;
  const auto min_elements = intel_vector_elementwise_min_elements<value_type>(operation);
  const auto max_elements = intel_vector_elementwise_max_elements<value_type>(operation);
  return same_value_type_views<LhsView, RhsView, OutputView>() && lhs.is_contiguous() && rhs.is_contiguous() &&
         output.is_contiguous() && min_elements != ArrayDispatchPolicy::disabled_backend_min_elements &&
         element_count >= min_elements && element_count <= max_elements &&
         intel_vector_elementwise_size_enabled<value_type>(operation, element_count);
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_intel_vector_elementwise(const VectorElementwiseOperation operation, const LhsView& lhs,
                                                   const RhsView& rhs, const OutputView& output) noexcept {
  return prefer_intel_vector_elementwise_for_elements(operation, lhs.size(), lhs, rhs, output);
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_vector_divide_for_elements(const std::size_t element_count, const LhsView& lhs,
                                                             const RhsView& rhs, const OutputView& output) noexcept {
  using value_type = std::remove_cv_t<typename OutputView::value_type>;
  const auto min_elements = mkl_vml_vector_divide_min_elements<value_type>();
  const auto max_elements = mkl_vml_vector_divide_max_elements<value_type>();
  return same_value_type_views<LhsView, RhsView, OutputView>() && lhs.is_contiguous() && rhs.is_contiguous() &&
         output.is_contiguous() && min_elements != ArrayDispatchPolicy::disabled_backend_min_elements &&
         element_count >= min_elements && element_count <= max_elements &&
         mkl_vml_vector_divide_size_enabled<value_type>(element_count);
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_vector_divide(const LhsView& lhs, const RhsView& rhs,
                                                const OutputView& output) noexcept {
  return prefer_mkl_vml_vector_divide_for_elements(lhs.size(), lhs, rhs, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_vector_unary_elementwise_for_elements(const VectorElementwiseOperation operation,
                                                                        const std::size_t element_count,
                                                                        const InputView& input,
                                                                        const OutputView& output) noexcept {
  using value_type = std::remove_cv_t<typename OutputView::value_type>;
  const auto min_elements = mkl_vml_vector_unary_elementwise_min_elements<value_type>(operation);
  const auto max_elements = mkl_vml_vector_unary_elementwise_max_elements<value_type>(operation);
  return (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) &&
         same_value_type_views<InputView, OutputView>() && views_are_contiguous(input, output) &&
         min_elements != ArrayDispatchPolicy::disabled_backend_min_elements && element_count >= min_elements &&
         element_count <= max_elements && mkl_vml_vector_elementwise_size_enabled<value_type>(operation, element_count);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_vector_unary_elementwise(const VectorElementwiseOperation operation,
                                                           const InputView& input, const OutputView& output) noexcept {
  return prefer_mkl_vml_vector_unary_elementwise_for_elements(operation, input.size(), input, output);
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_vector_binary_elementwise_for_elements(const VectorElementwiseOperation operation,
                                                                         const std::size_t element_count,
                                                                         const LhsView& lhs, const RhsView& rhs,
                                                                         const OutputView& output) noexcept {
  using value_type = std::remove_cv_t<typename OutputView::value_type>;
  const auto min_elements = mkl_vml_vector_unary_elementwise_min_elements<value_type>(operation);
  const auto max_elements = mkl_vml_vector_unary_elementwise_max_elements<value_type>(operation);
  return (std::is_same_v<value_type, float> || std::is_same_v<value_type, double>) &&
         same_value_type_views<LhsView, RhsView, OutputView>() && all_views_are_contiguous(lhs, rhs, output) &&
         min_elements != ArrayDispatchPolicy::disabled_backend_min_elements && element_count >= min_elements &&
         element_count <= max_elements && mkl_vml_vector_elementwise_size_enabled<value_type>(operation, element_count);
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_eigen_vector_elementwise_for_elements(const VectorElementwiseOperation operation,
                                                                const std::size_t element_count, const LhsView& lhs,
                                                                const RhsView& rhs, const OutputView& output) noexcept {
  using value_type = typename OutputView::value_type;
  const auto min_elements = eigen_vector_elementwise_min_elements<value_type>(operation);
  const auto max_elements = eigen_vector_elementwise_max_elements<value_type>(operation);
  return same_value_type_views<LhsView, RhsView, OutputView>() && lhs.is_contiguous() && rhs.is_contiguous() &&
         output.is_contiguous() && min_elements != ArrayDispatchPolicy::disabled_backend_min_elements &&
         element_count >= min_elements && element_count <= max_elements &&
         eigen_vector_elementwise_size_enabled<value_type>(operation, element_count);
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_eigen_vector_elementwise(const VectorElementwiseOperation operation, const LhsView& lhs,
                                                   const RhsView& rhs, const OutputView& output) noexcept {
  return prefer_eigen_vector_elementwise_for_elements(operation, lhs.size(), lhs, rhs, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_vector_elementwise_for_elements(const VectorElementwiseOperation operation,
                                                                const std::size_t element_count, const InputView& input,
                                                                const OutputView& output) noexcept {
  using value_type = typename OutputView::value_type;
  const auto min_elements = intel_vector_elementwise_min_elements<value_type>(operation);
  const auto max_elements = intel_vector_elementwise_max_elements<value_type>(operation);
  return same_value_type_views<InputView, OutputView>() && views_are_contiguous(input, output) &&
         min_elements != ArrayDispatchPolicy::disabled_backend_min_elements && element_count >= min_elements &&
         element_count <= max_elements && intel_vector_elementwise_size_enabled<value_type>(operation, element_count);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_vector_elementwise(const VectorElementwiseOperation operation, const InputView& input,
                                                   const OutputView& output) noexcept {
  return prefer_intel_vector_elementwise_for_elements(operation, input.size(), input, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_vector_elementwise_for_elements(const VectorElementwiseOperation operation,
                                                                const std::size_t element_count, const InputView& input,
                                                                const OutputView& output) noexcept {
  using value_type = typename OutputView::value_type;
  const auto min_elements = eigen_vector_elementwise_min_elements<value_type>(operation);
  const auto max_elements = eigen_vector_elementwise_max_elements<value_type>(operation);
  return same_value_type_views<InputView, OutputView>() && views_are_contiguous(input, output) &&
         min_elements != ArrayDispatchPolicy::disabled_backend_min_elements && element_count >= min_elements &&
         element_count <= max_elements && eigen_vector_elementwise_size_enabled<value_type>(operation, element_count);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_vector_elementwise(const VectorElementwiseOperation operation, const InputView& input,
                                                   const OutputView& output) noexcept {
  return prefer_eigen_vector_elementwise_for_elements(operation, input.size(), input, output);
}

template <typename InputView> [[nodiscard]] bool prefer_intel_vector_sum(const InputView& input) noexcept {
  return input.is_contiguous() && intel_vector_sum_size_enabled<typename InputView::value_type>(input.size());
}

template <typename InputView> [[nodiscard]] bool prefer_eigen_vector_sum(const InputView& input) noexcept {
  return input.is_contiguous() && eigen_vector_sum_size_enabled<typename InputView::value_type>(input.size());
}

template <typename InputView> [[nodiscard]] bool prefer_intel_vector_reduction_minmax(const InputView& input) noexcept {
  return input.is_contiguous() &&
         intel_vector_reduction_minmax_size_enabled<typename InputView::value_type>(input.size());
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_component_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::intel_complex_component_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::intel_complex_component_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_component_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_component_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_component_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr bool intel_complex_component_size_enabled(const ComplexComponentOperation operation,
                                                                  const std::size_t element_count) noexcept {
  using real_type = real_scalar_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<real_type, float>) {
    if (operation == ComplexComponentOperation::real) {
      return element_count >= 512U && element_count <= 2047U;
    }
    return element_count >= 1024U && element_count <= 131071U;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return (element_count >= 512U && element_count <= 8191U) || element_count >= 131072U;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_split_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_complex_split_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_complex_split_double_min_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return intel_complex_split_min_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_split_max_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_complex_split_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_complex_split_double_max_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return intel_complex_split_max_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::enabled_backend_max_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr bool intel_complex_split_size_enabled(const std::size_t element_count) noexcept {
  using real_type = real_scalar_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<real_type, double>) {
    return element_count <= 1024U || element_count >= 262144U;
  } else {
    return true;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_split_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_split_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_split_double_min_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return eigen_complex_split_min_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_split_max_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_split_float_max_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_split_double_max_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return eigen_complex_split_max_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::enabled_backend_max_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_from_real_imag_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_complex_from_real_imag_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_complex_from_real_imag_double_min_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return intel_complex_from_real_imag_min_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_from_real_imag_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_from_real_imag_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_from_real_imag_double_min_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return eigen_complex_from_real_imag_min_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_conjugate_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::intel_complex_conjugate_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::intel_complex_conjugate_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t mkl_vml_complex_conjugate_min_elements() noexcept {
  using real_type = real_scalar_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::mkl_vml_complex_conjugate_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::mkl_vml_complex_conjugate_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t mkl_vml_complex_conjugate_max_elements() noexcept {
  using real_type = real_scalar_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::mkl_vml_complex_conjugate_float_max_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::mkl_vml_complex_conjugate_double_max_elements;
  } else {
    return ArrayDispatchPolicy::enabled_backend_max_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr bool mkl_vml_complex_conjugate_size_enabled(const std::size_t element_count) noexcept {
  using real_type = real_scalar_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<real_type, double>) {
    return element_count <= 1024U || element_count >= 1048576U;
  }
  return true;
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_conjugate_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_conjugate_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_conjugate_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_polar_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::intel_complex_polar_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::intel_complex_polar_double_min_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return intel_complex_polar_min_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_polar_min_elements() noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_polar_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_polar_double_min_elements;
  } else if constexpr (is_complex_v<value_type>) {
    return eigen_complex_polar_min_elements<real_scalar_t<value_type>>();
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t intel_complex_multiply_conjugate_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::intel_complex_multiply_conjugate_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::intel_complex_multiply_conjugate_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t eigen_complex_multiply_conjugate_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::eigen_complex_multiply_conjugate_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::eigen_complex_multiply_conjugate_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T>
[[nodiscard]] constexpr bool eigen_complex_multiply_conjugate_size_enabled(const std::size_t element_count) noexcept {
  using real_type = real_scalar_t<std::remove_cv_t<T>>;
  if constexpr (std::is_same_v<real_type, double>) {
    return element_count < 32768U || element_count >= 131072U;
  }
  return true;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_component(const ComplexComponentOperation operation, const InputView& input,
                                                  const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, output) &&
         input.size() >= intel_complex_component_min_elements<input_type>() &&
         intel_complex_component_size_enabled<input_type>(operation, input.size());
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_component(const InputView& input, const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, output) &&
         input.size() >= eigen_complex_component_min_elements<input_type>();
}

template <typename InputView, typename RealOutputView, typename ImagOutputView>
[[nodiscard]] bool prefer_intel_complex_split(const InputView& input, const RealOutputView& real_output,
                                              const ImagOutputView& imag_output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, real_output, imag_output) &&
         input.size() >= intel_complex_split_min_elements<input_type>() &&
         input.size() <= intel_complex_split_max_elements<input_type>() &&
         intel_complex_split_size_enabled<input_type>(input.size());
}

template <typename InputView, typename RealOutputView, typename ImagOutputView>
[[nodiscard]] bool prefer_eigen_complex_split(const InputView& input, const RealOutputView& real_output,
                                              const ImagOutputView& imag_output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, real_output, imag_output) &&
         input.size() >= eigen_complex_split_min_elements<input_type>() &&
         input.size() <= eigen_complex_split_max_elements<input_type>();
}

template <typename RealInputView, typename ImagInputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_from_real_imag(const RealInputView& real_input, const ImagInputView& imag_input,
                                                       const OutputView& output) noexcept {
  using output_type = typename OutputView::value_type;
  return is_complex_v<output_type> && all_views_are_contiguous(real_input, imag_input, output) &&
         output.size() >= intel_complex_from_real_imag_min_elements<output_type>();
}

template <typename RealInputView, typename ImagInputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_from_real_imag(const RealInputView& real_input, const ImagInputView& imag_input,
                                                       const OutputView& output) noexcept {
  using output_type = typename OutputView::value_type;
  return is_complex_v<output_type> && all_views_are_contiguous(real_input, imag_input, output) &&
         output.size() >= eigen_complex_from_real_imag_min_elements<output_type>();
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_magnitude_for_elements(const std::size_t element_count, const InputView& input,
                                                               const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  using real_type = real_scalar_t<input_type>;
  if constexpr (!is_complex_v<input_type>) {
    return false;
  } else if constexpr (std::is_same_v<real_type, float>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::intel_complex_magnitude_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::intel_complex_magnitude_double_min_elements;
  } else {
    return false;
  }
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_magnitude(const InputView& input, const OutputView& output) noexcept {
  return prefer_intel_complex_magnitude_for_elements(input.size(), input, output);
}

template <typename T> [[nodiscard]] constexpr std::size_t mkl_vml_complex_magnitude_min_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::mkl_vml_complex_magnitude_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::mkl_vml_complex_magnitude_double_min_elements;
  } else {
    return ArrayDispatchPolicy::disabled_backend_min_elements;
  }
}

template <typename T> [[nodiscard]] constexpr std::size_t mkl_vml_complex_magnitude_max_elements() noexcept {
  using real_type = real_scalar_t<T>;
  if constexpr (std::is_same_v<real_type, float>) {
    return ArrayDispatchPolicy::mkl_vml_complex_magnitude_float_max_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return ArrayDispatchPolicy::mkl_vml_complex_magnitude_double_max_elements;
  } else {
    return ArrayDispatchPolicy::enabled_backend_max_elements;
  }
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_complex_magnitude_for_elements(const std::size_t element_count,
                                                                 const InputView& input,
                                                                 const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  if constexpr (!is_complex_v<input_type>) {
    return false;
  } else {
    const auto min_elements = mkl_vml_complex_magnitude_min_elements<input_type>();
    const auto max_elements = mkl_vml_complex_magnitude_max_elements<input_type>();
    return views_are_contiguous(input, output) && min_elements != ArrayDispatchPolicy::disabled_backend_min_elements &&
           element_count >= min_elements && element_count <= max_elements;
  }
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_complex_magnitude(const InputView& input, const OutputView& output) noexcept {
  return prefer_mkl_vml_complex_magnitude_for_elements(input.size(), input, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_magnitude_for_elements(const std::size_t element_count, const InputView& input,
                                                               const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  using real_type = real_scalar_t<input_type>;
  if constexpr (!is_complex_v<input_type>) {
    return false;
  } else if constexpr (std::is_same_v<real_type, float>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::eigen_complex_magnitude_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::eigen_complex_magnitude_double_min_elements;
  } else {
    return false;
  }
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_magnitude(const InputView& input, const OutputView& output) noexcept {
  return prefer_eigen_complex_magnitude_for_elements(input.size(), input, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_phase_for_elements(const std::size_t element_count, const InputView& input,
                                                           const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  using real_type = real_scalar_t<input_type>;
  if constexpr (!is_complex_v<input_type>) {
    return false;
  } else if constexpr (std::is_same_v<real_type, float>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::intel_complex_phase_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::intel_complex_phase_double_min_elements;
  } else {
    return false;
  }
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_phase(const InputView& input, const OutputView& output) noexcept {
  return prefer_intel_complex_phase_for_elements(input.size(), input, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_phase_for_elements(const std::size_t element_count, const InputView& input,
                                                           const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  using real_type = real_scalar_t<input_type>;
  if constexpr (!is_complex_v<input_type>) {
    return false;
  } else if constexpr (std::is_same_v<real_type, float>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::eigen_complex_phase_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return views_are_contiguous(input, output) &&
           element_count >= ArrayDispatchPolicy::eigen_complex_phase_double_min_elements;
  } else {
    return false;
  }
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_phase(const InputView& input, const OutputView& output) noexcept {
  return prefer_eigen_complex_phase_for_elements(input.size(), input, output);
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_mkl_vml_complex_conjugate(const InputView& input, const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  const auto min_elements = mkl_vml_complex_conjugate_min_elements<input_type>();
  const auto max_elements = mkl_vml_complex_conjugate_max_elements<input_type>();
  return is_complex_v<input_type> && all_views_are_contiguous(input, output) &&
         min_elements != ArrayDispatchPolicy::disabled_backend_min_elements && input.size() >= min_elements &&
         input.size() <= max_elements && mkl_vml_complex_conjugate_size_enabled<input_type>(input.size());
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_intel_complex_conjugate(const InputView& input, const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, output) &&
         input.size() >= intel_complex_conjugate_min_elements<input_type>();
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_complex_conjugate(const InputView& input, const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, output) &&
         input.size() >= eigen_complex_conjugate_min_elements<input_type>();
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_rectangular_to_polar(const InputView& input, const OutputView& output) noexcept {
  using input_type = typename InputView::value_type;
  using real_type = real_scalar_t<input_type>;
  if constexpr (!is_complex_v<input_type>) {
    return false;
  } else if constexpr (std::is_same_v<real_type, float>) {
    return views_are_contiguous(input, output) &&
           input.size() >= ArrayDispatchPolicy::eigen_rectangular_to_polar_float_min_elements;
  } else if constexpr (std::is_same_v<real_type, double>) {
    return views_are_contiguous(input, output) &&
           input.size() >= ArrayDispatchPolicy::eigen_rectangular_to_polar_double_min_elements;
  } else {
    return false;
  }
}

template <typename InputView, typename MagnitudeOutputView, typename PhaseOutputView>
[[nodiscard]] bool prefer_intel_rectangular_to_polar(const InputView& input,
                                                     const MagnitudeOutputView& magnitude_output,
                                                     const PhaseOutputView& phase_output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, magnitude_output, phase_output) &&
         input.size() >= intel_complex_polar_min_elements<input_type>();
}

template <typename InputView, typename MagnitudeOutputView, typename PhaseOutputView>
[[nodiscard]] bool prefer_eigen_rectangular_to_polar(const InputView& input,
                                                     const MagnitudeOutputView& magnitude_output,
                                                     const PhaseOutputView& phase_output) noexcept {
  using input_type = typename InputView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(input, magnitude_output, phase_output) &&
         input.size() >= eigen_complex_polar_min_elements<input_type>();
}

template <typename MagnitudeInputView, typename PhaseInputView, typename OutputView>
[[nodiscard]] bool prefer_intel_polar_to_rectangular(const MagnitudeInputView& magnitude_input,
                                                     const PhaseInputView& phase_input,
                                                     const OutputView& output) noexcept {
  using output_type = typename OutputView::value_type;
  return is_complex_v<output_type> && all_views_are_contiguous(magnitude_input, phase_input, output) &&
         output.size() >= intel_complex_polar_min_elements<output_type>();
}

template <typename MagnitudeInputView, typename PhaseInputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_polar_to_rectangular(const MagnitudeInputView& magnitude_input,
                                                     const PhaseInputView& phase_input,
                                                     const OutputView& output) noexcept {
  using output_type = typename OutputView::value_type;
  return is_complex_v<output_type> && all_views_are_contiguous(magnitude_input, phase_input, output) &&
         output.size() >= eigen_complex_polar_min_elements<output_type>();
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_intel_multiply_conjugate(const LhsView& lhs, const RhsView& rhs,
                                                   const OutputView& output) noexcept {
  using input_type = typename LhsView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(lhs, rhs, output) &&
         lhs.size() >= intel_complex_multiply_conjugate_min_elements<input_type>();
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool prefer_eigen_multiply_conjugate(const LhsView& lhs, const RhsView& rhs,
                                                   const OutputView& output) noexcept {
  using input_type = typename LhsView::value_type;
  return is_complex_v<input_type> && all_views_are_contiguous(lhs, rhs, output) &&
         lhs.size() >= eigen_complex_multiply_conjugate_min_elements<input_type>() &&
         eigen_complex_multiply_conjugate_size_enabled<input_type>(lhs.size());
}

template <typename Array4DViewT, typename CubeViewT, typename OutputView>
[[nodiscard]] bool prefer_eigen_array4d_cube_multiply(const Array4DViewT& array4d, const CubeViewT& cube,
                                                      const OutputView& output) noexcept {
  return array4d.is_contiguous() && cube.is_contiguous() && output.is_contiguous() &&
         array4d.dim0() * cube.size() >= ArrayDispatchPolicy::eigen_array4d_cube_multiply_min_elements;
}

template <typename LeftView, typename RightView, typename OutputView>
[[nodiscard]] bool prefer_eigen_reduce_conjugate_product(const LeftView& left, const RightView& right,
                                                         const OutputView& output, const Dim dim) noexcept {
  return dim == Dim::dim0 && left.is_contiguous() && right.is_contiguous() && output.is_contiguous() &&
         left.dim0() * output.size() >= ArrayDispatchPolicy::eigen_reduce_conjugate_product_min_elements;
}

template <typename CubeViewT, typename Array4DViewT, typename OutputView>
[[nodiscard]] bool prefer_eigen_cube_abs_sum_squared(const CubeViewT& cube, const Array4DViewT& array4d,
                                                     const OutputView& output, const Dim dim) noexcept {
  return dim == Dim::dim0 && cube.is_contiguous() && array4d.is_contiguous() && output.is_contiguous() &&
         array4d.dim0() * cube.size() >= ArrayDispatchPolicy::eigen_cube_abs_sum_squared_min_elements;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool prefer_eigen_sliding_patch_matrix(const InputView& input, const OutputView& output) noexcept {
  return input.is_contiguous() && output.is_contiguous() &&
         output.size() >= ArrayDispatchPolicy::eigen_sliding_patch_matrix_min_elements;
}

} // namespace ksj::array::detail
