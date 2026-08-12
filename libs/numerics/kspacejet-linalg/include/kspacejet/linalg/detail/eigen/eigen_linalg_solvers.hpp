#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

namespace ksj::linalg::detail::eigen {

#define KSJ_LINALG_EIGEN_SOLVER_DECLS(T)                                                                               \
  T determinant(const ksj::array::PooledMatrix<T>& matrix);                                                            \
  void inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output);                               \
  bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                                \
               LuFactorWorkspace<T>& workspace);                                                                       \
  ksj::array::PooledMatrix<T> inverse(ksj::array::MatrixView<const T> input);                                          \
  void pseudo_inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                         \
                      ksj::array::real_scalar_t<T> singular_tolerance);                                                \
  ksj::array::PooledMatrix<T> pseudo_inverse(ksj::array::MatrixView<const T> input,                                    \
                                             ksj::array::real_scalar_t<T> singular_tolerance);                         \
  void solve(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,                        \
             ksj::array::PooledVector<T>& output);                                                                     \
  ksj::array::PooledVector<T> solve(const ksj::array::PooledMatrix<T>& matrix,                                         \
                                    const ksj::array::PooledVector<T>& rhs);                                           \
  void solve(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                        \
             ksj::array::PooledMatrix<T>& output);                                                                     \
  ksj::array::PooledMatrix<T> solve(const ksj::array::PooledMatrix<T>& matrix,                                         \
                                    const ksj::array::PooledMatrix<T>& rhs);                                           \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output);                                                                     \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace);                                     \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace);                                     \
  ksj::array::PooledMatrix<T> cholesky_lower(const ksj::array::PooledMatrix<T>& matrix);                               \
  ksj::array::PooledVector<T> solve_cholesky(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledVector<T>& rhs);                                  \
  ksj::array::PooledMatrix<T> solve_cholesky(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledMatrix<T>& rhs);                                  \
  ksj::array::PooledVector<T> solve_qr(const ksj::array::PooledMatrix<T>& matrix,                                      \
                                       const ksj::array::PooledVector<T>& rhs);                                        \
  ksj::array::PooledMatrix<T> solve_qr(const ksj::array::PooledMatrix<T>& matrix,                                      \
                                       const ksj::array::PooledMatrix<T>& rhs);                                        \
  bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LeastSquaresQrWorkspace<T>& workspace);                              \
  bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LeastSquaresQrWorkspace<T>& workspace);                              \
  bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,      \
                               ksj::array::PooledVector<T>& output);                                                   \
  bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,      \
                               ksj::array::PooledMatrix<T>& output);                                                   \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,            \
                               ksj::array::VectorView<T> output);                                                      \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,            \
                               ksj::array::MatrixView<T> output);                                                      \
  ksj::array::PooledVector<T> solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,                           \
                                                  const ksj::array::PooledVector<T>& rhs, LeastSquaresSolver solver);  \
  ksj::array::PooledMatrix<T> solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,                           \
                                                  const ksj::array::PooledMatrix<T>& rhs, LeastSquaresSolver solver);  \
  bool solve_least_squares(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,          \
                           ksj::array::PooledVector<T>& output, LeastSquaresSolver solver);                            \
  bool solve_least_squares(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,          \
                           ksj::array::PooledMatrix<T>& output, LeastSquaresSolver solver);                            \
  ksj::array::PooledVector<T> solve_small(const ksj::array::PooledMatrix<T>& matrix,                                   \
                                          const ksj::array::PooledVector<T>& rhs);

KSJ_LINALG_EIGEN_SOLVER_DECLS(float)
KSJ_LINALG_EIGEN_SOLVER_DECLS(double)
KSJ_LINALG_EIGEN_SOLVER_DECLS(ksj::base::cf32)
KSJ_LINALG_EIGEN_SOLVER_DECLS(ksj::base::cf64)

#undef KSJ_LINALG_EIGEN_SOLVER_DECLS

} // namespace ksj::linalg::detail::eigen
