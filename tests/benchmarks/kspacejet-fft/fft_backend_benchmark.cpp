#include "benchmark_common.hpp"
#include "kspacejet/fft/fft.hpp"
#include "kspacejet/fft/detail/eigen/eigen_fft_transforms.hpp"
#include "kspacejet/fft/detail/intel/intel_fft_transforms.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <string_view>

namespace {

inline constexpr std::size_t kMaxTwoDimensionalSize = 512;
inline constexpr std::size_t kMaxThreeDimensionalSize = 128;
inline constexpr std::size_t kMaxThreeDimensionalSlices = 16;
inline constexpr std::size_t kFftConvolutionKernelSize = 15;
inline constexpr std::size_t kMaxDirectFftConvolutionSize = 64;
inline constexpr std::size_t kBatchSize = 8;
inline constexpr std::size_t kVolumeBatchSize = 4;
inline constexpr std::size_t kSegmentCount = 8;
inline constexpr std::size_t kMatrixSegmentCount = 4;

[[nodiscard]] ksj::benchmarks::RowMetadata fft_benchmark_row_metadata(const std::string_view case_name,
                                                                      const std::string_view backend,
                                                                      const std::string_view type_name) {
  const auto absolute_tolerance = case_name == "fft2" ? 1.0e-3 : -1.0;
  const auto relative_tolerance = case_name == "fft1d" && type_name == "complex_float" ? 1.5e-5 : -1.0;
  const auto timing_scope = backend == "plan_execute" || backend == "eigen_warm" ? std::string_view{"warm_plan"}
                            : backend == "plan_create_execute"                   ? std::string_view{"cold_plan"}
                            : backend == "public_api" || backend == "loop_alloc" ? std::string_view{"allocating"}
                                                                                 : std::string_view{"output_reuse"};
  if (backend.starts_with("eigen") || backend == "direct" || backend.starts_with("loop_") ||
      backend == "subview_loop" || backend == "fft2_inplace_shifted") {
    return ksj::benchmarks::reference_row(case_name, timing_scope, absolute_tolerance, relative_tolerance);
  }
  return ksj::benchmarks::candidate_row(case_name, timing_scope, absolute_tolerance, relative_tolerance);
}

inline void print_fft_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                    const std::string_view type_name, const std::size_t size,
                                    const ksj::benchmarks::Config& config,
                                    const ksj::benchmarks::Measurement& measurement, const double checksum) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum,
                             fft_benchmark_row_metadata(case_name, backend, type_name));
}

inline void print_fft_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                    const std::string_view type_name, const std::size_t size,
                                    const ksj::benchmarks::Config& config,
                                    const ksj::benchmarks::Measurement& measurement, const double checksum,
                                    const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum, metadata);
}

template <typename T> void fill_complex_matrix(ksj::array::PooledMatrix<std::complex<T>>& matrix) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      const auto real = static_cast<T>(static_cast<double>((row * 17U + col * 31U) % 251U) * 0.125);
      const auto imag = static_cast<T>(static_cast<double>((row * 13U + col * 19U) % 127U) * 0.0625);
      matrix(row, col) = {real, imag};
    }
  }
}

template <typename T>
[[nodiscard]] double checksum_complex_matrix(const ksj::array::PooledMatrix<std::complex<T>>& matrix) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    checksum += static_cast<double>(matrix.data()[index].real() + matrix.data()[index].imag());
  }
  return checksum;
}

template <typename T> void fill_complex_cube(ksj::array::PooledCube<std::complex<T>>& cube) {
  for (std::size_t slice = 0; slice < cube.dim2(); ++slice) {
    for (std::size_t col = 0; col < cube.dim1(); ++col) {
      for (std::size_t row = 0; row < cube.dim0(); ++row) {
        const auto real = static_cast<T>(static_cast<double>((row * 17U + col * 31U + slice * 7U) % 251U) * 0.125);
        const auto imag = static_cast<T>(static_cast<double>((row * 13U + col * 19U + slice * 11U) % 127U) * 0.0625);
        cube(row, col, slice) = {real, imag};
      }
    }
  }
}

template <typename T> [[nodiscard]] double checksum_complex_cube(const ksj::array::PooledCube<std::complex<T>>& cube) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < cube.size(); ++index) {
    checksum += static_cast<double>(cube.data()[index].real() + cube.data()[index].imag());
  }
  return checksum;
}

template <typename T> void fill_complex_array4d(ksj::array::PooledArray4D<std::complex<T>>& array) {
  for (std::size_t batch = 0; batch < array.dim3(); ++batch) {
    for (std::size_t slice = 0; slice < array.dim2(); ++slice) {
      for (std::size_t col = 0; col < array.dim1(); ++col) {
        for (std::size_t row = 0; row < array.dim0(); ++row) {
          const auto real =
            static_cast<T>(static_cast<double>((row * 17U + col * 31U + slice * 7U + batch * 5U) % 251U) * 0.125);
          const auto imag =
            static_cast<T>(static_cast<double>((row * 13U + col * 19U + slice * 11U + batch * 23U) % 127U) * 0.0625);
          array(row, col, slice, batch) = {real, imag};
        }
      }
    }
  }
}

template <typename T>
[[nodiscard]] double checksum_complex_array4d(const ksj::array::PooledArray4D<std::complex<T>>& array) {
  double checksum = 0.0;
  for (std::size_t index = 0; index < array.size(); ++index) {
    checksum += static_cast<double>(array.data()[index].real() + array.data()[index].imag());
  }
  return checksum;
}

template <typename T> [[nodiscard]] ksj::array::PooledMatrix<std::complex<T>> make_complex_kernel(std::size_t size) {
  auto kernel = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
  T scale{};
  for (std::size_t col = 0; col < kernel.cols(); ++col) {
    for (std::size_t row = 0; row < kernel.rows(); ++row) {
      const auto real = static_cast<T>(static_cast<double>((row * 5U + col * 11U) % 17U) * 0.01);
      const auto imag = static_cast<T>(static_cast<double>((row * 7U + col * 3U) % 13U) * -0.005);
      kernel(row, col) = {real, imag};
      scale += static_cast<T>(std::abs(real) + std::abs(imag));
    }
  }
  if (scale != T{}) {
    for (std::size_t index = 0; index < kernel.size(); ++index) {
      kernel.data()[index] /= scale;
    }
  }
  return kernel;
}

template <typename T>
void convolve2d_full_reference(const ksj::array::PooledMatrix<std::complex<T>>& input,
                               const ksj::array::PooledMatrix<std::complex<T>>& kernel,
                               ksj::array::PooledMatrix<std::complex<T>>& output) {
  std::fill(output.data(), output.data() + output.size(), std::complex<T>{});
  for (std::size_t input_col = 0; input_col < input.cols(); ++input_col) {
    for (std::size_t input_row = 0; input_row < input.rows(); ++input_row) {
      for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
        for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
          output(input_row + kernel_row, input_col + kernel_col) +=
            input(input_row, input_col) * kernel(kernel_row, kernel_col);
        }
      }
    }
  }
}

template <typename T>
void correlate2d_full_reference(const ksj::array::PooledMatrix<std::complex<T>>& input,
                                const ksj::array::PooledMatrix<std::complex<T>>& kernel,
                                ksj::array::PooledMatrix<std::complex<T>>& output) {
  std::fill(output.data(), output.data() + output.size(), std::complex<T>{});
  for (std::size_t input_col = 0; input_col < input.cols(); ++input_col) {
    for (std::size_t input_row = 0; input_row < input.rows(); ++input_row) {
      for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
        for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
          output(input_row + kernel_row, input_col + kernel_col) +=
            input(input_row, input_col) *
            std::conj(kernel(kernel.rows() - 1U - kernel_row, kernel.cols() - 1U - kernel_col));
        }
      }
    }
  }
}

template <typename T>
void convolve2d_window_reference(const ksj::array::PooledMatrix<std::complex<T>>& input,
                                 const ksj::array::PooledMatrix<std::complex<T>>& kernel,
                                 ksj::array::PooledMatrix<std::complex<T>>& output, const std::size_t row_offset,
                                 const std::size_t col_offset) {
  for (std::size_t output_col = 0; output_col < output.cols(); ++output_col) {
    for (std::size_t output_row = 0; output_row < output.rows(); ++output_row) {
      const auto full_row = output_row + row_offset;
      const auto full_col = output_col + col_offset;
      std::complex<T> sum{};
      for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
        if (full_col < kernel_col) {
          continue;
        }
        const auto input_col = full_col - kernel_col;
        if (input_col >= input.cols()) {
          continue;
        }
        for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
          if (full_row < kernel_row) {
            continue;
          }
          const auto input_row = full_row - kernel_row;
          if (input_row >= input.rows()) {
            continue;
          }
          sum += input(input_row, input_col) * kernel(kernel_row, kernel_col);
        }
      }
      output(output_row, output_col) = sum;
    }
  }
}

template <typename T>
void correlate2d_window_reference(const ksj::array::PooledMatrix<std::complex<T>>& input,
                                  const ksj::array::PooledMatrix<std::complex<T>>& kernel,
                                  ksj::array::PooledMatrix<std::complex<T>>& output, const std::size_t row_offset,
                                  const std::size_t col_offset) {
  for (std::size_t output_col = 0; output_col < output.cols(); ++output_col) {
    for (std::size_t output_row = 0; output_row < output.rows(); ++output_row) {
      const auto full_row = output_row + row_offset;
      const auto full_col = output_col + col_offset;
      std::complex<T> sum{};
      for (std::size_t kernel_col = 0; kernel_col < kernel.cols(); ++kernel_col) {
        if (full_col < kernel_col) {
          continue;
        }
        const auto input_col = full_col - kernel_col;
        if (input_col >= input.cols()) {
          continue;
        }
        for (std::size_t kernel_row = 0; kernel_row < kernel.rows(); ++kernel_row) {
          if (full_row < kernel_row) {
            continue;
          }
          const auto input_row = full_row - kernel_row;
          if (input_row >= input.rows()) {
            continue;
          }
          sum += input(input_row, input_col) *
                 std::conj(kernel(kernel.rows() - 1U - kernel_row, kernel.cols() - 1U - kernel_col));
        }
      }
      output(output_row, output_col) = sum;
    }
  }
}

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_vector<std::complex<T>>(size);
    auto output = ksj::array::make_pooled_vector<std::complex<T>>(size);
    ksj::benchmarks::require_pooled_storage("input", input);
    ksj::benchmarks::require_pooled_storage("output", output);
    ksj::benchmarks::fill_vector(input);

    ksj::fft::detail::eigen::fft_1d(input, output, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
    const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::detail::eigen::fft_1d(input, output, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
    });
    print_fft_benchmark_row("fft1d", "eigen", type_name, size, config, eigen_ns, ksj::benchmarks::checksum(output));

    if (ksj::fft::detail::intel::fft_1d(input, output, ksj::fft::Direction::forward, ksj::fft::Normalization::none)) {
      const auto intel_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::fft::detail::intel::fft_1d(input, output, ksj::fft::Direction::forward,
                                              ksj::fft::Normalization::none);
      });
      print_fft_benchmark_row("fft1d", "intel_mkl", type_name, size, config, intel_ns,
                              ksj::benchmarks::checksum(output));
    }

    ksj::fft::fft(input, output, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
    const auto free_policy_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft(input, output, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    const auto selected_backend =
      ksj::fft::detail::prefer_intel_fft<T>(size) ? std::string_view{"intel_mkl"} : std::string_view{"eigen"};
    print_fft_benchmark_row("fft1d", "free_api_policy", type_name, size, config, free_policy_ns,
                            ksj::benchmarks::checksum(output),
                            ksj::benchmarks::policy_row("fft1d", "output_reuse", selected_backend, -1.0,
                                                        type_name == "complex_float" ? 1.5e-5 : -1.0));

    const auto eigen_warm_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::detail::eigen::fft_1d(input, output, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_fft_benchmark_row("fft1d", "eigen_warm", type_name, size, config, eigen_warm_ns,
                            ksj::benchmarks::checksum(output));

    ksj::fft::Fft1Plan<T> fft1_plan(size);
    fft1_plan.execute(input, output);
    const auto plan_ns = ksj::benchmarks::measure(config, [&] {
      fft1_plan.execute(input, output);
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_fft_benchmark_row("fft1d", "plan_execute", type_name, size, config, plan_ns,
                            ksj::benchmarks::checksum(output));

    const auto cold_plan_ns = ksj::benchmarks::measure(config, [&] {
      auto plan = ksj::fft::Fft1Plan<T>(size);
      plan.execute(input, output);
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_fft_benchmark_row("fft1d", "plan_create_execute", type_name, size, config, cold_plan_ns,
                            ksj::benchmarks::checksum(output));

    auto segmented_input = ksj::array::make_pooled_vector<std::complex<T>>(size * kSegmentCount);
    auto segmented_output = ksj::array::make_pooled_vector<std::complex<T>>(size * kSegmentCount);
    ksj::benchmarks::fill_vector(segmented_input);

    double fft1d_segmented_output_checksum = 0.0;
    const auto fft1d_segmented_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft_segmented(segmented_input, segmented_output, kSegmentCount);
      fft1d_segmented_output_checksum = ksj::benchmarks::checksum(segmented_output);
      ksj::benchmarks::do_not_optimize(fft1d_segmented_output_checksum);
    });
    print_fft_benchmark_row("fft1d_segmented_x8", "api", type_name, size, config, fft1d_segmented_output_ns,
                            fft1d_segmented_output_checksum);

    double fft1d_segmented_public_checksum = 0.0;
    const auto fft1d_segmented_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::fft_segmented(segmented_input, kSegmentCount);
      fft1d_segmented_public_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(fft1d_segmented_public_checksum);
    });
    print_fft_benchmark_row("fft1d_segmented_x8", "public_api", type_name, size, config, fft1d_segmented_public_ns,
                            fft1d_segmented_public_checksum);

    if (size > kMaxTwoDimensionalSize) {
      continue;
    }

    auto matrix_input = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
    auto matrix_output = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
    ksj::benchmarks::require_pooled_storage("matrix_input", matrix_input);
    ksj::benchmarks::require_pooled_storage("matrix_output", matrix_output);
    fill_complex_matrix(matrix_input);

    ksj::fft::fft2(matrix_input, matrix_output);
    const auto fft2_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft2(matrix_input, matrix_output);
      ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
    });
    print_fft_benchmark_row("fft2", "api", type_name, size, config, fft2_output_ns,
                            checksum_complex_matrix(matrix_output));

    if (ksj::fft::detail::intel::fft_2d(matrix_input, matrix_output, ksj::fft::Direction::forward,
                                        ksj::fft::Normalization::none)) {
      const auto fft2_intel_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::fft::detail::intel::fft_2d(matrix_input, matrix_output, ksj::fft::Direction::forward,
                                              ksj::fft::Normalization::none);
        ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
      });
      print_fft_benchmark_row("fft2", "intel_mkl", type_name, size, config, fft2_intel_ns,
                              checksum_complex_matrix(matrix_output));
    }

    ksj::fft::Fft2Plan<T> plan(size, size);
    plan.execute(matrix_input, matrix_output);
    const auto fft2_plan_ns = ksj::benchmarks::measure(config, [&] {
      plan.execute(matrix_input, matrix_output);
      ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
    });
    print_fft_benchmark_row("fft2", "plan_execute", type_name, size, config, fft2_plan_ns,
                            checksum_complex_matrix(matrix_output));

    double fft2_public_checksum = 0.0;
    const auto fft2_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::fft2(matrix_input);
      fft2_public_checksum = checksum_complex_matrix(public_output);
      ksj::benchmarks::do_not_optimize(fft2_public_checksum);
    });
    print_fft_benchmark_row("fft2", "public_api", type_name, size, config, fft2_public_ns, fft2_public_checksum);

    auto matrix_inplace = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
    ksj::benchmarks::require_pooled_storage("matrix_inplace", matrix_inplace);

    double centered_fft2_inplace_checksum = 0.0;
    const auto centered_fft2_inplace_ns = ksj::benchmarks::measure(config, [&] {
      std::copy(matrix_input.data(), matrix_input.data() + matrix_input.size(), matrix_inplace.data());
      ksj::fft::fft2_inplace(matrix_inplace.view(), ksj::fft::Direction::forward, ksj::fft::Normalization::orthonormal,
                             true, true);
      centered_fft2_inplace_checksum = checksum_complex_matrix(matrix_inplace);
      ksj::benchmarks::do_not_optimize(centered_fft2_inplace_checksum);
    });
    print_fft_benchmark_row("centered_fft2", "fft2_inplace_shifted", type_name, size, config, centered_fft2_inplace_ns,
                            centered_fft2_inplace_checksum);

    ksj::fft::CenteredFft2Executor<T> centered_fft2_executor;
    double centered_fft2_executor_checksum = 0.0;
    const auto centered_fft2_executor_ns = ksj::benchmarks::measure(config, [&] {
      std::copy(matrix_input.data(), matrix_input.data() + matrix_input.size(), matrix_inplace.data());
      centered_fft2_executor.execute_inplace(matrix_inplace.view(), ksj::fft::Direction::forward, true, true);
      centered_fft2_executor_checksum = checksum_complex_matrix(matrix_inplace);
      ksj::benchmarks::do_not_optimize(centered_fft2_executor_checksum);
    });
    print_fft_benchmark_row("centered_fft2", "centered_executor", type_name, size, config, centered_fft2_executor_ns,
                            centered_fft2_executor_checksum);

    const auto convolution_kernel = make_complex_kernel<T>(kFftConvolutionKernelSize);
    auto convolution_output = ksj::array::make_pooled_matrix<std::complex<T>>(size + kFftConvolutionKernelSize - 1U,
                                                                              size + kFftConvolutionKernelSize - 1U);
    ksj::benchmarks::require_pooled_storage("convolution_output", convolution_output);

    if (size <= kMaxDirectFftConvolutionSize) {
      const auto direct_convolve_ns = ksj::benchmarks::measure(config, [&] {
        convolve2d_full_reference(matrix_input, convolution_kernel, convolution_output);
        ksj::benchmarks::do_not_optimize(convolution_output.data()[0]);
      });
      print_fft_benchmark_row("convolve2d_full_15x15", "direct", type_name, size, config, direct_convolve_ns,
                              checksum_complex_matrix(convolution_output));

      const auto direct_correlate_ns = ksj::benchmarks::measure(config, [&] {
        correlate2d_full_reference(matrix_input, convolution_kernel, convolution_output);
        ksj::benchmarks::do_not_optimize(convolution_output.data()[0]);
      });
      print_fft_benchmark_row("correlate2d_full_15x15", "direct", type_name, size, config, direct_correlate_ns,
                              checksum_complex_matrix(convolution_output));
    }

    ksj::fft::convolve2d_full_fft(matrix_input, convolution_kernel, convolution_output);
    const auto fft_convolve_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::convolve2d_full_fft(matrix_input, convolution_kernel, convolution_output);
      ksj::benchmarks::do_not_optimize(convolution_output.data()[0]);
    });
    print_fft_benchmark_row("convolve2d_full_15x15", "fft_output", type_name, size, config, fft_convolve_ns,
                            checksum_complex_matrix(convolution_output));

    double fft_convolve_public_checksum = 0.0;
    const auto fft_convolve_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::convolve2d_full_fft(matrix_input, convolution_kernel);
      fft_convolve_public_checksum = checksum_complex_matrix(public_output);
      ksj::benchmarks::do_not_optimize(fft_convolve_public_checksum);
    });
    print_fft_benchmark_row("convolve2d_full_15x15", "public_api", type_name, size, config, fft_convolve_public_ns,
                            fft_convolve_public_checksum);

    ksj::fft::correlate2d_full_fft(matrix_input, convolution_kernel, convolution_output);
    const auto fft_correlate_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::correlate2d_full_fft(matrix_input, convolution_kernel, convolution_output);
      ksj::benchmarks::do_not_optimize(convolution_output.data()[0]);
    });
    print_fft_benchmark_row("correlate2d_full_15x15", "fft_output", type_name, size, config, fft_correlate_ns,
                            checksum_complex_matrix(convolution_output));

    double fft_correlate_public_checksum = 0.0;
    const auto fft_correlate_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::correlate2d_full_fft(matrix_input, convolution_kernel);
      fft_correlate_public_checksum = checksum_complex_matrix(public_output);
      ksj::benchmarks::do_not_optimize(fft_correlate_public_checksum);
    });
    print_fft_benchmark_row("correlate2d_full_15x15", "public_api", type_name, size, config, fft_correlate_public_ns,
                            fft_correlate_public_checksum);

    const auto convolution_same_row_offset = (convolution_kernel.rows() - 1U) / 2U;
    const auto convolution_same_col_offset = (convolution_kernel.cols() - 1U) / 2U;
    if (size <= kMaxDirectFftConvolutionSize) {
      const auto direct_convolve_same_ns = ksj::benchmarks::measure(config, [&] {
        convolve2d_window_reference(matrix_input, convolution_kernel, matrix_output, convolution_same_row_offset,
                                    convolution_same_col_offset);
        ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
      });
      print_fft_benchmark_row("convolve2d_same_15x15", "direct", type_name, size, config, direct_convolve_same_ns,
                              checksum_complex_matrix(matrix_output));

      const auto direct_correlate_same_ns = ksj::benchmarks::measure(config, [&] {
        correlate2d_window_reference(matrix_input, convolution_kernel, matrix_output, convolution_same_row_offset,
                                     convolution_same_col_offset);
        ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
      });
      print_fft_benchmark_row("correlate2d_same_15x15", "direct", type_name, size, config, direct_correlate_same_ns,
                              checksum_complex_matrix(matrix_output));
    }

    ksj::fft::convolve2d_same_fft(matrix_input, convolution_kernel, matrix_output);
    const auto fft_convolve_same_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::convolve2d_same_fft(matrix_input, convolution_kernel, matrix_output);
      ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
    });
    print_fft_benchmark_row("convolve2d_same_15x15", "fft_output", type_name, size, config, fft_convolve_same_ns,
                            checksum_complex_matrix(matrix_output));

    double fft_convolve_same_public_checksum = 0.0;
    const auto fft_convolve_same_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::convolve2d_same_fft(matrix_input, convolution_kernel);
      fft_convolve_same_public_checksum = checksum_complex_matrix(public_output);
      ksj::benchmarks::do_not_optimize(fft_convolve_same_public_checksum);
    });
    print_fft_benchmark_row("convolve2d_same_15x15", "public_api", type_name, size, config, fft_convolve_same_public_ns,
                            fft_convolve_same_public_checksum);

    ksj::fft::correlate2d_same_fft(matrix_input, convolution_kernel, matrix_output);
    const auto fft_correlate_same_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::correlate2d_same_fft(matrix_input, convolution_kernel, matrix_output);
      ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
    });
    print_fft_benchmark_row("correlate2d_same_15x15", "fft_output", type_name, size, config, fft_correlate_same_ns,
                            checksum_complex_matrix(matrix_output));

    double fft_correlate_same_public_checksum = 0.0;
    const auto fft_correlate_same_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::correlate2d_same_fft(matrix_input, convolution_kernel);
      fft_correlate_same_public_checksum = checksum_complex_matrix(public_output);
      ksj::benchmarks::do_not_optimize(fft_correlate_same_public_checksum);
    });
    print_fft_benchmark_row("correlate2d_same_15x15", "public_api", type_name, size, config,
                            fft_correlate_same_public_ns, fft_correlate_same_public_checksum);

    if (size >= kFftConvolutionKernelSize) {
      auto convolution_valid_output = ksj::array::make_pooled_matrix<std::complex<T>>(
        size - kFftConvolutionKernelSize + 1U, size - kFftConvolutionKernelSize + 1U);
      ksj::benchmarks::require_pooled_storage("convolution_valid_output", convolution_valid_output);

      if (size <= kMaxDirectFftConvolutionSize) {
        const auto direct_convolve_valid_ns = ksj::benchmarks::measure(config, [&] {
          convolve2d_window_reference(matrix_input, convolution_kernel, convolution_valid_output,
                                      convolution_kernel.rows() - 1U, convolution_kernel.cols() - 1U);
          ksj::benchmarks::do_not_optimize(convolution_valid_output.data()[0]);
        });
        print_fft_benchmark_row("convolve2d_valid_15x15", "direct", type_name, size, config, direct_convolve_valid_ns,
                                checksum_complex_matrix(convolution_valid_output));

        const auto direct_correlate_valid_ns = ksj::benchmarks::measure(config, [&] {
          correlate2d_window_reference(matrix_input, convolution_kernel, convolution_valid_output,
                                       convolution_kernel.rows() - 1U, convolution_kernel.cols() - 1U);
          ksj::benchmarks::do_not_optimize(convolution_valid_output.data()[0]);
        });
        print_fft_benchmark_row("correlate2d_valid_15x15", "direct", type_name, size, config, direct_correlate_valid_ns,
                                checksum_complex_matrix(convolution_valid_output));
      }

      ksj::fft::convolve2d_valid_fft(matrix_input, convolution_kernel, convolution_valid_output);
      const auto fft_convolve_valid_ns = ksj::benchmarks::measure(config, [&] {
        ksj::fft::convolve2d_valid_fft(matrix_input, convolution_kernel, convolution_valid_output);
        ksj::benchmarks::do_not_optimize(convolution_valid_output.data()[0]);
      });
      print_fft_benchmark_row("convolve2d_valid_15x15", "fft_output", type_name, size, config, fft_convolve_valid_ns,
                              checksum_complex_matrix(convolution_valid_output));

      double fft_convolve_valid_public_checksum = 0.0;
      const auto fft_convolve_valid_public_ns = ksj::benchmarks::measure(config, [&] {
        const auto public_output = ksj::fft::convolve2d_valid_fft(matrix_input, convolution_kernel);
        fft_convolve_valid_public_checksum = checksum_complex_matrix(public_output);
        ksj::benchmarks::do_not_optimize(fft_convolve_valid_public_checksum);
      });
      print_fft_benchmark_row("convolve2d_valid_15x15", "public_api", type_name, size, config,
                              fft_convolve_valid_public_ns, fft_convolve_valid_public_checksum);

      ksj::fft::correlate2d_valid_fft(matrix_input, convolution_kernel, convolution_valid_output);
      const auto fft_correlate_valid_ns = ksj::benchmarks::measure(config, [&] {
        ksj::fft::correlate2d_valid_fft(matrix_input, convolution_kernel, convolution_valid_output);
        ksj::benchmarks::do_not_optimize(convolution_valid_output.data()[0]);
      });
      print_fft_benchmark_row("correlate2d_valid_15x15", "fft_output", type_name, size, config, fft_correlate_valid_ns,
                              checksum_complex_matrix(convolution_valid_output));

      double fft_correlate_valid_public_checksum = 0.0;
      const auto fft_correlate_valid_public_ns = ksj::benchmarks::measure(config, [&] {
        const auto public_output = ksj::fft::correlate2d_valid_fft(matrix_input, convolution_kernel);
        fft_correlate_valid_public_checksum = checksum_complex_matrix(public_output);
        ksj::benchmarks::do_not_optimize(fft_correlate_valid_public_checksum);
      });
      print_fft_benchmark_row("correlate2d_valid_15x15", "public_api", type_name, size, config,
                              fft_correlate_valid_public_ns, fft_correlate_valid_public_checksum);
    }

    double fftshift_public_checksum = 0.0;
    const auto fftshift_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto shifted = ksj::fft::fftshift(matrix_input);
      fftshift_public_checksum = checksum_complex_matrix(shifted);
      ksj::benchmarks::do_not_optimize(fftshift_public_checksum);
    });
    print_fft_benchmark_row("fftshift2", "public_api", type_name, size, config, fftshift_public_ns,
                            fftshift_public_checksum);

    const auto fftshift_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fftshift(matrix_input, matrix_output);
      ksj::benchmarks::do_not_optimize(matrix_output.data()[0]);
    });
    print_fft_benchmark_row("fftshift2", "api", type_name, size, config, fftshift_output_ns,
                            checksum_complex_matrix(matrix_output));

    double fft2_segmented_dim1_output_checksum = 0.0;
    const auto fft2_segmented_dim1_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft_segmented(matrix_input, matrix_output, ksj::array::Dim::dim1, kMatrixSegmentCount);
      fft2_segmented_dim1_output_checksum = checksum_complex_matrix(matrix_output);
      ksj::benchmarks::do_not_optimize(fft2_segmented_dim1_output_checksum);
    });
    print_fft_benchmark_row("fft2_segmented_dim1_x4", "api", type_name, size, config, fft2_segmented_dim1_output_ns,
                            fft2_segmented_dim1_output_checksum);

    double fft2_segmented_dim1_public_checksum = 0.0;
    const auto fft2_segmented_dim1_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::fft_segmented(matrix_input, ksj::array::Dim::dim1, kMatrixSegmentCount);
      fft2_segmented_dim1_public_checksum = checksum_complex_matrix(public_output);
      ksj::benchmarks::do_not_optimize(fft2_segmented_dim1_public_checksum);
    });
    print_fft_benchmark_row("fft2_segmented_dim1_x4", "public_api", type_name, size, config,
                            fft2_segmented_dim1_public_ns, fft2_segmented_dim1_public_checksum);

    auto batch_input = ksj::array::make_pooled_cube<std::complex<T>>(size, size, kBatchSize);
    auto batch_output = ksj::array::make_pooled_cube<std::complex<T>>(size, size, kBatchSize);
    auto slice_input = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
    auto slice_output = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
    fill_complex_cube(batch_input);

    double fft2_loop_batch_checksum = 0.0;
    const auto fft2_loop_batch_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t slice = 0; slice < kBatchSize; ++slice) {
        for (std::size_t col = 0; col < size; ++col) {
          for (std::size_t row = 0; row < size; ++row) {
            slice_input(row, col) = batch_input(row, col, slice);
          }
        }
        ksj::fft::fft2(slice_input, slice_output);
        for (std::size_t col = 0; col < size; ++col) {
          for (std::size_t row = 0; row < size; ++row) {
            batch_output(row, col, slice) = slice_output(row, col);
          }
        }
      }
      fft2_loop_batch_checksum = checksum_complex_cube(batch_output);
      ksj::benchmarks::do_not_optimize(fft2_loop_batch_checksum);
    });
    print_fft_benchmark_row("fft2_batch_x8", "loop_output", type_name, size, config, fft2_loop_batch_ns,
                            fft2_loop_batch_checksum);

    double fft2_loop_alloc_checksum = 0.0;
    const auto fft2_loop_alloc_ns = ksj::benchmarks::measure(config, [&] {
      auto local_slice_input = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
      auto local_slice_output = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
      for (std::size_t slice = 0; slice < kBatchSize; ++slice) {
        for (std::size_t col = 0; col < size; ++col) {
          for (std::size_t row = 0; row < size; ++row) {
            local_slice_input(row, col) = batch_input(row, col, slice);
          }
        }
        ksj::fft::fft2(local_slice_input, local_slice_output);
        for (std::size_t col = 0; col < size; ++col) {
          for (std::size_t row = 0; row < size; ++row) {
            batch_output(row, col, slice) = local_slice_output(row, col);
          }
        }
      }
      fft2_loop_alloc_checksum = checksum_complex_cube(batch_output);
      ksj::benchmarks::do_not_optimize(fft2_loop_alloc_checksum);
    });
    print_fft_benchmark_row("fft2_batch_x8", "loop_alloc", type_name, size, config, fft2_loop_alloc_ns,
                            fft2_loop_alloc_checksum);

    double fft2_subview_loop_checksum = 0.0;
    const auto fft2_subview_loop_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t slice = 0; slice < kBatchSize; ++slice) {
        ksj::fft::fft2(ksj::array::as_const_view(batch_input.view()).subview(ksj::array::_, ksj::array::_, slice),
                       batch_output.view().subview(ksj::array::_, ksj::array::_, slice));
      }
      fft2_subview_loop_checksum = checksum_complex_cube(batch_output);
      ksj::benchmarks::do_not_optimize(fft2_subview_loop_checksum);
    });
    print_fft_benchmark_row("fft2_batch_x8", "subview_loop", type_name, size, config, fft2_subview_loop_ns,
                            fft2_subview_loop_checksum);

    double fft2_eigen_batch_checksum = 0.0;
    const auto fft2_eigen_batch_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::detail::eigen::fft_2d_batch(batch_input, batch_output, ksj::fft::Direction::forward,
                                            ksj::fft::Normalization::none);
      fft2_eigen_batch_checksum = checksum_complex_cube(batch_output);
      ksj::benchmarks::do_not_optimize(fft2_eigen_batch_checksum);
    });
    print_fft_benchmark_row("fft2_batch_x8", "eigen_batch", type_name, size, config, fft2_eigen_batch_ns,
                            fft2_eigen_batch_checksum);

    if (ksj::fft::detail::intel::fft_2d_batch(batch_input, batch_output, ksj::fft::Direction::forward,
                                              ksj::fft::Normalization::none)) {
      double fft2_intel_batch_checksum = 0.0;
      const auto fft2_intel_batch_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::fft::detail::intel::fft_2d_batch(batch_input, batch_output, ksj::fft::Direction::forward,
                                                    ksj::fft::Normalization::none);
        fft2_intel_batch_checksum = checksum_complex_cube(batch_output);
        ksj::benchmarks::do_not_optimize(fft2_intel_batch_checksum);
      });
      print_fft_benchmark_row("fft2_batch_x8", "intel_mkl_batch", type_name, size, config, fft2_intel_batch_ns,
                              fft2_intel_batch_checksum);
    }

    double fft2_batch_output_checksum = 0.0;
    const auto fft2_batch_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft2_batch(batch_input, batch_output);
      fft2_batch_output_checksum = checksum_complex_cube(batch_output);
      ksj::benchmarks::do_not_optimize(fft2_batch_output_checksum);
    });
    print_fft_benchmark_row("fft2_batch_x8", "api", type_name, size, config, fft2_batch_output_ns,
                            fft2_batch_output_checksum);

    double fft2_plan_batch_checksum = 0.0;
    const auto fft2_plan_batch_ns = ksj::benchmarks::measure(config, [&] {
      plan.execute_batch(batch_input, batch_output);
      fft2_plan_batch_checksum = checksum_complex_cube(batch_output);
      ksj::benchmarks::do_not_optimize(fft2_plan_batch_checksum);
    });
    print_fft_benchmark_row("fft2_batch_x8", "plan_execute", type_name, size, config, fft2_plan_batch_ns,
                            fft2_plan_batch_checksum);

    if (size > kMaxThreeDimensionalSize) {
      continue;
    }

    const auto slices = std::min(size, kMaxThreeDimensionalSlices);
    auto volume_input = ksj::array::make_pooled_cube<std::complex<T>>(size, size, slices);
    auto volume_output = ksj::array::make_pooled_cube<std::complex<T>>(size, size, slices);
    fill_complex_cube(volume_input);

    double fft3_output_checksum = 0.0;
    const auto fft3_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft3(volume_input, volume_output);
      fft3_output_checksum = checksum_complex_cube(volume_output);
      ksj::benchmarks::do_not_optimize(fft3_output_checksum);
    });
    print_fft_benchmark_row("fft3_slab", "api", type_name, size, config, fft3_output_ns, fft3_output_checksum);

    if (ksj::fft::detail::intel::fft_3d(volume_input, volume_output, ksj::fft::Direction::forward,
                                        ksj::fft::Normalization::none)) {
      const auto fft3_intel_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::fft::detail::intel::fft_3d(volume_input, volume_output, ksj::fft::Direction::forward,
                                              ksj::fft::Normalization::none);
        ksj::benchmarks::do_not_optimize(volume_output.data()[0]);
      });
      print_fft_benchmark_row("fft3_slab", "intel_mkl", type_name, size, config, fft3_intel_ns,
                              checksum_complex_cube(volume_output));
    }

    ksj::fft::Fft3Plan<T> fft3_plan(size, size, slices);
    double fft3_plan_checksum = 0.0;
    const auto fft3_plan_ns = ksj::benchmarks::measure(config, [&] {
      fft3_plan.execute(volume_input, volume_output);
      fft3_plan_checksum = checksum_complex_cube(volume_output);
      ksj::benchmarks::do_not_optimize(fft3_plan_checksum);
    });
    print_fft_benchmark_row("fft3_slab", "plan_execute", type_name, size, config, fft3_plan_ns, fft3_plan_checksum);

    double fft3_public_checksum = 0.0;
    const auto fft3_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::fft::fft3(volume_input);
      fft3_public_checksum = checksum_complex_cube(public_output);
      ksj::benchmarks::do_not_optimize(fft3_public_checksum);
    });
    print_fft_benchmark_row("fft3_slab", "public_api", type_name, size, config, fft3_public_ns, fft3_public_checksum);

    auto volume_batch_input = ksj::array::make_pooled_array4d<std::complex<T>>(size, size, slices, kVolumeBatchSize);
    auto volume_batch_output = ksj::array::make_pooled_array4d<std::complex<T>>(size, size, slices, kVolumeBatchSize);
    fill_complex_array4d(volume_batch_input);

    const auto fft3_loop_batch_ns = ksj::benchmarks::measure(config, [&] {
      auto volume_loop_input = ksj::array::make_pooled_cube<std::complex<T>>(size, size, slices);
      auto volume_loop_output = ksj::array::make_pooled_cube<std::complex<T>>(size, size, slices);
      for (std::size_t batch = 0; batch < kVolumeBatchSize; ++batch) {
        for (std::size_t row = 0; row < size; ++row) {
          for (std::size_t col = 0; col < size; ++col) {
            for (std::size_t slice = 0; slice < slices; ++slice) {
              volume_loop_input(row, col, slice) = volume_batch_input(row, col, slice, batch);
            }
          }
        }
        ksj::fft::fft3(volume_loop_input, volume_loop_output);
        for (std::size_t row = 0; row < size; ++row) {
          for (std::size_t col = 0; col < size; ++col) {
            for (std::size_t slice = 0; slice < slices; ++slice) {
              volume_batch_output(row, col, slice, batch) = volume_loop_output(row, col, slice);
            }
          }
        }
      }
      ksj::benchmarks::do_not_optimize(volume_batch_output.data()[0]);
    });
    const auto fft3_loop_batch_checksum = checksum_complex_array4d(volume_batch_output);
    print_fft_benchmark_row("fft3_batch_x4", "loop_gather_scatter", type_name, size, config, fft3_loop_batch_ns,
                            fft3_loop_batch_checksum, ksj::benchmarks::reference_row("fft3_batch_x4", "output_reuse"));

    const auto fft3_execute_batch_ns = ksj::benchmarks::measure(config, [&] {
      ksj::fft::fft3_batch(volume_batch_input, volume_batch_output);
      ksj::benchmarks::do_not_optimize(volume_batch_output.data()[0]);
    });
    const auto fft3_execute_batch_checksum = checksum_complex_array4d(volume_batch_output);
    print_fft_benchmark_row("fft3_batch_x4", "api", type_name, size, config, fft3_execute_batch_ns,
                            fft3_execute_batch_checksum,
                            ksj::benchmarks::candidate_row("fft3_batch_x4", "output_reuse"));

    if (ksj::fft::detail::intel::fft_3d_batch(volume_batch_input, volume_batch_output, ksj::fft::Direction::forward,
                                              ksj::fft::Normalization::none)) {
      const auto fft3_intel_batch_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::fft::detail::intel::fft_3d_batch(volume_batch_input, volume_batch_output,
                                                    ksj::fft::Direction::forward, ksj::fft::Normalization::none);
        ksj::benchmarks::do_not_optimize(volume_batch_output.data()[0]);
      });
      print_fft_benchmark_row("fft3_batch_x4", "intel_mkl_contiguous_batch", type_name, size, config,
                              fft3_intel_batch_ns, checksum_complex_array4d(volume_batch_output),
                              ksj::benchmarks::candidate_row("fft3_batch_x4", "output_reuse"));
    }

    if (ksj::fft::detail::intel::fft_3d_batch_strided(volume_batch_input, volume_batch_output,
                                                      ksj::fft::Direction::forward, ksj::fft::Normalization::none)) {
      const auto fft3_intel_strided_batch_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::fft::detail::intel::fft_3d_batch_strided(
          volume_batch_input, volume_batch_output, ksj::fft::Direction::forward, ksj::fft::Normalization::none);
        ksj::benchmarks::do_not_optimize(volume_batch_output.data()[0]);
      });
      print_fft_benchmark_row("fft3_batch_x4", "intel_mkl_strided_batch", type_name, size, config,
                              fft3_intel_strided_batch_ns, checksum_complex_array4d(volume_batch_output),
                              ksj::benchmarks::candidate_row("fft3_batch_x4", "output_reuse"));
    }

    auto warm_volume_input = ksj::array::make_pooled_cube<std::complex<T>>(size, size, slices);
    auto warm_volume_output = ksj::array::make_pooled_cube<std::complex<T>>(size, size, slices);
    const auto fft3_warm_loop_batch_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t batch = 0; batch < kVolumeBatchSize; ++batch) {
        for (std::size_t row = 0; row < size; ++row) {
          for (std::size_t col = 0; col < size; ++col) {
            for (std::size_t slice = 0; slice < slices; ++slice) {
              warm_volume_input(row, col, slice) = volume_batch_input(row, col, slice, batch);
            }
          }
        }
        ksj::fft::fft3(warm_volume_input, warm_volume_output);
        for (std::size_t row = 0; row < size; ++row) {
          for (std::size_t col = 0; col < size; ++col) {
            for (std::size_t slice = 0; slice < slices; ++slice) {
              volume_batch_output(row, col, slice, batch) = warm_volume_output(row, col, slice);
            }
          }
        }
      }
      ksj::benchmarks::do_not_optimize(volume_batch_output.data()[0]);
    });
    print_fft_benchmark_row("fft3_batch_x4", "loop_gather_scatter", type_name, size, config, fft3_warm_loop_batch_ns,
                            checksum_complex_array4d(volume_batch_output),
                            ksj::benchmarks::reference_row("fft3_batch_x4", "warm_plan"));

    const auto fft3_plan_batch_ns = ksj::benchmarks::measure(config, [&] {
      fft3_plan.execute_batch(volume_batch_input, volume_batch_output);
      ksj::benchmarks::do_not_optimize(volume_batch_output.data()[0]);
    });
    print_fft_benchmark_row("fft3_batch_x4", "plan_execute", type_name, size, config, fft3_plan_batch_ns,
                            checksum_complex_array4d(volume_batch_output),
                            ksj::benchmarks::candidate_row("fft3_batch_x4", "warm_plan"));

    ksj::fft::ComplexArray4D<T> public_batch_output;
    const auto fft3_public_batch_ns = ksj::benchmarks::measure(config, [&] {
      public_batch_output = ksj::fft::fft3_batch(volume_batch_input);
      ksj::benchmarks::do_not_optimize(public_batch_output.data()[0]);
    });
    print_fft_benchmark_row("fft3_batch_x4", "public_api", type_name, size, config, fft3_public_batch_ns,
                            checksum_complex_array4d(public_batch_output),
                            ksj::benchmarks::candidate_row("fft3_batch_x4", "allocating"));
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_fft_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_type<float>("complex_float", config);
  run_for_type<double>("complex_double", config);
  return 0;
}
