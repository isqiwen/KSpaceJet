#include "image_benchmark_common.hpp"

#include <algorithm>
#include <string>

namespace ksj::benchmarks::image_benchmarks {
namespace {

constexpr std::string_view kOutputReuseScope = "output_reuse";

template <typename T, typename Function>
void emit_image_row(const std::string_view case_name, const std::string_view backend, const std::string_view type_name,
                    const std::size_t size, const Config& config, ksj::array::PooledImage<T>& output,
                    const RowMetadata metadata, Function function) {
  const auto measurement = measure(config, [&] {
    function();
    do_not_optimize(output.data());
  });
  const double checksum_value = checksum(output);
  print_row(case_name, backend, type_name, size, config, measurement, checksum_value, metadata);
}

template <typename T, typename Function>
void emit_image_abs_row(const std::string_view case_name, const std::string_view backend,
                        const std::string_view type_name, const std::size_t size, const Config& config,
                        ksj::array::PooledImage<T>& output, const RowMetadata metadata, Function function) {
  const auto measurement = measure(config, [&] {
    function();
    do_not_optimize(output.data());
  });
  double checksum_value = 0.0;
  for (std::size_t index = 0U; index < output.size(); ++index) {
    checksum_value += std::abs(static_cast<double>(output.data()[index]));
  }
  print_row(case_name, backend, type_name, size, config, measurement, checksum_value, metadata);
}

inline void require_backend(const bool success, const std::string_view name) {
  if (!success) {
    throw std::runtime_error(std::string{name} + " backend failed");
  }
}

template <typename T>
void builtin_threshold(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output.data()[index] = input.data()[index] >= static_cast<T>(0.5) ? T{1} : T{};
  }
}

template <typename T>
void builtin_normalize_minmax(const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  const auto [minimum, maximum] = std::minmax_element(input.data(), input.data() + input.size());
  const T range = *maximum - *minimum;
  if (range == T{}) {
    std::fill(output.data(), output.data() + output.size(), T{});
    return;
  }
  for (std::size_t index = 0U; index < input.size(); ++index) {
    output.data()[index] = (input.data()[index] - *minimum) / range;
  }
}

template <typename T> [[nodiscard]] constexpr std::string_view selected_threshold(const std::size_t pixels) {
  if (ksj::image::detail::prefer_intel_threshold<T>(pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_threshold<T>(pixels)) {
    return "opencv";
  }
  return "builtin";
}

template <typename T> [[nodiscard]] constexpr std::string_view selected_normalize(const std::size_t pixels) {
  if (ksj::image::detail::prefer_intel_normalize_minmax<T>(pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_normalize_minmax<T>(pixels)) {
    return "opencv";
  }
  return "builtin";
}

template <typename T> [[nodiscard]] constexpr std::string_view selected_box_filter(const std::size_t pixels) {
  if (ksj::image::detail::prefer_intel_box_filter<T>(pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_box_filter<T>(pixels)) {
    return "opencv";
  }
  return "eigen";
}

template <typename T> [[nodiscard]] constexpr std::string_view selected_gaussian_blur(const std::size_t pixels) {
  if (ksj::image::detail::prefer_intel_gaussian_blur<T>(pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_gaussian_blur<T>(pixels)) {
    return "opencv";
  }
  return "eigen";
}

template <typename T> [[nodiscard]] constexpr std::string_view selected_median_filter(const std::size_t pixels) {
  if (ksj::image::detail::prefer_intel_median_filter<T>(pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_median_filter<T>(pixels)) {
    return "opencv";
  }
  return "eigen";
}

template <typename T>
[[nodiscard]] constexpr std::string_view selected_sobel(const std::size_t pixels, const bool horizontal) {
  if (horizontal) {
    if (ksj::image::detail::prefer_intel_sobel_x<T>(pixels)) {
      return "intel_ipp";
    }
    if (ksj::image::detail::prefer_opencv_sobel_x<T>(pixels)) {
      return "opencv";
    }
  } else {
    if (ksj::image::detail::prefer_intel_sobel_y<T>(pixels)) {
      return "intel_ipp";
    }
    if (ksj::image::detail::prefer_opencv_sobel_y<T>(pixels)) {
      return "opencv";
    }
  }
  return "eigen";
}

template <typename T>
[[nodiscard]] constexpr std::string_view selected_resize_nearest(const std::size_t input_pixels,
                                                                 const std::size_t output_pixels) {
  if (ksj::image::detail::prefer_intel_resize_nearest<T>(input_pixels, output_pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_resize_nearest<T>(input_pixels, output_pixels)) {
    return "opencv";
  }
  return "eigen";
}

template <typename T>
[[nodiscard]] constexpr std::string_view selected_resize_linear(const std::size_t input_pixels,
                                                                const std::size_t output_pixels) {
  if (ksj::image::detail::prefer_intel_resize_linear<T>(input_pixels, output_pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_resize_linear<T>(input_pixels, output_pixels)) {
    return "opencv";
  }
  return "eigen";
}

template <typename T>
[[nodiscard]] constexpr std::string_view selected_resize_cubic(const std::size_t input_pixels,
                                                               const std::size_t output_pixels) {
  if (ksj::image::detail::prefer_intel_resize_cubic<T>(input_pixels, output_pixels)) {
    return "intel_ipp";
  }
  if (ksj::image::detail::prefer_opencv_resize_cubic<T>(input_pixels, output_pixels)) {
    return "opencv";
  }
  return "eigen";
}

template <typename T>
[[nodiscard]] constexpr std::string_view selected_resize_lanczos4(const std::size_t input_pixels,
                                                                  const std::size_t output_pixels) {
  return ksj::image::detail::prefer_opencv_resize_lanczos4<T>(input_pixels, output_pixels) ? "opencv" : "eigen";
}

template <typename T>
void run_threshold_policy_benchmarks(const std::string_view type_name, const std::size_t size, const Config& config,
                                     const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  constexpr std::string_view threshold_case = "policy_threshold";
  emit_image_row(threshold_case, "builtin", type_name, size, config, output,
                 reference_row(threshold_case, kOutputReuseScope), [&] {
                   builtin_threshold(input, output);
                 });
  if constexpr (std::is_same_v<T, float>) {
    emit_image_row(threshold_case, "intel_ipp", type_name, size, config, output,
                   candidate_row(threshold_case, kOutputReuseScope), [&] {
                     require_backend(ksj::image::detail::intel::threshold(ksj::array::as_const_view(input.view()),
                                                                          output.view(), 0.5F, 0.0F, 1.0F),
                                     "Intel IPP threshold");
                   });
  }
  emit_image_row(threshold_case, "opencv", type_name, size, config, output,
                 candidate_row(threshold_case, kOutputReuseScope), [&] {
                   require_backend(ksj::image::detail::opencv::threshold(ksj::array::as_const_view(input.view()),
                                                                         output.view(), static_cast<T>(0.5), T{}, T{1}),
                                   "OpenCV threshold");
                 });
  emit_image_row(threshold_case, "automatic", type_name, size, config, output,
                 policy_row(threshold_case, kOutputReuseScope, selected_threshold<T>(input.size())), [&] {
                   ksj::image::threshold(ksj::array::as_const_view(input.view()), output.view(), static_cast<T>(0.5),
                                         T{}, T{1});
                 });

  constexpr std::string_view normalize_case = "policy_normalize_minmax";
  emit_image_row(normalize_case, "builtin", type_name, size, config, output,
                 reference_row(normalize_case, kOutputReuseScope), [&] {
                   builtin_normalize_minmax(input, output);
                 });
  if constexpr (std::is_same_v<T, float>) {
    emit_image_row(normalize_case, "intel_ipp", type_name, size, config, output,
                   candidate_row(normalize_case, kOutputReuseScope), [&] {
                     require_backend(ksj::image::detail::intel::normalize_minmax(
                                       ksj::array::as_const_view(input.view()), output.view()),
                                     "Intel IPP normalize_minmax");
                   });
  }
  emit_image_row(normalize_case, "opencv", type_name, size, config, output,
                 candidate_row(normalize_case, kOutputReuseScope), [&] {
                   require_backend(ksj::image::detail::opencv::normalize_minmax(ksj::array::as_const_view(input.view()),
                                                                                output.view()),
                                   "OpenCV normalize_minmax");
                 });
  emit_image_row(normalize_case, "automatic", type_name, size, config, output,
                 policy_row(normalize_case, kOutputReuseScope, selected_normalize<T>(input.size())), [&] {
                   ksj::image::normalize_minmax(ksj::array::as_const_view(input.view()), output.view());
                 });
}

template <typename T>
void run_filter_policy_benchmarks(const std::string_view type_name, const std::size_t size, const Config& config,
                                  const ksj::array::PooledImage<T>& input, ksj::array::PooledImage<T>& output) {
  constexpr auto border = ksj::image::BorderMode::replicate;

  constexpr std::string_view box_case = "policy_box_filter_5x5";
  emit_image_row(box_case, "eigen", type_name, size, config, output, reference_row(box_case, kOutputReuseScope), [&] {
    ksj::image::detail::eigen::box_filter(input, output, 5U, 5U, border);
  });
  if constexpr (std::is_same_v<T, float>) {
    emit_image_row(box_case, "intel_ipp", type_name, size, config, output, candidate_row(box_case, kOutputReuseScope),
                   [&] {
                     require_backend(ksj::image::detail::intel::box_filter(ksj::array::as_const_view(input.view()),
                                                                           output.view(), 5U, 5U, border),
                                     "Intel IPP box_filter");
                   });
  }
  emit_image_row(box_case, "opencv", type_name, size, config, output, candidate_row(box_case, kOutputReuseScope), [&] {
    require_backend(
      ksj::image::detail::opencv::box_filter(ksj::array::as_const_view(input.view()), output.view(), 5U, 5U, border),
      "OpenCV box_filter");
  });
  emit_image_row(box_case, "automatic", type_name, size, config, output,
                 policy_row(box_case, kOutputReuseScope, selected_box_filter<T>(input.size())), [&] {
                   ksj::image::box_filter(input, output, 5U, 5U, border);
                 });

  constexpr std::string_view gaussian_case = "policy_gaussian_blur_5x5";
  emit_image_row(gaussian_case, "eigen", type_name, size, config, output,
                 reference_row(gaussian_case, kOutputReuseScope), [&] {
                   ksj::image::detail::eigen::gaussian_blur(input, output, 5U, 1.2, border);
                 });
  if constexpr (std::is_same_v<T, float>) {
    emit_image_row(gaussian_case, "intel_ipp", type_name, size, config, output,
                   candidate_row(gaussian_case, kOutputReuseScope), [&] {
                     require_backend(ksj::image::detail::intel::gaussian_blur(ksj::array::as_const_view(input.view()),
                                                                              output.view(), 5U, 1.2, border),
                                     "Intel IPP gaussian_blur");
                   });
  }
  emit_image_row(gaussian_case, "opencv", type_name, size, config, output,
                 candidate_row(gaussian_case, kOutputReuseScope), [&] {
                   require_backend(ksj::image::detail::opencv::gaussian_blur(ksj::array::as_const_view(input.view()),
                                                                             output.view(), 5U, 1.2, border),
                                   "OpenCV gaussian_blur");
                 });
  emit_image_row(gaussian_case, "automatic", type_name, size, config, output,
                 policy_row(gaussian_case, kOutputReuseScope, selected_gaussian_blur<T>(input.size())), [&] {
                   ksj::image::gaussian_blur(input, output, 5U, 1.2, border);
                 });

  constexpr std::string_view bilateral_case = "policy_bilateral_filter_5x5";
  emit_image_row(bilateral_case, "eigen", type_name, size, config, output,
                 reference_row(bilateral_case, kOutputReuseScope), [&] {
                   ksj::image::detail::eigen::bilateral_filter(input, output, 5U, 0.1, 1.5, border);
                 });
  if constexpr (std::is_same_v<T, float>) {
    emit_image_row(bilateral_case, "opencv", type_name, size, config, output,
                   candidate_row(bilateral_case, kOutputReuseScope), [&] {
                     require_backend(ksj::image::detail::opencv::bilateral_filter(
                                       ksj::array::as_const_view(input.view()), output.view(), 5U, 0.1, 1.5, border),
                                     "OpenCV bilateral_filter");
                   });
  }
  const auto selected_bilateral = ksj::image::detail::prefer_opencv_bilateral_filter<T>(input.size())
                                    ? std::string_view{"opencv"}
                                    : std::string_view{"eigen"};
  emit_image_row(bilateral_case, "automatic", type_name, size, config, output,
                 policy_row(bilateral_case, kOutputReuseScope, selected_bilateral), [&] {
                   ksj::image::bilateral_filter(input, output, 5U, 0.1, 1.5, border);
                 });

  constexpr std::string_view median_case = "policy_median_filter_3x3";
  emit_image_row(median_case, "eigen", type_name, size, config, output, reference_row(median_case, kOutputReuseScope),
                 [&] {
                   ksj::image::detail::eigen::median_filter(input, output, 3U, border);
                 });
  if constexpr (std::is_same_v<T, float>) {
    emit_image_row(median_case, "intel_ipp", type_name, size, config, output,
                   candidate_row(median_case, kOutputReuseScope), [&] {
                     require_backend(ksj::image::detail::intel::median_filter(ksj::array::as_const_view(input.view()),
                                                                              output.view(), 3U, border),
                                     "Intel IPP median_filter");
                   });
    emit_image_row(median_case, "opencv", type_name, size, config, output,
                   candidate_row(median_case, kOutputReuseScope), [&] {
                     require_backend(ksj::image::detail::opencv::median_filter(ksj::array::as_const_view(input.view()),
                                                                               output.view(), 3U, border),
                                     "OpenCV median_filter");
                   });
  }
  emit_image_row(median_case, "automatic", type_name, size, config, output,
                 policy_row(median_case, kOutputReuseScope, selected_median_filter<T>(input.size())), [&] {
                   ksj::image::median_filter(input, output, 3U, border);
                 });

  const auto emit_sobel = [&](const std::string_view case_name, const bool horizontal) {
    constexpr double relative_tolerance = std::is_same_v<T, float> ? 2.0e-5 : -1.0;
    emit_image_abs_row(case_name, "eigen", type_name, size, config, output,
                       reference_row(case_name, kOutputReuseScope, -1.0, relative_tolerance), [&] {
                         if (horizontal) {
                           ksj::image::detail::eigen::sobel_x(input, output, border);
                         } else {
                           ksj::image::detail::eigen::sobel_y(input, output, border);
                         }
                       });
    if constexpr (std::is_same_v<T, float>) {
      emit_image_abs_row(
        case_name, "intel_ipp", type_name, size, config, output,
        candidate_row(case_name, kOutputReuseScope, -1.0, relative_tolerance), [&] {
          const bool success =
            horizontal
              ? ksj::image::detail::intel::sobel_x(ksj::array::as_const_view(input.view()), output.view(), border)
              : ksj::image::detail::intel::sobel_y(ksj::array::as_const_view(input.view()), output.view(), border);
          require_backend(success, horizontal ? "Intel IPP sobel_x" : "Intel IPP sobel_y");
        });
    }
    emit_image_abs_row(
      case_name, "opencv", type_name, size, config, output,
      candidate_row(case_name, kOutputReuseScope, -1.0, relative_tolerance), [&] {
        const bool success =
          horizontal
            ? ksj::image::detail::opencv::sobel_x(ksj::array::as_const_view(input.view()), output.view(), border)
            : ksj::image::detail::opencv::sobel_y(ksj::array::as_const_view(input.view()), output.view(), border);
        require_backend(success, horizontal ? "OpenCV sobel_x" : "OpenCV sobel_y");
      });
    emit_image_abs_row(
      case_name, "automatic", type_name, size, config, output,
      policy_row(case_name, kOutputReuseScope, selected_sobel<T>(input.size(), horizontal), -1.0, relative_tolerance),
      [&] {
        if (horizontal) {
          ksj::image::sobel_x(input, output, border);
        } else {
          ksj::image::sobel_y(input, output, border);
        }
      });
  };
  emit_sobel("policy_sobel_x_3x3", true);
  emit_sobel("policy_sobel_y_3x3", false);

  constexpr std::string_view gradient_case = "policy_gradient_magnitude_3x3";
  emit_image_row(gradient_case, "eigen", type_name, size, config, output,
                 reference_row(gradient_case, kOutputReuseScope), [&] {
                   ksj::image::detail::eigen::gradient_magnitude(input, output, border);
                 });
  emit_image_row(
    gradient_case, "opencv", type_name, size, config, output, candidate_row(gradient_case, kOutputReuseScope), [&] {
      require_backend(
        ksj::image::detail::opencv::gradient_magnitude(ksj::array::as_const_view(input.view()), output.view(), border),
        "OpenCV gradient_magnitude");
    });
  const auto selected_gradient = ksj::image::detail::prefer_opencv_gradient_magnitude<T>(input.size())
                                   ? std::string_view{"opencv"}
                                   : std::string_view{"eigen"};
  emit_image_row(gradient_case, "automatic", type_name, size, config, output,
                 policy_row(gradient_case, kOutputReuseScope, selected_gradient), [&] {
                   ksj::image::gradient_magnitude(input, output, border);
                 });

  constexpr std::string_view morph_case = "policy_morph_close_5x5";
  emit_image_row(morph_case, "eigen", type_name, size, config, output, reference_row(morph_case, kOutputReuseScope),
                 [&] {
                   ksj::image::detail::eigen::morph_close(input, output, 5U, 5U, border);
                 });
  emit_image_row(morph_case, "opencv", type_name, size, config, output, candidate_row(morph_case, kOutputReuseScope),
                 [&] {
                   require_backend(ksj::image::detail::opencv::morph_close(ksj::array::as_const_view(input.view()),
                                                                           output.view(), 5U, 5U, border),
                                   "OpenCV morph_close");
                 });
  const auto selected_morph = ksj::image::detail::prefer_opencv_morphology<T>(input.size()) ? std::string_view{"opencv"}
                                                                                            : std::string_view{"eigen"};
  emit_image_row(morph_case, "automatic", type_name, size, config, output,
                 policy_row(morph_case, kOutputReuseScope, selected_morph), [&] {
                   ksj::image::morph_close(input, output, 5U, border);
                 });
}

template <typename T>
void run_resize_policy_benchmarks(const std::string_view type_name, const std::size_t size, const Config& config,
                                  const ksj::array::PooledImage<T>& input) {
  const std::size_t output_size = std::max<std::size_t>(1U, size / 2U);
  auto output = ksj::array::make_pooled_image<T>(output_size, output_size);
  const auto input_pixels = input.size();
  const auto output_pixels = output.size();

  const auto run_resize_case = [&](const std::string_view case_name, const auto eigen_function,
                                   const auto opencv_function, const auto public_function,
                                   const std::string_view selected_backend, const auto intel_function,
                                   const double relative_tolerance) {
    const auto eigen_group = std::string{case_name} + ".eigen";
    const auto intel_group = std::string{case_name} + ".intel_ipp";
    const auto opencv_group = std::string{case_name} + ".opencv";
    const auto policy_group = selected_backend == "intel_ipp" ? std::string_view{intel_group}
                              : selected_backend == "opencv"  ? std::string_view{opencv_group}
                                                              : std::string_view{eigen_group};
    emit_image_row(case_name, "eigen", type_name, size, config, output,
                   reference_row(eigen_group, kOutputReuseScope, -1.0, relative_tolerance), [&] {
                     eigen_function(input, output);
                   });
    if constexpr (std::is_same_v<T, float>) {
      emit_image_row(case_name, "intel_ipp", type_name, size, config, output,
                     reference_row(intel_group, kOutputReuseScope, -1.0, relative_tolerance), [&] {
                       require_backend(intel_function(input, output), "Intel IPP resize");
                     });
    }
    emit_image_row(case_name, "opencv", type_name, size, config, output,
                   reference_row(opencv_group, kOutputReuseScope, -1.0, relative_tolerance), [&] {
                     require_backend(opencv_function(input, output), "OpenCV resize");
                   });
    emit_image_row(case_name, "automatic", type_name, size, config, output,
                   policy_row(policy_group, kOutputReuseScope, selected_backend, -1.0, relative_tolerance), [&] {
                     public_function(input, output);
                   });
  };

  run_resize_case(
    "policy_resize_nearest_half",
    [](const auto& source, auto& destination) {
      ksj::image::detail::eigen::resize_nearest(source, destination);
    },
    [](const auto& source, auto& destination) {
      return ksj::image::detail::opencv::resize_nearest(source, destination);
    },
    [](const auto& source, auto& destination) {
      ksj::image::resize_nearest(source, destination);
    },
    selected_resize_nearest<T>(input_pixels, output_pixels),
    [](const auto& source, auto& destination) {
      return ksj::image::detail::intel::resize_nearest(source, destination);
    },
    -1.0);
  run_resize_case(
    "policy_resize_linear_half",
    [](const auto& source, auto& destination) {
      ksj::image::detail::eigen::resize_linear(source, destination);
    },
    [](const auto& source, auto& destination) {
      return ksj::image::detail::opencv::resize_linear(source, destination);
    },
    [](const auto& source, auto& destination) {
      ksj::image::resize_linear(source, destination);
    },
    selected_resize_linear<T>(input_pixels, output_pixels),
    [](const auto& source, auto& destination) {
      return ksj::image::detail::intel::resize_linear(source, destination);
    },
    std::is_same_v<T, float> ? 5.0e-3 : -1.0);
  run_resize_case(
    "policy_resize_cubic_half",
    [](const auto& source, auto& destination) {
      ksj::image::detail::eigen::resize_cubic(source, destination);
    },
    [](const auto& source, auto& destination) {
      return ksj::image::detail::opencv::resize_cubic(source, destination);
    },
    [](const auto& source, auto& destination) {
      ksj::image::resize_cubic(source, destination);
    },
    selected_resize_cubic<T>(input_pixels, output_pixels),
    [](const auto& source, auto& destination) {
      return ksj::image::detail::intel::resize_cubic(source, destination);
    },
    std::is_same_v<T, float> ? 5.0e-3 : -1.0);

  constexpr std::string_view area_case = "policy_resize_area_half";
  emit_image_row(area_case, "eigen", type_name, size, config, output, reference_row(area_case, kOutputReuseScope), [&] {
    ksj::image::detail::eigen::resize_area(input, output);
  });
  emit_image_row(area_case, "opencv", type_name, size, config, output, candidate_row(area_case, kOutputReuseScope),
                 [&] {
                   require_backend(ksj::image::detail::opencv::resize_area(input, output), "OpenCV resize_area");
                 });
  const auto selected_area = ksj::image::detail::prefer_opencv_resize_area<T>(input_pixels, output_pixels)
                               ? std::string_view{"opencv"}
                               : std::string_view{"eigen"};
  emit_image_row(area_case, "automatic", type_name, size, config, output,
                 policy_row(area_case, kOutputReuseScope, selected_area), [&] {
                   ksj::image::resize_area(input, output);
                 });

  constexpr std::string_view lanczos_case = "policy_resize_lanczos4_half";
  constexpr double lanczos_relative_tolerance = std::is_same_v<T, float> ? 5.0e-3 : -1.0;
  const auto lanczos_eigen_group = std::string{lanczos_case} + ".eigen";
  const auto lanczos_opencv_group = std::string{lanczos_case} + ".opencv";
  const auto selected_lanczos = selected_resize_lanczos4<T>(input_pixels, output_pixels);
  const auto lanczos_policy_group =
    selected_lanczos == "opencv" ? std::string_view{lanczos_opencv_group} : std::string_view{lanczos_eigen_group};
  emit_image_row(lanczos_case, "eigen", type_name, size, config, output,
                 reference_row(lanczos_eigen_group, kOutputReuseScope, -1.0, lanczos_relative_tolerance), [&] {
                   ksj::image::detail::eigen::resize_lanczos4(input, output);
                 });
  emit_image_row(lanczos_case, "opencv", type_name, size, config, output,
                 reference_row(lanczos_opencv_group, kOutputReuseScope, -1.0, lanczos_relative_tolerance), [&] {
                   require_backend(ksj::image::detail::opencv::resize_lanczos4(input, output),
                                   "OpenCV resize_lanczos4");
                 });
  emit_image_row(
    lanczos_case, "automatic", type_name, size, config, output,
    policy_row(lanczos_policy_group, kOutputReuseScope, selected_lanczos, -1.0, lanczos_relative_tolerance), [&] {
      ksj::image::resize_lanczos4(input, output);
    });
}

template <typename T>
void run_components_policy_benchmark(const std::string_view type_name, const std::size_t size, const Config& config) {
  constexpr std::string_view case_name = "policy_connected_components_8";
  auto input = ksj::array::make_pooled_image<T>(size, size);
  fill_component_mask(input);
  auto labels = ksj::array::make_pooled_image<ksj::image::ConnectedComponentLabel>(size, size);
  std::vector<ksj::image::ConnectedComponentStats> stats;
  std::size_t component_count = 0U;

  const auto emit_components_row = [&](const std::string_view backend, const RowMetadata metadata,
                                       const auto function) {
    const auto measurement = measure(config, [&] {
      component_count = function();
      do_not_optimize(component_count);
      do_not_optimize(labels.data());
    });
    const double checksum_value = checksum(labels) + static_cast<double>(component_count);
    print_row(case_name, backend, type_name, size, config, measurement, checksum_value, metadata);
  };

  emit_components_row("eigen", reference_row(case_name, kOutputReuseScope), [&] {
    return ksj::image::detail::eigen::connected_components(input, labels, &stats, ksj::image::Connectivity::eight);
  });
  emit_components_row("opencv", candidate_row(case_name, kOutputReuseScope), [&] {
    require_backend(ksj::image::detail::opencv::connected_components(input, labels, &stats,
                                                                     ksj::image::Connectivity::eight, component_count),
                    "OpenCV connected_components");
    return component_count;
  });
  const auto selected_backend = ksj::image::detail::prefer_opencv_connected_components<T>(input.size())
                                  ? std::string_view{"opencv"}
                                  : std::string_view{"eigen"};
  emit_components_row("automatic", policy_row(case_name, kOutputReuseScope, selected_backend), [&] {
    return ksj::image::connected_components(input, labels, &stats, ksj::image::Connectivity::eight);
  });
}

template <typename T> void run_policy_benchmarks(const std::string_view type_name, const Config& config) {
  for (const std::size_t size : config.sizes) {
    auto input = ksj::array::make_pooled_image<T>(size, size);
    auto output = ksj::array::make_pooled_image<T>(size, size);
    require_pooled_storage("policy input", input);
    require_pooled_storage("policy output", output);
    fill_image(input);

    run_threshold_policy_benchmarks(type_name, size, config, input, output);
    run_resize_policy_benchmarks(type_name, size, config, input);
    run_components_policy_benchmark<T>(type_name, size, config);
    if (size <= kMaxNeighborhoodSize) {
      run_filter_policy_benchmarks(type_name, size, config, input, output);
    }
  }
}

} // namespace

void run_policy_benchmarks_float(const Config& config) {
  run_policy_benchmarks<float>("float", config);
}

void run_policy_benchmarks_double(const Config& config) {
  run_policy_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::image_benchmarks
