#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>

namespace ksj::fft::detail {

struct FftDispatchPolicy {
  // Tuned by docs/benchmark_reports/2026-07-24/kspacejet-fft/xeon-silver-4410y-avx512-linux/benchmark_report.md.
  // Free 1D calls include MKL DFTI descriptor creation and commit.
  static constexpr std::size_t intel_fft1_float_min_size = 128U * 1024U;
  static constexpr std::size_t intel_fft1_double_min_size = 32U * 1024U;
  static constexpr std::size_t intel_fft2_float_min_pixels = 32U * 32U;
  static constexpr std::size_t intel_fft2_double_min_pixels = 16U * 16U;
  static constexpr std::size_t intel_fft2_batch_min_count = 1U;
  static constexpr std::size_t intel_fft3_float_min_voxels = 4U * 4U * 4U;
  static constexpr std::size_t intel_fft3_double_min_voxels = 4U * 4U * 4U;
  static constexpr std::size_t intel_fft3_batch_min_count = 1U;
};

template <typename T> [[nodiscard]] constexpr bool prefer_intel_fft(const std::size_t size) noexcept {
  if constexpr (std::is_same_v<T, float>) {
    return size >= FftDispatchPolicy::intel_fft1_float_min_size;
  } else if constexpr (std::is_same_v<T, double>) {
    return size >= FftDispatchPolicy::intel_fft1_double_min_size;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_fft2(const std::size_t rows, const std::size_t cols) noexcept {
  const auto pixels = rows * cols;
  if constexpr (std::is_same_v<T, float>) {
    return pixels >= FftDispatchPolicy::intel_fft2_float_min_pixels;
  } else if constexpr (std::is_same_v<T, double>) {
    return pixels >= FftDispatchPolicy::intel_fft2_double_min_pixels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_fft2_batch(const std::size_t rows, const std::size_t cols,
                                                     const std::size_t batch_count) noexcept {
  if (rows == 0U || cols == 0U || batch_count < FftDispatchPolicy::intel_fft2_batch_min_count) {
    return false;
  }
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_fft3(const std::size_t rows, const std::size_t cols,
                                               const std::size_t slices) noexcept {
  const auto voxels = rows * cols * slices;
  if constexpr (std::is_same_v<T, float>) {
    return voxels >= FftDispatchPolicy::intel_fft3_float_min_voxels;
  } else if constexpr (std::is_same_v<T, double>) {
    return voxels >= FftDispatchPolicy::intel_fft3_double_min_voxels;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] constexpr bool prefer_intel_fft3_batch(const std::size_t rows, const std::size_t cols,
                                                     const std::size_t slices, const std::size_t batch_count) noexcept {
  if (rows == 0U || cols == 0U || slices == 0U || batch_count < FftDispatchPolicy::intel_fft3_batch_min_count) {
    return false;
  }
  if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
    return true;
  } else {
    return false;
  }
}

} // namespace ksj::fft::detail
