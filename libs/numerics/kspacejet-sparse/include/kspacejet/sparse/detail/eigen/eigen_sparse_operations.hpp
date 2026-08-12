#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"

#include <type_traits>

namespace ksj::sparse {

enum class SparseDiagonal;
enum class SparseOperation;
enum class SparseTriangle;
template <typename T> class CsrMatrix;

} // namespace ksj::sparse

namespace ksj::sparse::detail::eigen {

template <typename T>
inline constexpr bool sparse_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double> ||
                                        std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;

template <typename T> inline constexpr bool unsupported_sparse_scalar_v = false;

void spmv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> vector,
          ksj::array::VectorView<float> output);
void spmv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> vector,
          ksj::array::VectorView<double> output);
void spmv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> vector,
          ksj::array::VectorView<ksj::base::cf32> output);
void spmv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> vector,
          ksj::array::VectorView<ksj::base::cf64> output);

template <typename T>
void spmv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> vector, ksj::array::VectorView<T> output) {
  if constexpr (sparse_scalar_v<T>) {
    spmv(matrix, vector, output);
  } else {
    (void)matrix;
    (void)vector;
    (void)output;
    static_assert(unsupported_sparse_scalar_v<T>, "ksj::sparse Eigen backend does not support this scalar type");
  }
}

void spmm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> dense,
          ksj::array::MatrixView<float> output, SparseOperation operation);
void spmm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> dense,
          ksj::array::MatrixView<double> output, SparseOperation operation);
void spmm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> dense,
          ksj::array::MatrixView<ksj::base::cf32> output, SparseOperation operation);
void spmm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> dense,
          ksj::array::MatrixView<ksj::base::cf64> output, SparseOperation operation);

template <typename T>
void spmm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> dense, ksj::array::MatrixView<T> output,
          const SparseOperation operation) {
  if constexpr (sparse_scalar_v<T>) {
    spmm(matrix, dense, output, operation);
  } else {
    (void)matrix;
    (void)dense;
    (void)output;
    (void)operation;
    static_assert(unsupported_sparse_scalar_v<T>, "ksj::sparse Eigen backend does not support this scalar type");
  }
}

CsrMatrix<float> convert_csr(const CsrMatrix<float>& matrix, SparseOperation operation);
CsrMatrix<double> convert_csr(const CsrMatrix<double>& matrix, SparseOperation operation);
CsrMatrix<ksj::base::cf32> convert_csr(const CsrMatrix<ksj::base::cf32>& matrix, SparseOperation operation);
CsrMatrix<ksj::base::cf64> convert_csr(const CsrMatrix<ksj::base::cf64>& matrix, SparseOperation operation);

template <typename T> [[nodiscard]] CsrMatrix<T> convert_csr(const CsrMatrix<T>& matrix, SparseOperation operation) {
  if constexpr (sparse_scalar_v<T>) {
    return convert_csr(matrix, operation);
  } else {
    (void)matrix;
    (void)operation;
    static_assert(unsupported_sparse_scalar_v<T>, "ksj::sparse Eigen backend does not support this scalar type");
  }
}

CsrMatrix<float> add(const CsrMatrix<float>& lhs, float alpha, const CsrMatrix<float>& rhs, SparseOperation operation);
CsrMatrix<double> add(const CsrMatrix<double>& lhs, double alpha, const CsrMatrix<double>& rhs,
                      SparseOperation operation);
CsrMatrix<ksj::base::cf32> add(const CsrMatrix<ksj::base::cf32>& lhs, ksj::base::cf32 alpha,
                               const CsrMatrix<ksj::base::cf32>& rhs, SparseOperation operation);
CsrMatrix<ksj::base::cf64> add(const CsrMatrix<ksj::base::cf64>& lhs, ksj::base::cf64 alpha,
                               const CsrMatrix<ksj::base::cf64>& rhs, SparseOperation operation);

template <typename T>
[[nodiscard]] CsrMatrix<T> add(const CsrMatrix<T>& lhs, const T& alpha, const CsrMatrix<T>& rhs,
                               SparseOperation operation) {
  if constexpr (sparse_scalar_v<T>) {
    return add(lhs, alpha, rhs, operation);
  } else {
    (void)lhs;
    (void)alpha;
    (void)rhs;
    (void)operation;
    static_assert(unsupported_sparse_scalar_v<T>, "ksj::sparse Eigen backend does not support this scalar type");
  }
}

void spsv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> rhs, ksj::array::VectorView<float> output,
          SparseTriangle triangle, SparseDiagonal diagonal, SparseOperation operation);
void spsv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> rhs,
          ksj::array::VectorView<double> output, SparseTriangle triangle, SparseDiagonal diagonal,
          SparseOperation operation);
void spsv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> rhs,
          ksj::array::VectorView<ksj::base::cf32> output, SparseTriangle triangle, SparseDiagonal diagonal,
          SparseOperation operation);
void spsv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> rhs,
          ksj::array::VectorView<ksj::base::cf64> output, SparseTriangle triangle, SparseDiagonal diagonal,
          SparseOperation operation);

template <typename T>
void spsv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> rhs, ksj::array::VectorView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  if constexpr (sparse_scalar_v<T>) {
    spsv(matrix, rhs, output, triangle, diagonal, operation);
  } else {
    (void)matrix;
    (void)rhs;
    (void)output;
    (void)triangle;
    (void)diagonal;
    (void)operation;
    static_assert(unsupported_sparse_scalar_v<T>, "ksj::sparse Eigen backend does not support this scalar type");
  }
}

void spsm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> rhs, ksj::array::MatrixView<float> output,
          SparseTriangle triangle, SparseDiagonal diagonal, SparseOperation operation);
void spsm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> rhs,
          ksj::array::MatrixView<double> output, SparseTriangle triangle, SparseDiagonal diagonal,
          SparseOperation operation);
void spsm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> rhs,
          ksj::array::MatrixView<ksj::base::cf32> output, SparseTriangle triangle, SparseDiagonal diagonal,
          SparseOperation operation);
void spsm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> rhs,
          ksj::array::MatrixView<ksj::base::cf64> output, SparseTriangle triangle, SparseDiagonal diagonal,
          SparseOperation operation);

template <typename T>
void spsm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> rhs, ksj::array::MatrixView<T> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  if constexpr (sparse_scalar_v<T>) {
    spsm(matrix, rhs, output, triangle, diagonal, operation);
  } else {
    (void)matrix;
    (void)rhs;
    (void)output;
    (void)triangle;
    (void)diagonal;
    (void)operation;
    static_assert(unsupported_sparse_scalar_v<T>, "ksj::sparse Eigen backend does not support this scalar type");
  }
}

} // namespace ksj::sparse::detail::eigen
