#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

namespace ksj::linalg::detail::eigen {

#define KSJ_LINALG_EIGEN_DECOMPOSITION_DECLS(T)                                                                        \
  ksj::array::PooledVector<ksj::array::real_scalar_t<T>> singular_values(ksj::array::MatrixView<const T> matrix);      \
  SingularValueDecomposition<T> svd(ksj::array::MatrixView<const T> matrix, bool full_matrices);                       \
  void left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output);                \
  ksj::array::PooledMatrix<T> left_singular_vectors(ksj::array::MatrixView<const T> matrix);                           \
  void self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                        \
                                        ksj::array::VectorView<ksj::array::real_scalar_t<T>> eigenvalues,              \
                                        ksj::array::MatrixView<T> eigenvectors);                                       \
  SelfAdjointEigenDecomposition<T> self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix);           \
  EigenDecomposition<T> eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix);                                \
  bool eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                                     \
                           ksj::array::VectorView<typename GeneralEigenWorkspace<T>::complex_type> eigenvalues,        \
                           ksj::array::MatrixView<typename GeneralEigenWorkspace<T>::complex_type> eigenvectors,       \
                           GeneralEigenWorkspace<T>& workspace);

KSJ_LINALG_EIGEN_DECOMPOSITION_DECLS(float)
KSJ_LINALG_EIGEN_DECOMPOSITION_DECLS(double)
KSJ_LINALG_EIGEN_DECOMPOSITION_DECLS(ksj::base::cf32)
KSJ_LINALG_EIGEN_DECOMPOSITION_DECLS(ksj::base::cf64)

#undef KSJ_LINALG_EIGEN_DECOMPOSITION_DECLS

} // namespace ksj::linalg::detail::eigen
