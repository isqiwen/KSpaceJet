#include "image_benchmark_common.hpp"

namespace ksj::benchmarks::image_benchmarks {
namespace {

template <typename T> void run_basic_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::require_pooled_storage("input", input);
    ksj::benchmarks::fill_image(input);

    double eigen_threshold_checksum = 0.0;
    const auto eigen_threshold_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::threshold(input, static_cast<T>(0.5), T{}, static_cast<T>(1));
      eigen_threshold_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_threshold_checksum);
    });
    print_image_benchmark_row("threshold", "eigen", type_name, size, config, eigen_threshold_ns,
                              eigen_threshold_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double intel_threshold_checksum = 0.0;
      const auto intel_threshold_ns = ksj::benchmarks::measure(config, [&] {
        auto output = ksj::array::make_pooled_image<T>(size, size);
        if (!ksj::image::detail::intel::threshold(ksj::array::as_const_view(input.view()), output.view(),
                                                  static_cast<T>(0.5), T{}, static_cast<T>(1))) {
          throw std::runtime_error("Intel IPP threshold backend failed");
        }
        intel_threshold_checksum = ksj::benchmarks::checksum(output);
        ksj::benchmarks::do_not_optimize(intel_threshold_checksum);
      });
      print_image_benchmark_row("threshold", "intel_ipp", type_name, size, config, intel_threshold_ns,
                                intel_threshold_checksum);
    }

    double opencv_threshold_checksum = 0.0;
    const auto opencv_threshold_ns = ksj::benchmarks::measure(config, [&] {
      auto output = ksj::array::make_pooled_image<T>(size, size);
      if (!ksj::image::detail::opencv::threshold(ksj::array::as_const_view(input.view()), output.view(),
                                                 static_cast<T>(0.5), T{}, static_cast<T>(1))) {
        throw std::runtime_error("OpenCV threshold backend failed");
      }
      opencv_threshold_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(opencv_threshold_checksum);
    });
    print_image_benchmark_row("threshold", "opencv", type_name, size, config, opencv_threshold_ns,
                              opencv_threshold_checksum);

    double public_threshold_checksum = 0.0;
    const auto public_threshold_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::threshold(input, static_cast<T>(0.5), T{}, static_cast<T>(1));
      public_threshold_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_threshold_checksum);
    });
    print_image_benchmark_row("threshold", "public_policy", type_name, size, config, public_threshold_ns,
                              public_threshold_checksum);

    double eigen_normalize_checksum = 0.0;
    const auto eigen_normalize_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::normalize_minmax(input);
      eigen_normalize_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_normalize_checksum);
    });
    print_image_benchmark_row("normalize_minmax", "eigen", type_name, size, config, eigen_normalize_ns,
                              eigen_normalize_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double intel_normalize_checksum = 0.0;
      const auto intel_normalize_ns = ksj::benchmarks::measure(config, [&] {
        auto output = ksj::array::make_pooled_image<T>(size, size);
        if (!ksj::image::detail::intel::normalize_minmax(ksj::array::as_const_view(input.view()), output.view())) {
          throw std::runtime_error("Intel IPP normalize_minmax backend failed");
        }
        intel_normalize_checksum = ksj::benchmarks::checksum(output);
        ksj::benchmarks::do_not_optimize(intel_normalize_checksum);
      });
      print_image_benchmark_row("normalize_minmax", "intel_ipp", type_name, size, config, intel_normalize_ns,
                                intel_normalize_checksum);
    }

    double opencv_normalize_checksum = 0.0;
    const auto opencv_normalize_ns = ksj::benchmarks::measure(config, [&] {
      auto output = ksj::array::make_pooled_image<T>(size, size);
      if (!ksj::image::detail::opencv::normalize_minmax(ksj::array::as_const_view(input.view()), output.view())) {
        throw std::runtime_error("OpenCV normalize_minmax backend failed");
      }
      opencv_normalize_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(opencv_normalize_checksum);
    });
    print_image_benchmark_row("normalize_minmax", "opencv", type_name, size, config, opencv_normalize_ns,
                              opencv_normalize_checksum);

    double public_normalize_checksum = 0.0;
    const auto public_normalize_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::normalize_minmax(input);
      public_normalize_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_normalize_checksum);
    });
    print_image_benchmark_row("normalize_minmax", "public_policy", type_name, size, config, public_normalize_ns,
                              public_normalize_checksum);

    double eigen_pad_checksum = 0.0;
    const auto eigen_pad_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::pad(input, 4, 4, 4, 4, ksj::image::BorderMode::replicate, T{});
      eigen_pad_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_pad_checksum);
    });
    print_image_benchmark_row("pad_replicate", "eigen", type_name, size, config, eigen_pad_ns, eigen_pad_checksum);

    const auto resized_size = size > 1U ? size / 2U : 1U;

    auto crop_output = ksj::array::make_pooled_image<T>(resized_size, resized_size);
    double eigen_crop_checksum = 0.0;
    const auto eigen_crop_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::center_crop(input, resized_size, resized_size);
      eigen_crop_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_crop_checksum);
    });
    print_image_benchmark_row("crop_center_half", "eigen", type_name, size, config, eigen_crop_ns, eigen_crop_checksum);

    double eigen_output_crop_checksum = 0.0;
    const auto eigen_output_crop_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::center_crop(input, crop_output);
      eigen_output_crop_checksum = ksj::benchmarks::checksum(crop_output);
      ksj::benchmarks::do_not_optimize(eigen_output_crop_checksum);
    });
    print_image_benchmark_row("crop_center_half", "eigen_output", type_name, size, config, eigen_output_crop_ns,
                              eigen_output_crop_checksum);

    double public_crop_checksum = 0.0;
    const auto public_crop_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::center_crop(input, resized_size, resized_size);
      public_crop_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_crop_checksum);
    });
    print_image_benchmark_row("crop_center_half", "public_api", type_name, size, config, public_crop_ns,
                              public_crop_checksum);

    double crop_checksum = 0.0;
    const auto crop_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::center_crop(input, crop_output);
      crop_checksum = ksj::benchmarks::checksum(crop_output);
      ksj::benchmarks::do_not_optimize(crop_checksum);
    });
    print_image_benchmark_row("crop_center_half", "api", type_name, size, config, crop_ns, crop_checksum);

    auto component_mask = ksj::array::make_pooled_image<T>(size, size);
    fill_component_mask(component_mask);
    auto component_labels = ksj::array::make_pooled_image<ksj::image::ConnectedComponentLabel>(size, size);
    std::vector<ksj::image::ConnectedComponentStats> component_stats;

    double eigen_components_checksum = 0.0;
    const auto eigen_components_ns = ksj::benchmarks::measure(config, [&] {
      const auto component_count = ksj::image::detail::eigen::connected_components(
        ksj::array::as_const_view(component_mask.view()), component_labels.view(), &component_stats,
        ksj::image::Connectivity::eight);
      eigen_components_checksum = ksj::benchmarks::checksum(component_labels) + static_cast<double>(component_count);
      ksj::benchmarks::do_not_optimize(eigen_components_checksum);
    });
    print_image_benchmark_row("connected_components_8", "eigen_output", type_name, size, config, eigen_components_ns,
                              eigen_components_checksum);

    double opencv_components_checksum = 0.0;
    const auto opencv_components_ns = ksj::benchmarks::measure(config, [&] {
      std::size_t component_count = 0U;
      if (!ksj::image::detail::opencv::connected_components(component_mask, component_labels, &component_stats,
                                                            ksj::image::Connectivity::eight, component_count)) {
        throw std::runtime_error("OpenCV connected_components backend failed");
      }
      opencv_components_checksum = ksj::benchmarks::checksum(component_labels) + static_cast<double>(component_count);
      ksj::benchmarks::do_not_optimize(opencv_components_checksum);
    });
    print_image_benchmark_row("connected_components_8", "opencv", type_name, size, config, opencv_components_ns,
                              opencv_components_checksum);

    double public_components_checksum = 0.0;
    const auto public_components_ns = ksj::benchmarks::measure(config, [&] {
      const auto result = ksj::image::connected_components(component_mask, ksj::image::Connectivity::eight);
      public_components_checksum = ksj::benchmarks::checksum(result.labels) + static_cast<double>(result.stats.size());
      ksj::benchmarks::do_not_optimize(public_components_checksum);
    });
    print_image_benchmark_row("connected_components_8", "public_api", type_name, size, config, public_components_ns,
                              public_components_checksum);

    double components_checksum = 0.0;
    const auto components_ns = ksj::benchmarks::measure(config, [&] {
      const auto component_count = ksj::image::connected_components(component_mask, component_labels, &component_stats,
                                                                    ksj::image::Connectivity::eight);
      components_checksum = ksj::benchmarks::checksum(component_labels) + static_cast<double>(component_count);
      ksj::benchmarks::do_not_optimize(components_checksum);
    });
    print_image_benchmark_row("connected_components_8", "api", type_name, size, config, components_ns,
                              components_checksum);

    auto region_input = ksj::array::make_pooled_image<T>(size, size);
    auto region_mask = ksj::array::make_pooled_image<ksj::image::RegionGrowMaskValue>(size, size);
    ksj::benchmarks::require_pooled_storage("region_input", region_input);
    ksj::benchmarks::require_pooled_storage("region_mask", region_mask);
    fill_region_grow_input(region_input);

    double region_output_checksum = 0.0;
    const auto region_output_ns = ksj::benchmarks::measure(config, [&] {
      const auto area = ksj::image::region_grow(region_input, region_mask, size / 2U, size / 2U, static_cast<T>(0.5),
                                                static_cast<T>(1.0), ksj::image::Connectivity::eight);
      region_output_checksum = checksum_mask(region_mask);
      ksj::benchmarks::do_not_optimize(area);
      ksj::benchmarks::do_not_optimize(region_output_checksum);
    });
    print_image_benchmark_row("region_grow_8", "api", type_name, size, config, region_output_ns,
                              region_output_checksum);

    double region_public_checksum = 0.0;
    const auto region_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto mask = ksj::image::region_grow(region_input, size / 2U, size / 2U, static_cast<T>(0.5),
                                                static_cast<T>(1.0), ksj::image::Connectivity::eight);
      region_public_checksum = checksum_mask(mask);
      ksj::benchmarks::do_not_optimize(region_public_checksum);
    });
    print_image_benchmark_row("region_grow_8", "public_api", type_name, size, config, region_public_ns,
                              region_public_checksum);
  }
}

} // namespace

void run_basic_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_basic_benchmarks<float>("float", config);
}

void run_basic_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_basic_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::image_benchmarks
