#include "kspacejet/linalg/detail/eigen/eigen_linalg_decompositions.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <algorithm>
#include <stdexcept>

#include <Eigen/Eigenvalues>
#include <Eigen/SVD>

namespace ksj::linalg::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<ksj::array::real_scalar_t<T>>
singular_values(ksj::array::MatrixView<const T> matrix) {
  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense);
  const auto values = decomposition.singularValues();
  auto output = ksj::array::make_pooled_vector<ksj::array::real_scalar_t<T>>(static_cast<std::size_t>(values.size()));
  for (Eigen::Index index = 0; index < values.size(); ++index) {
    output(static_cast<std::size_t>(index)) = values(index);
  }
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<ksj::array::real_scalar_t<T>>
singular_values(const ksj::array::PooledMatrix<T>& matrix) {
  return singular_values(ksj::array::as_const_view(matrix.view()));
}

template <typename T>
[[nodiscard]] SingularValueDecomposition<T> svd(ksj::array::MatrixView<const T> matrix, const bool full_matrices) {
  if (matrix.empty()) {
    throw std::invalid_argument("svd input must not be empty");
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  using real_type = ksj::array::real_scalar_t<T>;
  const auto rows = static_cast<Eigen::Index>(matrix.rows());
  const auto cols = static_cast<Eigen::Index>(matrix.cols());
  const auto value_count = std::min(rows, cols);
  const unsigned int flags =
    full_matrices ? (Eigen::ComputeFullU | Eigen::ComputeFullV) : (Eigen::ComputeThinU | Eigen::ComputeThinV);

  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, flags);
  if (decomposition.info() != Eigen::Success) {
    throw std::invalid_argument("svd decomposition failed");
  }

  SingularValueDecomposition<T> output{
    ksj::array::make_pooled_matrix<T>(matrix.rows(),
                                      full_matrices ? matrix.rows() : static_cast<std::size_t>(value_count)),
    ksj::array::make_pooled_vector<real_type>(static_cast<std::size_t>(value_count)),
    ksj::array::make_pooled_matrix<T>(full_matrices ? matrix.cols() : static_cast<std::size_t>(value_count),
                                      matrix.cols()),
  };

  as_eigen(output.u) = decomposition.matrixU();
  as_eigen(output.singular_values) = decomposition.singularValues();
  as_eigen(output.v_adjoint) = decomposition.matrixV().adjoint();
  return output;
}

template <typename T>
[[nodiscard]] SingularValueDecomposition<T> svd(const ksj::array::PooledMatrix<T>& matrix, const bool full_matrices) {
  return svd(ksj::array::as_const_view(matrix.view()), full_matrices);
}

template <std::size_t Rows, typename T>
[[nodiscard]] bool left_singular_vectors_fixed_row_gram(ksj::array::MatrixView<const T> matrix,
                                                        ksj::array::MatrixView<T> output) {
  if (matrix.rows() != Rows || matrix.cols() < Rows || output.rows() != Rows || output.cols() != Rows) {
    return false;
  }

  using input_matrix = Eigen::Matrix<T, static_cast<int>(Rows), Eigen::Dynamic, Eigen::RowMajor>;
  using gram_matrix = Eigen::Matrix<T, static_cast<int>(Rows), static_cast<int>(Rows), Eigen::RowMajor>;

  input_matrix dense(static_cast<Eigen::Index>(Rows), static_cast<Eigen::Index>(matrix.cols()));
  dense = as_eigen(matrix);

  const gram_matrix gram = dense * dense.adjoint();
  Eigen::SelfAdjointEigenSolver<gram_matrix> solver(gram);
  if (solver.info() != Eigen::Success) {
    throw std::invalid_argument("left_singular_vectors fixed-row eigensolve failed");
  }

  auto output_map = as_eigen(output);
  const auto eigenvectors = solver.eigenvectors();
  for (Eigen::Index col = 0; col < static_cast<Eigen::Index>(Rows); ++col) {
    output_map.col(col) = eigenvectors.col(static_cast<Eigen::Index>(Rows) - 1 - col);
  }
  return true;
}

template <typename T>
[[nodiscard]] bool left_singular_vectors_small_fixed_row_gram(ksj::array::MatrixView<const T> matrix,
                                                              ksj::array::MatrixView<T> output) {
  switch (matrix.rows()) {
    case 4U:
      return left_singular_vectors_fixed_row_gram<4U>(matrix, output);
    case 8U:
      return left_singular_vectors_fixed_row_gram<8U>(matrix, output);
    case 16U:
      return left_singular_vectors_fixed_row_gram<16U>(matrix, output);
    case 32U:
      return left_singular_vectors_fixed_row_gram<32U>(matrix, output);
    default:
      return false;
  }
}

template <typename T>
void left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output) {
  if (matrix.empty()) {
    throw std::invalid_argument("left_singular_vectors input must not be empty");
  }

  const auto value_count = std::min(matrix.rows(), matrix.cols());
  if (output.rows() != matrix.rows() || output.cols() != value_count) {
    throw std::invalid_argument("left_singular_vectors output dimension mismatch");
  }

  if (matrix.rows() <= matrix.cols()) {
    if (left_singular_vectors_small_fixed_row_gram(matrix, output)) {
      return;
    }

    using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    const dense_matrix dense = as_eigen(matrix);
    const dense_matrix gram = dense * dense.adjoint();
    Eigen::SelfAdjointEigenSolver<dense_matrix> solver(gram);
    if (solver.info() != Eigen::Success) {
      throw std::invalid_argument("left_singular_vectors eigensolve failed");
    }

    auto output_map = as_eigen(output);
    for (Eigen::Index col = 0; col < static_cast<Eigen::Index>(value_count); ++col) {
      output_map.col(col) = solver.eigenvectors().col(static_cast<Eigen::Index>(value_count) - 1 - col);
    }
    return;
  }

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::JacobiSVD<dense_matrix> decomposition(dense, Eigen::ComputeThinU | Eigen::ComputeThinV);
  if (decomposition.info() != Eigen::Success) {
    throw std::invalid_argument("left_singular_vectors decomposition failed");
  }
  as_eigen(output) = decomposition.matrixU();
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> left_singular_vectors(ksj::array::MatrixView<const T> matrix) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.rows(), std::min(matrix.rows(), matrix.cols()));
  left_singular_vectors(matrix, output.view());
  return output;
}

template <typename T>
void left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output) {
  left_singular_vectors(ksj::array::as_const_view(matrix.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix) {
  return left_singular_vectors(ksj::array::as_const_view(matrix.view()));
}

template <typename T>
void self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                                      ksj::array::VectorView<ksj::array::real_scalar_t<T>> eigenvalues,
                                      ksj::array::MatrixView<T> eigenvectors) {
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

  using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
  const dense_matrix dense = as_eigen(matrix);
  Eigen::SelfAdjointEigenSolver<dense_matrix> solver(dense);
  if (solver.info() != Eigen::Success) {
    throw std::invalid_argument("self-adjoint eigensolve failed");
  }

  as_eigen(eigenvalues) = solver.eigenvalues();
  as_eigen(eigenvectors) = solver.eigenvectors();
}

template <typename T>
[[nodiscard]] SelfAdjointEigenDecomposition<T>
self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix) {
  using real_type = ksj::array::real_scalar_t<T>;
  SelfAdjointEigenDecomposition<T> output{
    ksj::array::make_pooled_vector<real_type>(matrix.rows()),
    ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols()),
  };
  self_adjoint_eigen_decomposition(matrix, output.eigenvalues.view(), output.eigenvectors.view());
  return output;
}

template <typename T>
[[nodiscard]] SelfAdjointEigenDecomposition<T>
self_adjoint_eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix) {
  return self_adjoint_eigen_decomposition(ksj::array::as_const_view(matrix.view()));
}

template <typename T>
[[nodiscard]] EigenDecomposition<T> eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix) {
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("eigen_decomposition requires a square matrix");
  }
  if (matrix.empty()) {
    throw std::invalid_argument("eigen_decomposition input must not be empty");
  }

  using complex_type = typename EigenDecomposition<T>::complex_type;
  using output_matrix = Eigen::Matrix<complex_type, Eigen::Dynamic, Eigen::Dynamic>;

  EigenDecomposition<T> output{
    ksj::array::make_pooled_vector<complex_type>(matrix.rows()),
    ksj::array::make_pooled_matrix<complex_type>(matrix.rows(), matrix.cols()),
  };

  if constexpr (ksj::array::is_complex_v<T>) {
    using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
    const dense_matrix dense = as_eigen(matrix);
    Eigen::ComplexEigenSolver<dense_matrix> solver(dense);
    if (solver.info() != Eigen::Success) {
      throw std::invalid_argument("complex eigensolve failed");
    }
    as_eigen(output.eigenvalues) = solver.eigenvalues();
    as_eigen(output.eigenvectors) = solver.eigenvectors();
  } else {
    using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
    const dense_matrix dense = as_eigen(matrix);
    Eigen::EigenSolver<dense_matrix> solver(dense);
    if (solver.info() != Eigen::Success) {
      throw std::invalid_argument("eigensolve failed");
    }
    as_eigen(output.eigenvalues) = solver.eigenvalues();
    as_eigen(output.eigenvectors) = output_matrix(solver.eigenvectors());
  }

  return output;
}

template <typename T>
[[nodiscard]] bool
eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                    ksj::array::VectorView<typename GeneralEigenWorkspace<T>::complex_type> eigenvalues,
                    ksj::array::MatrixView<typename GeneralEigenWorkspace<T>::complex_type> eigenvectors,
                    GeneralEigenWorkspace<T>& workspace) {
  using complex_type = typename GeneralEigenWorkspace<T>::complex_type;
  if (matrix.rows() != matrix.cols() || matrix.empty() || eigenvalues.size() != matrix.rows() ||
      eigenvectors.rows() != matrix.rows() || eigenvectors.cols() != matrix.cols()) {
    return false;
  }

  workspace.resize(matrix.rows());
  as_eigen(workspace.matrix_work) = as_eigen(matrix);
  if constexpr (ksj::array::is_complex_v<T>) {
    using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
    Eigen::ComplexEigenSolver<dense_matrix> solver(as_eigen(workspace.matrix_work));
    if (solver.info() != Eigen::Success) {
      return false;
    }
    as_eigen(eigenvalues) = solver.eigenvalues();
    as_eigen(eigenvectors) = solver.eigenvectors();
  } else {
    using dense_matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;
    using output_matrix = Eigen::Matrix<complex_type, Eigen::Dynamic, Eigen::Dynamic>;
    Eigen::EigenSolver<dense_matrix> solver(as_eigen(workspace.matrix_work));
    if (solver.info() != Eigen::Success) {
      return false;
    }
    as_eigen(eigenvalues) = solver.eigenvalues();
    as_eigen(eigenvectors) = output_matrix(solver.eigenvectors());
  }
  return true;
}

#define KSJ_LINALG_EIGEN_DECOMPOSITION_WRAPPERS(T)                                                                     \
  ksj::array::PooledVector<ksj::array::real_scalar_t<T>> singular_values(ksj::array::MatrixView<const T> matrix) {     \
    return singular_values<T>(matrix);                                                                                 \
  }                                                                                                                    \
  SingularValueDecomposition<T> svd(ksj::array::MatrixView<const T> matrix, bool full_matrices) {                      \
    return svd<T>(matrix, full_matrices);                                                                              \
  }                                                                                                                    \
  void left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output) {               \
    left_singular_vectors<T>(matrix, output);                                                                          \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> left_singular_vectors(ksj::array::MatrixView<const T> matrix) {                          \
    return left_singular_vectors<T>(matrix);                                                                           \
  }                                                                                                                    \
  void self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                        \
                                        ksj::array::VectorView<ksj::array::real_scalar_t<T>> eigenvalues,              \
                                        ksj::array::MatrixView<T> eigenvectors) {                                      \
    self_adjoint_eigen_decomposition<T>(matrix, eigenvalues, eigenvectors);                                            \
  }                                                                                                                    \
  SelfAdjointEigenDecomposition<T> self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix) {          \
    return self_adjoint_eigen_decomposition<T>(matrix);                                                                \
  }                                                                                                                    \
  EigenDecomposition<T> eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix) {                               \
    return eigen_decomposition<T>(matrix);                                                                             \
  }

#define KSJ_LINALG_EIGEN_GENERAL_EIGEN_WRAPPERS(T, C)                                                                  \
  bool eigen_decomposition(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<C> eigenvalues,              \
                           ksj::array::MatrixView<C> eigenvectors, GeneralEigenWorkspace<T>& workspace) {              \
    return eigen_decomposition<T>(matrix, eigenvalues, eigenvectors, workspace);                                       \
  }

KSJ_LINALG_EIGEN_DECOMPOSITION_WRAPPERS(float)
KSJ_LINALG_EIGEN_DECOMPOSITION_WRAPPERS(double)
KSJ_LINALG_EIGEN_DECOMPOSITION_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_EIGEN_DECOMPOSITION_WRAPPERS(ksj::base::cf64)
KSJ_LINALG_EIGEN_GENERAL_EIGEN_WRAPPERS(float, ksj::base::cf32)
KSJ_LINALG_EIGEN_GENERAL_EIGEN_WRAPPERS(double, ksj::base::cf64)
KSJ_LINALG_EIGEN_GENERAL_EIGEN_WRAPPERS(ksj::base::cf32, ksj::base::cf32)
KSJ_LINALG_EIGEN_GENERAL_EIGEN_WRAPPERS(ksj::base::cf64, ksj::base::cf64)

#undef KSJ_LINALG_EIGEN_DECOMPOSITION_WRAPPERS
#undef KSJ_LINALG_EIGEN_GENERAL_EIGEN_WRAPPERS

} // namespace ksj::linalg::detail::eigen
