#pragma once

/// Sparse matrix-vector and matrix-matrix operations with explicit storage and shape requirements.

#include "kspacejet/array/array.hpp"

#include "kspacejet/sparse/csr_matrix.hpp"
#include "kspacejet/sparse/plan.hpp"
#include "kspacejet/sparse/detail/eigen/eigen_sparse_operations.hpp"
#include "kspacejet/sparse/detail/intel/intel_sparse_operations.hpp"
#include "kspacejet/sparse/detail/sparse_policy.hpp"

#include <stdexcept>

namespace ksj::sparse {

template <typename T>
void spmv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> vector, ksj::array::VectorView<T> output) {
  if (matrix.cols() != vector.size() || matrix.rows() != output.size()) {
    throw std::invalid_argument("spmv dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  if (detail::prefer_intel_spmv<T>(matrix.rows(), matrix.nonzeros()) && detail::intel::spmv(matrix, vector, output)) {
    return;
  }

  detail::eigen::spmv(matrix, vector, output);
}

template <typename T>
void spmv(const CsrMatrix<T>& matrix, ksj::array::VectorView<T> vector, ksj::array::VectorView<T> output) {
  spmv(matrix, ksj::array::as_const_view(vector), output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> spmv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> vector) {
  auto output = ksj::array::PooledVector<T>(matrix.rows());
  spmv(matrix, vector, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> spmv(const CsrMatrix<T>& matrix, ksj::array::VectorView<T> vector) {
  return spmv(matrix, ksj::array::as_const_view(vector));
}

template <typename T>
void spmv(const CsrMatrix<T>& matrix, const ksj::array::PooledVector<T>& vector, ksj::array::PooledVector<T>& output) {
  spmv(matrix, vector.view(), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> spmv(const CsrMatrix<T>& matrix, const ksj::array::PooledVector<T>& vector) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.rows());
  spmv(matrix, vector, output);
  return output;
}

/// Multiplies through a reusable CSR plan, excluding handle construction and optimization from each call.
template <typename T>
void spmv(const CsrPlan<T>& plan, ksj::array::VectorView<const T> vector, ksj::array::VectorView<T> output) {
  const auto& matrix = plan.matrix();
  if (matrix.cols() != vector.size() || matrix.rows() != output.size()) {
    throw std::invalid_argument("planned spmv dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  const auto& handle = detail::CsrPlanAccess<T>::intel_handle(plan);
  if (detail::prefer_intel_planned_spmv<T>(matrix.rows(), matrix.nonzeros()) &&
      detail::intel::spmv(handle, vector, output)) {
    return;
  }
  detail::eigen::spmv(matrix, vector, output);
}

template <typename T>
void spmv(const CsrPlan<T>& plan, ksj::array::VectorView<T> vector, ksj::array::VectorView<T> output) {
  spmv(plan, ksj::array::as_const_view(vector), output);
}

template <typename T>
void spmv(const CsrPlan<T>& plan, const ksj::array::PooledVector<T>& vector, ksj::array::PooledVector<T>& output) {
  spmv(plan, vector.view(), output.view());
}

template <typename T>
void spmm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> dense, ksj::array::MatrixView<T> output,
          const SparseOperation operation = SparseOperation::none) {
  const auto expected_dense_rows = operation == SparseOperation::none ? matrix.cols() : matrix.rows();
  const auto expected_output_rows = operation == SparseOperation::none ? matrix.rows() : matrix.cols();

  if (dense.rows() != expected_dense_rows || output.rows() != expected_output_rows || output.cols() != dense.cols()) {
    throw std::invalid_argument("spmm dimension mismatch");
  }

  if (output.empty()) {
    return;
  }
  if (detail::prefer_intel_spmm<T>(matrix.rows(), matrix.nonzeros(), dense.cols()) &&
      detail::intel::spmm(matrix, dense, output, operation)) {
    return;
  }

  detail::eigen::spmm(matrix, dense, output, operation);
}

template <typename T>
void spmm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<T> dense, ksj::array::MatrixView<T> output,
          const SparseOperation operation = SparseOperation::none) {
  spmm(matrix, ksj::array::as_const_view(dense), output, operation);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> spmm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> dense,
                                               const SparseOperation operation = SparseOperation::none) {
  const auto output_rows = operation == SparseOperation::none ? matrix.rows() : matrix.cols();
  auto output = ksj::array::PooledMatrix<T>(output_rows, dense.cols());
  spmm(matrix, dense, output.view(), operation);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> spmm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<T> dense,
                                               const SparseOperation operation = SparseOperation::none) {
  return spmm(matrix, ksj::array::as_const_view(dense), operation);
}

template <typename T>
void spmm(const CsrMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& dense, ksj::array::PooledMatrix<T>& output,
          const SparseOperation operation = SparseOperation::none) {
  spmm(matrix, dense.view(), output.view(), operation);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> spmm(const CsrMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& dense,
                                               const SparseOperation operation = SparseOperation::none) {
  const auto output_rows = operation == SparseOperation::none ? matrix.rows() : matrix.cols();
  auto output = ksj::array::make_pooled_matrix<T>(output_rows, dense.cols());
  spmm(matrix, dense, output, operation);
  return output;
}

/// Multiplies through a reusable CSR plan, excluding handle construction and optimization from each call.
template <typename T>
void spmm(const CsrPlan<T>& plan, ksj::array::MatrixView<const T> dense, ksj::array::MatrixView<T> output,
          const SparseOperation operation = SparseOperation::none) {
  const auto& matrix = plan.matrix();
  const auto expected_dense_rows = operation == SparseOperation::none ? matrix.cols() : matrix.rows();
  const auto expected_output_rows = operation == SparseOperation::none ? matrix.rows() : matrix.cols();
  if (dense.rows() != expected_dense_rows || output.rows() != expected_output_rows || output.cols() != dense.cols()) {
    throw std::invalid_argument("planned spmm dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  const auto& handle = detail::CsrPlanAccess<T>::intel_handle(plan);
  if (detail::prefer_intel_planned_spmm<T>(matrix.rows(), matrix.nonzeros(), dense.cols()) &&
      detail::intel::spmm(handle, dense, output, operation)) {
    return;
  }
  detail::eigen::spmm(matrix, dense, output, operation);
}

template <typename T>
void spmm(const CsrPlan<T>& plan, ksj::array::MatrixView<T> dense, ksj::array::MatrixView<T> output,
          const SparseOperation operation = SparseOperation::none) {
  spmm(plan, ksj::array::as_const_view(dense), output, operation);
}

template <typename T>
void spmm(const CsrPlan<T>& plan, const ksj::array::PooledMatrix<T>& dense, ksj::array::PooledMatrix<T>& output,
          const SparseOperation operation = SparseOperation::none) {
  spmm(plan, dense.view(), output.view(), operation);
}

[[nodiscard]] inline std::size_t operation_rows(const std::size_t rows, const std::size_t cols,
                                                const SparseOperation operation) noexcept {
  return operation == SparseOperation::none ? rows : cols;
}

[[nodiscard]] inline std::size_t operation_cols(const std::size_t rows, const std::size_t cols,
                                                const SparseOperation operation) noexcept {
  return operation == SparseOperation::none ? cols : rows;
}

template <typename T>
[[nodiscard]] CsrMatrix<T> convert_csr(const CsrMatrix<T>& matrix,
                                       const SparseOperation operation = SparseOperation::none) {
  auto output = CsrMatrix<T>{};
  if (detail::prefer_intel_sparse_transform<T>(matrix.nonzeros()) &&
      detail::intel::convert_csr(matrix, output, operation)) {
    return output;
  }
  return detail::eigen::convert_csr(matrix, operation);
}

template <typename T>
[[nodiscard]] CsrMatrix<T> add(const CsrMatrix<T>& lhs, const T& alpha, const CsrMatrix<T>& rhs,
                               const SparseOperation lhs_operation = SparseOperation::none) {
  if (operation_rows(lhs.rows(), lhs.cols(), lhs_operation) != rhs.rows() ||
      operation_cols(lhs.rows(), lhs.cols(), lhs_operation) != rhs.cols()) {
    throw std::invalid_argument("sparse add dimension mismatch");
  }

  auto output = CsrMatrix<T>{};
  if (detail::prefer_intel_sparse_transform<T>(lhs.nonzeros() + rhs.nonzeros()) &&
      detail::intel::add(lhs, alpha, rhs, output, lhs_operation)) {
    return output;
  }
  return detail::eigen::add(lhs, alpha, rhs, lhs_operation);
}

template <typename T>
void spsv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> rhs, ksj::array::VectorView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  const auto rows = operation_rows(matrix.rows(), matrix.cols(), operation);
  const auto cols = operation_cols(matrix.rows(), matrix.cols(), operation);
  if (rows != cols || rhs.size() != cols || output.size() != rows) {
    throw std::invalid_argument("spsv dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  if (detail::prefer_intel_sparse_triangular_vector_solve<T>(matrix.rows(), matrix.nonzeros()) &&
      detail::intel::spsv(matrix, rhs, output, triangle, diagonal, operation)) {
    return;
  }
  detail::eigen::spsv(matrix, rhs, output, triangle, diagonal, operation);
}

template <typename T>
void spsv(const CsrMatrix<T>& matrix, ksj::array::VectorView<T> rhs, ksj::array::VectorView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsv(matrix, ksj::array::as_const_view(rhs), output, triangle, diagonal, operation);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> spsv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> rhs,
                                               const SparseTriangle triangle, const SparseDiagonal diagonal,
                                               const SparseOperation operation = SparseOperation::none) {
  auto output = ksj::array::PooledVector<T>(operation_rows(matrix.rows(), matrix.cols(), operation));
  spsv(matrix, rhs, output.view(), triangle, diagonal, operation);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> spsv(const CsrMatrix<T>& matrix, ksj::array::VectorView<T> rhs,
                                               const SparseTriangle triangle, const SparseDiagonal diagonal,
                                               const SparseOperation operation = SparseOperation::none) {
  return spsv(matrix, ksj::array::as_const_view(rhs), triangle, diagonal, operation);
}

template <typename T>
void spsv(const CsrMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs, ksj::array::PooledVector<T>& output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsv(matrix, rhs.view(), output.view(), triangle, diagonal, operation);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> spsv(const CsrMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
                                               const SparseTriangle triangle, const SparseDiagonal diagonal,
                                               const SparseOperation operation = SparseOperation::none) {
  auto output = ksj::array::make_pooled_vector<T>(operation_rows(matrix.rows(), matrix.cols(), operation));
  spsv(matrix, rhs, output, triangle, diagonal, operation);
  return output;
}

/// Solves repeatedly with the same triangular CSR matrix and a reusable backend plan.
template <typename T>
void spsv(const CsrPlan<T>& plan, ksj::array::VectorView<const T> rhs, ksj::array::VectorView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  const auto& matrix = plan.matrix();
  const auto rows = operation_rows(matrix.rows(), matrix.cols(), operation);
  const auto cols = operation_cols(matrix.rows(), matrix.cols(), operation);
  if (rows != cols || rhs.size() != cols || output.size() != rows) {
    throw std::invalid_argument("planned spsv dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  const auto& handle = detail::CsrPlanAccess<T>::intel_handle(plan);
  if (detail::prefer_intel_planned_triangular_vector_solve<T>(matrix.rows(), matrix.nonzeros()) &&
      detail::intel::spsv(handle, rhs, output, triangle, diagonal, operation)) {
    return;
  }
  detail::eigen::spsv(matrix, rhs, output, triangle, diagonal, operation);
}

template <typename T>
void spsv(const CsrPlan<T>& plan, ksj::array::VectorView<T> rhs, ksj::array::VectorView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsv(plan, ksj::array::as_const_view(rhs), output, triangle, diagonal, operation);
}

template <typename T>
void spsv(const CsrPlan<T>& plan, const ksj::array::PooledVector<T>& rhs, ksj::array::PooledVector<T>& output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsv(plan, rhs.view(), output.view(), triangle, diagonal, operation);
}

template <typename T>
void spsm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> rhs, ksj::array::MatrixView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  const auto rows = operation_rows(matrix.rows(), matrix.cols(), operation);
  const auto cols = operation_cols(matrix.rows(), matrix.cols(), operation);
  if (rows != cols || rhs.rows() != cols || output.rows() != rows || output.cols() != rhs.cols()) {
    throw std::invalid_argument("spsm dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  if (detail::prefer_intel_sparse_triangular_matrix_solve<T>(matrix.rows(), matrix.nonzeros(), rhs.cols()) &&
      detail::intel::spsm(matrix, rhs, output, triangle, diagonal, operation)) {
    return;
  }
  detail::eigen::spsm(matrix, rhs, output, triangle, diagonal, operation);
}

template <typename T>
void spsm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<T> rhs, ksj::array::MatrixView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsm(matrix, ksj::array::as_const_view(rhs), output, triangle, diagonal, operation);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> spsm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> rhs,
                                               const SparseTriangle triangle, const SparseDiagonal diagonal,
                                               const SparseOperation operation = SparseOperation::none) {
  auto output = ksj::array::PooledMatrix<T>(operation_rows(matrix.rows(), matrix.cols(), operation), rhs.cols());
  spsm(matrix, rhs, output.view(), triangle, diagonal, operation);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> spsm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<T> rhs,
                                               const SparseTriangle triangle, const SparseDiagonal diagonal,
                                               const SparseOperation operation = SparseOperation::none) {
  return spsm(matrix, ksj::array::as_const_view(rhs), triangle, diagonal, operation);
}

template <typename T>
void spsm(const CsrMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs, ksj::array::PooledMatrix<T>& output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsm(matrix, rhs.view(), output.view(), triangle, diagonal, operation);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> spsm(const CsrMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,
                                               const SparseTriangle triangle, const SparseDiagonal diagonal,
                                               const SparseOperation operation = SparseOperation::none) {
  auto output = ksj::array::make_pooled_matrix<T>(operation_rows(matrix.rows(), matrix.cols(), operation), rhs.cols());
  spsm(matrix, rhs, output, triangle, diagonal, operation);
  return output;
}

/// Solves repeated dense right-hand sides with the same triangular CSR matrix and backend plan.
template <typename T>
void spsm(const CsrPlan<T>& plan, ksj::array::MatrixView<const T> rhs, ksj::array::MatrixView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  const auto& matrix = plan.matrix();
  const auto rows = operation_rows(matrix.rows(), matrix.cols(), operation);
  const auto cols = operation_cols(matrix.rows(), matrix.cols(), operation);
  if (rows != cols || rhs.rows() != cols || output.rows() != rows || output.cols() != rhs.cols()) {
    throw std::invalid_argument("planned spsm dimension mismatch");
  }
  if (output.empty()) {
    return;
  }
  const auto& handle = detail::CsrPlanAccess<T>::intel_handle(plan);
  if (detail::prefer_intel_planned_triangular_matrix_solve<T>(matrix.rows(), matrix.nonzeros(), rhs.cols()) &&
      detail::intel::spsm(handle, rhs, output, triangle, diagonal, operation)) {
    return;
  }
  detail::eigen::spsm(matrix, rhs, output, triangle, diagonal, operation);
}

template <typename T>
void spsm(const CsrPlan<T>& plan, ksj::array::MatrixView<T> rhs, ksj::array::MatrixView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsm(plan, ksj::array::as_const_view(rhs), output, triangle, diagonal, operation);
}

template <typename T>
void spsm(const CsrPlan<T>& plan, const ksj::array::PooledMatrix<T>& rhs, ksj::array::PooledMatrix<T>& output,
          const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation = SparseOperation::none) {
  spsm(plan, rhs.view(), output.view(), triangle, diagonal, operation);
}

} // namespace ksj::sparse
