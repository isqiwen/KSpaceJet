#include "benchmark_common.hpp"
#include "kspacejet/stats/stats.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_moments.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_error_metrics.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_norms.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_reductions.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_error_metrics.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_moments.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_norms.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_reductions.hpp"
#include "kspacejet/stats/detail/stats_policy.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {

inline constexpr std::size_t kMaxRssImageSize = 512;

template <typename T>
void fill_reference_vector(ksj::array::PooledVector<T>& reference, ksj::array::VectorView<const T> input) {
  for (std::size_t index = 0; index < reference.size(); ++index) {
    reference(index) = static_cast<T>(input(index) * static_cast<T>(0.75) + static_cast<T>(0.125));
  }
}

template <typename T>
void fill_reference_vector(ksj::array::PooledVector<std::complex<T>>& reference,
                           ksj::array::VectorView<const std::complex<T>> input) {
  for (std::size_t index = 0; index < reference.size(); ++index) {
    reference(index) = {static_cast<T>(input(index).real() * static_cast<T>(0.75) + static_cast<T>(0.125)),
                        static_cast<T>(input(index).imag() * static_cast<T>(0.5) - static_cast<T>(0.0625))};
  }
}

template <typename T> void copy_vector(ksj::array::PooledVector<T>& output, ksj::array::VectorView<const T> input) {
  for (std::size_t index = 0; index < output.size(); ++index) {
    output(index) = input(index);
  }
}

template <typename T> void fill_complex_cube(ksj::array::PooledCube<std::complex<T>>& cube) {
  for (std::size_t slice = 0; slice < cube.dim2(); ++slice) {
    for (std::size_t col = 0; col < cube.dim1(); ++col) {
      for (std::size_t row = 0; row < cube.dim0(); ++row) {
        const auto real = static_cast<T>(static_cast<double>((row * 17U + col * 31U + slice * 7U) % 251U) * 0.125);
        const auto imag = static_cast<T>(static_cast<double>((row * 13U + col * 19U + slice * 5U) % 127U) * 0.0625);
        cube(row, col, slice) = {real, imag};
      }
    }
  }
}

[[nodiscard]] ksj::benchmarks::RowMetadata scalar_row_metadata(const std::string_view case_name,
                                                               const std::string_view backend) {
  if (backend.starts_with("eigen") || backend.starts_with("manual")) {
    return ksj::benchmarks::reference_row(case_name, "scalar_result");
  }
  return ksj::benchmarks::candidate_row(case_name, "scalar_result");
}

inline void print_scalar_row(const std::string_view case_name, const std::string_view backend,
                             const std::string_view type_name, const std::size_t size,
                             const ksj::benchmarks::Config& config, const ksj::benchmarks::Measurement& measurement,
                             const double checksum) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum,
                             scalar_row_metadata(case_name, backend));
}

inline void print_scalar_row(const std::string_view case_name, const std::string_view backend,
                             const std::string_view type_name, const std::size_t size,
                             const ksj::benchmarks::Config& config, const ksj::benchmarks::Measurement& measurement,
                             const double checksum, const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum, metadata);
}

template <typename Result, typename ReferenceFunction, typename CandidateFunction, typename PolicyFunction>
void run_scalar_policy_case(std::string_view case_name, std::string_view comparison_group, std::string_view type_name,
                            const std::size_t size, const ksj::benchmarks::Config& config, const bool prefer_intel,
                            ReferenceFunction&& reference_function, CandidateFunction&& candidate_function,
                            PolicyFunction&& policy_function, const double absolute_tolerance = -1.0,
                            const double relative_tolerance = -1.0) {
  Result reference{};
  const auto reference_ns = ksj::benchmarks::measure(config, [&] {
    reference = reference_function();
    ksj::benchmarks::do_not_optimize(reference);
  });
  print_scalar_row(
    case_name, "eigen", type_name, size, config, reference_ns, static_cast<double>(reference),
    ksj::benchmarks::reference_row(comparison_group, "steady_state", absolute_tolerance, relative_tolerance));

  Result candidate{};
  if (candidate_function(candidate)) {
    const auto candidate_ns = ksj::benchmarks::measure(config, [&] {
      if (!candidate_function(candidate)) {
        throw std::runtime_error("Intel IPP stats backend failed during measurement");
      }
      ksj::benchmarks::do_not_optimize(candidate);
    });
    print_scalar_row(case_name, "intel_ipp", type_name, size, config, candidate_ns, static_cast<double>(candidate),
                     ksj::benchmarks::candidate_row(comparison_group, "steady_state"));
  }

  Result policy{};
  const auto policy_ns = ksj::benchmarks::measure(config, [&] {
    policy = policy_function();
    ksj::benchmarks::do_not_optimize(policy);
  });
  print_scalar_row(case_name, "public_policy", type_name, size, config, policy_ns, static_cast<double>(policy),
                   ksj::benchmarks::policy_row(comparison_group, "steady_state", prefer_intel ? "intel_ipp" : "eigen"));
}

template <typename T>
void run_norm_distance_policy_benchmarks(std::string_view type_name, const std::size_t size,
                                         const ksj::benchmarks::Config& config, ksj::array::VectorView<const T> input,
                                         ksj::array::VectorView<const T> reference) {
  using result_type = ksj::array::magnitude_result_t<T>;

  run_scalar_policy_case<result_type>(
    "max_abs", "stats.max_abs", type_name, size, config, ksj::stats::detail::prefer_intel_max_abs<T>(size),
    [&] {
      return ksj::stats::detail::eigen::max_abs(input);
    },
    [&](result_type& output) {
      return ksj::stats::detail::intel::max_abs(input, output);
    },
    [&] {
      return ksj::stats::max_abs(input);
    });

  run_scalar_policy_case<result_type>(
    "l1_distance", "stats.l1_distance", type_name, size, config, ksj::stats::detail::prefer_intel_l1_distance<T>(size),
    [&] {
      return ksj::stats::detail::eigen::l1_distance(input, reference);
    },
    [&](result_type& output) {
      return ksj::stats::detail::intel::l1_distance(input, reference, output);
    },
    [&] {
      return ksj::stats::l1_distance(input, reference);
    });

  run_scalar_policy_case<result_type>(
    "l2_distance", "stats.l2_distance", type_name, size, config, ksj::stats::detail::prefer_intel_l2_distance<T>(size),
    [&] {
      return ksj::stats::detail::eigen::l2_distance(input, reference);
    },
    [&](result_type& output) {
      return ksj::stats::detail::intel::l2_distance(input, reference, output);
    },
    [&] {
      return ksj::stats::l2_distance(input, reference);
    },
    -1.0, std::is_same_v<T, float> || std::is_same_v<T, ksj::base::cf32> ? 5.0e-5 : -1.0);

  run_scalar_policy_case<result_type>(
    "linf_distance", "stats.linf_distance", type_name, size, config,
    ksj::stats::detail::prefer_intel_linf_distance<T>(size),
    [&] {
      return ksj::stats::detail::eigen::linf_distance(input, reference);
    },
    [&](result_type& output) {
      return ksj::stats::detail::intel::linf_distance(input, reference, output);
    },
    [&] {
      return ksj::stats::linf_distance(input, reference);
    });
}

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_vector<T>(size);
    auto reference = ksj::array::make_pooled_vector<T>(size);
    auto equal_reference = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("input", input);
    ksj::benchmarks::require_pooled_storage("reference", reference);
    ksj::benchmarks::require_pooled_storage("equal_reference", equal_reference);
    ksj::benchmarks::fill_vector(input);
    const auto input_view = ksj::array::as_const_view(input.view());
    fill_reference_vector(reference, input_view);
    copy_vector(equal_reference, input_view);
    const auto reference_view = ksj::array::as_const_view(reference.view());
    const auto equal_reference_view = ksj::array::as_const_view(equal_reference.view());

    auto eigen_sum = ksj::stats::detail::eigen::sum(input_view);
    const auto eigen_sum_ns = ksj::benchmarks::measure(config, [&] {
      eigen_sum = ksj::stats::detail::eigen::sum(input_view);
      ksj::benchmarks::do_not_optimize(eigen_sum);
    });
    print_scalar_row("sum", "eigen", type_name, size, config, eigen_sum_ns, static_cast<double>(eigen_sum));

    T intel_sum{};
    if (ksj::stats::detail::intel::sum(input_view, intel_sum)) {
      const auto intel_sum_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::sum(input_view, intel_sum);
        ksj::benchmarks::do_not_optimize(intel_sum);
      });
      print_scalar_row("sum", "intel_ipp", type_name, size, config, intel_sum_ns, static_cast<double>(intel_sum));
    }

    auto public_sum = ksj::stats::sum(input);
    const auto public_sum_ns = ksj::benchmarks::measure(config, [&] {
      public_sum = ksj::stats::sum(input);
      ksj::benchmarks::do_not_optimize(public_sum);
    });
    print_scalar_row("sum", "public_api", type_name, size, config, public_sum_ns, static_cast<double>(public_sum));

    auto eigen_mean = ksj::stats::detail::eigen::mean(input_view);
    const auto eigen_mean_ns = ksj::benchmarks::measure(config, [&] {
      eigen_mean = ksj::stats::detail::eigen::mean(input_view);
      ksj::benchmarks::do_not_optimize(eigen_mean);
    });
    print_scalar_row("mean", "eigen", type_name, size, config, eigen_mean_ns, static_cast<double>(eigen_mean));

    T intel_mean{};
    if (ksj::stats::detail::intel::mean(input_view, intel_mean)) {
      const auto intel_mean_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::mean(input_view, intel_mean);
        ksj::benchmarks::do_not_optimize(intel_mean);
      });
      print_scalar_row("mean", "intel_ipp", type_name, size, config, intel_mean_ns, static_cast<double>(intel_mean));
    }

    auto public_mean = ksj::stats::mean(input);
    const auto public_mean_ns = ksj::benchmarks::measure(config, [&] {
      public_mean = ksj::stats::mean(input);
      ksj::benchmarks::do_not_optimize(public_mean);
    });
    print_scalar_row("mean", "public_api", type_name, size, config, public_mean_ns, static_cast<double>(public_mean));

    auto eigen_sos = ksj::stats::detail::eigen::sum_of_squares(input_view);
    const auto eigen_sos_ns = ksj::benchmarks::measure(config, [&] {
      eigen_sos = ksj::stats::detail::eigen::sum_of_squares(input_view);
      ksj::benchmarks::do_not_optimize(eigen_sos);
    });
    print_scalar_row("sum_of_squares", "eigen", type_name, size, config, eigen_sos_ns, static_cast<double>(eigen_sos));

    T intel_sos{};
    if (ksj::stats::detail::intel::sum_of_squares(input_view, intel_sos)) {
      const auto intel_sos_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::sum_of_squares(input_view, intel_sos);
        ksj::benchmarks::do_not_optimize(intel_sos);
      });
      print_scalar_row("sum_of_squares", "intel_ipp", type_name, size, config, intel_sos_ns,
                       static_cast<double>(intel_sos));
    }

    auto public_sos = ksj::stats::sum_of_squares(input);
    const auto public_sos_ns = ksj::benchmarks::measure(config, [&] {
      public_sos = ksj::stats::sum_of_squares(input);
      ksj::benchmarks::do_not_optimize(public_sos);
    });
    print_scalar_row("sum_of_squares", "public_api", type_name, size, config, public_sos_ns,
                     static_cast<double>(public_sos));

    auto eigen_rss = ksj::stats::detail::eigen::root_sum_of_squares(input_view);
    const auto eigen_rss_ns = ksj::benchmarks::measure(config, [&] {
      eigen_rss = ksj::stats::detail::eigen::root_sum_of_squares(input_view);
      ksj::benchmarks::do_not_optimize(eigen_rss);
    });
    print_scalar_row("root_sum_of_squares", "eigen", type_name, size, config, eigen_rss_ns,
                     static_cast<double>(eigen_rss));

    T intel_rss{};
    if (ksj::stats::detail::intel::root_sum_of_squares(input_view, intel_rss)) {
      const auto intel_rss_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::root_sum_of_squares(input_view, intel_rss);
        ksj::benchmarks::do_not_optimize(intel_rss);
      });
      print_scalar_row("root_sum_of_squares", "intel_ipp", type_name, size, config, intel_rss_ns,
                       static_cast<double>(intel_rss));
    }

    auto public_rss = ksj::stats::root_sum_of_squares(input);
    const auto public_rss_ns = ksj::benchmarks::measure(config, [&] {
      public_rss = ksj::stats::root_sum_of_squares(input);
      ksj::benchmarks::do_not_optimize(public_rss);
    });
    print_scalar_row("root_sum_of_squares", "public_api", type_name, size, config, public_rss_ns,
                     static_cast<double>(public_rss));

    auto eigen_sum_abs = ksj::stats::detail::eigen::sum_abs(input_view);
    const auto eigen_sum_abs_ns = ksj::benchmarks::measure(config, [&] {
      eigen_sum_abs = ksj::stats::detail::eigen::sum_abs(input_view);
      ksj::benchmarks::do_not_optimize(eigen_sum_abs);
    });
    print_scalar_row("sum_abs", "eigen", type_name, size, config, eigen_sum_abs_ns, static_cast<double>(eigen_sum_abs));

    T intel_sum_abs{};
    if (ksj::stats::detail::intel::sum_abs(input_view, intel_sum_abs)) {
      const auto intel_sum_abs_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::sum_abs(input_view, intel_sum_abs);
        ksj::benchmarks::do_not_optimize(intel_sum_abs);
      });
      print_scalar_row("sum_abs", "intel_ipp", type_name, size, config, intel_sum_abs_ns,
                       static_cast<double>(intel_sum_abs));
    }

    auto public_sum_abs = ksj::stats::sum_abs(input);
    const auto public_sum_abs_ns = ksj::benchmarks::measure(config, [&] {
      public_sum_abs = ksj::stats::sum_abs(input);
      ksj::benchmarks::do_not_optimize(public_sum_abs);
    });
    print_scalar_row("sum_abs", "public_api", type_name, size, config, public_sum_abs_ns,
                     static_cast<double>(public_sum_abs));

    auto eigen_max_index = ksj::stats::detail::eigen::max_index(input_view);
    const auto eigen_max_index_ns = ksj::benchmarks::measure(config, [&] {
      eigen_max_index = ksj::stats::detail::eigen::max_index(input_view);
      ksj::benchmarks::do_not_optimize(eigen_max_index);
    });
    print_scalar_row("max_index", "eigen", type_name, size, config, eigen_max_index_ns,
                     static_cast<double>(eigen_max_index));

    std::size_t intel_max_index = 0U;
    if (ksj::stats::detail::intel::max_index(input_view, intel_max_index)) {
      const auto intel_max_index_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::max_index(input_view, intel_max_index);
        ksj::benchmarks::do_not_optimize(intel_max_index);
      });
      print_scalar_row("max_index", "intel_ipp", type_name, size, config, intel_max_index_ns,
                       static_cast<double>(intel_max_index));
    }

    auto public_max_index = ksj::stats::max_index(input);
    const auto public_max_index_ns = ksj::benchmarks::measure(config, [&] {
      public_max_index = ksj::stats::max_index(input);
      ksj::benchmarks::do_not_optimize(public_max_index);
    });
    print_scalar_row("max_index", "public_api", type_name, size, config, public_max_index_ns,
                     static_cast<double>(public_max_index));

    auto eigen_min_index = ksj::stats::detail::eigen::min_index(input_view);
    const auto eigen_min_index_ns = ksj::benchmarks::measure(config, [&] {
      eigen_min_index = ksj::stats::detail::eigen::min_index(input_view);
      ksj::benchmarks::do_not_optimize(eigen_min_index);
    });
    print_scalar_row("min_index", "eigen", type_name, size, config, eigen_min_index_ns,
                     static_cast<double>(eigen_min_index));

    std::size_t intel_min_index = 0U;
    if (ksj::stats::detail::intel::min_index(input_view, intel_min_index)) {
      const auto intel_min_index_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::min_index(input_view, intel_min_index);
        ksj::benchmarks::do_not_optimize(intel_min_index);
      });
      print_scalar_row("min_index", "intel_ipp", type_name, size, config, intel_min_index_ns,
                       static_cast<double>(intel_min_index));
    }

    auto public_min_index = ksj::stats::min_index(input);
    const auto public_min_index_ns = ksj::benchmarks::measure(config, [&] {
      public_min_index = ksj::stats::min_index(input);
      ksj::benchmarks::do_not_optimize(public_min_index);
    });
    print_scalar_row("min_index", "public_api", type_name, size, config, public_min_index_ns,
                     static_cast<double>(public_min_index));

    auto eigen_rmse_diff = ksj::stats::detail::eigen::rmse(input_view);
    const auto eigen_rmse_diff_ns = ksj::benchmarks::measure(config, [&] {
      eigen_rmse_diff = ksj::stats::detail::eigen::rmse(input_view);
      ksj::benchmarks::do_not_optimize(eigen_rmse_diff);
    });
    print_scalar_row("rmse_diff", "eigen", type_name, size, config, eigen_rmse_diff_ns,
                     static_cast<double>(eigen_rmse_diff));

    T intel_rmse_diff{};
    if (ksj::stats::detail::intel::rmse(input_view, intel_rmse_diff)) {
      const auto intel_rmse_diff_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::rmse(input_view, intel_rmse_diff);
        ksj::benchmarks::do_not_optimize(intel_rmse_diff);
      });
      print_scalar_row("rmse_diff", "intel_ipp", type_name, size, config, intel_rmse_diff_ns,
                       static_cast<double>(intel_rmse_diff));
    }

    auto public_rmse_diff = ksj::stats::rmse(input);
    const auto public_rmse_diff_ns = ksj::benchmarks::measure(config, [&] {
      public_rmse_diff = ksj::stats::rmse(input);
      ksj::benchmarks::do_not_optimize(public_rmse_diff);
    });
    print_scalar_row("rmse_diff", "public_api", type_name, size, config, public_rmse_diff_ns,
                     static_cast<double>(public_rmse_diff));

    auto eigen_rmse_pair = ksj::stats::detail::eigen::rmse(input_view, reference_view);
    const auto eigen_rmse_pair_ns = ksj::benchmarks::measure(config, [&] {
      eigen_rmse_pair = ksj::stats::detail::eigen::rmse(input_view, reference_view);
      ksj::benchmarks::do_not_optimize(eigen_rmse_pair);
    });
    print_scalar_row("rmse_pair", "eigen", type_name, size, config, eigen_rmse_pair_ns,
                     static_cast<double>(eigen_rmse_pair));

    T intel_rmse_pair{};
    if (ksj::stats::detail::intel::rmse(input_view, reference_view, intel_rmse_pair)) {
      const auto intel_rmse_pair_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::rmse(input_view, reference_view, intel_rmse_pair);
        ksj::benchmarks::do_not_optimize(intel_rmse_pair);
      });
      print_scalar_row("rmse_pair", "intel_ipp", type_name, size, config, intel_rmse_pair_ns,
                       static_cast<double>(intel_rmse_pair));
    }

    auto public_rmse_pair = ksj::stats::rmse(input.view(), reference.view());
    const auto public_rmse_pair_ns = ksj::benchmarks::measure(config, [&] {
      public_rmse_pair = ksj::stats::rmse(input.view(), reference.view());
      ksj::benchmarks::do_not_optimize(public_rmse_pair);
    });
    print_scalar_row("rmse_pair", "public_api", type_name, size, config, public_rmse_pair_ns,
                     static_cast<double>(public_rmse_pair));

    auto eigen_equal = ksj::stats::detail::eigen::equal(input_view, equal_reference_view, T{});
    const auto eigen_equal_ns = ksj::benchmarks::measure(config, [&] {
      eigen_equal = ksj::stats::detail::eigen::equal(input_view, equal_reference_view, T{});
      ksj::benchmarks::do_not_optimize(eigen_equal);
    });
    print_scalar_row("equal", "eigen", type_name, size, config, eigen_equal_ns, eigen_equal ? 1.0 : 0.0);

    bool intel_equal = false;
    if (ksj::stats::detail::intel::equal(input_view, equal_reference_view, T{}, intel_equal)) {
      const auto intel_equal_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::equal(input_view, equal_reference_view, T{}, intel_equal);
        ksj::benchmarks::do_not_optimize(intel_equal);
      });
      print_scalar_row("equal", "intel_ipp", type_name, size, config, intel_equal_ns, intel_equal ? 1.0 : 0.0);
    }

    auto public_equal = ksj::stats::equal(input.view(), equal_reference.view(), T{});
    const auto public_equal_ns = ksj::benchmarks::measure(config, [&] {
      public_equal = ksj::stats::equal(input.view(), equal_reference.view(), T{});
      ksj::benchmarks::do_not_optimize(public_equal);
    });
    print_scalar_row("equal", "public_api", type_name, size, config, public_equal_ns, public_equal ? 1.0 : 0.0);

    const auto input_cube_view = ksj::array::as_const_view(input.reshape_view(size, 1U, 1U));
    const auto reference_cube_view = ksj::array::as_const_view(reference.reshape_view(size, 1U, 1U));
    auto eigen_squared_l2_distance =
      ksj::stats::detail::eigen::squared_l2_distance(input_cube_view, reference_cube_view);
    const auto eigen_squared_l2_distance_ns = ksj::benchmarks::measure(config, [&] {
      eigen_squared_l2_distance = ksj::stats::detail::eigen::squared_l2_distance(input_cube_view, reference_cube_view);
      ksj::benchmarks::do_not_optimize(eigen_squared_l2_distance);
    });
    print_scalar_row("squared_l2_distance", "eigen", type_name, size, config, eigen_squared_l2_distance_ns,
                     static_cast<double>(eigen_squared_l2_distance));

    T intel_squared_l2_distance{};
    if (ksj::stats::detail::intel::squared_l2_distance(input_cube_view, reference_cube_view,
                                                       intel_squared_l2_distance)) {
      const auto intel_squared_l2_distance_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::squared_l2_distance(input_cube_view, reference_cube_view,
                                                             intel_squared_l2_distance);
        ksj::benchmarks::do_not_optimize(intel_squared_l2_distance);
      });
      print_scalar_row("squared_l2_distance", "intel_ipp", type_name, size, config, intel_squared_l2_distance_ns,
                       static_cast<double>(intel_squared_l2_distance));
    }

    auto public_squared_l2_distance =
      ksj::stats::squared_l2_distance(input.reshape_view(size, 1U, 1U), reference.reshape_view(size, 1U, 1U));
    const auto public_squared_l2_distance_ns = ksj::benchmarks::measure(config, [&] {
      public_squared_l2_distance =
        ksj::stats::squared_l2_distance(input.reshape_view(size, 1U, 1U), reference.reshape_view(size, 1U, 1U));
      ksj::benchmarks::do_not_optimize(public_squared_l2_distance);
    });
    print_scalar_row("squared_l2_distance", "public_api", type_name, size, config, public_squared_l2_distance_ns,
                     static_cast<double>(public_squared_l2_distance));

    run_norm_distance_policy_benchmarks(type_name, size, config, input_view, reference_view);
  }
}

template <typename T> void run_for_complex_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_vector<std::complex<T>>(size);
    auto reference = ksj::array::make_pooled_vector<std::complex<T>>(size);
    auto equal_reference = ksj::array::make_pooled_vector<std::complex<T>>(size);
    ksj::benchmarks::require_pooled_storage("complex_input", input);
    ksj::benchmarks::require_pooled_storage("complex_reference", reference);
    ksj::benchmarks::require_pooled_storage("complex_equal_reference", equal_reference);
    ksj::benchmarks::fill_vector(input);
    const auto input_view = ksj::array::as_const_view(input.view());
    fill_reference_vector(reference, input_view);
    copy_vector(equal_reference, input_view);
    const auto reference_view = ksj::array::as_const_view(reference.view());
    const auto equal_reference_view = ksj::array::as_const_view(equal_reference.view());

    auto eigen_sos = ksj::stats::detail::eigen::sum_of_squares(input_view);
    const auto eigen_sos_ns = ksj::benchmarks::measure(config, [&] {
      eigen_sos = ksj::stats::detail::eigen::sum_of_squares(input_view);
      ksj::benchmarks::do_not_optimize(eigen_sos);
    });
    print_scalar_row("complex_sum_of_squares", "eigen", type_name, size, config, eigen_sos_ns,
                     static_cast<double>(eigen_sos));

    T intel_sos{};
    if (ksj::stats::detail::intel::sum_of_squares(input_view, intel_sos)) {
      const auto intel_sos_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::sum_of_squares(input_view, intel_sos);
        ksj::benchmarks::do_not_optimize(intel_sos);
      });
      print_scalar_row("complex_sum_of_squares", "intel_ipp", type_name, size, config, intel_sos_ns,
                       static_cast<double>(intel_sos));
    }

    auto public_sos = ksj::stats::sum_of_squares(input);
    const auto public_sos_ns = ksj::benchmarks::measure(config, [&] {
      public_sos = ksj::stats::sum_of_squares(input);
      ksj::benchmarks::do_not_optimize(public_sos);
    });
    print_scalar_row("complex_sum_of_squares", "public_api", type_name, size, config, public_sos_ns,
                     static_cast<double>(public_sos));

    auto eigen_rss = ksj::stats::detail::eigen::root_sum_of_squares(input_view);
    const auto eigen_rss_ns = ksj::benchmarks::measure(config, [&] {
      eigen_rss = ksj::stats::detail::eigen::root_sum_of_squares(input_view);
      ksj::benchmarks::do_not_optimize(eigen_rss);
    });
    print_scalar_row("complex_root_sum_of_squares", "eigen", type_name, size, config, eigen_rss_ns,
                     static_cast<double>(eigen_rss));

    T intel_rss{};
    if (ksj::stats::detail::intel::root_sum_of_squares(input_view, intel_rss)) {
      const auto intel_rss_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::root_sum_of_squares(input_view, intel_rss);
        ksj::benchmarks::do_not_optimize(intel_rss);
      });
      print_scalar_row("complex_root_sum_of_squares", "intel_ipp", type_name, size, config, intel_rss_ns,
                       static_cast<double>(intel_rss));
    }

    auto public_rss = ksj::stats::root_sum_of_squares(input);
    const auto public_rss_ns = ksj::benchmarks::measure(config, [&] {
      public_rss = ksj::stats::root_sum_of_squares(input);
      ksj::benchmarks::do_not_optimize(public_rss);
    });
    print_scalar_row("complex_root_sum_of_squares", "public_api", type_name, size, config, public_rss_ns,
                     static_cast<double>(public_rss));

    auto eigen_sum_abs = ksj::stats::detail::eigen::sum_abs(input_view);
    const auto eigen_sum_abs_ns = ksj::benchmarks::measure(config, [&] {
      eigen_sum_abs = ksj::stats::detail::eigen::sum_abs(input_view);
      ksj::benchmarks::do_not_optimize(eigen_sum_abs);
    });
    print_scalar_row("complex_sum_abs", "eigen", type_name, size, config, eigen_sum_abs_ns,
                     static_cast<double>(eigen_sum_abs));

    T intel_sum_abs{};
    if (ksj::stats::detail::intel::sum_abs(input_view, intel_sum_abs)) {
      const auto intel_sum_abs_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::sum_abs(input_view, intel_sum_abs);
        ksj::benchmarks::do_not_optimize(intel_sum_abs);
      });
      print_scalar_row("complex_sum_abs", "intel_ipp", type_name, size, config, intel_sum_abs_ns,
                       static_cast<double>(intel_sum_abs));
    }

    auto public_sum_abs = ksj::stats::sum_abs(input);
    const auto public_sum_abs_ns = ksj::benchmarks::measure(config, [&] {
      public_sum_abs = ksj::stats::sum_abs(input);
      ksj::benchmarks::do_not_optimize(public_sum_abs);
    });
    print_scalar_row("complex_sum_abs", "public_api", type_name, size, config, public_sum_abs_ns,
                     static_cast<double>(public_sum_abs));

    auto eigen_rmse_diff = ksj::stats::detail::eigen::rmse(input_view);
    const auto eigen_rmse_diff_ns = ksj::benchmarks::measure(config, [&] {
      eigen_rmse_diff = ksj::stats::detail::eigen::rmse(input_view);
      ksj::benchmarks::do_not_optimize(eigen_rmse_diff);
    });
    print_scalar_row("complex_rmse_diff", "eigen", type_name, size, config, eigen_rmse_diff_ns,
                     static_cast<double>(eigen_rmse_diff));

    T intel_rmse_diff{};
    if (ksj::stats::detail::intel::rmse(input_view, intel_rmse_diff)) {
      const auto intel_rmse_diff_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::rmse(input_view, intel_rmse_diff);
        ksj::benchmarks::do_not_optimize(intel_rmse_diff);
      });
      print_scalar_row("complex_rmse_diff", "intel_ipp", type_name, size, config, intel_rmse_diff_ns,
                       static_cast<double>(intel_rmse_diff));
    }

    auto public_rmse_diff = ksj::stats::rmse(input);
    const auto public_rmse_diff_ns = ksj::benchmarks::measure(config, [&] {
      public_rmse_diff = ksj::stats::rmse(input);
      ksj::benchmarks::do_not_optimize(public_rmse_diff);
    });
    print_scalar_row("complex_rmse_diff", "public_api", type_name, size, config, public_rmse_diff_ns,
                     static_cast<double>(public_rmse_diff));

    auto eigen_rmse_pair = ksj::stats::detail::eigen::rmse(input_view, reference_view);
    const auto eigen_rmse_pair_ns = ksj::benchmarks::measure(config, [&] {
      eigen_rmse_pair = ksj::stats::detail::eigen::rmse(input_view, reference_view);
      ksj::benchmarks::do_not_optimize(eigen_rmse_pair);
    });
    print_scalar_row("complex_rmse_pair", "eigen", type_name, size, config, eigen_rmse_pair_ns,
                     static_cast<double>(eigen_rmse_pair));

    T intel_rmse_pair{};
    if (ksj::stats::detail::intel::rmse(input_view, reference_view, intel_rmse_pair)) {
      const auto intel_rmse_pair_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::rmse(input_view, reference_view, intel_rmse_pair);
        ksj::benchmarks::do_not_optimize(intel_rmse_pair);
      });
      print_scalar_row("complex_rmse_pair", "intel_ipp", type_name, size, config, intel_rmse_pair_ns,
                       static_cast<double>(intel_rmse_pair));
    }

    auto public_rmse_pair = ksj::stats::rmse(input.view(), reference.view());
    const auto public_rmse_pair_ns = ksj::benchmarks::measure(config, [&] {
      public_rmse_pair = ksj::stats::rmse(input.view(), reference.view());
      ksj::benchmarks::do_not_optimize(public_rmse_pair);
    });
    print_scalar_row("complex_rmse_pair", "public_api", type_name, size, config, public_rmse_pair_ns,
                     static_cast<double>(public_rmse_pair));

    auto eigen_equal = ksj::stats::detail::eigen::equal(input_view, equal_reference_view, T{});
    const auto eigen_equal_ns = ksj::benchmarks::measure(config, [&] {
      eigen_equal = ksj::stats::detail::eigen::equal(input_view, equal_reference_view, T{});
      ksj::benchmarks::do_not_optimize(eigen_equal);
    });
    print_scalar_row("complex_equal", "eigen", type_name, size, config, eigen_equal_ns, eigen_equal ? 1.0 : 0.0);

    bool intel_equal = false;
    if (ksj::stats::detail::intel::equal(input_view, equal_reference_view, T{}, intel_equal)) {
      const auto intel_equal_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::equal(input_view, equal_reference_view, T{}, intel_equal);
        ksj::benchmarks::do_not_optimize(intel_equal);
      });
      print_scalar_row("complex_equal", "intel_ipp", type_name, size, config, intel_equal_ns, intel_equal ? 1.0 : 0.0);
    }

    auto public_equal = ksj::stats::equal(input.view(), equal_reference.view(), T{});
    const auto public_equal_ns = ksj::benchmarks::measure(config, [&] {
      public_equal = ksj::stats::equal(input.view(), equal_reference.view(), T{});
      ksj::benchmarks::do_not_optimize(public_equal);
    });
    print_scalar_row("complex_equal", "public_api", type_name, size, config, public_equal_ns, public_equal ? 1.0 : 0.0);

    const auto input_cube_view = ksj::array::as_const_view(input.reshape_view(size, 1U, 1U));
    const auto reference_cube_view = ksj::array::as_const_view(reference.reshape_view(size, 1U, 1U));
    auto eigen_squared_l2_distance =
      ksj::stats::detail::eigen::squared_l2_distance(input_cube_view, reference_cube_view);
    const auto eigen_squared_l2_distance_ns = ksj::benchmarks::measure(config, [&] {
      eigen_squared_l2_distance = ksj::stats::detail::eigen::squared_l2_distance(input_cube_view, reference_cube_view);
      ksj::benchmarks::do_not_optimize(eigen_squared_l2_distance);
    });
    print_scalar_row("complex_squared_l2_distance", "eigen", type_name, size, config, eigen_squared_l2_distance_ns,
                     static_cast<double>(eigen_squared_l2_distance));

    T intel_squared_l2_distance{};
    if (ksj::stats::detail::intel::squared_l2_distance(input_cube_view, reference_cube_view,
                                                       intel_squared_l2_distance)) {
      const auto intel_squared_l2_distance_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::stats::detail::intel::squared_l2_distance(input_cube_view, reference_cube_view,
                                                             intel_squared_l2_distance);
        ksj::benchmarks::do_not_optimize(intel_squared_l2_distance);
      });
      print_scalar_row("complex_squared_l2_distance", "intel_ipp", type_name, size, config,
                       intel_squared_l2_distance_ns, static_cast<double>(intel_squared_l2_distance));
    }

    auto public_squared_l2_distance =
      ksj::stats::squared_l2_distance(input.reshape_view(size, 1U, 1U), reference.reshape_view(size, 1U, 1U));
    const auto public_squared_l2_distance_ns = ksj::benchmarks::measure(config, [&] {
      public_squared_l2_distance =
        ksj::stats::squared_l2_distance(input.reshape_view(size, 1U, 1U), reference.reshape_view(size, 1U, 1U));
      ksj::benchmarks::do_not_optimize(public_squared_l2_distance);
    });
    print_scalar_row("complex_squared_l2_distance", "public_api", type_name, size, config,
                     public_squared_l2_distance_ns, static_cast<double>(public_squared_l2_distance));

    run_norm_distance_policy_benchmarks(type_name, size, config, input_view, reference_view);

    if (size > kMaxRssImageSize) {
      continue;
    }

    for (const auto coils : {8U, 16U, 32U}) {
      auto cube = ksj::array::make_pooled_cube<std::complex<T>>(size, size, coils);
      auto rss = ksj::array::make_pooled_matrix<T>(size, size);
      auto manual_rss = ksj::array::make_pooled_matrix<T>(size, size);
      ksj::benchmarks::require_pooled_storage("rss_cube", cube);
      ksj::benchmarks::require_pooled_storage("rss", rss);
      ksj::benchmarks::require_pooled_storage("manual_rss", manual_rss);
      fill_complex_cube(cube);

      double rss_checksum = 0.0;
      const auto cube_view = ksj::array::as_const_view(cube.view());
      const auto rss_ns = ksj::benchmarks::measure(config, [&] {
        ksj::stats::detail::eigen::root_sum_of_squares_across(cube_view, rss.view(), ksj::array::Dim::dim2);
        rss_checksum = ksj::benchmarks::checksum(rss);
        ksj::benchmarks::do_not_optimize(rss_checksum);
      });
      const auto case_name = coils == 8U    ? "rss_across_8_slices"
                             : coils == 16U ? "rss_across_16_slices"
                                            : "rss_across_32_slices";
      print_scalar_row(case_name, "eigen", type_name, size, config, rss_ns, rss_checksum);

      double api_rss_checksum = 0.0;
      const auto api_rss_ns = ksj::benchmarks::measure(config, [&] {
        ksj::stats::root_sum_of_squares_across(cube, rss, ksj::array::Dim::dim2);
        api_rss_checksum = ksj::benchmarks::checksum(rss);
        ksj::benchmarks::do_not_optimize(api_rss_checksum);
      });
      print_scalar_row(case_name, "api", type_name, size, config, api_rss_ns, api_rss_checksum);

      double manual_rss_checksum = 0.0;
      const auto manual_rss_ns = ksj::benchmarks::measure(config, [&] {
        for (std::size_t col = 0; col < cube.dim1(); ++col) {
          for (std::size_t row = 0; row < cube.dim0(); ++row) {
            T sum{};
            for (std::size_t slice = 0; slice < cube.dim2(); ++slice) {
              sum += std::norm(cube(row, col, slice));
            }
            manual_rss(row, col) = static_cast<T>(std::sqrt(sum));
          }
        }
        manual_rss_checksum = ksj::benchmarks::checksum(manual_rss);
        ksj::benchmarks::do_not_optimize(manual_rss_checksum);
      });
      print_scalar_row(case_name, "manual_output", type_name, size, config, manual_rss_ns, manual_rss_checksum);
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_stats_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  run_for_complex_type<float>("complex_float", config);
  run_for_complex_type<double>("complex_double", config);
  return 0;
}
