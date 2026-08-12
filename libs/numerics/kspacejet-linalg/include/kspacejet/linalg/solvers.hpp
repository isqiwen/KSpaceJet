#pragma once

/// Dense linear-system and least-squares solver APIs with explicit coefficient and right-hand-side Views.

#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/detail/eigen/eigen_linalg_solvers.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_solvers.hpp"
#include "kspacejet/linalg/detail/linalg_types.hpp"
#include "kspacejet/linalg/detail/linalg_policy.hpp"
#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <stdexcept>

namespace ksj::linalg {

template <typename T> [[nodiscard]] T determinant(const Matrix<T>& matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("determinant requires a square matrix");
  }
  return detail::eigen::determinant(matrix);
}

template <typename T> void inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (input.rows() != input.cols()) {
    throw std::invalid_argument("inverse requires a square matrix");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("inverse output dimension mismatch");
  }

  if (detail::prefer_intel_inverse<T>(input.rows()) && detail::intel::inverse(input, output)) {
    return;
  }

  detail::eigen::inverse(input, output);
}

template <typename T> [[nodiscard]] Matrix<T> inverse(ksj::array::MatrixView<const T> input) {
  auto output = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
  inverse(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                           LuFactorWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (input.rows() != input.cols() || output.rows() != input.rows() || output.cols() != input.cols()) {
    return false;
  }

  if (detail::prefer_intel_inverse_workspace<T>(input.rows()) && detail::intel::inverse(input, output, workspace)) {
    return true;
  }
  return detail::eigen::inverse(input, output, workspace);
}

template <typename T> void inverse(const Matrix<T>& input, Matrix<T>& output) {
  inverse(ksj::array::as_const_view(input.view()), output.view());
}

template <typename T> [[nodiscard]] Matrix<T> inverse(const Matrix<T>& matrix) {
  return inverse(ksj::array::as_const_view(matrix.view()));
}

template <typename T>
void pseudo_inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                    const ksj::array::real_scalar_t<T> singular_tolerance = ksj::array::real_scalar_t<T>{}) {
  detail::require_supported_linalg_scalar<T>();
  if (input.empty()) {
    throw std::invalid_argument("pseudo_inverse input must not be empty");
  }
  if (output.rows() != input.cols() || output.cols() != input.rows()) {
    throw std::invalid_argument("pseudo_inverse output dimension mismatch");
  }

  detail::eigen::pseudo_inverse(input, output, singular_tolerance);
}

template <typename T>
[[nodiscard]] Matrix<T>
pseudo_inverse(ksj::array::MatrixView<const T> input,
               const ksj::array::real_scalar_t<T> singular_tolerance = ksj::array::real_scalar_t<T>{}) {
  return detail::eigen::pseudo_inverse(input, singular_tolerance);
}

template <typename T>
void pseudo_inverse(const Matrix<T>& input, Matrix<T>& output,
                    const ksj::array::real_scalar_t<T> singular_tolerance = ksj::array::real_scalar_t<T>{}) {
  pseudo_inverse(ksj::array::as_const_view(input.view()), output.view(), singular_tolerance);
}

template <typename T>
[[nodiscard]] Matrix<T>
pseudo_inverse(const Matrix<T>& matrix,
               const ksj::array::real_scalar_t<T> singular_tolerance = ksj::array::real_scalar_t<T>{}) {
  return pseudo_inverse(ksj::array::as_const_view(matrix.view()), singular_tolerance);
}

template <typename T> void solve(const Matrix<T>& matrix, const Vector<T>& rhs, Vector<T>& output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve dimension mismatch");
  }
  if (output.size() != matrix.cols()) {
    throw std::invalid_argument("solve output dimension mismatch");
  }

  LuSolveWorkspace<T> workspace;
  if (detail::prefer_intel_solve_lu<T>(matrix.rows()) &&
      detail::intel::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                              output.view(), workspace)) {
    return;
  }

  detail::eigen::solve(matrix, rhs, output);
}

template <typename T> void solve(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve dimension mismatch");
  }
  if (output.rows() != matrix.cols() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("solve output dimension mismatch");
  }

  LuSolveWorkspace<T> workspace;
  if (detail::prefer_intel_solve_lu_matrix<T>(matrix.rows(), rhs.cols()) &&
      detail::intel::solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                              output.view(), workspace)) {
    return;
  }

  detail::eigen::solve(matrix, rhs, output);
}

template <typename T> [[nodiscard]] Vector<T> solve(const Matrix<T>& matrix, const Vector<T>& rhs) {
  auto output = ksj::array::make_pooled_vector<T>(rhs.size());
  solve(matrix, rhs, output);
  return output;
}

template <typename T> [[nodiscard]] Matrix<T> solve(const Matrix<T>& matrix, const Matrix<T>& rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  solve(matrix, rhs, output);
  return output;
}

template <typename T>
[[nodiscard]] bool try_solve_refined(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output,
                                     ksj::array::real_scalar_t<T>& reciprocal_condition) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_refined dimension mismatch");
  }
  if (output.rows() != matrix.cols() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("solve_refined output dimension mismatch");
  }

  return detail::intel::solve_refined(matrix, rhs, output, reciprocal_condition);
}

template <typename T> void solve_refined(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output) {
  ksj::array::real_scalar_t<T> reciprocal_condition{};
  if (!try_solve_refined(matrix, rhs, output, reciprocal_condition)) {
    // Refinement is part of this API's contract.  Do not silently downgrade to
    // an unrefined LU solve when LAPACK reports a singular/ill-conditioned
    // system or cannot run the requested operation.
    throw std::runtime_error("solve_refined failed");
  }
}

template <typename T> [[nodiscard]] Matrix<T> solve_refined(const Matrix<T>& matrix, const Matrix<T>& rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  solve_refined(matrix, rhs, output);
  return output;
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != matrix.cols()) {
    return false;
  }

  if (detail::prefer_intel_solve_lu_workspace<T>(matrix.rows()) &&
      detail::intel::solve_lu(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_lu(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != matrix.cols()) {
    return false;
  }

  LuSolveWorkspace<T> workspace;
  if (detail::prefer_intel_solve_lu<T>(matrix.rows()) && detail::intel::solve_lu(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_lu(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() || output.rows() != matrix.cols() ||
      output.cols() != rhs.cols()) {
    return false;
  }

  if (detail::prefer_intel_solve_lu_matrix_workspace<T>(matrix.rows(), rhs.cols()) &&
      detail::intel::solve_lu(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_lu(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() || output.rows() != matrix.cols() ||
      output.cols() != rhs.cols()) {
    return false;
  }

  LuSolveWorkspace<T> workspace;
  if (detail::prefer_intel_solve_lu_matrix<T>(matrix.rows(), rhs.cols()) &&
      detail::intel::solve_lu(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_lu(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] Vector<T> solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  if (!solve_lu(matrix, rhs, output.view())) {
    throw std::runtime_error("solve_lu failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] Matrix<T> solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  if (!solve_lu(matrix, rhs, output.view())) {
    throw std::runtime_error("solve_lu failed");
  }
  return output;
}

template <typename T> [[nodiscard]] bool solve_lu(const Matrix<T>& matrix, const Vector<T>& rhs, Vector<T>& output) {
  return solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view());
}

template <typename T>
[[nodiscard]] bool solve_lu(const Matrix<T>& matrix, const Vector<T>& rhs, Vector<T>& output,
                            LuSolveWorkspace<T>& workspace) {
  return solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                  workspace);
}

template <typename T> [[nodiscard]] Vector<T> solve_lu(const Matrix<T>& matrix, const Vector<T>& rhs) {
  return solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T>
[[nodiscard]] bool solve_lu(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output,
                            LuSolveWorkspace<T>& workspace) {
  return solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                  workspace);
}

template <typename T> [[nodiscard]] bool solve_lu(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output) {
  return solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view());
}

template <typename T> [[nodiscard]] Matrix<T> solve_lu(const Matrix<T>& matrix, const Matrix<T>& rhs) {
  return solve_lu(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T> [[nodiscard]] Matrix<T> cholesky_lower(const Matrix<T>& matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("cholesky requires a square matrix");
  }

  auto output = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
  if (detail::prefer_intel_cholesky_lower<T>(matrix.rows()) && detail::intel::cholesky_lower(matrix, output)) {
    return output;
  }

  return detail::eigen::cholesky_lower(matrix);
}

template <typename T> [[nodiscard]] Vector<T> solve_cholesky(const Matrix<T>& matrix, const Vector<T>& rhs) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_cholesky dimension mismatch");
  }

  auto output = ksj::array::make_pooled_vector<T>(rhs.size());
  if (detail::prefer_intel_solve_cholesky<T>(matrix.rows()) && detail::intel::solve_cholesky(matrix, rhs, output)) {
    return output;
  }

  return detail::eigen::solve_cholesky(matrix, rhs);
}

template <typename T> [[nodiscard]] Matrix<T> solve_cholesky(const Matrix<T>& matrix, const Matrix<T>& rhs) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_cholesky dimension mismatch");
  }

  auto output = ksj::array::make_pooled_matrix<T>(rhs.rows(), rhs.cols());
  if (detail::prefer_intel_solve_cholesky_matrix<T>(matrix.rows(), rhs.cols()) &&
      detail::intel::solve_cholesky(matrix, rhs, output)) {
    return output;
  }

  return detail::eigen::solve_cholesky(matrix, rhs);
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output, LeastSquaresQrWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  if (detail::prefer_intel_solve_qr_workspace<T>(matrix.cols()) &&
      detail::intel::solve_qr(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_qr(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  LeastSquaresQrWorkspace<T> workspace;
  if (detail::prefer_intel_solve_qr<T>(matrix.cols()) && detail::intel::solve_qr(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_qr(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output, LeastSquaresQrWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  if (detail::prefer_intel_solve_qr_matrix_workspace<T>(matrix.cols(), rhs.cols()) &&
      detail::intel::solve_qr(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_qr(matrix, rhs, output, workspace);
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  LeastSquaresQrWorkspace<T> workspace;
  if (detail::prefer_intel_solve_qr_matrix<T>(matrix.cols(), rhs.cols()) &&
      detail::intel::solve_qr(matrix, rhs, output, workspace)) {
    return true;
  }
  return detail::eigen::solve_qr(matrix, rhs, output, workspace);
}

template <typename T> [[nodiscard]] Vector<T> solve_qr(const Matrix<T>& matrix, const Vector<T>& rhs) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_qr dimension mismatch");
  }

  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  if (solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view())) {
    return output;
  }
  throw std::runtime_error("solve_qr failed");
}

template <typename T> [[nodiscard]] Matrix<T> solve_qr(const Matrix<T>& matrix, const Matrix<T>& rhs) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_qr dimension mismatch");
  }

  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  if (solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view())) {
    return output;
  }
  throw std::runtime_error("solve_qr failed");
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                                           ksj::array::VectorView<T> output, LeastSquaresSvdWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  if (detail::prefer_intel_least_squares_svd<T>(matrix.cols(), 1U) &&
      detail::intel::solve_least_squares_svd(matrix, rhs, output, workspace)) {
    return true;
  }

  return detail::eigen::solve_least_squares_svd(matrix, rhs, output);
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                                           ksj::array::MatrixView<T> output, LeastSquaresSvdWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  if (detail::prefer_intel_least_squares_svd<T>(matrix.cols(), rhs.cols()) &&
      detail::intel::solve_least_squares_svd(matrix, rhs, output, workspace)) {
    return true;
  }

  return detail::eigen::solve_least_squares_svd(matrix, rhs, output);
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(const Matrix<T>& matrix, const Vector<T>& rhs, Vector<T>& output,
                                           LeastSquaresSvdWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  return solve_least_squares_svd(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                 output.view(), workspace);
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output,
                                           LeastSquaresSvdWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  return solve_least_squares_svd(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                 output.view(), workspace);
}

template <typename T>
[[nodiscard]] Vector<T> solve_least_squares(const Matrix<T>& matrix, const Vector<T>& rhs,
                                            const LeastSquaresSolver solver = LeastSquaresSolver::qr) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_least_squares dimension mismatch");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("solve_least_squares input must not be empty");
  }

  if (solver == LeastSquaresSolver::qr) {
    return solve_qr(matrix, rhs);
  }
  if (solver == LeastSquaresSolver::rank_revealing_qr &&
      detail::prefer_intel_least_squares_rank_revealing_qr<T>(matrix.cols(), 1U)) {
    auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
    if (detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, output)) {
      return output;
    }
  }
  if (solver == LeastSquaresSolver::svd && detail::prefer_intel_least_squares_svd<T>(matrix.cols(), 1U)) {
    auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
    LeastSquaresSvdWorkspace<T> workspace;
    if (solve_least_squares_svd(matrix, rhs, output, workspace)) {
      return output;
    }
  }
  return detail::eigen::solve_least_squares(matrix, rhs, solver);
}

template <typename T>
[[nodiscard]] Matrix<T> solve_least_squares(const Matrix<T>& matrix, const Matrix<T>& rhs,
                                            const LeastSquaresSolver solver = LeastSquaresSolver::qr) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_least_squares dimension mismatch");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("solve_least_squares input must not be empty");
  }

  if (solver == LeastSquaresSolver::qr) {
    return solve_qr(matrix, rhs);
  }
  if (solver == LeastSquaresSolver::rank_revealing_qr &&
      detail::prefer_intel_least_squares_rank_revealing_qr<T>(matrix.cols(), rhs.cols())) {
    auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
    if (detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, output)) {
      return output;
    }
  }
  if (solver == LeastSquaresSolver::svd && detail::prefer_intel_least_squares_svd<T>(matrix.cols(), rhs.cols())) {
    auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
    LeastSquaresSvdWorkspace<T> workspace;
    if (solve_least_squares_svd(matrix, rhs, output, workspace)) {
      return output;
    }
  }
  return detail::eigen::solve_least_squares(matrix, rhs, solver);
}

template <typename T>
[[nodiscard]] bool solve_least_squares(const Matrix<T>& matrix, const Vector<T>& rhs, Vector<T>& output,
                                       LeastSquaresQrWorkspace<T>& qr_workspace,
                                       LeastSquaresSvdWorkspace<T>& svd_workspace,
                                       const LeastSquaresSolver solver = LeastSquaresSolver::qr) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.size() || output.size() != matrix.cols() || matrix.empty()) {
    return false;
  }

  if (solver == LeastSquaresSolver::qr) {
    return solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                    qr_workspace);
  }
  if (solver == LeastSquaresSolver::rank_revealing_qr &&
      detail::prefer_intel_least_squares_rank_revealing_qr<T>(matrix.cols(), 1U) &&
      detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, output)) {
    return true;
  }
  if (solver == LeastSquaresSolver::svd &&
      solve_least_squares_svd(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                              output.view(), svd_workspace)) {
    return true;
  }

  return detail::eigen::solve_least_squares(matrix, rhs, output, solver);
}

template <typename T>
[[nodiscard]] bool solve_least_squares(const Matrix<T>& matrix, const Matrix<T>& rhs, Matrix<T>& output,
                                       LeastSquaresQrWorkspace<T>& qr_workspace,
                                       LeastSquaresSvdWorkspace<T>& svd_workspace,
                                       const LeastSquaresSolver solver = LeastSquaresSolver::qr) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != rhs.rows() || output.rows() != matrix.cols() || output.cols() != rhs.cols() || matrix.empty()) {
    return false;
  }

  if (solver == LeastSquaresSolver::qr) {
    return solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                    qr_workspace);
  }
  if (solver == LeastSquaresSolver::rank_revealing_qr &&
      detail::prefer_intel_least_squares_rank_revealing_qr<T>(matrix.cols(), rhs.cols()) &&
      detail::intel::solve_least_squares_rank_revealing_qr(matrix, rhs, output)) {
    return true;
  }
  if (solver == LeastSquaresSolver::svd &&
      solve_least_squares_svd(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                              output.view(), svd_workspace)) {
    return true;
  }

  return detail::eigen::solve_least_squares(matrix, rhs, output, solver);
}

template <typename T> [[nodiscard]] Vector<T> solve_small(const Matrix<T>& matrix, const Vector<T>& rhs) {
  detail::require_supported_linalg_scalar<T>();
  return detail::eigen::solve_small(matrix, rhs);
}

} // namespace ksj::linalg
