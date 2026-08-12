#include "image_benchmark_common.hpp"

#include "kspacejet/stats/stats.hpp"

#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>

namespace ksj::benchmarks::image_benchmarks {
namespace {

template <typename ReferenceFunction, typename ReferenceChecksum, typename RoutedFunction, typename RoutedChecksum>
void run_routed_case(std::string_view case_name, std::string_view comparison_group, std::string_view reference_backend,
                     std::string_view timing_scope, std::string_view type_name, const std::size_t size,
                     const ksj::benchmarks::Config& config, ReferenceFunction&& reference_function,
                     ReferenceChecksum&& reference_checksum, RoutedFunction&& routed_function,
                     RoutedChecksum&& routed_checksum) {
  const auto reference_ns = ksj::benchmarks::measure(config, reference_function);
  const double reference_value = static_cast<double>(reference_checksum());
  ksj::benchmarks::print_row(case_name, reference_backend, type_name, size, config, reference_ns, reference_value,
                             ksj::benchmarks::reference_row(comparison_group, timing_scope));

  const auto routed_ns = ksj::benchmarks::measure(config, routed_function);
  const double routed_value = static_cast<double>(routed_checksum());
  ksj::benchmarks::print_row(case_name, "image_route", type_name, size, config, routed_ns, routed_value,
                             ksj::benchmarks::candidate_row(comparison_group, timing_scope));
}

template <typename T>
void run_arithmetic_route_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = ksj::array::make_pooled_image<T>(size, size);
    auto rhs = ksj::array::make_pooled_image<T>(size, size);
    auto array_output = ksj::array::make_pooled_image<T>(size, size);
    auto image_output = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::fill_image(lhs);
    ksj::benchmarks::fill_image(rhs);
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
      lhs.data()[index] -= static_cast<T>(0.5);
      rhs.data()[index] += static_cast<T>(1.0);
    }

    const auto lhs_view = ksj::array::as_const_view(lhs.view());
    const auto rhs_view = ksj::array::as_const_view(rhs.view());
    const auto array_checksum = [&] {
      return ksj::benchmarks::checksum(array_output);
    };
    const auto image_checksum = [&] {
      return ksj::benchmarks::checksum(image_output);
    };

    run_routed_case(
      "cwise_add", "image.route.cwise_add", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::add(lhs_view, rhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_add(lhs_view, rhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_subtract", "image.route.cwise_subtract", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::subtract(lhs_view, rhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_subtract(lhs_view, rhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_multiply", "image.route.cwise_multiply", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::multiply(lhs_view, rhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_multiply(lhs_view, rhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_divide", "image.route.cwise_divide", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::divide(lhs_view, rhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_divide(lhs_view, rhs_view, image_output.view());
      },
      image_checksum);

    constexpr T scalar = static_cast<T>(1.25);
    run_routed_case(
      "cwise_add_scalar", "image.route.cwise_add_scalar", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::add_scalar(lhs_view, scalar, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_add_scalar(lhs_view, scalar, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_subtract_scalar", "image.route.cwise_subtract_scalar", "array_policy", "output_reuse", type_name, size,
      config,
      [&] {
        ksj::array::subtract_scalar(lhs_view, scalar, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_subtract_scalar(lhs_view, scalar, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_multiply_scalar", "image.route.cwise_multiply_scalar", "array_policy", "output_reuse", type_name, size,
      config,
      [&] {
        ksj::array::scale(lhs_view, scalar, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_multiply_scalar(lhs_view, scalar, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_divide_scalar", "image.route.cwise_divide_scalar", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::divide_scalar(lhs_view, scalar, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_divide_scalar(lhs_view, scalar, image_output.view());
      },
      image_checksum);

    run_routed_case(
      "cwise_abs", "image.route.cwise_abs", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::absolute(lhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_abs(lhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_square", "image.route.cwise_square", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::square(lhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_square(lhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "cwise_sqrt", "image.route.cwise_sqrt", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::sqrt(rhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::cwise_sqrt(rhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "copy", "image.route.copy", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::copy(lhs_view, array_output.view());
      },
      array_checksum,
      [&] {
        ksj::image::copy(lhs_view, image_output.view());
      },
      image_checksum);
    run_routed_case(
      "fill", "image.route.fill", "array_policy", "output_reuse", type_name, size, config,
      [&] {
        ksj::array::fill(array_output.view(), scalar);
      },
      array_checksum,
      [&] {
        ksj::image::fill(image_output.view(), scalar);
      },
      image_checksum);
  }
}

template <typename T>
void run_measurement_route_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = ksj::array::make_pooled_image<T>(size, size);
    auto rhs = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::fill_image(lhs);
    ksj::benchmarks::fill_image(rhs);
    for (std::size_t index = 0U; index < lhs.size(); ++index) {
      lhs.data()[index] -= static_cast<T>(0.5);
      rhs.data()[index] = rhs.data()[index] * static_cast<T>(0.75) + static_cast<T>(0.125);
    }

    const auto lhs_view = ksj::array::as_const_view(lhs.view());
    const auto rhs_view = ksj::array::as_const_view(rhs.view());
    const auto lhs_vector = ksj::array::VectorView<const T>(lhs.data(), lhs.size());
    const auto rhs_vector = ksj::array::VectorView<const T>(rhs.data(), rhs.size());

    T reference_scalar{};
    double image_scalar = 0.0;
    run_routed_case(
      "mean", "image.route.mean", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::mean(lhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::mean(lhs_view);
      },
      [&] {
        return image_scalar;
      });

    T reference_mean{};
    T reference_variance{};
    ksj::image::MeanStdDev image_mean_stddev{};
    run_routed_case(
      "mean_stddev", "image.route.mean_stddev", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_mean = ksj::stats::mean(lhs_vector);
        reference_variance = ksj::stats::variance(lhs_vector, ksj::stats::VarianceNormalization::population);
      },
      [&] {
        return static_cast<double>(reference_mean) + std::sqrt(static_cast<double>(reference_variance));
      },
      [&] {
        image_mean_stddev = ksj::image::mean_stddev(lhs_view);
      },
      [&] {
        return image_mean_stddev.mean + image_mean_stddev.stddev;
      });

    std::pair<T, T> reference_minmax{};
    std::pair<T, T> image_minmax{};
    run_routed_case(
      "minmax", "image.route.minmax", "array_policy", "steady_state", type_name, size, config,
      [&] {
        reference_minmax = ksj::array::minmax(lhs_view);
      },
      [&] {
        return static_cast<double>(reference_minmax.first) + static_cast<double>(reference_minmax.second);
      },
      [&] {
        image_minmax = ksj::image::minmax(lhs_view);
      },
      [&] {
        return static_cast<double>(image_minmax.first) + static_cast<double>(image_minmax.second);
      });

    run_routed_case(
      "norm_l1", "image.route.norm_l1", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::sum_abs(lhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::norm_l1(lhs_view);
      },
      [&] {
        return image_scalar;
      });
    run_routed_case(
      "norm_l2", "image.route.norm_l2", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::root_sum_of_squares(lhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::norm_l2(lhs_view);
      },
      [&] {
        return image_scalar;
      });
    run_routed_case(
      "norm_inf", "image.route.norm_inf", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::max_abs(lhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::norm_inf(lhs_view);
      },
      [&] {
        return image_scalar;
      });
    run_routed_case(
      "norm_diff_l1", "image.route.norm_diff_l1", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::l1_distance(lhs_vector, rhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::norm_diff_l1(lhs_view, rhs_view);
      },
      [&] {
        return image_scalar;
      });
    run_routed_case(
      "norm_diff_l2", "image.route.norm_diff_l2", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::l2_distance(lhs_vector, rhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::norm_diff_l2(lhs_view, rhs_view);
      },
      [&] {
        return image_scalar;
      });
    run_routed_case(
      "norm_diff_inf", "image.route.norm_diff_inf", "stats_policy", "steady_state", type_name, size, config,
      [&] {
        reference_scalar = ksj::stats::linf_distance(lhs_vector, rhs_vector);
      },
      [&] {
        return reference_scalar;
      },
      [&] {
        image_scalar = ksj::image::norm_diff_inf(lhs_view, rhs_view);
      },
      [&] {
        return image_scalar;
      });
  }
}

} // namespace

void run_primitive_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_arithmetic_route_benchmarks<float>("float", config);
  run_measurement_route_benchmarks<float>("float", config);
}

void run_primitive_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_arithmetic_route_benchmarks<double>("double", config);
  run_measurement_route_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::image_benchmarks
