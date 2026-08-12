#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::image::detail {

// Tuned by docs/benchmark_reports/2026-07-23/kspacejet-numerics/xeon-silver-4410y-avx512-linux/benchmark_report.md.
struct ImageDispatchPolicy {
  static constexpr std::size_t intel_threshold_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t intel_normalize_float_min_pixels = 256U;
  static constexpr std::size_t opencv_threshold_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_threshold_double_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_normalize_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_normalize_double_min_pixels = 256U;
  static constexpr std::size_t opencv_resize_nearest_float_min_pixels = 1'048'576U;
  static constexpr std::size_t opencv_resize_nearest_double_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_resize_float_min_pixels = 262'144U;
  static constexpr std::size_t opencv_resize_float_max_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_resize_double_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_resize_double_max_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t intel_resize_nearest_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t intel_resize_linear_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t intel_resize_cubic_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t intel_gaussian_blur_float_min_pixels = 262'144U;
  static constexpr std::size_t opencv_resize_cubic_float_min_pixels = 1'024U;
  static constexpr std::size_t opencv_resize_cubic_double_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_resize_area_float_min_pixels = 64U;
  static constexpr std::size_t opencv_resize_area_double_min_pixels = 64U;
  static constexpr std::size_t opencv_resize_lanczos4_float_min_pixels = 256U;
  static constexpr std::size_t opencv_resize_lanczos4_double_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_box_filter_float_min_pixels = 16U;
  static constexpr std::size_t opencv_box_filter_double_min_pixels = 16U;
  static constexpr std::size_t intel_box_filter_float_min_pixels = 16'384U;
  static constexpr std::size_t opencv_gaussian_blur_float_min_pixels = 16U;
  static constexpr std::size_t opencv_gaussian_blur_double_min_pixels = 16U;
  static constexpr std::size_t opencv_bilateral_filter_float_min_pixels = 256U;
  static constexpr std::size_t opencv_median_filter_float_min_pixels = 16U;
  static constexpr std::size_t intel_median_filter_float_min_pixels = std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t opencv_sobel_x_float_min_pixels = 64U;
  static constexpr std::size_t opencv_sobel_x_double_min_pixels = 64U;
  static constexpr std::size_t opencv_sobel_y_float_min_pixels = 64U;
  static constexpr std::size_t opencv_sobel_y_double_min_pixels = 64U;
  static constexpr std::size_t intel_sobel_x_float_min_pixels = 65'536U;
  static constexpr std::size_t intel_sobel_y_float_min_pixels = 16'384U;
  static constexpr std::size_t opencv_gradient_magnitude_float_min_pixels = 1'024U;
  static constexpr std::size_t opencv_gradient_magnitude_double_min_pixels = 1'024U;
  static constexpr std::size_t opencv_connected_components_float_min_pixels = 64U;
  static constexpr std::size_t opencv_connected_components_double_min_pixels = 64U;
  static constexpr std::size_t opencv_morphology_float_min_pixels = 16U;
  static constexpr std::size_t opencv_morphology_double_min_pixels = 16U;
  static constexpr std::size_t intel_cubic_interpolate_2d_complex_float_min_pixels = 16U;
  static constexpr std::size_t opencv_cubic_interpolate_2d_complex_float_min_pixels =
    std::numeric_limits<std::size_t>::max();
};

template <typename T> [[nodiscard]] constexpr bool prefer_intel_threshold(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_threshold_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_normalize_minmax(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_normalize_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_threshold(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_threshold_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_threshold_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_normalize_minmax(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_normalize_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_normalize_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_resize_linear(const std::size_t input_pixels,
                                                         const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_float_min_pixels &&
           pixels <= ImageDispatchPolicy::opencv_resize_float_max_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_double_min_pixels &&
           pixels <= ImageDispatchPolicy::opencv_resize_double_max_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_resize_nearest(const std::size_t input_pixels,
                                                         const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_resize_nearest_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_resize_linear(const std::size_t input_pixels,
                                                        const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_resize_linear_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_resize_cubic(const std::size_t input_pixels,
                                                       const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_resize_cubic_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_resize_nearest(const std::size_t input_pixels,
                                                          const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_nearest_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_nearest_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_resize_cubic(const std::size_t input_pixels,
                                                        const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_cubic_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_cubic_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_resize_area(const std::size_t input_pixels,
                                                       const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_area_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_area_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_resize_lanczos4(const std::size_t input_pixels,
                                                           const std::size_t output_pixels) noexcept {
  const auto pixels = input_pixels > output_pixels ? input_pixels : output_pixels;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_lanczos4_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_resize_lanczos4_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_box_filter(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_box_filter_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_box_filter_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_box_filter(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_box_filter_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_gaussian_blur(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_gaussian_blur_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_gaussian_blur_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_gaussian_blur(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_gaussian_blur_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_bilateral_filter(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_bilateral_filter_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_median_filter(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_median_filter_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_median_filter(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_median_filter_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_sobel_x(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_sobel_x_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_sobel_x_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_sobel_y(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_sobel_y_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_sobel_y_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sobel_x(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_sobel_x_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_sobel_y(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::intel_sobel_y_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_gradient_magnitude(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_gradient_magnitude_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_gradient_magnitude_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_connected_components(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_connected_components_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_connected_components_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_opencv_morphology(const std::size_t pixels) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= ImageDispatchPolicy::opencv_morphology_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= ImageDispatchPolicy::opencv_morphology_double_min_pixels;
  } else {
    return false;
  }
}

[[nodiscard]] constexpr bool prefer_intel_cubic_interpolate_2d(const std::size_t pixels) noexcept {
  return pixels >= ImageDispatchPolicy::intel_cubic_interpolate_2d_complex_float_min_pixels;
}

[[nodiscard]] constexpr bool prefer_opencv_cubic_interpolate_2d(const std::size_t pixels) noexcept {
  return pixels >= ImageDispatchPolicy::opencv_cubic_interpolate_2d_complex_float_min_pixels;
}

} // namespace ksj::image::detail
