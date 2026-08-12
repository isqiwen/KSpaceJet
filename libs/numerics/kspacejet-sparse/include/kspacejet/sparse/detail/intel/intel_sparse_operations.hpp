#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"

#include <memory>
#include <type_traits>

namespace ksj::sparse {

enum class SparseDiagonal;
enum class SparseOperation;
enum class SparseTriangle;
template <typename T> class CsrMatrix;

} // namespace ksj::sparse

namespace ksj::sparse::detail::intel {

using SparseHandle = std::shared_ptr<void>;

template <typename T>
inline constexpr bool sparse_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double> ||
                                        std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;

[[nodiscard]] bool make_handle(const CsrMatrix<float>& matrix, SparseHandle& handle);
[[nodiscard]] bool make_handle(const CsrMatrix<double>& matrix, SparseHandle& handle);
[[nodiscard]] bool make_handle(const CsrMatrix<ksj::base::cf32>& matrix, SparseHandle& handle);
[[nodiscard]] bool make_handle(const CsrMatrix<ksj::base::cf64>& matrix, SparseHandle& handle);

template <typename T> [[nodiscard]] bool make_handle(const CsrMatrix<T>& matrix, SparseHandle& handle) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)matrix;
    (void)handle;
    return false;
  } else {
    return make_handle(matrix, handle);
  }
}

[[nodiscard]] bool spmv(const SparseHandle& handle, ksj::array::VectorView<const float> vector,
                        ksj::array::VectorView<float> output);
[[nodiscard]] bool spmv(const SparseHandle& handle, ksj::array::VectorView<const double> vector,
                        ksj::array::VectorView<double> output);
[[nodiscard]] bool spmv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf32> vector,
                        ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool spmv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf64> vector,
                        ksj::array::VectorView<ksj::base::cf64> output);

template <typename T>
[[nodiscard]] bool spmv(const SparseHandle& handle, ksj::array::VectorView<const T> vector,
                        ksj::array::VectorView<T> output) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)handle;
    (void)vector;
    (void)output;
    return false;
  } else {
    return spmv(handle, vector, output);
  }
}

[[nodiscard]] bool spmv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> vector,
                        ksj::array::VectorView<float> output);
[[nodiscard]] bool spmv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> vector,
                        ksj::array::VectorView<double> output);
[[nodiscard]] bool spmv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> vector,
                        ksj::array::VectorView<ksj::base::cf32> output);
[[nodiscard]] bool spmv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> vector,
                        ksj::array::VectorView<ksj::base::cf64> output);

template <typename T>
[[nodiscard]] bool spmv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> vector,
                        ksj::array::VectorView<T> output) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)matrix;
    (void)vector;
    (void)output;
    return false;
  } else {
    return spmv(matrix, vector, output);
  }
}

template <typename T>
[[nodiscard]] bool spmv(const CsrMatrix<T>& matrix, const ksj::array::PooledVector<T>& vector,
                        ksj::array::PooledVector<T>& output) {
  return spmv(matrix, ksj::array::as_const_view(vector.view()), output.view());
}

[[nodiscard]] bool spmm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> dense,
                        ksj::array::MatrixView<float> output, SparseOperation operation);
[[nodiscard]] bool spmm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> dense,
                        ksj::array::MatrixView<double> output, SparseOperation operation);
[[nodiscard]] bool spmm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> dense,
                        ksj::array::MatrixView<ksj::base::cf32> output, SparseOperation operation);
[[nodiscard]] bool spmm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> dense,
                        ksj::array::MatrixView<ksj::base::cf64> output, SparseOperation operation);

[[nodiscard]] bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const float> dense,
                        ksj::array::MatrixView<float> output, SparseOperation operation);
[[nodiscard]] bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const double> dense,
                        ksj::array::MatrixView<double> output, SparseOperation operation);
[[nodiscard]] bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf32> dense,
                        ksj::array::MatrixView<ksj::base::cf32> output, SparseOperation operation);
[[nodiscard]] bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf64> dense,
                        ksj::array::MatrixView<ksj::base::cf64> output, SparseOperation operation);

template <typename T>
[[nodiscard]] bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const T> dense,
                        ksj::array::MatrixView<T> output, const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)handle;
    (void)dense;
    (void)output;
    (void)operation;
    return false;
  } else {
    return spmm(handle, dense, output, operation);
  }
}

template <typename T>
[[nodiscard]] bool spmm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> dense,
                        ksj::array::MatrixView<T> output, const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)matrix;
    (void)dense;
    (void)output;
    (void)operation;
    return false;
  } else {
    return spmm(matrix, dense, output, operation);
  }
}

[[nodiscard]] bool convert_csr(const CsrMatrix<float>& matrix, CsrMatrix<float>& output, SparseOperation operation);
[[nodiscard]] bool convert_csr(const CsrMatrix<double>& matrix, CsrMatrix<double>& output, SparseOperation operation);
[[nodiscard]] bool convert_csr(const CsrMatrix<ksj::base::cf32>& matrix, CsrMatrix<ksj::base::cf32>& output,
                               SparseOperation operation);
[[nodiscard]] bool convert_csr(const CsrMatrix<ksj::base::cf64>& matrix, CsrMatrix<ksj::base::cf64>& output,
                               SparseOperation operation);

template <typename T>
[[nodiscard]] bool convert_csr(const CsrMatrix<T>& matrix, CsrMatrix<T>& output, const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)matrix;
    (void)output;
    (void)operation;
    return false;
  } else {
    return convert_csr(matrix, output, operation);
  }
}

[[nodiscard]] bool add(const CsrMatrix<float>& lhs, float alpha, const CsrMatrix<float>& rhs, CsrMatrix<float>& output,
                       SparseOperation operation);
[[nodiscard]] bool add(const CsrMatrix<double>& lhs, double alpha, const CsrMatrix<double>& rhs,
                       CsrMatrix<double>& output, SparseOperation operation);
[[nodiscard]] bool add(const CsrMatrix<ksj::base::cf32>& lhs, ksj::base::cf32 alpha,
                       const CsrMatrix<ksj::base::cf32>& rhs, CsrMatrix<ksj::base::cf32>& output,
                       SparseOperation operation);
[[nodiscard]] bool add(const CsrMatrix<ksj::base::cf64>& lhs, ksj::base::cf64 alpha,
                       const CsrMatrix<ksj::base::cf64>& rhs, CsrMatrix<ksj::base::cf64>& output,
                       SparseOperation operation);

template <typename T>
[[nodiscard]] bool add(const CsrMatrix<T>& lhs, const T& alpha, const CsrMatrix<T>& rhs, CsrMatrix<T>& output,
                       const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)lhs;
    (void)alpha;
    (void)rhs;
    (void)output;
    (void)operation;
    return false;
  } else {
    return add(lhs, alpha, rhs, output, operation);
  }
}

[[nodiscard]] bool spsv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> rhs,
                        ksj::array::VectorView<float> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> rhs,
                        ksj::array::VectorView<double> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> rhs,
                        ksj::array::VectorView<ksj::base::cf32> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);
[[nodiscard]] bool spsv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> rhs,
                        ksj::array::VectorView<ksj::base::cf64> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);

[[nodiscard]] bool spsv(const SparseHandle& handle, ksj::array::VectorView<const float> rhs,
                        ksj::array::VectorView<float> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsv(const SparseHandle& handle, ksj::array::VectorView<const double> rhs,
                        ksj::array::VectorView<double> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf32> rhs,
                        ksj::array::VectorView<ksj::base::cf32> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);
[[nodiscard]] bool spsv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf64> rhs,
                        ksj::array::VectorView<ksj::base::cf64> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);

template <typename T>
[[nodiscard]] bool spsv(const SparseHandle& handle, ksj::array::VectorView<const T> rhs,
                        ksj::array::VectorView<T> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
                        const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)handle;
    (void)rhs;
    (void)output;
    (void)triangle;
    (void)diagonal;
    (void)operation;
    return false;
  } else {
    return spsv(handle, rhs, output, triangle, diagonal, operation);
  }
}

template <typename T>
[[nodiscard]] bool spsv(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> rhs,
                        ksj::array::VectorView<T> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
                        const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)matrix;
    (void)rhs;
    (void)output;
    (void)triangle;
    (void)diagonal;
    (void)operation;
    return false;
  } else {
    return spsv(matrix, rhs, output, triangle, diagonal, operation);
  }
}

[[nodiscard]] bool spsm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> rhs,
                        ksj::array::MatrixView<float> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> rhs,
                        ksj::array::MatrixView<double> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> rhs,
                        ksj::array::MatrixView<ksj::base::cf32> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);
[[nodiscard]] bool spsm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> rhs,
                        ksj::array::MatrixView<ksj::base::cf64> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);

[[nodiscard]] bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const float> rhs,
                        ksj::array::MatrixView<float> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const double> rhs,
                        ksj::array::MatrixView<double> output, SparseTriangle triangle, SparseDiagonal diagonal,
                        SparseOperation operation);
[[nodiscard]] bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf32> rhs,
                        ksj::array::MatrixView<ksj::base::cf32> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);
[[nodiscard]] bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf64> rhs,
                        ksj::array::MatrixView<ksj::base::cf64> output, SparseTriangle triangle,
                        SparseDiagonal diagonal, SparseOperation operation);

template <typename T>
[[nodiscard]] bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const T> rhs,
                        ksj::array::MatrixView<T> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
                        const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)handle;
    (void)rhs;
    (void)output;
    (void)triangle;
    (void)diagonal;
    (void)operation;
    return false;
  } else {
    return spsm(handle, rhs, output, triangle, diagonal, operation);
  }
}

template <typename T>
[[nodiscard]] bool spsm(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> rhs,
                        ksj::array::MatrixView<T> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
                        const SparseOperation operation) {
  if constexpr (!sparse_scalar_v<T>) {
    (void)matrix;
    (void)rhs;
    (void)output;
    (void)triangle;
    (void)diagonal;
    (void)operation;
    return false;
  } else {
    return spsm(matrix, rhs, output, triangle, diagonal, operation);
  }
}

} // namespace ksj::sparse::detail::intel
