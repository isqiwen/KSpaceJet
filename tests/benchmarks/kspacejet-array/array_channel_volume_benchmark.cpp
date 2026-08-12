#include "array_benchmark_common.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_dimwise.hpp"

#include <cmath>
#include <complex>
#include <utility>

namespace ksj::benchmarks::array_benchmarks {
namespace {

void fill_complex_array4d(ksj::array::PooledArray4D<ksj::base::cf32>& array) {
  for (std::size_t dim0 = 0U; dim0 < array.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < array.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < array.dim2(); ++dim2) {
        for (std::size_t dim3 = 0U; dim3 < array.dim3(); ++dim3) {
          const auto real = static_cast<float>((dim0 * 23U + dim1 * 17U + dim2 * 31U + dim3 * 7U) % 251U) * 0.125F;
          const auto imag = static_cast<float>((dim0 * 29U + dim1 * 13U + dim2 * 19U + dim3 * 11U) % 127U) * 0.0625F;
          array(dim0, dim1, dim2, dim3) = {real, imag};
        }
      }
    }
  }
}

void fill_real_cube(ksj::array::PooledCube<float>& cube) {
  for (std::size_t dim0 = 0U; dim0 < cube.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < cube.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < cube.dim2(); ++dim2) {
        cube(dim0, dim1, dim2) = static_cast<float>(((dim0 * 17U + dim1 * 31U + dim2 * 7U) % 5U) + 1U);
      }
    }
  }
}

void fill_complex_cube(ksj::array::PooledCube<ksj::base::cf32>& cube) {
  for (std::size_t dim0 = 0U; dim0 < cube.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < cube.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < cube.dim2(); ++dim2) {
        const auto real = static_cast<float>((dim0 * 17U + dim1 * 31U + dim2 * 7U) % 251U) * 0.125F;
        const auto imag = static_cast<float>((dim0 * 29U + dim1 * 13U + dim2 * 11U) % 127U) * 0.0625F;
        cube(dim0, dim1, dim2) = {real, imag};
      }
    }
  }
}

[[nodiscard]] double checksum_array4d(const ksj::array::PooledArray4D<ksj::base::cf32>& array) {
  double checksum = 0.0;
  for (std::size_t index = 0U; index < array.size(); ++index) {
    checksum += static_cast<double>(array[index].real() + array[index].imag());
  }
  return checksum;
}

[[nodiscard]] double checksum_cube(const ksj::array::PooledCube<ksj::base::cf32>& cube) {
  double checksum = 0.0;
  for (std::size_t index = 0U; index < cube.size(); ++index) {
    checksum += static_cast<double>(cube[index].real() + cube[index].imag());
  }
  return checksum;
}

void manual_multiply_array4d_by_cube(const ksj::array::PooledArray4D<ksj::base::cf32>& channels,
                                     const ksj::array::PooledCube<float>& volume,
                                     ksj::array::PooledArray4D<ksj::base::cf32>& output) {
  const auto volume_size = volume.size();
  for (std::size_t channel = 0U; channel < channels.dim0(); ++channel) {
    const auto channel_offset = channel * volume_size;
    for (std::size_t index = 0U; index < volume_size; ++index) {
      output[channel_offset + index] = channels[channel_offset + index] * volume[index];
    }
  }
}

void manual_reduce_conjugate_product(const ksj::array::PooledArray4D<ksj::base::cf32>& channels,
                                     const ksj::array::PooledArray4D<ksj::base::cf32>& weights,
                                     ksj::array::PooledCube<ksj::base::cf32>& output) {
  const auto volume_size = output.size();
  for (std::size_t index = 0U; index < volume_size; ++index) {
    output[index] = {};
  }
  for (std::size_t channel = 0U; channel < channels.dim0(); ++channel) {
    const auto channel_offset = channel * volume_size;
    for (std::size_t index = 0U; index < volume_size; ++index) {
      output[index] += channels[channel_offset + index] * std::conj(weights[channel_offset + index]);
    }
  }
}

void manual_multiply_cube_by_abs_sum_squared(const ksj::array::PooledCube<ksj::base::cf32>& volume,
                                             const ksj::array::PooledArray4D<ksj::base::cf32>& channels,
                                             ksj::array::PooledCube<ksj::base::cf32>& output) {
  const auto volume_size = volume.size();
  for (std::size_t index = 0U; index < volume_size; ++index) {
    float magnitude_sum = 0.0F;
    for (std::size_t channel = 0U; channel < channels.dim0(); ++channel) {
      magnitude_sum += std::abs(channels[channel * volume_size + index]);
    }
    output[index] = volume[index] * magnitude_sum * magnitude_sum;
  }
}

void run_channel_volume_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto element_count : config.sizes) {
    const auto shape4d = element_count_shape4d(element_count);
    const auto channels_count = shape4d.dim0;
    const auto rows = shape4d.dim1;
    const auto cols = shape4d.dim2;
    const auto slices = shape4d.dim3;
    const auto logical_size = element_count;

    auto channels = ksj::array::make_pooled_array4d<ksj::base::cf32>(channels_count, rows, cols, slices);
    auto weights = ksj::array::make_pooled_array4d<ksj::base::cf32>(channels_count, rows, cols, slices);
    auto volume = ksj::array::make_pooled_cube<float>(rows, cols, slices);
    auto complex_volume = ksj::array::make_pooled_cube<ksj::base::cf32>(rows, cols, slices);
    auto channel_output = ksj::array::make_pooled_array4d<ksj::base::cf32>(channels_count, rows, cols, slices);
    auto phasor_output = ksj::array::make_pooled_cube<ksj::base::cf32>(rows, cols, slices);
    auto cube_output = ksj::array::make_pooled_cube<ksj::base::cf32>(rows, cols, slices);
    ksj::benchmarks::require_pooled_storage("channel_volume_channels", channels);
    ksj::benchmarks::require_pooled_storage("channel_volume_weights", weights);
    ksj::benchmarks::require_pooled_storage("channel_volume_volume", volume);
    ksj::benchmarks::require_pooled_storage("channel_volume_complex_volume", complex_volume);
    ksj::benchmarks::require_pooled_storage("channel_volume_phasor_output", phasor_output);
    fill_complex_array4d(channels);
    fill_complex_array4d(weights);
    fill_real_cube(volume);
    fill_complex_cube(complex_volume);
    const auto channels_input = std::as_const(channels).view();
    const auto weights_input = std::as_const(weights).view();
    const auto volume_input = std::as_const(volume).view();
    const auto complex_volume_input = std::as_const(complex_volume).view();

    double manual_multiply_checksum = 0.0;
    const auto manual_multiply_ns = ksj::benchmarks::measure(config, [&] {
      manual_multiply_array4d_by_cube(channels, volume, channel_output);
      manual_multiply_checksum = checksum_array4d(channel_output);
      ksj::benchmarks::do_not_optimize(manual_multiply_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_multiply", "manual", type_name, logical_size, config, manual_multiply_ns,
                               manual_multiply_checksum,
                               ksj::benchmarks::reference_row("channel_volume_multiply", "output_reuse"));

    double detail_multiply_checksum = 0.0;
    const auto detail_multiply_ns = ksj::benchmarks::measure(config, [&] {
      [[maybe_unused]] const auto success = ksj::array::detail::eigen::multiply_array4d_by_cube_contiguous(
        channels_input, volume_input, channel_output.view());
      detail_multiply_checksum = checksum_array4d(channel_output);
      ksj::benchmarks::do_not_optimize(detail_multiply_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_multiply", "detail_contiguous", type_name, logical_size, config,
                               detail_multiply_ns, detail_multiply_checksum,
                               ksj::benchmarks::candidate_row("channel_volume_multiply", "output_reuse"));

    double public_multiply_checksum = 0.0;
    const auto public_multiply_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::multiply_array4d_by_cube(channels_input, volume_input, channel_output.view());
      public_multiply_checksum = checksum_array4d(channel_output);
      ksj::benchmarks::do_not_optimize(public_multiply_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_multiply", "public", type_name, logical_size, config, public_multiply_ns,
                               public_multiply_checksum,
                               ksj::benchmarks::candidate_row("channel_volume_multiply", "output_reuse"));

    double manual_combine_checksum = 0.0;
    const auto manual_combine_ns = ksj::benchmarks::measure(config, [&] {
      manual_reduce_conjugate_product(channels, weights, cube_output);
      manual_combine_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(manual_combine_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_combine", "manual", type_name, logical_size, config, manual_combine_ns,
                               manual_combine_checksum,
                               ksj::benchmarks::reference_row("channel_volume_combine", "output_reuse"));

    double detail_combine_checksum = 0.0;
    const auto detail_combine_ns = ksj::benchmarks::measure(config, [&] {
      [[maybe_unused]] const auto success = ksj::array::detail::eigen::reduce_conjugate_product_contiguous(
        channels_input, weights_input, cube_output.view(), ksj::array::Dim::dim0);
      detail_combine_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(detail_combine_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_combine", "detail_contiguous", type_name, logical_size, config,
                               detail_combine_ns, detail_combine_checksum,
                               ksj::benchmarks::candidate_row("channel_volume_combine", "output_reuse"));

    double public_combine_checksum = 0.0;
    const auto public_combine_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::reduce_conjugate_product(channels_input, weights_input, cube_output.view(), ksj::array::Dim::dim0);
      public_combine_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(public_combine_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_combine", "public", type_name, logical_size, config, public_combine_ns,
                               public_combine_checksum,
                               ksj::benchmarks::candidate_row("channel_volume_combine", "output_reuse"));

    double manual_magnitude_checksum = 0.0;
    const auto manual_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      manual_multiply_cube_by_abs_sum_squared(complex_volume, channels, cube_output);
      manual_magnitude_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(manual_magnitude_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_magnitude_sum_squared", "manual", type_name, logical_size, config,
                               manual_magnitude_ns, manual_magnitude_checksum,
                               ksj::benchmarks::reference_row("channel_volume_magnitude_sum_squared", "output_reuse"));

    double detail_magnitude_checksum = 0.0;
    const auto detail_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      [[maybe_unused]] const auto success = ksj::array::detail::eigen::multiply_cube_by_abs_sum_squared_contiguous(
        complex_volume_input, channels_input, cube_output.view(), ksj::array::Dim::dim0);
      detail_magnitude_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(detail_magnitude_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_magnitude_sum_squared", "detail_contiguous", type_name, logical_size,
                               config, detail_magnitude_ns, detail_magnitude_checksum,
                               ksj::benchmarks::candidate_row("channel_volume_magnitude_sum_squared", "output_reuse"));

    double public_magnitude_checksum = 0.0;
    const auto public_magnitude_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::multiply_cube_by_abs_sum_squared(complex_volume_input, channels_input, cube_output.view(),
                                                   ksj::array::Dim::dim0);
      public_magnitude_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(public_magnitude_checksum);
    });
    ksj::benchmarks::print_row("channel_volume_magnitude_sum_squared", "public", type_name, logical_size, config,
                               public_magnitude_ns, public_magnitude_checksum,
                               ksj::benchmarks::candidate_row("channel_volume_magnitude_sum_squared", "output_reuse"));

    constexpr float epsilon = 1.0e-6F;
    constexpr float scale = 0.5F;
    double separate_phasor_checksum = 0.0;
    const auto separate_phasor_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::fill(cube_output.view(), ksj::base::cf32{});
      ksj::array::complex_unit_phasor(complex_volume_input, phasor_output.view(), epsilon);
      ksj::array::accumulate_conjugate_product_scaled(cube_output.view(), phasor_output.view(), complex_volume_input,
                                                      scale);
      separate_phasor_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(separate_phasor_checksum);
    });
    ksj::benchmarks::print_row(
      "channel_volume_phasor_conjugate_product", "separate_public", type_name, logical_size, config, separate_phasor_ns,
      separate_phasor_checksum,
      ksj::benchmarks::reference_row("channel_volume_phasor_conjugate_product", "output_reuse"));

    double fused_phasor_checksum = 0.0;
    const auto fused_phasor_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::fill(cube_output.view(), ksj::base::cf32{});
      ksj::array::complex_unit_phasor_and_accumulate_conjugate_product(
        complex_volume_input, phasor_output.view(), cube_output.view(), complex_volume_input, epsilon, scale);
      fused_phasor_checksum = checksum_cube(cube_output);
      ksj::benchmarks::do_not_optimize(fused_phasor_checksum);
    });
    ksj::benchmarks::print_row(
      "channel_volume_phasor_conjugate_product", "public_fused", type_name, logical_size, config, fused_phasor_ns,
      fused_phasor_checksum, ksj::benchmarks::candidate_row("channel_volume_phasor_conjugate_product", "output_reuse"));
  }
}

} // namespace

void run_channel_volume_benchmarks_complex_float(const ksj::benchmarks::Config& config) {
  run_channel_volume_benchmarks("complex_float", config);
}

} // namespace ksj::benchmarks::array_benchmarks
