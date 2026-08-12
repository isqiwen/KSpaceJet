#pragma once

/// Matrix factorization and eigendecomposition APIs, including their output and failure semantics.

#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/detail/eigen/eigen_linalg_decompositions.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_decompositions.hpp"
#include "kspacejet/linalg/detail/linalg_types.hpp"
#include "kspacejet/linalg/detail/linalg_policy.hpp"
#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::linalg {

namespace detail {

template <typename T>
[[nodiscard]] bool try_intel_singular_values(ksj::array::MatrixView<const T> matrix,
                                             Vector<ksj::array::real_scalar_t<T>>& output) {
  if (!prefer_intel_singular_values<T>(matrix.rows(), matrix.cols())) {
    return false;
  }

  return intel::singular_values(matrix, output);
}

template <typename T>
[[nodiscard]] bool try_intel_singular_values(const Matrix<T>& matrix, Vector<ksj::array::real_scalar_t<T>>& output) {
  return try_intel_singular_values(ksj::array::as_const_view(matrix.view()), output);
}

template <typename T>
[[nodiscard]] bool run_intel_svd(ksj::array::MatrixView<const T> matrix, const bool full_matrices,
                                 SingularValueDecomposition<T>& output) {
  using real_type = ksj::array::real_scalar_t<T>;
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  auto u = ksj::array::make_pooled_matrix<T>(matrix.rows(), full_matrices ? matrix.rows() : value_count);
  auto values = ksj::array::make_pooled_vector<real_type>(value_count);
  auto v_adjoint = ksj::array::make_pooled_matrix<T>(full_matrices ? matrix.cols() : value_count, matrix.cols());
  if (!intel::svd(matrix, u, values, v_adjoint, full_matrices)) {
    return false;
  }

  output.u = std::move(u);
  output.singular_values = std::move(values);
  output.v_adjoint = std::move(v_adjoint);
  return true;
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] bool run_intel_svd_in_place(ksj::array::MatrixView<T> matrix, const bool full_matrices,
                                          SingularValueDecomposition<T>& output) {
  using real_type = ksj::array::real_scalar_t<T>;
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  auto u = ksj::array::make_pooled_matrix<T>(matrix.rows(), full_matrices ? matrix.rows() : value_count);
  auto values = ksj::array::make_pooled_vector<real_type>(value_count);
  auto v_adjoint = ksj::array::make_pooled_matrix<T>(full_matrices ? matrix.cols() : value_count, matrix.cols());
  if (!intel::svd_in_place(matrix, u, values, v_adjoint, full_matrices)) {
    return false;
  }

  output.u = std::move(u);
  output.singular_values = std::move(values);
  output.v_adjoint = std::move(v_adjoint);
  return true;
}

template <typename T>
[[nodiscard]] bool try_intel_svd(ksj::array::MatrixView<const T> matrix, const SvdMode mode,
                                 SingularValueDecomposition<T>& output) {
  if (!prefer_intel_svd<T>(matrix.rows(), matrix.cols())) {
    return false;
  }

  return run_intel_svd(matrix, mode == SvdMode::full, output);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] bool try_intel_svd_in_place(ksj::array::MatrixView<T> matrix, const SvdMode mode,
                                          SingularValueDecomposition<T>& output) {
  if (!prefer_intel_svd_in_place<T>(matrix.rows(), matrix.cols()) || !intel::can_svd_in_place(matrix)) {
    return false;
  }
  if (!run_intel_svd_in_place(matrix, mode == SvdMode::full, output)) {
    throw std::runtime_error("svd_in_place failed");
  }
  return true;
}

template <typename T>
[[nodiscard]] bool try_intel_svd(const Matrix<T>& matrix, const SvdMode mode, SingularValueDecomposition<T>& output) {
  return try_intel_svd(ksj::array::as_const_view(matrix.view()), mode, output);
}

template <typename T>
[[nodiscard]] bool svd_output_dimensions_match(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                                               ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                                               ksj::array::MatrixView<T> v_adjoint, const bool full_matrices) {
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  const auto expected_u_cols = full_matrices ? matrix.rows() : value_count;
  const auto expected_v_rows = full_matrices ? matrix.cols() : value_count;
  return !matrix.empty() && u.rows() == matrix.rows() && u.cols() == expected_u_cols && values.size() == value_count &&
         v_adjoint.rows() == expected_v_rows && v_adjoint.cols() == matrix.cols();
}

template <typename T>
void resize_svd_workspace_if_needed(SvdWorkspace<T>& workspace, const std::size_t rows, const std::size_t cols) {
  const auto value_count = std::min(rows, cols);
  const auto superb_size = value_count > 1U ? value_count - 1U : 1U;
  if (workspace.matrix_work.rows() != rows || workspace.matrix_work.cols() != cols ||
      workspace.superb_work.size() != superb_size) {
    workspace.resize(rows, cols);
  }
}

template <typename T>
void copy_svd_outputs(const SingularValueDecomposition<T>& decomposition, ksj::array::MatrixView<T> u,
                      ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                      ksj::array::MatrixView<T> v_adjoint) {
  ksj::array::copy(ksj::array::as_const_view(decomposition.u.view()), u);
  ksj::array::copy(ksj::array::as_const_view(decomposition.singular_values.view()), values);
  ksj::array::copy(ksj::array::as_const_view(decomposition.v_adjoint.view()), v_adjoint);
}

template <typename T>
[[nodiscard]] bool try_intel_svd_workspace(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                                           ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                                           ksj::array::MatrixView<T> v_adjoint, SvdWorkspace<T>& workspace,
                                           const SvdMode mode) {
  if (!prefer_intel_svd_workspace<T>(matrix.rows(), matrix.cols())) {
    return false;
  }
  return intel::svd(matrix, u, values, v_adjoint, workspace, mode == SvdMode::full);
}

template <typename T>
[[nodiscard]] bool try_intel_left_singular_vectors(ksj::array::MatrixView<const T> matrix,
                                                   ksj::array::MatrixView<T> output) {
  if (!prefer_intel_left_singular_vectors<T>(matrix.rows(), matrix.cols())) {
    return false;
  }

  LeftSingularVectorsWorkspace<T> workspace;
  workspace.resize(matrix.rows(), matrix.cols());
  return intel::left_singular_vectors(matrix, output, workspace);
}

template <typename T>
[[nodiscard]] bool
try_intel_self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                                           ksj::array::VectorView<ksj::array::real_scalar_t<T>> eigenvalues,
                                           ksj::array::MatrixView<T> eigenvectors) {
  if (!prefer_intel_self_adjoint_eigen<T>(matrix.rows())) {
    return false;
  }

  return intel::self_adjoint_eigen_decomposition(matrix, eigenvalues, eigenvectors);
}

template <typename T>
[[nodiscard]] bool try_intel_self_adjoint_eigen_decomposition(const Matrix<T>& matrix,
                                                              Vector<ksj::array::real_scalar_t<T>>& eigenvalues,
                                                              Matrix<T>& eigenvectors) {
  return prefer_intel_self_adjoint_eigen<T>(matrix.rows()) &&
         intel::self_adjoint_eigen_decomposition(ksj::array::as_const_view(matrix.view()), eigenvalues.view(),
                                                 eigenvectors.view());
}

} // namespace detail

template <typename T>
[[nodiscard]] Vector<ksj::array::real_scalar_t<T>> singular_values(ksj::array::MatrixView<const T> matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (detail::prefer_intel_singular_values<T>(matrix.rows(), matrix.cols())) {
    using real_type = ksj::array::real_scalar_t<T>;
    auto output = ksj::array::make_pooled_vector<real_type>(std::min(matrix.rows(), matrix.cols()));
    if (detail::try_intel_singular_values(matrix, output)) {
      return output;
    }
  }

  return detail::eigen::singular_values(matrix);
}

template <typename T> [[nodiscard]] Vector<ksj::array::real_scalar_t<T>> singular_values(const Matrix<T>& matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (detail::prefer_intel_singular_values<T>(matrix.rows(), matrix.cols())) {
    using real_type = ksj::array::real_scalar_t<T>;
    auto output = ksj::array::make_pooled_vector<real_type>(std::min(matrix.rows(), matrix.cols()));
    if (detail::try_intel_singular_values(matrix, output)) {
      return output;
    }
  }

  return detail::eigen::singular_values(ksj::array::as_const_view(matrix.view()));
}

template <typename T>
[[nodiscard]] SingularValueDecomposition<T> svd(ksj::array::MatrixView<const T> matrix, const SvdMode mode) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("svd input must not be empty");
  }
  SingularValueDecomposition<T> output;
  if (detail::try_intel_svd(matrix, mode, output)) {
    return output;
  }

  return detail::eigen::svd(matrix, mode == SvdMode::full);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> svd(const Matrix<T>& matrix, const SvdMode mode) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("svd input must not be empty");
  }
  SingularValueDecomposition<T> output;
  if (detail::try_intel_svd(matrix, mode, output)) {
    return output;
  }

  return detail::eigen::svd(ksj::array::as_const_view(matrix.view()), mode == SvdMode::full);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] SingularValueDecomposition<T> svd_in_place(ksj::array::MatrixView<T> matrix, const SvdMode mode) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("svd_in_place input must not be empty");
  }
  SingularValueDecomposition<T> output;
  if (detail::try_intel_svd_in_place(matrix, mode, output)) {
    return output;
  }

  return detail::eigen::svd(ksj::array::as_const_view(matrix), mode == SvdMode::full);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> svd_in_place(Matrix<T>& matrix, const SvdMode mode) {
  return svd_in_place(matrix.view(), mode);
}

template <typename T>
[[nodiscard]] bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                       ksj::array::VectorView<ksj::array::real_scalar_t<T>> values, ksj::array::MatrixView<T> v_adjoint,
                       SvdWorkspace<T>& workspace, const SvdMode mode) {
  detail::require_supported_linalg_scalar<T>();
  const auto full_matrices = mode == SvdMode::full;
  if (!detail::svd_output_dimensions_match(matrix, u, values, v_adjoint, full_matrices)) {
    return false;
  }
  detail::resize_svd_workspace_if_needed(workspace, matrix.rows(), matrix.cols());
  if (detail::try_intel_svd_workspace(matrix, u, values, v_adjoint, workspace, mode)) {
    return true;
  }

  const auto decomposition = detail::eigen::svd(matrix, full_matrices);
  detail::copy_svd_outputs(decomposition, u, values, v_adjoint);
  return true;
}

template <typename T>
[[nodiscard]] bool svd(const Matrix<T>& matrix, Matrix<T>& u, Vector<ksj::array::real_scalar_t<T>>& values,
                       Matrix<T>& v_adjoint, SvdWorkspace<T>& workspace, const SvdMode mode) {
  return svd(ksj::array::as_const_view(matrix.view()), u.view(), values.view(), v_adjoint.view(), workspace, mode);
}

template <typename T>
[[nodiscard]] bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                       ksj::array::VectorView<ksj::array::real_scalar_t<T>> values, ksj::array::MatrixView<T> v_adjoint,
                       SvdWorkspace<T>& workspace) {
  return svd(matrix, u, values, v_adjoint, workspace, SvdMode::thin);
}

template <typename T>
[[nodiscard]] bool svd(const Matrix<T>& matrix, Matrix<T>& u, Vector<ksj::array::real_scalar_t<T>>& values,
                       Matrix<T>& v_adjoint, SvdWorkspace<T>& workspace) {
  return svd(matrix, u, values, v_adjoint, workspace, SvdMode::thin);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> svd(ksj::array::MatrixView<const T> matrix) {
  return svd(matrix, SvdMode::thin);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> svd(const Matrix<T>& matrix) {
  return svd(matrix, SvdMode::thin);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] SingularValueDecomposition<T> svd_in_place(ksj::array::MatrixView<T> matrix) {
  return svd_in_place(matrix, SvdMode::thin);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> svd_in_place(Matrix<T>& matrix) {
  return svd_in_place(matrix, SvdMode::thin);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> full_svd(ksj::array::MatrixView<const T> matrix) {
  return svd(matrix, SvdMode::full);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> full_svd(const Matrix<T>& matrix) {
  return svd(matrix, SvdMode::full);
}

template <typename T>
[[nodiscard]] bool full_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                            ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                            ksj::array::MatrixView<T> v_adjoint, SvdWorkspace<T>& workspace) {
  return svd(matrix, u, values, v_adjoint, workspace, SvdMode::full);
}

template <typename T>
[[nodiscard]] bool full_svd(const Matrix<T>& matrix, Matrix<T>& u, Vector<ksj::array::real_scalar_t<T>>& values,
                            Matrix<T>& v_adjoint, SvdWorkspace<T>& workspace) {
  return svd(matrix, u, values, v_adjoint, workspace, SvdMode::full);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] SingularValueDecomposition<T> full_svd_in_place(ksj::array::MatrixView<T> matrix) {
  return svd_in_place(matrix, SvdMode::full);
}

template <typename T> [[nodiscard]] SingularValueDecomposition<T> full_svd_in_place(Matrix<T>& matrix) {
  return svd_in_place(matrix, SvdMode::full);
}

template <typename T>
void left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("left_singular_vectors input must not be empty");
  }
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  if (output.rows() != matrix.rows() || output.cols() != value_count) {
    throw std::invalid_argument("left_singular_vectors output dimension mismatch");
  }

  if (detail::try_intel_left_singular_vectors(matrix, output)) {
    return;
  }

  detail::eigen::left_singular_vectors(matrix, output);
}

template <typename T> [[nodiscard]] Matrix<T> left_singular_vectors(ksj::array::MatrixView<const T> matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("left_singular_vectors input must not be empty");
  }
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  auto output = ksj::array::make_pooled_matrix<T>(matrix.rows(), value_count);
  left_singular_vectors(matrix, output.view());
  return output;
}

template <typename T>
void left_singular_vectors(const Matrix<T>& matrix, Matrix<T>& output, LeftSingularVectorsWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("left_singular_vectors input must not be empty");
  }
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  if (output.rows() != matrix.rows() || output.cols() != value_count) {
    throw std::invalid_argument("left_singular_vectors output dimension mismatch");
  }
  const auto superb_size = value_count > 1U ? value_count - 1U : 1U;
  if (workspace.matrix_work.rows() != matrix.rows() || workspace.matrix_work.cols() != matrix.cols() ||
      workspace.values_work.size() != value_count || workspace.superb_work.size() != superb_size) {
    workspace.resize(matrix.rows(), matrix.cols());
  }
  if (detail::prefer_intel_left_singular_vectors<T>(matrix.rows(), matrix.cols()) &&
      detail::intel::left_singular_vectors(matrix, output, workspace)) {
    return;
  }

  detail::eigen::left_singular_vectors(ksj::array::as_const_view(matrix.view()), output.view());
}

template <typename T> void left_singular_vectors(const Matrix<T>& matrix, Matrix<T>& output) {
  LeftSingularVectorsWorkspace<T> workspace;
  left_singular_vectors(matrix, output, workspace);
}

template <typename T> [[nodiscard]] Matrix<T> left_singular_vectors(const Matrix<T>& matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.empty()) {
    throw std::invalid_argument("left_singular_vectors input must not be empty");
  }
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  auto output = ksj::array::make_pooled_matrix<T>(matrix.rows(), value_count);
  left_singular_vectors(matrix, output);
  return output;
}

template <typename T>
[[nodiscard]] SelfAdjointEigenDecomposition<T>
self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition requires a square matrix");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition input must not be empty");
  }
  using real_type = ksj::array::real_scalar_t<T>;
  auto values = ksj::array::make_pooled_vector<real_type>(matrix.rows());
  auto vectors = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
  if (!detail::try_intel_self_adjoint_eigen_decomposition(matrix, values.view(), vectors.view())) {
    detail::eigen::self_adjoint_eigen_decomposition(matrix, values.view(), vectors.view());
  }
  return SelfAdjointEigenDecomposition<T>{std::move(values), std::move(vectors)};
}

template <typename T>
void self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                                      ksj::array::VectorView<ksj::array::real_scalar_t<T>> eigenvalues,
                                      ksj::array::MatrixView<T> eigenvectors) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition requires a square matrix");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition input must not be empty");
  }
  if (eigenvalues.size() != matrix.rows()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition eigenvalue output dimension mismatch");
  }
  if (eigenvectors.rows() != matrix.rows() || eigenvectors.cols() != matrix.cols()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition eigenvector output dimension mismatch");
  }

  if (detail::try_intel_self_adjoint_eigen_decomposition(matrix, eigenvalues, eigenvectors)) {
    return;
  }

  detail::eigen::self_adjoint_eigen_decomposition(matrix, eigenvalues, eigenvectors);
}

template <typename T>
[[nodiscard]] SelfAdjointEigenDecomposition<T> self_adjoint_eigen_decomposition(const Matrix<T>& matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition requires a square matrix");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition input must not be empty");
  }
  using real_type = ksj::array::real_scalar_t<T>;
  auto values = ksj::array::make_pooled_vector<real_type>(matrix.rows());
  auto vectors = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
  if (detail::try_intel_self_adjoint_eigen_decomposition(matrix, values, vectors)) {
    return SelfAdjointEigenDecomposition<T>{std::move(values), std::move(vectors)};
  }

  detail::eigen::self_adjoint_eigen_decomposition(ksj::array::as_const_view(matrix.view()), values.view(),
                                                  vectors.view());
  return SelfAdjointEigenDecomposition<T>{std::move(values), std::move(vectors)};
}

template <typename T> [[nodiscard]] EigenDecomposition<T> eigen_decomposition(const Matrix<T>& matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("eigen_decomposition requires a square matrix");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("eigen_decomposition input must not be empty");
  }
  using complex_type = typename EigenDecomposition<T>::complex_type;
  auto values = ksj::array::make_pooled_vector<complex_type>(matrix.rows());
  auto vectors = ksj::array::make_pooled_matrix<complex_type>(matrix.rows(), matrix.cols());
  if (detail::prefer_intel_general_eigen<T>(matrix.rows()) &&
      detail::intel::eigen_decomposition(matrix, values, vectors)) {
    return EigenDecomposition<T>{std::move(values), std::move(vectors)};
  }

  return detail::eigen::eigen_decomposition(matrix);
}

template <typename T>
[[nodiscard]] bool
eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                    ksj::array::VectorView<typename GeneralEigenWorkspace<T>::complex_type> eigenvalues,
                    ksj::array::MatrixView<typename GeneralEigenWorkspace<T>::complex_type> eigenvectors,
                    GeneralEigenWorkspace<T>& workspace) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols() || matrix.empty() || eigenvalues.size() != matrix.rows() ||
      eigenvectors.rows() != matrix.rows() || eigenvectors.cols() != matrix.cols()) {
    return false;
  }

  if (detail::prefer_intel_general_eigen_workspace<T>(matrix.rows()) &&
      detail::intel::eigen_decomposition(matrix, eigenvalues, eigenvectors, workspace)) {
    return true;
  }
  return detail::eigen::eigen_decomposition(matrix, eigenvalues, eigenvectors, workspace);
}

template <typename T> [[nodiscard]] EigenDecomposition<T> eigen_decomposition(ksj::array::MatrixView<const T> matrix) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("eigen_decomposition requires a square matrix");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("eigen_decomposition input must not be empty");
  }

  using complex_type = typename GeneralEigenWorkspace<T>::complex_type;
  auto values = ksj::array::make_pooled_vector<complex_type>(matrix.rows());
  auto vectors = ksj::array::make_pooled_matrix<complex_type>(matrix.rows(), matrix.cols());
  GeneralEigenWorkspace<T> workspace;
  if (detail::prefer_intel_general_eigen<T>(matrix.rows()) &&
      detail::intel::eigen_decomposition(matrix, values.view(), vectors.view(), workspace)) {
    return EigenDecomposition<T>{std::move(values), std::move(vectors)};
  }
  if (!detail::eigen::eigen_decomposition(matrix, values.view(), vectors.view(), workspace)) {
    throw std::runtime_error("eigen_decomposition failed");
  }
  return EigenDecomposition<T>{std::move(values), std::move(vectors)};
}

} // namespace ksj::linalg
