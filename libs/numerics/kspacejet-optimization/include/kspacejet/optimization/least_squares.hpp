#pragma once

/// Linear and nonlinear least-squares problem definitions and solver entry points.

#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/solvers.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include "kspacejet/optimization/detail/eigen/eigen_optimization_least_squares.hpp"

#include <stdexcept>
#include <type_traits>

namespace ksj::optimization {

enum class LeastSquaresMethod {
  qr,
  svd,
  normal_equations,
};

template <typename T> struct LeastSquaresWorkspace {
  ksj::linalg::LeastSquaresQrWorkspace<T> qr;
  ksj::linalg::LeastSquaresSvdWorkspace<T> svd;
};

template <typename T>
void least_squares(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                   ksj::array::VectorView<T> output, LeastSquaresWorkspace<T>& workspace,
                   const LeastSquaresMethod method = LeastSquaresMethod::qr) {
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size()) {
    throw std::invalid_argument("least_squares dimension mismatch");
  }

  if (method == LeastSquaresMethod::qr) {
    if (!ksj::linalg::solve_qr(matrix, rhs, output, workspace.qr)) {
      throw std::runtime_error("least_squares qr failed");
    }
    return;
  }

  if (method == LeastSquaresMethod::svd) {
    if (!ksj::linalg::solve_least_squares_svd(matrix, rhs, output, workspace.svd)) {
      throw std::runtime_error("least_squares svd failed");
    }
    return;
  }

  detail::eigen::least_squares(matrix, rhs, output, method);
}

template <typename T>
void least_squares(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                   ksj::array::VectorView<T> output, const LeastSquaresMethod method = LeastSquaresMethod::qr) {
  LeastSquaresWorkspace<T> workspace;
  least_squares(matrix, rhs, output, workspace, method);
}

template <typename T>
void least_squares(ksj::array::MatrixView<T> matrix, ksj::array::VectorView<T> rhs, ksj::array::VectorView<T> output,
                   LeastSquaresWorkspace<T>& workspace, const LeastSquaresMethod method = LeastSquaresMethod::qr)
  requires(!std::is_const_v<T>)
{
  least_squares(ksj::array::as_const_view(matrix), ksj::array::as_const_view(rhs), output, workspace, method);
}

template <typename T>
void least_squares(ksj::array::MatrixView<T> matrix, ksj::array::VectorView<T> rhs, ksj::array::VectorView<T> output,
                   const LeastSquaresMethod method = LeastSquaresMethod::qr)
  requires(!std::is_const_v<T>)
{
  least_squares(ksj::array::as_const_view(matrix), ksj::array::as_const_view(rhs), output, method);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> least_squares(ksj::array::MatrixView<const T> matrix,
                                                        ksj::array::VectorView<const T> rhs,
                                                        const LeastSquaresMethod method = LeastSquaresMethod::qr) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  least_squares(matrix, rhs, output.view(), method);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> least_squares(ksj::array::MatrixView<T> matrix, ksj::array::VectorView<T> rhs,
                                                        const LeastSquaresMethod method = LeastSquaresMethod::qr)
  requires(!std::is_const_v<T>)
{
  return least_squares(ksj::array::as_const_view(matrix), ksj::array::as_const_view(rhs), method);
}

template <typename T>
void least_squares(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
                   ksj::array::PooledVector<T>& output, LeastSquaresWorkspace<T>& workspace,
                   const LeastSquaresMethod method = LeastSquaresMethod::qr) {
  least_squares(matrix.view(), rhs.view(), output.view(), workspace, method);
}

template <typename T>
void least_squares(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
                   ksj::array::PooledVector<T>& output, const LeastSquaresMethod method = LeastSquaresMethod::qr) {
  least_squares(matrix.view(), rhs.view(), output.view(), method);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> least_squares(const ksj::array::PooledMatrix<T>& matrix,
                                                        const ksj::array::PooledVector<T>& rhs,
                                                        const LeastSquaresMethod method = LeastSquaresMethod::qr) {
  return least_squares(matrix.view(), rhs.view(), method);
}

} // namespace ksj::optimization
