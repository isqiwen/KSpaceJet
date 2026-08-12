#include "benchmark_common.hpp"
#include "kspacejet/special/detail/eigen/eigen_special_functions.hpp"
#include "kspacejet/special/detail/intel/intel_special_functions.hpp"
#include "kspacejet/special/detail/special_policy.hpp"
#include "kspacejet/special/special.hpp"

#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

template <typename T> void fill_positive_input(ksj::array::PooledVector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(0.25 + static_cast<double>((i % 64U) + 1U) / 16.0);
  }
}

template <typename T> void fill_angle_input(ksj::array::PooledVector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(-0.75 + static_cast<double>(i % 97U) / 64.0);
  }
}

template <typename T> void fill_unit_input(ksj::array::PooledVector<T>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    vector(i) = static_cast<T>(-0.95 + 1.90 * static_cast<double>(i % 127U) / 126.0);
  }
}

template <typename T> void fill_complex_input(ksj::array::PooledVector<std::complex<T>>& vector) {
  for (std::size_t i = 0; i < vector.size(); ++i) {
    const auto real = static_cast<T>(0.25 + static_cast<double>((i % 31U) + 1U) / 48.0);
    const auto imag = static_cast<T>(-0.20 + static_cast<double>(i % 29U) / 72.0);
    vector(i) = {real, imag};
  }
}

template <typename T>
[[nodiscard]] std::string_view selected_policy_backend(const std::string_view case_name, const std::size_t size) {
  using value_type = std::remove_cv_t<T>;
  const auto prefer_intel = [&] {
    if (case_name == "gamma") {
      return ksj::special::detail::prefer_intel_gamma<value_type>(size);
    }
    if (case_name == "log_gamma") {
      return ksj::special::detail::prefer_intel_log_gamma<value_type>(size);
    }
    if (case_name == "bessel_i0") {
      return ksj::special::detail::prefer_intel_bessel_i0<value_type>(size);
    }
    if (case_name == "bessel_j0") {
      return ksj::special::detail::prefer_intel_bessel_j0<value_type>(size);
    }
    if (case_name == "bessel_j1") {
      return ksj::special::detail::prefer_intel_bessel_j1<value_type>(size);
    }
    if (case_name == "sin") {
      if constexpr (std::is_same_v<value_type, std::complex<float>> ||
                    std::is_same_v<value_type, std::complex<double>>) {
        return ksj::special::detail::prefer_intel_vml_complex_sin<value_type>(size);
      } else {
        return ksj::special::detail::prefer_intel_sin<value_type>(size);
      }
    }
    if (case_name == "cos") {
      if constexpr (std::is_same_v<value_type, std::complex<float>> ||
                    std::is_same_v<value_type, std::complex<double>>) {
        return ksj::special::detail::prefer_intel_vml_complex_cos<value_type>(size);
      }
    }
    if (case_name == "sqrt") {
      if constexpr (std::is_same_v<value_type, std::complex<float>> ||
                    std::is_same_v<value_type, std::complex<double>>) {
        return ksj::special::detail::prefer_intel_vml_complex_sqrt<value_type>(size);
      }
    }
    if (case_name == "exp") {
      return ksj::special::detail::prefer_intel_exp<value_type>(size);
    }
    if constexpr (std::is_same_v<value_type, std::complex<float>> || std::is_same_v<value_type, std::complex<double>>) {
      return ksj::special::detail::prefer_intel_vml_complex<value_type>(size);
    } else {
      return ksj::special::detail::prefer_intel_vml_real<value_type>(size);
    }
  }();
  return prefer_intel ? "intel_mkl_vml" : "eigen";
}

template <typename OutputT, typename Function>
void run_returning_vector_case(std::string_view case_name, std::string_view backend_name, std::string_view type_name,
                               std::size_t size, const ksj::benchmarks::Config& config, Function&& function,
                               const std::string_view selected_backend = {}) {
  auto output = function();
  if (!output.empty()) {
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  }
  const auto ns = ksj::benchmarks::measure(config, [&] {
    output = function();
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  });
  const auto metadata =
    backend_name == "eigen"
      ? ksj::benchmarks::reference_row(case_name, "allocating")
      : (backend_name == "public_policy" ? ksj::benchmarks::policy_row(case_name, "allocating", selected_backend)
                                         : ksj::benchmarks::candidate_row(case_name, "allocating"));
  ksj::benchmarks::print_row(case_name, backend_name, type_name, size, config, ns, ksj::benchmarks::checksum(output),
                             metadata);
}

template <typename InputT, typename OutputT, typename Function>
void run_intel_unary_case(std::string_view case_name, std::string_view type_name, std::size_t size,
                          const ksj::benchmarks::Config& config, ksj::array::VectorView<const InputT> input,
                          Function&& function) {
  run_returning_vector_case<OutputT>(case_name, "intel_mkl_vml", type_name, size, config, [&] {
    auto output = ksj::array::make_pooled_vector<OutputT>(input.size());
    ksj::benchmarks::require_pooled_storage("intel_output", output);
    if (!function(input, output.view())) {
      throw std::runtime_error("Intel VML backend rejected special benchmark input");
    }
    return output;
  });
}

template <typename T, typename EigenFunction, typename IntelFunction, typename PublicFunction>
void run_unary_same_case(std::string_view case_name, std::string_view type_name, std::size_t size,
                         const ksj::benchmarks::Config& config, ksj::array::VectorView<const T> input,
                         EigenFunction&& eigen_function, IntelFunction&& intel_function,
                         PublicFunction&& public_function) {
  run_returning_vector_case<T>(case_name, "eigen", type_name, size, config, [&] {
    return eigen_function(input);
  });
  run_intel_unary_case<T, T>(case_name, type_name, size, config, input, std::forward<IntelFunction>(intel_function));
  run_returning_vector_case<T>(
    case_name, "public_policy", type_name, size, config,
    [&] {
      return public_function(input);
    },
    selected_policy_backend<T>(case_name, size));
}

template <typename InputT, typename OutputT, typename EigenFunction, typename IntelFunction, typename PublicFunction>
void run_unary_output_case(std::string_view case_name, std::string_view type_name, std::size_t size,
                           const ksj::benchmarks::Config& config, ksj::array::VectorView<const InputT> input,
                           EigenFunction&& eigen_function, IntelFunction&& intel_function,
                           PublicFunction&& public_function) {
  run_returning_vector_case<OutputT>(case_name, "eigen", type_name, size, config, [&] {
    return eigen_function(input);
  });
  run_intel_unary_case<InputT, OutputT>(case_name, type_name, size, config, input,
                                        std::forward<IntelFunction>(intel_function));
  run_returning_vector_case<OutputT>(
    case_name, "public_policy", type_name, size, config,
    [&] {
      return public_function(input);
    },
    selected_policy_backend<InputT>(case_name, size));
}

template <typename T, typename EigenFunction, typename IntelFunction, typename PublicFunction>
void run_binary_same_case(std::string_view case_name, std::string_view type_name, std::size_t size,
                          const ksj::benchmarks::Config& config, ksj::array::VectorView<const T> lhs,
                          ksj::array::VectorView<const T> rhs, EigenFunction&& eigen_function,
                          IntelFunction&& intel_function, PublicFunction&& public_function) {
  run_returning_vector_case<T>(case_name, "eigen", type_name, size, config, [&] {
    return eigen_function(lhs, rhs);
  });
  run_returning_vector_case<T>(case_name, "intel_mkl_vml", type_name, size, config, [&] {
    auto output = ksj::array::make_pooled_vector<T>(lhs.size());
    ksj::benchmarks::require_pooled_storage("intel_output", output);
    if (!intel_function(lhs, rhs, output.view())) {
      throw std::runtime_error("Intel VML backend rejected special benchmark input");
    }
    return output;
  });
  run_returning_vector_case<T>(
    case_name, "public_policy", type_name, size, config,
    [&] {
      return public_function(lhs, rhs);
    },
    selected_policy_backend<T>(case_name, size));
}

template <typename T, typename EigenFunction, typename IntelFunction, typename PublicFunction>
void run_unary_scalar_same_case(std::string_view case_name, std::string_view type_name, std::size_t size,
                                const ksj::benchmarks::Config& config, ksj::array::VectorView<const T> input,
                                const T scalar, EigenFunction&& eigen_function, IntelFunction&& intel_function,
                                PublicFunction&& public_function) {
  run_returning_vector_case<T>(case_name, "eigen", type_name, size, config, [&] {
    return eigen_function(input, scalar);
  });
  run_returning_vector_case<T>(case_name, "intel_mkl_vml", type_name, size, config, [&] {
    auto output = ksj::array::make_pooled_vector<T>(input.size());
    ksj::benchmarks::require_pooled_storage("intel_output", output);
    if (!intel_function(input, scalar, output.view())) {
      throw std::runtime_error("Intel VML backend rejected special benchmark input");
    }
    return output;
  });
  run_returning_vector_case<T>(
    case_name, "public_policy", type_name, size, config,
    [&] {
      return public_function(input, scalar);
    },
    selected_policy_backend<T>(case_name, size));
}

template <typename T>
void run_real_unary_cases(std::string_view type_name, std::size_t size, const ksj::benchmarks::Config& config,
                          ksj::array::VectorView<const T> positive_input, ksj::array::VectorView<const T> angle_input,
                          ksj::array::VectorView<const T> unit_input) {
  run_unary_same_case<T>(
    "gamma", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::gamma(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::gamma(source, output);
    },
    [](auto source) {
      return ksj::special::gamma(source);
    });
  run_unary_same_case<T>(
    "log_gamma", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::log_gamma(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::log_gamma(source, output);
    },
    [](auto source) {
      return ksj::special::log_gamma(source);
    });
  run_unary_same_case<T>(
    "bessel_i0", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::bessel_i0(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::bessel_i0(source, output);
    },
    [](auto source) {
      return ksj::special::bessel_i0(source);
    });
  run_unary_same_case<T>(
    "bessel_j0", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::bessel_j0(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::bessel_j0(source, output);
    },
    [](auto source) {
      return ksj::special::bessel_j0(source);
    });
  run_unary_same_case<T>(
    "bessel_j1", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::bessel_j1(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::bessel_j1(source, output);
    },
    [](auto source) {
      return ksj::special::bessel_j1(source);
    });

  run_unary_same_case<T>(
    "sin", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::sin(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::sin(source, output);
    },
    [](auto source) {
      return ksj::special::sin(source);
    });
  run_unary_same_case<T>(
    "cos", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::cos(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::cos(source, output);
    },
    [](auto source) {
      return ksj::special::cos(source);
    });
  run_unary_same_case<T>(
    "tan", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::tan(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::tan(source, output);
    },
    [](auto source) {
      return ksj::special::tan(source);
    });
  run_unary_same_case<T>(
    "asin", type_name, size, config, unit_input,
    [](auto source) {
      return ksj::special::detail::eigen::asin(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::asin(source, output);
    },
    [](auto source) {
      return ksj::special::asin(source);
    });
  run_unary_same_case<T>(
    "acos", type_name, size, config, unit_input,
    [](auto source) {
      return ksj::special::detail::eigen::acos(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::acos(source, output);
    },
    [](auto source) {
      return ksj::special::acos(source);
    });
  run_unary_same_case<T>(
    "atan", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::atan(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::atan(source, output);
    },
    [](auto source) {
      return ksj::special::atan(source);
    });

  run_unary_same_case<T>(
    "ln", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::ln(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::ln(source, output);
    },
    [](auto source) {
      return ksj::special::ln(source);
    });
  run_unary_same_case<T>(
    "log10", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::log10(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::log10(source, output);
    },
    [](auto source) {
      return ksj::special::log10(source);
    });
  run_unary_same_case<T>(
    "log2", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::log2(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::log2(source, output);
    },
    [](auto source) {
      return ksj::special::log2(source);
    });
  run_unary_same_case<T>(
    "sqrt", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::sqrt(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::sqrt(source, output);
    },
    [](auto source) {
      return ksj::special::sqrt(source);
    });
  run_unary_same_case<T>(
    "cbrt", type_name, size, config, positive_input,
    [](auto source) {
      return ksj::special::detail::eigen::cbrt(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::cbrt(source, output);
    },
    [](auto source) {
      return ksj::special::cbrt(source);
    });

  run_unary_same_case<T>(
    "erf", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::erf(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::erf(source, output);
    },
    [](auto source) {
      return ksj::special::erf(source);
    });
  run_unary_same_case<T>(
    "cdf_norm", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::cdf_norm(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::cdf_norm(source, output);
    },
    [](auto source) {
      return ksj::special::cdf_norm(source);
    });
  run_unary_same_case<T>(
    "exp", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::exp(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::exp(source, output);
    },
    [](auto source) {
      return ksj::special::exp(source);
    });
  run_unary_same_case<T>(
    "exp2", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::exp2(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::exp2(source, output);
    },
    [](auto source) {
      return ksj::special::exp2(source);
    });
  run_unary_same_case<T>(
    "expm1", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::expm1(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::expm1(source, output);
    },
    [](auto source) {
      return ksj::special::expm1(source);
    });
  run_unary_same_case<T>(
    "log1p", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::log1p(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::log1p(source, output);
    },
    [](auto source) {
      return ksj::special::log1p(source);
    });
  run_unary_same_case<T>(
    "erfc", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::erfc(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::erfc(source, output);
    },
    [](auto source) {
      return ksj::special::erfc(source);
    });
  run_unary_same_case<T>(
    "sinpi", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::sinpi(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::sinpi(source, output);
    },
    [](auto source) {
      return ksj::special::sinpi(source);
    });
  run_unary_same_case<T>(
    "cospi", type_name, size, config, angle_input,
    [](auto source) {
      return ksj::special::detail::eigen::cospi(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::cospi(source, output);
    },
    [](auto source) {
      return ksj::special::cospi(source);
    });
}

template <typename T>
void run_real_binary_cases(std::string_view type_name, std::size_t size, const ksj::benchmarks::Config& config,
                           ksj::array::VectorView<const T> positive_input,
                           ksj::array::VectorView<const T> angle_input) {
  run_binary_same_case<T>(
    "atan2", type_name, size, config, angle_input, positive_input,
    [](auto y, auto x) {
      return ksj::special::detail::eigen::atan2(y, x);
    },
    [](auto y, auto x, auto output) {
      return ksj::special::detail::intel::atan2(y, x, output);
    },
    [](auto y, auto x) {
      return ksj::special::atan2(y, x);
    });
  run_binary_same_case<T>(
    "pow_vector", type_name, size, config, positive_input, angle_input,
    [](auto base, auto exponent) {
      return ksj::special::detail::eigen::pow(base, exponent);
    },
    [](auto base, auto exponent, auto output) {
      return ksj::special::detail::intel::pow(base, exponent, output);
    },
    [](auto base, auto exponent) {
      return ksj::special::pow(base, exponent);
    });
  run_unary_scalar_same_case<T>(
    "pow_scalar", type_name, size, config, positive_input, static_cast<T>(1.5),
    [](auto base, auto exponent) {
      return ksj::special::detail::eigen::pow(base, exponent);
    },
    [](auto base, auto exponent, auto output) {
      return ksj::special::detail::intel::pow(base, exponent, output);
    },
    [](auto base, auto exponent) {
      return ksj::special::pow(base, exponent);
    });
}

template <typename ComplexT, typename RealT>
void run_complex_cases(std::string_view type_name, std::size_t size, const ksj::benchmarks::Config& config,
                       ksj::array::VectorView<const ComplexT> input) {
  run_unary_same_case<ComplexT>(
    "sin", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::sin(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::sin(source, output);
    },
    [](auto source) {
      return ksj::special::sin(source);
    });
  run_unary_same_case<ComplexT>(
    "cos", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::cos(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::cos(source, output);
    },
    [](auto source) {
      return ksj::special::cos(source);
    });
  run_unary_same_case<ComplexT>(
    "ln", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::ln(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::ln(source, output);
    },
    [](auto source) {
      return ksj::special::ln(source);
    });
  run_unary_same_case<ComplexT>(
    "sqrt", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::sqrt(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::sqrt(source, output);
    },
    [](auto source) {
      return ksj::special::sqrt(source);
    });
  run_unary_same_case<ComplexT>(
    "exp", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::exp(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::exp(source, output);
    },
    [](auto source) {
      return ksj::special::exp(source);
    });
  run_unary_same_case<ComplexT>(
    "conj", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::conj(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::conj(source, output);
    },
    [](auto source) {
      return ksj::special::conj(source);
    });
  run_unary_output_case<ComplexT, RealT>(
    "abs", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::abs(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::abs(source, output);
    },
    [](auto source) {
      return ksj::special::abs(source);
    });
  run_unary_output_case<ComplexT, RealT>(
    "arg", type_name, size, config, input,
    [](auto source) {
      return ksj::special::detail::eigen::arg(source);
    },
    [](auto source, auto output) {
      return ksj::special::detail::intel::arg(source, output);
    },
    [](auto source) {
      return ksj::special::arg(source);
    });
}

template <typename T> void run_for_real_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto positive_input = ksj::array::make_pooled_vector<T>(size);
    auto angle_input = ksj::array::make_pooled_vector<T>(size);
    auto unit_input = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("positive_input", positive_input);
    ksj::benchmarks::require_pooled_storage("angle_input", angle_input);
    ksj::benchmarks::require_pooled_storage("unit_input", unit_input);
    fill_positive_input(positive_input);
    fill_angle_input(angle_input);
    fill_unit_input(unit_input);

    const auto positive_view = ksj::array::as_const_view(positive_input.view());
    const auto angle_view = ksj::array::as_const_view(angle_input.view());
    const auto unit_view = ksj::array::as_const_view(unit_input.view());
    run_real_unary_cases<T>(type_name, size, config, positive_view, angle_view, unit_view);
    run_real_binary_cases<T>(type_name, size, config, positive_view, angle_view);
  }
}

template <typename RealT, typename ComplexT>
void run_for_complex_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_vector<ComplexT>(size);
    ksj::benchmarks::require_pooled_storage("complex_input", input);
    fill_complex_input(input);
    run_complex_cases<ComplexT, RealT>(type_name, size, config, ksj::array::as_const_view(input.view()));
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_special_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_real_type<float>("float", config);
  run_for_real_type<double>("double", config);
  run_for_complex_type<float, ksj::base::cf32>("complex_float", config);
  run_for_complex_type<double, ksj::base::cf64>("complex_double", config);
  return 0;
}
