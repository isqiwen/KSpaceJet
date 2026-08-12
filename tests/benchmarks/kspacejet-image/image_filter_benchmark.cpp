#include "image_benchmark_common.hpp"

namespace ksj::benchmarks::image_benchmarks {
namespace {

template <typename T> void run_filter_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::require_pooled_storage("input", input);
    ksj::benchmarks::fill_image(input);

    if (size > kMaxNeighborhoodSize) {
      continue;
    }

    auto neighborhood_output = ksj::array::make_pooled_image<T>(size, size);
    double eigen_box_checksum = 0.0;
    const auto eigen_box_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::box_filter(input, 5, 5, ksj::image::BorderMode::replicate);
      eigen_box_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_box_checksum);
    });
    print_image_benchmark_row("box_filter_5x5", "eigen", type_name, size, config, eigen_box_ns, eigen_box_checksum);

    double eigen_output_box_checksum = 0.0;
    const auto eigen_output_box_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::box_filter(input, neighborhood_output, 5, 5, ksj::image::BorderMode::replicate);
      eigen_output_box_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_box_checksum);
    });
    print_image_benchmark_row("box_filter_5x5", "eigen_output", type_name, size, config, eigen_output_box_ns,
                              eigen_output_box_checksum);

    double opencv_box_checksum = 0.0;
    const auto opencv_box_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::box_filter(ksj::array::as_const_view(input.view()), neighborhood_output.view(),
                                                  5, 5, ksj::image::BorderMode::replicate)) {
        throw std::runtime_error("OpenCV box_filter backend failed");
      }
      opencv_box_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(opencv_box_checksum);
    });
    print_image_benchmark_row("box_filter_5x5", "opencv", type_name, size, config, opencv_box_ns, opencv_box_checksum);

    double public_box_checksum = 0.0;
    const auto public_box_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::box_filter(input, 5, ksj::image::BorderMode::replicate);
      public_box_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_box_checksum);
    });
    print_image_benchmark_row("box_filter_5x5", "public_api", type_name, size, config, public_box_ns,
                              public_box_checksum);

    double box_checksum = 0.0;
    const auto box_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::box_filter(input, neighborhood_output, 5, ksj::image::BorderMode::replicate);
      box_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(box_checksum);
    });
    print_image_benchmark_row("box_filter_5x5", "api", type_name, size, config, box_ns, box_checksum);

    double eigen_gaussian_checksum = 0.0;
    const auto eigen_gaussian_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::gaussian_blur(input, 5, 1.2, ksj::image::BorderMode::replicate);
      eigen_gaussian_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_gaussian_checksum);
    });
    print_image_benchmark_row("gaussian_blur_5x5", "eigen", type_name, size, config, eigen_gaussian_ns,
                              eigen_gaussian_checksum);

    double eigen_output_gaussian_checksum = 0.0;
    const auto eigen_output_gaussian_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::gaussian_blur(input, neighborhood_output, 5, 1.2, ksj::image::BorderMode::replicate);
      eigen_output_gaussian_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_gaussian_checksum);
    });
    print_image_benchmark_row("gaussian_blur_5x5", "eigen_output", type_name, size, config, eigen_output_gaussian_ns,
                              eigen_output_gaussian_checksum);

    double opencv_gaussian_checksum = 0.0;
    const auto opencv_gaussian_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::gaussian_blur(ksj::array::as_const_view(input.view()),
                                                     neighborhood_output.view(), 5, 1.2,
                                                     ksj::image::BorderMode::replicate)) {
        throw std::runtime_error("OpenCV gaussian_blur backend failed");
      }
      opencv_gaussian_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(opencv_gaussian_checksum);
    });
    print_image_benchmark_row("gaussian_blur_5x5", "opencv", type_name, size, config, opencv_gaussian_ns,
                              opencv_gaussian_checksum);

    double public_gaussian_checksum = 0.0;
    const auto public_gaussian_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::gaussian_blur(input, 5, 1.2, ksj::image::BorderMode::replicate);
      public_gaussian_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_gaussian_checksum);
    });
    print_image_benchmark_row("gaussian_blur_5x5", "public_api", type_name, size, config, public_gaussian_ns,
                              public_gaussian_checksum);

    double gaussian_checksum = 0.0;
    const auto gaussian_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::gaussian_blur(input, neighborhood_output, 5, 1.2, ksj::image::BorderMode::replicate);
      gaussian_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(gaussian_checksum);
    });
    print_image_benchmark_row("gaussian_blur_5x5", "api", type_name, size, config, gaussian_ns, gaussian_checksum);

    double eigen_bilateral_checksum = 0.0;
    const auto eigen_bilateral_ns = ksj::benchmarks::measure(config, [&] {
      const auto output =
        ksj::image::detail::eigen::bilateral_filter(input, 5, 0.25, 1.5, ksj::image::BorderMode::replicate);
      eigen_bilateral_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_bilateral_checksum);
    });
    print_image_benchmark_row("bilateral_filter_5x5", "eigen", type_name, size, config, eigen_bilateral_ns,
                              eigen_bilateral_checksum);

    double eigen_output_bilateral_checksum = 0.0;
    const auto eigen_output_bilateral_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::bilateral_filter(input, neighborhood_output, 5, 0.25, 1.5,
                                                  ksj::image::BorderMode::replicate);
      eigen_output_bilateral_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_bilateral_checksum);
    });
    print_image_benchmark_row("bilateral_filter_5x5", "eigen_output", type_name, size, config,
                              eigen_output_bilateral_ns, eigen_output_bilateral_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double opencv_bilateral_checksum = 0.0;
      const auto opencv_bilateral_ns = ksj::benchmarks::measure(config, [&] {
        if (!ksj::image::detail::opencv::bilateral_filter(ksj::array::as_const_view(input.view()),
                                                          neighborhood_output.view(), 5, 0.25, 1.5,
                                                          ksj::image::BorderMode::replicate)) {
          throw std::runtime_error("OpenCV bilateral_filter backend failed");
        }
        opencv_bilateral_checksum = ksj::benchmarks::checksum(neighborhood_output);
        ksj::benchmarks::do_not_optimize(opencv_bilateral_checksum);
      });
      print_image_benchmark_row("bilateral_filter_5x5", "opencv", type_name, size, config, opencv_bilateral_ns,
                                opencv_bilateral_checksum);
    }

    double public_bilateral_checksum = 0.0;
    const auto public_bilateral_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::bilateral_filter(input, 5, 0.25, 1.5, ksj::image::BorderMode::replicate);
      public_bilateral_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_bilateral_checksum);
    });
    print_image_benchmark_row("bilateral_filter_5x5", "public_api", type_name, size, config, public_bilateral_ns,
                              public_bilateral_checksum);

    double bilateral_checksum = 0.0;
    const auto bilateral_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::bilateral_filter(input, neighborhood_output, 5, 0.25, 1.5, ksj::image::BorderMode::replicate);
      bilateral_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(bilateral_checksum);
    });
    print_image_benchmark_row("bilateral_filter_5x5", "api", type_name, size, config, bilateral_ns, bilateral_checksum);

    double eigen_median_checksum = 0.0;
    const auto eigen_median_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::median_filter(input, 3, ksj::image::BorderMode::replicate);
      eigen_median_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_median_checksum);
    });
    print_image_benchmark_row("median_filter_3x3", "eigen", type_name, size, config, eigen_median_ns,
                              eigen_median_checksum);

    double eigen_output_median_checksum = 0.0;
    const auto eigen_output_median_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::median_filter(input, neighborhood_output, 3, ksj::image::BorderMode::replicate);
      eigen_output_median_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_median_checksum);
    });
    print_image_benchmark_row("median_filter_3x3", "eigen_output", type_name, size, config, eigen_output_median_ns,
                              eigen_output_median_checksum);

    if constexpr (std::is_same_v<T, float>) {
      double intel_median_checksum = 0.0;
      const auto intel_median_ns = ksj::benchmarks::measure(config, [&] {
        if (!ksj::image::detail::intel::median_filter(ksj::array::as_const_view(input.view()),
                                                      neighborhood_output.view(), 3,
                                                      ksj::image::BorderMode::replicate)) {
          throw std::runtime_error("Intel median_filter backend failed");
        }
        intel_median_checksum = ksj::benchmarks::checksum(neighborhood_output);
        ksj::benchmarks::do_not_optimize(intel_median_checksum);
      });
      print_image_benchmark_row("median_filter_3x3", "intel_ipp", type_name, size, config, intel_median_ns,
                                intel_median_checksum);

      double opencv_median_checksum = 0.0;
      const auto opencv_median_ns = ksj::benchmarks::measure(config, [&] {
        if (!ksj::image::detail::opencv::median_filter(ksj::array::as_const_view(input.view()),
                                                       neighborhood_output.view(), 3,
                                                       ksj::image::BorderMode::replicate)) {
          throw std::runtime_error("OpenCV median_filter backend failed");
        }
        opencv_median_checksum = ksj::benchmarks::checksum(neighborhood_output);
        ksj::benchmarks::do_not_optimize(opencv_median_checksum);
      });
      print_image_benchmark_row("median_filter_3x3", "opencv", type_name, size, config, opencv_median_ns,
                                opencv_median_checksum);
    }

    double public_median_checksum = 0.0;
    const auto public_median_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::median_filter(input, 3, ksj::image::BorderMode::replicate);
      public_median_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_median_checksum);
    });
    print_image_benchmark_row("median_filter_3x3", "public_api", type_name, size, config, public_median_ns,
                              public_median_checksum);

    double median_checksum = 0.0;
    const auto median_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::median_filter(input, neighborhood_output, 3, ksj::image::BorderMode::replicate);
      median_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(median_checksum);
    });
    print_image_benchmark_row("median_filter_3x3", "api", type_name, size, config, median_ns, median_checksum);

    double eigen_sobel_x_checksum = 0.0;
    const auto eigen_sobel_x_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::sobel_x(input, ksj::image::BorderMode::replicate);
      eigen_sobel_x_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_sobel_x_checksum);
    });
    print_image_benchmark_row("sobel_x_3x3", "eigen", type_name, size, config, eigen_sobel_x_ns,
                              eigen_sobel_x_checksum);

    double eigen_output_sobel_x_checksum = 0.0;
    const auto eigen_output_sobel_x_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::sobel_x(input, neighborhood_output, ksj::image::BorderMode::replicate);
      eigen_output_sobel_x_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_sobel_x_checksum);
    });
    print_image_benchmark_row("sobel_x_3x3", "eigen_output", type_name, size, config, eigen_output_sobel_x_ns,
                              eigen_output_sobel_x_checksum);

    double opencv_sobel_x_checksum = 0.0;
    const auto opencv_sobel_x_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::sobel_x(ksj::array::as_const_view(input.view()), neighborhood_output.view(),
                                               ksj::image::BorderMode::replicate)) {
        throw std::runtime_error("OpenCV sobel_x backend failed");
      }
      opencv_sobel_x_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(opencv_sobel_x_checksum);
    });
    print_image_benchmark_row("sobel_x_3x3", "opencv", type_name, size, config, opencv_sobel_x_ns,
                              opencv_sobel_x_checksum);

    double public_sobel_x_checksum = 0.0;
    const auto public_sobel_x_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::sobel_x(input, ksj::image::BorderMode::replicate);
      public_sobel_x_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_sobel_x_checksum);
    });
    print_image_benchmark_row("sobel_x_3x3", "public_api", type_name, size, config, public_sobel_x_ns,
                              public_sobel_x_checksum);

    double sobel_x_checksum = 0.0;
    const auto sobel_x_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::sobel_x(input, neighborhood_output, ksj::image::BorderMode::replicate);
      sobel_x_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(sobel_x_checksum);
    });
    print_image_benchmark_row("sobel_x_3x3", "api", type_name, size, config, sobel_x_ns, sobel_x_checksum);

    double eigen_sobel_y_checksum = 0.0;
    const auto eigen_sobel_y_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::sobel_y(input, ksj::image::BorderMode::replicate);
      eigen_sobel_y_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_sobel_y_checksum);
    });
    print_image_benchmark_row("sobel_y_3x3", "eigen", type_name, size, config, eigen_sobel_y_ns,
                              eigen_sobel_y_checksum);

    double eigen_output_sobel_y_checksum = 0.0;
    const auto eigen_output_sobel_y_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::sobel_y(input, neighborhood_output, ksj::image::BorderMode::replicate);
      eigen_output_sobel_y_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_sobel_y_checksum);
    });
    print_image_benchmark_row("sobel_y_3x3", "eigen_output", type_name, size, config, eigen_output_sobel_y_ns,
                              eigen_output_sobel_y_checksum);

    double opencv_sobel_y_checksum = 0.0;
    const auto opencv_sobel_y_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::sobel_y(ksj::array::as_const_view(input.view()), neighborhood_output.view(),
                                               ksj::image::BorderMode::replicate)) {
        throw std::runtime_error("OpenCV sobel_y backend failed");
      }
      opencv_sobel_y_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(opencv_sobel_y_checksum);
    });
    print_image_benchmark_row("sobel_y_3x3", "opencv", type_name, size, config, opencv_sobel_y_ns,
                              opencv_sobel_y_checksum);

    double public_sobel_y_checksum = 0.0;
    const auto public_sobel_y_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::sobel_y(input, ksj::image::BorderMode::replicate);
      public_sobel_y_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_sobel_y_checksum);
    });
    print_image_benchmark_row("sobel_y_3x3", "public_api", type_name, size, config, public_sobel_y_ns,
                              public_sobel_y_checksum);

    double sobel_y_checksum = 0.0;
    const auto sobel_y_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::sobel_y(input, neighborhood_output, ksj::image::BorderMode::replicate);
      sobel_y_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(sobel_y_checksum);
    });
    print_image_benchmark_row("sobel_y_3x3", "api", type_name, size, config, sobel_y_ns, sobel_y_checksum);

    double eigen_gradient_checksum = 0.0;
    const auto eigen_gradient_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::gradient_magnitude(input, ksj::image::BorderMode::replicate);
      eigen_gradient_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_gradient_checksum);
    });
    print_image_benchmark_row("gradient_magnitude_3x3", "eigen", type_name, size, config, eigen_gradient_ns,
                              eigen_gradient_checksum);

    double eigen_output_gradient_checksum = 0.0;
    const auto eigen_output_gradient_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::gradient_magnitude(input, neighborhood_output, ksj::image::BorderMode::replicate);
      eigen_output_gradient_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_gradient_checksum);
    });
    print_image_benchmark_row("gradient_magnitude_3x3", "eigen_output", type_name, size, config,
                              eigen_output_gradient_ns, eigen_output_gradient_checksum);

    auto sobel_x_output = ksj::array::make_pooled_image<T>(size, size);
    auto sobel_y_output = ksj::array::make_pooled_image<T>(size, size);
    double eigen_two_pass_gradient_checksum = 0.0;
    const auto eigen_two_pass_gradient_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::sobel_x(input, sobel_x_output, ksj::image::BorderMode::replicate);
      ksj::image::detail::eigen::sobel_y(input, sobel_y_output, ksj::image::BorderMode::replicate);
      for (std::size_t index = 0; index < neighborhood_output.size(); ++index) {
        neighborhood_output.data()[index] = static_cast<T>(std::hypot(
          static_cast<double>(sobel_x_output.data()[index]), static_cast<double>(sobel_y_output.data()[index])));
      }
      eigen_two_pass_gradient_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_two_pass_gradient_checksum);
    });
    print_image_benchmark_row("gradient_magnitude_3x3", "eigen_two_pass", type_name, size, config,
                              eigen_two_pass_gradient_ns, eigen_two_pass_gradient_checksum);

    double opencv_gradient_checksum = 0.0;
    const auto opencv_gradient_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::gradient_magnitude(
            ksj::array::as_const_view(input.view()), neighborhood_output.view(), ksj::image::BorderMode::replicate)) {
        throw std::runtime_error("OpenCV gradient_magnitude backend failed");
      }
      opencv_gradient_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(opencv_gradient_checksum);
    });
    print_image_benchmark_row("gradient_magnitude_3x3", "opencv", type_name, size, config, opencv_gradient_ns,
                              opencv_gradient_checksum);

    double public_gradient_checksum = 0.0;
    const auto public_gradient_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::gradient_magnitude(input, ksj::image::BorderMode::replicate);
      public_gradient_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_gradient_checksum);
    });
    print_image_benchmark_row("gradient_magnitude_3x3", "public_api", type_name, size, config, public_gradient_ns,
                              public_gradient_checksum);

    double gradient_checksum = 0.0;
    const auto gradient_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::gradient_magnitude(input, neighborhood_output, ksj::image::BorderMode::replicate);
      gradient_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(gradient_checksum);
    });
    print_image_benchmark_row("gradient_magnitude_3x3", "api", type_name, size, config, gradient_ns, gradient_checksum);

    double eigen_laplacian_checksum = 0.0;
    const auto eigen_laplacian_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::laplacian(input, ksj::image::BorderMode::replicate);
      eigen_laplacian_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_laplacian_checksum);
    });
    print_image_benchmark_row("laplacian_3x3", "eigen", type_name, size, config, eigen_laplacian_ns,
                              eigen_laplacian_checksum);

    double eigen_output_laplacian_checksum = 0.0;
    const auto eigen_output_laplacian_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::laplacian(input, neighborhood_output, ksj::image::BorderMode::replicate);
      eigen_output_laplacian_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_laplacian_checksum);
    });
    print_image_benchmark_row("laplacian_3x3", "eigen_output", type_name, size, config, eigen_output_laplacian_ns,
                              eigen_output_laplacian_checksum);

    double public_laplacian_checksum = 0.0;
    const auto public_laplacian_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::laplacian(input, ksj::image::BorderMode::replicate);
      public_laplacian_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_laplacian_checksum);
    });
    print_image_benchmark_row("laplacian_3x3", "public_api", type_name, size, config, public_laplacian_ns,
                              public_laplacian_checksum);

    double laplacian_checksum = 0.0;
    const auto laplacian_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::laplacian(input, neighborhood_output, ksj::image::BorderMode::replicate);
      laplacian_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(laplacian_checksum);
    });
    print_image_benchmark_row("laplacian_3x3", "api", type_name, size, config, laplacian_ns, laplacian_checksum);

    double eigen_unsharp_checksum = 0.0;
    const auto eigen_unsharp_ns = ksj::benchmarks::measure(config, [&] {
      const auto output =
        ksj::image::detail::eigen::unsharp_mask(input, 1.0, 5, 1.2, ksj::image::BorderMode::replicate);
      eigen_unsharp_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_unsharp_checksum);
    });
    print_image_benchmark_row("unsharp_mask_5x5", "eigen", type_name, size, config, eigen_unsharp_ns,
                              eigen_unsharp_checksum);

    double eigen_output_unsharp_checksum = 0.0;
    const auto eigen_output_unsharp_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::unsharp_mask(input, neighborhood_output, 1.0, 5, 1.2,
                                              ksj::image::BorderMode::replicate);
      eigen_output_unsharp_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_unsharp_checksum);
    });
    print_image_benchmark_row("unsharp_mask_5x5", "eigen_output", type_name, size, config, eigen_output_unsharp_ns,
                              eigen_output_unsharp_checksum);

    double public_unsharp_checksum = 0.0;
    const auto public_unsharp_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::unsharp_mask(input, 1.0, 5, 1.2, ksj::image::BorderMode::replicate);
      public_unsharp_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_unsharp_checksum);
    });
    print_image_benchmark_row("unsharp_mask_5x5", "public_api", type_name, size, config, public_unsharp_ns,
                              public_unsharp_checksum);

    double unsharp_checksum = 0.0;
    const auto unsharp_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::unsharp_mask(input, neighborhood_output, 1.0, 5, 1.2, ksj::image::BorderMode::replicate);
      unsharp_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(unsharp_checksum);
    });
    print_image_benchmark_row("unsharp_mask_5x5", "api", type_name, size, config, unsharp_ns, unsharp_checksum);

    double eigen_morph_checksum = 0.0;
    const auto eigen_morph_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::detail::eigen::morph_close(input, 5, 5, ksj::image::BorderMode::replicate);
      eigen_morph_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_morph_checksum);
    });
    print_image_benchmark_row("morph_close_5x5", "eigen", type_name, size, config, eigen_morph_ns,
                              eigen_morph_checksum);

    double eigen_output_morph_checksum = 0.0;
    const auto eigen_output_morph_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::detail::eigen::morph_close(input, neighborhood_output, 5, 5, ksj::image::BorderMode::replicate);
      eigen_output_morph_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(eigen_output_morph_checksum);
    });
    print_image_benchmark_row("morph_close_5x5", "eigen_output", type_name, size, config, eigen_output_morph_ns,
                              eigen_output_morph_checksum);

    double opencv_morph_checksum = 0.0;
    const auto opencv_morph_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::image::detail::opencv::morph_close(ksj::array::as_const_view(input.view()), neighborhood_output.view(),
                                                   5, 5, ksj::image::BorderMode::replicate)) {
        throw std::runtime_error("OpenCV morph_close backend failed");
      }
      opencv_morph_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(opencv_morph_checksum);
    });
    print_image_benchmark_row("morph_close_5x5", "opencv", type_name, size, config, opencv_morph_ns,
                              opencv_morph_checksum);

    double public_morph_checksum = 0.0;
    const auto public_morph_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::image::morph_close(input, 5, ksj::image::BorderMode::replicate);
      public_morph_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_morph_checksum);
    });
    print_image_benchmark_row("morph_close_5x5", "public_api", type_name, size, config, public_morph_ns,
                              public_morph_checksum);

    double morph_checksum = 0.0;
    const auto morph_ns = ksj::benchmarks::measure(config, [&] {
      ksj::image::morph_close(input, neighborhood_output, 5, ksj::image::BorderMode::replicate);
      morph_checksum = ksj::benchmarks::checksum(neighborhood_output);
      ksj::benchmarks::do_not_optimize(morph_checksum);
    });
    print_image_benchmark_row("morph_close_5x5", "api", type_name, size, config, morph_ns, morph_checksum);
  }
}

} // namespace

void run_filter_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_filter_benchmarks<float>("float", config);
}

void run_filter_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_filter_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::image_benchmarks
