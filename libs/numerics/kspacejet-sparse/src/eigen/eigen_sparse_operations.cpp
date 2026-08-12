#include "kspacejet/sparse/sparse.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/sparse/detail/eigen/eigen_sparse_operations.hpp"

#include <Eigen/Sparse>

namespace ksj::sparse::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;

template <typename T> using EigenCsrMap = Eigen::Map<const Eigen::SparseMatrix<T, Eigen::RowMajor, int>>;

template <typename T> using EigenCsr = Eigen::SparseMatrix<T, Eigen::RowMajor, int>;

template <typename T> [[nodiscard]] EigenCsrMap<T> map_csr(const CsrMatrix<T>& matrix) {
  return EigenCsrMap<T>(static_cast<Eigen::Index>(matrix.rows()), static_cast<Eigen::Index>(matrix.cols()),
                        static_cast<Eigen::Index>(matrix.nonzeros()), matrix.row_offsets().data(),
                        matrix.column_indices().data(), matrix.values().data());
}

template <typename T> [[nodiscard]] CsrMatrix<T> make_csr_from_eigen(EigenCsr<T> matrix) {
  matrix.makeCompressed();
  return CsrMatrix<T>(
    static_cast<std::size_t>(matrix.rows()), static_cast<std::size_t>(matrix.cols()),
    ksj::array::VectorView<const int>(matrix.outerIndexPtr(), static_cast<std::size_t>(matrix.outerSize()) + 1U),
    ksj::array::VectorView<const int>(matrix.innerIndexPtr(), static_cast<std::size_t>(matrix.nonZeros())),
    ksj::array::VectorView<const T>(matrix.valuePtr(), static_cast<std::size_t>(matrix.nonZeros())));
}

template <typename T>
[[nodiscard]] EigenCsr<T> operated_matrix(const CsrMatrix<T>& matrix, const SparseOperation operation) {
  EigenCsr<T> output;
  const auto mapped = map_csr(matrix);
  switch (operation) {
    case SparseOperation::transpose:
      output = mapped.transpose();
      break;
    case SparseOperation::conjugate_transpose:
      output = mapped.adjoint();
      break;
    case SparseOperation::none:
    default:
      output = mapped;
      break;
  }
  output.makeCompressed();
  return output;
}

template <typename InputView, typename OutputView, typename ResizeOutput, typename Assign>
void assign_with_alias_guard(InputView input, OutputView output, ResizeOutput&& resize_output, Assign&& assign) {
  if (ksj::array::detail::views_may_overlap(input, output)) {
    auto output_work = resize_output();
    auto output_eigen = as_eigen(output_work);
    assign(output_eigen);
    ksj::array::copy(output_work.view(), output);
    return;
  }

  auto output_eigen = as_eigen(output);
  assign(output_eigen);
}

[[nodiscard]] SparseTriangle effective_triangle(const SparseTriangle triangle,
                                                const SparseOperation operation) noexcept {
  if (operation == SparseOperation::none) {
    return triangle;
  }
  return triangle == SparseTriangle::lower ? SparseTriangle::upper : SparseTriangle::lower;
}

template <int TriangleMode, typename MatrixExpr, typename RhsExpr, typename OutputExpr>
void assign_triangular_solve(const MatrixExpr& matrix, const RhsExpr& rhs, OutputExpr& output,
                             const SparseDiagonal diagonal) {
  if (diagonal == SparseDiagonal::unit) {
    output = matrix.template triangularView<TriangleMode | Eigen::UnitDiag>().solve(rhs);
  } else {
    output = matrix.template triangularView<TriangleMode>().solve(rhs);
  }
}

template <typename T>
void spmv_impl(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> vector, ksj::array::VectorView<T> output) {
  const auto vector_eigen = as_eigen(vector);
  assign_with_alias_guard(
    vector, output,
    [&] {
      return ksj::array::make_pooled_vector<T>(output.size());
    },
    [&](auto& output_eigen) {
      output_eigen = map_csr(matrix) * vector_eigen;
    });
}

template <typename T>
void spmm_impl(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> dense, ksj::array::MatrixView<T> output,
               const SparseOperation operation) {
  const auto dense_eigen = as_eigen(dense);
  const auto eigen_matrix = map_csr(matrix);
  assign_with_alias_guard(
    dense, output,
    [&] {
      return ksj::array::make_pooled_matrix<T>(output.rows(), output.cols());
    },
    [&](auto& output_eigen) {
      switch (operation) {
        case SparseOperation::none:
          output_eigen = eigen_matrix * dense_eigen;
          break;
        case SparseOperation::transpose:
          output_eigen = eigen_matrix.transpose() * dense_eigen;
          break;
        case SparseOperation::conjugate_transpose:
          output_eigen = eigen_matrix.adjoint() * dense_eigen;
          break;
      }
    });
}

template <typename T>
[[nodiscard]] CsrMatrix<T> convert_csr_impl(const CsrMatrix<T>& matrix, const SparseOperation operation) {
  return make_csr_from_eigen(operated_matrix(matrix, operation));
}

template <typename T>
[[nodiscard]] CsrMatrix<T> add_impl(const CsrMatrix<T>& lhs, const T& alpha, const CsrMatrix<T>& rhs,
                                    const SparseOperation operation) {
  EigenCsr<T> output = alpha * operated_matrix(lhs, operation) + map_csr(rhs);
  return make_csr_from_eigen(std::move(output));
}

template <typename T>
void spsv_impl(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> rhs, ksj::array::VectorView<T> output,
               const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  const auto actual_triangle = effective_triangle(triangle, operation);
  const auto rhs_eigen = as_eigen(rhs);
  auto assign_solution = [&](const auto& solved_matrix) {
    assign_with_alias_guard(
      rhs, output,
      [&] {
        return ksj::array::make_pooled_vector<T>(output.size());
      },
      [&](auto& output_eigen) {
        if (actual_triangle == SparseTriangle::lower) {
          assign_triangular_solve<Eigen::Lower>(solved_matrix, rhs_eigen, output_eigen, diagonal);
        } else {
          assign_triangular_solve<Eigen::Upper>(solved_matrix, rhs_eigen, output_eigen, diagonal);
        }
      });
  };

  if (operation == SparseOperation::none) {
    assign_solution(map_csr(matrix));
  } else {
    const auto solved_matrix = operated_matrix(matrix, operation);
    assign_solution(solved_matrix);
  }
}

template <typename T>
void spsm_impl(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> rhs, ksj::array::MatrixView<T> output,
               const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  const auto actual_triangle = effective_triangle(triangle, operation);
  const auto rhs_eigen = as_eigen(rhs);
  auto assign_solution = [&](const auto& solved_matrix) {
    assign_with_alias_guard(
      rhs, output,
      [&] {
        return ksj::array::make_pooled_matrix<T>(output.rows(), output.cols());
      },
      [&](auto& output_eigen) {
        if (actual_triangle == SparseTriangle::lower) {
          assign_triangular_solve<Eigen::Lower>(solved_matrix, rhs_eigen, output_eigen, diagonal);
        } else {
          assign_triangular_solve<Eigen::Upper>(solved_matrix, rhs_eigen, output_eigen, diagonal);
        }
      });
  };

  if (operation == SparseOperation::none) {
    assign_solution(map_csr(matrix));
  } else {
    const auto solved_matrix = operated_matrix(matrix, operation);
    assign_solution(solved_matrix);
  }
}

} // namespace

void spmv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> vector,
          ksj::array::VectorView<float> output) {
  spmv_impl(matrix, vector, output);
}

void spmv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> vector,
          ksj::array::VectorView<double> output) {
  spmv_impl(matrix, vector, output);
}

void spmv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> vector,
          ksj::array::VectorView<ksj::base::cf32> output) {
  spmv_impl(matrix, vector, output);
}

void spmv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> vector,
          ksj::array::VectorView<ksj::base::cf64> output) {
  spmv_impl(matrix, vector, output);
}

void spmm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> dense,
          ksj::array::MatrixView<float> output, const SparseOperation operation) {
  spmm_impl(matrix, dense, output, operation);
}

void spmm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> dense,
          ksj::array::MatrixView<double> output, const SparseOperation operation) {
  spmm_impl(matrix, dense, output, operation);
}

void spmm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> dense,
          ksj::array::MatrixView<ksj::base::cf32> output, const SparseOperation operation) {
  spmm_impl(matrix, dense, output, operation);
}

void spmm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> dense,
          ksj::array::MatrixView<ksj::base::cf64> output, const SparseOperation operation) {
  spmm_impl(matrix, dense, output, operation);
}

CsrMatrix<float> convert_csr(const CsrMatrix<float>& matrix, const SparseOperation operation) {
  return convert_csr_impl(matrix, operation);
}

CsrMatrix<double> convert_csr(const CsrMatrix<double>& matrix, const SparseOperation operation) {
  return convert_csr_impl(matrix, operation);
}

CsrMatrix<ksj::base::cf32> convert_csr(const CsrMatrix<ksj::base::cf32>& matrix, const SparseOperation operation) {
  return convert_csr_impl(matrix, operation);
}

CsrMatrix<ksj::base::cf64> convert_csr(const CsrMatrix<ksj::base::cf64>& matrix, const SparseOperation operation) {
  return convert_csr_impl(matrix, operation);
}

CsrMatrix<float> add(const CsrMatrix<float>& lhs, const float alpha, const CsrMatrix<float>& rhs,
                     const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, operation);
}

CsrMatrix<double> add(const CsrMatrix<double>& lhs, const double alpha, const CsrMatrix<double>& rhs,
                      const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, operation);
}

CsrMatrix<ksj::base::cf32> add(const CsrMatrix<ksj::base::cf32>& lhs, const ksj::base::cf32 alpha,
                               const CsrMatrix<ksj::base::cf32>& rhs, const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, operation);
}

CsrMatrix<ksj::base::cf64> add(const CsrMatrix<ksj::base::cf64>& lhs, const ksj::base::cf64 alpha,
                               const CsrMatrix<ksj::base::cf64>& rhs, const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, operation);
}

void spsv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> rhs, ksj::array::VectorView<float> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> rhs,
          ksj::array::VectorView<double> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> rhs,
          ksj::array::VectorView<ksj::base::cf32> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> rhs,
          ksj::array::VectorView<ksj::base::cf64> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> rhs, ksj::array::MatrixView<float> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> rhs,
          ksj::array::MatrixView<double> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> rhs,
          ksj::array::MatrixView<ksj::base::cf32> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

void spsm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> rhs,
          ksj::array::MatrixView<ksj::base::cf64> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

} // namespace ksj::sparse::detail::eigen
