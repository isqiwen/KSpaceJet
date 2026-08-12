#include "linalg_benchmark_common.hpp"

namespace ksj::benchmarks::linalg_benchmarks {
namespace {

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  run_small_solve_benchmarks<T>(type_name, config);

  for (const auto size : config.sizes) {
    auto lhs = ksj::array::make_pooled_matrix<T>(size, size);
    auto rhs = ksj::array::make_pooled_matrix<T>(size, size);
    auto matrix = ksj::array::make_pooled_matrix<T>(size, size);
    auto vector = ksj::array::make_pooled_vector<T>(size);
    auto vector_rhs = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("lhs", lhs);
    ksj::benchmarks::require_pooled_storage("rhs", rhs);
    ksj::benchmarks::require_pooled_storage("matrix", matrix);
    ksj::benchmarks::require_pooled_storage("vector", vector);
    ksj::benchmarks::require_pooled_storage("vector_rhs", vector_rhs);
    fill_matrix(lhs);
    fill_matrix(rhs);
    fill_matrix(matrix);
    fill_vector(vector);
    fill_vector(vector_rhs);

    auto eigen_mm = ksj::array::make_pooled_matrix<T>(size, size);
    ksj::benchmarks::require_pooled_storage("eigen_mm", eigen_mm);
    ksj::linalg::detail::eigen::matmul(const_matrix_view(lhs), const_matrix_view(rhs), eigen_mm.view());
    const auto eigen_mm_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::detail::eigen::matmul(const_matrix_view(lhs), const_matrix_view(rhs), eigen_mm.view());
    });
    print_linalg_benchmark_row("matmul", "eigen", type_name, size, config, eigen_mm_ns, checksum(eigen_mm),
                               {"matmul", "output_reuse", ksj::benchmarks::RowRole::reference});

    auto intel_mm = ksj::array::make_pooled_matrix<T>(size, size);
    ksj::benchmarks::require_pooled_storage("intel_mm", intel_mm);
    if (ksj::linalg::detail::intel::matmul(const_matrix_view(lhs), const_matrix_view(rhs), intel_mm.view())) {
      const auto intel_mm_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::matmul(const_matrix_view(lhs), const_matrix_view(rhs), intel_mm.view());
      });
      print_linalg_benchmark_row("matmul", "intel_mkl", type_name, size, config, intel_mm_ns, checksum(intel_mm),
                                 {"matmul", "output_reuse", ksj::benchmarks::RowRole::candidate});
    }

    auto public_mm = ksj::array::make_pooled_matrix<T>(size, size);
    ksj::benchmarks::require_pooled_storage("public_mm", public_mm);
    ksj::linalg::matmul(const_matrix_view(lhs), const_matrix_view(rhs), public_mm.view());
    const auto public_mm_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::matmul(const_matrix_view(lhs), const_matrix_view(rhs), public_mm.view());
    });
    print_linalg_benchmark_row("matmul", "public_policy", type_name, size, config, public_mm_ns, checksum(public_mm),
                               {"matmul", "output_reuse", ksj::benchmarks::RowRole::policy, "intel_mkl"});

    auto eigen_gemv = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("eigen_gemv", eigen_gemv);
    ksj::linalg::detail::eigen::gemv(const_matrix_view(matrix), const_vector_view(vector), eigen_gemv.view());
    const auto eigen_gemv_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::detail::eigen::gemv(const_matrix_view(matrix), const_vector_view(vector), eigen_gemv.view());
    });
    print_linalg_benchmark_row("gemv", "eigen", type_name, size, config, eigen_gemv_ns, checksum(eigen_gemv),
                               {"gemv", "output_reuse", ksj::benchmarks::RowRole::reference});

    auto intel_gemv = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("intel_gemv", intel_gemv);
    if (ksj::linalg::detail::intel::gemv(const_matrix_view(matrix), const_vector_view(vector), intel_gemv.view())) {
      const auto intel_gemv_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::gemv(const_matrix_view(matrix), const_vector_view(vector), intel_gemv.view());
      });
      print_linalg_benchmark_row("gemv", "intel_mkl", type_name, size, config, intel_gemv_ns, checksum(intel_gemv),
                                 {"gemv", "output_reuse", ksj::benchmarks::RowRole::candidate});
    }

    auto public_gemv = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("public_gemv", public_gemv);
    ksj::linalg::gemv(const_matrix_view(matrix), const_vector_view(vector), public_gemv.view());
    const auto public_gemv_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::gemv(const_matrix_view(matrix), const_vector_view(vector), public_gemv.view());
    });
    const auto gemv_selected_backend = ksj::linalg::detail::prefer_intel_gemv(size, size) ? "intel_mkl" : "eigen";
    print_linalg_benchmark_row("gemv", "public_policy", type_name, size, config, public_gemv_ns, checksum(public_gemv),
                               {"gemv", "output_reuse", ksj::benchmarks::RowRole::policy, gemv_selected_backend});

    auto eigen_dot = ksj::linalg::detail::eigen::dot(const_vector_view(vector), const_vector_view(vector_rhs));
    const auto eigen_dot_ns = ksj::benchmarks::measure(config, [&] {
      eigen_dot = ksj::linalg::detail::eigen::dot(const_vector_view(vector), const_vector_view(vector_rhs));
      ksj::benchmarks::do_not_optimize(eigen_dot);
    });
    print_linalg_benchmark_row("dot", "eigen", type_name, size, config, eigen_dot_ns, static_cast<double>(eigen_dot),
                               {"dot", "output_reuse", ksj::benchmarks::RowRole::reference});

    T intel_dot{};
    if (ksj::linalg::detail::intel::dot(const_vector_view(vector), const_vector_view(vector_rhs), intel_dot)) {
      const auto intel_dot_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::dot(const_vector_view(vector), const_vector_view(vector_rhs), intel_dot);
        ksj::benchmarks::do_not_optimize(intel_dot);
      });
      print_linalg_benchmark_row("dot", "intel_mkl", type_name, size, config, intel_dot_ns,
                                 static_cast<double>(intel_dot),
                                 {"dot", "output_reuse", ksj::benchmarks::RowRole::candidate});
    }

    auto public_dot = ksj::linalg::dot(const_vector_view(vector), const_vector_view(vector_rhs));
    const auto public_dot_ns = ksj::benchmarks::measure(config, [&] {
      public_dot = ksj::linalg::dot(const_vector_view(vector), const_vector_view(vector_rhs));
      ksj::benchmarks::do_not_optimize(public_dot);
    });
    const auto dot_selected_backend = ksj::linalg::detail::prefer_intel_dot<T>(size) ? "intel_mkl" : "eigen";
    print_linalg_benchmark_row("dot", "public_policy", type_name, size, config, public_dot_ns,
                               static_cast<double>(public_dot),
                               {"dot", "output_reuse", ksj::benchmarks::RowRole::policy, dot_selected_backend});

    auto eigen_scale = ksj::linalg::detail::eigen::scale(vector, static_cast<T>(2));
    const auto eigen_scale_ns = ksj::benchmarks::measure(config, [&] {
      eigen_scale = ksj::linalg::detail::eigen::scale(vector, static_cast<T>(2));
      ksj::benchmarks::do_not_optimize(eigen_scale.data()[0]);
    });
    print_linalg_benchmark_row("scale_vector", "eigen", type_name, size, config, eigen_scale_ns, checksum(eigen_scale));

    auto eigen_scale_output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("eigen_scale_output", eigen_scale_output);
    const auto eigen_scale_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::scale(vector, eigen_scale_output, static_cast<T>(2));
      ksj::benchmarks::do_not_optimize(eigen_scale_output.data()[0]);
    });
    print_linalg_benchmark_row("scale_vector", "eigen_output", type_name, size, config, eigen_scale_output_ns,
                               checksum(eigen_scale_output));

    auto manual_scale = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("manual_scale", manual_scale);
    const auto manual_scale_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0; index < vector.size(); ++index) {
        manual_scale(index) = vector(index) * static_cast<T>(2);
      }
      ksj::benchmarks::do_not_optimize(manual_scale.data()[0]);
    });
    print_linalg_benchmark_row("scale_vector", "manual_output", type_name, size, config, manual_scale_ns,
                               checksum(manual_scale));

    auto eigen_axpy = ksj::linalg::detail::eigen::axpy(static_cast<T>(2), vector, vector_rhs);
    const auto eigen_axpy_ns = ksj::benchmarks::measure(config, [&] {
      eigen_axpy = ksj::linalg::detail::eigen::axpy(static_cast<T>(2), vector, vector_rhs);
      ksj::benchmarks::do_not_optimize(eigen_axpy.data()[0]);
    });
    print_linalg_benchmark_row("axpy", "eigen", type_name, size, config, eigen_axpy_ns, checksum(eigen_axpy));

    auto eigen_axpy_output = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("eigen_axpy_output", eigen_axpy_output);
    const auto eigen_axpy_output_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::axpy(static_cast<T>(2), vector, vector_rhs, eigen_axpy_output);
      ksj::benchmarks::do_not_optimize(eigen_axpy_output.data()[0]);
    });
    print_linalg_benchmark_row("axpy", "eigen_output", type_name, size, config, eigen_axpy_output_ns,
                               checksum(eigen_axpy_output));

    auto manual_axpy = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("manual_axpy", manual_axpy);
    const auto manual_axpy_ns = ksj::benchmarks::measure(config, [&] {
      for (std::size_t index = 0; index < vector.size(); ++index) {
        manual_axpy(index) = static_cast<T>(2) * vector(index) + vector_rhs(index);
      }
      ksj::benchmarks::do_not_optimize(manual_axpy.data()[0]);
    });
    print_linalg_benchmark_row("axpy", "manual_output", type_name, size, config, manual_axpy_ns, checksum(manual_axpy));

    auto eigen_squared_norm = ksj::linalg::detail::eigen::squared_norm(vector);
    const auto eigen_squared_norm_ns = ksj::benchmarks::measure(config, [&] {
      eigen_squared_norm = ksj::linalg::detail::eigen::squared_norm(vector);
      ksj::benchmarks::do_not_optimize(eigen_squared_norm);
    });
    print_linalg_benchmark_row("squared_norm", "eigen", type_name, size, config, eigen_squared_norm_ns,
                               static_cast<double>(eigen_squared_norm));

    const auto sample_rows = size * 2U;
    if (size <= kMaxStatisticsSize) {
      auto samples = ksj::array::make_pooled_matrix<T>(sample_rows, size);
      auto covariance_output = ksj::array::make_pooled_matrix<T>(size, size);
      auto whitened_samples = ksj::array::make_pooled_matrix<T>(sample_rows, size);
      ksj::benchmarks::require_pooled_storage("samples", samples);
      ksj::benchmarks::require_pooled_storage("covariance_output", covariance_output);
      ksj::benchmarks::require_pooled_storage("whitened_samples", whitened_samples);
      fill_samples(samples);
      ksj::linalg::detail::eigen::covariance(samples, covariance_output, true);

      const auto covariance_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::detail::eigen::covariance(samples, covariance_output, true);
        ksj::benchmarks::do_not_optimize(covariance_output.data()[0]);
      });
      print_linalg_benchmark_row("covariance", "eigen_loop_output", type_name, size, config, covariance_output_ns,
                                 checksum(covariance_output));

      auto intel_covariance_output = ksj::array::make_pooled_matrix<T>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_covariance_output", intel_covariance_output);
      if (ksj::linalg::detail::intel::covariance_centered_product(samples, intel_covariance_output, true)) {
        const auto intel_covariance_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::covariance_centered_product(samples, intel_covariance_output, true);
          ksj::benchmarks::do_not_optimize(intel_covariance_output.data()[0]);
        });
        print_linalg_benchmark_row("covariance", "intel_centered_gemm", type_name, size, config, intel_covariance_ns,
                                   checksum(intel_covariance_output));
      }

      auto public_covariance_output = ksj::array::make_pooled_matrix<T>(size, size);
      ksj::benchmarks::require_pooled_storage("public_covariance_output", public_covariance_output);
      const auto public_covariance_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::stats::covariance(samples, public_covariance_output);
        ksj::benchmarks::do_not_optimize(public_covariance_output.data()[0]);
      });
      print_linalg_benchmark_row("covariance", "public_output_policy", type_name, size, config,
                                 public_covariance_output_ns, checksum(public_covariance_output));

      auto covariance_public = ksj::stats::covariance(samples);
      const auto covariance_public_ns = ksj::benchmarks::measure(config, [&] {
        covariance_public = ksj::stats::covariance(samples);
        ksj::benchmarks::do_not_optimize(covariance_public.data()[0]);
      });
      print_linalg_benchmark_row("covariance", "public_api", type_name, size, config, covariance_public_ns,
                                 checksum(covariance_public));

      auto whitening_matrix =
        ksj::linalg::detail::eigen::whitening_matrix_from_covariance(covariance_output, static_cast<T>(1.0e-12));
      const auto whitening_ns = ksj::benchmarks::measure(config, [&] {
        whitening_matrix =
          ksj::linalg::detail::eigen::whitening_matrix_from_covariance(covariance_output, static_cast<T>(1.0e-12));
        ksj::benchmarks::do_not_optimize(whitening_matrix.data()[0]);
      });
      print_linalg_benchmark_row("whitening_matrix", "eigen_self_adjoint", type_name, size, config, whitening_ns,
                                 checksum(whitening_matrix));

      auto intel_whitening_matrix = ksj::array::make_pooled_matrix<T>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_whitening_matrix", intel_whitening_matrix);
      if (ksj::linalg::detail::intel::whitening_matrix_from_covariance(covariance_output, intel_whitening_matrix)) {
        const auto intel_whitening_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::whitening_matrix_from_covariance(covariance_output, intel_whitening_matrix);
          ksj::benchmarks::do_not_optimize(intel_whitening_matrix.data()[0]);
        });
        print_linalg_benchmark_row("whitening_matrix", "intel_lapacke", type_name, size, config, intel_whitening_ns,
                                   checksum(intel_whitening_matrix));
      }

      auto public_whitening_matrix = ksj::linalg::whitening_matrix_from_covariance(covariance_output);
      const auto public_whitening_ns = ksj::benchmarks::measure(config, [&] {
        public_whitening_matrix = ksj::linalg::whitening_matrix_from_covariance(covariance_output);
        ksj::benchmarks::do_not_optimize(public_whitening_matrix.data()[0]);
      });
      print_linalg_benchmark_row("whitening_matrix", "public_policy", type_name, size, config, public_whitening_ns,
                                 checksum(public_whitening_matrix));

      ksj::linalg::detail::eigen::whiten_samples(samples, whitening_matrix, whitened_samples);
      const auto whiten_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::detail::eigen::whiten_samples(samples, whitening_matrix, whitened_samples);
        ksj::benchmarks::do_not_optimize(whitened_samples.data()[0]);
      });
      print_linalg_benchmark_row("whiten_samples", "eigen_output", type_name, size, config, whiten_output_ns,
                                 checksum(whitened_samples));

      auto intel_whitened_samples = ksj::array::make_pooled_matrix<T>(sample_rows, size);
      ksj::benchmarks::require_pooled_storage("intel_whitened_samples", intel_whitened_samples);
      if (ksj::linalg::detail::intel::whiten_samples(samples, whitening_matrix, intel_whitened_samples)) {
        const auto intel_whiten_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::whiten_samples(samples, whitening_matrix, intel_whitened_samples);
          ksj::benchmarks::do_not_optimize(intel_whitened_samples.data()[0]);
        });
        print_linalg_benchmark_row("whiten_samples", "intel_gemm_output", type_name, size, config, intel_whiten_ns,
                                   checksum(intel_whitened_samples));
      }

      auto public_whitened_samples = ksj::array::make_pooled_matrix<T>(sample_rows, size);
      ksj::benchmarks::require_pooled_storage("public_whitened_samples", public_whitened_samples);
      const auto public_whiten_output_ns = ksj::benchmarks::measure(config, [&] {
        ksj::linalg::whiten_samples(samples, whitening_matrix, public_whitened_samples);
        ksj::benchmarks::do_not_optimize(public_whitened_samples.data()[0]);
      });
      print_linalg_benchmark_row("whiten_samples", "public_output_policy", type_name, size, config,
                                 public_whiten_output_ns, checksum(public_whitened_samples));

      auto whiten_public = ksj::linalg::whiten_samples(samples, whitening_matrix);
      const auto whiten_public_ns = ksj::benchmarks::measure(config, [&] {
        whiten_public = ksj::linalg::whiten_samples(samples, whitening_matrix);
        ksj::benchmarks::do_not_optimize(whiten_public.data()[0]);
      });
      print_linalg_benchmark_row("whiten_samples", "public_api", type_name, size, config, whiten_public_ns,
                                 checksum(whiten_public));
    }

    if (size > kMaxDecompositionSize) {
      continue;
    }

    auto solve_matrix = ksj::array::make_pooled_matrix<T>(size, size);
    ksj::benchmarks::require_pooled_storage("solve_matrix", solve_matrix);
    fill_well_conditioned_matrix(solve_matrix);

    auto eigen_solve = ksj::linalg::detail::eigen::solve(solve_matrix, vector);
    const auto eigen_solve_ns = ksj::benchmarks::measure(config, [&] {
      eigen_solve = ksj::linalg::detail::eigen::solve(solve_matrix, vector);
      ksj::benchmarks::do_not_optimize(eigen_solve.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_lu", "eigen_lu", type_name, size, config, eigen_solve_ns,
                               checksum(eigen_solve));

    auto intel_solve = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("intel_solve", intel_solve);
    if (ksj::linalg::detail::intel::solve_lu(solve_matrix, vector, intel_solve)) {
      const auto intel_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_lu(solve_matrix, vector, intel_solve);
        ksj::benchmarks::do_not_optimize(intel_solve.data()[0]);
      });
      print_linalg_benchmark_row("solve_vector_lu", "intel_lapacke", type_name, size, config, intel_solve_ns,
                                 checksum(intel_solve));
    }

    auto public_solve = ksj::linalg::solve(solve_matrix, vector);
    const auto public_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_solve = ksj::linalg::solve(solve_matrix, vector);
      ksj::benchmarks::do_not_optimize(public_solve.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_lu", "public_policy", type_name, size, config, public_solve_ns,
                               checksum(public_solve));

    auto public_output_solve = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("public_output_solve", public_output_solve);
    ksj::linalg::solve(solve_matrix, vector, public_output_solve);
    const auto public_output_solve_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::solve(solve_matrix, vector, public_output_solve);
      ksj::benchmarks::do_not_optimize(public_output_solve.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_lu", "public_output", type_name, size, config, public_output_solve_ns,
                               checksum(public_output_solve));

    const auto rhs_cols = std::min(kMatrixRhsCols, size);
    auto solve_rhs_matrix = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("solve_rhs_matrix", solve_rhs_matrix);
    fill_matrix(solve_rhs_matrix);

    auto eigen_solve_matrix = ksj::linalg::detail::eigen::solve(solve_matrix, solve_rhs_matrix);
    const auto eigen_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      eigen_solve_matrix = ksj::linalg::detail::eigen::solve(solve_matrix, solve_rhs_matrix);
      ksj::benchmarks::do_not_optimize(eigen_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_lu", "eigen_lu", type_name, size, config, eigen_solve_matrix_ns,
                               checksum(eigen_solve_matrix));

    auto intel_solve_matrix = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("intel_solve_matrix", intel_solve_matrix);
    if (ksj::linalg::detail::intel::solve_lu(solve_matrix, solve_rhs_matrix, intel_solve_matrix)) {
      const auto intel_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_lu(solve_matrix, solve_rhs_matrix, intel_solve_matrix);
        ksj::benchmarks::do_not_optimize(intel_solve_matrix.data()[0]);
      });
      print_linalg_benchmark_row("solve_matrix_lu", "intel_lapacke", type_name, size, config, intel_solve_matrix_ns,
                                 checksum(intel_solve_matrix));
    }

    auto public_solve_matrix = ksj::linalg::solve(solve_matrix, solve_rhs_matrix);
    const auto public_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      public_solve_matrix = ksj::linalg::solve(solve_matrix, solve_rhs_matrix);
      ksj::benchmarks::do_not_optimize(public_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_lu", "public_policy", type_name, size, config, public_solve_matrix_ns,
                               checksum(public_solve_matrix));

    auto public_output_solve_matrix = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("public_output_solve_matrix", public_output_solve_matrix);
    ksj::linalg::solve(solve_matrix, solve_rhs_matrix, public_output_solve_matrix);
    const auto public_output_solve_matrix_ns = ksj::benchmarks::measure(config, [&] {
      ksj::linalg::solve(solve_matrix, solve_rhs_matrix, public_output_solve_matrix);
      ksj::benchmarks::do_not_optimize(public_output_solve_matrix.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_lu", "public_output", type_name, size, config,
                               public_output_solve_matrix_ns, checksum(public_output_solve_matrix));

    auto cholesky_lower = ksj::linalg::detail::eigen::cholesky_lower(solve_matrix);
    const auto cholesky_lower_ns = ksj::benchmarks::measure(config, [&] {
      cholesky_lower = ksj::linalg::detail::eigen::cholesky_lower(solve_matrix);
      ksj::benchmarks::do_not_optimize(cholesky_lower.data()[0]);
    });
    print_linalg_benchmark_row("cholesky_lower", "eigen_llt", type_name, size, config, cholesky_lower_ns,
                               checksum(cholesky_lower));

    auto intel_cholesky_lower = ksj::array::make_pooled_matrix<T>(size, size);
    ksj::benchmarks::require_pooled_storage("intel_cholesky_lower", intel_cholesky_lower);
    if (ksj::linalg::detail::intel::cholesky_lower(solve_matrix, intel_cholesky_lower)) {
      const auto intel_cholesky_lower_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::cholesky_lower(solve_matrix, intel_cholesky_lower);
        ksj::benchmarks::do_not_optimize(intel_cholesky_lower.data()[0]);
      });
      print_linalg_benchmark_row("cholesky_lower", "intel_lapacke", type_name, size, config, intel_cholesky_lower_ns,
                                 checksum(intel_cholesky_lower));
    }

    auto public_cholesky_lower = ksj::linalg::cholesky_lower(solve_matrix);
    const auto public_cholesky_lower_ns = ksj::benchmarks::measure(config, [&] {
      public_cholesky_lower = ksj::linalg::cholesky_lower(solve_matrix);
      ksj::benchmarks::do_not_optimize(public_cholesky_lower.data()[0]);
    });
    print_linalg_benchmark_row("cholesky_lower", "public_policy", type_name, size, config, public_cholesky_lower_ns,
                               checksum(public_cholesky_lower));

    auto cholesky_solution = ksj::linalg::detail::eigen::solve_cholesky(solve_matrix, vector);
    const auto cholesky_solve_ns = ksj::benchmarks::measure(config, [&] {
      cholesky_solution = ksj::linalg::detail::eigen::solve_cholesky(solve_matrix, vector);
      ksj::benchmarks::do_not_optimize(cholesky_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_cholesky", "eigen_llt", type_name, size, config, cholesky_solve_ns,
                               checksum(cholesky_solution));

    auto intel_cholesky_solution = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("intel_cholesky_solution", intel_cholesky_solution);
    if (ksj::linalg::detail::intel::solve_cholesky(solve_matrix, vector, intel_cholesky_solution)) {
      const auto intel_cholesky_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_cholesky(solve_matrix, vector, intel_cholesky_solution);
        ksj::benchmarks::do_not_optimize(intel_cholesky_solution.data()[0]);
      });
      print_linalg_benchmark_row("solve_vector_cholesky", "intel_lapacke", type_name, size, config,
                                 intel_cholesky_solve_ns, checksum(intel_cholesky_solution));
    }

    auto public_cholesky_solution = ksj::linalg::solve_cholesky(solve_matrix, vector);
    const auto public_cholesky_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_cholesky_solution = ksj::linalg::solve_cholesky(solve_matrix, vector);
      ksj::benchmarks::do_not_optimize(public_cholesky_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_cholesky", "public_policy", type_name, size, config,
                               public_cholesky_solve_ns, checksum(public_cholesky_solution));

    auto cholesky_matrix_solution = ksj::linalg::detail::eigen::solve_cholesky(solve_matrix, solve_rhs_matrix);
    const auto cholesky_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      cholesky_matrix_solution = ksj::linalg::detail::eigen::solve_cholesky(solve_matrix, solve_rhs_matrix);
      ksj::benchmarks::do_not_optimize(cholesky_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_cholesky", "eigen_llt", type_name, size, config, cholesky_matrix_solve_ns,
                               checksum(cholesky_matrix_solution));

    auto intel_cholesky_matrix_solution = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("intel_cholesky_matrix_solution", intel_cholesky_matrix_solution);
    if (ksj::linalg::detail::intel::solve_cholesky(solve_matrix, solve_rhs_matrix, intel_cholesky_matrix_solution)) {
      const auto intel_cholesky_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_cholesky(solve_matrix, solve_rhs_matrix,
                                                         intel_cholesky_matrix_solution);
        ksj::benchmarks::do_not_optimize(intel_cholesky_matrix_solution.data()[0]);
      });
      print_linalg_benchmark_row("solve_matrix_cholesky", "intel_lapacke", type_name, size, config,
                                 intel_cholesky_matrix_solve_ns, checksum(intel_cholesky_matrix_solution));
    }

    auto public_cholesky_matrix_solution = ksj::linalg::solve_cholesky(solve_matrix, solve_rhs_matrix);
    const auto public_cholesky_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_cholesky_matrix_solution = ksj::linalg::solve_cholesky(solve_matrix, solve_rhs_matrix);
      ksj::benchmarks::do_not_optimize(public_cholesky_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_cholesky", "public_policy", type_name, size, config,
                               public_cholesky_matrix_solve_ns, checksum(public_cholesky_matrix_solution));

    auto qr_matrix = ksj::array::make_pooled_matrix<T>(sample_rows, size);
    auto qr_rhs = ksj::array::make_pooled_vector<T>(sample_rows);
    ksj::benchmarks::require_pooled_storage("qr_matrix", qr_matrix);
    ksj::benchmarks::require_pooled_storage("qr_rhs", qr_rhs);
    fill_least_squares_matrix(qr_matrix);
    fill_vector(qr_rhs);
    auto qr_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs);
    const auto qr_solve_ns = ksj::benchmarks::measure(config, [&] {
      qr_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs);
      ksj::benchmarks::do_not_optimize(qr_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_qr", "eigen_col_piv_qr", type_name, size, config, qr_solve_ns,
                               checksum(qr_solution));

    auto intel_qr_solution = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("intel_qr_solution", intel_qr_solution);
    if (ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs, intel_qr_solution)) {
      const auto intel_qr_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs, intel_qr_solution);
        ksj::benchmarks::do_not_optimize(intel_qr_solution.data()[0]);
      });
      print_linalg_benchmark_row("solve_vector_qr", "intel_lapacke", type_name, size, config, intel_qr_solve_ns,
                                 checksum(intel_qr_solution));
    }

    auto public_qr_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs);
    const auto public_qr_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_qr_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs);
      ksj::benchmarks::do_not_optimize(public_qr_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_vector_qr", "public_policy", type_name, size, config, public_qr_solve_ns,
                               checksum(public_qr_solution));

    auto qr_rhs_matrix = ksj::array::make_pooled_matrix<T>(sample_rows, rhs_cols);
    ksj::benchmarks::require_pooled_storage("qr_rhs_matrix", qr_rhs_matrix);
    fill_matrix(qr_rhs_matrix);

    auto qr_matrix_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs_matrix);
    const auto qr_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      qr_matrix_solution = ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_rhs_matrix);
      ksj::benchmarks::do_not_optimize(qr_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_qr", "eigen_col_piv_qr", type_name, size, config, qr_matrix_solve_ns,
                               checksum(qr_matrix_solution));

    auto intel_qr_matrix_solution = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    ksj::benchmarks::require_pooled_storage("intel_qr_matrix_solution", intel_qr_matrix_solution);
    if (ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs_matrix, intel_qr_matrix_solution)) {
      const auto intel_qr_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::solve_qr(qr_matrix, qr_rhs_matrix, intel_qr_matrix_solution);
        ksj::benchmarks::do_not_optimize(intel_qr_matrix_solution.data()[0]);
      });
      print_linalg_benchmark_row("solve_matrix_qr", "intel_lapacke", type_name, size, config, intel_qr_matrix_solve_ns,
                                 checksum(intel_qr_matrix_solution));
    }

    auto public_qr_matrix_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs_matrix);
    const auto public_qr_matrix_solve_ns = ksj::benchmarks::measure(config, [&] {
      public_qr_matrix_solution = ksj::linalg::solve_qr(qr_matrix, qr_rhs_matrix);
      ksj::benchmarks::do_not_optimize(public_qr_matrix_solution.data()[0]);
    });
    print_linalg_benchmark_row("solve_matrix_qr", "public_policy", type_name, size, config, public_qr_matrix_solve_ns,
                               checksum(public_qr_matrix_solution));

    if (size <= kMaxLeastSquaresVariantSize) {
      auto least_squares_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs);
      const auto least_squares_qr_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs);
        ksj::benchmarks::do_not_optimize(least_squares_qr.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_vector", "public_qr", type_name, size, config, least_squares_qr_ns,
                                 checksum(least_squares_qr));

      auto least_squares_svd =
        ksj::linalg::detail::eigen::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_svd =
          ksj::linalg::detail::eigen::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_svd.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_vector", "eigen_jacobi_svd", type_name, size, config,
                                 least_squares_svd_ns, checksum(least_squares_svd));

      auto intel_least_squares_svd = ksj::array::make_pooled_vector<T>(size);
      ksj::benchmarks::require_pooled_storage("intel_least_squares_svd", intel_least_squares_svd);
      if (ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs, intel_least_squares_svd)) {
        const auto intel_least_squares_svd_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs, intel_least_squares_svd);
          ksj::benchmarks::do_not_optimize(intel_least_squares_svd.data()[0]);
        });
        print_linalg_benchmark_row("least_squares_vector", "intel_lapacke_gelss", type_name, size, config,
                                   intel_least_squares_svd_ns, checksum(intel_least_squares_svd));
      }

      auto least_squares_public_svd =
        ksj::linalg::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_public_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_public_svd =
          ksj::linalg::solve_least_squares(qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_public_svd.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_vector", "public_svd_policy", type_name, size, config,
                                 least_squares_public_svd_ns, checksum(least_squares_public_svd));

      auto least_squares_normal = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations);
      const auto least_squares_normal_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_normal = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations);
        ksj::benchmarks::do_not_optimize(least_squares_normal.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_vector", "normal_equations_ldlt", type_name, size, config,
                                 least_squares_normal_ns, checksum(least_squares_normal));

      auto least_squares_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
      const auto least_squares_normal_cholesky_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
        ksj::benchmarks::do_not_optimize(least_squares_normal_cholesky.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_vector", "normal_equations_llt", type_name, size, config,
                                 least_squares_normal_cholesky_ns, checksum(least_squares_normal_cholesky));

      auto least_squares_matrix_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix);
      const auto least_squares_matrix_qr_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_qr = ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_qr.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_matrix", "public_qr", type_name, size, config,
                                 least_squares_matrix_qr_ns, checksum(least_squares_matrix_qr));

      auto least_squares_matrix_svd =
        ksj::linalg::detail::eigen::solve_least_squares(qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_matrix_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_svd = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_svd.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_matrix", "eigen_jacobi_svd", type_name, size, config,
                                 least_squares_matrix_svd_ns, checksum(least_squares_matrix_svd));

      auto intel_least_squares_matrix_svd = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
      ksj::benchmarks::require_pooled_storage("intel_least_squares_matrix_svd", intel_least_squares_matrix_svd);
      if (ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs_matrix,
                                                              intel_least_squares_matrix_svd)) {
        const auto intel_least_squares_matrix_svd_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::solve_least_squares_svd(qr_matrix, qr_rhs_matrix,
                                                                    intel_least_squares_matrix_svd);
          ksj::benchmarks::do_not_optimize(intel_least_squares_matrix_svd.data()[0]);
        });
        print_linalg_benchmark_row("least_squares_matrix", "intel_lapacke_gelss", type_name, size, config,
                                   intel_least_squares_matrix_svd_ns, checksum(intel_least_squares_matrix_svd));
      }

      auto least_squares_matrix_public_svd =
        ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
      const auto least_squares_matrix_public_svd_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_public_svd =
          ksj::linalg::solve_least_squares(qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::svd);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_public_svd.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_matrix", "public_svd_policy", type_name, size, config,
                                 least_squares_matrix_public_svd_ns, checksum(least_squares_matrix_public_svd));

      auto least_squares_matrix_normal = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations);
      const auto least_squares_matrix_normal_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_normal = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_normal.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_matrix", "normal_equations_ldlt", type_name, size, config,
                                 least_squares_matrix_normal_ns, checksum(least_squares_matrix_normal));

      auto least_squares_matrix_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
        qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
      const auto least_squares_matrix_normal_cholesky_ns = ksj::benchmarks::measure(config, [&] {
        least_squares_matrix_normal_cholesky = ksj::linalg::detail::eigen::solve_least_squares(
          qr_matrix, qr_rhs_matrix, ksj::linalg::LeastSquaresSolver::normal_equations_cholesky);
        ksj::benchmarks::do_not_optimize(least_squares_matrix_normal_cholesky.data()[0]);
      });
      print_linalg_benchmark_row("least_squares_matrix", "normal_equations_llt", type_name, size, config,
                                 least_squares_matrix_normal_cholesky_ns,
                                 checksum(least_squares_matrix_normal_cholesky));

      run_rank_deficient_least_squares_benchmarks<T>("rank_deficient_least_squares", type_name, size, sample_rows,
                                                     rhs_cols, config);
    }

    auto singular_values = ksj::linalg::detail::eigen::singular_values(const_matrix_view(matrix));
    const auto singular_values_ns = ksj::benchmarks::measure(config, [&] {
      singular_values = ksj::linalg::detail::eigen::singular_values(const_matrix_view(matrix));
      ksj::benchmarks::do_not_optimize(singular_values.data()[0]);
    });
    print_linalg_benchmark_row("singular_values", "eigen_jacobi_svd", type_name, size, config, singular_values_ns,
                               checksum(singular_values));

    auto intel_singular_values = ksj::array::make_pooled_vector<T>(size);
    ksj::benchmarks::require_pooled_storage("intel_singular_values", intel_singular_values);
    if (ksj::linalg::detail::intel::singular_values(matrix, intel_singular_values)) {
      const auto intel_singular_values_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::singular_values(matrix, intel_singular_values);
        ksj::benchmarks::do_not_optimize(intel_singular_values.data()[0]);
      });
      print_linalg_benchmark_row("singular_values", "intel_lapacke", type_name, size, config, intel_singular_values_ns,
                                 checksum(intel_singular_values));
    }

    auto public_singular_values = ksj::linalg::singular_values(matrix);
    const auto public_singular_values_ns = ksj::benchmarks::measure(config, [&] {
      public_singular_values = ksj::linalg::singular_values(matrix);
      ksj::benchmarks::do_not_optimize(public_singular_values.data()[0]);
    });
    print_linalg_benchmark_row("singular_values", "public_policy", type_name, size, config, public_singular_values_ns,
                               checksum(public_singular_values));

    if (size <= kMaxFullSvdSize) {
      auto full_svd = ksj::linalg::detail::eigen::svd(const_matrix_view(matrix), true);
      const auto full_svd_ns = ksj::benchmarks::measure(config, [&] {
        full_svd = ksj::linalg::detail::eigen::svd(const_matrix_view(matrix), true);
        ksj::benchmarks::do_not_optimize(full_svd.u.data()[0]);
        ksj::benchmarks::do_not_optimize(full_svd.v_adjoint.data()[0]);
      });
      print_linalg_benchmark_row("svd_full_uv", "eigen_jacobi_svd", type_name, size, config, full_svd_ns,
                                 checksum(full_svd.singular_values));

      auto intel_svd_u = ksj::array::make_pooled_matrix<T>(size, size);
      auto intel_svd_values = ksj::array::make_pooled_vector<T>(size);
      auto intel_svd_v_adjoint = ksj::array::make_pooled_matrix<T>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_svd_u", intel_svd_u);
      ksj::benchmarks::require_pooled_storage("intel_svd_values", intel_svd_values);
      ksj::benchmarks::require_pooled_storage("intel_svd_v_adjoint", intel_svd_v_adjoint);
      if (ksj::linalg::detail::intel::svd(matrix, intel_svd_u, intel_svd_values, intel_svd_v_adjoint, true)) {
        const auto intel_full_svd_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::svd(matrix, intel_svd_u, intel_svd_values, intel_svd_v_adjoint, true);
          ksj::benchmarks::do_not_optimize(intel_svd_u.data()[0]);
          ksj::benchmarks::do_not_optimize(intel_svd_v_adjoint.data()[0]);
        });
        print_linalg_benchmark_row("svd_full_uv", "intel_lapacke", type_name, size, config, intel_full_svd_ns,
                                   checksum(intel_svd_values));
      }

      auto public_full_svd = ksj::linalg::full_svd(matrix);
      const auto public_full_svd_ns = ksj::benchmarks::measure(config, [&] {
        public_full_svd = ksj::linalg::full_svd(matrix);
        ksj::benchmarks::do_not_optimize(public_full_svd.u.data()[0]);
        ksj::benchmarks::do_not_optimize(public_full_svd.v_adjoint.data()[0]);
      });
      print_linalg_benchmark_row("svd_full_uv", "public_policy", type_name, size, config, public_full_svd_ns,
                                 checksum(public_full_svd.singular_values));

      auto workspace_svd_u = ksj::array::make_pooled_matrix<T>(size, size);
      auto workspace_svd_values = ksj::array::make_pooled_vector<T>(size);
      auto workspace_svd_v_adjoint = ksj::array::make_pooled_matrix<T>(size, size);
      ksj::linalg::SvdWorkspace<T> svd_workspace;
      ksj::benchmarks::require_pooled_storage("workspace_svd_u", workspace_svd_u);
      ksj::benchmarks::require_pooled_storage("workspace_svd_values", workspace_svd_values);
      ksj::benchmarks::require_pooled_storage("workspace_svd_v_adjoint", workspace_svd_v_adjoint);
      (void)ksj::linalg::full_svd(matrix, workspace_svd_u, workspace_svd_values, workspace_svd_v_adjoint,
                                  svd_workspace);
      const auto workspace_full_svd_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::full_svd(matrix, workspace_svd_u, workspace_svd_values, workspace_svd_v_adjoint,
                                    svd_workspace);
        ksj::benchmarks::do_not_optimize(workspace_svd_u.data()[0]);
        ksj::benchmarks::do_not_optimize(workspace_svd_v_adjoint.data()[0]);
      });
      print_linalg_benchmark_row("svd_full_uv", "public_workspace", type_name, size, config, workspace_full_svd_ns,
                                 checksum(workspace_svd_values));
    }

    auto self_adjoint = ksj::linalg::detail::eigen::self_adjoint_eigen_decomposition(const_matrix_view(solve_matrix));
    const auto self_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      self_adjoint = ksj::linalg::detail::eigen::self_adjoint_eigen_decomposition(const_matrix_view(solve_matrix));
      ksj::benchmarks::do_not_optimize(self_adjoint.eigenvalues.data()[0]);
      ksj::benchmarks::do_not_optimize(self_adjoint.eigenvectors.data()[0]);
    });
    print_linalg_benchmark_row("self_adjoint_eigen", "eigen_solver", type_name, size, config, self_adjoint_ns,
                               checksum(self_adjoint.eigenvalues));

    auto intel_self_adjoint_values = ksj::array::make_pooled_vector<T>(size);
    auto intel_self_adjoint_vectors = ksj::array::make_pooled_matrix<T>(size, size);
    ksj::benchmarks::require_pooled_storage("intel_self_adjoint_values", intel_self_adjoint_values);
    ksj::benchmarks::require_pooled_storage("intel_self_adjoint_vectors", intel_self_adjoint_vectors);
    if (ksj::linalg::detail::intel::self_adjoint_eigen_decomposition(solve_matrix, intel_self_adjoint_values,
                                                                     intel_self_adjoint_vectors)) {
      const auto intel_self_adjoint_ns = ksj::benchmarks::measure(config, [&] {
        (void)ksj::linalg::detail::intel::self_adjoint_eigen_decomposition(solve_matrix, intel_self_adjoint_values,
                                                                           intel_self_adjoint_vectors);
        ksj::benchmarks::do_not_optimize(intel_self_adjoint_values.data()[0]);
        ksj::benchmarks::do_not_optimize(intel_self_adjoint_vectors.data()[0]);
      });
      print_linalg_benchmark_row("self_adjoint_eigen", "intel_lapacke", type_name, size, config, intel_self_adjoint_ns,
                                 checksum(intel_self_adjoint_values));
    }

    auto public_self_adjoint = ksj::linalg::self_adjoint_eigen_decomposition(solve_matrix);
    const auto public_self_adjoint_ns = ksj::benchmarks::measure(config, [&] {
      public_self_adjoint = ksj::linalg::self_adjoint_eigen_decomposition(solve_matrix);
      ksj::benchmarks::do_not_optimize(public_self_adjoint.eigenvalues.data()[0]);
      ksj::benchmarks::do_not_optimize(public_self_adjoint.eigenvectors.data()[0]);
    });
    print_linalg_benchmark_row("self_adjoint_eigen", "public_policy", type_name, size, config, public_self_adjoint_ns,
                               checksum(public_self_adjoint.eigenvalues));

    if (size <= kMaxGeneralEigenSize) {
      auto general_eigen = ksj::linalg::detail::eigen::eigen_decomposition(matrix);
      const auto general_eigen_ns = ksj::benchmarks::measure(config, [&] {
        general_eigen = ksj::linalg::detail::eigen::eigen_decomposition(matrix);
        ksj::benchmarks::do_not_optimize(general_eigen.eigenvalues.data()[0]);
        ksj::benchmarks::do_not_optimize(general_eigen.eigenvectors.data()[0]);
      });
      print_linalg_benchmark_row("general_eigen", "eigen_solver", type_name, size, config, general_eigen_ns,
                                 static_cast<double>(as_eigen(general_eigen.eigenvalues).real().sum()));

      auto intel_general_values = ksj::array::make_pooled_vector<std::complex<T>>(size);
      auto intel_general_vectors = ksj::array::make_pooled_matrix<std::complex<T>>(size, size);
      ksj::benchmarks::require_pooled_storage("intel_general_values", intel_general_values);
      ksj::benchmarks::require_pooled_storage("intel_general_vectors", intel_general_vectors);
      if (ksj::linalg::detail::intel::eigen_decomposition(matrix, intel_general_values, intel_general_vectors)) {
        const auto intel_general_eigen_ns = ksj::benchmarks::measure(config, [&] {
          (void)ksj::linalg::detail::intel::eigen_decomposition(matrix, intel_general_values, intel_general_vectors);
          ksj::benchmarks::do_not_optimize(intel_general_values.data()[0]);
          ksj::benchmarks::do_not_optimize(intel_general_vectors.data()[0]);
        });
        print_linalg_benchmark_row("general_eigen", "intel_lapacke", type_name, size, config, intel_general_eigen_ns,
                                   static_cast<double>(as_eigen(intel_general_values).real().sum()));
      }

      auto public_general_eigen = ksj::linalg::eigen_decomposition(matrix);
      const auto public_general_eigen_ns = ksj::benchmarks::measure(config, [&] {
        public_general_eigen = ksj::linalg::eigen_decomposition(matrix);
        ksj::benchmarks::do_not_optimize(public_general_eigen.eigenvalues.data()[0]);
        ksj::benchmarks::do_not_optimize(public_general_eigen.eigenvectors.data()[0]);
      });
      print_linalg_benchmark_row("general_eigen", "public_policy", type_name, size, config, public_general_eigen_ns,
                                 static_cast<double>(as_eigen(public_general_eigen.eigenvalues).real().sum()));
    }
  }
}

} // namespace

void run_real_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_for_type<float>("float", config);
}

void run_real_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_for_type<double>("double", config);
}

} // namespace ksj::benchmarks::linalg_benchmarks
