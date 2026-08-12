#include "benchmark_common.hpp"
#include "kspacejet/sparse/sparse.hpp"
#include "kspacejet/sparse/detail/eigen/eigen_sparse_operations.hpp"
#include "kspacejet/sparse/detail/intel/intel_sparse_operations.hpp"
#include "kspacejet/sparse/detail/sparse_policy.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

template <typename T> [[nodiscard]] ksj::sparse::CsrMatrix<T> make_diagonal_matrix(const std::size_t size) {
  auto row_offsets = ksj::array::make_pooled_vector<std::size_t>(size + 1U);
  auto column_indices = ksj::array::make_pooled_vector<std::size_t>(size);
  auto values = ksj::array::make_pooled_vector<T>(size);
  for (std::size_t row = 0; row < size; ++row) {
    row_offsets(row) = row;
    column_indices(row) = row;
    values(row) = static_cast<T>(2.0 + static_cast<double>(row % 7U) * 0.125);
  }
  row_offsets(size) = size;
  return {size, size, ksj::array::as_const_view(row_offsets.view()), ksj::array::as_const_view(column_indices.view()),
          ksj::array::as_const_view(values.view())};
}

template <typename T> [[nodiscard]] ksj::sparse::CsrMatrix<T> make_tridiagonal_matrix(const std::size_t size) {
  const auto nonzeros = size == 0U ? 0U : size * 3U - 2U;
  auto row_offsets = ksj::array::make_pooled_vector<std::size_t>(size + 1U);
  auto column_indices = ksj::array::make_pooled_vector<std::size_t>(nonzeros);
  auto values = ksj::array::make_pooled_vector<T>(nonzeros);

  std::size_t value_index = 0;
  row_offsets(0) = 0;
  for (std::size_t row = 0; row < size; ++row) {
    if (row > 0U) {
      column_indices(value_index) = row - 1U;
      values(value_index) = T{-1};
      ++value_index;
    }
    column_indices(value_index) = row;
    values(value_index) = T{2};
    ++value_index;
    if (row + 1U < size) {
      column_indices(value_index) = row + 1U;
      values(value_index) = T{-1};
      ++value_index;
    }
    row_offsets(row + 1U) = value_index;
  }
  return {size, size, ksj::array::as_const_view(row_offsets.view()), ksj::array::as_const_view(column_indices.view()),
          ksj::array::as_const_view(values.view())};
}

template <typename T> [[nodiscard]] ksj::sparse::CsrMatrix<T> make_lower_bidiagonal_matrix(const std::size_t size) {
  const auto nonzeros = size == 0U ? 0U : size * 2U - 1U;
  auto row_offsets = ksj::array::make_pooled_vector<std::size_t>(size + 1U);
  auto column_indices = ksj::array::make_pooled_vector<std::size_t>(nonzeros);
  auto values = ksj::array::make_pooled_vector<T>(nonzeros);

  std::size_t value_index = 0U;
  row_offsets(0) = 0U;
  for (std::size_t row = 0U; row < size; ++row) {
    if (row > 0U) {
      column_indices(value_index) = row - 1U;
      values(value_index) = T{-0.25};
      ++value_index;
    }
    column_indices(value_index) = row;
    values(value_index) = T{2};
    ++value_index;
    row_offsets(row + 1U) = value_index;
  }
  return {size, size, ksj::array::as_const_view(row_offsets.view()), ksj::array::as_const_view(column_indices.view()),
          ksj::array::as_const_view(values.view())};
}

template <typename T> [[nodiscard]] double checksum_csr(const ksj::sparse::CsrMatrix<T>& matrix) {
  double checksum = static_cast<double>(matrix.rows() + matrix.cols() + matrix.nonzeros());
  for (std::size_t row = 0U; row < matrix.rows(); ++row) {
    const auto begin = static_cast<std::size_t>(matrix.row_offsets()(row));
    const auto end = static_cast<std::size_t>(matrix.row_offsets()(row + 1U));
    for (std::size_t index = begin; index < end; ++index) {
      checksum += static_cast<double>((row + 1U) * (static_cast<std::size_t>(matrix.column_indices()(index)) + 1U)) *
                  static_cast<double>(matrix.values()(index));
    }
  }
  return checksum;
}

template <typename T>
void benchmark_spmv(std::string_view case_name, const ksj::sparse::CsrMatrix<T>& matrix, std::string_view type_name,
                    const std::size_t size, const ksj::benchmarks::Config& config) {
  auto vector = ksj::array::make_pooled_vector<T>(matrix.cols());
  auto output = ksj::array::make_pooled_vector<T>(matrix.rows());
  ksj::benchmarks::fill_vector(vector);
  ksj::benchmarks::require_pooled_storage("vector", vector);
  ksj::benchmarks::require_pooled_storage("output", output);

  const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::detail::eigen::spmv(matrix, ksj::array::as_const_view(vector.view()), output.view());
  });
  const auto eigen_checksum = ksj::benchmarks::checksum(output);
  ksj::benchmarks::print_row(case_name, "eigen", type_name, size, config, eigen_ns, eigen_checksum,
                             ksj::benchmarks::reference_row(case_name, "output_reuse"));
  ksj::benchmarks::print_row(case_name, "eigen", type_name, size, config, eigen_ns, eigen_checksum,
                             ksj::benchmarks::reference_row(case_name, "warm_plan"));

  if (ksj::sparse::detail::intel::spmv(matrix, vector, output)) {
    const auto intel_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spmv(matrix, vector, output);
    });
    ksj::benchmarks::print_row(case_name, "intel_mkl_one_shot", type_name, size, config, intel_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::candidate_row(case_name, "output_reuse"));
  }

  const auto public_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spmv(matrix, vector, output);
  });
  ksj::benchmarks::print_row(
    case_name, "public_policy", type_name, size, config, public_ns, ksj::benchmarks::checksum(output),
    ksj::benchmarks::policy_row(
      case_name, "output_reuse",
      ksj::sparse::detail::prefer_intel_spmv<T>(matrix.rows(), matrix.nonzeros()) ? "intel_mkl_one_shot" : "eigen"));

  ksj::sparse::detail::intel::SparseHandle intel_handle;
  if (ksj::sparse::detail::intel::make_handle(matrix, intel_handle)) {
    const auto handle_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spmv(intel_handle, ksj::array::as_const_view(vector.view()), output.view());
    });
    ksj::benchmarks::print_row(case_name, "intel_mkl_handle", type_name, size, config, handle_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::candidate_row(case_name, "warm_plan"));
  }

  auto plan = ksj::sparse::make_csr_plan(matrix);
  const auto plan_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spmv(plan, vector, output);
  });
  ksj::benchmarks::print_row(
    case_name, "public_plan_policy", type_name, size, config, plan_ns, ksj::benchmarks::checksum(output),
    ksj::benchmarks::policy_row(case_name, "warm_plan",
                                ksj::sparse::detail::prefer_intel_planned_spmv<T>(matrix.rows(), matrix.nonzeros())
                                  ? "intel_mkl_handle"
                                  : "eigen"));
}

template <typename T>
void benchmark_spmm(const ksj::sparse::CsrMatrix<T>& matrix, std::string_view type_name, const std::size_t size,
                    const std::size_t dense_cols, const ksj::benchmarks::Config& config) {
  const auto case_name = "spmm_tridiagonal_" + std::to_string(dense_cols) + "cols";
  auto dense = ksj::array::make_pooled_matrix<T>(matrix.cols(), dense_cols);
  auto output = ksj::array::make_pooled_matrix<T>(matrix.rows(), dense_cols);
  ksj::benchmarks::fill_matrix(dense);
  ksj::benchmarks::require_pooled_storage("dense", dense);
  ksj::benchmarks::require_pooled_storage("dense_output", output);

  const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::detail::eigen::spmm(matrix, ksj::array::as_const_view(dense.view()), output.view(),
                                     ksj::sparse::SparseOperation::none);
  });
  const auto eigen_checksum = ksj::benchmarks::checksum(output);
  ksj::benchmarks::print_row(case_name, "eigen", type_name, size, config, eigen_ns, eigen_checksum,
                             ksj::benchmarks::reference_row(case_name, "output_reuse"));
  ksj::benchmarks::print_row(case_name, "eigen", type_name, size, config, eigen_ns, eigen_checksum,
                             ksj::benchmarks::reference_row(case_name, "warm_plan"));

  if (ksj::sparse::detail::intel::spmm(matrix, ksj::array::as_const_view(dense.view()), output.view(),
                                       ksj::sparse::SparseOperation::none)) {
    const auto intel_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spmm(matrix, ksj::array::as_const_view(dense.view()), output.view(),
                                             ksj::sparse::SparseOperation::none);
    });
    ksj::benchmarks::print_row(case_name, "intel_mkl_one_shot", type_name, size, config, intel_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::candidate_row(case_name, "output_reuse"));
  }

  const auto public_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spmm(matrix, dense, output);
  });
  ksj::benchmarks::print_row(
    case_name, "public_policy", type_name, size, config, public_ns, ksj::benchmarks::checksum(output),
    ksj::benchmarks::policy_row(case_name, "output_reuse",
                                ksj::sparse::detail::prefer_intel_spmm<T>(matrix.rows(), matrix.nonzeros(), dense_cols)
                                  ? "intel_mkl_one_shot"
                                  : "eigen"));

  ksj::sparse::detail::intel::SparseHandle intel_handle;
  if (ksj::sparse::detail::intel::make_handle(matrix, intel_handle)) {
    const auto handle_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spmm(intel_handle, ksj::array::as_const_view(dense.view()), output.view(),
                                             ksj::sparse::SparseOperation::none);
    });
    ksj::benchmarks::print_row(case_name, "intel_mkl_handle", type_name, size, config, handle_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::candidate_row(case_name, "warm_plan"));
  }

  auto plan = ksj::sparse::make_csr_plan(matrix);
  const auto plan_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spmm(plan, dense, output);
  });
  ksj::benchmarks::print_row(case_name, "public_plan_policy", type_name, size, config, plan_ns,
                             ksj::benchmarks::checksum(output),
                             ksj::benchmarks::policy_row(case_name, "warm_plan",
                                                         ksj::sparse::detail::prefer_intel_planned_spmm<T>(
                                                           matrix.rows(), matrix.nonzeros(), dense_cols)
                                                           ? "intel_mkl_handle"
                                                           : "eigen"));
}

template <typename T>
void benchmark_transforms(const ksj::sparse::CsrMatrix<T>& matrix, std::string_view type_name, const std::size_t size,
                          const ksj::benchmarks::Config& config) {
  constexpr std::string_view convert_case = "convert_csr_transpose";
  double eigen_convert_checksum = 0.0;
  const auto eigen_convert_ns = ksj::benchmarks::measure(config, [&] {
    const auto output = ksj::sparse::detail::eigen::convert_csr(matrix, ksj::sparse::SparseOperation::transpose);
    eigen_convert_checksum = checksum_csr(output);
    ksj::benchmarks::do_not_optimize(eigen_convert_checksum);
  });
  ksj::benchmarks::print_row(convert_case, "eigen", type_name, size, config, eigen_convert_ns, eigen_convert_checksum,
                             ksj::benchmarks::reference_row(convert_case, "allocating"));

  ksj::sparse::CsrMatrix<T> intel_convert_output;
  if (ksj::sparse::detail::intel::convert_csr(matrix, intel_convert_output, ksj::sparse::SparseOperation::transpose)) {
    double intel_convert_checksum = 0.0;
    const auto intel_convert_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::convert_csr(matrix, intel_convert_output,
                                                    ksj::sparse::SparseOperation::transpose);
      intel_convert_checksum = checksum_csr(intel_convert_output);
      ksj::benchmarks::do_not_optimize(intel_convert_checksum);
    });
    ksj::benchmarks::print_row(convert_case, "intel_mkl", type_name, size, config, intel_convert_ns,
                               intel_convert_checksum, ksj::benchmarks::candidate_row(convert_case, "allocating"));
  }

  double public_convert_checksum = 0.0;
  const auto public_convert_ns = ksj::benchmarks::measure(config, [&] {
    const auto output = ksj::sparse::convert_csr(matrix, ksj::sparse::SparseOperation::transpose);
    public_convert_checksum = checksum_csr(output);
    ksj::benchmarks::do_not_optimize(public_convert_checksum);
  });
  ksj::benchmarks::print_row(convert_case, "public_policy", type_name, size, config, public_convert_ns,
                             public_convert_checksum, ksj::benchmarks::policy_row(convert_case, "allocating", "eigen"));

  constexpr std::string_view add_case = "add_csr";
  double eigen_add_checksum = 0.0;
  const auto eigen_add_ns = ksj::benchmarks::measure(config, [&] {
    const auto output = ksj::sparse::detail::eigen::add(matrix, T{0.5}, matrix, ksj::sparse::SparseOperation::none);
    eigen_add_checksum = checksum_csr(output);
    ksj::benchmarks::do_not_optimize(eigen_add_checksum);
  });
  ksj::benchmarks::print_row(add_case, "eigen", type_name, size, config, eigen_add_ns, eigen_add_checksum,
                             ksj::benchmarks::reference_row(add_case, "allocating"));

  ksj::sparse::CsrMatrix<T> intel_add_output;
  if (ksj::sparse::detail::intel::add(matrix, T{0.5}, matrix, intel_add_output, ksj::sparse::SparseOperation::none)) {
    double intel_add_checksum = 0.0;
    const auto intel_add_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::add(matrix, T{0.5}, matrix, intel_add_output,
                                            ksj::sparse::SparseOperation::none);
      intel_add_checksum = checksum_csr(intel_add_output);
      ksj::benchmarks::do_not_optimize(intel_add_checksum);
    });
    ksj::benchmarks::print_row(add_case, "intel_mkl", type_name, size, config, intel_add_ns, intel_add_checksum,
                               ksj::benchmarks::candidate_row(add_case, "allocating"));
  }

  double public_add_checksum = 0.0;
  const auto public_add_ns = ksj::benchmarks::measure(config, [&] {
    const auto output = ksj::sparse::add(matrix, T{0.5}, matrix);
    public_add_checksum = checksum_csr(output);
    ksj::benchmarks::do_not_optimize(public_add_checksum);
  });
  ksj::benchmarks::print_row(add_case, "public_policy", type_name, size, config, public_add_ns, public_add_checksum,
                             ksj::benchmarks::policy_row(add_case, "allocating", "eigen"));
}

template <typename T>
void benchmark_triangular_solves(const ksj::sparse::CsrMatrix<T>& matrix, std::string_view type_name,
                                 const std::size_t size, const ksj::benchmarks::Config& config) {
  constexpr auto triangle = ksj::sparse::SparseTriangle::lower;
  constexpr auto diagonal = ksj::sparse::SparseDiagonal::non_unit;
  constexpr auto operation = ksj::sparse::SparseOperation::none;
  constexpr std::string_view vector_case = "spsv_lower";

  auto rhs = ksj::array::make_pooled_vector<T>(size);
  auto output = ksj::array::make_pooled_vector<T>(size);
  ksj::benchmarks::fill_vector(rhs);
  const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::detail::eigen::spsv(matrix, ksj::array::as_const_view(rhs.view()), output.view(), triangle, diagonal,
                                     operation);
  });
  const auto eigen_checksum = ksj::benchmarks::checksum(output);
  ksj::benchmarks::print_row(vector_case, "eigen", type_name, size, config, eigen_ns, eigen_checksum,
                             ksj::benchmarks::reference_row(vector_case, "output_reuse"));
  ksj::benchmarks::print_row(vector_case, "eigen", type_name, size, config, eigen_ns, eigen_checksum,
                             ksj::benchmarks::reference_row(vector_case, "warm_plan"));

  if (ksj::sparse::detail::intel::spsv(matrix, ksj::array::as_const_view(rhs.view()), output.view(), triangle, diagonal,
                                       operation)) {
    const auto intel_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spsv(matrix, ksj::array::as_const_view(rhs.view()), output.view(), triangle,
                                             diagonal, operation);
    });
    ksj::benchmarks::print_row(vector_case, "intel_mkl_one_shot", type_name, size, config, intel_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::candidate_row(vector_case, "output_reuse"));
  }

  const auto public_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spsv(matrix, rhs, output, triangle, diagonal);
  });
  ksj::benchmarks::print_row(vector_case, "public_policy", type_name, size, config, public_ns,
                             ksj::benchmarks::checksum(output),
                             ksj::benchmarks::policy_row(vector_case, "output_reuse", "eigen"));

  ksj::sparse::detail::intel::SparseHandle intel_handle;
  if (ksj::sparse::detail::intel::make_handle(matrix, intel_handle)) {
    const auto handle_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spsv(intel_handle, ksj::array::as_const_view(rhs.view()), output.view(),
                                             triangle, diagonal, operation);
    });
    ksj::benchmarks::print_row(vector_case, "intel_mkl_handle", type_name, size, config, handle_ns,
                               ksj::benchmarks::checksum(output),
                               ksj::benchmarks::candidate_row(vector_case, "warm_plan"));
  }

  auto plan = ksj::sparse::make_csr_plan(matrix);
  const auto plan_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spsv(plan, rhs, output, triangle, diagonal);
  });
  ksj::benchmarks::print_row(vector_case, "public_plan_policy", type_name, size, config, plan_ns,
                             ksj::benchmarks::checksum(output),
                             ksj::benchmarks::policy_row(vector_case, "warm_plan", "eigen"));

  constexpr std::size_t dense_cols = 8U;
  constexpr std::string_view matrix_case = "spsm_lower_8cols";
  auto dense_rhs = ksj::array::make_pooled_matrix<T>(size, dense_cols);
  auto dense_output = ksj::array::make_pooled_matrix<T>(size, dense_cols);
  ksj::benchmarks::fill_matrix(dense_rhs);

  const auto eigen_spsm_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::detail::eigen::spsm(matrix, ksj::array::as_const_view(dense_rhs.view()), dense_output.view(), triangle,
                                     diagonal, operation);
  });
  const auto eigen_spsm_checksum = ksj::benchmarks::checksum(dense_output);
  ksj::benchmarks::print_row(matrix_case, "eigen", type_name, size, config, eigen_spsm_ns, eigen_spsm_checksum,
                             ksj::benchmarks::reference_row(matrix_case, "output_reuse"));
  ksj::benchmarks::print_row(matrix_case, "eigen", type_name, size, config, eigen_spsm_ns, eigen_spsm_checksum,
                             ksj::benchmarks::reference_row(matrix_case, "warm_plan"));

  if (ksj::sparse::detail::intel::spsm(matrix, ksj::array::as_const_view(dense_rhs.view()), dense_output.view(),
                                       triangle, diagonal, operation)) {
    const auto intel_spsm_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spsm(matrix, ksj::array::as_const_view(dense_rhs.view()), dense_output.view(),
                                             triangle, diagonal, operation);
    });
    ksj::benchmarks::print_row(matrix_case, "intel_mkl_one_shot", type_name, size, config, intel_spsm_ns,
                               ksj::benchmarks::checksum(dense_output),
                               ksj::benchmarks::candidate_row(matrix_case, "output_reuse"));
  }

  const auto public_spsm_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spsm(matrix, dense_rhs, dense_output, triangle, diagonal);
  });
  ksj::benchmarks::print_row(
    matrix_case, "public_policy", type_name, size, config, public_spsm_ns, ksj::benchmarks::checksum(dense_output),
    ksj::benchmarks::policy_row(
      matrix_case, "output_reuse",
      ksj::sparse::detail::prefer_intel_sparse_triangular_matrix_solve<T>(matrix.rows(), matrix.nonzeros(), dense_cols)
        ? "intel_mkl_one_shot"
        : "eigen"));

  if (intel_handle) {
    const auto handle_spsm_ns = ksj::benchmarks::measure(config, [&] {
      (void)ksj::sparse::detail::intel::spsm(intel_handle, ksj::array::as_const_view(dense_rhs.view()),
                                             dense_output.view(), triangle, diagonal, operation);
    });
    ksj::benchmarks::print_row(matrix_case, "intel_mkl_handle", type_name, size, config, handle_spsm_ns,
                               ksj::benchmarks::checksum(dense_output),
                               ksj::benchmarks::candidate_row(matrix_case, "warm_plan"));
  }

  const auto plan_spsm_ns = ksj::benchmarks::measure(config, [&] {
    ksj::sparse::spsm(plan, dense_rhs, dense_output, triangle, diagonal);
  });
  ksj::benchmarks::print_row(
    matrix_case, "public_plan_policy", type_name, size, config, plan_spsm_ns, ksj::benchmarks::checksum(dense_output),
    ksj::benchmarks::policy_row(
      matrix_case, "warm_plan",
      ksj::sparse::detail::prefer_intel_planned_triangular_matrix_solve<T>(matrix.rows(), matrix.nonzeros(), dense_cols)
        ? "intel_mkl_handle"
        : "eigen"));
}

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  constexpr std::array<std::size_t, 3> dense_column_counts{1U, 8U, 32U};
  for (const auto size : config.sizes) {
    const auto diagonal = make_diagonal_matrix<T>(size);
    const auto tridiagonal = make_tridiagonal_matrix<T>(size);
    const auto lower_bidiagonal = make_lower_bidiagonal_matrix<T>(size);

    benchmark_spmv("spmv_diagonal", diagonal, type_name, size, config);
    benchmark_spmv("spmv_tridiagonal", tridiagonal, type_name, size, config);
    for (const auto dense_cols : dense_column_counts) {
      benchmark_spmm(tridiagonal, type_name, size, dense_cols, config);
    }
    benchmark_transforms(tridiagonal, type_name, size, config);
    benchmark_triangular_solves(lower_bidiagonal, type_name, size, config);
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_sparse_backend_benchmark [--iterations N] [--trials N] "
                              "[--sizes 16,32,64] [--csv]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
