#include "array_benchmark_common.hpp"

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_dimwise.hpp"

#include <algorithm>
#include <complex>
#include <string_view>
#include <utility>

namespace ksj::benchmarks::array_benchmarks {
namespace {

void fill_complex_array4d(ksj::array::PooledArray4D<ksj::base::cf32>& array) {
  for (std::size_t channel = 0U; channel < array.dim0(); ++channel) {
    for (std::size_t row = 0U; row < array.dim1(); ++row) {
      for (std::size_t col = 0U; col < array.dim2(); ++col) {
        for (std::size_t slice = 0U; slice < array.dim3(); ++slice) {
          const auto real = static_cast<float>((channel * 23U + row * 17U + col * 31U + slice * 7U) % 251U) * 0.125F;
          const auto imag = static_cast<float>((channel * 29U + row * 13U + col * 19U + slice * 11U) % 127U) * 0.0625F;
          array(channel, row, col, slice) = {real, imag};
        }
      }
    }
  }
}

[[nodiscard]] double checksum_complex_matrix(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix) {
  double checksum = 0.0;
  for (std::size_t index = 0U; index < matrix.size(); ++index) {
    checksum += static_cast<double>(matrix[index].real() + matrix[index].imag());
  }
  return checksum;
}

void manual_extract_sliding_patch_matrix(const ksj::array::PooledArray4D<ksj::base::cf32>& input,
                                         const std::size_t kernel_rows, const std::size_t kernel_cols,
                                         const std::size_t kernel_slices,
                                         ksj::array::PooledMatrix<ksj::base::cf32>& output) {
  const auto patch_rows = input.dim1() - kernel_rows + 1U;
  const auto patch_cols = input.dim2() - kernel_cols + 1U;
  const auto patch_slices = input.dim3() - kernel_slices + 1U;
  const auto kernel_size = kernel_rows * kernel_cols * kernel_slices;

  for (std::size_t channel = 0U; channel < input.dim0(); ++channel) {
    for (std::size_t patch_row = 0U; patch_row < patch_rows; ++patch_row) {
      for (std::size_t patch_col = 0U; patch_col < patch_cols; ++patch_col) {
        for (std::size_t patch_slice = 0U; patch_slice < patch_slices; ++patch_slice) {
          const auto output_row = (patch_row * patch_cols + patch_col) * patch_slices + patch_slice;
          for (std::size_t kernel_row = 0U; kernel_row < kernel_rows; ++kernel_row) {
            for (std::size_t kernel_col = 0U; kernel_col < kernel_cols; ++kernel_col) {
              for (std::size_t kernel_slice = 0U; kernel_slice < kernel_slices; ++kernel_slice) {
                const auto kernel_index = (kernel_row * kernel_cols + kernel_col) * kernel_slices + kernel_slice;
                const auto output_col = kernel_size * channel + kernel_index;
                output(output_row, output_col) =
                  input(channel, patch_row + kernel_row, patch_col + kernel_col, patch_slice + kernel_slice);
              }
            }
          }
        }
      }
    }
  }
}

void run_calibration_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto element_count : config.sizes) {
    constexpr std::size_t kernel_rows = 3U;
    constexpr std::size_t kernel_cols = 3U;
    constexpr std::size_t kernel_slices = 3U;

    const auto shape4d = calibration_shape4d(element_count);
    const auto channels = shape4d.dim0;
    const auto rows = shape4d.dim1;
    const auto cols = shape4d.dim2;
    const auto slices = shape4d.dim3;
    if (rows < kernel_rows || cols < kernel_cols || slices < kernel_slices) {
      continue;
    }
    const auto patch_rows = rows - kernel_rows + 1U;
    const auto patch_cols = cols - kernel_cols + 1U;
    const auto patch_slices = slices - kernel_slices + 1U;
    const auto matrix_rows = patch_rows * patch_cols * patch_slices;
    const auto matrix_cols = channels * kernel_rows * kernel_cols * kernel_slices;
    const auto logical_size = element_count;

    auto input = ksj::array::make_pooled_array4d<ksj::base::cf32>(channels, rows, cols, slices);
    auto output = ksj::array::make_pooled_matrix<ksj::base::cf32>(matrix_rows, matrix_cols);
    ksj::benchmarks::require_pooled_storage("calibration_input", input);
    ksj::benchmarks::require_pooled_storage("calibration_output", output);
    fill_complex_array4d(input);

    double manual_checksum = 0.0;
    const auto manual_ns = ksj::benchmarks::measure(config, [&] {
      manual_extract_sliding_patch_matrix(input, kernel_rows, kernel_cols, kernel_slices, output);
      manual_checksum = checksum_complex_matrix(output);
      ksj::benchmarks::do_not_optimize(manual_checksum);
    });
    ksj::benchmarks::print_row("calibration_patch_matrix", "manual", type_name, logical_size, config, manual_ns,
                               manual_checksum,
                               ksj::benchmarks::reference_row("calibration_patch_matrix", "output_reuse"));

    double detail_checksum = 0.0;
    const auto detail_ns = ksj::benchmarks::measure(config, [&] {
      [[maybe_unused]] const auto success = ksj::array::detail::eigen::extract_sliding_patch_matrix_contiguous(
        std::as_const(input).view(), kernel_rows, kernel_cols, kernel_slices, output.view());
      detail_checksum = checksum_complex_matrix(output);
      ksj::benchmarks::do_not_optimize(detail_checksum);
    });
    ksj::benchmarks::print_row("calibration_patch_matrix", "detail_contiguous", type_name, logical_size, config,
                               detail_ns, detail_checksum,
                               ksj::benchmarks::candidate_row("calibration_patch_matrix", "output_reuse"));

    double public_checksum = 0.0;
    const auto public_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::extract_sliding_patch_matrix(std::as_const(input).view(), kernel_rows, kernel_cols, kernel_slices,
                                               output.view());
      public_checksum = checksum_complex_matrix(output);
      ksj::benchmarks::do_not_optimize(public_checksum);
    });
    ksj::benchmarks::print_row("calibration_patch_matrix", "public", type_name, logical_size, config, public_ns,
                               public_checksum,
                               ksj::benchmarks::candidate_row("calibration_patch_matrix", "output_reuse"));
  }
}

} // namespace

void run_calibration_benchmarks_complex_float(const ksj::benchmarks::Config& config) {
  run_calibration_benchmarks("complex_float", config);
}

} // namespace ksj::benchmarks::array_benchmarks
