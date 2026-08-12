#include "benchmark_common.hpp"
#include "kspacejet/signal/signal.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_filters.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_convolution.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_phase.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_convolution.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_windows.hpp"
#include "kspacejet/signal/detail/opencv/opencv_signal_convolution.hpp"

#include <cstddef>
#include <limits>
#include <numbers>
#include <string_view>

namespace ksj::benchmarks::signal_benchmarks {
void run_filter_benchmarks_float(const Config& config);
void run_filter_benchmarks_double(const Config& config);
} // namespace ksj::benchmarks::signal_benchmarks

namespace {

inline constexpr std::size_t kLargeCorrelationKernelSize = 31;
inline constexpr std::size_t kMaxEigenLargeCorrelationSize = 128;

[[nodiscard]] ksj::benchmarks::RowMetadata signal_benchmark_row_metadata(const std::string_view case_name,
                                                                         const std::string_view backend) {
  const auto output_reuse = (case_name.starts_with("window_") && backend != "public_api") ||
                            case_name.starts_with("correlate2d_") || backend.starts_with("intel_") ||
                            backend.starts_with("opencv_") || backend == "fft_detail" || backend == "eigen_separable" ||
                            backend.find("output") != std::string_view::npos;
  const auto timing_scope = output_reuse ? std::string_view{"output_reuse"} : std::string_view{"allocating"};
  if (backend.starts_with("eigen")) {
    return ksj::benchmarks::reference_row(case_name, timing_scope);
  }
  return ksj::benchmarks::candidate_row(case_name, timing_scope);
}

inline void print_signal_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                       const std::string_view type_name, const std::size_t size,
                                       const ksj::benchmarks::Config& config,
                                       const ksj::benchmarks::Measurement& measurement, const double checksum) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum,
                             signal_benchmark_row_metadata(case_name, backend));
}

inline void print_signal_benchmark_row(const std::string_view case_name, const std::string_view backend,
                                       const std::string_view type_name, const std::size_t size,
                                       const ksj::benchmarks::Config& config,
                                       const ksj::benchmarks::Measurement& measurement, const double checksum,
                                       const ksj::benchmarks::RowMetadata& metadata) {
  ksj::benchmarks::print_row(case_name, backend, type_name, size, config, measurement, checksum, metadata);
}

template <typename T> [[nodiscard]] constexpr double aggregate_checksum_tolerance(const std::size_t elements) {
  if constexpr (std::is_same_v<T, float>) {
    return 16.0 * static_cast<double>(std::numeric_limits<float>::epsilon()) * static_cast<double>(elements);
  } else {
    return -1.0;
  }
}

template <typename T>
[[nodiscard]] constexpr std::string_view
selected_correlation_backend(const std::size_t input_rows, const std::size_t input_cols, const std::size_t kernel_rows,
                             const std::size_t kernel_cols) {
  const auto input_pixels = input_rows * input_cols;
  const auto kernel_pixels = kernel_rows * kernel_cols;
  if (ksj::signal::detail::prefer_intel_correlate2d_same<T>(input_pixels, kernel_pixels)) {
    return "intel_ipp_crosscorr";
  }
  if (ksj::signal::detail::prefer_fft_correlate2d_same<T>(input_rows, input_cols, kernel_rows, kernel_cols)) {
    return "fft_detail";
  }
  if (ksj::signal::detail::prefer_opencv_correlate2d_same<T>(input_pixels, kernel_pixels)) {
    return "opencv_filter2d";
  }
  return "eigen";
}

template <typename T> [[nodiscard]] ksj::array::PooledImage<T> make_large_correlation_kernel(const std::size_t size) {
  auto kernel = ksj::array::make_pooled_image<T>(size, size);
  const auto center = static_cast<T>(size / 2U);
  T sum{};
  for (std::size_t row = 0; row < kernel.rows(); ++row) {
    for (std::size_t col = 0; col < kernel.cols(); ++col) {
      const auto row_distance = static_cast<T>(row) - center;
      const auto col_distance = static_cast<T>(col) - center;
      const auto radius2 = row_distance * row_distance + col_distance * col_distance;
      const auto value = T{1} / (T{1} + radius2);
      kernel(row, col) = value;
      sum += value;
    }
  }
  for (std::size_t index = 0; index < kernel.size(); ++index) {
    kernel.data()[index] /= sum;
  }
  return kernel;
}

template <typename T> void fill_wrapped_phase(ksj::array::PooledVector<T>& phase) {
  const auto period = T{2} * std::numbers::pi_v<T>;
  for (std::size_t index = 0; index < phase.size(); ++index) {
    auto value = static_cast<T>(index) * static_cast<T>(0.35);
    while (value > std::numbers::pi_v<T>) {
      value -= period;
    }
    phase(index) = value;
  }
}

template <typename T> void fill_wrapped_phase_image(ksj::array::PooledImage<T>& phase) {
  const auto period = T{2} * std::numbers::pi_v<T>;
  for (std::size_t row = 0; row < phase.rows(); ++row) {
    for (std::size_t col = 0; col < phase.cols(); ++col) {
      auto value = static_cast<T>(row) * static_cast<T>(0.18) + static_cast<T>(col) * static_cast<T>(0.27);
      while (value > std::numbers::pi_v<T>) {
        value -= period;
      }
      phase(row, col) = value;
    }
  }
}

template <typename T> void run_window(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto eigen_output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("eigen_window", eigen_output);
    ksj::signal::detail::eigen::window(eigen_output.view(), ksj::signal::WindowKind::hann);
    const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::window(eigen_output.view(), ksj::signal::WindowKind::hann);
    });
    print_signal_benchmark_row("window_hann", "eigen", type_name, size, config, eigen_ns,
                               ksj::benchmarks::checksum(eigen_output));

    auto intel_output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("intel_window", intel_output);
    if (ksj::signal::detail::intel::window(intel_output.view(), ksj::signal::WindowKind::hann)) {
      const auto intel_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::intel::window(intel_output.view(), ksj::signal::WindowKind::hann);
      });
      print_signal_benchmark_row("window_hann", "intel_ipp", type_name, size, config, intel_ns,
                                 ksj::benchmarks::checksum(intel_output));
    }

    auto exponential_output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("exponential_window", exponential_output);
    const auto exponential_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::exponential_window(exponential_output.view(), T{8}, T{2});
      ksj::benchmarks::do_not_optimize(exponential_output.data()[0]);
    });
    print_signal_benchmark_row("window_exponential", "eigen", type_name, size, config, exponential_ns,
                               ksj::benchmarks::checksum(exponential_output));

    auto fermi_output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("fermi_window", fermi_output);
    const auto fermi_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::fermi_window(fermi_output.view(), static_cast<T>(size) * static_cast<T>(0.35), T{4});
      ksj::benchmarks::do_not_optimize(fermi_output.data()[0]);
    });
    print_signal_benchmark_row("window_fermi", "eigen", type_name, size, config, fermi_ns,
                               ksj::benchmarks::checksum(fermi_output));

    double bandpass_public_checksum = 0.0;
    const auto bandpass_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::signal::fermi_bandpass_window(size, static_cast<T>(size) * static_cast<T>(0.12),
                                                             static_cast<T>(size) * static_cast<T>(0.36), T{4});
      bandpass_public_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(bandpass_public_checksum);
    });
    print_signal_benchmark_row("window_fermi_bandpass", "public_api", type_name, size, config, bandpass_public_ns,
                               bandpass_public_checksum);

    double dual_public_checksum = 0.0;
    const auto dual_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::signal::dual_fermi_band_window(size, static_cast<T>(size) * static_cast<T>(0.2),
                                                              static_cast<T>(size) * static_cast<T>(0.06), T{3});
      dual_public_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(dual_public_checksum);
    });
    print_signal_benchmark_row("window_dual_fermi_band", "public_api", type_name, size, config, dual_public_ns,
                               dual_public_checksum);
  }
}

template <typename T> void run_resample(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::fill_vector(input);
    ksj::benchmarks::require_pooled_storage("resample_input", input);

    double public_linear_checksum = 0.0;
    const auto public_linear_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::signal::resample(input, size * 2U, ksj::signal::ResampleKernel::linear);
      public_linear_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(public_linear_checksum);
    });
    print_signal_benchmark_row("resample_linear_x2", "public_api", type_name, size, config, public_linear_ns,
                               public_linear_checksum);

    double public_nearest_checksum = 0.0;
    const auto public_nearest_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::signal::resample(input, size * 2U, ksj::signal::ResampleKernel::nearest);
      public_nearest_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(public_nearest_checksum);
    });
    print_signal_benchmark_row("resample_nearest_x2", "public_api", type_name, size, config, public_nearest_ns,
                               public_nearest_checksum);

    double public_cubic_checksum = 0.0;
    const auto public_cubic_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::signal::resample(input, size * 2U, ksj::signal::ResampleKernel::cubic);
      public_cubic_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(public_cubic_checksum);
    });
    print_signal_benchmark_row("resample_cubic_x2", "public_api", type_name, size, config, public_cubic_ns,
                               public_cubic_checksum);

    double public_mitchell_checksum = 0.0;
    const auto public_mitchell_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::signal::resample(input, size * 2U, ksj::signal::ResampleKernel::mitchell);
      public_mitchell_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(public_mitchell_checksum);
    });
    print_signal_benchmark_row("resample_mitchell_x2", "public_api", type_name, size, config, public_mitchell_ns,
                               public_mitchell_checksum);

    double public_lanczos_checksum = 0.0;
    const auto public_lanczos_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::signal::resample(input, size * 2U, ksj::signal::ResampleKernel::lanczos3);
      public_lanczos_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(public_lanczos_checksum);
    });
    print_signal_benchmark_row("resample_lanczos3_x2", "public_api", type_name, size, config, public_lanczos_ns,
                               public_lanczos_checksum);
  }
}

template <typename T> void run_convolve(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto signal = ksj::array::make_pooled_vector<T>(size);
    auto kernel = ksj::array::make_pooled_vector<T>(size / 4U + 1U);
    auto output = ksj::array::make_pooled_vector<T>(signal.size() + kernel.size() - 1U);
    ksj::benchmarks::fill_vector(signal);
    ksj::benchmarks::fill_vector(kernel);
    ksj::benchmarks::require_pooled_storage("signal", signal);
    ksj::benchmarks::require_pooled_storage("kernel", kernel);
    ksj::benchmarks::require_pooled_storage("output", output);

    double eigen_checksum = 0.0;
    const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
      const auto eigen_output = ksj::signal::detail::eigen::convolve(signal, kernel);
      eigen_checksum = ksj::benchmarks::checksum(eigen_output);
      ksj::benchmarks::do_not_optimize(eigen_checksum);
    });
    print_signal_benchmark_row("convolve", "eigen", type_name, size, config, eigen_ns, eigen_checksum);

    double eigen_output_checksum = 0.0;
    const auto eigen_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::convolve(ksj::array::as_const_view(signal.view()),
                                           ksj::array::as_const_view(kernel.view()), output.view());
      eigen_output_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(eigen_output_checksum);
    });
    print_signal_benchmark_row("convolve", "eigen_output", type_name, size, config, eigen_output_ns,
                               eigen_output_checksum, ksj::benchmarks::reference_row("convolve", "output_reuse"));

    if (ksj::signal::detail::intel::convolve(ksj::array::as_const_view(signal.view()),
                                             ksj::array::as_const_view(kernel.view()), output.view())) {
      double intel_output_checksum = 0.0;
      const auto intel_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::intel::convolve(ksj::array::as_const_view(signal.view()),
                                                   ksj::array::as_const_view(kernel.view()), output.view());
        intel_output_checksum = ksj::benchmarks::checksum(output);
        ksj::benchmarks::do_not_optimize(intel_output_checksum);
      });
      print_signal_benchmark_row("convolve", "intel_ipp", type_name, size, config, intel_ns, intel_output_checksum,
                                 ksj::benchmarks::candidate_row("convolve", "output_reuse"));
    }

    double public_output_checksum = 0.0;
    const auto public_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::convolve(ksj::array::as_const_view(signal.view()), ksj::array::as_const_view(kernel.view()),
                            output.view());
      public_output_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(public_output_checksum);
    });
    print_signal_benchmark_row(
      "convolve", "public_output_policy", type_name, size, config, public_output_ns, public_output_checksum,
      ksj::benchmarks::policy_row(
        "convolve", "output_reuse",
        ksj::signal::detail::prefer_intel_convolve<T>(signal.size(), kernel.size()) ? "intel_ipp" : "eigen_output"));
  }
}

template <typename T> void run_phase(std::string_view type_name, const ksj::benchmarks::Config& config) {
  for (const auto size : config.sizes) {
    auto phase = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("phase", phase);
    fill_wrapped_phase(phase);

    double unwrap_checksum = 0.0;
    const auto unwrap_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::signal::detail::eigen::unwrap_phase(phase);
      unwrap_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(unwrap_checksum);
    });
    print_signal_benchmark_row("unwrap_phase", "eigen", type_name, size, config, unwrap_ns, unwrap_checksum);

    auto phase_image = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::require_pooled_storage("phase_image", phase_image);
    fill_wrapped_phase_image(phase_image);

    double unwrap_2d_public_checksum = 0.0;
    const auto unwrap_2d_public_ns = ksj::benchmarks::measure(config, [&] {
      const auto output = ksj::signal::unwrap_phase_2d(phase_image);
      unwrap_2d_public_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(unwrap_2d_public_checksum);
    });
    print_signal_benchmark_row("unwrap_phase_2d", "public_api", type_name, size, config, unwrap_2d_public_ns,
                               unwrap_2d_public_checksum);
  }
}

template <typename T> void run_correlate2d(std::string_view type_name, const ksj::benchmarks::Config& config) {
  auto row_kernel = ksj::array::make_pooled_vector<T>(5);
  auto col_kernel = ksj::array::make_pooled_vector<T>(5);
  ksj::benchmarks::require_pooled_storage("row_kernel", row_kernel);
  ksj::benchmarks::require_pooled_storage("col_kernel", col_kernel);
  row_kernel(0) = static_cast<T>(1);
  row_kernel(1) = static_cast<T>(4);
  row_kernel(2) = static_cast<T>(6);
  row_kernel(3) = static_cast<T>(4);
  row_kernel(4) = static_cast<T>(1);
  col_kernel(0) = static_cast<T>(1);
  col_kernel(1) = static_cast<T>(2);
  col_kernel(2) = static_cast<T>(0);
  col_kernel(3) = static_cast<T>(-2);
  col_kernel(4) = static_cast<T>(-1);
  const auto kernel = ksj::signal::compose_separable_kernel(row_kernel, col_kernel);
  const auto large_kernel = make_large_correlation_kernel<T>(kLargeCorrelationKernelSize);

  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_image<T>(size, size);
    auto output = ksj::array::make_pooled_image<T>(size, size);
    auto separable_scratch = ksj::array::make_pooled_image<T>(size, size);
    ksj::benchmarks::require_pooled_storage("correlate_input", input);
    ksj::benchmarks::require_pooled_storage("correlate_output", output);
    ksj::benchmarks::require_pooled_storage("correlate_separable_scratch", separable_scratch);
    ksj::benchmarks::fill_image(input);

    const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                   ksj::array::as_const_view(kernel.view()), output.view());
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_signal_benchmark_row("correlate2d_same_5x5", "eigen", type_name, size, config, eigen_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::reference_row("correlate2d_same_5x5_dense", "output_reuse",
                                                              aggregate_checksum_tolerance<T>(input.size())));

    if (ksj::signal::detail::opencv::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                      ksj::array::as_const_view(kernel.view()), output.view())) {
      const auto opencv_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::opencv::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                            ksj::array::as_const_view(kernel.view()), output.view());
        ksj::benchmarks::do_not_optimize(output.data()[0]);
      });
      print_signal_benchmark_row("correlate2d_same_5x5", "opencv_filter2d", type_name, size, config, opencv_ns,
                                 ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("correlate2d_same_5x5_dense", "output_reuse"));
    }

    const auto eigen_separable_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::correlate2d_same_separable(
        ksj::array::as_const_view(input.view()), ksj::array::as_const_view(row_kernel.view()),
        ksj::array::as_const_view(col_kernel.view()), output.view(), separable_scratch.view());
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_signal_benchmark_row("correlate2d_same_5x5", "eigen_separable", type_name, size, config, eigen_separable_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::reference_row("correlate2d_same_5x5_separable", "workspace_reuse",
                                                              aggregate_checksum_tolerance<T>(input.size())));

    if (ksj::signal::detail::opencv::correlate2d_same_separable(
          ksj::array::as_const_view(input.view()), ksj::array::as_const_view(row_kernel.view()),
          ksj::array::as_const_view(col_kernel.view()), output.view())) {
      const auto opencv_separable_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::opencv::correlate2d_same_separable(
          ksj::array::as_const_view(input.view()), ksj::array::as_const_view(row_kernel.view()),
          ksj::array::as_const_view(col_kernel.view()), output.view());
        ksj::benchmarks::do_not_optimize(output.data()[0]);
      });
      print_signal_benchmark_row("correlate2d_same_5x5", "opencv_sepfilter2d", type_name, size, config,
                                 opencv_separable_ns, ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("correlate2d_same_5x5_separable", "workspace_reuse"));
    }

    const auto public_policy_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::correlate2d_same(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()),
                                    output.view());
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_signal_benchmark_row(
      "correlate2d_same_5x5", "public_policy", type_name, size, config, public_policy_ns,
      ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row(
        "correlate2d_same_5x5_dense", "output_reuse",
        selected_correlation_backend<T>(input.rows(), input.cols(), kernel.rows(), kernel.cols())));

    const auto public_separable_policy_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::correlate2d_same_separable(
        ksj::array::as_const_view(input.view()), ksj::array::as_const_view(row_kernel.view()),
        ksj::array::as_const_view(col_kernel.view()), output.view(), separable_scratch.view());
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_signal_benchmark_row(
      "correlate2d_same_5x5", "public_separable_policy", type_name, size, config, public_separable_policy_ns,
      ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row("correlate2d_same_5x5_separable", "workspace_reuse",
                                  ksj::signal::detail::prefer_opencv_correlate2d_same_separable<T>(
                                    input.size(), row_kernel.size(), col_kernel.size())
                                    ? "opencv_sepfilter2d"
                                    : "eigen_separable"));

    if (size <= kMaxEigenLargeCorrelationSize) {
      const auto eigen_large_ns = ksj::benchmarks::measure(config, [&] {
        ksj::signal::detail::eigen::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                     ksj::array::as_const_view(large_kernel.view()), output.view());
        ksj::benchmarks::do_not_optimize(output.data()[0]);
      });
      print_signal_benchmark_row("correlate2d_same_31x31", "eigen", type_name, size, config, eigen_large_ns,
                                 ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::reference_row("correlate2d_same_31x31", "output_reuse",
                                                                aggregate_checksum_tolerance<T>(input.size())));
    }

    if (ksj::signal::detail::opencv::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                      ksj::array::as_const_view(large_kernel.view()), output.view())) {
      const auto opencv_large_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::opencv::correlate2d_same(
          ksj::array::as_const_view(input.view()), ksj::array::as_const_view(large_kernel.view()), output.view());
        ksj::benchmarks::do_not_optimize(output.data()[0]);
      });
      print_signal_benchmark_row("correlate2d_same_31x31", "opencv_filter2d", type_name, size, config, opencv_large_ns,
                                 ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("correlate2d_same_31x31", "output_reuse"));
    }

    if (ksj::signal::detail::intel::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                     ksj::array::as_const_view(large_kernel.view()), output.view())) {
      const auto intel_large_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::intel::correlate2d_same(
          ksj::array::as_const_view(input.view()), ksj::array::as_const_view(large_kernel.view()), output.view());
        ksj::benchmarks::do_not_optimize(output.data()[0]);
      });
      print_signal_benchmark_row("correlate2d_same_31x31", "intel_ipp_crosscorr", type_name, size, config,
                                 intel_large_ns, ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("correlate2d_same_31x31", "output_reuse"));
    }

    ksj::signal::detail::fft::correlate2d_same(ksj::array::as_const_view(input.view()),
                                               ksj::array::as_const_view(large_kernel.view()), output.view());
    double fft_large_checksum = 0.0;
    const auto fft_large_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::fft::correlate2d_same(ksj::array::as_const_view(input.view()),
                                                 ksj::array::as_const_view(large_kernel.view()), output.view());
      fft_large_checksum = ksj::benchmarks::checksum(output);
      ksj::benchmarks::do_not_optimize(fft_large_checksum);
    });
    print_signal_benchmark_row("correlate2d_same_31x31", "fft_detail", type_name, size, config, fft_large_ns,
                               fft_large_checksum,
                               ksj::benchmarks::candidate_row("correlate2d_same_31x31", "output_reuse"));

    const auto public_large_policy_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::correlate2d_same(ksj::array::as_const_view(input.view()),
                                    ksj::array::as_const_view(large_kernel.view()), output.view());
      ksj::benchmarks::do_not_optimize(output.data()[0]);
    });
    print_signal_benchmark_row(
      "correlate2d_same_31x31", "public_policy", type_name, size, config, public_large_policy_ns,
      ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row(
        "correlate2d_same_31x31", "output_reuse",
        selected_correlation_backend<T>(input.rows(), input.cols(), large_kernel.rows(), large_kernel.cols())));

    double public_fft_large_checksum = 0.0;
    const auto public_fft_large_ns = ksj::benchmarks::measure(config, [&] {
      const auto public_output = ksj::signal::correlate2d_same_fft(input, large_kernel);
      public_fft_large_checksum = ksj::benchmarks::checksum(public_output);
      ksj::benchmarks::do_not_optimize(public_fft_large_checksum);
    });
    print_signal_benchmark_row("correlate2d_same_31x31", "public_fft_api", type_name, size, config, public_fft_large_ns,
                               public_fft_large_checksum);
  }
}

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  run_window<T>(type_name, config);
  run_resample<T>(type_name, config);
  run_convolve<T>(type_name, config);
  run_phase<T>(type_name, config);
  run_correlate2d<T>(type_name, config);
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_signal_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  ksj::benchmarks::signal_benchmarks::run_filter_benchmarks_float(config);
  ksj::benchmarks::signal_benchmarks::run_filter_benchmarks_double(config);
  return 0;
}
