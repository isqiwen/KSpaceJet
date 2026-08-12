#include "benchmark_common.hpp"
#include "kspacejet/signal/signal.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_convolution.hpp"
#include "kspacejet/signal/detail/eigen/eigen_signal_filters.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_convolution.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_filters.hpp"

#include <cstddef>
#include <string_view>

namespace ksj::benchmarks::signal_benchmarks {
namespace {

template <typename T> void run_filter_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  constexpr std::size_t taps_size = 17U;
  constexpr std::size_t median_kernel_size = 5U;
  constexpr auto causal_border = ksj::signal::SignalBorderMode::causal_replicate;

  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_vector<T>(size);
    auto taps = ksj::array::make_pooled_vector<T>(taps_size);
    auto output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::fill_vector(input);
    for (std::size_t index = 0U; index < taps.size(); ++index) {
      taps(index) = static_cast<T>(1.0 / static_cast<double>(taps.size()));
    }

    const auto eigen_fir_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::fir_filter(ksj::array::as_const_view(input.view()),
                                             ksj::array::as_const_view(taps.view()), output.view());
    });
    ksj::benchmarks::print_row("fir_filter_17tap", "eigen", type_name, size, config, eigen_fir_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::reference_row("fir_filter_17tap", "workspace_reuse"));

    ksj::signal::FirFilterWorkspace<T> intel_fir_workspace;
    if (ksj::signal::detail::intel::fir_filter(ksj::array::as_const_view(input.view()),
                                               ksj::array::as_const_view(taps.view()), output.view(),
                                               intel_fir_workspace)) {
      const auto intel_fir_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::intel::fir_filter(ksj::array::as_const_view(input.view()),
                                                     ksj::array::as_const_view(taps.view()), output.view(),
                                                     intel_fir_workspace);
      });
      ksj::benchmarks::print_row("fir_filter_17tap", "intel_ipp", type_name, size, config, intel_fir_ns,
                                 ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("fir_filter_17tap", "workspace_reuse"));
    }

    ksj::signal::FirFilterWorkspace<T> public_fir_workspace;
    const auto public_fir_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::fir_filter(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(taps.view()),
                              output.view(), public_fir_workspace);
    });
    ksj::benchmarks::print_row(
      "fir_filter_17tap", "public_policy", type_name, size, config, public_fir_ns, ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row(
        "fir_filter_17tap", "workspace_reuse",
        ksj::signal::detail::prefer_intel_fir_filter<T>(input.size(), taps.size()) ? "intel_ipp" : "eigen"));

    auto numerator = ksj::array::make_pooled_vector<T>(3U);
    auto denominator = ksj::array::make_pooled_vector<T>(3U);
    numerator(0U) = static_cast<T>(0.2);
    numerator(1U) = static_cast<T>(0.1);
    numerator(2U) = static_cast<T>(0.05);
    denominator(0U) = T{1};
    denominator(1U) = static_cast<T>(-0.3);
    denominator(2U) = static_cast<T>(0.12);

    const auto eigen_iir_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::iir_filter(ksj::array::as_const_view(input.view()),
                                             ksj::array::as_const_view(numerator.view()),
                                             ksj::array::as_const_view(denominator.view()), output.view());
    });
    ksj::benchmarks::print_row("iir_filter_order2", "eigen", type_name, size, config, eigen_iir_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::reference_row("iir_filter_order2", "workspace_reuse"));

    ksj::signal::IirFilterWorkspace<T> intel_iir_workspace;
    if (ksj::signal::detail::intel::iir_filter(
          ksj::array::as_const_view(input.view()), ksj::array::as_const_view(numerator.view()),
          ksj::array::as_const_view(denominator.view()), output.view(), intel_iir_workspace)) {
      const auto intel_iir_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::intel::iir_filter(
          ksj::array::as_const_view(input.view()), ksj::array::as_const_view(numerator.view()),
          ksj::array::as_const_view(denominator.view()), output.view(), intel_iir_workspace);
      });
      ksj::benchmarks::print_row("iir_filter_order2", "intel_ipp", type_name, size, config, intel_iir_ns,
                                 ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("iir_filter_order2", "workspace_reuse"));
    }

    ksj::signal::IirFilterWorkspace<T> public_iir_workspace;
    const auto public_iir_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::iir_filter(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(numerator.view()),
                              ksj::array::as_const_view(denominator.view()), output.view(), public_iir_workspace);
    });
    ksj::benchmarks::print_row(
      "iir_filter_order2", "public_policy", type_name, size, config, public_iir_ns, ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row("iir_filter_order2", "workspace_reuse",
                                  ksj::signal::detail::prefer_intel_iir_filter<T>(input.size()) ? "intel_ipp"
                                                                                                : "eigen"));

    ksj::signal::MedianFilterWorkspace<T> eigen_median_workspace;
    const auto eigen_median_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::median_filter(ksj::array::as_const_view(input.view()), output.view(),
                                                median_kernel_size, causal_border, eigen_median_workspace);
    });
    ksj::benchmarks::print_row("median_filter_causal_5", "eigen", type_name, size, config, eigen_median_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::reference_row("median_filter_causal_5", "workspace_reuse"));

    ksj::signal::MedianFilterWorkspace<T> intel_median_workspace;
    ksj::array::copy(ksj::array::as_const_view(input.view()), output.view());
    if (ksj::signal::detail::intel::median_filter_in_place(output.view(), median_kernel_size, causal_border,
                                                           intel_median_workspace)) {
      const auto intel_median_ns = ksj::benchmarks::measure(config, [&] {
        ksj::array::copy(ksj::array::as_const_view(input.view()), output.view());
        (void)ksj::signal::detail::intel::median_filter_in_place(output.view(), median_kernel_size, causal_border,
                                                                 intel_median_workspace);
      });
      ksj::benchmarks::print_row("median_filter_causal_5", "intel_ipp_copy_in_place", type_name, size, config,
                                 intel_median_ns, ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("median_filter_causal_5", "workspace_reuse"));
    }

    ksj::signal::MedianFilterWorkspace<T> public_median_workspace;
    const auto public_median_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::median_filter(ksj::array::as_const_view(input.view()), output.view(), median_kernel_size,
                                 public_median_workspace, causal_border);
    });
    ksj::benchmarks::print_row(
      "median_filter_causal_5", "public_policy", type_name, size, config, public_median_ns,
      ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row(
        "median_filter_causal_5", "workspace_reuse",
        ksj::signal::detail::prefer_intel_median_filter<T>(input.size()) ? "intel_ipp_copy_in_place" : "eigen"));
  }
}

template <typename T>
void run_convolve2d_benchmarks(std::string_view type_name, const ksj::benchmarks::Config& config) {
  constexpr std::size_t kernel_size = 5U;
  for (const auto size : config.sizes) {
    auto input = ksj::array::make_pooled_matrix<T>(size, size);
    auto kernel = ksj::array::make_pooled_matrix<T>(kernel_size, kernel_size);
    auto output = ksj::array::make_pooled_matrix<T>(size + kernel_size - 1U, size + kernel_size - 1U);
    ksj::benchmarks::fill_matrix(input);
    ksj::benchmarks::fill_matrix(kernel);

    const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::detail::eigen::convolve2d_full(ksj::array::as_const_view(input.view()),
                                                  ksj::array::as_const_view(kernel.view()), output.view());
    });
    ksj::benchmarks::print_row("convolve2d_full_5x5", "eigen", type_name, size, config, eigen_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::reference_row("convolve2d_full_5x5", "output_reuse"));

    if (ksj::signal::detail::intel::convolve2d_full(ksj::array::as_const_view(input.view()),
                                                    ksj::array::as_const_view(kernel.view()), output.view())) {
      const auto intel_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::signal::detail::intel::convolve2d_full(ksj::array::as_const_view(input.view()),
                                                          ksj::array::as_const_view(kernel.view()), output.view());
      });
      ksj::benchmarks::print_row("convolve2d_full_5x5", "intel_ipp", type_name, size, config, intel_ns,
                                 ksj::benchmarks::checksum(output),
                                 ksj::benchmarks::candidate_row("convolve2d_full_5x5", "output_reuse"));
    }

    const auto public_ns = ksj::benchmarks::measure(config, [&] {
      ksj::signal::convolve2d_full(ksj::array::as_const_view(input.view()), ksj::array::as_const_view(kernel.view()),
                                   output.view());
    });
    ksj::benchmarks::print_row(
      "convolve2d_full_5x5", "public_policy", type_name, size, config, public_ns, ksj::benchmarks::checksum(output),
      ksj::benchmarks::policy_row(
        "convolve2d_full_5x5", "output_reuse",
        ksj::signal::detail::prefer_intel_convolve2d_full<T>(input.size(), kernel.size()) ? "intel_ipp" : "eigen"));
  }
}

} // namespace

void run_filter_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_filter_benchmarks<float>("float", config);
  run_convolve2d_benchmarks<float>("float", config);
}

void run_filter_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_filter_benchmarks<double>("double", config);
  run_convolve2d_benchmarks<double>("double", config);
}

} // namespace ksj::benchmarks::signal_benchmarks
