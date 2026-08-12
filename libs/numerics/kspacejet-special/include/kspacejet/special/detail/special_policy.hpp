#pragma once

#include <cstddef>
#include <complex>
#include <limits>
#include <type_traits>

namespace ksj::special::detail {

struct SpecialDispatchPolicy {
  static constexpr std::size_t disabled_backend_min_elements = std::numeric_limits<std::size_t>::max();

  static constexpr std::size_t intel_gamma_float_min_elements = 1024U;
  static constexpr std::size_t intel_gamma_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_log_gamma_float_min_elements = 256U;
  static constexpr std::size_t intel_log_gamma_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_bessel_i0_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_bessel_i0_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_bessel_j0_float_min_elements = 64U;
  static constexpr std::size_t intel_bessel_j0_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_bessel_j1_float_min_elements = 256U;
  static constexpr std::size_t intel_bessel_j1_double_min_elements = 256U;
  static constexpr std::size_t intel_sin_float_min_elements = 128U;
  static constexpr std::size_t intel_sin_double_min_elements = 128U;
  static constexpr std::size_t intel_exp_float_min_elements = 128U;
  static constexpr std::size_t intel_exp_double_min_elements = 128U;
  static constexpr std::size_t intel_exp_complex_float_min_elements = 64U;
  static constexpr std::size_t intel_exp_complex_double_min_elements = 64U;
  static constexpr std::size_t intel_vml_real_float_min_elements = 128U;
  static constexpr std::size_t intel_vml_real_double_min_elements = 128U;
  static constexpr std::size_t intel_vml_complex_float_min_elements = 64U;
  static constexpr std::size_t intel_vml_complex_double_min_elements = 64U;
  // Tuned by docs/benchmark_reports/2026-07-23/kspacejet-special/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  static constexpr std::size_t intel_vml_complex_sin_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vml_complex_sin_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vml_complex_cos_float_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vml_complex_cos_double_min_elements = disabled_backend_min_elements;
  static constexpr std::size_t intel_vml_complex_sqrt_float_min_elements = 64U;
  static constexpr std::size_t intel_vml_complex_sqrt_double_min_elements = disabled_backend_min_elements;

  static constexpr std::size_t eigen_gamma_min_elements = 1U;
  static constexpr std::size_t eigen_log_gamma_min_elements = 1U;
  static constexpr std::size_t eigen_bessel_i0_min_elements = 1U;
  static constexpr std::size_t eigen_bessel_j0_min_elements = 1U;
  static constexpr std::size_t eigen_bessel_j1_min_elements = 1U;
  static constexpr std::size_t eigen_bessel_j_min_elements = 1U;
  static constexpr std::size_t eigen_sin_min_elements = 1U;
  static constexpr std::size_t eigen_exp_min_elements = 1U;
  static constexpr std::size_t eigen_elementwise_min_elements = 1U;
};

template <typename T> constexpr bool supported_real_special_scalar_v = std::is_floating_point_v<T>;

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_gamma(const std::size_t elements) noexcept {
  return supported_real_special_scalar_v<std::remove_cv_t<T>> &&
         elements >= SpecialDispatchPolicy::eigen_gamma_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_gamma(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return elements >= SpecialDispatchPolicy::intel_gamma_float_min_elements;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return elements >= SpecialDispatchPolicy::intel_gamma_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_log_gamma(const std::size_t elements) noexcept {
  return supported_real_special_scalar_v<std::remove_cv_t<T>> &&
         elements >= SpecialDispatchPolicy::eigen_log_gamma_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_log_gamma(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return elements >= SpecialDispatchPolicy::intel_log_gamma_float_min_elements;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return elements >= SpecialDispatchPolicy::intel_log_gamma_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_bessel_i0(const std::size_t elements) noexcept {
  return supported_real_special_scalar_v<std::remove_cv_t<T>> &&
         elements >= SpecialDispatchPolicy::eigen_bessel_i0_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_bessel_i0(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return elements >= SpecialDispatchPolicy::intel_bessel_i0_float_min_elements;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return elements >= SpecialDispatchPolicy::intel_bessel_i0_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_bessel_j0(const std::size_t elements) noexcept {
  return supported_real_special_scalar_v<std::remove_cv_t<T>> &&
         elements >= SpecialDispatchPolicy::eigen_bessel_j0_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_bessel_j0(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return elements >= SpecialDispatchPolicy::intel_bessel_j0_float_min_elements;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return elements >= SpecialDispatchPolicy::intel_bessel_j0_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_bessel_j1(const std::size_t elements) noexcept {
  return supported_real_special_scalar_v<std::remove_cv_t<T>> &&
         elements >= SpecialDispatchPolicy::eigen_bessel_j1_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_bessel_j1(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return elements >= SpecialDispatchPolicy::intel_bessel_j1_float_min_elements;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return elements >= SpecialDispatchPolicy::intel_bessel_j1_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_bessel_j(const std::size_t elements) noexcept {
  return elements >= SpecialDispatchPolicy::eigen_bessel_j_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_sin(const std::size_t elements) noexcept {
  return supported_real_special_scalar_v<std::remove_cv_t<T>> &&
         elements >= SpecialDispatchPolicy::eigen_sin_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sin(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<std::remove_cv_t<T>, float>) {
    return elements >= SpecialDispatchPolicy::intel_sin_float_min_elements;
  } else if constexpr (std::is_same_v<std::remove_cv_t<T>, double>) {
    return elements >= SpecialDispatchPolicy::intel_sin_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_exp(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  return (std::is_same_v<value_type, float> || std::is_same_v<value_type, double> ||
          std::is_same_v<value_type, std::complex<float>> || std::is_same_v<value_type, std::complex<double>>) &&
         elements >= SpecialDispatchPolicy::eigen_exp_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_exp(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return elements >= SpecialDispatchPolicy::intel_exp_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return elements >= SpecialDispatchPolicy::intel_exp_double_min_elements;
  } else if constexpr (std::is_same_v<value_type, std::complex<float>>) {
    return elements >= SpecialDispatchPolicy::intel_exp_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, std::complex<double>>) {
    return elements >= SpecialDispatchPolicy::intel_exp_complex_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_eigen_elementwise(const std::size_t elements) noexcept {
  return elements >= SpecialDispatchPolicy::eigen_elementwise_min_elements;
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_vml_real(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, float>) {
    return elements >= SpecialDispatchPolicy::intel_vml_real_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, double>) {
    return elements >= SpecialDispatchPolicy::intel_vml_real_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_vml_complex(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, std::complex<float>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, std::complex<double>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_vml_complex_sin(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, std::complex<float>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_sin_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, std::complex<double>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_sin_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_vml_complex_cos(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, std::complex<float>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_cos_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, std::complex<double>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_cos_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_vml_complex_sqrt(const std::size_t elements) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_same_v<value_type, std::complex<float>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_sqrt_float_min_elements;
  } else if constexpr (std::is_same_v<value_type, std::complex<double>>) {
    return elements >= SpecialDispatchPolicy::intel_vml_complex_sqrt_double_min_elements;
  } else {
    return false;
  }
}

} // namespace ksj::special::detail
