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
#include <cmath>
#include <cstdint>
#include <limits>
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

// A bounded, allocation-free self-adjoint eigensolver for callback-bound
// numerical work.  Unlike the convenience overload above, this path never
// constructs an Eigen dynamic matrix or a pooled array: all O(n^2) temporary
// storage is the caller's `workspace` matrix.  It is deliberately limited to
// the receive-channel domain used by the reconstruction Providers.
//
// `matrix`, `eigenvectors`, and `workspace` must be contiguous n-by-n views;
// `eigenvalues` must have n entries; and none of their backing spans may
// overlap.  On valid input the routine writes ascending eigenvalues and the
// matching column eigenvectors.  `false` means the bounded Jacobi iteration
// did not converge; invalid views throw std::invalid_argument, matching the
// established decomposition API convention.
inline constexpr std::size_t kSelfAdjointEigenWorkspaceMaximumDimension = 64U;

namespace detail {

template <typename T> [[nodiscard]] constexpr T self_adjoint_conjugate(const T value) noexcept {
  if constexpr (ksj::array::is_complex_v<T>) {
    return std::conj(value);
  } else {
    return value;
  }
}

template <typename T>
[[nodiscard]] constexpr ksj::array::real_scalar_t<T> self_adjoint_real_part(const T value) noexcept {
  if constexpr (ksj::array::is_complex_v<T>) {
    return value.real();
  } else {
    return value;
  }
}

template <typename Left, typename Right>
[[nodiscard]] bool self_adjoint_contiguous_spans_overlap(const Left* const left, const std::size_t left_count,
                                                         const Right* const right,
                                                         const std::size_t right_count) noexcept {
  if (left == nullptr || right == nullptr || left_count == 0U || right_count == 0U) {
    return false;
  }
  const auto left_first = reinterpret_cast<std::uintptr_t>(left);
  const auto right_first = reinterpret_cast<std::uintptr_t>(right);
  const auto left_bytes = left_count * sizeof(Left);
  const auto right_bytes = right_count * sizeof(Right);
  return left_first < right_first + right_bytes && right_first < left_first + left_bytes;
}

} // namespace detail

template <typename T>
[[nodiscard]] bool self_adjoint_eigen_decomposition_with_workspace(
  const ksj::array::MatrixView<const T> matrix, const ksj::array::VectorView<ksj::array::real_scalar_t<T>> eigenvalues,
  const ksj::array::MatrixView<T> eigenvectors, const ksj::array::MatrixView<T> workspace) {
  detail::require_supported_linalg_scalar<T>();
  using real_type = ksj::array::real_scalar_t<T>;

  const auto dimension = matrix.rows();
  if (matrix.rows() != matrix.cols() || matrix.empty() || dimension > kSelfAdjointEigenWorkspaceMaximumDimension ||
      eigenvalues.size() != dimension || eigenvectors.rows() != dimension || eigenvectors.cols() != dimension ||
      workspace.rows() != dimension || workspace.cols() != dimension || !matrix.is_contiguous() ||
      !eigenvectors.is_contiguous() || !workspace.is_contiguous() || !eigenvalues.is_contiguous() ||
      matrix.data() == nullptr || eigenvalues.data() == nullptr || eigenvectors.data() == nullptr ||
      workspace.data() == nullptr) {
    throw std::invalid_argument(
      "self_adjoint_eigen_decomposition_with_workspace requires non-empty contiguous matching views up to 64");
  }
  const auto matrix_elements = dimension * dimension;
  if (detail::self_adjoint_contiguous_spans_overlap(matrix.data(), matrix_elements, eigenvectors.data(),
                                                    matrix_elements) ||
      detail::self_adjoint_contiguous_spans_overlap(matrix.data(), matrix_elements, workspace.data(),
                                                    matrix_elements) ||
      detail::self_adjoint_contiguous_spans_overlap(eigenvectors.data(), matrix_elements, workspace.data(),
                                                    matrix_elements) ||
      detail::self_adjoint_contiguous_spans_overlap(eigenvalues.data(), dimension, matrix.data(), matrix_elements) ||
      detail::self_adjoint_contiguous_spans_overlap(eigenvalues.data(), dimension, eigenvectors.data(),
                                                    matrix_elements) ||
      detail::self_adjoint_contiguous_spans_overlap(eigenvalues.data(), dimension, workspace.data(), matrix_elements)) {
    throw std::invalid_argument("self_adjoint_eigen_decomposition_with_workspace views must not overlap");
  }

  real_type diagonal_scale{};
  for (std::size_t row = 0U; row < dimension; ++row) {
    for (std::size_t column = 0U; column < dimension; ++column) {
      workspace(row, column) = matrix(row, column);
      eigenvectors(row, column) = row == column ? T{1} : T{};
    }
    if (!std::isfinite(detail::self_adjoint_real_part(workspace(row, row)))) {
      return false;
    }
    diagonal_scale += std::abs(detail::self_adjoint_real_part(workspace(row, row)));
  }
  const auto tolerance = std::numeric_limits<real_type>::epsilon() * std::max(real_type{1}, diagonal_scale) *
                         static_cast<real_type>(dimension) * real_type{8};
  const auto maximum_rotations = dimension * dimension * 64U;

  for (std::size_t rotation = 0U; rotation < maximum_rotations; ++rotation) {
    std::size_t pivot_row = 0U;
    std::size_t pivot_column = 0U;
    real_type pivot_magnitude{};
    for (std::size_t row = 0U; row < dimension; ++row) {
      for (std::size_t column = row + 1U; column < dimension; ++column) {
        const auto magnitude = static_cast<real_type>(std::abs(workspace(row, column)));
        if (!std::isfinite(magnitude)) {
          return false;
        }
        if (magnitude > pivot_magnitude) {
          pivot_magnitude = magnitude;
          pivot_row = row;
          pivot_column = column;
        }
      }
    }
    if (pivot_magnitude <= tolerance) {
      for (std::size_t index = 0U; index < dimension; ++index) {
        eigenvalues(index) = detail::self_adjoint_real_part(workspace(index, index));
      }
      for (std::size_t first = 0U; first < dimension; ++first) {
        std::size_t minimum = first;
        for (std::size_t candidate = first + 1U; candidate < dimension; ++candidate) {
          if (eigenvalues(candidate) < eigenvalues(minimum)) {
            minimum = candidate;
          }
        }
        if (minimum == first) {
          continue;
        }
        std::swap(eigenvalues(first), eigenvalues(minimum));
        for (std::size_t row = 0U; row < dimension; ++row) {
          std::swap(eigenvectors(row, first), eigenvectors(row, minimum));
        }
      }
      return true;
    }

    const auto diagonal_row = detail::self_adjoint_real_part(workspace(pivot_row, pivot_row));
    const auto diagonal_column = detail::self_adjoint_real_part(workspace(pivot_column, pivot_column));
    const auto tau = (diagonal_column - diagonal_row) / (real_type{2} * pivot_magnitude);
    const auto tangent = tau >= real_type{} ? real_type{1} / (tau + std::sqrt(real_type{1} + tau * tau))
                                            : -real_type{1} / (-tau + std::sqrt(real_type{1} + tau * tau));
    const auto cosine = real_type{1} / std::sqrt(real_type{1} + tangent * tangent);
    const auto sine = tangent * cosine;
    const auto phase = workspace(pivot_row, pivot_column) / pivot_magnitude;
    const auto conjugate_phase = detail::self_adjoint_conjugate(phase);

    for (std::size_t index = 0U; index < dimension; ++index) {
      if (index == pivot_row || index == pivot_column) {
        continue;
      }
      const auto row_value = workspace(index, pivot_row);
      const auto column_value = workspace(index, pivot_column) * conjugate_phase;
      const auto updated_row = cosine * row_value - sine * column_value;
      const auto updated_column = sine * row_value + cosine * column_value;
      workspace(index, pivot_row) = updated_row;
      workspace(pivot_row, index) = detail::self_adjoint_conjugate(updated_row);
      workspace(index, pivot_column) = updated_column;
      workspace(pivot_column, index) = detail::self_adjoint_conjugate(updated_column);
    }
    workspace(pivot_row, pivot_row) = T{cosine * cosine * diagonal_row -
                                        real_type{2} * cosine * sine * pivot_magnitude + sine * sine * diagonal_column};
    workspace(pivot_column, pivot_column) = T{
      sine * sine * diagonal_row + real_type{2} * cosine * sine * pivot_magnitude + cosine * cosine * diagonal_column};
    workspace(pivot_row, pivot_column) = T{};
    workspace(pivot_column, pivot_row) = T{};

    for (std::size_t row = 0U; row < dimension; ++row) {
      const auto row_vector = eigenvectors(row, pivot_row);
      const auto column_vector = eigenvectors(row, pivot_column) * conjugate_phase;
      eigenvectors(row, pivot_row) = cosine * row_vector - sine * column_vector;
      eigenvectors(row, pivot_column) = sine * row_vector + cosine * column_vector;
    }
  }
  return false;
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
