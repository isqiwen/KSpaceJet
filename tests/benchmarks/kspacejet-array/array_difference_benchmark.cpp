#include "array_benchmark_common.hpp"
#include "kspacejet/base/types.hpp"

#include <algorithm>

namespace ksj::benchmarks::array_benchmarks {
namespace {

[[nodiscard]] std::size_t row_major_cube_index(const std::size_t dim0, const std::size_t dim1, const std::size_t dim2,
                                               const std::size_t dim1_extent, const std::size_t dim2_extent) noexcept {
  return (dim0 * dim1_extent + dim1) * dim2_extent + dim2;
}

void fill_complex_cube(ksj::array::PooledCube<ksj::base::cf32>& cube) {
  for (std::size_t dim0 = 0U; dim0 < cube.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < cube.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < cube.dim2(); ++dim2) {
        const auto real = static_cast<float>((dim0 * 17U + dim1 * 31U + dim2 * 7U) % 251U) * 0.125F;
        const auto imag = static_cast<float>((dim0 * 13U + dim1 * 19U + dim2 * 11U) % 127U) * 0.0625F;
        cube(dim0, dim1, dim2) = {real, imag};
      }
    }
  }
}

void fill_complex_array4d(ksj::array::PooledArray4D<ksj::base::cf32>& array) {
  for (std::size_t dim0 = 0U; dim0 < array.dim0(); ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < array.dim1(); ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < array.dim2(); ++dim2) {
        for (std::size_t dim3 = 0U; dim3 < array.dim3(); ++dim3) {
          const auto real = static_cast<float>((dim0 * 29U + dim1 * 17U + dim2 * 31U + dim3 * 7U) % 251U) * 0.125F;
          const auto imag = static_cast<float>((dim0 * 23U + dim1 * 13U + dim2 * 19U + dim3 * 11U) % 127U) * 0.0625F;
          array(dim0, dim1, dim2, dim3) = {real, imag};
        }
      }
    }
  }
}

[[nodiscard]] double checksum_complex_cube(const ksj::array::PooledCube<ksj::base::cf32>& cube) {
  double checksum = 0.0;
  for (std::size_t index = 0U; index < cube.size(); ++index) {
    checksum += static_cast<double>(cube.data()[index].real() + cube.data()[index].imag());
  }
  return checksum;
}

[[nodiscard]] double checksum_complex_array4d(const ksj::array::PooledArray4D<ksj::base::cf32>& array) {
  double checksum = 0.0;
  for (std::size_t index = 0U; index < array.size(); ++index) {
    checksum += static_cast<double>(array.data()[index].real() + array.data()[index].imag());
  }
  return checksum;
}

void manual_forward_difference_zero_axis0(const ksj::array::PooledCube<ksj::base::cf32>& input,
                                          ksj::array::PooledCube<ksj::base::cf32>& output) {
  const auto dim0_extent = input.dim0();
  const auto dim1_extent = input.dim1();
  const auto dim2_extent = input.dim2();
  for (std::size_t dim0 = 0U; dim0 < dim0_extent; ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < dim1_extent; ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < dim2_extent; ++dim2) {
        const auto index = row_major_cube_index(dim0, dim1, dim2, dim1_extent, dim2_extent);
        if (dim0 + 1U < dim0_extent) {
          output.data()[index] =
            input.data()[row_major_cube_index(dim0 + 1U, dim1, dim2, dim1_extent, dim2_extent)] - input.data()[index];
        } else {
          output.data()[index] = {};
        }
      }
    }
  }
}

void manual_forward_difference_periodic_axis2(const ksj::array::PooledCube<ksj::base::cf32>& input,
                                              ksj::array::PooledCube<ksj::base::cf32>& output) {
  const auto dim0_extent = input.dim0();
  const auto dim1_extent = input.dim1();
  const auto dim2_extent = input.dim2();
  for (std::size_t dim0 = 0U; dim0 < dim0_extent; ++dim0) {
    for (std::size_t dim1 = 0U; dim1 < dim1_extent; ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < dim2_extent; ++dim2) {
        const auto index = row_major_cube_index(dim0, dim1, dim2, dim1_extent, dim2_extent);
        const auto next_dim2 = dim2 + 1U < dim2_extent ? dim2 + 1U : 0U;
        output.data()[index] =
          input.data()[row_major_cube_index(dim0, dim1, next_dim2, dim1_extent, dim2_extent)] - input.data()[index];
      }
    }
  }
}

void manual_adjoint_forward_difference_periodic_axis0(const ksj::array::PooledCube<ksj::base::cf32>& input,
                                                      ksj::array::PooledCube<ksj::base::cf32>& output) {
  const auto dim0_extent = input.dim0();
  const auto dim1_extent = input.dim1();
  const auto dim2_extent = input.dim2();
  for (std::size_t dim0 = 0U; dim0 < dim0_extent; ++dim0) {
    const auto previous_dim0 = dim0 == 0U ? dim0_extent - 1U : dim0 - 1U;
    for (std::size_t dim1 = 0U; dim1 < dim1_extent; ++dim1) {
      for (std::size_t dim2 = 0U; dim2 < dim2_extent; ++dim2) {
        const auto index = row_major_cube_index(dim0, dim1, dim2, dim1_extent, dim2_extent);
        output.data()[index] =
          input.data()[row_major_cube_index(previous_dim0, dim1, dim2, dim1_extent, dim2_extent)] - input.data()[index];
      }
    }
  }
}

void manual_forward_difference_periodic_axis0(const ksj::array::PooledArray4D<ksj::base::cf32>& input,
                                              ksj::array::PooledArray4D<ksj::base::cf32>& output) {
  const auto volume_size = input.dim1() * input.dim2() * input.dim3();
  for (std::size_t dim0 = 0U; dim0 < input.dim0(); ++dim0) {
    const auto next_dim0 = dim0 + 1U < input.dim0() ? dim0 + 1U : 0U;
    const auto source_offset = dim0 * volume_size;
    const auto next_offset = next_dim0 * volume_size;
    for (std::size_t index = 0U; index < volume_size; ++index) {
      output.data()[source_offset + index] = input.data()[next_offset + index] - input.data()[source_offset + index];
    }
  }
}

void manual_adjoint_forward_difference_periodic_axis0(const ksj::array::PooledArray4D<ksj::base::cf32>& input,
                                                      ksj::array::PooledArray4D<ksj::base::cf32>& output) {
  const auto volume_size = input.dim1() * input.dim2() * input.dim3();
  for (std::size_t dim0 = 0U; dim0 < input.dim0(); ++dim0) {
    const auto previous_dim0 = dim0 == 0U ? input.dim0() - 1U : dim0 - 1U;
    const auto source_offset = dim0 * volume_size;
    const auto previous_offset = previous_dim0 * volume_size;
    for (std::size_t index = 0U; index < volume_size; ++index) {
      output.data()[source_offset + index] =
        input.data()[previous_offset + index] - input.data()[source_offset + index];
    }
  }
}

void run_difference_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto element_count : config.sizes) {
    const auto shape3d = element_count_shape3d(element_count);
    const auto rows = shape3d.dim0;
    const auto cols = shape3d.dim1;
    const auto slices = shape3d.dim2;
    const auto logical_size = element_count;

    auto input = ksj::array::make_pooled_cube<ksj::base::cf32>(rows, cols, slices);
    auto output = ksj::array::make_pooled_cube<ksj::base::cf32>(rows, cols, slices);
    ksj::benchmarks::require_pooled_storage("cube_difference_input", input);
    ksj::benchmarks::require_pooled_storage("cube_difference_output", output);
    fill_complex_cube(input);

    double manual_zero_checksum = 0.0;
    const auto manual_zero_ns = ksj::benchmarks::measure(config, [&] {
      manual_forward_difference_zero_axis0(input, output);
      manual_zero_checksum = checksum_complex_cube(output);
      ksj::benchmarks::do_not_optimize(manual_zero_checksum);
    });
    ksj::benchmarks::print_row("cube_forward_difference", "manual_zero_axis0", type_name, logical_size, config,
                               manual_zero_ns, manual_zero_checksum,
                               ksj::benchmarks::reference_row("cube_forward_difference_zero_axis0", "output_reuse"));

    double public_zero_checksum = 0.0;
    const auto public_zero_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::zero);
      public_zero_checksum = checksum_complex_cube(output);
      ksj::benchmarks::do_not_optimize(public_zero_checksum);
    });
    ksj::benchmarks::print_row("cube_forward_difference", "public_zero_axis0", type_name, logical_size, config,
                               public_zero_ns, public_zero_checksum,
                               ksj::benchmarks::candidate_row("cube_forward_difference_zero_axis0", "output_reuse"));

    double manual_periodic_checksum = 0.0;
    const auto manual_periodic_ns = ksj::benchmarks::measure(config, [&] {
      manual_forward_difference_periodic_axis2(input, output);
      manual_periodic_checksum = checksum_complex_cube(output);
      ksj::benchmarks::do_not_optimize(manual_periodic_checksum);
    });
    ksj::benchmarks::print_row(
      "cube_forward_difference", "manual_periodic_z", type_name, logical_size, config, manual_periodic_ns,
      manual_periodic_checksum,
      ksj::benchmarks::reference_row("cube_forward_difference_periodic_axis2", "output_reuse"));

    double public_periodic_checksum = 0.0;
    const auto public_periodic_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::forward_difference(input.view(), output.view(), 2U, ksj::array::DifferenceBoundary::periodic);
      public_periodic_checksum = checksum_complex_cube(output);
      ksj::benchmarks::do_not_optimize(public_periodic_checksum);
    });
    ksj::benchmarks::print_row(
      "cube_forward_difference", "public_periodic_z", type_name, logical_size, config, public_periodic_ns,
      public_periodic_checksum,
      ksj::benchmarks::candidate_row("cube_forward_difference_periodic_axis2", "output_reuse"));

    double manual_adjoint_checksum = 0.0;
    const auto manual_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      manual_adjoint_forward_difference_periodic_axis0(input, output);
      manual_adjoint_checksum = checksum_complex_cube(output);
      ksj::benchmarks::do_not_optimize(manual_adjoint_checksum);
    });
    ksj::benchmarks::print_row("cube_adjoint_difference", "manual_periodic_x", type_name, logical_size, config,
                               manual_adjoint_ns, manual_adjoint_checksum,
                               ksj::benchmarks::reference_row("cube_adjoint_difference", "output_reuse"));

    double public_adjoint_checksum = 0.0;
    const auto public_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::adjoint_forward_difference(input.view(), output.view(), 0U, ksj::array::DifferenceBoundary::periodic);
      public_adjoint_checksum = checksum_complex_cube(output);
      ksj::benchmarks::do_not_optimize(public_adjoint_checksum);
    });
    ksj::benchmarks::print_row("cube_adjoint_difference", "public_periodic_x", type_name, logical_size, config,
                               public_adjoint_ns, public_adjoint_checksum,
                               ksj::benchmarks::candidate_row("cube_adjoint_difference", "output_reuse"));

    const auto shape4d = element_count_shape4d(element_count);
    auto array4d_input =
      ksj::array::make_pooled_array4d<ksj::base::cf32>(shape4d.dim0, shape4d.dim1, shape4d.dim2, shape4d.dim3);
    auto array4d_output =
      ksj::array::make_pooled_array4d<ksj::base::cf32>(shape4d.dim0, shape4d.dim1, shape4d.dim2, shape4d.dim3);
    ksj::benchmarks::require_pooled_storage("array4d_difference_input", array4d_input);
    ksj::benchmarks::require_pooled_storage("array4d_difference_output", array4d_output);
    fill_complex_array4d(array4d_input);
    const auto array4d_logical_size = array4d_input.size();

    double manual_4d_forward_checksum = 0.0;
    const auto manual_4d_forward_ns = ksj::benchmarks::measure(config, [&] {
      manual_forward_difference_periodic_axis0(array4d_input, array4d_output);
      manual_4d_forward_checksum = checksum_complex_array4d(array4d_output);
      ksj::benchmarks::do_not_optimize(manual_4d_forward_checksum);
    });
    ksj::benchmarks::print_row("array4d_forward_difference", "manual_periodic_echo", type_name, array4d_logical_size,
                               config, manual_4d_forward_ns, manual_4d_forward_checksum,
                               ksj::benchmarks::reference_row("array4d_forward_difference", "output_reuse"));

    double public_4d_forward_checksum = 0.0;
    const auto public_4d_forward_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::forward_difference(array4d_input.view(), array4d_output.view(), 0U,
                                     ksj::array::DifferenceBoundary::periodic);
      public_4d_forward_checksum = checksum_complex_array4d(array4d_output);
      ksj::benchmarks::do_not_optimize(public_4d_forward_checksum);
    });
    ksj::benchmarks::print_row("array4d_forward_difference", "public_periodic_echo", type_name, array4d_logical_size,
                               config, public_4d_forward_ns, public_4d_forward_checksum,
                               ksj::benchmarks::candidate_row("array4d_forward_difference", "output_reuse"));

    double manual_4d_adjoint_checksum = 0.0;
    const auto manual_4d_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      manual_adjoint_forward_difference_periodic_axis0(array4d_input, array4d_output);
      manual_4d_adjoint_checksum = checksum_complex_array4d(array4d_output);
      ksj::benchmarks::do_not_optimize(manual_4d_adjoint_checksum);
    });
    ksj::benchmarks::print_row("array4d_adjoint_difference", "manual_periodic_echo", type_name, array4d_logical_size,
                               config, manual_4d_adjoint_ns, manual_4d_adjoint_checksum,
                               ksj::benchmarks::reference_row("array4d_adjoint_difference", "output_reuse"));

    double public_4d_adjoint_checksum = 0.0;
    const auto public_4d_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::adjoint_forward_difference(array4d_input.view(), array4d_output.view(), 0U,
                                             ksj::array::DifferenceBoundary::periodic);
      public_4d_adjoint_checksum = checksum_complex_array4d(array4d_output);
      ksj::benchmarks::do_not_optimize(public_4d_adjoint_checksum);
    });
    ksj::benchmarks::print_row("array4d_adjoint_difference", "public_periodic_echo", type_name, array4d_logical_size,
                               config, public_4d_adjoint_ns, public_4d_adjoint_checksum,
                               ksj::benchmarks::candidate_row("array4d_adjoint_difference", "output_reuse"));
  }
}

} // namespace

void run_difference_benchmarks_complex_float(const ksj::benchmarks::Config& config) {
  run_difference_benchmarks("complex_float", config);
}

} // namespace ksj::benchmarks::array_benchmarks
