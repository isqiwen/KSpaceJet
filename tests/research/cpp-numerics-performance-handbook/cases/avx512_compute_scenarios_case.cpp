#include "kspacejet/base/types.hpp"
#include "common.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#if defined(KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS) && KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS
#include <immintrin.h>
#endif

namespace {

using namespace ksj::research::cpp_numerics_performance;

#if defined(__GNUC__) || defined(__clang__)
#define KSJ_RESEARCH_RESTRICT __restrict__
#else
#define KSJ_RESEARCH_RESTRICT
#endif

#ifndef KSJ_NUMERICS_PERF_SCENARIO_VARIANT
#define KSJ_NUMERICS_PERF_SCENARIO_VARIANT "default"
#endif

#ifndef KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS
#define KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS 0
#endif

constexpr std::string_view kScenarioVariant = KSJ_NUMERICS_PERF_SCENARIO_VARIANT;

#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
constexpr bool kUseAvx512Intrinsics = true;
#else
constexpr bool kUseAvx512Intrinsics = false;
#endif

constexpr std::string_view kImplementationVariant = kUseAvx512Intrinsics ? "manual_intrinsics" : "scalar";
constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 2.0F * kPi;
constexpr float kPhaseUnwrapThreshold = 1.5F * kPi;

// These scenarios mix classic SIMD-friendly loops with KSpaceJet-derived hot loop shapes
// and can grow to cover any compute pattern that may benefit from AVX512.
[[nodiscard]] std::string case_variant(std::string_view implementation) {
  std::string variant{kScenarioVariant};
  variant += '/';
  variant += implementation;
  return variant;
}

template <typename T> [[nodiscard]] double checksum_linear(const T* data, std::size_t size) {
  double sum{};
  for (std::size_t i = 0; i < size; ++i) {
    sum += static_cast<double>(data[i]);
  }
  return sum;
}

[[maybe_unused]] void classic_fma_triad_scalar(const float* KSJ_RESEARCH_RESTRICT lhs,
                                               const float* KSJ_RESEARCH_RESTRICT rhs,
                                               float* KSJ_RESEARCH_RESTRICT output, std::size_t size, float alpha) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = alpha * lhs[i] + rhs[i];
  }
}

[[maybe_unused]] void classic_fma_triad_f64_scalar(const double* KSJ_RESEARCH_RESTRICT lhs,
                                                   const double* KSJ_RESEARCH_RESTRICT rhs,
                                                   double* KSJ_RESEARCH_RESTRICT output, std::size_t size,
                                                   double alpha) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = alpha * lhs[i] + rhs[i];
  }
}

[[maybe_unused]] float classic_dot_scalar(const float* lhs, const float* rhs, std::size_t size) {
  float value{};
  for (std::size_t i = 0; i < size; ++i) {
    value += lhs[i] * rhs[i];
  }
  return value;
}

[[maybe_unused]] double classic_dot_f64_scalar(const double* lhs, const double* rhs, std::size_t size) {
  double value{};
  for (std::size_t i = 0; i < size; ++i) {
    value += lhs[i] * rhs[i];
  }
  return value;
}

[[maybe_unused]] void waterfat_weight_gate_scalar(const float* KSJ_RESEARCH_RESTRICT buffer_x,
                                                  const float* KSJ_RESEARCH_RESTRICT buffer_y,
                                                  const float* KSJ_RESEARCH_RESTRICT weight_in,
                                                  float* KSJ_RESEARCH_RESTRICT weight_out, std::size_t size,
                                                  float threshold) {
  constexpr float kEpsilonWeight = 0.000001F;
  for (std::size_t i = 0; i < size; ++i) {
    weight_out[i] = (buffer_x[i] > threshold)   ? kEpsilonWeight
                    : (buffer_y[i] > threshold) ? kEpsilonWeight
                                                : weight_in[i] * weight_in[i];
  }
}

[[maybe_unused]] void prewhiten_covariance_pair_scalar(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs,
                                                       std::size_t sample_count, float& real_sum, float& imag_sum) {
  float real{};
  float imag{};
  for (std::size_t sample = 0; sample < sample_count; ++sample) {
    real += lhs[sample].real() * rhs[sample].real() + lhs[sample].imag() * rhs[sample].imag();
    imag += lhs[sample].real() * rhs[sample].imag() - lhs[sample].imag() * rhs[sample].real();
  }
  real_sum = real;
  imag_sum = imag;
}

[[maybe_unused]] void prewhiten_channel_mix_scalar(const Image<ksj::base::cf32>& input,
                                                   const Image<ksj::base::cf32>& lower_triangle,
                                                   Image<ksj::base::cf32>& output) {
  for (std::size_t channel = 0; channel < output.rows(); ++channel) {
    for (std::size_t sample = 0; sample < output.cols(); ++sample) {
      output(channel, sample) = {};
    }
  }

  for (std::size_t output_channel = 0; output_channel < output.rows(); ++output_channel) {
    for (std::size_t input_channel = 0; input_channel <= output_channel; ++input_channel) {
      const auto coeff = lower_triangle(output_channel, input_channel);
      for (std::size_t sample = 0; sample < output.cols(); ++sample) {
        output(output_channel, sample) += coeff * input(input_channel, sample);
      }
    }
  }
}

[[maybe_unused]] void rss_coil_combine_scalar(const Image<ksj::base::cf32>& input, Vector<float>& output) {
  for (std::size_t voxel = 0; voxel < input.rows(); ++voxel) {
    float sum{};
    for (std::size_t coil = 0; coil < input.cols(); ++coil) {
      const auto value = input(voxel, coil);
      sum += value.real() * value.real() + value.imag() * value.imag();
    }
    output(voxel) = std::sqrt(sum);
  }
}

[[maybe_unused]] void sum_and_sumsq_scalar(const float* input, std::size_t size, float& sum, float& sumsq) {
  float local_sum{};
  float local_sumsq{};
  for (std::size_t i = 0; i < size; ++i) {
    local_sum += input[i];
    local_sumsq += input[i] * input[i];
  }
  sum = local_sum;
  sumsq = local_sumsq;
}

[[maybe_unused]] void min_max_scalar(const float* input, std::size_t size, float& min_value, float& max_value) {
  auto local_min = std::numeric_limits<float>::infinity();
  auto local_max = -std::numeric_limits<float>::infinity();
  for (std::size_t i = 0; i < size; ++i) {
    local_min = std::min(local_min, input[i]);
    local_max = std::max(local_max, input[i]);
  }
  min_value = local_min;
  max_value = local_max;
}

[[maybe_unused]] void fused_axpby_relu_scalar(const float* KSJ_RESEARCH_RESTRICT lhs,
                                              const float* KSJ_RESEARCH_RESTRICT rhs,
                                              float* KSJ_RESEARCH_RESTRICT output, std::size_t size, float alpha,
                                              float beta, float bias) {
  for (std::size_t i = 0; i < size; ++i) {
    const auto value = alpha * lhs[i] + beta * rhs[i] + bias;
    output[i] = value > 0.0F ? value : 0.0F;
  }
}

[[maybe_unused]] float l2_distance_scalar(const float* lhs, const float* rhs, std::size_t size) {
  float sum{};
  for (std::size_t i = 0; i < size; ++i) {
    const auto delta = lhs[i] - rhs[i];
    sum += delta * delta;
  }
  return sum;
}

[[maybe_unused]] void complex_mul_aos_scalar(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs,
                                             ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = lhs[i] * rhs[i];
  }
}

[[maybe_unused]] void complex_conj_mul_aos_scalar(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs,
                                                  ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = lhs[i] * std::conj(rhs[i]);
  }
}

[[maybe_unused]] void complex_normalize_scalar(const ksj::base::cf32* input, ksj::base::cf32* output,
                                               std::size_t size) {
  constexpr float kEpsilon = 0.000001F;
  for (std::size_t i = 0; i < size; ++i) {
    const auto real = input[i].real();
    const auto imag = input[i].imag();
    const auto magnitude = std::max(std::sqrt((real * real) + (imag * imag)), kEpsilon);
    output[i] = {real / magnitude, imag / magnitude};
  }
}

[[maybe_unused]] void complex_magnitude_squared_scalar(const ksj::base::cf32* input, float* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    const auto real = input[i].real();
    const auto imag = input[i].imag();
    output[i] = real * real + imag * imag;
  }
}

[[maybe_unused]] void complex_deinterleave_scalar(const ksj::base::cf32* input, float* real, float* imag,
                                                  std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    real[i] = input[i].real();
    imag[i] = input[i].imag();
  }
}

[[maybe_unused]] void view_copy_real_component_scalar(const ksj::base::cf32* source, const ksj::base::cf32* destination,
                                                      ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = {source[i].real(), destination[i].imag()};
  }
}

[[maybe_unused]] void view_copy_imag_component_scalar(const ksj::base::cf32* source, const ksj::base::cf32* destination,
                                                      ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = {destination[i].real(), source[i].imag()};
  }
}

[[maybe_unused]] void complex_discard_imag_clamp_scalar(const ksj::base::cf32* input, ksj::base::cf32* output,
                                                        std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = {std::max(input[i].real(), 0.0F), 0.0F};
  }
}

[[maybe_unused]] void phase_difference_scalar(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs,
                                              ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = {1.0F, lhs[i].imag() - rhs[i].imag()};
  }
}

[[maybe_unused]] void phase_unwrap_1p5_scalar(const ksj::base::cf32* input, ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    auto phase = input[i].imag();
    if (phase < -kPhaseUnwrapThreshold) {
      phase += kTwoPi;
    } else if (phase > kPhaseUnwrapThreshold) {
      phase -= kTwoPi;
    }
    output[i] = {input[i].real(), phase};
  }
}

[[maybe_unused]] void sdat_complex_magnitude_scalar(const float* interleaved_complex, float* output,
                                                    std::size_t complex_count) {
  for (std::size_t i = 0; i < complex_count; ++i) {
    const auto real = interleaved_complex[(i * 2U)];
    const auto imag = interleaved_complex[(i * 2U) + 1U];
    output[i] = std::sqrt((real * real) + (imag * imag));
  }
}

[[maybe_unused]] float gather_weighted_sum_scalar(const float* table, const std::int32_t* indices, const float* weights,
                                                  std::size_t size) {
  float sum{};
  for (std::size_t i = 0; i < size; ++i) {
    sum += table[indices[i]] * weights[i];
  }
  return sum;
}

[[maybe_unused]] void strided_scale_pack_scalar(const float* input, float* output, std::size_t size, float scale) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = input[(i * 2U) + 1U] * scale;
  }
}

[[maybe_unused]] void image_threshold_scalar(const float* input, float* output, std::size_t size, float threshold,
                                             float low_value, float high_value) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = input[i] >= threshold ? high_value : low_value;
  }
}

[[maybe_unused]] std::size_t masked_threshold_compact_scalar(const float* input, float* output, std::size_t size,
                                                             float threshold, float scale) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < size; ++i) {
    if (input[i] > threshold) {
      output[count] = input[i] * scale;
      ++count;
    }
  }
  return count;
}

[[maybe_unused]] void hamming2d_complex_filter_scalar(const ksj::base::cf32* input, const float* weights,
                                                      ksj::base::cf32* output, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    output[i] = input[i] * weights[i];
  }
}

[[maybe_unused]] void signal_fir_8tap_convolve_scalar(const float* signal, const float* kernel, float* output,
                                                      std::size_t output_size) {
  for (std::size_t i = 0; i < output_size; ++i) {
    float sum{};
    for (std::size_t tap = 0; tap < 8U; ++tap) {
      sum += signal[i + tap] * kernel[tap];
    }
    output[i] = sum;
  }
}

[[maybe_unused]] void roi_stride_materialize_scalar(const Image<float>& input, Vector<float>& output,
                                                    std::size_t col_offset, std::size_t roi_width) {
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < roi_width; ++col) {
      output((row * roi_width) + col) = input(row, col + col_offset);
    }
  }
}

[[maybe_unused]] void five_point_stencil_scalar(const float* input, float* output, std::size_t size) {
  if (size < 5U) {
    for (std::size_t i = 0; i < size; ++i) {
      output[i] = 0.0F;
    }
    return;
  }

  output[0] = 0.0F;
  output[1] = 0.0F;
  output[size - 2U] = 0.0F;
  output[size - 1U] = 0.0F;
  for (std::size_t i = 2; i + 2U < size; ++i) {
    output[i] = input[i] * 0.50F + (input[i - 1U] + input[i + 1U]) * 0.20F + (input[i - 2U] + input[i + 2U]) * 0.05F;
  }
}

[[maybe_unused]] void image_3x3_blur_scalar(const Image<float>& input, Image<float>& output) {
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = 0.0F;
    }
  }

  for (std::size_t row = 1; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1; col + 1U < input.cols(); ++col) {
      const auto center = input(row, col) * 0.25F;
      const auto cardinals =
        (input(row - 1U, col) + input(row + 1U, col) + input(row, col - 1U) + input(row, col + 1U)) * 0.125F;
      const auto diagonals = (input(row - 1U, col - 1U) + input(row - 1U, col + 1U) + input(row + 1U, col - 1U) +
                              input(row + 1U, col + 1U)) *
                             0.0625F;
      output(row, col) = center + cardinals + diagonals;
    }
  }
}

[[maybe_unused]] void image_transpose_scatter_scalar(const Image<float>& input, Image<float>& output) {
  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 0; col < input.cols(); ++col) {
      output(col, row) = input(row, col);
    }
  }
}

[[nodiscard]] std::size_t clamp_extent(std::ptrdiff_t value, std::size_t extent) {
  if (value < 0) {
    return 0;
  }
  const auto index = static_cast<std::size_t>(value);
  return index < extent ? index : extent - 1U;
}

[[nodiscard]] float wrap_to_pi_scalar(float value) {
  if (value > kPi) {
    value -= kTwoPi;
  } else if (value < -kPi) {
    value += kTwoPi;
  }
  return value;
}

[[nodiscard]] float boundary_blend_pixel(const float* tile, std::size_t row, std::size_t col, std::size_t rows,
                                         std::size_t cols) {
  const auto up = clamp_extent(static_cast<std::ptrdiff_t>(row) - 1, rows);
  const auto down = clamp_extent(static_cast<std::ptrdiff_t>(row) + 1, rows);
  const auto left = clamp_extent(static_cast<std::ptrdiff_t>(col) - 1, cols);
  const auto right = clamp_extent(static_cast<std::ptrdiff_t>(col) + 1, cols);
  const auto center_index = (row * cols) + col;
  return (tile[center_index] * 0.50F) + ((tile[(up * cols) + col] + tile[(down * cols) + col] +
                                          tile[(row * cols) + left] + tile[(row * cols) + right]) *
                                         0.125F);
}

[[maybe_unused]] void small_tile_boundary_blend_scalar(const float* input, float* output, std::size_t tile_count,
                                                       std::size_t tile_rows, std::size_t tile_cols) {
  const auto tile_size = tile_rows * tile_cols;
  for (std::size_t tile_index = 0; tile_index < tile_count; ++tile_index) {
    const auto* tile_input = input + (tile_index * tile_size);
    auto* tile_output = output + (tile_index * tile_size);
    for (std::size_t row = 0; row < tile_rows; ++row) {
      for (std::size_t col = 0; col < tile_cols; ++col) {
        tile_output[(row * tile_cols) + col] = boundary_blend_pixel(tile_input, row, col, tile_rows, tile_cols);
      }
    }
  }
}

[[maybe_unused]] void mask_erode_cross_2d_scalar(const Image<float>& input, Image<float>& output) {
  std::fill(output.data(), output.data() + output.size(), 0.0F);
  for (std::size_t row = 1; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1; col + 1U < input.cols(); ++col) {
      const bool keep = input(row, col) > 0.5F && input(row - 1U, col) > 0.5F && input(row + 1U, col) > 0.5F &&
                        input(row, col - 1U) > 0.5F && input(row, col + 1U) > 0.5F;
      output(row, col) = keep ? 1.0F : 0.0F;
    }
  }
}

[[maybe_unused]] void separable_3tap_filter_2d_scalar(const Image<float>& input, Image<float>& temp,
                                                      Image<float>& output) {
  std::fill(temp.data(), temp.data() + temp.size(), 0.0F);
  std::fill(output.data(), output.data() + output.size(), 0.0F);

  for (std::size_t row = 0; row < input.rows(); ++row) {
    for (std::size_t col = 1; col + 1U < input.cols(); ++col) {
      temp(row, col) = (input(row, col - 1U) * 0.25F) + (input(row, col) * 0.50F) + (input(row, col + 1U) * 0.25F);
    }
  }

  for (std::size_t row = 1; row + 1U < input.rows(); ++row) {
    for (std::size_t col = 1; col + 1U < input.cols(); ++col) {
      output(row, col) = (temp(row - 1U, col) * 0.25F) + (temp(row, col) * 0.50F) + (temp(row + 1U, col) * 0.25F);
    }
  }
}

[[nodiscard]] constexpr std::size_t volume_offset(std::size_t x, std::size_t y, std::size_t z, std::size_t rows,
                                                  std::size_t cols) noexcept {
  return ((z * rows) + y) * cols + x;
}

[[nodiscard]] constexpr std::size_t volume_column_major_offset(std::size_t row, std::size_t col, std::size_t slice,
                                                               std::size_t rows, std::size_t cols) noexcept {
  return (slice * rows * cols) + (col * rows) + row;
}

[[maybe_unused]] void phase_quality_3d_scalar(const float* phase, float* quality, std::size_t rows, std::size_t cols,
                                              std::size_t slices) {
  const auto total_size = rows * cols * slices;
  std::fill(quality, quality + total_size, 0.0F);

  for (std::size_t z = 1; z + 1U < slices; ++z) {
    for (std::size_t y = 1; y + 1U < rows; ++y) {
      for (std::size_t x = 1; x + 1U < cols; ++x) {
        const auto center_index = volume_offset(x, y, z, rows, cols);
        const auto center = phase[center_index];
        const auto horizontal =
          wrap_to_pi_scalar(phase[center_index - 1U] - center) - wrap_to_pi_scalar(center - phase[center_index + 1U]);
        const auto vertical = wrap_to_pi_scalar(phase[center_index - cols] - center) -
                              wrap_to_pi_scalar(center - phase[center_index + cols]);
        const auto through_plane = wrap_to_pi_scalar(phase[center_index - (rows * cols)] - center) -
                                   wrap_to_pi_scalar(center - phase[center_index + (rows * cols)]);
        quality[center_index] =
          1.0F / (std::sqrt((horizontal * horizontal) + (vertical * vertical) + (through_plane * through_plane)) +
                  std::numeric_limits<float>::epsilon());
      }
    }
  }
}

[[maybe_unused]] void volume_7point_stencil_scalar(const float* input, float* output, std::size_t rows,
                                                   std::size_t cols, std::size_t slices) {
  const auto total_size = rows * cols * slices;
  std::fill(output, output + total_size, 0.0F);

  for (std::size_t z = 1; z + 1U < slices; ++z) {
    for (std::size_t y = 1; y + 1U < rows; ++y) {
      for (std::size_t x = 1; x + 1U < cols; ++x) {
        const auto center = volume_offset(x, y, z, rows, cols);
        output[center] = (input[center] * 0.40F) + ((input[center - 1U] + input[center + 1U]) * 0.10F) +
                         ((input[center - cols] + input[center + cols]) * 0.10F) +
                         ((input[center - (rows * cols)] + input[center + (rows * cols)]) * 0.10F);
      }
    }
  }
}

[[maybe_unused]] void volume_zpad_scale_scalar(const ksj::base::cf32* input, ksj::base::cf32* output, std::size_t rows,
                                               std::size_t cols, std::size_t slices, std::size_t padded_rows,
                                               std::size_t padded_cols, std::size_t padded_slices, float scale) {
  std::fill(output, output + (padded_rows * padded_cols * padded_slices), ksj::base::cf32{});

  const auto row_offset = (padded_rows - rows) / 2U;
  const auto col_offset = (padded_cols - cols) / 2U;
  const auto slice_offset = (padded_slices - slices) / 2U;
  for (std::size_t z = 0; z < slices; ++z) {
    for (std::size_t col = 0; col < cols; ++col) {
      const auto* source = input + volume_column_major_offset(0, col, z, rows, cols);
      auto* destination =
        output + volume_column_major_offset(row_offset, col + col_offset, z + slice_offset, padded_rows, padded_cols);
      for (std::size_t row = 0; row < rows; ++row) {
        destination[row] = source[row] * scale;
      }
    }
  }
}

[[maybe_unused]] void weighted_coil_sum_scalar(const Image<float>& input, const Vector<float>& weights,
                                               Vector<float>& output) {
  for (std::size_t voxel = 0; voxel < input.rows(); ++voxel) {
    float sum{};
    for (std::size_t coil = 0; coil < input.cols(); ++coil) {
      sum += input(voxel, coil) * weights(coil);
    }
    output(voxel) = sum;
  }
}

[[maybe_unused]] void int16_affine_clamp_scalar(const std::int16_t* input, std::int16_t* output, std::size_t size,
                                                std::int16_t scale, std::int16_t bias, std::int16_t lower,
                                                std::int16_t upper) {
  for (std::size_t i = 0; i < size; ++i) {
    auto value = static_cast<int>(input[i]) * static_cast<int>(scale) + static_cast<int>(bias);
    value = std::max(value, static_cast<int>(lower));
    value = std::min(value, static_cast<int>(upper));
    output[i] = static_cast<std::int16_t>(value);
  }
}

[[maybe_unused]] void int32_affine_clamp_scalar(const std::int32_t* input, std::int32_t* output, std::size_t size,
                                                std::int32_t scale, std::int32_t bias, std::int32_t lower,
                                                std::int32_t upper) {
  for (std::size_t i = 0; i < size; ++i) {
    auto value = input[i] * scale + bias;
    value = value < lower ? lower : value;
    value = value > upper ? upper : value;
    output[i] = value;
  }
}

#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)

constexpr __mmask16 kComplexRealLaneMask = 0x5555;
constexpr __mmask16 kComplexImagLaneMask = 0xAAAA;

[[nodiscard]] __m512 wrap_to_pi_avx512(__m512 values) {
  const auto pi = _mm512_set1_ps(kPi);
  const auto negative_pi = _mm512_set1_ps(-kPi);
  const auto two_pi = _mm512_set1_ps(kTwoPi);
  const auto lower_mask = _mm512_cmp_ps_mask(values, negative_pi, _CMP_LT_OQ);
  const auto upper_mask = _mm512_cmp_ps_mask(values, pi, _CMP_GT_OQ);
  values = _mm512_mask_add_ps(values, lower_mask, values, two_pi);
  values = _mm512_mask_sub_ps(values, upper_mask, values, two_pi);
  return values;
}

void classic_fma_triad_avx512(const float* KSJ_RESEARCH_RESTRICT lhs, const float* KSJ_RESEARCH_RESTRICT rhs,
                              float* KSJ_RESEARCH_RESTRICT output, std::size_t size, float alpha) {
  const auto alpha_value = _mm512_set1_ps(alpha);
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto lhs_values = _mm512_load_ps(lhs + i);
    const auto rhs_values = _mm512_load_ps(rhs + i);
    _mm512_store_ps(output + i, _mm512_fmadd_ps(alpha_value, lhs_values, rhs_values));
  }
  classic_fma_triad_scalar(lhs + i, rhs + i, output + i, size - i, alpha);
}

float classic_dot_avx512(const float* lhs, const float* rhs, std::size_t size) {
  auto sum = _mm512_setzero_ps();
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto lhs_values = _mm512_load_ps(lhs + i);
    const auto rhs_values = _mm512_load_ps(rhs + i);
    sum = _mm512_fmadd_ps(lhs_values, rhs_values, sum);
  }

  alignas(64) float lanes[16];
  _mm512_store_ps(lanes, sum);
  float value{};
  for (const float lane : lanes) {
    value += lane;
  }
  for (; i < size; ++i) {
    value += lhs[i] * rhs[i];
  }
  return value;
}

void classic_fma_triad_f64_avx512(const double* KSJ_RESEARCH_RESTRICT lhs, const double* KSJ_RESEARCH_RESTRICT rhs,
                                  double* KSJ_RESEARCH_RESTRICT output, std::size_t size, double alpha) {
  const auto alpha_value = _mm512_set1_pd(alpha);
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto lhs_values = _mm512_load_pd(lhs + i);
    const auto rhs_values = _mm512_load_pd(rhs + i);
    _mm512_store_pd(output + i, _mm512_fmadd_pd(alpha_value, lhs_values, rhs_values));
  }
  classic_fma_triad_f64_scalar(lhs + i, rhs + i, output + i, size - i, alpha);
}

double classic_dot_f64_avx512(const double* lhs, const double* rhs, std::size_t size) {
  auto sum = _mm512_setzero_pd();
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto lhs_values = _mm512_load_pd(lhs + i);
    const auto rhs_values = _mm512_load_pd(rhs + i);
    sum = _mm512_fmadd_pd(lhs_values, rhs_values, sum);
  }

  alignas(64) double lanes[8];
  _mm512_store_pd(lanes, sum);
  double value{};
  for (const double lane : lanes) {
    value += lane;
  }
  for (; i < size; ++i) {
    value += lhs[i] * rhs[i];
  }
  return value;
}

void waterfat_weight_gate_avx512(const float* KSJ_RESEARCH_RESTRICT buffer_x,
                                 const float* KSJ_RESEARCH_RESTRICT buffer_y,
                                 const float* KSJ_RESEARCH_RESTRICT weight_in, float* KSJ_RESEARCH_RESTRICT weight_out,
                                 std::size_t size, float threshold) {
  const auto threshold_value = _mm512_set1_ps(threshold);
  const auto epsilon_value = _mm512_set1_ps(0.000001F);
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto x = _mm512_load_ps(buffer_x + i);
    const auto y = _mm512_load_ps(buffer_y + i);
    const auto weight = _mm512_load_ps(weight_in + i);
    const auto squared = _mm512_mul_ps(weight, weight);
    const auto clipped_mask = static_cast<__mmask16>(_mm512_cmp_ps_mask(x, threshold_value, _CMP_GT_OQ) |
                                                     _mm512_cmp_ps_mask(y, threshold_value, _CMP_GT_OQ));
    _mm512_store_ps(weight_out + i, _mm512_mask_mov_ps(squared, clipped_mask, epsilon_value));
  }
  waterfat_weight_gate_scalar(buffer_x + i, buffer_y + i, weight_in + i, weight_out + i, size - i, threshold);
}

void prewhiten_covariance_pair_avx512(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, std::size_t sample_count,
                                      float& real_sum, float& imag_sum) {
  const auto conjugate_imag_sign =
    _mm512_castsi512_ps(_mm512_set1_epi64(static_cast<long long>(0x8000000000000000ULL)));
  auto sum = _mm512_setzero_ps();

  std::size_t sample = 0;
  for (; sample + 8U <= sample_count; sample += 8U) {
    const auto lhs_values = _mm512_load_ps(reinterpret_cast<const float*>(lhs + sample));
    const auto rhs_values = _mm512_load_ps(reinterpret_cast<const float*>(rhs + sample));
    const auto lhs_conjugate = _mm512_xor_ps(lhs_values, conjugate_imag_sign);
    const auto lhs_real = _mm512_moveldup_ps(lhs_conjugate);
    const auto lhs_imag = _mm512_movehdup_ps(lhs_conjugate);
    const auto rhs_swapped = _mm512_permute_ps(rhs_values, 0xB1);
    sum = _mm512_add_ps(sum, _mm512_fmaddsub_ps(lhs_real, rhs_values, _mm512_mul_ps(lhs_imag, rhs_swapped)));
  }

  alignas(64) float lanes[16];
  _mm512_store_ps(lanes, sum);
  float real{};
  float imag{};
  for (std::size_t lane = 0; lane < 16U; lane += 2U) {
    real += lanes[lane];
    imag += lanes[lane + 1U];
  }
  for (; sample < sample_count; ++sample) {
    real += lhs[sample].real() * rhs[sample].real() + lhs[sample].imag() * rhs[sample].imag();
    imag += lhs[sample].real() * rhs[sample].imag() - lhs[sample].imag() * rhs[sample].real();
  }
  real_sum = real;
  imag_sum = imag;
}

void prewhiten_channel_mix_avx512(const Image<ksj::base::cf32>& input, const Image<ksj::base::cf32>& lower_triangle,
                                  Image<ksj::base::cf32>& output) {
  std::fill(output.data(), output.data() + output.size(), ksj::base::cf32{});

  for (std::size_t output_channel = 0; output_channel < output.rows(); ++output_channel) {
    auto* output_values = reinterpret_cast<float*>(&output(output_channel, 0));
    for (std::size_t input_channel = 0; input_channel <= output_channel; ++input_channel) {
      const auto coeff = lower_triangle(output_channel, input_channel);
      const auto coeff_real = coeff.real();
      const auto coeff_imag = coeff.imag();
      const auto coeff_pairs =
        _mm512_set_ps(coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real,
                      coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real);
      const auto coeff_swapped =
        _mm512_set_ps(coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag,
                      coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag, coeff_real, coeff_imag);
      const auto* input_values = reinterpret_cast<const float*>(&input(input_channel, 0));

      std::size_t sample = 0;
      for (; sample + 8U <= input.cols(); sample += 8U) {
        const auto values = _mm512_loadu_ps(input_values + (sample * 2U));
        const auto real = _mm512_moveldup_ps(values);
        const auto imag = _mm512_movehdup_ps(values);
        const auto product = _mm512_fmaddsub_ps(real, coeff_pairs, _mm512_mul_ps(imag, coeff_swapped));
        const auto accumulated = _mm512_add_ps(_mm512_loadu_ps(output_values + (sample * 2U)), product);
        _mm512_storeu_ps(output_values + (sample * 2U), accumulated);
      }

      for (; sample < input.cols(); ++sample) {
        output(output_channel, sample) += coeff * input(input_channel, sample);
      }
    }
  }
}

void rss_coil_combine_avx512(const Image<ksj::base::cf32>& input, Vector<float>& output) {
  for (std::size_t voxel = 0; voxel < input.rows(); ++voxel) {
    const auto* values = reinterpret_cast<const float*>(&input(voxel, 0));
    const auto value_count = input.cols() * 2U;
    auto sum = _mm512_setzero_ps();
    std::size_t i = 0;
    for (; i + 16U <= value_count; i += 16U) {
      const auto loaded = _mm512_loadu_ps(values + i);
      sum = _mm512_fmadd_ps(loaded, loaded, sum);
    }

    alignas(64) float lanes[16];
    _mm512_store_ps(lanes, sum);
    float scalar_sum{};
    for (const float lane : lanes) {
      scalar_sum += lane;
    }
    for (; i < value_count; ++i) {
      scalar_sum += values[i] * values[i];
    }
    output(voxel) = std::sqrt(scalar_sum);
  }
}

void sum_and_sumsq_avx512(const float* input, std::size_t size, float& sum, float& sumsq) {
  auto sum_values = _mm512_setzero_ps();
  auto sumsq_values = _mm512_setzero_ps();
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto values = _mm512_load_ps(input + i);
    sum_values = _mm512_add_ps(sum_values, values);
    sumsq_values = _mm512_fmadd_ps(values, values, sumsq_values);
  }

  alignas(64) float sum_lanes[16];
  alignas(64) float sumsq_lanes[16];
  _mm512_store_ps(sum_lanes, sum_values);
  _mm512_store_ps(sumsq_lanes, sumsq_values);
  float local_sum{};
  float local_sumsq{};
  for (std::size_t lane = 0; lane < 16U; ++lane) {
    local_sum += sum_lanes[lane];
    local_sumsq += sumsq_lanes[lane];
  }
  for (; i < size; ++i) {
    local_sum += input[i];
    local_sumsq += input[i] * input[i];
  }
  sum = local_sum;
  sumsq = local_sumsq;
}

void min_max_avx512(const float* input, std::size_t size, float& min_value, float& max_value) {
  auto min_values = _mm512_set1_ps(std::numeric_limits<float>::infinity());
  auto max_values = _mm512_set1_ps(-std::numeric_limits<float>::infinity());
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto values = _mm512_load_ps(input + i);
    min_values = _mm512_min_ps(min_values, values);
    max_values = _mm512_max_ps(max_values, values);
  }

  alignas(64) float min_lanes[16];
  alignas(64) float max_lanes[16];
  _mm512_store_ps(min_lanes, min_values);
  _mm512_store_ps(max_lanes, max_values);
  auto local_min = std::numeric_limits<float>::infinity();
  auto local_max = -std::numeric_limits<float>::infinity();
  for (std::size_t lane = 0; lane < 16U; ++lane) {
    local_min = std::min(local_min, min_lanes[lane]);
    local_max = std::max(local_max, max_lanes[lane]);
  }
  for (; i < size; ++i) {
    local_min = std::min(local_min, input[i]);
    local_max = std::max(local_max, input[i]);
  }
  min_value = local_min;
  max_value = local_max;
}

void fused_axpby_relu_avx512(const float* KSJ_RESEARCH_RESTRICT lhs, const float* KSJ_RESEARCH_RESTRICT rhs,
                             float* KSJ_RESEARCH_RESTRICT output, std::size_t size, float alpha, float beta,
                             float bias) {
  const auto alpha_value = _mm512_set1_ps(alpha);
  const auto beta_value = _mm512_set1_ps(beta);
  const auto bias_value = _mm512_set1_ps(bias);
  const auto zero = _mm512_setzero_ps();

  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto lhs_values = _mm512_load_ps(lhs + i);
    const auto rhs_values = _mm512_load_ps(rhs + i);
    const auto value =
      _mm512_add_ps(_mm512_fmadd_ps(alpha_value, lhs_values, _mm512_mul_ps(beta_value, rhs_values)), bias_value);
    _mm512_store_ps(output + i, _mm512_max_ps(value, zero));
  }
  fused_axpby_relu_scalar(lhs + i, rhs + i, output + i, size - i, alpha, beta, bias);
}

float l2_distance_avx512(const float* lhs, const float* rhs, std::size_t size) {
  auto sum = _mm512_setzero_ps();
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto lhs_values = _mm512_load_ps(lhs + i);
    const auto rhs_values = _mm512_load_ps(rhs + i);
    const auto delta = _mm512_sub_ps(lhs_values, rhs_values);
    sum = _mm512_fmadd_ps(delta, delta, sum);
  }

  alignas(64) float lanes[16];
  _mm512_store_ps(lanes, sum);
  float value{};
  for (const float lane : lanes) {
    value += lane;
  }
  for (; i < size; ++i) {
    const auto delta = lhs[i] - rhs[i];
    value += delta * delta;
  }
  return value;
}

void complex_mul_aos_avx512(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, ksj::base::cf32* output,
                            std::size_t size) {
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto lhs_values = _mm512_load_ps(reinterpret_cast<const float*>(lhs + i));
    const auto rhs_values = _mm512_load_ps(reinterpret_cast<const float*>(rhs + i));
    const auto lhs_real = _mm512_moveldup_ps(lhs_values);
    const auto lhs_imag = _mm512_movehdup_ps(lhs_values);
    const auto rhs_swapped = _mm512_permute_ps(rhs_values, 0xB1);
    const auto output_values = _mm512_fmaddsub_ps(lhs_real, rhs_values, _mm512_mul_ps(lhs_imag, rhs_swapped));
    _mm512_store_ps(reinterpret_cast<float*>(output + i), output_values);
  }
  complex_mul_aos_scalar(lhs + i, rhs + i, output + i, size - i);
}

void complex_conj_mul_aos_avx512(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, ksj::base::cf32* output,
                                 std::size_t size) {
  const auto conjugate_imag_sign =
    _mm512_castsi512_ps(_mm512_set1_epi64(static_cast<long long>(0x8000000000000000ULL)));
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto lhs_values = _mm512_loadu_ps(reinterpret_cast<const float*>(lhs + i));
    const auto rhs_values =
      _mm512_xor_ps(_mm512_loadu_ps(reinterpret_cast<const float*>(rhs + i)), conjugate_imag_sign);
    const auto lhs_real = _mm512_moveldup_ps(lhs_values);
    const auto lhs_imag = _mm512_movehdup_ps(lhs_values);
    const auto rhs_swapped = _mm512_permute_ps(rhs_values, 0xB1);
    const auto output_values = _mm512_fmaddsub_ps(lhs_real, rhs_values, _mm512_mul_ps(lhs_imag, rhs_swapped));
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i), output_values);
  }
  complex_conj_mul_aos_scalar(lhs + i, rhs + i, output + i, size - i);
}

void complex_normalize_avx512(const ksj::base::cf32* input, ksj::base::cf32* output, std::size_t size) {
  const auto epsilon = _mm512_set1_ps(0.000001F);
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto values = _mm512_loadu_ps(reinterpret_cast<const float*>(input + i));
    const auto real = _mm512_moveldup_ps(values);
    const auto imag = _mm512_movehdup_ps(values);
    const auto magnitude =
      _mm512_max_ps(_mm512_sqrt_ps(_mm512_fmadd_ps(real, real, _mm512_mul_ps(imag, imag))), epsilon);
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i), _mm512_div_ps(values, magnitude));
  }
  complex_normalize_scalar(input + i, output + i, size - i);
}

void complex_magnitude_squared_avx512(const ksj::base::cf32* input, float* output, std::size_t size) {
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto values = _mm512_load_ps(reinterpret_cast<const float*>(input + i));
    const auto real = _mm512_moveldup_ps(values);
    const auto imag = _mm512_movehdup_ps(values);
    const auto magnitude = _mm512_fmadd_ps(real, real, _mm512_mul_ps(imag, imag));
    _mm512_mask_compressstoreu_ps(output + i, 0x5555, magnitude);
  }
  complex_magnitude_squared_scalar(input + i, output + i, size - i);
}

void complex_deinterleave_avx512(const ksj::base::cf32* input, float* real, float* imag, std::size_t size) {
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto values = _mm512_load_ps(reinterpret_cast<const float*>(input + i));
    _mm512_mask_compressstoreu_ps(real + i, kComplexRealLaneMask, values);
    _mm512_mask_compressstoreu_ps(imag + i, kComplexImagLaneMask, values);
  }
  complex_deinterleave_scalar(input + i, real + i, imag + i, size - i);
}

void view_copy_real_component_avx512(const ksj::base::cf32* source, const ksj::base::cf32* destination,
                                     ksj::base::cf32* output, std::size_t size) {
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto source_values = _mm512_loadu_ps(reinterpret_cast<const float*>(source + i));
    const auto destination_values = _mm512_loadu_ps(reinterpret_cast<const float*>(destination + i));
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i),
                     _mm512_mask_blend_ps(kComplexRealLaneMask, destination_values, source_values));
  }
  view_copy_real_component_scalar(source + i, destination + i, output + i, size - i);
}

void view_copy_imag_component_avx512(const ksj::base::cf32* source, const ksj::base::cf32* destination,
                                     ksj::base::cf32* output, std::size_t size) {
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto source_values = _mm512_loadu_ps(reinterpret_cast<const float*>(source + i));
    const auto destination_values = _mm512_loadu_ps(reinterpret_cast<const float*>(destination + i));
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i),
                     _mm512_mask_blend_ps(kComplexImagLaneMask, destination_values, source_values));
  }
  view_copy_imag_component_scalar(source + i, destination + i, output + i, size - i);
}

void complex_discard_imag_clamp_avx512(const ksj::base::cf32* input, ksj::base::cf32* output, std::size_t size) {
  const auto zero = _mm512_setzero_ps();
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto values = _mm512_loadu_ps(reinterpret_cast<const float*>(input + i));
    const auto clamped = _mm512_max_ps(values, zero);
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i), _mm512_mask_blend_ps(kComplexRealLaneMask, zero, clamped));
  }
  complex_discard_imag_clamp_scalar(input + i, output + i, size - i);
}

void phase_difference_avx512(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, ksj::base::cf32* output,
                             std::size_t size) {
  const auto one = _mm512_set1_ps(1.0F);
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto lhs_values = _mm512_loadu_ps(reinterpret_cast<const float*>(lhs + i));
    const auto rhs_values = _mm512_loadu_ps(reinterpret_cast<const float*>(rhs + i));
    const auto diff = _mm512_sub_ps(lhs_values, rhs_values);
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i), _mm512_mask_blend_ps(kComplexRealLaneMask, diff, one));
  }
  phase_difference_scalar(lhs + i, rhs + i, output + i, size - i);
}

void phase_unwrap_1p5_avx512(const ksj::base::cf32* input, ksj::base::cf32* output, std::size_t size) {
  const auto lower_threshold = _mm512_set1_ps(-kPhaseUnwrapThreshold);
  const auto upper_threshold = _mm512_set1_ps(kPhaseUnwrapThreshold);
  const auto two_pi = _mm512_set1_ps(kTwoPi);
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    auto values = _mm512_loadu_ps(reinterpret_cast<const float*>(input + i));
    const auto lower_mask =
      static_cast<__mmask16>(_mm512_cmp_ps_mask(values, lower_threshold, _CMP_LT_OQ) & kComplexImagLaneMask);
    const auto upper_mask =
      static_cast<__mmask16>(_mm512_cmp_ps_mask(values, upper_threshold, _CMP_GT_OQ) & kComplexImagLaneMask);
    values = _mm512_mask_add_ps(values, lower_mask, values, two_pi);
    values = _mm512_mask_sub_ps(values, upper_mask, values, two_pi);
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i), values);
  }
  phase_unwrap_1p5_scalar(input + i, output + i, size - i);
}

void sdat_complex_magnitude_avx512(const float* interleaved_complex, float* output, std::size_t complex_count) {
  std::size_t i = 0;
  for (; i + 8U <= complex_count; i += 8U) {
    const auto values = _mm512_loadu_ps(interleaved_complex + (i * 2U));
    const auto real = _mm512_moveldup_ps(values);
    const auto imag = _mm512_movehdup_ps(values);
    const auto magnitude = _mm512_sqrt_ps(_mm512_fmadd_ps(real, real, _mm512_mul_ps(imag, imag)));
    _mm512_mask_compressstoreu_ps(output + i, 0x5555, magnitude);
  }
  sdat_complex_magnitude_scalar(interleaved_complex + (i * 2U), output + i, complex_count - i);
}

void hamming2d_complex_filter_avx512(const ksj::base::cf32* input, const float* weights, ksj::base::cf32* output,
                                     std::size_t size) {
  const auto offsets = _mm512_set_epi32(7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0);
  std::size_t i = 0;
  for (; i + 8U <= size; i += 8U) {
    const auto values = _mm512_loadu_ps(reinterpret_cast<const float*>(input + i));
    const auto weight_pairs = _mm512_i32gather_ps(offsets, weights + i, 4);
    _mm512_storeu_ps(reinterpret_cast<float*>(output + i), _mm512_mul_ps(values, weight_pairs));
  }
  hamming2d_complex_filter_scalar(input + i, weights + i, output + i, size - i);
}

float gather_weighted_sum_avx512(const float* table, const std::int32_t* indices, const float* weights,
                                 std::size_t size) {
  auto sum = _mm512_setzero_ps();
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto index_values = _mm512_load_si512(reinterpret_cast<const __m512i*>(indices + i));
    const auto gathered = _mm512_i32gather_ps(index_values, table, 4);
    const auto weight_values = _mm512_load_ps(weights + i);
    sum = _mm512_fmadd_ps(gathered, weight_values, sum);
  }

  alignas(64) float lanes[16];
  _mm512_store_ps(lanes, sum);
  float value{};
  for (const float lane : lanes) {
    value += lane;
  }
  for (; i < size; ++i) {
    value += table[indices[i]] * weights[i];
  }
  return value;
}

void strided_scale_pack_avx512(const float* input, float* output, std::size_t size, float scale) {
  const auto scale_value = _mm512_set1_ps(scale);
  const auto offsets = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto values = _mm512_i32gather_ps(offsets, input + (i * 2U) + 1U, 4);
    _mm512_store_ps(output + i, _mm512_mul_ps(values, scale_value));
  }
  strided_scale_pack_scalar(input + (i * 2U), output + i, size - i, scale);
}

void image_threshold_avx512(const float* input, float* output, std::size_t size, float threshold, float low_value,
                            float high_value) {
  const auto threshold_values = _mm512_set1_ps(threshold);
  const auto low_values = _mm512_set1_ps(low_value);
  const auto high_values = _mm512_set1_ps(high_value);
  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto values = _mm512_loadu_ps(input + i);
    const auto mask = _mm512_cmp_ps_mask(values, threshold_values, _CMP_GE_OQ);
    _mm512_storeu_ps(output + i, _mm512_mask_mov_ps(low_values, mask, high_values));
  }
  image_threshold_scalar(input + i, output + i, size - i, threshold, low_value, high_value);
}

std::size_t masked_threshold_compact_avx512(const float* input, float* output, std::size_t size, float threshold,
                                            float scale) {
  const auto threshold_values = _mm512_set1_ps(threshold);
  const auto scale_values = _mm512_set1_ps(scale);
  std::size_t i = 0;
  std::size_t count = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto values = _mm512_loadu_ps(input + i);
    const auto keep_mask = _mm512_cmp_ps_mask(values, threshold_values, _CMP_GT_OQ);
    _mm512_mask_compressstoreu_ps(output + count, keep_mask, _mm512_mul_ps(values, scale_values));
    count += static_cast<std::size_t>(__builtin_popcount(static_cast<unsigned>(keep_mask)));
  }
  for (; i < size; ++i) {
    if (input[i] > threshold) {
      output[count] = input[i] * scale;
      ++count;
    }
  }
  return count;
}

void signal_fir_8tap_convolve_avx512(const float* signal, const float* kernel, float* output, std::size_t output_size) {
  const auto k0 = _mm512_set1_ps(kernel[0]);
  const auto k1 = _mm512_set1_ps(kernel[1]);
  const auto k2 = _mm512_set1_ps(kernel[2]);
  const auto k3 = _mm512_set1_ps(kernel[3]);
  const auto k4 = _mm512_set1_ps(kernel[4]);
  const auto k5 = _mm512_set1_ps(kernel[5]);
  const auto k6 = _mm512_set1_ps(kernel[6]);
  const auto k7 = _mm512_set1_ps(kernel[7]);
  std::size_t i = 0;
  for (; i + 16U <= output_size; i += 16U) {
    auto sum = _mm512_mul_ps(_mm512_loadu_ps(signal + i), k0);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 1U), k1, sum);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 2U), k2, sum);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 3U), k3, sum);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 4U), k4, sum);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 5U), k5, sum);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 6U), k6, sum);
    sum = _mm512_fmadd_ps(_mm512_loadu_ps(signal + i + 7U), k7, sum);
    _mm512_storeu_ps(output + i, sum);
  }
  signal_fir_8tap_convolve_scalar(signal + i, kernel, output + i, output_size - i);
}

void roi_stride_materialize_avx512(const Image<float>& input, Vector<float>& output, std::size_t col_offset,
                                   std::size_t roi_width) {
  for (std::size_t row = 0; row < input.rows(); ++row) {
    const auto* source = &input(row, col_offset);
    auto* destination = output.data() + (row * roi_width);
    std::size_t col = 0;
    for (; col + 16U <= roi_width; col += 16U) {
      _mm512_storeu_ps(destination + col, _mm512_loadu_ps(source + col));
    }
    for (; col < roi_width; ++col) {
      destination[col] = source[col];
    }
  }
}

void image_transpose_scatter_avx512(const Image<float>& input, Image<float>& output) {
  const auto row_count = static_cast<int>(input.rows());
  const auto offsets = _mm512_set_epi32(15 * row_count, 14 * row_count, 13 * row_count, 12 * row_count, 11 * row_count,
                                        10 * row_count, 9 * row_count, 8 * row_count, 7 * row_count, 6 * row_count,
                                        5 * row_count, 4 * row_count, 3 * row_count, 2 * row_count, row_count, 0);
  for (std::size_t row = 0; row < input.rows(); ++row) {
    std::size_t col = 0;
    for (; col + 16U <= input.cols(); col += 16U) {
      const auto values = _mm512_loadu_ps(&input(row, col));
      _mm512_i32scatter_ps(&output(col, row), offsets, values, 4);
    }
    for (; col < input.cols(); ++col) {
      output(col, row) = input(row, col);
    }
  }
}

void five_point_stencil_avx512(const float* input, float* output, std::size_t size) {
  if (size < 5U) {
    five_point_stencil_scalar(input, output, size);
    return;
  }

  const auto center_weight = _mm512_set1_ps(0.50F);
  const auto neighbor_weight = _mm512_set1_ps(0.20F);
  const auto outer_weight = _mm512_set1_ps(0.05F);

  output[0] = 0.0F;
  output[1] = 0.0F;
  output[size - 2U] = 0.0F;
  output[size - 1U] = 0.0F;

  std::size_t i = 2;
  for (; i + 16U <= size - 2U; i += 16U) {
    const auto left2 = _mm512_loadu_ps(input + i - 2U);
    const auto left1 = _mm512_loadu_ps(input + i - 1U);
    const auto center = _mm512_loadu_ps(input + i);
    const auto right1 = _mm512_loadu_ps(input + i + 1U);
    const auto right2 = _mm512_loadu_ps(input + i + 2U);
    const auto neighbors = _mm512_add_ps(left1, right1);
    const auto outers = _mm512_add_ps(left2, right2);
    auto value = _mm512_mul_ps(center, center_weight);
    value = _mm512_fmadd_ps(neighbors, neighbor_weight, value);
    value = _mm512_fmadd_ps(outers, outer_weight, value);
    _mm512_storeu_ps(output + i, value);
  }

  for (; i + 2U < size; ++i) {
    output[i] = input[i] * 0.50F + (input[i - 1U] + input[i + 1U]) * 0.20F + (input[i - 2U] + input[i + 2U]) * 0.05F;
  }
}

void image_3x3_blur_avx512(const Image<float>& input, Image<float>& output) {
  for (std::size_t row = 0; row < output.rows(); ++row) {
    for (std::size_t col = 0; col < output.cols(); ++col) {
      output(row, col) = 0.0F;
    }
  }

  const auto center_weight = _mm512_set1_ps(0.25F);
  const auto cardinal_weight = _mm512_set1_ps(0.125F);
  const auto diagonal_weight = _mm512_set1_ps(0.0625F);

  for (std::size_t row = 1; row + 1U < input.rows(); ++row) {
    std::size_t col = 1;
    for (; col + 16U <= input.cols() - 1U; col += 16U) {
      const auto top_left = _mm512_loadu_ps(&input(row - 1U, col - 1U));
      const auto top = _mm512_loadu_ps(&input(row - 1U, col));
      const auto top_right = _mm512_loadu_ps(&input(row - 1U, col + 1U));
      const auto left = _mm512_loadu_ps(&input(row, col - 1U));
      const auto center = _mm512_loadu_ps(&input(row, col));
      const auto right = _mm512_loadu_ps(&input(row, col + 1U));
      const auto bottom_left = _mm512_loadu_ps(&input(row + 1U, col - 1U));
      const auto bottom = _mm512_loadu_ps(&input(row + 1U, col));
      const auto bottom_right = _mm512_loadu_ps(&input(row + 1U, col + 1U));

      const auto cardinals = _mm512_add_ps(_mm512_add_ps(top, bottom), _mm512_add_ps(left, right));
      const auto diagonals =
        _mm512_add_ps(_mm512_add_ps(top_left, top_right), _mm512_add_ps(bottom_left, bottom_right));
      auto value = _mm512_mul_ps(center, center_weight);
      value = _mm512_fmadd_ps(cardinals, cardinal_weight, value);
      value = _mm512_fmadd_ps(diagonals, diagonal_weight, value);
      _mm512_storeu_ps(&output(row, col), value);
    }
    for (; col + 1U < input.cols(); ++col) {
      const auto center = input(row, col) * 0.25F;
      const auto cardinals =
        (input(row - 1U, col) + input(row + 1U, col) + input(row, col - 1U) + input(row, col + 1U)) * 0.125F;
      const auto diagonals = (input(row - 1U, col - 1U) + input(row - 1U, col + 1U) + input(row + 1U, col - 1U) +
                              input(row + 1U, col + 1U)) *
                             0.0625F;
      output(row, col) = center + cardinals + diagonals;
    }
  }
}

void small_tile_boundary_blend_avx512(const float* input, float* output, std::size_t tile_count, std::size_t tile_rows,
                                      std::size_t tile_cols) {
  if (tile_rows < 3U || tile_cols < 18U) {
    small_tile_boundary_blend_scalar(input, output, tile_count, tile_rows, tile_cols);
    return;
  }

  const auto tile_size = tile_rows * tile_cols;
  const auto center_weight = _mm512_set1_ps(0.50F);
  const auto neighbor_weight = _mm512_set1_ps(0.125F);

  for (std::size_t tile_index = 0; tile_index < tile_count; ++tile_index) {
    const auto* tile_input = input + (tile_index * tile_size);
    auto* tile_output = output + (tile_index * tile_size);

    for (std::size_t col = 0; col < tile_cols; ++col) {
      tile_output[col] = boundary_blend_pixel(tile_input, 0, col, tile_rows, tile_cols);
      tile_output[((tile_rows - 1U) * tile_cols) + col] =
        boundary_blend_pixel(tile_input, tile_rows - 1U, col, tile_rows, tile_cols);
    }

    for (std::size_t row = 1; row + 1U < tile_rows; ++row) {
      tile_output[row * tile_cols] = boundary_blend_pixel(tile_input, row, 0, tile_rows, tile_cols);

      std::size_t col = 1;
      for (; col + 16U <= tile_cols - 1U; col += 16U) {
        const auto center_index = (row * tile_cols) + col;
        const auto center = _mm512_loadu_ps(tile_input + center_index);
        const auto up = _mm512_loadu_ps(tile_input + center_index - tile_cols);
        const auto down = _mm512_loadu_ps(tile_input + center_index + tile_cols);
        const auto left = _mm512_loadu_ps(tile_input + center_index - 1U);
        const auto right = _mm512_loadu_ps(tile_input + center_index + 1U);
        auto value = _mm512_mul_ps(center, center_weight);
        value =
          _mm512_fmadd_ps(_mm512_add_ps(_mm512_add_ps(up, down), _mm512_add_ps(left, right)), neighbor_weight, value);
        _mm512_storeu_ps(tile_output + center_index, value);
      }

      for (; col + 1U < tile_cols; ++col) {
        tile_output[(row * tile_cols) + col] = boundary_blend_pixel(tile_input, row, col, tile_rows, tile_cols);
      }
      tile_output[(row * tile_cols) + tile_cols - 1U] =
        boundary_blend_pixel(tile_input, row, tile_cols - 1U, tile_rows, tile_cols);
    }
  }
}

void mask_erode_cross_2d_avx512(const Image<float>& input, Image<float>& output) {
  std::fill(output.data(), output.data() + output.size(), 0.0F);

  const auto threshold = _mm512_set1_ps(0.5F);
  const auto one = _mm512_set1_ps(1.0F);
  const auto zero = _mm512_setzero_ps();
  for (std::size_t row = 1; row + 1U < input.rows(); ++row) {
    std::size_t col = 1;
    for (; col + 16U <= input.cols() - 1U; col += 16U) {
      auto value = _mm512_loadu_ps(&input(row, col));
      value = _mm512_min_ps(value, _mm512_loadu_ps(&input(row - 1U, col)));
      value = _mm512_min_ps(value, _mm512_loadu_ps(&input(row + 1U, col)));
      value = _mm512_min_ps(value, _mm512_loadu_ps(&input(row, col - 1U)));
      value = _mm512_min_ps(value, _mm512_loadu_ps(&input(row, col + 1U)));
      const auto keep_mask = _mm512_cmp_ps_mask(value, threshold, _CMP_GT_OQ);
      _mm512_storeu_ps(&output(row, col), _mm512_mask_mov_ps(zero, keep_mask, one));
    }
    for (; col + 1U < input.cols(); ++col) {
      const bool keep = input(row, col) > 0.5F && input(row - 1U, col) > 0.5F && input(row + 1U, col) > 0.5F &&
                        input(row, col - 1U) > 0.5F && input(row, col + 1U) > 0.5F;
      output(row, col) = keep ? 1.0F : 0.0F;
    }
  }
}

void separable_3tap_filter_2d_avx512(const Image<float>& input, Image<float>& temp, Image<float>& output) {
  std::fill(temp.data(), temp.data() + temp.size(), 0.0F);
  std::fill(output.data(), output.data() + output.size(), 0.0F);

  const auto edge_weight = _mm512_set1_ps(0.25F);
  const auto center_weight = _mm512_set1_ps(0.50F);

  for (std::size_t row = 0; row < input.rows(); ++row) {
    std::size_t col = 1;
    for (; col + 16U <= input.cols() - 1U; col += 16U) {
      const auto left = _mm512_loadu_ps(&input(row, col - 1U));
      const auto center = _mm512_loadu_ps(&input(row, col));
      const auto right = _mm512_loadu_ps(&input(row, col + 1U));
      auto value = _mm512_mul_ps(center, center_weight);
      value = _mm512_fmadd_ps(_mm512_add_ps(left, right), edge_weight, value);
      _mm512_storeu_ps(&temp(row, col), value);
    }
    for (; col + 1U < input.cols(); ++col) {
      temp(row, col) = (input(row, col - 1U) * 0.25F) + (input(row, col) * 0.50F) + (input(row, col + 1U) * 0.25F);
    }
  }

  for (std::size_t row = 1; row + 1U < input.rows(); ++row) {
    std::size_t col = 1;
    for (; col + 16U <= input.cols() - 1U; col += 16U) {
      const auto top = _mm512_loadu_ps(&temp(row - 1U, col));
      const auto center = _mm512_loadu_ps(&temp(row, col));
      const auto bottom = _mm512_loadu_ps(&temp(row + 1U, col));
      auto value = _mm512_mul_ps(center, center_weight);
      value = _mm512_fmadd_ps(_mm512_add_ps(top, bottom), edge_weight, value);
      _mm512_storeu_ps(&output(row, col), value);
    }
    for (; col + 1U < input.cols(); ++col) {
      output(row, col) = (temp(row - 1U, col) * 0.25F) + (temp(row, col) * 0.50F) + (temp(row + 1U, col) * 0.25F);
    }
  }
}

void phase_quality_3d_avx512(const float* phase, float* quality, std::size_t rows, std::size_t cols,
                             std::size_t slices) {
  const auto total_size = rows * cols * slices;
  std::fill(quality, quality + total_size, 0.0F);

  const auto epsilon = _mm512_set1_ps(std::numeric_limits<float>::epsilon());
  const auto one = _mm512_set1_ps(1.0F);
  const auto slice_stride = rows * cols;
  for (std::size_t z = 1; z + 1U < slices; ++z) {
    for (std::size_t y = 1; y + 1U < rows; ++y) {
      std::size_t x = 1;
      const auto row_base = volume_offset(0, y, z, rows, cols);
      for (; x + 16U <= cols - 1U; x += 16U) {
        const auto center_index = row_base + x;
        const auto center = _mm512_loadu_ps(phase + center_index);
        const auto horizontal =
          _mm512_sub_ps(wrap_to_pi_avx512(_mm512_sub_ps(_mm512_loadu_ps(phase + center_index - 1U), center)),
                        wrap_to_pi_avx512(_mm512_sub_ps(center, _mm512_loadu_ps(phase + center_index + 1U))));
        const auto vertical =
          _mm512_sub_ps(wrap_to_pi_avx512(_mm512_sub_ps(_mm512_loadu_ps(phase + center_index - cols), center)),
                        wrap_to_pi_avx512(_mm512_sub_ps(center, _mm512_loadu_ps(phase + center_index + cols))));
        const auto through_plane =
          _mm512_sub_ps(wrap_to_pi_avx512(_mm512_sub_ps(_mm512_loadu_ps(phase + center_index - slice_stride), center)),
                        wrap_to_pi_avx512(_mm512_sub_ps(center, _mm512_loadu_ps(phase + center_index + slice_stride))));
        auto sum = _mm512_mul_ps(horizontal, horizontal);
        sum = _mm512_fmadd_ps(vertical, vertical, sum);
        sum = _mm512_fmadd_ps(through_plane, through_plane, sum);
        _mm512_storeu_ps(quality + center_index, _mm512_div_ps(one, _mm512_add_ps(_mm512_sqrt_ps(sum), epsilon)));
      }
      for (; x + 1U < cols; ++x) {
        const auto center = row_base + x;
        const auto center_value = phase[center];
        const auto horizontal =
          wrap_to_pi_scalar(phase[center - 1U] - center_value) - wrap_to_pi_scalar(center_value - phase[center + 1U]);
        const auto vertical = wrap_to_pi_scalar(phase[center - cols] - center_value) -
                              wrap_to_pi_scalar(center_value - phase[center + cols]);
        const auto through_plane = wrap_to_pi_scalar(phase[center - slice_stride] - center_value) -
                                   wrap_to_pi_scalar(center_value - phase[center + slice_stride]);
        quality[center] =
          1.0F / (std::sqrt((horizontal * horizontal) + (vertical * vertical) + (through_plane * through_plane)) +
                  std::numeric_limits<float>::epsilon());
      }
    }
  }
}

void volume_7point_stencil_avx512(const float* input, float* output, std::size_t rows, std::size_t cols,
                                  std::size_t slices) {
  const auto total_size = rows * cols * slices;
  std::fill(output, output + total_size, 0.0F);

  const auto center_weight = _mm512_set1_ps(0.40F);
  const auto neighbor_weight = _mm512_set1_ps(0.10F);
  const auto slice_stride = rows * cols;
  for (std::size_t z = 1; z + 1U < slices; ++z) {
    for (std::size_t y = 1; y + 1U < rows; ++y) {
      std::size_t x = 1;
      const auto row_base = volume_offset(0, y, z, rows, cols);
      for (; x + 16U <= cols - 1U; x += 16U) {
        const auto center_index = row_base + x;
        const auto left = _mm512_loadu_ps(input + center_index - 1U);
        const auto right = _mm512_loadu_ps(input + center_index + 1U);
        const auto top = _mm512_loadu_ps(input + center_index - cols);
        const auto bottom = _mm512_loadu_ps(input + center_index + cols);
        const auto prev_slice = _mm512_loadu_ps(input + center_index - slice_stride);
        const auto next_slice = _mm512_loadu_ps(input + center_index + slice_stride);
        auto value = _mm512_mul_ps(_mm512_loadu_ps(input + center_index), center_weight);
        value = _mm512_fmadd_ps(_mm512_add_ps(left, right), neighbor_weight, value);
        value = _mm512_fmadd_ps(_mm512_add_ps(top, bottom), neighbor_weight, value);
        value = _mm512_fmadd_ps(_mm512_add_ps(prev_slice, next_slice), neighbor_weight, value);
        _mm512_storeu_ps(output + center_index, value);
      }
      for (; x + 1U < cols; ++x) {
        const auto center = row_base + x;
        output[center] = (input[center] * 0.40F) + ((input[center - 1U] + input[center + 1U]) * 0.10F) +
                         ((input[center - cols] + input[center + cols]) * 0.10F) +
                         ((input[center - slice_stride] + input[center + slice_stride]) * 0.10F);
      }
    }
  }
}

void volume_zpad_scale_avx512(const ksj::base::cf32* input, ksj::base::cf32* output, std::size_t rows, std::size_t cols,
                              std::size_t slices, std::size_t padded_rows, std::size_t padded_cols,
                              std::size_t padded_slices, float scale) {
  std::fill(output, output + (padded_rows * padded_cols * padded_slices), ksj::base::cf32{});

  const auto row_offset = (padded_rows - rows) / 2U;
  const auto col_offset = (padded_cols - cols) / 2U;
  const auto slice_offset = (padded_slices - slices) / 2U;
  const auto scale_value = _mm512_set1_ps(scale);
  for (std::size_t z = 0; z < slices; ++z) {
    for (std::size_t col = 0; col < cols; ++col) {
      const auto* source = reinterpret_cast<const float*>(input + volume_column_major_offset(0, col, z, rows, cols));
      auto* destination = reinterpret_cast<float*>(
        output + volume_column_major_offset(row_offset, col + col_offset, z + slice_offset, padded_rows, padded_cols));
      std::size_t value = 0;
      const auto float_count = rows * 2U;
      for (; value + 16U <= float_count; value += 16U) {
        _mm512_storeu_ps(destination + value, _mm512_mul_ps(_mm512_loadu_ps(source + value), scale_value));
      }
      for (; value < float_count; ++value) {
        destination[value] = source[value] * scale;
      }
    }
  }
}

void weighted_coil_sum_avx512(const Image<float>& input, const Vector<float>& weights, Vector<float>& output) {
  for (std::size_t voxel = 0; voxel < input.rows(); ++voxel) {
    const auto* values = &input(voxel, 0);
    auto sum = _mm512_setzero_ps();
    std::size_t coil = 0;
    for (; coil + 16U <= input.cols(); coil += 16U) {
      const auto value = _mm512_loadu_ps(values + coil);
      const auto weight = _mm512_loadu_ps(weights.data() + coil);
      sum = _mm512_fmadd_ps(value, weight, sum);
    }

    alignas(64) float lanes[16];
    _mm512_store_ps(lanes, sum);
    float scalar_sum{};
    for (const float lane : lanes) {
      scalar_sum += lane;
    }
    for (; coil < input.cols(); ++coil) {
      scalar_sum += values[coil] * weights(coil);
    }
    output(voxel) = scalar_sum;
  }
}

#if defined(__AVX512BW__)
void int16_affine_clamp_avx512(const std::int16_t* input, std::int16_t* output, std::size_t size, std::int16_t scale,
                               std::int16_t bias, std::int16_t lower, std::int16_t upper) {
  const auto scale_values = _mm512_set1_epi16(scale);
  const auto bias_values = _mm512_set1_epi16(bias);
  const auto lower_values = _mm512_set1_epi16(lower);
  const auto upper_values = _mm512_set1_epi16(upper);

  std::size_t i = 0;
  for (; i + 32U <= size; i += 32U) {
    const auto input_values = _mm512_load_si512(reinterpret_cast<const __m512i*>(input + i));
    auto value = _mm512_add_epi16(_mm512_mullo_epi16(input_values, scale_values), bias_values);
    value = _mm512_max_epi16(value, lower_values);
    value = _mm512_min_epi16(value, upper_values);
    _mm512_store_si512(reinterpret_cast<__m512i*>(output + i), value);
  }
  int16_affine_clamp_scalar(input + i, output + i, size - i, scale, bias, lower, upper);
}
#endif

void int32_affine_clamp_avx512(const std::int32_t* input, std::int32_t* output, std::size_t size, std::int32_t scale,
                               std::int32_t bias, std::int32_t lower, std::int32_t upper) {
  const auto scale_values = _mm512_set1_epi32(scale);
  const auto bias_values = _mm512_set1_epi32(bias);
  const auto lower_values = _mm512_set1_epi32(lower);
  const auto upper_values = _mm512_set1_epi32(upper);

  std::size_t i = 0;
  for (; i + 16U <= size; i += 16U) {
    const auto input_values = _mm512_load_si512(reinterpret_cast<const __m512i*>(input + i));
    auto value = _mm512_add_epi32(_mm512_mullo_epi32(input_values, scale_values), bias_values);
    value = _mm512_max_epi32(value, lower_values);
    value = _mm512_min_epi32(value, upper_values);
    _mm512_store_si512(reinterpret_cast<__m512i*>(output + i), value);
  }
  int32_affine_clamp_scalar(input + i, output + i, size - i, scale, bias, lower, upper);
}

#endif

void run_classic_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<float>(size);
    auto rhs = make_vector<float>(size);
    auto output = make_vector<float>(size);
    fill_vector(lhs);
    fill_vector(rhs);

    const auto fma_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      classic_fma_triad_avx512(lhs.data(), rhs.data(), output.data(), output.size(), 0.25F);
#else
      classic_fma_triad_scalar(lhs.data(), rhs.data(), output.data(), output.size(), 0.25F);
#endif
      do_not_optimize(output.data());
    });
    print_row("classic_fma_triad", case_variant(kImplementationVariant), "float", size, 0, config, fma_measurement,
              checksum(output));

    float dot{};
    const auto dot_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      dot = classic_dot_avx512(lhs.data(), rhs.data(), lhs.size());
#else
      dot = classic_dot_scalar(lhs.data(), rhs.data(), lhs.size());
#endif
      do_not_optimize(dot);
    });
    print_row("classic_dot_product", case_variant(kImplementationVariant), "float", size, 0, config, dot_measurement,
              static_cast<double>(dot));
  }
}

void run_double_precision_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<double>(size);
    auto rhs = make_vector<double>(size);
    auto output = make_vector<double>(size);
    fill_vector(lhs);
    fill_vector(rhs);

    const auto fma_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      classic_fma_triad_f64_avx512(lhs.data(), rhs.data(), output.data(), output.size(), 0.25);
#else
      classic_fma_triad_f64_scalar(lhs.data(), rhs.data(), output.data(), output.size(), 0.25);
#endif
      do_not_optimize(output.data());
    });
    print_row("classic_fma_triad_f64", case_variant(kImplementationVariant), "double", size, 0, config, fma_measurement,
              checksum(output));

    double dot{};
    const auto dot_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      dot = classic_dot_f64_avx512(lhs.data(), rhs.data(), lhs.size());
#else
      dot = classic_dot_f64_scalar(lhs.data(), rhs.data(), lhs.size());
#endif
      do_not_optimize(dot);
    });
    print_row("classic_dot_f64", case_variant(kImplementationVariant), "double", size, 0, config, dot_measurement, dot);
  }
}

void run_elementwise_and_reduction_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<float>(size);
    auto rhs = make_vector<float>(size);
    auto output = make_vector<float>(size);
    fill_vector(lhs);
    fill_vector(rhs);
    for (std::size_t i = 0; i < size; ++i) {
      rhs(i) += static_cast<float>((i % 7U) + 1U) * 0.03125F;
    }

    const auto fused_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      fused_axpby_relu_avx512(lhs.data(), rhs.data(), output.data(), output.size(), 0.35F, -0.65F, 1.25F);
#else
      fused_axpby_relu_scalar(lhs.data(), rhs.data(), output.data(), output.size(), 0.35F, -0.65F, 1.25F);
#endif
      do_not_optimize(output.data());
    });
    print_row("fused_axpby_relu", case_variant(kImplementationVariant), "float", size, 0, config, fused_measurement,
              checksum(output));

    float distance{};
    const auto distance_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      distance = l2_distance_avx512(lhs.data(), rhs.data(), lhs.size());
#else
      distance = l2_distance_scalar(lhs.data(), rhs.data(), lhs.size());
#endif
      do_not_optimize(distance);
    });
    print_row("l2_distance_reduce", case_variant(kImplementationVariant), "float", size, 0, config,
              distance_measurement, static_cast<double>(distance));
  }
}

void run_extra_reduction_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<float>(size);
    fill_vector(input);
    for (std::size_t i = 0; i < size; ++i) {
      if (i % 5U == 0U) {
        input(i) = -input(i);
      }
    }

    float sum{};
    float sumsq{};
    const auto sum_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      sum_and_sumsq_avx512(input.data(), input.size(), sum, sumsq);
#else
      sum_and_sumsq_scalar(input.data(), input.size(), sum, sumsq);
#endif
      do_not_optimize(sum);
      do_not_optimize(sumsq);
    });
    print_row("sum_and_sumsq", case_variant(kImplementationVariant), "float", size, 0, config, sum_measurement,
              static_cast<double>(sum + sumsq));

    float min_value{};
    float max_value{};
    const auto minmax_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      min_max_avx512(input.data(), input.size(), min_value, max_value);
#else
      min_max_scalar(input.data(), input.size(), min_value, max_value);
#endif
      do_not_optimize(min_value);
      do_not_optimize(max_value);
    });
    print_row("min_max_reduce", case_variant(kImplementationVariant), "float", size, 0, config, minmax_measurement,
              static_cast<double>(min_value + max_value));
  }
}

void run_complex_mul_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<ksj::base::cf32>(size);
    auto rhs = make_vector<ksj::base::cf32>(size);
    auto output = make_vector<ksj::base::cf32>(size);
    fill_vector(lhs);
    fill_vector(rhs);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      complex_mul_aos_avx512(lhs.data(), rhs.data(), output.data(), output.size());
#else
      complex_mul_aos_scalar(lhs.data(), rhs.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("complex_mul_aos", case_variant(kImplementationVariant), "complex_float", size, 0, config, measurement,
              checksum(output));
  }
}

void run_complex_correlation_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<ksj::base::cf32>(size);
    auto rhs = make_vector<ksj::base::cf32>(size);
    auto output = make_vector<ksj::base::cf32>(size);
    fill_vector(lhs);
    fill_vector(rhs);
    for (std::size_t i = 0; i < size; ++i) {
      lhs(i) += ksj::base::cf32{0.125F, -0.25F};
      rhs(i) += ksj::base::cf32{-0.375F, 0.5F};
    }

    const auto conj_mul_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      complex_conj_mul_aos_avx512(lhs.data(), rhs.data(), output.data(), output.size());
#else
      complex_conj_mul_aos_scalar(lhs.data(), rhs.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("complex_conj_mul_aos", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              conj_mul_measurement, checksum(output));

    const auto normalize_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      complex_normalize_avx512(lhs.data(), output.data(), output.size());
#else
      complex_normalize_scalar(lhs.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("complex_normalize", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              normalize_measurement, checksum(output));
  }
}

void run_complex_layout_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<ksj::base::cf32>(size);
    auto magnitude = make_vector<float>(size);
    auto real = make_vector<float>(size);
    auto imag = make_vector<float>(size);
    fill_vector(input);

    const auto magnitude_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      complex_magnitude_squared_avx512(input.data(), magnitude.data(), magnitude.size());
#else
      complex_magnitude_squared_scalar(input.data(), magnitude.data(), magnitude.size());
#endif
      do_not_optimize(magnitude.data());
    });
    print_row("complex_mag_sq", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              magnitude_measurement, checksum(magnitude));

    const auto deinterleave_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      complex_deinterleave_avx512(input.data(), real.data(), imag.data(), input.size());
#else
      complex_deinterleave_scalar(input.data(), real.data(), imag.data(), input.size());
#endif
      do_not_optimize(real.data());
      do_not_optimize(imag.data());
    });
    print_row("complex_deinterleave", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              deinterleave_measurement, checksum(real) + checksum(imag));
  }
}

void run_recon_runtime_view_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto source = make_vector<ksj::base::cf32>(size);
    auto destination = make_vector<ksj::base::cf32>(size);
    auto output = make_vector<ksj::base::cf32>(size);
    fill_vector(source);
    fill_vector(destination);
    for (std::size_t i = 0; i < size; ++i) {
      destination(i) += ksj::base::cf32{1.25F, -0.75F};
    }

    const auto real_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      view_copy_real_component_avx512(source.data(), destination.data(), output.data(), output.size());
#else
      view_copy_real_component_scalar(source.data(), destination.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("view_copy_real_component", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              real_measurement, checksum(output));

    const auto imag_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      view_copy_imag_component_avx512(source.data(), destination.data(), output.data(), output.size());
#else
      view_copy_imag_component_scalar(source.data(), destination.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("view_copy_imag_component", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              imag_measurement, checksum(output));
  }
}

void run_mri_math_functor_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<ksj::base::cf32>(size);
    auto rhs = make_vector<ksj::base::cf32>(size);
    auto output = make_vector<ksj::base::cf32>(size);
    fill_vector(lhs);
    fill_vector(rhs);
    for (std::size_t i = 0; i < size; ++i) {
      const auto phase = (static_cast<float>(static_cast<int>(i % 23U) - 11) * 0.55F);
      lhs(i) = {lhs(i).real() - 12.0F, phase};
      rhs(i) = {rhs(i).real(), phase * 0.25F};
    }

    const auto clamp_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      complex_discard_imag_clamp_avx512(lhs.data(), output.data(), output.size());
#else
      complex_discard_imag_clamp_scalar(lhs.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("complex_discard_imag_clamp", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              clamp_measurement, checksum(output));

    const auto phase_diff_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      phase_difference_avx512(lhs.data(), rhs.data(), output.data(), output.size());
#else
      phase_difference_scalar(lhs.data(), rhs.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("phase_difference", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              phase_diff_measurement, checksum(output));

    const auto unwrap_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      phase_unwrap_1p5_avx512(lhs.data(), output.data(), output.size());
#else
      phase_unwrap_1p5_scalar(lhs.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("phase_unwrap_1p5", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              unwrap_measurement, checksum(output));
  }
}

void run_sdat_complex_view_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto interleaved_complex = make_vector<float>(size * 2U);
    auto magnitude = make_vector<float>(size);
    fill_vector(interleaved_complex);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      sdat_complex_magnitude_avx512(interleaved_complex.data(), magnitude.data(), magnitude.size());
#else
      sdat_complex_magnitude_scalar(interleaved_complex.data(), magnitude.data(), magnitude.size());
#endif
      do_not_optimize(magnitude.data());
    });
    print_row("sdat_complex_magnitude", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              measurement, checksum(magnitude));
  }
}

void run_numerics_image_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<float>(size);
    auto output = make_vector<float>(size);
    fill_vector(input);
    for (std::size_t i = 0; i < size; ++i) {
      if ((i % 11U) == 0U) {
        input(i) = -input(i);
      }
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      image_threshold_avx512(input.data(), output.data(), output.size(), 16.0F, 0.0F, 255.0F);
#else
      image_threshold_scalar(input.data(), output.data(), output.size(), 16.0F, 0.0F, 255.0F);
#endif
      do_not_optimize(output.data());
    });
    print_row("image_threshold", case_variant(kImplementationVariant), "float", size, 0, config, measurement,
              checksum(output));
  }
}

void run_numerics_signal_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto signal = make_vector<float>(size + 7U);
    auto kernel = make_vector<float>(8U);
    auto output = make_vector<float>(size);
    fill_vector(signal);
    for (std::size_t tap = 0; tap < kernel.size(); ++tap) {
      kernel(tap) = 1.0F / static_cast<float>((tap + 1U) * 8U);
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      signal_fir_8tap_convolve_avx512(signal.data(), kernel.data(), output.data(), output.size());
#else
      signal_fir_8tap_convolve_scalar(signal.data(), kernel.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("signal_fir_8tap_convolve", case_variant(kImplementationVariant), "float", size, 0, config, measurement,
              checksum(output));
  }
}

void run_gather_weighted_sum_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto table = make_vector<float>(size);
    auto weights = make_vector<float>(size);
    auto indices = make_vector<std::int32_t>(size);
    fill_vector(table);
    fill_vector(weights);
    for (std::size_t i = 0; i < size; ++i) {
      indices(i) = static_cast<std::int32_t>((i * 37U + 11U) % size);
      weights(i) = 1.0F / static_cast<float>((i % 31U) + 1U);
    }

    float sum{};
    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      sum = gather_weighted_sum_avx512(table.data(), indices.data(), weights.data(), indices.size());
#else
      sum = gather_weighted_sum_scalar(table.data(), indices.data(), weights.data(), indices.size());
#endif
      do_not_optimize(sum);
    });
    print_row("gather_weighted_sum", case_variant(kImplementationVariant), "float", size, 0, config, measurement,
              static_cast<double>(sum));
  }
}

void run_masked_roi_compact_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<float>(size);
    auto output = make_vector<float>(size);
    fill_vector(input);
    for (std::size_t i = 0; i < size; ++i) {
      input(i) = static_cast<float>(static_cast<int>(i % 29U) - 11) * 0.75F;
    }

    std::size_t selected_count{};
    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      selected_count = masked_threshold_compact_avx512(input.data(), output.data(), input.size(), 4.0F, 0.5F);
#else
      selected_count = masked_threshold_compact_scalar(input.data(), output.data(), input.size(), 4.0F, 0.5F);
#endif
      do_not_optimize(selected_count);
      do_not_optimize(output.data());
    });
    print_row("masked_threshold_compact", case_variant(kImplementationVariant), "float", size, 0, config, measurement,
              checksum_linear(output.data(), selected_count) + static_cast<double>(selected_count));
  }
}

void run_layout_and_stride_cases(const Config& config) {
  for (const auto size : config.sizes) {
    auto strided_input = make_vector<float>((size * 2U) + 1U);
    auto strided_output = make_vector<float>(size);
    fill_vector(strided_input);

    const auto strided_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      strided_scale_pack_avx512(strided_input.data(), strided_output.data(), strided_output.size(), 0.125F);
#else
      strided_scale_pack_scalar(strided_input.data(), strided_output.data(), strided_output.size(), 0.125F);
#endif
      do_not_optimize(strided_output.data());
    });
    print_row("strided_scale_pack", case_variant(kImplementationVariant), "float", size, 0, config, strided_measurement,
              checksum(strided_output));

    constexpr std::size_t kRoiWidth = 64U;
    constexpr std::size_t kImageCols = 128U;
    constexpr std::size_t kRoiColOffset = 17U;
    const auto rows = std::max<std::size_t>(1U, (size + kRoiWidth - 1U) / kRoiWidth);
    auto image = make_image<float>(rows, kImageCols);
    auto roi = make_vector<float>(rows * kRoiWidth);
    fill_image(image);

    const auto roi_measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      roi_stride_materialize_avx512(image, roi, kRoiColOffset, kRoiWidth);
#else
      roi_stride_materialize_scalar(image, roi, kRoiColOffset, kRoiWidth);
#endif
      do_not_optimize(roi.data());
    });
    print_row("roi_stride_pack", case_variant(kImplementationVariant), "float", roi.size(), 0, config, roi_measurement,
              checksum(roi));
  }
}

void run_transpose_layout_case(const Config& config) {
  constexpr std::size_t kImageCols = 128U;
  for (const auto size : config.sizes) {
    const auto rows = std::max<std::size_t>(16U, (size + kImageCols - 1U) / kImageCols);
    auto input = make_image<float>(rows, kImageCols);
    auto output = make_image<float>(kImageCols, rows);
    fill_image(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      image_transpose_scatter_avx512(input, output);
#else
      image_transpose_scatter_scalar(input, output);
#endif
      do_not_optimize(output.data());
    });
    print_row("image_transpose_scatter", case_variant(kImplementationVariant), "float", output.size(), 0, config,
              measurement, checksum(output));
  }
}

void run_stencil_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<float>(size);
    auto output = make_vector<float>(size);
    fill_vector(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      five_point_stencil_avx512(input.data(), output.data(), output.size());
#else
      five_point_stencil_scalar(input.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("five_point_stencil", case_variant(kImplementationVariant), "float", size, 0, config, measurement,
              checksum(output));
  }
}

void run_image_filter_case(const Config& config) {
  constexpr std::size_t kImageCols = 128U;
  for (const auto size : config.sizes) {
    const auto rows = std::max<std::size_t>(8U, (size + kImageCols - 1U) / kImageCols);
    auto input = make_image<float>(rows, kImageCols);
    auto output = make_image<float>(rows, kImageCols);
    fill_image(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      image_3x3_blur_avx512(input, output);
#else
      image_3x3_blur_scalar(input, output);
#endif
      do_not_optimize(output.data());
    });
    print_row("image_3x3_blur", case_variant(kImplementationVariant), "float", output.size(), 0, config, measurement,
              checksum(output));
  }
}

void run_mask_morphology_case(const Config& config) {
  constexpr std::size_t kImageCols = 128U;
  for (const auto size : config.sizes) {
    const auto rows = std::max<std::size_t>(8U, (size + kImageCols - 1U) / kImageCols);
    auto input = make_image<float>(rows, kImageCols);
    auto output = make_image<float>(rows, kImageCols);
    for (std::size_t row = 0; row < rows; ++row) {
      for (std::size_t col = 0; col < kImageCols; ++col) {
        input(row, col) = (((row * 3U) + (col * 5U)) % 23U) > 3U ? 1.0F : 0.0F;
      }
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      mask_erode_cross_2d_avx512(input, output);
#else
      mask_erode_cross_2d_scalar(input, output);
#endif
      do_not_optimize(output.data());
    });
    print_row("mask_erode_cross_2d", case_variant(kImplementationVariant), "float", output.size(), 0, config,
              measurement, checksum(output));
  }
}

void run_hamming_filter_case(const Config& config) {
  constexpr std::size_t kImageCols = 128U;
  for (const auto size : config.sizes) {
    const auto rows = std::max<std::size_t>(8U, (size + kImageCols - 1U) / kImageCols);
    auto input = make_vector<ksj::base::cf32>(rows * kImageCols);
    auto weights = make_vector<float>(rows * kImageCols);
    auto output = make_vector<ksj::base::cf32>(rows * kImageCols);
    fill_vector(input);

    const auto row_denominator = static_cast<float>(std::max<std::size_t>(1U, rows - 1U));
    const auto col_denominator = static_cast<float>(kImageCols - 1U);
    for (std::size_t row = 0; row < rows; ++row) {
      const auto row_weight = 0.54F - 0.46F * std::cos((kTwoPi * static_cast<float>(row)) / row_denominator);
      for (std::size_t col = 0; col < kImageCols; ++col) {
        const auto col_weight = 0.54F - 0.46F * std::cos((kTwoPi * static_cast<float>(col)) / col_denominator);
        weights((row * kImageCols) + col) = row_weight * col_weight;
      }
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      hamming2d_complex_filter_avx512(input.data(), weights.data(), output.data(), output.size());
#else
      hamming2d_complex_filter_scalar(input.data(), weights.data(), output.data(), output.size());
#endif
      do_not_optimize(output.data());
    });
    print_row("hamming2d_complex_filter", case_variant(kImplementationVariant), "complex_float", output.size(), 0,
              config, measurement, checksum(output));
  }
}

void run_small_tile_boundary_blend_case(const Config& config) {
  constexpr std::size_t kTileRows = 32U;
  constexpr std::size_t kTileCols = 32U;
  constexpr std::size_t kTileSize = kTileRows * kTileCols;
  for (const auto size : config.sizes) {
    const auto tile_count = std::max<std::size_t>(1U, (size + kTileSize - 1U) / kTileSize);
    const auto element_count = tile_count * kTileSize;
    auto input = make_vector<float>(element_count);
    auto output = make_vector<float>(element_count);
    fill_vector(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      small_tile_boundary_blend_avx512(input.data(), output.data(), tile_count, kTileRows, kTileCols);
#else
      small_tile_boundary_blend_scalar(input.data(), output.data(), tile_count, kTileRows, kTileCols);
#endif
      do_not_optimize(output.data());
    });
    print_row("small_tile_boundary_blend", case_variant(kImplementationVariant), "float", element_count, 0, config,
              measurement, checksum(output));
  }
}

void run_separable_filter_case(const Config& config) {
  constexpr std::size_t kImageCols = 128U;
  for (const auto size : config.sizes) {
    const auto rows = std::max<std::size_t>(8U, (size + kImageCols - 1U) / kImageCols);
    auto input = make_image<float>(rows, kImageCols);
    auto temp = make_image<float>(rows, kImageCols);
    auto output = make_image<float>(rows, kImageCols);
    fill_image(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      separable_3tap_filter_2d_avx512(input, temp, output);
#else
      separable_3tap_filter_2d_scalar(input, temp, output);
#endif
      do_not_optimize(output.data());
    });
    print_row("separable_3tap_filter_2d", case_variant(kImplementationVariant), "float", output.size(), 0, config,
              measurement, checksum(output));
  }
}

void run_volume_neighborhood_case(const Config& config) {
  constexpr std::size_t kRows = 32U;
  constexpr std::size_t kCols = 32U;
  constexpr std::size_t kSliceSize = kRows * kCols;
  for (const auto size : config.sizes) {
    const auto slices = std::max<std::size_t>(4U, (size + kSliceSize - 1U) / kSliceSize);
    const auto element_count = slices * kSliceSize;
    auto input = make_vector<float>(element_count);
    auto output = make_vector<float>(element_count);
    fill_vector(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      volume_7point_stencil_avx512(input.data(), output.data(), kRows, kCols, slices);
#else
      volume_7point_stencil_scalar(input.data(), output.data(), kRows, kCols, slices);
#endif
      do_not_optimize(output.data());
    });
    print_row("volume_7point_stencil", case_variant(kImplementationVariant), "float", element_count, 0, config,
              measurement, checksum(output));
  }
}

void run_phase_quality_3d_case(const Config& config) {
  constexpr std::size_t kRows = 32U;
  constexpr std::size_t kCols = 32U;
  constexpr std::size_t kSliceSize = kRows * kCols;
  for (const auto size : config.sizes) {
    const auto slices = std::max<std::size_t>(4U, (size + kSliceSize - 1U) / kSliceSize);
    const auto element_count = slices * kSliceSize;
    auto phase = make_vector<float>(element_count);
    auto quality = make_vector<float>(element_count);
    for (std::size_t i = 0; i < element_count; ++i) {
      phase(i) = static_cast<float>(static_cast<int>((i * 7U) % 97U) - 48) * (kPi / 48.0F);
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      phase_quality_3d_avx512(phase.data(), quality.data(), kRows, kCols, slices);
#else
      phase_quality_3d_scalar(phase.data(), quality.data(), kRows, kCols, slices);
#endif
      do_not_optimize(quality.data());
    });
    print_row("phase_quality_3d", case_variant(kImplementationVariant), "float", element_count, 0, config, measurement,
              checksum(quality));
  }
}

void run_volume_zpad_scale_case(const Config& config) {
  constexpr std::size_t kRows = 16U;
  constexpr std::size_t kCols = 16U;
  constexpr std::size_t kPaddedRows = 32U;
  constexpr std::size_t kPaddedCols = 32U;
  constexpr std::size_t kSliceSize = kRows * kCols;
  for (const auto size : config.sizes) {
    const auto slices = std::max<std::size_t>(4U, (size + kSliceSize - 1U) / kSliceSize);
    const auto padded_slices = slices + 4U;
    auto input = make_vector<ksj::base::cf32>(slices * kSliceSize);
    auto output = make_vector<ksj::base::cf32>(padded_slices * kPaddedRows * kPaddedCols);
    fill_vector(input);

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      volume_zpad_scale_avx512(input.data(), output.data(), kRows, kCols, slices, kPaddedRows, kPaddedCols,
                               padded_slices, 0.5F);
#else
      volume_zpad_scale_scalar(input.data(), output.data(), kRows, kCols, slices, kPaddedRows, kPaddedCols,
                               padded_slices, 0.5F);
#endif
      do_not_optimize(output.data());
    });
    print_row("volume_zpad_scale", case_variant(kImplementationVariant), "complex_float", input.size(), 0, config,
              measurement, checksum(output));
  }
}

void run_weighted_coil_sum_case(const Config& config) {
  for (const auto size : config.sizes) {
    for (const auto coils : config.coils) {
      auto input = make_image<float>(size, coils);
      auto weights = make_vector<float>(coils);
      auto output = make_vector<float>(size);
      fill_image(input);
      for (std::size_t coil = 0; coil < coils; ++coil) {
        weights(coil) = 1.0F / static_cast<float>(coil + 1U);
      }

      const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
        weighted_coil_sum_avx512(input, weights, output);
#else
        weighted_coil_sum_scalar(input, weights, output);
#endif
        do_not_optimize(output.data());
      });
      print_row("weighted_coil_sum", case_variant(kImplementationVariant), "float", size, coils, config, measurement,
                checksum(output));
    }
  }
}

void run_int16_affine_clamp_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<std::int16_t>(size);
    auto output = make_vector<std::int16_t>(size);
    for (std::size_t i = 0; i < size; ++i) {
      input(i) = static_cast<std::int16_t>(static_cast<int>(i % 2048U) - 1024);
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__) && defined(__AVX512BW__)
      int16_affine_clamp_avx512(input.data(), output.data(), output.size(), 3, 17, -2000, 2000);
#else
      int16_affine_clamp_scalar(input.data(), output.data(), output.size(), 3, 17, -2000, 2000);
#endif
      do_not_optimize(output.data());
    });
    print_row("int16_affine_clamp", case_variant(kImplementationVariant), "int16", size, 0, config, measurement,
              checksum_linear(output.data(), output.size()));
  }
}

void run_int32_affine_clamp_case(const Config& config) {
  for (const auto size : config.sizes) {
    auto input = make_vector<std::int32_t>(size);
    auto output = make_vector<std::int32_t>(size);
    for (std::size_t i = 0; i < size; ++i) {
      input(i) = static_cast<std::int32_t>(i % 4096U) - 2048;
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      int32_affine_clamp_avx512(input.data(), output.data(), output.size(), 3, 17, -2048, 2047);
#else
      int32_affine_clamp_scalar(input.data(), output.data(), output.size(), 3, 17, -2048, 2047);
#endif
      do_not_optimize(output.data());
    });
    print_row("int32_affine_clamp", case_variant(kImplementationVariant), "int32", size, 0, config, measurement,
              checksum(output));
  }
}

void run_waterfat_weight_gate(const Config& config) {
  for (const auto size : config.sizes) {
    auto buffer_x = make_vector<float>(size);
    auto buffer_y = make_vector<float>(size);
    auto weight_in = make_vector<float>(size);
    auto weight_out = make_vector<float>(size);
    fill_vector(weight_in);
    for (std::size_t i = 0; i < size; ++i) {
      buffer_x(i) = (i % 9U == 0U) ? 1.25F : 0.125F;
      buffer_y(i) = (i % 13U == 0U) ? 1.50F : 0.250F;
    }

    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      waterfat_weight_gate_avx512(buffer_x.data(), buffer_y.data(), weight_in.data(), weight_out.data(),
                                  weight_out.size(), 1.0F);
#else
      waterfat_weight_gate_scalar(buffer_x.data(), buffer_y.data(), weight_in.data(), weight_out.data(),
                                  weight_out.size(), 1.0F);
#endif
      do_not_optimize(weight_out.data());
    });
    print_row("waterfat_weight_gate", case_variant(kImplementationVariant), "float", size, 0, config, measurement,
              checksum(weight_out));
  }
}

void run_prewhiten_covariance(const Config& config) {
  for (const auto size : config.sizes) {
    auto lhs = make_vector<ksj::base::cf32>(size);
    auto rhs = make_vector<ksj::base::cf32>(size);
    fill_vector(lhs);
    fill_vector(rhs);

    float real{};
    float imag{};
    const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
      prewhiten_covariance_pair_avx512(lhs.data(), rhs.data(), lhs.size(), real, imag);
#else
      prewhiten_covariance_pair_scalar(lhs.data(), rhs.data(), lhs.size(), real, imag);
#endif
      do_not_optimize(real);
      do_not_optimize(imag);
    });
    print_row("prewhiten_covariance", case_variant(kImplementationVariant), "complex_float", size, 0, config,
              measurement, static_cast<double>(real + imag));
  }
}

void run_prewhiten_channel_mix(const Config& config) {
  for (const auto size : config.sizes) {
    for (const auto coils : config.coils) {
      const auto sample_count = std::max<std::size_t>(64U, size / coils);
      auto input = make_image<ksj::base::cf32>(coils, sample_count);
      auto lower_triangle = make_image<ksj::base::cf32>(coils, coils);
      auto output = make_image<ksj::base::cf32>(coils, sample_count);
      fill_image(input);
      for (std::size_t row = 0; row < coils; ++row) {
        for (std::size_t col = 0; col < coils; ++col) {
          if (col <= row) {
            const auto denom = static_cast<float>(row + col + 1U);
            lower_triangle(row, col) = {1.0F / denom, 0.05F / denom};
          } else {
            lower_triangle(row, col) = {};
          }
        }
      }

      const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
        prewhiten_channel_mix_avx512(input, lower_triangle, output);
#else
        prewhiten_channel_mix_scalar(input, lower_triangle, output);
#endif
        do_not_optimize(output.data());
      });
      print_row("prewhiten_channel_mix", case_variant(kImplementationVariant), "complex_float", output.size(), coils,
                config, measurement, checksum(output));
    }
  }
}

void run_rss_coil_combine(const Config& config) {
  for (const auto size : config.sizes) {
    for (const auto coils : config.coils) {
      auto input = make_image<ksj::base::cf32>(size, coils);
      auto output = make_vector<float>(size);
      fill_image(input);

      const auto measurement = measure(config, [&] {
#if KSJ_NUMERICS_PERF_ENABLE_AVX512_INTRINSICS && defined(__AVX512F__)
        rss_coil_combine_avx512(input, output);
#else
        rss_coil_combine_scalar(input, output);
#endif
        do_not_optimize(output.data());
      });
      print_row("rss_coil_combine", case_variant(kImplementationVariant), "complex_float", size, coils, config,
                measurement, checksum(output));
    }
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  parse_args(argc, argv, config,
             "usage: ksj_numerics_perf_compute_scenarios_* [--iterations N] [--trials N] [--sizes A,B,C] "
             "[--coils A,B,C]");
  initialize_numerics_runtime();
  print_header(config);
  run_classic_cases(config);
  run_double_precision_cases(config);
  run_elementwise_and_reduction_cases(config);
  run_extra_reduction_cases(config);
  run_complex_mul_case(config);
  run_complex_correlation_cases(config);
  run_complex_layout_cases(config);
  run_recon_runtime_view_cases(config);
  run_mri_math_functor_cases(config);
  run_sdat_complex_view_case(config);
  run_numerics_image_case(config);
  run_numerics_signal_case(config);
  run_gather_weighted_sum_case(config);
  run_masked_roi_compact_case(config);
  run_layout_and_stride_cases(config);
  run_transpose_layout_case(config);
  run_stencil_case(config);
  run_image_filter_case(config);
  run_mask_morphology_case(config);
  run_hamming_filter_case(config);
  run_small_tile_boundary_blend_case(config);
  run_separable_filter_case(config);
  run_volume_neighborhood_case(config);
  run_phase_quality_3d_case(config);
  run_volume_zpad_scale_case(config);
  run_weighted_coil_sum_case(config);
  run_int16_affine_clamp_case(config);
  run_int32_affine_clamp_case(config);
  run_waterfat_weight_gate(config);
  run_prewhiten_covariance(config);
  run_prewhiten_channel_mix(config);
  run_rss_coil_combine(config);
  return 0;
}

#undef KSJ_RESEARCH_RESTRICT
