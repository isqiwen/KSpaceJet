#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"

namespace ksj::linalg::detail::eigen {

#define KSJ_LINALG_EIGEN_BLAS_DECLS(T)                                                                                 \
  void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output);                                                                       \
  void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output, const T& alpha);                                                       \
  void gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,                            \
            ksj::array::VectorView<T> output);                                                                         \
  T dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs);                                     \
  T dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs);                                    \
  ksj::array::real_scalar_t<T> squared_norm(const ksj::array::PooledVector<T>& input);                                 \
  ksj::array::real_scalar_t<T> norm_l2(const ksj::array::PooledVector<T>& input);                                      \
  void transpose(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output);                      \
  ksj::array::PooledMatrix<T> transpose(const ksj::array::PooledMatrix<T>& matrix);                                    \
  void hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                         \
                      ksj::array::real_scalar_t<T> scale);                                                             \
  ksj::array::real_scalar_t<T> diagonal_abs_sum(ksj::array::MatrixView<const T> matrix);                               \
  void add_to_diagonal(ksj::array::MatrixView<T> matrix, const T& value);

#define KSJ_LINALG_EIGEN_SCALE_DECLS(T, S)                                                                             \
  void scale(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output, const S& scalar);          \
  void scale(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output, const S& scalar);          \
  ksj::array::PooledVector<T> scale(const ksj::array::PooledVector<T>& input, const S& scalar);                        \
  ksj::array::PooledMatrix<T> scale(const ksj::array::PooledMatrix<T>& input, const S& scalar);                        \
  void axpy(const S& alpha, const ksj::array::PooledVector<T>& x, const ksj::array::PooledVector<T>& y,                \
            ksj::array::PooledVector<T>& output);                                                                      \
  ksj::array::PooledVector<T> axpy(const S& alpha, const ksj::array::PooledVector<T>& x,                               \
                                   const ksj::array::PooledVector<T>& y);

KSJ_LINALG_EIGEN_BLAS_DECLS(float)
KSJ_LINALG_EIGEN_BLAS_DECLS(double)
KSJ_LINALG_EIGEN_BLAS_DECLS(ksj::base::cf32)
KSJ_LINALG_EIGEN_BLAS_DECLS(ksj::base::cf64)

KSJ_LINALG_EIGEN_SCALE_DECLS(float, float)
KSJ_LINALG_EIGEN_SCALE_DECLS(double, double)
KSJ_LINALG_EIGEN_SCALE_DECLS(ksj::base::cf32, ksj::base::cf32)
KSJ_LINALG_EIGEN_SCALE_DECLS(ksj::base::cf32, float)
KSJ_LINALG_EIGEN_SCALE_DECLS(ksj::base::cf64, ksj::base::cf64)
KSJ_LINALG_EIGEN_SCALE_DECLS(ksj::base::cf64, double)

#undef KSJ_LINALG_EIGEN_BLAS_DECLS
#undef KSJ_LINALG_EIGEN_SCALE_DECLS

} // namespace ksj::linalg::detail::eigen
