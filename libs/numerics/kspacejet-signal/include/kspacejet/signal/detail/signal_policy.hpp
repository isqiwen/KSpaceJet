#pragma once

#include <cstddef>
#include <type_traits>

namespace ksj::signal::detail {

struct SignalDispatchPolicy {
  // Measured on the production-profile Xeon Silver 4410Y benchmark sweep documented under
  // docs/benchmark_reports/2026-07-23/kspacejet-signal/xeon-silver-4410y/.
  static constexpr std::size_t intel_window_float_min_size = 32;
  static constexpr std::size_t intel_window_double_min_size = 0;
  static constexpr std::size_t intel_exponential_window_float_min_elements = 64U;
  static constexpr std::size_t intel_exponential_window_double_min_elements = 64U;
  static constexpr std::size_t intel_convolve_float_min_multiply_adds = 12U * 1024U;
  static constexpr std::size_t intel_convolve_double_min_multiply_adds = 12U * 1024U;
  static constexpr std::size_t intel_convolve2d_full_float_min_multiply_adds = 4U * 1024U;
  static constexpr std::size_t intel_fir_filter_float_min_multiply_adds = 1024U;
  static constexpr std::size_t intel_fir_filter_double_min_multiply_adds = 1024U;
  static constexpr std::size_t intel_iir_filter_float_min_elements = 64U;
  static constexpr std::size_t intel_iir_filter_double_min_elements = 32U;
  static constexpr std::size_t intel_median_filter_float_min_elements = 8U;
  static constexpr std::size_t intel_correlate2d_same_float_min_pixels = 32U * 32U;
  static constexpr std::size_t intel_correlate2d_same_min_kernel_pixels = 31U * 31U;
  static constexpr std::size_t opencv_correlate2d_same_float_min_pixels = 64;
  static constexpr std::size_t opencv_correlate2d_same_double_min_pixels = 64;
  static constexpr std::size_t opencv_correlate2d_same_min_kernel_pixels = 9;
  static constexpr std::size_t opencv_correlate2d_same_large_kernel_pixels = 31U * 31U;
  static constexpr std::size_t opencv_correlate2d_same_large_kernel_min_input_pixels = 16U * 16U;
  static constexpr std::size_t opencv_correlate2d_same_separable_float_min_pixels = 256;
  static constexpr std::size_t opencv_correlate2d_same_separable_double_min_pixels = 256;
  static constexpr std::size_t opencv_correlate2d_same_separable_min_kernel_elements = 6;
  static constexpr std::size_t fft_correlate2d_same_min_kernel_pixels = 31U * 31U;
  static constexpr std::size_t fft_correlate2d_same_float_min_pixels = 64U * 64U;
  static constexpr std::size_t fft_correlate2d_same_double_min_side = 240U;
  static constexpr std::size_t fft_correlate2d_same_double_max_side = 320U;
};

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_convolve(const std::size_t signal_elements,
                                                   const std::size_t kernel_elements) noexcept {
  if (signal_elements == 0U || kernel_elements == 0U) {
    return false;
  }

  if constexpr (std::is_same_v<T, float>) {
    return kernel_elements >=
           (SignalDispatchPolicy::intel_convolve_float_min_multiply_adds + signal_elements - 1U) / signal_elements;
  } else if constexpr (std::is_same_v<T, double>) {
    return kernel_elements >=
           (SignalDispatchPolicy::intel_convolve_double_min_multiply_adds + signal_elements - 1U) / signal_elements;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_convolve2d_full(const std::size_t input_elements,
                                                          const std::size_t kernel_elements) noexcept {
  if (input_elements == 0U || kernel_elements == 0U) {
    return false;
  }

  if constexpr (std::is_same_v<T, float>) {
    return kernel_elements >=
           (SignalDispatchPolicy::intel_convolve2d_full_float_min_multiply_adds + input_elements - 1U) / input_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_window(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= SignalDispatchPolicy::intel_window_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= SignalDispatchPolicy::intel_window_double_min_size;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_exponential_window(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= SignalDispatchPolicy::intel_exponential_window_float_min_elements;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= SignalDispatchPolicy::intel_exponential_window_double_min_elements;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_fir_filter(const std::size_t signal_elements,
                                                     const std::size_t taps_elements) noexcept {
  if (signal_elements == 0U || taps_elements == 0U) {
    return false;
  }

  if constexpr (std::is_same_v<T, float>) {
    return taps_elements >=
           (SignalDispatchPolicy::intel_fir_filter_float_min_multiply_adds + signal_elements - 1U) / signal_elements;
  } else if constexpr (std::is_same_v<T, double>) {
    return taps_elements >=
           (SignalDispatchPolicy::intel_fir_filter_double_min_multiply_adds + signal_elements - 1U) / signal_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_iir_filter(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return elements >= SignalDispatchPolicy::intel_iir_filter_float_min_elements;
  } else if constexpr (std::is_same_v<T, double>) {
    return elements >= SignalDispatchPolicy::intel_iir_filter_double_min_elements;
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] constexpr bool prefer_intel_median_filter(const std::size_t elements) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return elements >= SignalDispatchPolicy::intel_median_filter_float_min_elements;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_correlate2d_same(const std::size_t input_pixels,
                                                           const std::size_t kernel_pixels) noexcept {
  if (kernel_pixels < SignalDispatchPolicy::intel_correlate2d_same_min_kernel_pixels) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return input_pixels >= SignalDispatchPolicy::intel_correlate2d_same_float_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_correlate2d_same(const std::size_t input_pixels,
                                                            const std::size_t kernel_pixels) noexcept {
  if (kernel_pixels < SignalDispatchPolicy::opencv_correlate2d_same_min_kernel_pixels) {
    return false;
  }
  if (kernel_pixels >= SignalDispatchPolicy::opencv_correlate2d_same_large_kernel_pixels &&
      input_pixels < SignalDispatchPolicy::opencv_correlate2d_same_large_kernel_min_input_pixels) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return input_pixels >= SignalDispatchPolicy::opencv_correlate2d_same_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return input_pixels >= SignalDispatchPolicy::opencv_correlate2d_same_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_opencv_correlate2d_same_separable(const std::size_t input_pixels,
                                                                      const std::size_t row_kernel_elements,
                                                                      const std::size_t col_kernel_elements) noexcept {
  if (row_kernel_elements + col_kernel_elements <
      SignalDispatchPolicy::opencv_correlate2d_same_separable_min_kernel_elements) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return input_pixels >= SignalDispatchPolicy::opencv_correlate2d_same_separable_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return input_pixels >= SignalDispatchPolicy::opencv_correlate2d_same_separable_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_fft_correlate2d_same(const std::size_t input_rows, const std::size_t input_cols,
                                                         const std::size_t kernel_rows,
                                                         const std::size_t kernel_cols) noexcept {
  const auto input_pixels = input_rows * input_cols;
  const auto kernel_pixels = kernel_rows * kernel_cols;
  if (kernel_pixels < SignalDispatchPolicy::fft_correlate2d_same_min_kernel_pixels) {
    return false;
  }
  if constexpr (std::is_same_v<T, float>) {
    return input_pixels >= SignalDispatchPolicy::fft_correlate2d_same_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return input_rows == input_cols && input_rows >= SignalDispatchPolicy::fft_correlate2d_same_double_min_side &&
           input_rows <= SignalDispatchPolicy::fft_correlate2d_same_double_max_side;
  } else {
    return false;
  }
}

} // namespace ksj::signal::detail
