#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"

namespace ksj::linalg::detail::intel {

#define KSJ_LINALG_INTEL_BLAS_DECLS(T)                                                                                 \
  bool matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output);                                                                       \
  bool matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output, const T& alpha);                                                       \
  bool hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                         \
                      ksj::array::real_scalar_t<T> scale);                                                             \
  bool gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,                            \
            ksj::array::VectorView<T> output);                                                                         \
  bool dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs, T& output);                       \
  bool dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs, T& output);                      \
  bool copy(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output);                                  \
  bool scale(ksj::array::VectorView<const T> input, const T& scalar, ksj::array::VectorView<T> output);                \
  bool axpy(const T& alpha, ksj::array::VectorView<const T> x, ksj::array::VectorView<const T> y,                      \
            ksj::array::VectorView<T> output);

KSJ_LINALG_INTEL_BLAS_DECLS(float)
KSJ_LINALG_INTEL_BLAS_DECLS(double)
KSJ_LINALG_INTEL_BLAS_DECLS(ksj::base::cf32)
KSJ_LINALG_INTEL_BLAS_DECLS(ksj::base::cf64)

#undef KSJ_LINALG_INTEL_BLAS_DECLS

} // namespace ksj::linalg::detail::intel
