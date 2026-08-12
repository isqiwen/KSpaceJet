#include "benchmark_common.hpp"

#include "kspacejet/nufft/detail/bart/bart_nufft2.hpp"
#include "kspacejet/nufft/detail/nufft_policy.hpp"
#include "kspacejet/nufft/nufft.hpp"

#include <algorithm>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {

using Complex = ksj::base::cf32;

[[nodiscard]] std::size_t sample_count(const std::size_t side) {
  return std::max<std::size_t>(1U, side * 4U);
}

void fill_image(ksj::array::PooledMatrix<Complex>& image) {
  for (std::size_t row = 0U; row < image.rows(); ++row) {
    for (std::size_t col = 0U; col < image.cols(); ++col) {
      image(row, col) = {static_cast<float>((row + 1U) * (col + 3U)) / 31.0F,
                         static_cast<float>((row + col + 1U) % 7U) / 19.0F};
    }
  }
}

void fill_trajectory(ksj::array::PooledMatrix<float>& trajectory) {
  constexpr auto scale = std::numbers::pi_v<float> / 17.0F;
  for (std::size_t sample = 0U; sample < trajectory.rows(); ++sample) {
    const auto row_index = static_cast<int>((sample * 3U) % 17U) - 8;
    const auto col_index = static_cast<int>((sample * 5U + 1U) % 17U) - 8;
    trajectory(sample, 0U) = static_cast<float>(row_index) * scale;
    trajectory(sample, 1U) = static_cast<float>(col_index) * scale;
  }
}

void fill_samples(ksj::array::PooledVector<Complex>& samples) {
  for (std::size_t sample = 0U; sample < samples.size(); ++sample) {
    samples(sample) = {static_cast<float>((sample % 11U) + 1U) / 17.0F, static_cast<float>((sample % 5U) + 1U) / 23.0F};
  }
}

[[nodiscard]] std::string_view selected_automatic_backend() {
  return ksj::nufft::detail::prefer_bart_nufft2(ksj::nufft::Backend::automatic) ? "bart" : "eigen_direct";
}

[[nodiscard]] double checksum(const ksj::array::PooledVector<Complex>& values) {
  double sum = 0.0;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    sum += static_cast<double>(values(index).real()) + 0.5 * static_cast<double>(values(index).imag());
  }
  return sum;
}

[[nodiscard]] double checksum(const ksj::array::PooledMatrix<Complex>& values) {
  double sum = 0.0;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    sum += static_cast<double>(values.data()[index].real()) + 0.5 * static_cast<double>(values.data()[index].imag());
  }
  return sum;
}

template <typename Function>
void measure_vector_row(const std::string_view case_name, const std::string_view backend,
                        const std::string_view timing_scope, const std::string_view role,
                        const std::string_view selected_backend, const std::size_t side,
                        const ksj::benchmarks::Config& config, ksj::array::PooledVector<Complex>& output,
                        Function&& function) {
  function();
  ksj::benchmarks::do_not_optimize(output.data()[0]);
  const auto measurement = ksj::benchmarks::measure(config, [&] {
    function();
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  });
  const auto metadata =
    role == "reference"
      ? ksj::benchmarks::reference_row(case_name, timing_scope, 1.0e-3, 1.0e-4)
      : (role == "policy" ? ksj::benchmarks::policy_row(case_name, timing_scope, selected_backend, 1.0e-3, 1.0e-4)
                          : ksj::benchmarks::candidate_row(case_name, timing_scope, 1.0e-3, 1.0e-4));
  ksj::benchmarks::print_row(case_name, backend, "complex_float", side, config, measurement, checksum(output),
                             metadata);
}

template <typename Function>
void measure_matrix_row(const std::string_view case_name, const std::string_view backend,
                        const std::string_view timing_scope, const std::string_view role,
                        const std::string_view selected_backend, const std::size_t side,
                        const ksj::benchmarks::Config& config, ksj::array::PooledMatrix<Complex>& output,
                        Function&& function) {
  function();
  ksj::benchmarks::do_not_optimize(output.data()[0]);
  const auto measurement = ksj::benchmarks::measure(config, [&] {
    function();
    ksj::benchmarks::do_not_optimize(output.data()[0]);
  });
  const auto metadata =
    role == "reference"
      ? ksj::benchmarks::reference_row(case_name, timing_scope, 1.0e-3, 1.0e-4)
      : (role == "policy" ? ksj::benchmarks::policy_row(case_name, timing_scope, selected_backend, 1.0e-3, 1.0e-4)
                          : ksj::benchmarks::candidate_row(case_name, timing_scope, 1.0e-3, 1.0e-4));
  ksj::benchmarks::print_row(case_name, backend, "complex_float", side, config, measurement, checksum(output),
                             metadata);
}

void run_forward(const std::size_t side, const ksj::benchmarks::Config& config) {
  const auto grid = ksj::nufft::Grid2D{side, side, 0.0, 0.0};
  auto image = ksj::array::make_pooled_matrix<Complex>(side, side);
  auto trajectory = ksj::array::make_pooled_matrix<float>(sample_count(side), 2U);
  auto output = ksj::array::make_pooled_vector<Complex>(trajectory.rows());
  ksj::benchmarks::require_pooled_storage("nufft_image", image);
  ksj::benchmarks::require_pooled_storage("nufft_trajectory", trajectory);
  ksj::benchmarks::require_pooled_storage("nufft_output", output);
  fill_image(image);
  fill_trajectory(trajectory);

  constexpr auto case_name = "nufft2_forward_direct_dft";
  constexpr auto automatic_options = ksj::nufft::Nufft2Options{ksj::nufft::Backend::automatic, true};
  const auto selected_backend = selected_automatic_backend();

  measure_vector_row(case_name, "eigen_direct", "cold_plan", "reference", {}, side, config, output, [&] {
    ksj::nufft::direct_nudft2_forward(grid, image.view(), trajectory.view(), output.view());
  });
  if (ksj::nufft::bart_backend_available()) {
    measure_vector_row(case_name, "bart", "cold_plan", "candidate", {}, side, config, output, [&] {
      auto workspace = ksj::nufft::Nufft2Workspace<float>{};
      if (!ksj::nufft::detail::bart::nufft2_forward(grid, ksj::array::as_const_view(image.view()),
                                                    ksj::array::as_const_view(trajectory.view()), output.view(),
                                                    workspace, true)) {
        throw std::runtime_error("BART NUFFT forward backend is unavailable");
      }
    });
  }
  measure_vector_row(case_name, "public_policy", "cold_plan", "policy", selected_backend, side, config, output, [&] {
    ksj::nufft::nufft2_forward(grid, image.view(), trajectory.view(), output.view(), automatic_options);
  });

  auto bart_workspace = ksj::nufft::Nufft2Workspace<float>{};
  if (ksj::nufft::bart_backend_available() &&
      !ksj::nufft::detail::bart::nufft2_forward(grid, ksj::array::as_const_view(image.view()),
                                                ksj::array::as_const_view(trajectory.view()), output.view(),
                                                bart_workspace, true)) {
    throw std::runtime_error("BART NUFFT forward warm-up failed");
  }
  measure_vector_row(case_name, "eigen_direct", "warm_plan", "reference", {}, side, config, output, [&] {
    ksj::nufft::direct_nudft2_forward(grid, image.view(), trajectory.view(), output.view());
  });
  if (ksj::nufft::bart_backend_available()) {
    measure_vector_row(case_name, "bart", "warm_plan", "candidate", {}, side, config, output, [&] {
      if (!ksj::nufft::detail::bart::nufft2_forward(grid, ksj::array::as_const_view(image.view()),
                                                    ksj::array::as_const_view(trajectory.view()), output.view(),
                                                    bart_workspace, true)) {
        throw std::runtime_error("BART NUFFT forward backend is unavailable");
      }
    });
  }
  auto public_workspace = ksj::nufft::Nufft2Workspace<float>{};
  if (selected_backend == "bart") {
    ksj::nufft::nufft2_forward(grid, image.view(), trajectory.view(), output.view(), public_workspace,
                               automatic_options);
  }
  measure_vector_row(case_name, "public_policy", "warm_plan", "policy", selected_backend, side, config, output, [&] {
    ksj::nufft::nufft2_forward(grid, image.view(), trajectory.view(), output.view(), public_workspace,
                               automatic_options);
  });
}

void run_adjoint(const std::size_t side, const ksj::benchmarks::Config& config) {
  const auto grid = ksj::nufft::Grid2D{side, side, 0.0, 0.0};
  auto samples = ksj::array::make_pooled_vector<Complex>(sample_count(side));
  auto trajectory = ksj::array::make_pooled_matrix<float>(samples.size(), 2U);
  auto image = ksj::array::make_pooled_matrix<Complex>(side, side);
  ksj::benchmarks::require_pooled_storage("nufft_samples", samples);
  ksj::benchmarks::require_pooled_storage("nufft_trajectory", trajectory);
  ksj::benchmarks::require_pooled_storage("nufft_image", image);
  fill_samples(samples);
  fill_trajectory(trajectory);

  constexpr auto case_name = "nufft2_adjoint_direct_dft";
  constexpr auto automatic_options = ksj::nufft::Nufft2Options{ksj::nufft::Backend::automatic, true};
  const auto selected_backend = selected_automatic_backend();

  measure_matrix_row(case_name, "eigen_direct", "cold_plan", "reference", {}, side, config, image, [&] {
    ksj::nufft::direct_nudft2_adjoint(grid, samples.view(), trajectory.view(), image.view());
  });
  if (ksj::nufft::bart_backend_available()) {
    measure_matrix_row(case_name, "bart", "cold_plan", "candidate", {}, side, config, image, [&] {
      auto workspace = ksj::nufft::Nufft2Workspace<float>{};
      if (!ksj::nufft::detail::bart::nufft2_adjoint(grid, ksj::array::as_const_view(samples.view()),
                                                    ksj::array::as_const_view(trajectory.view()), image.view(),
                                                    workspace, true)) {
        throw std::runtime_error("BART NUFFT adjoint backend is unavailable");
      }
    });
  }
  measure_matrix_row(case_name, "public_policy", "cold_plan", "policy", selected_backend, side, config, image, [&] {
    ksj::nufft::nufft2_adjoint(grid, samples.view(), trajectory.view(), image.view(), automatic_options);
  });

  auto bart_workspace = ksj::nufft::Nufft2Workspace<float>{};
  if (ksj::nufft::bart_backend_available() &&
      !ksj::nufft::detail::bart::nufft2_adjoint(grid, ksj::array::as_const_view(samples.view()),
                                                ksj::array::as_const_view(trajectory.view()), image.view(),
                                                bart_workspace, true)) {
    throw std::runtime_error("BART NUFFT adjoint warm-up failed");
  }
  measure_matrix_row(case_name, "eigen_direct", "warm_plan", "reference", {}, side, config, image, [&] {
    ksj::nufft::direct_nudft2_adjoint(grid, samples.view(), trajectory.view(), image.view());
  });
  if (ksj::nufft::bart_backend_available()) {
    measure_matrix_row(case_name, "bart", "warm_plan", "candidate", {}, side, config, image, [&] {
      if (!ksj::nufft::detail::bart::nufft2_adjoint(grid, ksj::array::as_const_view(samples.view()),
                                                    ksj::array::as_const_view(trajectory.view()), image.view(),
                                                    bart_workspace, true)) {
        throw std::runtime_error("BART NUFFT adjoint backend is unavailable");
      }
    });
  }
  auto public_workspace = ksj::nufft::Nufft2Workspace<float>{};
  if (selected_backend == "bart") {
    ksj::nufft::nufft2_adjoint(grid, samples.view(), trajectory.view(), image.view(), public_workspace,
                               automatic_options);
  }
  measure_matrix_row(case_name, "public_policy", "warm_plan", "policy", selected_backend, side, config, image, [&] {
    ksj::nufft::nufft2_adjoint(grid, samples.view(), trajectory.view(), image.view(), public_workspace,
                               automatic_options);
  });
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_nufft_backend_benchmark [--iterations N] [--sizes 4,8,16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  for (const auto side : config.sizes) {
    run_forward(side, config);
    run_adjoint(side, config);
  }
  return 0;
}
