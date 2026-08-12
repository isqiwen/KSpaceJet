#include "linalg_benchmark_common.hpp"

#include <algorithm>
#include <omp.h>

namespace ksj::benchmarks::linalg_benchmarks {
namespace {

template <typename Real>
[[nodiscard]] std::complex<Real> manual_conjugate_dot(ksj::array::VectorView<const std::complex<Real>> lhs,
                                                      ksj::array::VectorView<const std::complex<Real>> rhs) {
  std::complex<Real> sum{};
  for (std::size_t index = 0U; index < lhs.size(); ++index) {
    sum += std::conj(lhs[index]) * rhs[index];
  }
  return sum;
}

template <typename Real>
[[nodiscard]] std::complex<Real> manual_omp_conjugate_dot(ksj::array::VectorView<const std::complex<Real>> lhs,
                                                          ksj::array::VectorView<const std::complex<Real>> rhs) {
  Real real_sum{};
  Real imag_sum{};
  const auto size = static_cast<std::ptrdiff_t>(lhs.size());
#pragma omp parallel for reduction(+ : real_sum, imag_sum) num_threads(std::max(1, 2 * omp_get_num_procs() - 1))
  for (std::ptrdiff_t index = 0; index < size; ++index) {
    const auto product = std::conj(lhs[static_cast<std::size_t>(index)]) * rhs[static_cast<std::size_t>(index)];
    real_sum += product.real();
    imag_sum += product.imag();
  }
  return {real_sum, imag_sum};
}

template <typename Real> void run_for_complex_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  using complex_type = std::complex<Real>;

  for (const auto size : config.sizes) {
    if (size > kMaxDecompositionSize) {
      continue;
    }

    auto matrix = ksj::array::make_pooled_matrix<complex_type>(size, size);
    auto hermitian = ksj::array::make_pooled_matrix<complex_type>(size, size);
    ksj::benchmarks::require_pooled_storage("complex_matrix", matrix);
    ksj::benchmarks::require_pooled_storage("complex_hermitian", hermitian);
    fill_complex_matrix(matrix);
    fill_hermitian_matrix(hermitian);

    auto eigen_inverse = ksj::linalg::detail::eigen::inverse(ksj::array::as_const_view(matrix.view()));
    const auto eigen_inverse_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::detail::eigen::inverse(ksj::array::as_const_view(matrix.view()), eigen_inverse.view());
      ksj::benchmarks::do_not_optimize(eigen_inverse.data()[0]);
    });
    print_linalg_benchmark_row("complex_inverse", "eigen_output", type_name, size, config, eigen_inverse_ns,
                               checksum(eigen_inverse));

    auto intel_inverse = ksj::array::make_pooled_matrix<complex_type>(size, size);
    if (ksj::linalg::detail::intel::inverse(ksj::array::as_const_view(matrix.view()), intel_inverse.view())) {
      const auto intel_inverse_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::inverse(ksj::array::as_const_view(matrix.view()), intel_inverse.view());
        ksj::benchmarks::do_not_optimize(intel_inverse.data()[0]);
      });
      print_linalg_benchmark_row("complex_inverse", "intel_lapacke", type_name, size, config, intel_inverse_ns,
                                 checksum(intel_inverse));
    }

    ksj::linalg::LuFactorWorkspace<complex_type> inverse_workspace;
    auto workspace_inverse = ksj::array::make_pooled_matrix<complex_type>(size, size);
    (void)ksj::linalg::inverse(ksj::array::as_const_view(matrix.view()), workspace_inverse.view(), inverse_workspace);
    const auto workspace_inverse_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::linalg::inverse(ksj::array::as_const_view(matrix.view()), workspace_inverse.view(), inverse_workspace);
      ksj::benchmarks::do_not_optimize(workspace_inverse.data()[0]);
    });
    print_linalg_benchmark_row("complex_inverse", "public_workspace", type_name, size, config, workspace_inverse_ns,
                               checksum(workspace_inverse));

    const auto sample_rows = size * 2U;
    if (size <= kMaxStatisticsSize) {
      auto samples = ksj::array::make_pooled_matrix<complex_type>(sample_rows, size);
      auto covariance_output = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto whitened_samples = ksj::array::make_pooled_matrix<complex_type>(sample_rows, size);
      ksj::benchmarks::require_pooled_storage("complex_samples", samples);
      ksj::benchmarks::require_pooled_storage("complex_covariance_output", covariance_output);
      ksj::benchmarks::require_pooled_storage("complex_whitened_samples", whitened_samples);
      fill_complex_matrix(samples);
      ksj::linalg::detail::eigen::covariance(samples, covariance_output, true);

      const auto covariance_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::detail::eigen::covariance(samples, covariance_output, true);
        ksj::benchmarks::do_not_optimize(covariance_output.data()[0]);
      });
      print_linalg_benchmark_row("complex_covariance", "eigen_loop_output", type_name, size, config,
                                 covariance_output_ns, checksum(covariance_output));

      auto intel_covariance_output = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_complex_covariance_output", intel_covariance_output);
      if (ksj::linalg::detail::intel::covariance_centered_product(samples, intel_covariance_output, true)) {
        const auto intel_covariance_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::covariance_centered_product(samples, intel_covariance_output, true);
          ksj::benchmarks::do_not_optimize(intel_covariance_output.data()[0]);
        });
        print_linalg_benchmark_row("complex_covariance", "intel_centered_gemm", type_name, size, config,
                                   intel_covariance_ns, checksum(intel_covariance_output));
      }

      auto public_covariance_output = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::benchmarks::require_pooled_storage("public_complex_covariance_output", public_covariance_output);
      const auto public_covariance_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::stats::covariance(samples, public_covariance_output);
        ksj::benchmarks::do_not_optimize(public_covariance_output.data()[0]);
      });
      print_linalg_benchmark_row("complex_covariance", "public_output_policy", type_name, size, config,
                                 public_covariance_output_ns, checksum(public_covariance_output));

      auto covariance_public = ksj::stats::covariance(samples);
      const auto covariance_public_ns = ksj::benchmarks::measure(config, [&] {
        covariance_public = ksj::stats::covariance(samples);
        ksj::benchmarks::do_not_optimize(covariance_public.data()[0]);
      });
      print_linalg_benchmark_row("complex_covariance", "public_api", type_name, size, config, covariance_public_ns,
                                 checksum(covariance_public));

      auto samples_whitening =
        ksj::linalg::detail::eigen::whitening_matrix_from_covariance(covariance_output, static_cast<Real>(1.0e-12));
      ksj::linalg::detail::eigen::whiten_samples(samples, samples_whitening, whitened_samples);
      const auto whiten_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::detail::eigen::whiten_samples(samples, samples_whitening, whitened_samples);
        ksj::benchmarks::do_not_optimize(whitened_samples.data()[0]);
      });
      print_linalg_benchmark_row("complex_whiten_samples", "eigen_output", type_name, size, config, whiten_output_ns,
                                 checksum(whitened_samples));

      auto intel_whitened_samples = ksj::array::make_pooled_matrix<complex_type>(sample_rows, size);
      ksj::benchmarks::require_pooled_storage("intel_complex_whitened_samples", intel_whitened_samples);
      if (ksj::linalg::detail::intel::whiten_samples(samples, samples_whitening, intel_whitened_samples)) {
        const auto intel_whiten_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::whiten_samples(samples, samples_whitening, intel_whitened_samples);
          ksj::benchmarks::do_not_optimize(intel_whitened_samples.data()[0]);
        });
        print_linalg_benchmark_row("complex_whiten_samples", "intel_gemm_output", type_name, size, config,
                                   intel_whiten_ns, checksum(intel_whitened_samples));
      }

      auto public_whitened_samples = ksj::array::make_pooled_matrix<complex_type>(sample_rows, size);
      ksj::benchmarks::require_pooled_storage("public_complex_whitened_samples", public_whitened_samples);
      const auto public_whiten_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::whiten_samples(samples, samples_whitening, public_whitened_samples);
        ksj::benchmarks::do_not_optimize(public_whitened_samples.data()[0]);
      });
      print_linalg_benchmark_row("complex_whiten_samples", "public_output_policy", type_name, size, config,
                                 public_whiten_output_ns, checksum(public_whitened_samples));

      auto whiten_public = ksj::linalg::whiten_samples(samples, samples_whitening);
      const auto whiten_public_ns = ksj::benchmarks::measure(config, [&] {
        whiten_public = ksj::linalg::whiten_samples(samples, samples_whitening);
        ksj::benchmarks::do_not_optimize(whiten_public.data()[0]);
      });
      print_linalg_benchmark_row("complex_whiten_samples", "public_api", type_name, size, config, whiten_public_ns,
                                 checksum(whiten_public));
    }

    auto whitening_matrix =
      ksj::linalg::detail::eigen::whitening_matrix_from_covariance(hermitian, static_cast<Real>(1.0e-12));
    const auto whitening_ns = ksj::benchmarks::measure(config, [&] {
      whitening_matrix =
        ksj::linalg::detail::eigen::whitening_matrix_from_covariance(hermitian, static_cast<Real>(1.0e-12));
      ksj::benchmarks::do_not_optimize(whitening_matrix.data()[0]);
    });
    print_linalg_benchmark_row("complex_whitening_matrix", "eigen_self_adjoint", type_name, size, config, whitening_ns,
                               checksum(whitening_matrix));

    auto intel_whitening_matrix = ksj::array::make_pooled_matrix<complex_type>(size, size);
    ksj::benchmarks::require_pooled_storage("intel_complex_whitening_matrix", intel_whitening_matrix);
    if (ksj::linalg::detail::intel::whitening_matrix_from_covariance(hermitian, intel_whitening_matrix)) {
      const auto intel_whitening_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::whitening_matrix_from_covariance(hermitian, intel_whitening_matrix);
        ksj::benchmarks::do_not_optimize(intel_whitening_matrix.data()[0]);
      });
      print_linalg_benchmark_row("complex_whitening_matrix", "intel_lapacke", type_name, size, config,
                                 intel_whitening_ns, checksum(intel_whitening_matrix));
    }

    auto public_whitening_matrix = ksj::linalg::whitening_matrix_from_covariance(hermitian);
    const auto public_whitening_ns = ksj::benchmarks::measure(config, [&] {
      public_whitening_matrix = ksj::linalg::whitening_matrix_from_covariance(hermitian);
      ksj::benchmarks::do_not_optimize(public_whitening_matrix.data()[0]);
    });
    print_linalg_benchmark_row("complex_whitening_matrix", "public_policy", type_name, size, config,
                               public_whitening_ns, checksum(public_whitening_matrix));

    auto vector = ksj::array::make_pooled_vector<complex_type>(size);
    auto vector_rhs = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::benchmarks::require_pooled_storage("complex_vector", vector);
    ksj::benchmarks::require_pooled_storage("complex_vector_rhs", vector_rhs);
    fill_complex_vector(vector);
    fill_complex_vector(vector_rhs);

    const auto vector_view = ksj::array::as_const_view(vector.view());
    const auto vector_rhs_view = ksj::array::as_const_view(vector_rhs.view());

    auto manual_dot = manual_conjugate_dot(vector_view, vector_rhs_view);
    const auto manual_dot_ns = ksj::benchmarks::measure(config, [&] {
      manual_dot = manual_conjugate_dot(vector_view, vector_rhs_view);
      ksj::benchmarks::do_not_optimize(manual_dot);
    });
    print_linalg_benchmark_row("complex_dot", "manual_serial", type_name, size, config, manual_dot_ns,
                               static_cast<double>(std::abs(manual_dot)));

    auto manual_omp_dot = manual_omp_conjugate_dot(vector_view, vector_rhs_view);
    const auto manual_omp_dot_ns = ksj::benchmarks::measure(config, [&] {
      manual_omp_dot = manual_omp_conjugate_dot(vector_view, vector_rhs_view);
      ksj::benchmarks::do_not_optimize(manual_omp_dot);
    });
    print_linalg_benchmark_row("complex_dot", "manual_omp_reduction", type_name, size, config, manual_omp_dot_ns,
                               static_cast<double>(std::abs(manual_omp_dot)));

    auto eigen_dot = ksj::linalg::detail::eigen::dot(vector_view, vector_rhs_view);
    const auto eigen_dot_ns = ksj::benchmarks::measure(config, [&] {
      eigen_dot = ksj::linalg::detail::eigen::dot(vector_view, vector_rhs_view);
      ksj::benchmarks::do_not_optimize(eigen_dot);
    });
    print_linalg_benchmark_row("complex_dot", "eigen_view", type_name, size, config, eigen_dot_ns,
                               static_cast<double>(std::abs(eigen_dot)));

    complex_type intel_dot{};
    if (ksj::linalg::detail::intel::dot(vector_view, vector_rhs_view, intel_dot)) {
      const auto intel_dot_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::dot(vector_view, vector_rhs_view, intel_dot);
        ksj::benchmarks::do_not_optimize(intel_dot);
      });
      print_linalg_benchmark_row("complex_dot", "intel_cblas_dotc", type_name, size, config, intel_dot_ns,
                                 static_cast<double>(std::abs(intel_dot)));
    }

    auto public_dot = ksj::linalg::dot(vector_view, vector_rhs_view);
    const auto public_dot_ns = ksj::benchmarks::measure(config, [&] {
      public_dot = ksj::linalg::dot(vector_view, vector_rhs_view);
      ksj::benchmarks::do_not_optimize(public_dot);
    });
    print_linalg_benchmark_row("complex_dot", "public_policy", type_name, size, config, public_dot_ns,
                               static_cast<double>(std::abs(public_dot)));

    auto cg_x = ksj::array::make_pooled_vector<complex_type>(size);
    auto cg_direction = ksj::array::make_pooled_vector<complex_type>(size);
    auto cg_residual = ksj::array::make_pooled_vector<complex_type>(size);
    auto cg_transformed_direction = ksj::array::make_pooled_vector<complex_type>(size);
    auto cg_x_output = ksj::array::make_pooled_vector<complex_type>(size);
    auto cg_residual_output = ksj::array::make_pooled_vector<complex_type>(size);
    auto cg_direction_output = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::benchmarks::require_pooled_storage("complex_cg_x", cg_x);
    ksj::benchmarks::require_pooled_storage("complex_cg_direction", cg_direction);
    ksj::benchmarks::require_pooled_storage("complex_cg_residual", cg_residual);
    ksj::benchmarks::require_pooled_storage("complex_cg_transformed_direction", cg_transformed_direction);
    ksj::benchmarks::require_pooled_storage("complex_cg_x_output", cg_x_output);
    ksj::benchmarks::require_pooled_storage("complex_cg_residual_output", cg_residual_output);
    ksj::benchmarks::require_pooled_storage("complex_cg_direction_output", cg_direction_output);
    fill_complex_vector(cg_x);
    fill_complex_vector(cg_direction);
    fill_complex_vector(cg_residual);
    fill_complex_vector(cg_transformed_direction);
    const complex_type alpha{static_cast<Real>(0.125), static_cast<Real>(-0.0625)};
    const complex_type beta{static_cast<Real>(0.25), static_cast<Real>(0.03125)};

    const auto manual_cg_update_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0U; index < size; ++index) {
        cg_x_output[index] = cg_x[index] + alpha * cg_direction[index];
        cg_residual_output[index] = cg_residual[index] - alpha * cg_transformed_direction[index];
        cg_direction_output[index] = cg_residual[index] + beta * cg_direction[index];
      }
      ksj::benchmarks::do_not_optimize(cg_x_output.data()[0]);
      ksj::benchmarks::do_not_optimize(cg_residual_output.data()[0]);
      ksj::benchmarks::do_not_optimize(cg_direction_output.data()[0]);
    });
    print_linalg_benchmark_row("complex_cg_update", "manual_output", type_name, size, config, manual_cg_update_ns,
                               checksum(cg_x_output) + checksum(cg_residual_output) + checksum(cg_direction_output));

    const auto public_cg_update_ns = ksj::benchmarks::measure(config, [&] {
      ksj::array::linear_combination(cg_x.view(), complex_type{1}, cg_direction.view(), alpha, cg_x_output.view());
      ksj::array::linear_combination(cg_residual.view(), complex_type{1}, cg_transformed_direction.view(), -alpha,
                                     cg_residual_output.view());
      ksj::array::linear_combination(cg_residual.view(), complex_type{1}, cg_direction.view(), beta,
                                     cg_direction_output.view());
      ksj::benchmarks::do_not_optimize(cg_x_output.data()[0]);
      ksj::benchmarks::do_not_optimize(cg_residual_output.data()[0]);
      ksj::benchmarks::do_not_optimize(cg_direction_output.data()[0]);
    });
    print_linalg_benchmark_row("complex_cg_update", "array_linear_combination", type_name, size, config,
                               public_cg_update_ns,
                               checksum(cg_x_output) + checksum(cg_residual_output) + checksum(cg_direction_output));

    auto eigen_solve = ksj::linalg::detail::eigen::solve(matrix, vector_rhs);
    const auto eigen_solve_ns = ksj::benchmarks::measure(config, [&] {
      eigen_solve = ksj::linalg::detail::eigen::solve(matrix, vector_rhs);
      ksj::benchmarks::do_not_optimize(eigen_solve.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_lu", "eigen_lu", type_name, size, config, eigen_solve_ns,
                               checksum(eigen_solve));

    auto intel_solve = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::benchmarks::require_pooled_storage("intel_complex_solve", intel_solve);
    if (ksj::linalg::detail::intel::solve_lu(matrix, vector_rhs, intel_solve)) {
      const auto intel_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_lu(matrix, vector_rhs, intel_solve);
        ksj::benchmarks::do_not_optimize(intel_solve.data()[0]);
      });
      print_linalg_benchmark_row("complex_solve_vector_lu", "intel_lapacke", type_name, size, config, intel_solve_ns,
                                 checksum(intel_solve));
    }

    auto public_solve = ksj::linalg::solve(matrix, vector_rhs);
    const auto public_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_solve = ksj::linalg::solve(matrix, vector_rhs);
      ksj::benchmarks::do_not_optimize(public_solve.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_lu", "public_policy", type_name, size, config, public_solve_ns,
                               checksum(public_solve));

    auto public_output_solve = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::benchmarks::require_pooled_storage("public_complex_output_solve", public_output_solve);
    ksj::linalg::solve(matrix, vector_rhs, public_output_solve);
    const auto public_output_solve_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::solve(matrix, vector_rhs, public_output_solve);
      ksj::benchmarks::do_not_optimize(public_output_solve.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_lu", "public_output", type_name, size, config,
                               public_output_solve_ns, checksum(public_output_solve));

    const auto rhs_cols = std::min(kMatrixRhsCols, size);
    auto rhs_matrix = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("complex_rhs_matrix", rhs_matrix);
    fill_complex_matrix(rhs_matrix);

    auto eigen_solve_matrix = ksj::linalg::detail::eigen::solve(matrix, rhs_matrix);
    const auto eigen_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      eigen_solve_matrix = ksj::linalg::detail::eigen::solve(matrix, rhs_matrix);
      ksj::benchmarks::do_not_optimize(eigen_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_lu", "eigen_lu", type_name, size, config, eigen_solve_matrix_ns,
                               checksum(eigen_solve_matrix));

    auto intel_solve_matrix = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("intel_complex_solve_matrix", intel_solve_matrix);
    if (ksj::linalg::detail::intel::solve_lu(matrix, rhs_matrix, intel_solve_matrix)) {
      const auto intel_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_lu(matrix, rhs_matrix, intel_solve_matrix);
        ksj::benchmarks::do_not_optimize(intel_solve_matrix.data()[0]);
      });
      print_linalg_benchmark_row("complex_solve_matrix_lu", "intel_lapacke", type_name, size, config,
                                 intel_solve_matrix_ns, checksum(intel_solve_matrix));
    }

    auto public_solve_matrix = ksj::linalg::solve(matrix, rhs_matrix);
    const auto public_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      public_solve_matrix = ksj::linalg::solve(matrix, rhs_matrix);
      ksj::benchmarks::do_not_optimize(public_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_lu", "public_policy", type_name, size, config,
                               public_solve_matrix_ns, checksum(public_solve_matrix));

    auto public_output_solve_matrix = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("public_complex_output_solve_matrix", public_output_solve_matrix);
    ksj::linalg::solve(matrix, rhs_matrix, public_output_solve_matrix);
    const auto public_output_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::solve(matrix, rhs_matrix, public_output_solve_matrix);
      ksj::benchmarks::do_not_optimize(public_output_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_lu", "public_output", type_name, size, config,
                               public_output_solve_matrix_ns, checksum(public_output_solve_matrix));

    ksj::linalg::LuSolveWorkspace<complex_type> lu_workspace;
    auto public_workspace_solve_matrix = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    (void)ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs_matrix.view()),
                                public_workspace_solve_matrix.view(), lu_workspace);
    const auto public_workspace_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::linalg::solve_lu(ksj::array::as_const_view(matrix.view()),
                                  ksj::array::as_const_view(rhs_matrix.view()), public_workspace_solve_matrix.view(),
                                  lu_workspace);
      ksj::benchmarks::do_not_optimize(public_workspace_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_lu", "public_workspace", type_name, size, config,
                               public_workspace_solve_matrix_ns, checksum(public_workspace_solve_matrix));

    auto cholesky_lower = ksj::linalg::detail::eigen::cholesky_lower(hermitian);
    const auto cholesky_lower_ns = ksj::benchmarks::measure(config, [&] {
      cholesky_lower = ksj::linalg::detail::eigen::cholesky_lower(hermitian);
      ksj::benchmarks::do_not_optimize(cholesky_lower.data()[0]);
    });
    print_linalg_benchmark_row("complex_cholesky_lower", "eigen_llt", type_name, size, config, cholesky_lower_ns,
                               checksum(cholesky_lower));

    auto intel_cholesky_lower = ksj::array::make_pooled_matrix<complex_type>(size, size);
    ksj::benchmarks::require_pooled_storage("intel_complex_cholesky_lower", intel_cholesky_lower);
    if (ksj::linalg::detail::intel::cholesky_lower(hermitian, intel_cholesky_lower)) {
      const auto intel_cholesky_lower_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::cholesky_lower(hermitian, intel_cholesky_lower);
        ksj::benchmarks::do_not_optimize(intel_cholesky_lower.data()[0]);
      });
      print_linalg_benchmark_row("complex_cholesky_lower", "intel_lapacke", type_name, size, config,
                                 intel_cholesky_lower_ns, checksum(intel_cholesky_lower));
    }

    auto public_cholesky_lower = ksj::linalg::cholesky_lower(hermitian);
    const auto public_cholesky_lower_ns = ksj::benchmarks::measure(config, [&] {
      public_cholesky_lower = ksj::linalg::cholesky_lower(hermitian);
      ksj::benchmarks::do_not_optimize(public_cholesky_lower.data()[0]);
    });
    print_linalg_benchmark_row("complex_cholesky_lower", "public_policy", type_name, size, config,
                               public_cholesky_lower_ns, checksum(public_cholesky_lower));

    auto cholesky_solution = ksj::linalg::detail::eigen::solve_cholesky(hermitian, vector_rhs);
    const auto cholesky_solve_ns = ksj::benchmarks::measure(config, [&] {
      cholesky_solution = ksj::linalg::detail::eigen::solve_cholesky(hermitian, vector_rhs);
      ksj::benchmarks::do_not_optimize(cholesky_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_cholesky", "eigen_llt", type_name, size, config, cholesky_solve_ns,
                               checksum(cholesky_solution));

    auto intel_cholesky_solution = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::benchmarks::require_pooled_storage("intel_complex_cholesky_solution", intel_cholesky_solution);
    if (ksj::linalg::detail::intel::solve_cholesky(hermitian, vector_rhs, intel_cholesky_solution)) {
      const auto intel_cholesky_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_cholesky(hermitian, vector_rhs, intel_cholesky_solution);
        ksj::benchmarks::do_not_optimize(intel_cholesky_solution.data()[0]);
      });
      print_linalg_benchmark_row("complex_solve_vector_cholesky", "intel_lapacke", type_name, size, config,
                                 intel_cholesky_solve_ns, checksum(intel_cholesky_solution));
    }

    auto public_cholesky_solution = ksj::linalg::solve_cholesky(hermitian, vector_rhs);
    const auto public_cholesky_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_cholesky_solution = ksj::linalg::solve_cholesky(hermitian, vector_rhs);
      ksj::benchmarks::do_not_optimize(public_cholesky_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_cholesky", "public_policy", type_name, size, config,
                               public_cholesky_solve_ns, checksum(public_cholesky_solution));

    auto cholesky_matrix_solution = ksj::linalg::detail::eigen::solve_cholesky(hermitian, rhs_matrix);
    const auto cholesky_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      cholesky_matrix_solution = ksj::linalg::detail::eigen::solve_cholesky(hermitian, rhs_matrix);
      ksj::benchmarks::do_not_optimize(cholesky_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_cholesky", "eigen_llt", type_name, size, config,
                               cholesky_matrix_solve_ns, checksum(cholesky_matrix_solution));

    auto intel_cholesky_matrix_solution = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("intel_complex_cholesky_matrix_solution", intel_cholesky_matrix_solution);
    if (ksj::linalg::detail::intel::solve_cholesky(hermitian, rhs_matrix, intel_cholesky_matrix_solution)) {
      const auto intel_cholesky_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_cholesky(hermitian, rhs_matrix, intel_cholesky_matrix_solution);
        ksj::benchmarks::do_not_optimize(intel_cholesky_matrix_solution.data()[0]);
      });
      print_linalg_benchmark_row("complex_solve_matrix_cholesky", "intel_lapacke", type_name, size, config,
                                 intel_cholesky_matrix_solve_ns, checksum(intel_cholesky_matrix_solution));
    }

    auto public_cholesky_matrix_solution = ksj::linalg::solve_cholesky(hermitian, rhs_matrix);
    const auto public_cholesky_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_cholesky_matrix_solution = ksj::linalg::solve_cholesky(hermitian, rhs_matrix);
      ksj::benchmarks::do_not_optimize(public_cholesky_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_cholesky", "public_policy", type_name, size, config,
                               public_cholesky_matrix_solve_ns, checksum(public_cholesky_matrix_solution));

    auto qr_matrix = ksj::array::make_pooled_matrix<complex_type>(sample_rows, size);
    auto qr_rhs = ksj::array::make_pooled_vector<complex_type>(sample_rows);
    ksj::benchmarks::require_pooled_storage("complex_qr_matrix", qr_matrix);
    ksj::benchmarks::require_pooled_storage("complex_qr_rhs", qr_rhs);
    fill_complex_matrix(qr_matrix);
    fill_complex_vector(qr_rhs);

    auto qr_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs);
    const auto qr_solve_ns = ksj::benchmarks::measure(config, [&] {
      qr_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs);
      ksj::benchmarks::do_not_optimize(qr_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_qr", "eigen_col_piv_qr", type_name, size, config, qr_solve_ns,
                               checksum(qr_solution));

    auto intel_qr_solution = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::benchmarks::require_pooled_storage("intel_complex_qr_solution", intel_qr_solution);
    if (ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs, intel_qr_solution)) {
      const auto intel_qr_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs, intel_qr_solution);
        ksj::benchmarks::do_not_optimize(intel_qr_solution.data()[0]);
      });
      print_linalg_benchmark_row("complex_solve_vector_qr", "intel_lapacke", type_name, size, config, intel_qr_solve_ns,
                                 checksum(intel_qr_solution));
    }

    auto public_qr_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs);
    const auto public_qr_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_qr_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs);
      ksj::benchmarks::do_not_optimize(public_qr_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_vector_qr", "public_policy", type_name, size, config, public_qr_solve_ns,
                               checksum(public_qr_solution));

    auto qr_rhs_matrix = ksj::array::make_pooled_matrix<complex_type>(sample_rows, rhs_cols);
    ksj::benchmarks::require_pooled_storage("complex_qr_rhs_matrix", qr_rhs_matrix);
    fill_complex_matrix(qr_rhs_matrix);

    auto qr_matrix_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs_matrix);
    const auto qr_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      qr_matrix_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs_matrix);
      ksj::benchmarks::do_not_optimize(qr_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_qr", "eigen_col_piv_qr", type_name, size, config,
                               qr_matrix_solve_ns, checksum(qr_matrix_solution));

    auto intel_qr_matrix_solution = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("intel_complex_qr_matrix_solution", intel_qr_matrix_solution);
    if (ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs_matrix, intel_qr_matrix_solution)) {
      const auto intel_qr_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs_matrix, intel_qr_matrix_solution);
        ksj::benchmarks::do_not_optimize(intel_qr_matrix_solution.data()[0]);
      });
      print_linalg_benchmark_row("complex_solve_matrix_qr", "intel_lapacke", type_name, size, config,
                                 intel_qr_matrix_solve_ns, checksum(intel_qr_matrix_solution));
    }

    auto public_qr_matrix_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs_matrix);
    const auto public_qr_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_qr_matrix_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs_matrix);
      ksj::benchmarks::do_not_optimize(public_qr_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("complex_solve_matrix_qr", "public_policy", type_name, size, config,
                               public_qr_matrix_solve_ns, checksum(public_qr_matrix_solution));

    if (size <= kMaxLeastSquaresVariantSize) {
      auto least_squares_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs);
      const auto least_squares_qr_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs);
        ksj::benchmarks::do_not_optimize(least_squares_qr.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_vector", "public_qr", type_name, size, config,
                                 least_squares_qr_ns, checksum(least_squares_qr));

      auto least_squares_svd =
        ksj::linalg::detail::eigen::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_svd =
          ksj::linalg::detail::eigen::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_svd.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_vector", "eigen_jacobi_svd", type_name, size, config,
                                 least_squares_svd_ns, checksum(least_squares_svd));

      auto intel_least_squares_svd = ksj::array::make_pooled_vector<complex_type>(size);
      ksj::benchmarks::require_pooled_storage("intel_complex_least_squares_svd", intel_least_squares_svd);
      if (ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs, intel_least_squares_svd)) {
        const auto intel_least_squares_svd_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs, intel_least_squares_svd);
          ksj::benchmarks::do_not_optimize(intel_least_squares_svd.data()[0]);
        });
        print_linalg_benchmark_row("complex_least_squares_vector", "intel_lapacke_gelss", type_name, size, config,
                                   intel_least_squares_svd_ns, checksum(intel_least_squares_svd));
      }

      auto least_squares_public_svd =
        ksj::linalg::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_public_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_public_svd =
          ksj::linalg::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_public_svd.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_vector", "public_svd_policy", type_name, size, config,
                                 least_squares_public_svd_ns, checksum(least_squares_public_svd));

      auto least_squares_normal = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations);
      const auto least_squares_normal_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_normal = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations);
        ksj::benchmarks::do_not_optimize(least_squares_normal.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_vector", "normal_equations_ldlt", type_name, size, config,
                                 least_squares_normal_ns, checksum(least_squares_normal));

      auto least_squares_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
      const auto least_squares_normal_cholesky_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
        ksj::benchmarks::do_not_optimize(least_squares_normal_cholesky.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_vector", "normal_equations_llt", type_name, size, config,
                                 least_squares_normal_cholesky_ns, checksum(least_squares_normal_cholesky));

      auto least_squares_matrix_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix);
      const auto least_squares_matrix_qr_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_qr.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_matrix", "public_qr", type_name, size, config,
                                 least_squares_matrix_qr_ns, checksum(least_squares_matrix_qr));

      auto least_squares_matrix_svd =
        ksj::linalg::detail::eigen::solve_least_squares(qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_matrix_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_svd = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_svd.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_matrix", "eigen_jacobi_svd", type_name, size, config,
                                 least_squares_matrix_svd_ns, checksum(least_squares_matrix_svd));

      auto intel_least_squares_matrix_svd = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
      ksj::benchmarks::require_pooled_storage("intel_complex_least_squares_matrix_svd", intel_least_squares_matrix_svd);
      if (ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs_matrix,
                                                              intel_least_squares_matrix_svd)) {
        const auto intel_least_squares_matrix_svd_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs_matrix,
                                                                    intel_least_squares_matrix_svd);
          ksj::benchmarks::do_not_optimize(intel_least_squares_matrix_svd.data()[0]);
        });
        print_linalg_benchmark_row("complex_least_squares_matrix", "intel_lapacke_gelss", type_name, size, config,
                                   intel_least_squares_matrix_svd_ns, checksum(intel_least_squares_matrix_svd));
      }

      auto least_squares_matrix_public_svd =
        ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_matrix_public_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_public_svd =
          ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_public_svd.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_matrix", "public_svd_policy", type_name, size, config,
                                 least_squares_matrix_public_svd_ns, checksum(least_squares_matrix_public_svd));

      auto least_squares_matrix_normal = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations);
      const auto least_squares_matrix_normal_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_normal = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_normal.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_matrix", "normal_equations_ldlt", type_name, size, config,
                                 least_squares_matrix_normal_ns, checksum(least_squares_matrix_normal));

      auto least_squares_matrix_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
      const auto least_squares_matrix_normal_cholesky_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_normal_cholesky.data()[0]);
      });
      print_linalg_benchmark_row("complex_least_squares_matrix", "normal_equations_llt", type_name, size, config,
                                 least_squares_matrix_normal_cholesky_ns,
                                 checksum(least_squares_matrix_normal_cholesky));

      run_rank_deficient_least_squares_benchmarks<complex_type>("complex_rank_deficient_least_squares", type_name, size,
                                                                sample_rows, rhs_cols, config);
    }

    auto singular_values = ksj::linalg::detail::eigen::singular_values(const_matrix_view(matrix));
    const auto singular_values_ns = ksj::benchmarks::measure(config, [&] {
      singular_values = ksj::linalg::detail::eigen::singular_values(const_matrix_view(matrix));
      ksj::benchmarks::do_not_optimize(singular_values.data()[0]);
    });
    print_linalg_benchmark_row("complex_singular_values", "eigen_jacobi_svd", type_name, size, config,
                               singular_values_ns, checksum(singular_values));

    auto intel_singular_values = ksj::array::make_pooled_vector<Real>(size);
    ksj::benchmarks::require_pooled_storage("intel_complex_singular_values", intel_singular_values);
    if (ksj::linalg::detail::intel::singular_values(matrix, intel_singular_values)) {
      const auto intel_singular_values_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::singular_values(matrix, intel_singular_values);
        ksj::benchmarks::do_not_optimize(intel_singular_values.data()[0]);
      });
      print_linalg_benchmark_row("complex_singular_values", "intel_lapacke", type_name, size, config,
                                 intel_singular_values_ns, checksum(intel_singular_values));
    }

    auto public_singular_values = ksj::linalg::singular_values(matrix);
    const auto public_singular_values_ns = ksj::benchmarks::measure(config, [&] {
      public_singular_values = ksj::linalg::singular_values(matrix);
      ksj::benchmarks::do_not_optimize(public_singular_values.data()[0]);
    });
    print_linalg_benchmark_row("complex_singular_values", "public_policy", type_name, size, config,
                               public_singular_values_ns, checksum(public_singular_values));

    if (size <= 64U) {
      const auto ecalib_kernel_count = std::max(size * 8U, size);
      auto ecalib_matrix = ksj::array::make_pooled_matrix<complex_type>(size, ecalib_kernel_count);
      auto ecalib_left_u = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::benchmarks::require_pooled_storage("ecalib_svd_matrix", ecalib_matrix);
      ksj::benchmarks::require_pooled_storage("ecalib_left_u", ecalib_left_u);
      fill_complex_matrix(ecalib_matrix);

      auto ecalib_allocating_svd = ksj::linalg::svd(ecalib_matrix);
      const auto ecalib_allocating_svd_ns = ksj::benchmarks::measure(config, [&] {
        ecalib_allocating_svd = ksj::linalg::svd(ecalib_matrix);
        ksj::benchmarks::do_not_optimize(ecalib_allocating_svd.u.data()[0]);
      });
      print_linalg_benchmark_row("complex_left_singular_vectors_ecalib", "svd_allocating_u", type_name, size, config,
                                 ecalib_allocating_svd_ns, checksum(ecalib_allocating_svd.u));

      ksj::linalg::left_singular_vectors(ksj::array::as_const_view(ecalib_matrix.view()), ecalib_left_u.view());
      const auto ecalib_left_u_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::left_singular_vectors(ksj::array::as_const_view(ecalib_matrix.view()), ecalib_left_u.view());
        ksj::benchmarks::do_not_optimize(ecalib_left_u.data()[0]);
      });
      print_linalg_benchmark_row("complex_left_singular_vectors_ecalib", "left_u_output", type_name, size, config,
                                 ecalib_left_u_ns, checksum(ecalib_left_u));
    }

    if (size <= kMaxFullSvdSize) {
      auto full_svd = ksj::linalg::detail::eigen::svd(const_matrix_view(matrix), true);
      const auto full_svd_ns = ksj::benchmarks::measure(config, [&] {
        full_svd = ksj::linalg::detail::eigen::svd(const_matrix_view(matrix), true);
        ksj::benchmarks::do_not_optimize(full_svd.u.data()[0]);
        ksj::benchmarks::do_not_optimize(full_svd.v_adjoint.data()[0]);
      });
      print_linalg_benchmark_row("complex_svd_full_uv", "eigen_jacobi_svd", type_name, size, config, full_svd_ns,
                                 checksum(full_svd.singular_values));

      auto intel_svd_u = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto intel_svd_values = ksj::array::make_pooled_vector<Real>(size);
      auto intel_svd_v_adjoint = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_complex_svd_u", intel_svd_u);
      ksj::benchmarks::require_pooled_storage("intel_complex_svd_values", intel_svd_values);
      ksj::benchmarks::require_pooled_storage("intel_complex_svd_v_adjoint", intel_svd_v_adjoint);
      if (ksj::linalg::detail::intel::svd(matrix, intel_svd_u, intel_svd_values, intel_svd_v_adjoint, true)) {
        const auto intel_full_svd_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::svd(matrix, intel_svd_u, intel_svd_values, intel_svd_v_adjoint, true);
          ksj::benchmarks::do_not_optimize(intel_svd_u.data()[0]);
          ksj::benchmarks::do_not_optimize(intel_svd_v_adjoint.data()[0]);
        });
        print_linalg_benchmark_row("complex_svd_full_uv", "intel_lapacke", type_name, size, config, intel_full_svd_ns,
                                   checksum(intel_svd_values));
      }

      auto public_full_svd = ksj::linalg::full_svd(matrix);
      const auto public_full_svd_ns = ksj::benchmarks::measure(config, [&] {
        public_full_svd = ksj::linalg::full_svd(matrix);
        ksj::benchmarks::do_not_optimize(public_full_svd.u.data()[0]);
        ksj::benchmarks::do_not_optimize(public_full_svd.v_adjoint.data()[0]);
      });
      print_linalg_benchmark_row("complex_svd_full_uv", "public_policy", type_name, size, config, public_full_svd_ns,
                                 checksum(public_full_svd.singular_values));

      auto workspace_svd_u = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto workspace_svd_values = ksj::array::make_pooled_vector<Real>(size);
      auto workspace_svd_v_adjoint = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::linalg::SvdWorkspace<complex_type> svd_workspace;
      ksj::benchmarks::require_pooled_storage("workspace_complex_svd_u", workspace_svd_u);
      ksj::benchmarks::require_pooled_storage("workspace_complex_svd_values", workspace_svd_values);
      ksj::benchmarks::require_pooled_storage("workspace_complex_svd_v_adjoint", workspace_svd_v_adjoint);
      (void)ksj::linalg::full_svd(matrix, workspace_svd_u, workspace_svd_values, workspace_svd_v_adjoint,
                                  svd_workspace);
      const auto workspace_full_svd_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::full_svd(matrix, workspace_svd_u, workspace_svd_values, workspace_svd_v_adjoint,
                                    svd_workspace);
        ksj::benchmarks::do_not_optimize(workspace_svd_u.data()[0]);
        ksj::benchmarks::do_not_optimize(workspace_svd_v_adjoint.data()[0]);
      });
      print_linalg_benchmark_row("complex_svd_full_uv", "public_workspace", type_name, size, config,
                                 workspace_full_svd_ns, checksum(workspace_svd_values));
    }

    auto self_adjoint = ksj::linalg::detail::eigen::self_adjoint_eigen_decomposition(const_matrix_view(hermitian));
    const auto self_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      self_adjoint = ksj::linalg::detail::eigen::self_adjoint_eigen_decomposition(const_matrix_view(hermitian));
      ksj::benchmarks::do_not_optimize(self_adjoint.eigenvalues.data()[0]);
      ksj::benchmarks::do_not_optimize(self_adjoint.eigenvectors.data()[0]);
    });
    print_linalg_benchmark_row("complex_self_adjoint_eigen", "eigen_solver", type_name, size, config, self_adjoint_ns,
                               checksum(self_adjoint.eigenvalues));

    auto intel_self_adjoint_values = ksj::array::make_pooled_vector<Real>(size);
    auto intel_self_adjoint_vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
    ksj::benchmarks::require_pooled_storage("intel_complex_self_adjoint_values", intel_self_adjoint_values);
    ksj::benchmarks::require_pooled_storage("intel_complex_self_adjoint_vectors", intel_self_adjoint_vectors);
    if (ksj::linalg::detail::intel::self_adjoint_eigen_decomposition(hermitian, intel_self_adjoint_values,
                                                                     intel_self_adjoint_vectors)) {
      const auto intel_self_adjoint_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::self_adjoint_eigen_decomposition(hermitian, intel_self_adjoint_values,
                                                                           intel_self_adjoint_vectors);
        ksj::benchmarks::do_not_optimize(intel_self_adjoint_values.data()[0]);
        ksj::benchmarks::do_not_optimize(intel_self_adjoint_vectors.data()[0]);
      });
      print_linalg_benchmark_row("complex_self_adjoint_eigen", "intel_lapacke", type_name, size, config,
                                 intel_self_adjoint_ns, checksum(intel_self_adjoint_values));
    }

    auto public_self_adjoint = ksj::linalg::self_adjoint_eigen_decomposition(hermitian);
    const auto public_self_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      public_self_adjoint = ksj::linalg::self_adjoint_eigen_decomposition(hermitian);
      ksj::benchmarks::do_not_optimize(public_self_adjoint.eigenvalues.data()[0]);
      ksj::benchmarks::do_not_optimize(public_self_adjoint.eigenvectors.data()[0]);
    });
    print_linalg_benchmark_row("complex_self_adjoint_eigen", "public_policy", type_name, size, config,
                               public_self_adjoint_ns, checksum(public_self_adjoint.eigenvalues));

    if (size <= kMaxGeneralEigenSize) {
      auto general_eigen = ksj::linalg::detail::eigen::eigen_decomposition(matrix);
      const auto general_eigen_ns = ksj::benchmarks::measure(config, [&] {
        general_eigen = ksj::linalg::detail::eigen::eigen_decomposition(matrix);
        ksj::benchmarks::do_not_optimize(general_eigen.eigenvalues.data()[0]);
        ksj::benchmarks::do_not_optimize(general_eigen.eigenvectors.data()[0]);
      });
      print_linalg_benchmark_row("complex_general_eigen", "eigen_solver", type_name, size, config, general_eigen_ns,
                                 checksum(general_eigen.eigenvalues));

      auto intel_general_values = ksj::array::make_pooled_vector<complex_type>(size);
      auto intel_general_vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_complex_general_values", intel_general_values);
      ksj::benchmarks::require_pooled_storage("intel_complex_general_vectors", intel_general_vectors);
      if (ksj::linalg::detail::intel::eigen_decomposition(matrix, intel_general_values, intel_general_vectors)) {
        const auto intel_general_eigen_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::eigen_decomposition(matrix, intel_general_values, intel_general_vectors);
          ksj::benchmarks::do_not_optimize(intel_general_values.data()[0]);
          ksj::benchmarks::do_not_optimize(intel_general_vectors.data()[0]);
        });
        print_linalg_benchmark_row("complex_general_eigen", "intel_lapacke", type_name, size, config,
                                   intel_general_eigen_ns, checksum(intel_general_values));
      }

      auto public_general_eigen = ksj::linalg::eigen_decomposition(matrix);
      const auto public_general_eigen_ns = ksj::benchmarks::measure(config, [&] {
        public_general_eigen = ksj::linalg::eigen_decomposition(matrix);
        ksj::benchmarks::do_not_optimize(public_general_eigen.eigenvalues.data()[0]);
        ksj::benchmarks::do_not_optimize(public_general_eigen.eigenvectors.data()[0]);
      });
      print_linalg_benchmark_row("complex_general_eigen", "public_policy", type_name, size, config,
                                 public_general_eigen_ns, checksum(public_general_eigen.eigenvalues));

      ksj::linalg::GeneralEigenWorkspace<complex_type> general_eigen_workspace;
      auto workspace_general_values = ksj::array::make_pooled_vector<complex_type>(size);
      auto workspace_general_vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
      (void)ksj::linalg::eigen_decomposition(ksj::array::as_const_view(matrix.view()), workspace_general_values.view(),
                                             workspace_general_vectors.view(), general_eigen_workspace);
      const auto workspace_general_eigen_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::eigen_decomposition(ksj::array::as_const_view(matrix.view()),
                                               workspace_general_values.view(), workspace_general_vectors.view(),
                                               general_eigen_workspace);
        ksj::benchmarks::do_not_optimize(workspace_general_values.data()[0]);
        ksj::benchmarks::do_not_optimize(workspace_general_vectors.data()[0]);
      });
      print_linalg_benchmark_row("complex_general_eigen", "public_workspace", type_name, size, config,
                                 workspace_general_eigen_ns, checksum(workspace_general_values));
    }
  }
}

} // namespace

void run_complex_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_for_complex_type<float>("complex_float", config);
}

void run_complex_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_for_complex_type<double>("complex_double", config);
}

} // namespace ksj::benchmarks::linalg_benchmarks
