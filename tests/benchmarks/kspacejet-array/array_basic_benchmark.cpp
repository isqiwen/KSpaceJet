#include "array_benchmark_common.hpp"

#include "kspacejet/array/detail/eigen/eigen_array_elementwise.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_reductions.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_storage.hpp"
#include "kspacejet/array/detail/intel/intel_array_elementwise.hpp"
#include "kspacejet/array/detail/intel/intel_array_reductions.hpp"
#include "kspacejet/array/detail/intel/intel_array_storage.hpp"
#include "kspacejet/array/detail/intel/intel_array_vml.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ksj::benchmarks::array_benchmarks {
namespace {

template <typename Fn> void require_backend(Fn&& fn, const char* message) {
  if (!fn()) {
    throw std::runtime_error(message);
  }
}

template <typename T>
[[nodiscard]] ksj::benchmarks::RowMetadata
row_metadata(const std::string_view operation, const std::string_view backend, const std::string_view timing_scope,
             const std::size_t size, const bool vector_overload = true) {
  const auto selected_backend =
    backend == "public_policy" ? selected_elementwise_backend<T>(operation, size, vector_overload) : std::string_view{};
  const auto effective_backend = selected_backend.empty() ? backend : selected_backend;
  const auto relative_tolerance = benchmark_relative_tolerance<T>(operation, size, effective_backend);
  if (operation == "vector_hypot" && backend == "eigen_expr") {
    // The naive expression has different overflow/underflow semantics from hypot().
    return ksj::benchmarks::candidate_row("vector_hypot_naive_expression", timing_scope, -1.0, relative_tolerance);
  }
  if (backend.starts_with("manual_")) {
    return ksj::benchmarks::reference_row(operation, timing_scope, -1.0, relative_tolerance);
  }
  if (backend == "public_policy") {
    return ksj::benchmarks::policy_row(operation, timing_scope, selected_backend, -1.0, relative_tolerance);
  }
  return ksj::benchmarks::candidate_row(operation, timing_scope, -1.0, relative_tolerance);
}

[[nodiscard]] ksj::benchmarks::RowMetadata array_basic_row_metadata(const std::string_view operation,
                                                                    const std::string_view backend) {
  const auto output_reuse = backend.starts_with("pooled_") || backend.starts_with("manual_") ||
                            backend.find("output") != std::string_view::npos;
  const auto timing_scope = output_reuse ? std::string_view{"output_reuse"} : std::string_view{"allocating"};
  if (backend.starts_with("manual_")) {
    return ksj::benchmarks::reference_row(operation, timing_scope);
  }
  return ksj::benchmarks::candidate_row(operation, timing_scope);
}

inline void print_array_basic_benchmark_row(const std::string_view operation, const std::string_view backend,
                                            const std::string_view type_name, const std::size_t size,
                                            const ksj::benchmarks::Config& config,
                                            const ksj::benchmarks::Measurement& measurement, const double checksum) {
  if (backend == "pooled_eigen") {
    const auto baseline_group = std::string{operation} + "_pooled_eigen_baseline";
    ksj::benchmarks::print_row(operation, backend, type_name, size, config, measurement, checksum,
                               ksj::benchmarks::reference_row(baseline_group, "output_reuse"));
    return;
  }
  ksj::benchmarks::print_row(operation, backend, type_name, size, config, measurement, checksum,
                             array_basic_row_metadata(operation, backend));
}

inline void print_array_basic_benchmark_row(const std::string_view operation, const std::string_view backend,
                                            const std::string_view type_name, const std::size_t size,
                                            const ksj::benchmarks::Config& config,
                                            const ksj::benchmarks::Measurement& measurement, const double checksum,
                                            const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(operation, backend, type_name, size, config, measurement, checksum, metadata);
}

inline void print_array_basic_benchmark_row(const std::string_view operation, const std::string_view backend,
                                            const std::string_view type_name, const std::size_t size,
                                            const std::size_t iterations,
                                            const ksj::benchmarks::Measurement& measurement, const double checksum,
                                            const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(operation, backend, type_name, size, iterations, measurement, checksum, metadata);
}

template <typename T, typename Fn>
void measure_vector_output(std::string_view operation, std::string_view backend, std::string_view type_name,
                           std::size_t size, const ksj::benchmarks::Config& config, ksj::array::PooledVector<T>& output,
                           Fn&& fn) {
  fn();
  ksj::benchmarks::do_not_optimize(output.data()[0]);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    fn();
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  });
  print_array_basic_benchmark_row(operation, backend, type_name, size, config, ns, ksj::benchmarks::checksum(output),
                                  row_metadata<T>(operation, backend, "output_reuse", size));
}

template <typename Pooled, typename Fn>
void measure_pooled_output(std::string_view operation, std::string_view backend, std::string_view type_name,
                           std::size_t size, const ksj::benchmarks::Config& config, Pooled& output, Fn&& fn) {
  fn();
  ksj::benchmarks::do_not_optimize(output.data()[0]);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    fn();
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  });
  using value_type = std::remove_cv_t<typename Pooled::value_type>;
  print_array_basic_benchmark_row(operation, backend, type_name, size, config, ns, ksj::benchmarks::checksum(output),
                                  row_metadata<value_type>(operation, backend, "output_reuse", size, false));
}

template <typename View> [[nodiscard]] auto flat_const_vector_view(View view) {
  using value_type = std::remove_const_t<std::remove_pointer_t<decltype(view.data())>>;
  if (!view.is_contiguous()) {
    throw std::runtime_error("benchmark expected a contiguous input view");
  }
  return ksj::array::VectorView<const value_type>(view.data(), view.size());
}

template <typename View> [[nodiscard]] auto flat_vector_view(View view) {
  using value_type = std::remove_pointer_t<decltype(view.data())>;
  if (!view.is_contiguous()) {
    throw std::runtime_error("benchmark expected a contiguous output view");
  }
  return ksj::array::VectorView<value_type>(view.data(), view.size());
}

template <typename T, typename Fn>
void measure_vector_scalar(std::string_view operation, std::string_view backend, std::string_view type_name,
                           std::size_t size, const ksj::benchmarks::Config& config, Fn&& fn) {
  auto value = fn();
  ksj::benchmarks::do_not_optimize(value);
  const auto ns = ksj::benchmarks::measure(config, [&] {
    value = fn();
    ksj::benchmarks::do_not_optimize(value);
  });
  print_array_basic_benchmark_row(operation, backend, type_name, size, config, ns, static_cast<double>(value),
                                  row_metadata<T>(operation, backend, "scalar_result", size));
}

template <typename T>
void print_sum_oracle(const std::string_view type_name, const std::size_t size,
                      const ksj::array::PooledVector<T>& input) {
  long double sum = 0.0L;
  for (std::size_t index = 0U; index < input.size(); ++index) {
    sum += static_cast<long double>(input.data()[index]);
  }
  print_array_basic_benchmark_row("vector_sum", "high_precision_oracle", type_name, size, 1U,
                                  ksj::benchmarks::Measurement{}, static_cast<double>(sum),
                                  ksj::benchmarks::oracle_row("vector_sum", "scalar_result"));
}

template <typename T> void run_basic_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto element_count : config.sizes) {
    const auto vector_size = element_count;
    auto pooled_vector = ksj::array::make_pooled_vector<T>(vector_size);
    auto pooled_rhs_vector = ksj::array::make_pooled_vector<T>(vector_size);
    auto pooled_vector_temp = ksj::array::make_pooled_vector<T>(vector_size);
    auto pooled_vector_out = ksj::array::make_pooled_vector<T>(vector_size);
    ksj::benchmarks::require_pooled_storage("pooled_vector", pooled_vector);
    ksj::benchmarks::require_pooled_storage("pooled_rhs_vector", pooled_rhs_vector);
    ksj::benchmarks::require_pooled_storage("pooled_vector_temp", pooled_vector_temp);
    ksj::benchmarks::require_pooled_storage("pooled_vector_out", pooled_vector_out);
    ksj::benchmarks::fill_vector(pooled_vector);
    ksj::benchmarks::fill_vector(pooled_rhs_vector);

    measure_vector_output("vector_fill", "manual_std_fill", type_name, vector_size, config, pooled_vector_out, [&] {
      std::fill_n(pooled_vector_out.data(), pooled_vector_out.size(), static_cast<T>(3.25));
    });
    measure_vector_output("vector_fill", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::fill(pooled_vector_out.view(), static_cast<T>(3.25));
    });
    measure_vector_output("vector_fill", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::fill(pooled_vector_out.view(), static_cast<T>(3.25));
        },
        "eigen vector fill backend rejected benchmark view");
    });
    measure_vector_output("vector_fill", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::fill(pooled_vector_out.view(), static_cast<T>(3.25));
        },
        "intel vector fill backend rejected benchmark view");
    });

    measure_vector_output("vector_copy", "manual_std_copy", type_name, vector_size, config, pooled_vector_out, [&] {
      std::copy_n(pooled_vector.data(), pooled_vector.size(), pooled_vector_out.data());
    });
    measure_vector_output("vector_copy", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::copy(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_copy", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::copy(ksj::array::as_const_view(pooled_vector.view()),
                                                 pooled_vector_out.view());
        },
        "eigen vector copy backend rejected benchmark view");
    });
    measure_vector_output("vector_copy", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::copy(ksj::array::as_const_view(pooled_vector.view()),
                                                 pooled_vector_out.view());
        },
        "intel vector copy backend rejected benchmark view");
    });

    measure_vector_output("vector_add", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = pooled_vector.data()[index] + pooled_rhs_vector.data()[index];
      }
    });
    measure_vector_output("vector_add", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::add(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_add", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::add(ksj::array::as_const_view(pooled_vector.view()),
                                                ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                pooled_vector_out.view());
        },
        "eigen vector add backend rejected benchmark view");
    });
    measure_vector_output("vector_add", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::add(ksj::array::as_const_view(pooled_vector.view()),
                                                ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                pooled_vector_out.view());
        },
        "intel vector add backend rejected benchmark view");
    });
    measure_vector_output("vector_add", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::add(ksj::array::as_const_view(pooled_vector.view()),
                                                     ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                     pooled_vector_out.view());
        },
        "MKL VML vector add backend rejected benchmark view");
    });

    measure_vector_output("vector_subtract", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = pooled_vector.data()[index] - pooled_rhs_vector.data()[index];
      }
    });
    measure_vector_output("vector_subtract", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::subtract(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_subtract", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::subtract(ksj::array::as_const_view(pooled_vector.view()),
                                                     ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                     pooled_vector_out.view());
        },
        "eigen vector subtract backend rejected benchmark view");
    });
    measure_vector_output("vector_subtract", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::subtract(ksj::array::as_const_view(pooled_vector.view()),
                                                     ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                     pooled_vector_out.view());
        },
        "intel vector subtract backend rejected benchmark view");
    });
    measure_vector_output("vector_subtract", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::subtract(ksj::array::as_const_view(pooled_vector.view()),
                                                          ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                          pooled_vector_out.view());
        },
        "MKL VML vector subtract backend rejected benchmark view");
    });

    measure_vector_output("vector_multiply", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = pooled_vector.data()[index] * pooled_rhs_vector.data()[index];
      }
    });
    measure_vector_output("vector_multiply", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::multiply(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_multiply", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::multiply(ksj::array::as_const_view(pooled_vector.view()),
                                                     ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                     pooled_vector_out.view());
        },
        "eigen vector multiply backend rejected benchmark view");
    });
    measure_vector_output("vector_multiply", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::multiply(ksj::array::as_const_view(pooled_vector.view()),
                                                     ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                     pooled_vector_out.view());
        },
        "intel vector multiply backend rejected benchmark view");
    });
    measure_vector_output("vector_multiply", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::multiply(ksj::array::as_const_view(pooled_vector.view()),
                                                          ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                          pooled_vector_out.view());
        },
        "MKL VML vector multiply backend rejected benchmark view");
    });

    measure_vector_output("vector_divide", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = pooled_vector.data()[index] / pooled_rhs_vector.data()[index];
      }
    });
    measure_vector_output("vector_divide", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::divide(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_divide", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::divide(ksj::array::as_const_view(pooled_vector.view()),
                                                   ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                   pooled_vector_out.view());
        },
        "eigen vector divide backend rejected benchmark view");
    });
    measure_vector_output("vector_divide", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::divide(ksj::array::as_const_view(pooled_vector.view()),
                                                   ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                   pooled_vector_out.view());
        },
        "intel vector divide backend rejected benchmark view");
    });
    measure_vector_output("vector_divide", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::divide(ksj::array::as_const_view(pooled_vector.view()),
                                                        ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                        pooled_vector_out.view());
        },
        "MKL VML vector divide backend rejected benchmark view");
    });

    as_eigen(pooled_vector_out).array() = as_eigen(pooled_vector).array() * static_cast<T>(2);
    ksj::benchmarks::do_not_optimize(pooled_vector_out.data()[0]);
    const auto pooled_vector_scale_ns = ksj::benchmarks::measure(config, [&] {
      as_eigen(pooled_vector_out).array() = as_eigen(pooled_vector).array() * static_cast<T>(2);
      ksj::benchmarks::do_not_optimize(pooled_vector_out.data()[0]);
    });
    print_array_basic_benchmark_row("vector_scale", "pooled_eigen", type_name, vector_size, config,
                                    pooled_vector_scale_ns, ksj::benchmarks::checksum(pooled_vector_out));

    measure_vector_output("vector_scale", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = pooled_vector.data()[index] * static_cast<T>(2);
      }
    });
    measure_vector_output("vector_scale", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::scale(pooled_vector.view(), static_cast<T>(2), pooled_vector_out.view());
    });
    measure_vector_output("vector_scale", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::scale(ksj::array::as_const_view(pooled_vector.view()), static_cast<T>(2),
                                                  pooled_vector_out.view());
        },
        "eigen vector scale backend rejected benchmark view");
    });
    measure_vector_output("vector_scale", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::scale(ksj::array::as_const_view(pooled_vector.view()), static_cast<T>(2),
                                                  pooled_vector_out.view());
        },
        "intel vector scale backend rejected benchmark view");
    });

    measure_vector_output("vector_scale_add", "manual_fused", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] =
          pooled_vector.data()[index] * static_cast<T>(2) + pooled_rhs_vector.data()[index];
      }
    });
    measure_vector_output(
      "vector_scale_add", "separate_public", type_name, vector_size, config, pooled_vector_out, [&] {
        ksj::array::scale(pooled_vector.view(), static_cast<T>(2), pooled_vector_temp.view());
        ksj::array::add(pooled_vector_temp.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
      });
    measure_vector_output("vector_scale_add", "public_fused", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::scale_add(pooled_vector.view(), static_cast<T>(2), pooled_rhs_vector.view(),
                            pooled_vector_out.view());
    });

    measure_vector_output("vector_subtract_scalar", "manual_loop", type_name, vector_size, config, pooled_vector_out,
                          [&] {
                            for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
                              pooled_vector_out.data()[index] = pooled_vector.data()[index] - static_cast<T>(2);
                            }
                          });
    measure_vector_output(
      "vector_subtract_scalar", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
        ksj::array::subtract_scalar(pooled_vector.view(), static_cast<T>(2), pooled_vector_out.view());
      });
    measure_vector_output(
      "vector_subtract_scalar", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::eigen::subtract_scalar(ksj::array::as_const_view(pooled_vector.view()),
                                                              static_cast<T>(2), pooled_vector_out.view());
          },
          "eigen vector subtract_scalar backend rejected benchmark view");
      });
    measure_vector_output(
      "vector_subtract_scalar", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::intel::subtract_scalar(ksj::array::as_const_view(pooled_vector.view()),
                                                              static_cast<T>(2), pooled_vector_out.view());
          },
          "intel vector subtract_scalar backend rejected benchmark view");
      });

    measure_vector_output("vector_scalar_subtract", "manual_loop", type_name, vector_size, config, pooled_vector_out,
                          [&] {
                            for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
                              pooled_vector_out.data()[index] = static_cast<T>(2) - pooled_vector.data()[index];
                            }
                          });
    measure_vector_output(
      "vector_scalar_subtract", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
        ksj::array::scalar_subtract(pooled_vector.view(), static_cast<T>(2), pooled_vector_out.view());
      });
    measure_vector_output(
      "vector_scalar_subtract", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::eigen::scalar_subtract(ksj::array::as_const_view(pooled_vector.view()),
                                                              static_cast<T>(2), pooled_vector_out.view());
          },
          "eigen vector scalar_subtract backend rejected benchmark view");
      });
    measure_vector_output(
      "vector_scalar_subtract", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::intel::scalar_subtract(ksj::array::as_const_view(pooled_vector.view()),
                                                              static_cast<T>(2), pooled_vector_out.view());
          },
          "intel vector scalar_subtract backend rejected benchmark view");
      });

    measure_vector_output("vector_divide_scalar", "manual_loop", type_name, vector_size, config, pooled_vector_out,
                          [&] {
                            for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
                              pooled_vector_out.data()[index] = pooled_vector.data()[index] / static_cast<T>(2);
                            }
                          });
    measure_vector_output(
      "vector_divide_scalar", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
        ksj::array::divide_scalar(pooled_vector.view(), static_cast<T>(2), pooled_vector_out.view());
      });
    measure_vector_output(
      "vector_divide_scalar", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::eigen::divide_scalar(ksj::array::as_const_view(pooled_vector.view()),
                                                            static_cast<T>(2), pooled_vector_out.view());
          },
          "eigen vector divide_scalar backend rejected benchmark view");
      });
    measure_vector_output(
      "vector_divide_scalar", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::intel::divide_scalar(ksj::array::as_const_view(pooled_vector.view()),
                                                            static_cast<T>(2), pooled_vector_out.view());
          },
          "intel vector divide_scalar backend rejected benchmark view");
      });

    measure_vector_output("vector_scalar_divide", "manual_loop", type_name, vector_size, config, pooled_vector_out,
                          [&] {
                            for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
                              pooled_vector_out.data()[index] = static_cast<T>(2) / pooled_vector.data()[index];
                            }
                          });
    measure_vector_output(
      "vector_scalar_divide", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
        ksj::array::scalar_divide(pooled_vector.view(), static_cast<T>(2), pooled_vector_out.view());
      });
    measure_vector_output(
      "vector_scalar_divide", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
        require_backend(
          [&] {
            return ksj::array::detail::eigen::scalar_divide(ksj::array::as_const_view(pooled_vector.view()),
                                                            static_cast<T>(2), pooled_vector_out.view());
          },
          "eigen vector scalar_divide backend rejected benchmark view");
      });
    if constexpr (std::is_same_v<T, float>) {
      measure_vector_output(
        "vector_scalar_divide", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
          require_backend(
            [&] {
              return ksj::array::detail::intel::scalar_divide(ksj::array::as_const_view(pooled_vector.view()),
                                                              static_cast<T>(2), pooled_vector_out.view());
            },
            "intel vector scalar_divide backend rejected benchmark view");
        });
    }

    measure_vector_output("vector_negate", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = -pooled_vector.data()[index];
      }
    });
    measure_vector_output("vector_negate", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::negate(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_negate", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::negate(ksj::array::as_const_view(pooled_vector.view()),
                                                   pooled_vector_out.view());
        },
        "eigen vector negate backend rejected benchmark view");
    });
    measure_vector_output("vector_negate", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::negate(ksj::array::as_const_view(pooled_vector.view()),
                                                   pooled_vector_out.view());
        },
        "intel vector negate backend rejected benchmark view");
    });

    measure_vector_output("vector_absolute", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::abs(pooled_vector.data()[index]);
      }
    });
    measure_vector_output("vector_absolute", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::absolute(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_absolute", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::absolute(ksj::array::as_const_view(pooled_vector.view()),
                                                     pooled_vector_out.view());
        },
        "eigen vector absolute backend rejected benchmark view");
    });
    measure_vector_output("vector_absolute", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::absolute(ksj::array::as_const_view(pooled_vector.view()),
                                                     pooled_vector_out.view());
        },
        "intel vector absolute backend rejected benchmark view");
    });
    measure_vector_output("vector_absolute", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::absolute(ksj::array::as_const_view(pooled_vector.view()),
                                                          pooled_vector_out.view());
        },
        "MKL VML vector absolute backend rejected benchmark view");
    });

    measure_vector_output("vector_square", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = pooled_vector.data()[index] * pooled_vector.data()[index];
      }
    });
    measure_vector_output("vector_square", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::square(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_square", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::square(ksj::array::as_const_view(pooled_vector.view()),
                                                   pooled_vector_out.view());
        },
        "eigen vector square backend rejected benchmark view");
    });
    measure_vector_output("vector_square", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::square(ksj::array::as_const_view(pooled_vector.view()),
                                                   pooled_vector_out.view());
        },
        "intel vector square backend rejected benchmark view");
    });

    measure_vector_output("vector_sqrt", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::sqrt(pooled_vector.data()[index]);
      }
    });
    measure_vector_output("vector_sqrt", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::sqrt(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_sqrt", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::sqrt(ksj::array::as_const_view(pooled_vector.view()),
                                                 pooled_vector_out.view());
        },
        "eigen vector sqrt backend rejected benchmark view");
    });
    measure_vector_output("vector_sqrt", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::sqrt(ksj::array::as_const_view(pooled_vector.view()),
                                                 pooled_vector_out.view());
        },
        "intel vector sqrt backend rejected benchmark view");
    });
    measure_vector_output("vector_sqrt", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::sqrt(ksj::array::as_const_view(pooled_vector.view()),
                                                      pooled_vector_out.view());
        },
        "MKL VML vector sqrt backend rejected benchmark view");
    });

    measure_vector_output("vector_inverse", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = static_cast<T>(1) / pooled_vector.data()[index];
      }
    });
    measure_vector_output("vector_inverse", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::inverse(ksj::array::as_const_view(pooled_vector.view()),
                                                    pooled_vector_out.view());
        },
        "Eigen vector inverse backend rejected benchmark view");
    });
    measure_vector_output("vector_inverse", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::inverse(ksj::array::as_const_view(pooled_vector.view()),
                                                         pooled_vector_out.view());
        },
        "MKL VML vector inverse backend rejected benchmark view");
    });
    measure_vector_output("vector_inverse", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::inverse(pooled_vector.view(), pooled_vector_out.view());
    });

    measure_vector_output("vector_rsqrt", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = static_cast<T>(1) / std::sqrt(pooled_vector.data()[index]);
      }
    });
    measure_vector_output("vector_rsqrt", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::inverse_sqrt(ksj::array::as_const_view(pooled_vector.view()),
                                                         pooled_vector_out.view());
        },
        "Eigen vector inverse_sqrt backend rejected benchmark view");
    });
    measure_vector_output("vector_rsqrt", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::inverse_sqrt(ksj::array::as_const_view(pooled_vector.view()),
                                                              pooled_vector_out.view());
        },
        "MKL VML vector inverse_sqrt backend rejected benchmark view");
    });
    measure_vector_output("vector_rsqrt", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::rsqrt(pooled_vector.view(), pooled_vector_out.view());
    });

    measure_vector_output("vector_exp", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::exp(pooled_vector.data()[index]);
      }
    });
    measure_vector_output("vector_exp", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::exp(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_exp", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::exp(ksj::array::as_const_view(pooled_vector.view()),
                                                pooled_vector_out.view());
        },
        "eigen vector exp backend rejected benchmark view");
    });
    measure_vector_output("vector_exp", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::exp(ksj::array::as_const_view(pooled_vector.view()),
                                                pooled_vector_out.view());
        },
        "intel vector exp backend rejected benchmark view");
    });
    measure_vector_output("vector_exp", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::exp(ksj::array::as_const_view(pooled_vector.view()),
                                                     pooled_vector_out.view());
        },
        "MKL VML vector exp backend rejected benchmark view");
    });

    measure_vector_output("vector_log", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::log(pooled_vector.data()[index]);
      }
    });
    measure_vector_output("vector_log", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::log(pooled_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_log", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::log(ksj::array::as_const_view(pooled_vector.view()),
                                                pooled_vector_out.view());
        },
        "eigen vector log backend rejected benchmark view");
    });
    measure_vector_output("vector_log", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::log(ksj::array::as_const_view(pooled_vector.view()),
                                                pooled_vector_out.view());
        },
        "intel vector log backend rejected benchmark view");
    });
    measure_vector_output("vector_log", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::log(ksj::array::as_const_view(pooled_vector.view()),
                                                     pooled_vector_out.view());
        },
        "MKL VML vector log backend rejected benchmark view");
    });

    measure_vector_output("vector_minimum", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::min(pooled_vector.data()[index], pooled_rhs_vector.data()[index]);
      }
    });
    measure_vector_output("vector_minimum", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::minimum(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_minimum", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::minimum(ksj::array::as_const_view(pooled_vector.view()),
                                                    ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                    pooled_vector_out.view());
        },
        "eigen vector minimum backend rejected benchmark view");
    });
    measure_vector_output("vector_minimum", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::minimum(ksj::array::as_const_view(pooled_vector.view()),
                                                    ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                    pooled_vector_out.view());
        },
        "intel vector minimum backend rejected benchmark view");
    });
    measure_vector_output("vector_minimum", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::minimum(ksj::array::as_const_view(pooled_vector.view()),
                                                         ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                         pooled_vector_out.view());
        },
        "MKL VML vector minimum backend rejected benchmark view");
    });

    measure_vector_output("vector_maximum", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::max(pooled_vector.data()[index], pooled_rhs_vector.data()[index]);
      }
    });
    measure_vector_output("vector_maximum", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::maximum(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });
    measure_vector_output("vector_maximum", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::maximum(ksj::array::as_const_view(pooled_vector.view()),
                                                    ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                    pooled_vector_out.view());
        },
        "eigen vector maximum backend rejected benchmark view");
    });
    measure_vector_output("vector_maximum", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::maximum(ksj::array::as_const_view(pooled_vector.view()),
                                                    ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                    pooled_vector_out.view());
        },
        "intel vector maximum backend rejected benchmark view");
    });
    measure_vector_output("vector_maximum", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::maximum(ksj::array::as_const_view(pooled_vector.view()),
                                                         ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                         pooled_vector_out.view());
        },
        "MKL VML vector maximum backend rejected benchmark view");
    });

    measure_vector_output("vector_hypot", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] = std::hypot(pooled_vector.data()[index], pooled_rhs_vector.data()[index]);
      }
    });
    measure_vector_output("vector_hypot", "eigen_expr", type_name, vector_size, config, pooled_vector_out, [&] {
      as_eigen(pooled_vector_out).array() =
        (as_eigen(pooled_vector).array().square() + as_eigen(pooled_rhs_vector).array().square()).sqrt();
    });
    measure_vector_output("vector_hypot", "mkl_vml_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::hypot(ksj::array::as_const_view(pooled_vector.view()),
                                                       ksj::array::as_const_view(pooled_rhs_vector.view()),
                                                       pooled_vector_out.view());
        },
        "MKL VML vector hypot backend rejected benchmark view");
    });
    measure_vector_output("vector_hypot", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::hypot(pooled_vector.view(), pooled_rhs_vector.view(), pooled_vector_out.view());
    });

    measure_vector_output("vector_clamp", "manual_loop", type_name, vector_size, config, pooled_vector_out, [&] {
      for (std::size_t index = 0U; index < pooled_vector.size(); ++index) {
        pooled_vector_out.data()[index] =
          std::clamp(pooled_vector.data()[index], static_cast<T>(2), static_cast<T>(12));
      }
    });
    measure_vector_output("vector_clamp", "public_policy", type_name, vector_size, config, pooled_vector_out, [&] {
      ksj::array::clamp(pooled_vector.view(), pooled_vector_out.view(), static_cast<T>(2), static_cast<T>(12));
    });
    measure_vector_output("vector_clamp", "eigen_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::clamp(ksj::array::as_const_view(pooled_vector.view()), static_cast<T>(2),
                                                  static_cast<T>(12), pooled_vector_out.view());
        },
        "eigen vector clamp backend rejected benchmark view");
    });
    measure_vector_output("vector_clamp", "intel_detail", type_name, vector_size, config, pooled_vector_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::clamp(ksj::array::as_const_view(pooled_vector.view()), static_cast<T>(2),
                                                  static_cast<T>(12), pooled_vector_out.view());
        },
        "intel vector clamp backend rejected benchmark view");
    });

    print_sum_oracle(type_name, vector_size, pooled_vector);
    measure_vector_scalar<T>("vector_sum", "manual_loop", type_name, vector_size, config, [&] {
      return std::accumulate(pooled_vector.data(), pooled_vector.data() + pooled_vector.size(), T{});
    });
    measure_vector_scalar<T>("vector_sum", "public_policy", type_name, vector_size, config, [&] {
      return ksj::array::sum(pooled_vector.view());
    });
    measure_vector_scalar<T>("vector_sum", "eigen_detail", type_name, vector_size, config, [&] {
      T output{};
      require_backend(
        [&] {
          return ksj::array::detail::eigen::sum(ksj::array::as_const_view(pooled_vector.view()), output);
        },
        "eigen vector sum backend rejected benchmark view");
      return output;
    });
    measure_vector_scalar<T>("vector_sum", "intel_detail", type_name, vector_size, config, [&] {
      T output{};
      require_backend(
        [&] {
          return ksj::array::detail::intel::sum(ksj::array::as_const_view(pooled_vector.view()), output);
        },
        "intel vector sum backend rejected benchmark view");
      return output;
    });

    measure_vector_scalar<T>("vector_min", "manual_loop", type_name, vector_size, config, [&] {
      return *std::min_element(pooled_vector.data(), pooled_vector.data() + pooled_vector.size());
    });
    measure_vector_scalar<T>("vector_min", "public_policy", type_name, vector_size, config, [&] {
      return ksj::array::min(pooled_vector.view());
    });
    measure_vector_scalar<T>("vector_min", "eigen_detail", type_name, vector_size, config, [&] {
      T output{};
      require_backend(
        [&] {
          return ksj::array::detail::eigen::min(ksj::array::as_const_view(pooled_vector.view()), output);
        },
        "eigen vector min backend rejected benchmark view");
      return output;
    });
    measure_vector_scalar<T>("vector_min", "intel_detail", type_name, vector_size, config, [&] {
      T output{};
      require_backend(
        [&] {
          return ksj::array::detail::intel::min(ksj::array::as_const_view(pooled_vector.view()), output);
        },
        "intel vector min backend rejected benchmark view");
      return output;
    });

    measure_vector_scalar<T>("vector_max", "manual_loop", type_name, vector_size, config, [&] {
      return *std::max_element(pooled_vector.data(), pooled_vector.data() + pooled_vector.size());
    });
    measure_vector_scalar<T>("vector_max", "public_policy", type_name, vector_size, config, [&] {
      return ksj::array::max(pooled_vector.view());
    });
    measure_vector_scalar<T>("vector_max", "eigen_detail", type_name, vector_size, config, [&] {
      T output{};
      require_backend(
        [&] {
          return ksj::array::detail::eigen::max(ksj::array::as_const_view(pooled_vector.view()), output);
        },
        "eigen vector max backend rejected benchmark view");
      return output;
    });
    measure_vector_scalar<T>("vector_max", "intel_detail", type_name, vector_size, config, [&] {
      T output{};
      require_backend(
        [&] {
          return ksj::array::detail::intel::max(ksj::array::as_const_view(pooled_vector.view()), output);
        },
        "intel vector max backend rejected benchmark view");
      return output;
    });

    EigenVector<T> eigen_vector(static_cast<Eigen::Index>(vector_size));
    EigenVector<T> eigen_vector_out(static_cast<Eigen::Index>(vector_size));
    fill_eigen_vector(eigen_vector);
    eigen_vector_out.array() = eigen_vector.array() * static_cast<T>(2);
    ksj::benchmarks::do_not_optimize(eigen_vector_out.data()[0]);
    const auto eigen_vector_scale_ns = ksj::benchmarks::measure(config, [&] {
      eigen_vector_out.array() = eigen_vector.array() * static_cast<T>(2);
      ksj::benchmarks::do_not_optimize(eigen_vector_out.data()[0]);
    });
    print_array_basic_benchmark_row("vector_scale", "eigen_heap", type_name, vector_size, config, eigen_vector_scale_ns,
                                    static_cast<double>(eigen_vector_out.sum()));

    const auto shape2d = element_count_shape2d(element_count);
    auto pooled_lhs = ksj::array::make_pooled_matrix<T>(shape2d.rows, shape2d.cols);
    auto pooled_rhs = ksj::array::make_pooled_matrix<T>(shape2d.rows, shape2d.cols);
    auto pooled_matrix_out = ksj::array::make_pooled_matrix<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("pooled_lhs", pooled_lhs);
    ksj::benchmarks::require_pooled_storage("pooled_rhs", pooled_rhs);
    ksj::benchmarks::require_pooled_storage("pooled_matrix_out", pooled_matrix_out);
    ksj::benchmarks::fill_matrix(pooled_lhs);
    ksj::benchmarks::fill_matrix(pooled_rhs);
    const auto matrix_size = pooled_lhs.size();

    measure_pooled_output("matrix_add", "manual_loop", type_name, matrix_size, config, pooled_matrix_out, [&] {
      for (std::size_t index = 0U; index < pooled_lhs.size(); ++index) {
        pooled_matrix_out.data()[index] = pooled_lhs.data()[index] + pooled_rhs.data()[index];
      }
    });
    measure_pooled_output("matrix_add", "public_policy", type_name, matrix_size, config, pooled_matrix_out, [&] {
      ksj::array::add(pooled_lhs.view(), pooled_rhs.view(), pooled_matrix_out.view());
    });
    measure_pooled_output("matrix_add", "flatten_eigen_detail", type_name, matrix_size, config, pooled_matrix_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::add(flat_const_vector_view(ksj::array::as_const_view(pooled_lhs.view())),
                                                flat_const_vector_view(ksj::array::as_const_view(pooled_rhs.view())),
                                                flat_vector_view(pooled_matrix_out.view()));
        },
        "eigen flattened matrix add backend rejected benchmark view");
    });
    measure_pooled_output("matrix_add", "flatten_intel_detail", type_name, matrix_size, config, pooled_matrix_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::add(flat_const_vector_view(ksj::array::as_const_view(pooled_lhs.view())),
                                                flat_const_vector_view(ksj::array::as_const_view(pooled_rhs.view())),
                                                flat_vector_view(pooled_matrix_out.view()));
        },
        "intel flattened matrix add backend rejected benchmark view");
    });
    measure_pooled_output("matrix_add", "flatten_mkl_vml_detail", type_name, matrix_size, config, pooled_matrix_out,
                          [&] {
                            require_backend(
                              [&] {
                                return ksj::array::detail::intel::vml::add(
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_lhs.view())),
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_rhs.view())),
                                  flat_vector_view(pooled_matrix_out.view()));
                              },
                              "MKL VML flattened matrix add backend rejected benchmark view");
                          });

    as_eigen(pooled_matrix_out) = as_eigen(pooled_lhs) + as_eigen(pooled_rhs);
    ksj::benchmarks::do_not_optimize(pooled_matrix_out.data()[0]);
    const auto pooled_matrix_add_ns = ksj::benchmarks::measure(config, [&] {
      as_eigen(pooled_matrix_out) = as_eigen(pooled_lhs) + as_eigen(pooled_rhs);
      ksj::benchmarks::do_not_optimize(pooled_matrix_out.data()[0]);
    });
    print_array_basic_benchmark_row("matrix_add", "pooled_eigen", type_name, matrix_size, config, pooled_matrix_add_ns,
                                    ksj::benchmarks::checksum(pooled_matrix_out));

    EigenMatrix<T> eigen_lhs(static_cast<Eigen::Index>(shape2d.rows), static_cast<Eigen::Index>(shape2d.cols));
    EigenMatrix<T> eigen_rhs(static_cast<Eigen::Index>(shape2d.rows), static_cast<Eigen::Index>(shape2d.cols));
    EigenMatrix<T> eigen_matrix_out(static_cast<Eigen::Index>(shape2d.rows), static_cast<Eigen::Index>(shape2d.cols));
    fill_eigen_matrix(eigen_lhs);
    fill_eigen_matrix(eigen_rhs);
    eigen_matrix_out = eigen_lhs + eigen_rhs;
    ksj::benchmarks::do_not_optimize(eigen_matrix_out.data()[0]);
    const auto eigen_matrix_add_ns = ksj::benchmarks::measure(config, [&] {
      eigen_matrix_out = eigen_lhs + eigen_rhs;
      ksj::benchmarks::do_not_optimize(eigen_matrix_out.data()[0]);
    });
    print_array_basic_benchmark_row("matrix_add", "eigen_heap", type_name, matrix_size, config, eigen_matrix_add_ns,
                                    static_cast<double>(eigen_matrix_out.sum()));

    auto pooled_image = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    auto pooled_rhs_image = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    auto pooled_image_out = ksj::array::make_pooled_image<T>(shape2d.rows, shape2d.cols);
    ksj::benchmarks::require_pooled_storage("pooled_image", pooled_image);
    ksj::benchmarks::require_pooled_storage("pooled_rhs_image", pooled_rhs_image);
    ksj::benchmarks::require_pooled_storage("pooled_image_out", pooled_image_out);
    ksj::benchmarks::fill_image(pooled_image);
    ksj::benchmarks::fill_image(pooled_rhs_image);
    const auto image_size = pooled_image.size();

    measure_pooled_output("image_add", "manual_loop", type_name, image_size, config, pooled_image_out, [&] {
      for (std::size_t index = 0U; index < pooled_image.size(); ++index) {
        pooled_image_out.data()[index] = pooled_image.data()[index] + pooled_rhs_image.data()[index];
      }
    });
    measure_pooled_output("image_add", "public_policy", type_name, image_size, config, pooled_image_out, [&] {
      ksj::array::add(pooled_image.view(), pooled_rhs_image.view(), pooled_image_out.view());
    });
    measure_pooled_output("image_add", "flatten_eigen_detail", type_name, image_size, config, pooled_image_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::add(
            flat_const_vector_view(ksj::array::as_const_view(pooled_image.view())),
            flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_image.view())),
            flat_vector_view(pooled_image_out.view()));
        },
        "eigen flattened image add backend rejected benchmark view");
    });
    measure_pooled_output("image_add", "flatten_intel_detail", type_name, image_size, config, pooled_image_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::add(
            flat_const_vector_view(ksj::array::as_const_view(pooled_image.view())),
            flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_image.view())),
            flat_vector_view(pooled_image_out.view()));
        },
        "intel flattened image add backend rejected benchmark view");
    });
    measure_pooled_output("image_add", "flatten_mkl_vml_detail", type_name, image_size, config, pooled_image_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::add(
            flat_const_vector_view(ksj::array::as_const_view(pooled_image.view())),
            flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_image.view())),
            flat_vector_view(pooled_image_out.view()));
        },
        "MKL VML flattened image add backend rejected benchmark view");
    });

    double pooled_layout_checksum = 0.0;
    {
      const auto matrix = ksj::array::copy_as_matrix(pooled_image);
      ksj::benchmarks::do_not_optimize(matrix.data()[0]);
    }
    const auto pooled_layout_copy_ns = ksj::benchmarks::measure(config, [&] {
      auto matrix = ksj::array::copy_as_matrix(pooled_image);
      pooled_layout_checksum = ksj::benchmarks::checksum(matrix);
      ksj::benchmarks::do_not_optimize(pooled_layout_checksum);
    });
    print_array_basic_benchmark_row("copy_image_as_matrix", "pooled_eigen", type_name, image_size, config,
                                    pooled_layout_copy_ns, pooled_layout_checksum);

    EigenImage<T> eigen_image(static_cast<Eigen::Index>(shape2d.rows), static_cast<Eigen::Index>(shape2d.cols));
    fill_eigen_image(eigen_image);
    double eigen_layout_checksum = 0.0;
    {
      const EigenMatrix<T> matrix = eigen_image;
      ksj::benchmarks::do_not_optimize(matrix.data()[0]);
    }
    const auto eigen_layout_copy_ns = ksj::benchmarks::measure(config, [&] {
      EigenMatrix<T> matrix = eigen_image;
      eigen_layout_checksum = static_cast<double>(matrix.sum());
      ksj::benchmarks::do_not_optimize(eigen_layout_checksum);
    });
    print_array_basic_benchmark_row("copy_image_as_matrix", "eigen_heap", type_name, image_size, config,
                                    eigen_layout_copy_ns, eigen_layout_checksum);

    const auto shape3d = element_count_shape3d(element_count);
    auto pooled_cube = ksj::array::make_pooled_cube<T>(shape3d.dim0, shape3d.dim1, shape3d.dim2);
    auto pooled_rhs_cube = ksj::array::make_pooled_cube<T>(shape3d.dim0, shape3d.dim1, shape3d.dim2);
    auto pooled_cube_out = ksj::array::make_pooled_cube<T>(shape3d.dim0, shape3d.dim1, shape3d.dim2);
    ksj::benchmarks::require_pooled_storage("pooled_cube", pooled_cube);
    ksj::benchmarks::require_pooled_storage("pooled_rhs_cube", pooled_rhs_cube);
    ksj::benchmarks::require_pooled_storage("pooled_cube_out", pooled_cube_out);
    ksj::benchmarks::fill_cube(pooled_cube);
    ksj::benchmarks::fill_cube(pooled_rhs_cube);
    const auto cube_size = pooled_cube.size();

    measure_pooled_output("cube_add", "manual_loop", type_name, cube_size, config, pooled_cube_out, [&] {
      for (std::size_t index = 0U; index < pooled_cube.size(); ++index) {
        pooled_cube_out.data()[index] = pooled_cube.data()[index] + pooled_rhs_cube.data()[index];
      }
    });
    measure_pooled_output("cube_add", "public_policy", type_name, cube_size, config, pooled_cube_out, [&] {
      ksj::array::add(pooled_cube.view(), pooled_rhs_cube.view(), pooled_cube_out.view());
    });
    measure_pooled_output("cube_add", "flatten_eigen_detail", type_name, cube_size, config, pooled_cube_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::eigen::add(
            flat_const_vector_view(ksj::array::as_const_view(pooled_cube.view())),
            flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_cube.view())),
            flat_vector_view(pooled_cube_out.view()));
        },
        "eigen flattened cube add backend rejected benchmark view");
    });
    measure_pooled_output("cube_add", "flatten_intel_detail", type_name, cube_size, config, pooled_cube_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::add(
            flat_const_vector_view(ksj::array::as_const_view(pooled_cube.view())),
            flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_cube.view())),
            flat_vector_view(pooled_cube_out.view()));
        },
        "intel flattened cube add backend rejected benchmark view");
    });
    measure_pooled_output("cube_add", "flatten_mkl_vml_detail", type_name, cube_size, config, pooled_cube_out, [&] {
      require_backend(
        [&] {
          return ksj::array::detail::intel::vml::add(
            flat_const_vector_view(ksj::array::as_const_view(pooled_cube.view())),
            flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_cube.view())),
            flat_vector_view(pooled_cube_out.view()));
        },
        "MKL VML flattened cube add backend rejected benchmark view");
    });

    const auto shape4d = element_count_shape4d(element_count);
    auto pooled_array4d = ksj::array::make_pooled_array4d<T>(shape4d.dim0, shape4d.dim1, shape4d.dim2, shape4d.dim3);
    auto pooled_rhs_array4d =
      ksj::array::make_pooled_array4d<T>(shape4d.dim0, shape4d.dim1, shape4d.dim2, shape4d.dim3);
    auto pooled_array4d_out =
      ksj::array::make_pooled_array4d<T>(shape4d.dim0, shape4d.dim1, shape4d.dim2, shape4d.dim3);
    ksj::benchmarks::require_pooled_storage("pooled_array4d", pooled_array4d);
    ksj::benchmarks::require_pooled_storage("pooled_rhs_array4d", pooled_rhs_array4d);
    ksj::benchmarks::require_pooled_storage("pooled_array4d_out", pooled_array4d_out);
    ksj::benchmarks::fill_array4d(pooled_array4d);
    ksj::benchmarks::fill_array4d(pooled_rhs_array4d);
    const auto array4d_size = pooled_array4d.size();

    measure_pooled_output("array4d_add", "manual_loop", type_name, array4d_size, config, pooled_array4d_out, [&] {
      for (std::size_t index = 0U; index < pooled_array4d.size(); ++index) {
        pooled_array4d_out.data()[index] = pooled_array4d.data()[index] + pooled_rhs_array4d.data()[index];
      }
    });
    measure_pooled_output("array4d_add", "public_policy", type_name, array4d_size, config, pooled_array4d_out, [&] {
      ksj::array::add(pooled_array4d.view(), pooled_rhs_array4d.view(), pooled_array4d_out.view());
    });
    measure_pooled_output("array4d_add", "flatten_eigen_detail", type_name, array4d_size, config, pooled_array4d_out,
                          [&] {
                            require_backend(
                              [&] {
                                return ksj::array::detail::eigen::add(
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_array4d.view())),
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_array4d.view())),
                                  flat_vector_view(pooled_array4d_out.view()));
                              },
                              "eigen flattened array4d add backend rejected benchmark view");
                          });
    measure_pooled_output("array4d_add", "flatten_intel_detail", type_name, array4d_size, config, pooled_array4d_out,
                          [&] {
                            require_backend(
                              [&] {
                                return ksj::array::detail::intel::add(
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_array4d.view())),
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_array4d.view())),
                                  flat_vector_view(pooled_array4d_out.view()));
                              },
                              "intel flattened array4d add backend rejected benchmark view");
                          });
    measure_pooled_output("array4d_add", "flatten_mkl_vml_detail", type_name, array4d_size, config, pooled_array4d_out,
                          [&] {
                            require_backend(
                              [&] {
                                return ksj::array::detail::intel::vml::add(
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_array4d.view())),
                                  flat_const_vector_view(ksj::array::as_const_view(pooled_rhs_array4d.view())),
                                  flat_vector_view(pooled_array4d_out.view()));
                              },
                              "MKL VML flattened array4d add backend rejected benchmark view");
                          });
  }
}

} // namespace

void run_basic_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_basic_benchmarks<float>("float", config);
}

void run_basic_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_basic_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::array_benchmarks
