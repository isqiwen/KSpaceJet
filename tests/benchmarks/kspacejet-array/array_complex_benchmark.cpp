#include "array_benchmark_common.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_elementwise.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_complex.hpp"
#include "kspacejet/array/detail/intel/intel_array_elementwise.hpp"
#include "kspacejet/array/detail/intel/intel_array_complex.hpp"
#include "kspacejet/array/detail/intel/intel_array_vml.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ksj::benchmarks::array_benchmarks {
namespace {

template <typename T>
[[nodiscard]] std::complex<T> manual_soft_threshold(const std::complex<T>& value, const T lambda, const T epsilon) {
  const auto magnitude = std::abs(value);
  if (magnitude < epsilon || magnitude == T{}) {
    return {};
  }
  return value / magnitude * std::max(magnitude - lambda, T{});
}

template <typename Fn> void require_backend(Fn&& fn, const char* message) {
  if (!fn()) {
    throw std::runtime_error(message);
  }
}

template <typename T>
[[nodiscard]] ksj::benchmarks::RowMetadata row_metadata(const std::string_view operation,
                                                        const std::string_view backend,
                                                        const std::string_view timing_scope, const std::size_t size) {
  const auto selected_backend =
    backend == "public_policy" ? selected_complex_backend<T>(operation, size) : std::string_view{};
  const auto effective_backend = selected_backend.empty() ? backend : selected_backend;
  const auto relative_tolerance = benchmark_relative_tolerance<T>(operation, size, effective_backend);
  if (backend.starts_with("manual_")) {
    return ksj::benchmarks::reference_row(operation, timing_scope, -1.0, relative_tolerance);
  }
  if (backend == "public_policy") {
    return ksj::benchmarks::policy_row(operation, timing_scope, selected_backend, -1.0, relative_tolerance);
  }
  return ksj::benchmarks::candidate_row(operation, timing_scope, -1.0, relative_tolerance);
}

[[nodiscard]] ksj::benchmarks::RowMetadata array_complex_row_metadata(const std::string_view operation,
                                                                      const std::string_view backend) {
  const auto output_reuse = backend.starts_with("pooled_") || backend.starts_with("manual_") ||
                            backend.find("output") != std::string_view::npos;
  const auto timing_scope = output_reuse ? std::string_view{"output_reuse"} : std::string_view{"allocating"};
  if (backend.starts_with("manual_")) {
    return ksj::benchmarks::reference_row(operation, timing_scope);
  }
  return ksj::benchmarks::candidate_row(operation, timing_scope);
}

inline void print_array_complex_benchmark_row(const std::string_view operation, const std::string_view backend,
                                              const std::string_view type_name, const std::size_t size,
                                              const ksj::benchmarks::Config& config,
                                              const ksj::benchmarks::Measurement& measurement, const double checksum) {
  ksj::benchmarks::print_row(operation, backend, type_name, size, config, measurement, checksum,
                             array_complex_row_metadata(operation, backend));
}

inline void print_array_complex_benchmark_row(const std::string_view operation, const std::string_view backend,
                                              const std::string_view type_name, const std::size_t size,
                                              const ksj::benchmarks::Config& config,
                                              const ksj::benchmarks::Measurement& measurement, const double checksum,
                                              const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(operation, backend, type_name, size, config, measurement, checksum, metadata);
}

inline void print_array_complex_benchmark_row(const std::string_view operation, const std::string_view backend,
                                              const std::string_view type_name, const std::size_t size,
                                              const std::size_t iterations,
                                              const ksj::benchmarks::Measurement& measurement, const double checksum,
                                              const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(operation, backend, type_name, size, iterations, measurement, checksum, metadata);
}

template <typename T, typename Fn>
void measure_complex_vector_output(std::string_view operation, std::string_view backend, std::string_view type_name,
                                   std::size_t size, const ksj::benchmarks::Config& config,
                                   ksj::array::PooledVector<std::complex<T>>& output, Fn&& fn) {
  fn();
  auto sentinel = output.data()[0].real() + output.data()[0].imag();
  ksj::benchmarks::do_not_optimize(sentinel);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    fn();
    sentinel = output.data()[0].real() + output.data()[0].imag();
    ksj::benchmarks::do_not_optimize(sentinel);
  });
  print_array_complex_benchmark_row(operation, backend, type_name, size, config, ns, ksj::benchmarks::checksum(output),
                                    row_metadata<T>(operation, backend, "output_reuse", size));
}

template <typename T, typename Fn>
void measure_real_vector_output(std::string_view operation, std::string_view backend, std::string_view type_name,
                                std::size_t size, const ksj::benchmarks::Config& config,
                                ksj::array::PooledVector<T>& output, Fn&& fn) {
  fn();
  auto sentinel = output.data()[0];
  ksj::benchmarks::do_not_optimize(sentinel);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    fn();
    sentinel = output.data()[0];
    ksj::benchmarks::do_not_optimize(sentinel);
  });
  print_array_complex_benchmark_row(operation, backend, type_name, size, config, ns, ksj::benchmarks::checksum(output),
                                    row_metadata<T>(operation, backend, "output_reuse", size));
}

template <typename T, typename Fn>
void measure_real_pair_vector_output(std::string_view operation, std::string_view backend, std::string_view type_name,
                                     std::size_t size, const ksj::benchmarks::Config& config,
                                     ksj::array::PooledVector<T>& first_output,
                                     ksj::array::PooledVector<T>& second_output, Fn&& fn) {
  fn();
  auto sentinel = first_output.data()[0] + second_output.data()[0];
  ksj::benchmarks::do_not_optimize(sentinel);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    fn();
    sentinel = first_output.data()[0] + second_output.data()[0];
    ksj::benchmarks::do_not_optimize(sentinel);
  });
  const auto checksum = ksj::benchmarks::checksum(first_output) + ksj::benchmarks::checksum(second_output);
  print_array_complex_benchmark_row(operation, backend, type_name, size, config, ns, checksum,
                                    row_metadata<T>(operation, backend, "output_reuse", size));
}

template <typename T, typename Fn>
void measure_complex_scalar(std::string_view operation, std::string_view backend, std::string_view type_name,
                            std::size_t size, const ksj::benchmarks::Config& config, Fn&& fn) {
  auto value = fn();
  ksj::benchmarks::do_not_optimize(value);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    value = fn();
    ksj::benchmarks::do_not_optimize(value);
  });
  print_array_complex_benchmark_row(operation, backend, type_name, size, config, ns, static_cast<double>(value),
                                    row_metadata<T>(operation, backend, "scalar_result", size));
}

template <typename T>
void print_squared_norm_oracle(const std::string_view type_name, const std::size_t size,
                               const ksj::array::PooledVector<std::complex<T>>& input) {
  long double sum = 0.0L;
  for (std::size_t index = 0U; index < input.size(); ++index) {
    const auto real = static_cast<long double>(input.data()[index].real());
    const auto imag = static_cast<long double>(input.data()[index].imag());
    sum += real * real + imag * imag;
  }
  print_array_complex_benchmark_row("complex_vector_squared_norm", "high_precision_oracle", type_name, size, 1U,
                                    ksj::benchmarks::Measurement{}, static_cast<double>(sum),
                                    ksj::benchmarks::oracle_row("complex_vector_squared_norm", "scalar_result"));
}

template <typename T>
void run_complex_elementwise_benchmarks(std::string_view type_name, const std::size_t element_count,
                                        const ksj::benchmarks::Config& config) {
  using complex_type = std::complex<T>;
  const auto vector_size = element_count;
  auto input = ksj::array::make_pooled_vector<complex_type>(vector_size);
  auto rhs = ksj::array::make_pooled_vector<complex_type>(vector_size);
  auto output = ksj::array::make_pooled_vector<complex_type>(vector_size);
  auto magnitude = ksj::array::make_pooled_vector<T>(vector_size);
  auto magnitude_squared = ksj::array::make_pooled_vector<T>(vector_size);
  auto real_output = ksj::array::make_pooled_vector<T>(vector_size);
  auto imag_output = ksj::array::make_pooled_vector<T>(vector_size);
  auto real_input = ksj::array::make_pooled_vector<T>(vector_size);
  auto imag_input = ksj::array::make_pooled_vector<T>(vector_size);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_input", input);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_rhs", rhs);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_output", output);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_magnitude", magnitude);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_magnitude_squared", magnitude_squared);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_real_output", real_output);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_imag_output", imag_output);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_real_input", real_input);
  ksj::benchmarks::require_pooled_storage("complex_elementwise_imag_input", imag_input);
  ksj::benchmarks::fill_vector(input);
  ksj::benchmarks::fill_vector(rhs);
  ksj::benchmarks::fill_vector(real_input);
  ksj::benchmarks::fill_vector(imag_input);

  measure_real_vector_output("complex_vector_abs", "manual_loop", type_name, vector_size, config, magnitude, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      magnitude.data()[index] = std::abs(input.data()[index]);
    }
  });
  measure_real_vector_output("complex_vector_abs", "public_policy", type_name, vector_size, config, magnitude, [&] {
    ksj::array::absolute(input.view(), magnitude.view());
  });
  measure_real_vector_output("complex_vector_abs", "eigen_detail", type_name, vector_size, config, magnitude, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::absolute(ksj::array::as_const_view(input.view()), magnitude.view());
      },
      "eigen complex abs backend rejected benchmark view");
  });
  measure_real_vector_output("complex_vector_abs", "intel_detail", type_name, vector_size, config, magnitude, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::absolute(ksj::array::as_const_view(input.view()), magnitude.view());
      },
      "intel complex abs backend rejected benchmark view");
  });
  measure_real_vector_output("complex_vector_abs", "mkl_vml_detail", type_name, vector_size, config, magnitude, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::vml::absolute(ksj::array::as_const_view(input.view()), magnitude.view());
      },
      "MKL VML complex abs backend rejected benchmark view");
  });

  measure_real_vector_output("complex_vector_real", "manual_loop", type_name, vector_size, config, real_output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      real_output.data()[index] = input.data()[index].real();
    }
  });
  measure_real_vector_output("complex_vector_real", "public_policy", type_name, vector_size, config, real_output, [&] {
    ksj::array::real(input.view(), real_output.view());
  });
  measure_real_vector_output("complex_vector_real", "eigen_detail", type_name, vector_size, config, real_output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::complex_real(ksj::array::as_const_view(input.view()), real_output.view());
      },
      "eigen complex real backend rejected benchmark view");
  });
  measure_real_vector_output("complex_vector_real", "intel_detail", type_name, vector_size, config, real_output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::complex_real(ksj::array::as_const_view(input.view()), real_output.view());
      },
      "intel complex real backend rejected benchmark view");
  });

  measure_real_vector_output("complex_vector_imag", "manual_loop", type_name, vector_size, config, imag_output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      imag_output.data()[index] = input.data()[index].imag();
    }
  });
  measure_real_vector_output("complex_vector_imag", "public_policy", type_name, vector_size, config, imag_output, [&] {
    ksj::array::imag(input.view(), imag_output.view());
  });
  measure_real_vector_output("complex_vector_imag", "eigen_detail", type_name, vector_size, config, imag_output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::complex_imag(ksj::array::as_const_view(input.view()), imag_output.view());
      },
      "eigen complex imag backend rejected benchmark view");
  });
  measure_real_vector_output("complex_vector_imag", "intel_detail", type_name, vector_size, config, imag_output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::complex_imag(ksj::array::as_const_view(input.view()), imag_output.view());
      },
      "intel complex imag backend rejected benchmark view");
  });

  measure_real_pair_vector_output("complex_vector_split", "manual_loop", type_name, vector_size, config, real_output,
                                  imag_output, [&] {
                                    for (std::size_t index = 0U; index < input.size(); ++index) {
                                      real_output.data()[index] = input.data()[index].real();
                                      imag_output.data()[index] = input.data()[index].imag();
                                    }
                                  });
  measure_real_pair_vector_output("complex_vector_split", "public_policy", type_name, vector_size, config, real_output,
                                  imag_output, [&] {
                                    ksj::array::split_complex(input.view(), real_output.view(), imag_output.view());
                                  });
  measure_real_pair_vector_output(
    "complex_vector_split", "eigen_detail", type_name, vector_size, config, real_output, imag_output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::split_complex(ksj::array::as_const_view(input.view()), real_output.view(),
                                                          imag_output.view());
        },
        "eigen split_complex backend rejected benchmark view");
    });
  measure_real_pair_vector_output(
    "complex_vector_split", "intel_detail", type_name, vector_size, config, real_output, imag_output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::split_complex(ksj::array::as_const_view(input.view()), real_output.view(),
                                                          imag_output.view());
        },
        "intel split_complex backend rejected benchmark view");
    });

  measure_complex_vector_output("complex_vector_from_real_imag", "manual_loop", type_name, vector_size, config, output,
                                [&] {
                                  for (std::size_t index = 0U; index < output.size(); ++index) {
                                    output.data()[index] = {real_input.data()[index], imag_input.data()[index]};
                                  }
                                });
  measure_complex_vector_output(
    "complex_vector_from_real_imag", "public_policy", type_name, vector_size, config, output, [&] {
      ksj::array::complex_from_real_imag(real_input.view(), imag_input.view(), output.view());
    });
  measure_complex_vector_output(
    "complex_vector_from_real_imag", "eigen_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::complex_from_real_imag(
            ksj::array::as_const_view(real_input.view()), ksj::array::as_const_view(imag_input.view()), output.view());
        },
        "eigen complex_from_real_imag backend rejected benchmark view");
    });
  measure_complex_vector_output(
    "complex_vector_from_real_imag", "intel_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::complex_from_real_imag(
            ksj::array::as_const_view(real_input.view()), ksj::array::as_const_view(imag_input.view()), output.view());
        },
        "intel complex_from_real_imag backend rejected benchmark view");
    });

  measure_complex_vector_output("complex_vector_conjugate", "manual_loop", type_name, vector_size, config, output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = std::conj(input.data()[index]);
    }
  });
  measure_complex_vector_output("complex_vector_conjugate", "public_policy", type_name, vector_size, config, output,
                                [&] {
                                  ksj::array::conjugate(input.view(), output.view());
                                });
  measure_complex_vector_output(
    "complex_vector_conjugate", "eigen_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::complex_conjugate(ksj::array::as_const_view(input.view()), output.view());
        },
        "eigen complex conjugate backend rejected benchmark view");
    });
  measure_complex_vector_output(
    "complex_vector_conjugate", "intel_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::complex_conjugate(ksj::array::as_const_view(input.view()), output.view());
        },
        "intel complex conjugate backend rejected benchmark view");
    });
  measure_complex_vector_output("complex_vector_conjugate", "mkl_vml_detail", type_name, vector_size, config, output,
                                [&] {
                                  require_backend(
                                    [&] {
                                      return ksj::array::detail::intel::vml::complex_conjugate(
                                        ksj::array::as_const_view(input.view()), output.view());
                                    },
                                    "MKL VML complex conjugate backend rejected benchmark view");
                                });

  measure_real_pair_vector_output("complex_vector_rectangular_to_polar_split", "manual_loop", type_name, vector_size,
                                  config, real_output, imag_output, [&] {
                                    for (std::size_t index = 0U; index < input.size(); ++index) {
                                      real_output.data()[index] = std::abs(input.data()[index]);
                                      imag_output.data()[index] = std::arg(input.data()[index]);
                                    }
                                  });
  measure_real_pair_vector_output("complex_vector_rectangular_to_polar_split", "public_policy", type_name, vector_size,
                                  config, real_output, imag_output, [&] {
                                    ksj::array::rectangular_to_polar(input.view(), real_output.view(),
                                                                     imag_output.view());
                                  });
  measure_real_pair_vector_output("complex_vector_rectangular_to_polar_split", "eigen_detail", type_name, vector_size,
                                  config, real_output, imag_output, [&] {
                                    require_backend(
                                      [&] {
                                        return ksj::array::detail::eigen::rectangular_to_polar(
                                          ksj::array::as_const_view(input.view()), real_output.view(),
                                          imag_output.view());
                                      },
                                      "eigen rectangular_to_polar split backend rejected benchmark view");
                                  });
  measure_real_pair_vector_output("complex_vector_rectangular_to_polar_split", "intel_detail", type_name, vector_size,
                                  config, real_output, imag_output, [&] {
                                    require_backend(
                                      [&] {
                                        return ksj::array::detail::intel::rectangular_to_polar(
                                          ksj::array::as_const_view(input.view()), real_output.view(),
                                          imag_output.view());
                                      },
                                      "intel rectangular_to_polar split backend rejected benchmark view");
                                  });

  measure_complex_vector_output(
    "complex_vector_polar_to_rectangular_split", "manual_loop", type_name, vector_size, config, output, [&] {
      for (std::size_t index = 0U; index < output.size(); ++index) {
        output.data()[index] = std::polar(real_input.data()[index], imag_input.data()[index]);
      }
    });
  measure_complex_vector_output("complex_vector_polar_to_rectangular_split", "public_policy", type_name, vector_size,
                                config, output, [&] {
                                  ksj::array::polar_to_rectangular(real_input.view(), imag_input.view(), output.view());
                                });
  measure_complex_vector_output(
    "complex_vector_polar_to_rectangular_split", "eigen_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::polar_to_rectangular(
            ksj::array::as_const_view(real_input.view()), ksj::array::as_const_view(imag_input.view()), output.view());
        },
        "eigen polar_to_rectangular split backend rejected benchmark view");
    });
  measure_complex_vector_output(
    "complex_vector_polar_to_rectangular_split", "intel_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::polar_to_rectangular(
            ksj::array::as_const_view(real_input.view()), ksj::array::as_const_view(imag_input.view()), output.view());
        },
        "intel polar_to_rectangular split backend rejected benchmark view");
    });

  measure_complex_vector_output("complex_vector_multiply_conjugate", "manual_loop", type_name, vector_size, config,
                                output, [&] {
                                  for (std::size_t index = 0U; index < input.size(); ++index) {
                                    output.data()[index] = input.data()[index] * std::conj(rhs.data()[index]);
                                  }
                                });
  measure_complex_vector_output("complex_vector_multiply_conjugate", "public_policy", type_name, vector_size, config,
                                output, [&] {
                                  ksj::array::multiply_conjugate(input.view(), rhs.view(), output.view());
                                });
  measure_complex_vector_output(
    "complex_vector_multiply_conjugate", "eigen_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::multiply_conjugate(ksj::array::as_const_view(input.view()),
                                                               ksj::array::as_const_view(rhs.view()), output.view());
        },
        "eigen multiply_conjugate backend rejected benchmark view");
    });
  measure_complex_vector_output(
    "complex_vector_multiply_conjugate", "intel_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::multiply_conjugate(ksj::array::as_const_view(input.view()),
                                                               ksj::array::as_const_view(rhs.view()), output.view());
        },
        "intel multiply_conjugate backend rejected benchmark view");
    });

  measure_complex_vector_output("complex_vector_add", "manual_loop", type_name, vector_size, config, output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = input.data()[index] + rhs.data()[index];
    }
  });
  measure_complex_vector_output("complex_vector_add", "public_policy", type_name, vector_size, config, output, [&] {
    ksj::array::add(input.view(), rhs.view(), output.view());
  });
  measure_complex_vector_output("complex_vector_add", "eigen_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::add(ksj::array::as_const_view(input.view()),
                                              ksj::array::as_const_view(rhs.view()), output.view());
      },
      "eigen complex vector add backend rejected benchmark view");
  });
  measure_complex_vector_output("complex_vector_add", "intel_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::add(ksj::array::as_const_view(input.view()),
                                              ksj::array::as_const_view(rhs.view()), output.view());
      },
      "intel complex vector add backend rejected benchmark view");
  });

  measure_complex_vector_output("complex_vector_subtract", "manual_loop", type_name, vector_size, config, output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = input.data()[index] - rhs.data()[index];
    }
  });
  measure_complex_vector_output("complex_vector_subtract", "public_policy", type_name, vector_size, config, output,
                                [&] {
                                  ksj::array::subtract(input.view(), rhs.view(), output.view());
                                });
  measure_complex_vector_output("complex_vector_subtract", "eigen_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::subtract(ksj::array::as_const_view(input.view()),
                                                   ksj::array::as_const_view(rhs.view()), output.view());
      },
      "eigen complex vector subtract backend rejected benchmark view");
  });
  measure_complex_vector_output("complex_vector_subtract", "intel_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::subtract(ksj::array::as_const_view(input.view()),
                                                   ksj::array::as_const_view(rhs.view()), output.view());
      },
      "intel complex vector subtract backend rejected benchmark view");
  });

  measure_complex_vector_output("complex_vector_multiply", "manual_loop", type_name, vector_size, config, output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = input.data()[index] * rhs.data()[index];
    }
  });
  measure_complex_vector_output("complex_vector_multiply", "public_policy", type_name, vector_size, config, output,
                                [&] {
                                  ksj::array::multiply(input.view(), rhs.view(), output.view());
                                });
  measure_complex_vector_output("complex_vector_multiply", "eigen_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::multiply(ksj::array::as_const_view(input.view()),
                                                   ksj::array::as_const_view(rhs.view()), output.view());
      },
      "eigen complex vector multiply backend rejected benchmark view");
  });
  measure_complex_vector_output("complex_vector_multiply", "intel_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::multiply(ksj::array::as_const_view(input.view()),
                                                   ksj::array::as_const_view(rhs.view()), output.view());
      },
      "intel complex vector multiply backend rejected benchmark view");
  });
  measure_complex_vector_output(
    "complex_vector_multiply", "mkl_vml_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::multiply(ksj::array::as_const_view(input.view()),
                                                          ksj::array::as_const_view(rhs.view()), output.view());
        },
        "MKL VML complex vector multiply backend rejected benchmark view");
    });

  measure_complex_vector_output("complex_vector_divide", "manual_loop", type_name, vector_size, config, output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = input.data()[index] / rhs.data()[index];
    }
  });
  measure_complex_vector_output("complex_vector_divide", "public_policy", type_name, vector_size, config, output, [&] {
    ksj::array::divide(input.view(), rhs.view(), output.view());
  });
  measure_complex_vector_output("complex_vector_divide", "eigen_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::divide(ksj::array::as_const_view(input.view()),
                                                 ksj::array::as_const_view(rhs.view()), output.view());
      },
      "eigen complex vector divide backend rejected benchmark view");
  });
  measure_complex_vector_output("complex_vector_divide", "intel_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::divide(ksj::array::as_const_view(input.view()),
                                                 ksj::array::as_const_view(rhs.view()), output.view());
      },
      "intel complex vector divide backend rejected benchmark view");
  });
  measure_complex_vector_output("complex_vector_divide", "mkl_vml_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::vml::divide(ksj::array::as_const_view(input.view()),
                                                      ksj::array::as_const_view(rhs.view()), output.view());
      },
      "MKL VML complex vector divide backend rejected benchmark view");
  });

  const complex_type scalar{static_cast<T>(1.25), static_cast<T>(0.5)};
  measure_complex_vector_output("complex_vector_add_scalar", "manual_loop", type_name, vector_size, config, output,
                                [&] {
                                  for (std::size_t index = 0U; index < input.size(); ++index) {
                                    output.data()[index] = input.data()[index] + scalar;
                                  }
                                });
  measure_complex_vector_output("complex_vector_add_scalar", "public_policy", type_name, vector_size, config, output,
                                [&] {
                                  ksj::array::add_scalar(input.view(), scalar, output.view());
                                });
  measure_complex_vector_output(
    "complex_vector_add_scalar", "eigen_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::add_scalar(ksj::array::as_const_view(input.view()), scalar, output.view());
        },
        "eigen complex vector add_scalar backend rejected benchmark view");
    });
  measure_complex_vector_output(
    "complex_vector_add_scalar", "intel_detail", type_name, vector_size, config, output, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::add_scalar(ksj::array::as_const_view(input.view()), scalar, output.view());
        },
        "intel complex vector add_scalar backend rejected benchmark view");
    });

  measure_complex_vector_output("complex_vector_scale", "manual_loop", type_name, vector_size, config, output, [&] {
    for (std::size_t index = 0U; index < input.size(); ++index) {
      output.data()[index] = input.data()[index] * scalar;
    }
  });
  measure_complex_vector_output("complex_vector_scale", "public_policy", type_name, vector_size, config, output, [&] {
    ksj::array::scale(input.view(), scalar, output.view());
  });
  measure_complex_vector_output("complex_vector_scale", "eigen_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::eigen::scale(ksj::array::as_const_view(input.view()), scalar, output.view());
      },
      "eigen complex vector scale backend rejected benchmark view");
  });
  measure_complex_vector_output("complex_vector_scale", "intel_detail", type_name, vector_size, config, output, [&] {
    require_backend(
      [&] {
        return ksj::array::detail::intel::scale(ksj::array::as_const_view(input.view()), scalar, output.view());
      },
      "intel complex vector scale backend rejected benchmark view");
  });

  print_squared_norm_oracle(type_name, vector_size, input);
  measure_complex_scalar<T>("complex_vector_squared_norm", "manual_fused", type_name, vector_size, config, [&] {
    T sum{};
    for (std::size_t index = 0U; index < input.size(); ++index) {
      sum += static_cast<T>(std::norm(input.data()[index]));
    }
    return sum;
  });
  measure_complex_scalar<T>("complex_vector_squared_norm", "separate_public", type_name, vector_size, config, [&] {
    ksj::array::absolute(input.view(), magnitude.view());
    ksj::array::square(magnitude.view(), magnitude_squared.view());
    return ksj::array::sum(magnitude_squared.view());
  });
  measure_complex_scalar<T>("complex_vector_squared_norm", "public_fused", type_name, vector_size, config, [&] {
    return ksj::array::squared_norm(input.view());
  });
}

template <typename T> void run_complex_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto element_count : config.sizes) {
    run_complex_elementwise_benchmarks<T>(type_name, element_count, config);

    const auto shape2d = element_count_shape2d(element_count);
    auto complex_image = ksj::array::make_pooled_image<std::complex<T>>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("complex_image", complex_image);
    fill_complex_image(complex_image);
    const auto image_size = complex_image.size();

    double public_magnitude_checksum = 0.0;
    const auto public_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::array::magnitude(complex_image);
      public_magnitude_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_magnitude_checksum);
    });
    print_array_complex_benchmark_row("complex_magnitude_image", "public_api", type_name, image_size, config,
                                      public_magnitude_ns, public_magnitude_checksum);

    auto public_magnitude_output = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("public_magnitude_output", public_magnitude_output);
    double public_magnitude_output_checksum = 0.0;
    const auto public_magnitude_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::magnitude(complex_image.view(), public_magnitude_output.view());
      public_magnitude_output_checksum = ksj::benchmarks::checksum(public_magnitude_output);
      ksj::benchmarks::do_not_optimize(public_magnitude_output_checksum);
    });
    print_array_complex_benchmark_row("complex_magnitude_image", "public_output", type_name, image_size, config,
                                      public_magnitude_output_ns, public_magnitude_output_checksum);

    auto eigen_magnitude = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("eigen_magnitude", eigen_magnitude);
    double eigen_magnitude_checksum = 0.0;
    const auto eigen_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::array::detail::eigen::complex_magnitude(ksj::array::as_const_view(complex_image.view()),
                                                        eigen_magnitude.view())) {
        throw std::runtime_error("eigen complex magnitude backend is unavailable");
      }
      eigen_magnitude_checksum = ksj::benchmarks::checksum(eigen_magnitude);
      ksj::benchmarks::do_not_optimize(eigen_magnitude_checksum);
    });
    print_array_complex_benchmark_row("complex_magnitude_image", "pooled_eigen", type_name, image_size, config,
                                      eigen_magnitude_ns, eigen_magnitude_checksum);

    auto intel_magnitude = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("intel_magnitude", intel_magnitude);
    double intel_magnitude_checksum = 0.0;
    const auto intel_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::array::detail::intel::complex_magnitude(ksj::array::as_const_view(complex_image.view()),
                                                        intel_magnitude.view())) {
        throw std::runtime_error("intel complex magnitude backend is unavailable");
      }
      intel_magnitude_checksum = ksj::benchmarks::checksum(intel_magnitude);
      ksj::benchmarks::do_not_optimize(intel_magnitude_checksum);
    });
    print_array_complex_benchmark_row("complex_magnitude_image", "pooled_intel", type_name, image_size, config,
                                      intel_magnitude_ns, intel_magnitude_checksum);

    auto manual_magnitude = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("manual_magnitude", manual_magnitude);
    double manual_magnitude_checksum = 0.0;
    const auto manual_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0; index < complex_image.size(); ++index) {
        manual_magnitude.data()[index] = std::abs(complex_image.data()[index]);
      }
      manual_magnitude_checksum = ksj::benchmarks::checksum(manual_magnitude);
      ksj::benchmarks::do_not_optimize(manual_magnitude_checksum);
    });
    print_array_complex_benchmark_row("complex_magnitude_image", "manual_output", type_name, image_size, config,
                                      manual_magnitude_ns, manual_magnitude_checksum);

    auto public_soft_threshold_output = ksj::array::make_pooled_image<std::complex<T>>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("public_soft_threshold_output", public_soft_threshold_output);
    double public_soft_threshold_output_checksum = 0.0;
    const auto soft_threshold_epsilon = static_cast<T>(1.0e-9L);
    const auto public_soft_threshold_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::soft_threshold(ksj::array::as_const_view(complex_image.view()), public_soft_threshold_output.view(),
                                 T{0.25}, soft_threshold_epsilon);
      public_soft_threshold_output_checksum = checksum_complex_image(public_soft_threshold_output);
      ksj::benchmarks::do_not_optimize(public_soft_threshold_output_checksum);
    });
    print_array_complex_benchmark_row("complex_soft_threshold_image", "public_output", type_name, image_size, config,
                                      public_soft_threshold_output_ns, public_soft_threshold_output_checksum);

    auto manual_soft_threshold_output = ksj::array::make_pooled_image<std::complex<T>>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("manual_soft_threshold_output", manual_soft_threshold_output);
    double manual_soft_threshold_output_checksum = 0.0;
    const auto manual_soft_threshold_output_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0; index < complex_image.size(); ++index) {
        manual_soft_threshold_output.data()[index] =
          manual_soft_threshold(complex_image.data()[index], T{0.25}, soft_threshold_epsilon);
      }
      manual_soft_threshold_output_checksum = checksum_complex_image(manual_soft_threshold_output);
      ksj::benchmarks::do_not_optimize(manual_soft_threshold_output_checksum);
    });
    print_array_complex_benchmark_row("complex_soft_threshold_image", "manual_output", type_name, image_size, config,
                                      manual_soft_threshold_output_ns, manual_soft_threshold_output_checksum);

    double public_phase_checksum = 0.0;
    const auto public_phase_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::array::phase(complex_image);
      public_phase_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_phase_checksum);
    });
    print_array_complex_benchmark_row("complex_phase_image", "public_api", type_name, image_size, config,
                                      public_phase_ns, public_phase_checksum);

    auto public_phase_output = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("public_phase_output", public_phase_output);
    double public_phase_output_checksum = 0.0;
    const auto public_phase_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::phase(complex_image.view(), public_phase_output.view());
      public_phase_output_checksum = ksj::benchmarks::checksum(public_phase_output);
      ksj::benchmarks::do_not_optimize(public_phase_output_checksum);
    });
    print_array_complex_benchmark_row("complex_phase_image", "public_output", type_name, image_size, config,
                                      public_phase_output_ns, public_phase_output_checksum);

    auto eigen_phase = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("eigen_phase", eigen_phase);
    double eigen_phase_checksum = 0.0;
    const auto eigen_phase_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::array::detail::eigen::complex_phase(ksj::array::as_const_view(complex_image.view()),
                                                    eigen_phase.view())) {
        throw std::runtime_error("eigen complex phase backend is unavailable");
      }
      eigen_phase_checksum = ksj::benchmarks::checksum(eigen_phase);
      ksj::benchmarks::do_not_optimize(eigen_phase_checksum);
    });
    print_array_complex_benchmark_row("complex_phase_image", "pooled_eigen", type_name, image_size, config,
                                      eigen_phase_ns, eigen_phase_checksum);

    auto intel_phase = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("intel_phase", intel_phase);
    double intel_phase_checksum = 0.0;
    const auto intel_phase_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::array::detail::intel::complex_phase(ksj::array::as_const_view(complex_image.view()),
                                                    intel_phase.view())) {
        throw std::runtime_error("intel complex phase backend is unavailable");
      }
      intel_phase_checksum = ksj::benchmarks::checksum(intel_phase);
      ksj::benchmarks::do_not_optimize(intel_phase_checksum);
    });
    print_array_complex_benchmark_row("complex_phase_image", "pooled_intel", type_name, image_size, config,
                                      intel_phase_ns, intel_phase_checksum);

    auto manual_phase = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("manual_phase", manual_phase);
    double manual_phase_checksum = 0.0;
    const auto manual_phase_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0; index < complex_image.size(); ++index) {
        manual_phase.data()[index] = std::arg(complex_image.data()[index]);
      }
      manual_phase_checksum = ksj::benchmarks::checksum(manual_phase);
      ksj::benchmarks::do_not_optimize(manual_phase_checksum);
    });
    print_array_complex_benchmark_row("complex_phase_image", "manual_output", type_name, image_size, config,
                                      manual_phase_ns, manual_phase_checksum);

    double public_polar_checksum = 0.0;
    const auto public_polar_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::array::rectangular_to_polar(complex_image);
      public_polar_checksum = checksum_complex_image(output);
      ksj::benchmarks::do_not_optimize(public_polar_checksum);
    });
    print_array_complex_benchmark_row("rectangular_to_polar_image", "public_api", type_name, image_size, config,
                                      public_polar_ns, public_polar_checksum);

    auto public_polar_output = ksj::array::make_pooled_image<std::complex<T>>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("public_polar_output", public_polar_output);
    double public_polar_output_checksum = 0.0;
    const auto public_polar_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::rectangular_to_polar(complex_image.view(), public_polar_output.view());
      public_polar_output_checksum = checksum_complex_image(public_polar_output);
      ksj::benchmarks::do_not_optimize(public_polar_output_checksum);
    });
    print_array_complex_benchmark_row("rectangular_to_polar_image", "public_output", type_name, image_size, config,
                                      public_polar_output_ns, public_polar_output_checksum);

    auto eigen_polar = ksj::array::make_pooled_image<std::complex<T>>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("eigen_polar", eigen_polar);
    double eigen_polar_checksum = 0.0;
    const auto eigen_polar_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::array::detail::eigen::rectangular_to_polar(ksj::array::as_const_view(complex_image.view()),
                                                           eigen_polar.view())) {
        throw std::runtime_error("eigen rectangular_to_polar backend is unavailable");
      }
      eigen_polar_checksum = checksum_complex_image(eigen_polar);
      ksj::benchmarks::do_not_optimize(eigen_polar_checksum);
    });
    print_array_complex_benchmark_row("rectangular_to_polar_image", "pooled_eigen", type_name, image_size, config,
                                      eigen_polar_ns, eigen_polar_checksum);

    auto manual_polar = ksj::array::make_pooled_image<std::complex<T>>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("manual_polar", manual_polar);
    double manual_polar_checksum = 0.0;
    const auto manual_polar_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0; index < complex_image.size(); ++index) {
        const auto& value = complex_image.data()[index];
        manual_polar.data()[index] = {std::abs(value), std::arg(value)};
      }
      manual_polar_checksum = checksum_complex_image(manual_polar);
      ksj::benchmarks::do_not_optimize(manual_polar_checksum);
    });
    print_array_complex_benchmark_row("rectangular_to_polar_image", "manual_output", type_name, image_size, config,
                                      manual_polar_ns, manual_polar_checksum);
  }
}

} // namespace

void run_complex_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_complex_benchmarks<float>("float", config);
}

void run_complex_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_complex_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::array_benchmarks
