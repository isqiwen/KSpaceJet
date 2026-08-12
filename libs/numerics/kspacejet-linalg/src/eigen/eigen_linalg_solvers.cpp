#include "kspacejet/linalg/detail/eigen/eigen_linalg_solvers.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

#include <Eigen/Cholesky>
#include <Eigen/LU>
#include <Eigen/QR>
#include <Eigen/SVD>

namespace ksj::linalg::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;
}

template <typename T> [[nodiscard]] T determinant(const ksj::array::PooledMatrix<T>& matrix) {
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("determinant requires a square matrix");
  }
  return as_eigen(matrix).determinant();
}

template <typename T> void inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  if (input.rows() != input.cols()) {
    throw std::invalid_argument("inverse requires a square matrix");
  }
  if (output.rows() != input.rows() || output.cols() != input.cols()) {
    throw std::invalid_argument("inverse output dimension mismatch");
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
  const dense_matrix dense = as_eigen(input);
  as_eigen(output) = dense.inverse();
}

template <typename T>
[[nodiscard]] bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                           LuFactorWorkspace<T>& workspace) {
  if (input.rows() != input.cols() || output.rows() != input.rows() || output.cols() != input.cols()) {
    return false;
  }

  workspace.resize(input.rows());
  as_eigen(workspace.matrix_work) = as_eigen(input);
  const auto lu = as_eigen(workspace.matrix_work).fullPivLu();
  if (!lu.isInvertible()) {
    return false;
  }
  as_eigen(output) = lu.inverse();
  return true;
}

template <typename T> [[nodiscard]] ksj::array::PooledMatrix<T> inverse(ksj::array::MatrixView<const T> input) {
  auto output = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
  inverse(input, output.view());
  return output;
}

template <typename T> [[nodiscard]] ksj::array::PooledMatrix<T> inverse(const ksj::array::PooledMatrix<T>& matrix) {
  return inverse(ksj::array::as_const_view(matrix.view()));
}

template <typename T>
void pseudo_inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                    const ksj::array::real_scalar_t<T> singular_tolerance) {
  if (input.empty()) {
    throw std::invalid_argument("pseudo_inverse input must not be empty");
  }
  if (output.rows() != input.cols() || output.cols() != input.rows()) {
    throw std::invalid_argument("pseudo_inverse output dimension mismatch");
  }

  using real_type = ksj::array::real_scalar_t<T>;
  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
  using real_vector = Eigen::Matrix<real_type, Eigen::Dynamic, 1>;
  const dense_matrix dense = as_eigen(input);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, Eigen::ComputeThinU | Eigen::ComputeThinV);
  const real_vector singular_values = decomposition.singularValues();
  const real_type max_singular_value = singular_values.size() == 0 ? real_type{} : singular_values.maxCoeff();
  const real_type threshold = singular_tolerance > real_type{}
                                ? singular_tolerance
                                : std::numeric_limits<real_type>::epsilon() *
                                    static_cast<real_type>(std::max(input.rows(), input.cols())) * max_singular_value;

  dense_matrix inverted_singular_values =
    dense_matrix::Zero(decomposition.matrixV().cols(), decomposition.matrixU().cols());
  for (Eigen::Index index = 0; index < singular_values.size(); ++index) {
    if (singular_values(index) > threshold) {
      inverted_singular_values(index, index) = T{1} / T{singular_values(index)};
    }
  }

  as_eigen(output) = decomposition.matrixV() * inverted_singular_values * decomposition.matrixU().adjoint();
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> pseudo_inverse(ksj::array::MatrixView<const T> input,
                                                         const ksj::array::real_scalar_t<T> singular_tolerance) {
  auto output = ksj::array::make_pooled_matrix<T>(input.cols(), input.rows());
  pseudo_inverse(input, output.view(), singular_tolerance);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T>
pseudo_inverse(const ksj::array::PooledMatrix<T>& matrix,
               const ksj::array::real_scalar_t<T> singular_tolerance = ksj::array::real_scalar_t<T>{}) {
  return pseudo_inverse(ksj::array::as_const_view(matrix.view()), singular_tolerance);
}

template <typename T>
void solve(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
           ksj::array::PooledVector<T>& output) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve dimension mismatch");
  }
  if (output.size() != matrix.cols()) {
    throw std::invalid_argument("solve output dimension mismatch");
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  using dense_vector = Eigen::Matrix<T, Eigen::Dynamic, 1>;
  const dense_matrix dense = as_eigen(matrix);
  const dense_vector dense_rhs = as_eigen(rhs);
  as_eigen(output) = dense.partialPivLu().solve(dense_rhs);
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != matrix.cols()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  using dense_vector = Eigen::Matrix<T, Eigen::Dynamic, 1>;
  const dense_matrix dense = as_eigen(matrix);
  const dense_vector dense_rhs = as_eigen(rhs);
  const auto lu = dense.fullPivLu();
  if (!lu.isInvertible()) {
    return false;
  }

  as_eigen(output) = lu.solve(dense_rhs);
  return true;
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != matrix.cols()) {
    return false;
  }

  workspace.resize(matrix.rows(), 1U);
  as_eigen(workspace.matrix_work) = as_eigen(matrix);
  as_eigen(workspace.rhs_work).col(0) = as_eigen(rhs);
  const auto lu = as_eigen(workspace.matrix_work).fullPivLu();
  if (!lu.isInvertible()) {
    return false;
  }

  as_eigen(output) = lu.solve(as_eigen(workspace.rhs_work).col(0));
  return true;
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() || output.rows() != matrix.cols() ||
      output.cols() != rhs.cols()) {
    return false;
  }

  workspace.resize(matrix.rows(), rhs.cols());
  as_eigen(workspace.matrix_work) = as_eigen(matrix);
  as_eigen(workspace.rhs_work) = as_eigen(rhs);
  const auto lu = as_eigen(workspace.matrix_work).fullPivLu();
  if (!lu.isInvertible()) {
    return false;
  }

  as_eigen(output) = lu.solve(as_eigen(workspace.rhs_work));
  return true;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> solve(const ksj::array::PooledMatrix<T>& matrix,
                                                const ksj::array::PooledVector<T>& rhs) {
  auto output = ksj::array::make_pooled_vector<T>(rhs.size());
  solve(matrix, rhs, output);
  return output;
}

template <typename T>
void solve(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,
           ksj::array::PooledMatrix<T>& output) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve dimension mismatch");
  }
  if (output.rows() != matrix.cols() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("solve output dimension mismatch");
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  const dense_matrix dense_rhs = as_eigen(rhs);
  as_eigen(output) = dense.partialPivLu().solve(dense_rhs);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> solve(const ksj::array::PooledMatrix<T>& matrix,
                                                const ksj::array::PooledMatrix<T>& rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  solve(matrix, rhs, output);
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> cholesky_lower(const ksj::array::PooledMatrix<T>& matrix) {
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("cholesky requires a square matrix");
  }
  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::LLT<dense_matrix> factorization(dense);
  if (factorization.info() != Eigen::Success) {
    throw std::invalid_argument("cholesky factorization failed");
  }
  auto output = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
  as_eigen(output) = factorization.matrixL();
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> solve_cholesky(const ksj::array::PooledMatrix<T>& matrix,
                                                         const ksj::array::PooledVector<T>& rhs) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_cholesky dimension mismatch");
  }
  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::LLT<dense_matrix> factorization(dense);
  if (factorization.info() != Eigen::Success) {
    throw std::invalid_argument("cholesky factorization failed");
  }
  auto output = ksj::array::make_pooled_vector<T>(rhs.size());
  as_eigen(output) = factorization.solve(as_eigen(rhs));
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> solve_cholesky(const ksj::array::PooledMatrix<T>& matrix,
                                                         const ksj::array::PooledMatrix<T>& rhs) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_cholesky dimension mismatch");
  }
  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::LLT<dense_matrix> factorization(dense);
  if (factorization.info() != Eigen::Success) {
    throw std::invalid_argument("cholesky factorization failed");
  }
  auto output = ksj::array::make_pooled_matrix<T>(rhs.rows(), rhs.cols());
  as_eigen(output) = factorization.solve(as_eigen(rhs));
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> solve_qr(const ksj::array::PooledMatrix<T>& matrix,
                                                   const ksj::array::PooledVector<T>& rhs) {
  if (matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_qr dimension mismatch");
  }
  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  as_eigen(output) = as_eigen(matrix).colPivHouseholderQr().solve(as_eigen(rhs));
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> solve_qr(const ksj::array::PooledMatrix<T>& matrix,
                                                   const ksj::array::PooledMatrix<T>& rhs) {
  if (matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_qr dimension mismatch");
  }
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  as_eigen(output) = as_eigen(matrix).colPivHouseholderQr().solve(as_eigen(rhs));
  return output;
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output, LeastSquaresQrWorkspace<T>& workspace) {
  (void)workspace;
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  as_eigen(output) = as_eigen(matrix).colPivHouseholderQr().solve(as_eigen(rhs));
  return true;
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output, LeastSquaresQrWorkspace<T>& workspace) {
  (void)workspace;
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  as_eigen(output) = as_eigen(matrix).colPivHouseholderQr().solve(as_eigen(rhs));
  return true;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T>
solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,
                                      const ksj::array::PooledVector<T>& rhs) {
  if (matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_least_squares_rank_revealing_qr dimension mismatch");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("solve_least_squares_rank_revealing_qr input must not be empty");
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::CompleteOrthogonalDecomposition<dense_matrix> decomposition(dense);
  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  as_eigen(output) = decomposition.solve(as_eigen(rhs));
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T>
solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,
                                      const ksj::array::PooledMatrix<T>& rhs) {
  if (matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_least_squares_rank_revealing_qr dimension mismatch");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("solve_least_squares_rank_revealing_qr input must not be empty");
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::CompleteOrthogonalDecomposition<dense_matrix> decomposition(dense);
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  as_eigen(output) = decomposition.solve(as_eigen(rhs));
  return output;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix,
                                           const ksj::array::PooledVector<T>& rhs,
                                           ksj::array::PooledVector<T>& output) {
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, Eigen::ComputeThinU | Eigen::ComputeThinV);
  if (decomposition.info() != Eigen::Success) {
    return false;
  }

  as_eigen(output) = decomposition.solve(as_eigen(rhs));
  return true;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                                           ksj::array::VectorView<T> output) {
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, Eigen::ComputeThinU | Eigen::ComputeThinV);
  if (decomposition.info() != Eigen::Success) {
    return false;
  }

  as_eigen(output) = decomposition.solve(as_eigen(rhs));
  return true;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix,
                                                                  const ksj::array::PooledVector<T>& rhs) {
  if (matrix.rows() != rhs.size()) {
    throw std::invalid_argument("solve_least_squares_svd dimension mismatch");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("solve_least_squares_svd input must not be empty");
  }

  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  if (!solve_least_squares_svd(matrix, rhs, output)) {
    throw std::invalid_argument("least-squares svd decomposition failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix,
                                           const ksj::array::PooledMatrix<T>& rhs,
                                           ksj::array::PooledMatrix<T>& output) {
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, Eigen::ComputeThinU | Eigen::ComputeThinV);
  if (decomposition.info() != Eigen::Success) {
    return false;
  }

  as_eigen(output) = decomposition.solve(as_eigen(rhs));
  return true;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                                           ksj::array::MatrixView<T> output) {
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, Eigen::ComputeThinU | Eigen::ComputeThinV);
  if (decomposition.info() != Eigen::Success) {
    return false;
  }

  as_eigen(output) = decomposition.solve(as_eigen(rhs));
  return true;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix,
                                                                  const ksj::array::PooledMatrix<T>& rhs) {
  if (matrix.rows() != rhs.rows()) {
    throw std::invalid_argument("solve_least_squares_svd dimension mismatch");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("solve_least_squares_svd input must not be empty");
  }

  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  if (!solve_least_squares_svd(matrix, rhs, output)) {
    throw std::invalid_argument("least-squares svd decomposition failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_normal_equations(const ksj::array::PooledMatrix<T>& matrix,
                                                        const ksj::array::PooledVector<T>& rhs,
                                                        ksj::array::PooledVector<T>& output) {
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  using dense_vector = Eigen::Matrix<T, Eigen::Dynamic, 1>;
  const auto dense = as_eigen(matrix);
  const auto dense_rhs = as_eigen(rhs);
  const dense_matrix normal_matrix = dense.adjoint() * dense;
  const dense_vector normal_rhs = dense.adjoint() * dense_rhs;
  // PartialPivLU assumes an invertible input and cannot report a singular
  // factorization in Eigen 3.4.  Normal equations are singular whenever the
  // design matrix is rank deficient, so use the rank-revealing variant here
  // rather than returning a potentially invalid solution.
  Eigen::FullPivLU<dense_matrix> factorization(normal_matrix);
  if (!factorization.isInvertible()) {
    return false;
  }

  as_eigen(output) = factorization.solve(normal_rhs);
  return true;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_normal_equations_cholesky(const ksj::array::PooledMatrix<T>& matrix,
                                                                 const ksj::array::PooledVector<T>& rhs,
                                                                 ksj::array::PooledVector<T>& output) {
  if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  using dense_vector = Eigen::Matrix<T, Eigen::Dynamic, 1>;
  const auto dense = as_eigen(matrix);
  const auto dense_rhs = as_eigen(rhs);
  const dense_matrix normal_matrix = dense.adjoint() * dense;
  const dense_vector normal_rhs = dense.adjoint() * dense_rhs;
  Eigen::LLT<dense_matrix> factorization(normal_matrix);
  if (factorization.info() != Eigen::Success) {
    return false;
  }

  as_eigen(output) = factorization.solve(normal_rhs);
  return true;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_normal_equations(const ksj::array::PooledMatrix<T>& matrix,
                                                        const ksj::array::PooledMatrix<T>& rhs,
                                                        ksj::array::PooledMatrix<T>& output) {
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const auto dense = as_eigen(matrix);
  const auto dense_rhs = as_eigen(rhs);
  const dense_matrix normal_matrix = dense.adjoint() * dense;
  const dense_matrix normal_rhs = dense.adjoint() * dense_rhs;
  // See the vector-RHS overload above: normal equations require an
  // invertibility check, which PartialPivLU cannot provide in Eigen 3.4.
  Eigen::FullPivLU<dense_matrix> factorization(normal_matrix);
  if (!factorization.isInvertible()) {
    return false;
  }

  as_eigen(output) = factorization.solve(normal_rhs);
  return true;
}

template <typename T>
[[nodiscard]] bool solve_least_squares_normal_equations_cholesky(const ksj::array::PooledMatrix<T>& matrix,
                                                                 const ksj::array::PooledMatrix<T>& rhs,
                                                                 ksj::array::PooledMatrix<T>& output) {
  if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() || matrix.empty()) {
    return false;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const auto dense = as_eigen(matrix);
  const auto dense_rhs = as_eigen(rhs);
  const dense_matrix normal_matrix = dense.adjoint() * dense;
  const dense_matrix normal_rhs = dense.adjoint() * dense_rhs;
  Eigen::LLT<dense_matrix> factorization(normal_matrix);
  if (factorization.info() != Eigen::Success) {
    return false;
  }

  as_eigen(output) = factorization.solve(normal_rhs);
  return true;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T>
solve_least_squares_normal_equations(const ksj::array::PooledMatrix<T>& matrix,
                                     const ksj::array::PooledVector<T>& rhs) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  if (!solve_least_squares_normal_equations(matrix, rhs, output)) {
    throw std::invalid_argument("least-squares normal-equations factorization failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T>
solve_least_squares_normal_equations_cholesky(const ksj::array::PooledMatrix<T>& matrix,
                                              const ksj::array::PooledVector<T>& rhs) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.cols());
  if (!solve_least_squares_normal_equations_cholesky(matrix, rhs, output)) {
    throw std::invalid_argument("least-squares normal-equations cholesky factorization failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T>
solve_least_squares_normal_equations(const ksj::array::PooledMatrix<T>& matrix,
                                     const ksj::array::PooledMatrix<T>& rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  if (!solve_least_squares_normal_equations(matrix, rhs, output)) {
    throw std::invalid_argument("least-squares normal-equations factorization failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T>
solve_least_squares_normal_equations_cholesky(const ksj::array::PooledMatrix<T>& matrix,
                                              const ksj::array::PooledMatrix<T>& rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), rhs.cols());
  if (!solve_least_squares_normal_equations_cholesky(matrix, rhs, output)) {
    throw std::invalid_argument("least-squares normal-equations cholesky factorization failed");
  }
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,
                                                              const ksj::array::PooledVector<T>& rhs,
                                                              const LeastSquaresSolver solver) {
  switch (solver) {
    case LeastSquaresSolver::qr:
      return solve_qr(matrix, rhs);
    case LeastSquaresSolver::rank_revealing_qr:
      return solve_least_squares_rank_revealing_qr(matrix, rhs);
    case LeastSquaresSolver::svd:
      return solve_least_squares_svd(matrix, rhs);
    case LeastSquaresSolver::normal_equations:
      return solve_least_squares_normal_equations(matrix, rhs);
    case LeastSquaresSolver::normal_equations_cholesky:
      return solve_least_squares_normal_equations_cholesky(matrix, rhs);
  }
  throw std::invalid_argument("unknown least-squares solver");
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,
                                                              const ksj::array::PooledMatrix<T>& rhs,
                                                              const LeastSquaresSolver solver) {
  switch (solver) {
    case LeastSquaresSolver::qr:
      return solve_qr(matrix, rhs);
    case LeastSquaresSolver::rank_revealing_qr:
      return solve_least_squares_rank_revealing_qr(matrix, rhs);
    case LeastSquaresSolver::svd:
      return solve_least_squares_svd(matrix, rhs);
    case LeastSquaresSolver::normal_equations:
      return solve_least_squares_normal_equations(matrix, rhs);
    case LeastSquaresSolver::normal_equations_cholesky:
      return solve_least_squares_normal_equations_cholesky(matrix, rhs);
  }
  throw std::invalid_argument("unknown least-squares solver");
}

template <typename T>
[[nodiscard]] bool solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,
                                       const ksj::array::PooledVector<T>& rhs, ksj::array::PooledVector<T>& output,
                                       const LeastSquaresSolver solver) {
  switch (solver) {
    case LeastSquaresSolver::qr:
      {
        LeastSquaresQrWorkspace<T> workspace;
        return solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                        workspace);
      }
    case LeastSquaresSolver::rank_revealing_qr:
      {
        const auto solution = solve_least_squares_rank_revealing_qr(matrix, rhs);
        if (solution.size() != output.size()) {
          return false;
        }
        ksj::array::copy(solution.view(), output.view());
        return true;
      }
    case LeastSquaresSolver::svd:
      return solve_least_squares_svd(matrix, rhs, output);
    case LeastSquaresSolver::normal_equations:
      return solve_least_squares_normal_equations(matrix, rhs, output);
    case LeastSquaresSolver::normal_equations_cholesky:
      return solve_least_squares_normal_equations_cholesky(matrix, rhs, output);
  }
  return false;
}

template <typename T>
[[nodiscard]] bool solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,
                                       const ksj::array::PooledMatrix<T>& rhs, ksj::array::PooledMatrix<T>& output,
                                       const LeastSquaresSolver solver) {
  switch (solver) {
    case LeastSquaresSolver::qr:
      {
        LeastSquaresQrWorkspace<T> workspace;
        return solve_qr(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                        workspace);
      }
    case LeastSquaresSolver::rank_revealing_qr:
      {
        const auto solution = solve_least_squares_rank_revealing_qr(matrix, rhs);
        if (solution.rows() != output.rows() || solution.cols() != output.cols()) {
          return false;
        }
        ksj::array::copy(solution.view(), output.view());
        return true;
      }
    case LeastSquaresSolver::svd:
      return solve_least_squares_svd(matrix, rhs, output);
    case LeastSquaresSolver::normal_equations:
      return solve_least_squares_normal_equations(matrix, rhs, output);
    case LeastSquaresSolver::normal_equations_cholesky:
      return solve_least_squares_normal_equations_cholesky(matrix, rhs, output);
  }
  return false;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> solve_small(const ksj::array::PooledMatrix<T>& matrix,
                                                      const ksj::array::PooledVector<T>& rhs) {
  if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || matrix.rows() == 0U || matrix.rows() > 4U) {
    throw std::invalid_argument("solve_small supports square systems of size 1..4");
  }

  auto output = ksj::array::make_pooled_vector<T>(rhs.size());
  if (matrix.rows() == 1U) {
    output(0) = rhs(0) / matrix(0, 0);
    return output;
  }
  if (matrix.rows() == 2U) {
    const auto a = matrix(0, 0);
    const auto b = matrix(0, 1);
    const auto c = matrix(1, 0);
    const auto d = matrix(1, 1);
    const auto determinant = a * d - b * c;
    if (determinant == T{}) {
      throw std::invalid_argument("solve_small matrix is singular");
    }
    output(0) = (d * rhs(0) - b * rhs(1)) / determinant;
    output(1) = (-c * rhs(0) + a * rhs(1)) / determinant;
    return output;
  }
  using real_type = ksj::array::real_scalar_t<T>;
  std::array<std::array<T, 4>, 4> coefficients{};
  std::array<T, 4> values{};
  const auto size = matrix.rows();

  for (std::size_t row = 0; row < size; ++row) {
    values[row] = rhs(row);
    for (std::size_t col = 0; col < size; ++col) {
      coefficients[row][col] = matrix(row, col);
    }
  }

  for (std::size_t col = 0; col < size; ++col) {
    auto pivot = col;
    auto pivot_abs = static_cast<real_type>(std::abs(coefficients[col][col]));
    for (std::size_t row = col + 1U; row < size; ++row) {
      const auto row_abs = static_cast<real_type>(std::abs(coefficients[row][col]));
      if (row_abs > pivot_abs) {
        pivot = row;
        pivot_abs = row_abs;
      }
    }
    if (pivot_abs == real_type{}) {
      throw std::invalid_argument("solve_small matrix is singular");
    }
    if (pivot != col) {
      std::swap(coefficients[pivot], coefficients[col]);
      std::swap(values[pivot], values[col]);
    }

    const auto pivot_value = coefficients[col][col];
    for (std::size_t row = col + 1U; row < size; ++row) {
      const auto factor = coefficients[row][col] / pivot_value;
      coefficients[row][col] = T{};
      for (std::size_t inner_col = col + 1U; inner_col < size; ++inner_col) {
        coefficients[row][inner_col] -= factor * coefficients[col][inner_col];
      }
      values[row] -= factor * values[col];
    }
  }

  for (std::size_t reverse = size; reverse > 0U; --reverse) {
    const auto row = reverse - 1U;
    auto value = values[row];
    for (std::size_t col = row + 1U; col < size; ++col) {
      value -= coefficients[row][col] * output(col);
    }
    output(row) = value / coefficients[row][row];
  }
  return output;
}

#define KSJ_LINALG_EIGEN_SOLVER_WRAPPERS(T)                                                                            \
  T determinant(const ksj::array::PooledMatrix<T>& matrix) {                                                           \
    return determinant<T>(matrix);                                                                                     \
  }                                                                                                                    \
  void inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {                              \
    inverse<T>(input, output);                                                                                         \
  }                                                                                                                    \
  bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                                \
               LuFactorWorkspace<T>& workspace) {                                                                      \
    return inverse<T>(input, output, workspace);                                                                       \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> inverse(ksj::array::MatrixView<const T> input) {                                         \
    return inverse<T>(input);                                                                                          \
  }                                                                                                                    \
  void pseudo_inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                         \
                      ksj::array::real_scalar_t<T> singular_tolerance) {                                               \
    pseudo_inverse<T>(input, output, singular_tolerance);                                                              \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> pseudo_inverse(ksj::array::MatrixView<const T> input,                                    \
                                             ksj::array::real_scalar_t<T> singular_tolerance) {                        \
    return pseudo_inverse<T>(input, singular_tolerance);                                                               \
  }                                                                                                                    \
  void solve(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,                        \
             ksj::array::PooledVector<T>& output) {                                                                    \
    solve<T>(matrix, rhs, output);                                                                                     \
  }                                                                                                                    \
  ksj::array::PooledVector<T> solve(const ksj::array::PooledMatrix<T>& matrix,                                         \
                                    const ksj::array::PooledVector<T>& rhs) {                                          \
    return solve<T>(matrix, rhs);                                                                                      \
  }                                                                                                                    \
  void solve(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                        \
             ksj::array::PooledMatrix<T>& output) {                                                                    \
    solve<T>(matrix, rhs, output);                                                                                     \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> solve(const ksj::array::PooledMatrix<T>& matrix,                                         \
                                    const ksj::array::PooledMatrix<T>& rhs) {                                          \
    return solve<T>(matrix, rhs);                                                                                      \
  }                                                                                                                    \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output) {                                                                    \
    return solve_lu<T>(matrix, rhs, output);                                                                           \
  }                                                                                                                    \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace) {                                    \
    return solve_lu<T>(matrix, rhs, output, workspace);                                                                \
  }                                                                                                                    \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace) {                                    \
    return solve_lu<T>(matrix, rhs, output, workspace);                                                                \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> cholesky_lower(const ksj::array::PooledMatrix<T>& matrix) {                              \
    return cholesky_lower<T>(matrix);                                                                                  \
  }                                                                                                                    \
  ksj::array::PooledVector<T> solve_cholesky(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledVector<T>& rhs) {                                 \
    return solve_cholesky<T>(matrix, rhs);                                                                             \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> solve_cholesky(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledMatrix<T>& rhs) {                                 \
    return solve_cholesky<T>(matrix, rhs);                                                                             \
  }                                                                                                                    \
  ksj::array::PooledVector<T> solve_qr(const ksj::array::PooledMatrix<T>& matrix,                                      \
                                       const ksj::array::PooledVector<T>& rhs) {                                       \
    return solve_qr<T>(matrix, rhs);                                                                                   \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> solve_qr(const ksj::array::PooledMatrix<T>& matrix,                                      \
                                       const ksj::array::PooledMatrix<T>& rhs) {                                       \
    return solve_qr<T>(matrix, rhs);                                                                                   \
  }                                                                                                                    \
  bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LeastSquaresQrWorkspace<T>& workspace) {                             \
    return solve_qr<T>(matrix, rhs, output, workspace);                                                                \
  }                                                                                                                    \
  bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LeastSquaresQrWorkspace<T>& workspace) {                             \
    return solve_qr<T>(matrix, rhs, output, workspace);                                                                \
  }                                                                                                                    \
  bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,      \
                               ksj::array::PooledVector<T>& output) {                                                  \
    return solve_least_squares_svd<T>(matrix, rhs, output);                                                            \
  }                                                                                                                    \
  bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,      \
                               ksj::array::PooledMatrix<T>& output) {                                                  \
    return solve_least_squares_svd<T>(matrix, rhs, output);                                                            \
  }                                                                                                                    \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,            \
                               ksj::array::VectorView<T> output) {                                                     \
    return solve_least_squares_svd<T>(matrix, rhs, output);                                                            \
  }                                                                                                                    \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,            \
                               ksj::array::MatrixView<T> output) {                                                     \
    return solve_least_squares_svd<T>(matrix, rhs, output);                                                            \
  }                                                                                                                    \
  ksj::array::PooledVector<T> solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,                           \
                                                  const ksj::array::PooledVector<T>& rhs, LeastSquaresSolver solver) { \
    return solve_least_squares<T>(matrix, rhs, solver);                                                                \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> solve_least_squares(const ksj::array::PooledMatrix<T>& matrix,                           \
                                                  const ksj::array::PooledMatrix<T>& rhs, LeastSquaresSolver solver) { \
    return solve_least_squares<T>(matrix, rhs, solver);                                                                \
  }                                                                                                                    \
  bool solve_least_squares(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,          \
                           ksj::array::PooledVector<T>& output, LeastSquaresSolver solver) {                           \
    return solve_least_squares<T>(matrix, rhs, output, solver);                                                        \
  }                                                                                                                    \
  bool solve_least_squares(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,          \
                           ksj::array::PooledMatrix<T>& output, LeastSquaresSolver solver) {                           \
    return solve_least_squares<T>(matrix, rhs, output, solver);                                                        \
  }                                                                                                                    \
  ksj::array::PooledVector<T> solve_small(const ksj::array::PooledMatrix<T>& matrix,                                   \
                                          const ksj::array::PooledVector<T>& rhs) {                                    \
    return solve_small<T>(matrix, rhs);                                                                                \
  }

KSJ_LINALG_EIGEN_SOLVER_WRAPPERS(float)
KSJ_LINALG_EIGEN_SOLVER_WRAPPERS(double)
KSJ_LINALG_EIGEN_SOLVER_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_EIGEN_SOLVER_WRAPPERS(ksj::base::cf64)

#undef KSJ_LINALG_EIGEN_SOLVER_WRAPPERS

} // namespace ksj::linalg::detail::eigen
