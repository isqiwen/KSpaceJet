#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

namespace ksj::linalg::detail::intel {

#define KSJ_LINALG_INTEL_DECOMPOSITION_DECLS(T)                                                                        \
  bool can_svd_in_place(ksj::array::MatrixView<T> matrix);                                                             \
  bool svd_in_place(ksj::array::MatrixView<T> matrix, ksj::array::PooledMatrix<T>& u,                                  \
                    ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,                                    \
                    ksj::array::PooledMatrix<T>& v_adjoint, bool full_matrices);                                       \
  bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::PooledMatrix<T>& u,                                     \
           ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values, ksj::array::PooledMatrix<T>& v_adjoint,     \
           bool full_matrices);                                                                                        \
  bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,                                        \
           ksj::array::VectorView<ksj::array::real_scalar_t<T>> values, ksj::array::MatrixView<T> v_adjoint,           \
           SvdWorkspace<T>& workspace, bool full_matrices);                                                            \
  bool svd(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& u,                                  \
           ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values, ksj::array::PooledMatrix<T>& v_adjoint,     \
           bool full_matrices);                                                                                        \
  bool left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output);          \
  bool left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output,                 \
                             LeftSingularVectorsWorkspace<T>& workspace);                                              \
  bool left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output,           \
                             LeftSingularVectorsWorkspace<T>& workspace);                                              \
  bool self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                        \
                                        ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,                   \
                                        ksj::array::MatrixView<T> vectors);                                            \
  bool self_adjoint_eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix,                                     \
                                        ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,                \
                                        ksj::array::PooledMatrix<T>& vectors);                                         \
  bool eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix,                                                  \
                           ksj::array::PooledVector<typename EigenDecomposition<T>::complex_type>& values,             \
                           ksj::array::PooledMatrix<typename EigenDecomposition<T>::complex_type>& vectors);           \
  bool eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                                     \
                           ksj::array::VectorView<typename GeneralEigenWorkspace<T>::complex_type> values,             \
                           ksj::array::MatrixView<typename GeneralEigenWorkspace<T>::complex_type> vectors,            \
                           GeneralEigenWorkspace<T>& workspace);

KSJ_LINALG_INTEL_DECOMPOSITION_DECLS(float)
KSJ_LINALG_INTEL_DECOMPOSITION_DECLS(double)
KSJ_LINALG_INTEL_DECOMPOSITION_DECLS(ksj::base::cf32)
KSJ_LINALG_INTEL_DECOMPOSITION_DECLS(ksj::base::cf64)

bool singular_values(ksj::array::MatrixView<const float> matrix, ksj::array::PooledVector<float>& output);
bool singular_values(ksj::array::MatrixView<const double> matrix, ksj::array::PooledVector<double>& output);
bool singular_values(ksj::array::MatrixView<const ksj::base::cf32> matrix, ksj::array::PooledVector<float>& output);
bool singular_values(ksj::array::MatrixView<const ksj::base::cf64> matrix, ksj::array::PooledVector<double>& output);
bool singular_values(const ksj::array::PooledMatrix<float>& matrix, ksj::array::PooledVector<float>& output);
bool singular_values(const ksj::array::PooledMatrix<double>& matrix, ksj::array::PooledVector<double>& output);
bool singular_values(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix, ksj::array::PooledVector<float>& output);
bool singular_values(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix, ksj::array::PooledVector<double>& output);

#undef KSJ_LINALG_INTEL_DECOMPOSITION_DECLS

} // namespace ksj::linalg::detail::intel
