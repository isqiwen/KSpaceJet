#include "image_benchmark_common.hpp"

namespace ksj::benchmarks::image_benchmarks {
namespace {

template <typename T> void run_resize_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::require_pooled_storage("input", input);
    ksj::benchmarks::fill_image(input);

    const auto resized_size = size > 1U ? size / 2U : 1U;

    auto resize_output = ksj::array::make_pooled_image<T>(resized_size, resized_size);
    double eigen_nearest_checksum = 0.0;
    const auto eigen_nearest_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::resize_nearest(input, resized_size, resized_size);
      eigen_nearest_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_nearest_checksum);
    });
    print_image_benchmark_row("resize_nearest_half", "eigen", type_name, size, config, eigen_nearest_ns,
                              eigen_nearest_checksum);

    double eigen_output_nearest_checksum = 0.0;
    const auto eigen_output_nearest_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::resize_nearest(input, resize_output);
      eigen_output_nearest_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(eigen_output_nearest_checksum);
    });
    print_image_benchmark_row("resize_nearest_half", "eigen_output", type_name, size, config, eigen_output_nearest_ns,
                              eigen_output_nearest_checksum);

    double opencv_nearest_checksum = 0.0;
    const auto opencv_nearest_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::resize_nearest(input, resize_output)) {
        throw std::runtime_error("OpenCV resize_nearest backend failed");
      }
      opencv_nearest_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(opencv_nearest_checksum);
    });
    print_image_benchmark_row("resize_nearest_half", "opencv", type_name, size, config, opencv_nearest_ns,
                              opencv_nearest_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double intel_nearest_checksum = 0.0;
      const auto intel_nearest_ns = ksj::benchmarks::measure(config, [&] {
        if (!ksj::image::detail::intel::resize_nearest(input, resize_output)) {
          throw std::runtime_error("Intel resize_nearest backend failed");
        }
        intel_nearest_checksum = ksj::benchmarks::checksum(resize_output);
        ksj::benchmarks::do_not_optimize(intel_nearest_checksum);
      });
      print_image_benchmark_row("resize_nearest_half", "intel", type_name, size, config, intel_nearest_ns,
                                intel_nearest_checksum);
    }

    double public_nearest_checksum = 0.0;
    const auto public_nearest_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::resize_nearest(input, resized_size, resized_size);
      public_nearest_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_nearest_checksum);
    });
    print_image_benchmark_row("resize_nearest_half", "public_api", type_name, size, config, public_nearest_ns,
                              public_nearest_checksum);

    double nearest_checksum = 0.0;
    const auto nearest_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::resize_nearest(input, resize_output);
      nearest_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(nearest_checksum);
    });
    print_image_benchmark_row("resize_nearest_half", "api", type_name, size, config, nearest_ns, nearest_checksum);

    double eigen_resize_checksum = 0.0;
    const auto eigen_resize_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::resize_linear(input, resized_size, resized_size);
      eigen_resize_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_resize_checksum);
    });
    print_image_benchmark_row("resize_linear_half", "eigen", type_name, size, config, eigen_resize_ns,
                              eigen_resize_checksum);

    double eigen_output_resize_checksum = 0.0;
    const auto eigen_output_resize_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::resize_linear(input, resize_output);
      eigen_output_resize_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(eigen_output_resize_checksum);
    });
    print_image_benchmark_row("resize_linear_half", "eigen_output", type_name, size, config, eigen_output_resize_ns,
                              eigen_output_resize_checksum);

    double opencv_resize_checksum = 0.0;
    const auto opencv_resize_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::resize_linear(input, resize_output)) {
        throw std::runtime_error("OpenCV resize_linear backend failed");
      }
      opencv_resize_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(opencv_resize_checksum);
    });
    print_image_benchmark_row("resize_linear_half", "opencv", type_name, size, config, opencv_resize_ns,
                              opencv_resize_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double intel_resize_checksum = 0.0;
      const auto intel_resize_ns = ksj::benchmarks::measure(config, [&] {
        if (!ksj::image::detail::intel::resize_linear(input, resize_output)) {
          throw std::runtime_error("Intel resize_linear backend failed");
        }
        intel_resize_checksum = ksj::benchmarks::checksum(resize_output);
        ksj::benchmarks::do_not_optimize(intel_resize_checksum);
      });
      print_image_benchmark_row("resize_linear_half", "intel", type_name, size, config, intel_resize_ns,
                                intel_resize_checksum);
    }

    double public_resize_checksum = 0.0;
    const auto public_resize_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::resize_linear(input, resized_size, resized_size);
      public_resize_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_resize_checksum);
    });
    print_image_benchmark_row("resize_linear_half", "public_api", type_name, size, config, public_resize_ns,
                              public_resize_checksum);

    double resize_checksum = 0.0;
    const auto resize_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::resize_linear(input, resize_output);
      resize_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(resize_checksum);
    });
    print_image_benchmark_row("resize_linear_half", "api", type_name, size, config, resize_ns, resize_checksum);

    double eigen_cubic_checksum = 0.0;
    const auto eigen_cubic_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::resize_cubic(input, resized_size, resized_size);
      eigen_cubic_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_cubic_checksum);
    });
    print_image_benchmark_row("resize_cubic_half", "eigen", type_name, size, config, eigen_cubic_ns,
                              eigen_cubic_checksum);

    double eigen_output_cubic_checksum = 0.0;
    const auto eigen_output_cubic_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::resize_cubic(input, resize_output);
      eigen_output_cubic_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(eigen_output_cubic_checksum);
    });
    print_image_benchmark_row("resize_cubic_half", "eigen_output", type_name, size, config, eigen_output_cubic_ns,
                              eigen_output_cubic_checksum);

    double opencv_cubic_checksum = 0.0;
    const auto opencv_cubic_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::resize_cubic(input, resize_output)) {
        throw std::runtime_error("OpenCV resize_cubic backend failed");
      }
      opencv_cubic_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(opencv_cubic_checksum);
    });
    print_image_benchmark_row("resize_cubic_half", "opencv", type_name, size, config, opencv_cubic_ns,
                              opencv_cubic_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double intel_cubic_checksum = 0.0;
      const auto intel_cubic_ns = ksj::benchmarks::measure(config, [&] {
        if (!ksj::image::detail::intel::resize_cubic(input, resize_output)) {
          throw std::runtime_error("Intel resize_cubic backend failed");
        }
        intel_cubic_checksum = ksj::benchmarks::checksum(resize_output);
        ksj::benchmarks::do_not_optimize(intel_cubic_checksum);
      });
      print_image_benchmark_row("resize_cubic_half", "intel", type_name, size, config, intel_cubic_ns,
                                intel_cubic_checksum);
    }

    double public_cubic_checksum = 0.0;
    const auto public_cubic_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::resize_cubic(input, resized_size, resized_size);
      public_cubic_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_cubic_checksum);
    });
    print_image_benchmark_row("resize_cubic_half", "public_api", type_name, size, config, public_cubic_ns,
                              public_cubic_checksum);

    double cubic_checksum = 0.0;
    const auto cubic_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::resize_cubic(input, resize_output);
      cubic_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(cubic_checksum);
    });
    print_image_benchmark_row("resize_cubic_half", "api", type_name, size, config, cubic_ns, cubic_checksum);

    double eigen_lanczos_checksum = 0.0;
    const auto eigen_lanczos_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::resize_lanczos4(input, resized_size, resized_size);
      eigen_lanczos_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_lanczos_checksum);
    });
    print_image_benchmark_row("resize_lanczos4_half", "eigen", type_name, size, config, eigen_lanczos_ns,
                              eigen_lanczos_checksum);

    double eigen_output_lanczos_checksum = 0.0;
    const auto eigen_output_lanczos_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::resize_lanczos4(input, resize_output);
      eigen_output_lanczos_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(eigen_output_lanczos_checksum);
    });
    print_image_benchmark_row("resize_lanczos4_half", "eigen_output", type_name, size, config, eigen_output_lanczos_ns,
                              eigen_output_lanczos_checksum);

    double opencv_lanczos_checksum = 0.0;
    const auto opencv_lanczos_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::resize_lanczos4(input, resize_output)) {
        throw std::runtime_error("OpenCV resize_lanczos4 backend failed");
      }
      opencv_lanczos_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(opencv_lanczos_checksum);
    });
    print_image_benchmark_row("resize_lanczos4_half", "opencv", type_name, size, config, opencv_lanczos_ns,
                              opencv_lanczos_checksum);

    double public_lanczos_checksum = 0.0;
    const auto public_lanczos_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::resize_lanczos4(input, resized_size, resized_size);
      public_lanczos_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_lanczos_checksum);
    });
    print_image_benchmark_row("resize_lanczos4_half", "public_api", type_name, size, config, public_lanczos_ns,
                              public_lanczos_checksum);

    double lanczos_checksum = 0.0;
    const auto lanczos_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::resize_lanczos4(input, resize_output);
      lanczos_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(lanczos_checksum);
    });
    print_image_benchmark_row("resize_lanczos4_half", "api", type_name, size, config, lanczos_ns, lanczos_checksum);

    double eigen_area_checksum = 0.0;
    const auto eigen_area_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::resize_area(input, resized_size, resized_size);
      eigen_area_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_area_checksum);
    });
    print_image_benchmark_row("resize_area_half", "eigen", type_name, size, config, eigen_area_ns, eigen_area_checksum);

    double eigen_output_area_checksum = 0.0;
    const auto eigen_output_area_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::resize_area(input, resize_output);
      eigen_output_area_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(eigen_output_area_checksum);
    });
    print_image_benchmark_row("resize_area_half", "eigen_output", type_name, size, config, eigen_output_area_ns,
                              eigen_output_area_checksum);

    double opencv_area_checksum = 0.0;
    const auto opencv_area_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::resize_area(input, resize_output)) {
        throw std::runtime_error("OpenCV resize_area backend failed");
      }
      opencv_area_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(opencv_area_checksum);
    });
    print_image_benchmark_row("resize_area_half", "opencv", type_name, size, config, opencv_area_ns,
                              opencv_area_checksum);

    double public_area_checksum = 0.0;
    const auto public_area_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::resize_area(input, resized_size, resized_size);
      public_area_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_area_checksum);
    });
    print_image_benchmark_row("resize_area_half", "public_api", type_name, size, config, public_area_ns,
                              public_area_checksum);

    double area_checksum = 0.0;
    const auto area_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::resize_area(input, resize_output);
      area_checksum = ksj::benchmarks::checksum(resize_output);
      ksj::benchmarks::do_not_optimize(area_checksum);
    });
    print_image_benchmark_row("resize_area_half", "api", type_name, size, config, area_ns, area_checksum);
  }
}

} // namespace

void run_resize_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_resize_benchmarks<float>("float", config);
}

void run_resize_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_resize_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::image_benchmarks
