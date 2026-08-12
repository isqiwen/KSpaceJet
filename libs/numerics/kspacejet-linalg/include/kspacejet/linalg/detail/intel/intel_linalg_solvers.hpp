#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

namespace ksj::linalg::detail::intel {

#define KSJ_LINALG_INTEL_SOLVER_DECLS(T)                                                                               \
  bool solve_lu(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,                     \
                ksj::array::PooledVector<T>& output);                                                                  \
  bool solve_lu(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                     \
                ksj::array::PooledMatrix<T>& output);                                                                  \
  bool solve_refined(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                \
                     ksj::array::PooledMatrix<T>& output, ksj::array::real_scalar_t<T>& reciprocal_condition);         \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace);                                     \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace);                                     \
  bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output);                               \
  bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                                \
               LuFactorWorkspace<T>& workspace);                                                                       \
  bool cholesky_lower(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output);                 \
  bool solve_cholesky(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,               \
                      ksj::array::PooledVector<T>& output);                                                            \
  bool solve_cholesky(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,               \
                      ksj::array::PooledMatrix<T>& output);                                                            \
  bool solve_qr(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,                     \
                ksj::array::PooledVector<T>& output);                                                                  \
  bool solve_qr(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                     \
                ksj::array::PooledMatrix<T>& output);                                                                  \
  bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LeastSquaresQrWorkspace<T>& workspace);                              \
  bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LeastSquaresQrWorkspace<T>& workspace);                              \
  bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,      \
                               ksj::array::PooledVector<T>& output);                                                   \
  bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,      \
                               ksj::array::PooledMatrix<T>& output);                                                   \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,            \
                               ksj::array::VectorView<T> output, LeastSquaresSvdWorkspace<T>& workspace);              \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,            \
                               ksj::array::MatrixView<T> output, LeastSquaresSvdWorkspace<T>& workspace);              \
  bool solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledVector<T>& rhs,                                   \
                                             ksj::array::PooledVector<T>& output);                                     \
  bool solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledMatrix<T>& rhs,                                   \
                                             ksj::array::PooledMatrix<T>& output);

KSJ_LINALG_INTEL_SOLVER_DECLS(float)
KSJ_LINALG_INTEL_SOLVER_DECLS(double)
KSJ_LINALG_INTEL_SOLVER_DECLS(ksj::base::cf32)
KSJ_LINALG_INTEL_SOLVER_DECLS(ksj::base::cf64)

#undef KSJ_LINALG_INTEL_SOLVER_DECLS

} // namespace ksj::linalg::detail::intel
