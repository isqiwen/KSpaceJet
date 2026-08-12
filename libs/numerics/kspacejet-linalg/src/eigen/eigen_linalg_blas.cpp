#include "kspacejet/linalg/detail/eigen/eigen_linalg_blas.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/linalg/types.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <cmath>
#include <complex>
#include <stdexcept>

namespace ksj::linalg::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;
}

template <typename T>
void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,
            ksj::array::MatrixView<T> output) {
  if (lhs.cols() != rhs.rows()) {
    throw std::invalid_argument("matmul dimension mismatch");
  }
  if (output.rows() != lhs.rows() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("matmul output dimension mismatch");
  }

  as_eigen(output) = as_eigen(lhs) * as_eigen(rhs);
}

template <typename T>
void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs, ksj::array::MatrixView<T> output,
            const T& alpha) {
  if (lhs.cols() != rhs.rows()) {
    throw std::invalid_argument("matmul dimension mismatch");
  }
  if (output.rows() != lhs.rows() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("matmul output dimension mismatch");
  }

  as_eigen(output) = alpha * (as_eigen(lhs) * as_eigen(rhs));
}

template <typename T>
void matmul(const ksj::array::PooledMatrix<T>& lhs, const ksj::array::PooledMatrix<T>& rhs,
            ksj::array::PooledMatrix<T>& output) {
  matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> matmul(ksj::array::MatrixView<const T> lhs,
                                                 ksj::array::MatrixView<const T> rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(lhs.rows(), rhs.cols());
  matmul(lhs, rhs, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> matmul(const ksj::array::PooledMatrix<T>& lhs,
                                                 const ksj::array::PooledMatrix<T>& rhs) {
  return matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T>
void gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,
          ksj::array::VectorView<T> output) {
  if (matrix.cols() != vector.size()) {
    throw std::invalid_argument("gemv dimension mismatch");
  }
  if (output.size() != matrix.rows()) {
    throw std::invalid_argument("gemv output dimension mismatch");
  }

  as_eigen(output) = as_eigen(matrix) * as_eigen(vector);
}

template <typename T>
void gemv(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& vector,
          ksj::array::PooledVector<T>& output) {
  gemv(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(vector.view()), output.view());
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> gemv(ksj::array::MatrixView<const T> matrix,
                                               ksj::array::VectorView<const T> vector) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.rows());
  gemv(matrix, vector, output.view());
  return output;
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> gemv(const ksj::array::PooledMatrix<T>& matrix,
                                               const ksj::array::PooledVector<T>& vector) {
  return gemv(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(vector.view()));
}

template <typename T> [[nodiscard]] T dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs) {
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("dot dimension mismatch");
  }
  return as_eigen(lhs).dot(as_eigen(rhs));
}

template <typename T>
[[nodiscard]] T dot(const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs) {
  return dot(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T> [[nodiscard]] T dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs) {
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("dotu dimension mismatch");
  }
  return (as_eigen(lhs).array() * as_eigen(rhs).array()).sum();
}

template <typename T>
[[nodiscard]] T dotu(const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs) {
  return dotu(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T>
[[nodiscard]] ksj::array::real_scalar_t<T> squared_norm(const ksj::array::PooledVector<T>& input) {
  return static_cast<ksj::array::real_scalar_t<T>>(as_eigen(input).squaredNorm());
}

template <typename T> [[nodiscard]] ksj::array::real_scalar_t<T> norm_l2(const ksj::array::PooledVector<T>& input) {
  using real_type = ksj::array::real_scalar_t<T>;
  return static_cast<real_type>(std::sqrt(squared_norm(input)));
}

template <typename T, typename Scalar>
void scale(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output, const Scalar& scalar) {
  if (input.size() != output.size()) {
    throw std::invalid_argument("scale output dimension mismatch");
  }
  as_eigen(output) = as_eigen(input) * scalar;
}

template <typename T, typename Scalar>
void scale(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output, const Scalar& scalar) {
  if (input.rows() != output.rows() || input.cols() != output.cols()) {
    throw std::invalid_argument("scale output dimension mismatch");
  }
  as_eigen(output) = as_eigen(input) * scalar;
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledVector<T> scale(const ksj::array::PooledVector<T>& input, const Scalar& scalar) {
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  scale(input, output, scalar);
  return output;
}

template <typename T, typename Scalar>
[[nodiscard]] ksj::array::PooledMatrix<T> scale(const ksj::array::PooledMatrix<T>& input, const Scalar& scalar) {
  auto output = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
  scale(input, output, scalar);
  return output;
}

template <typename Scalar, typename T>
void axpy(const Scalar& alpha, const ksj::array::PooledVector<T>& x, const ksj::array::PooledVector<T>& y,
          ksj::array::PooledVector<T>& output) {
  if (x.size() != y.size() || x.size() != output.size()) {
    throw std::invalid_argument("axpy dimension mismatch");
  }
  as_eigen(output) = alpha * as_eigen(x) + as_eigen(y);
}

template <typename Scalar, typename T>
[[nodiscard]] ksj::array::PooledVector<T> axpy(const Scalar& alpha, const ksj::array::PooledVector<T>& x,
                                               const ksj::array::PooledVector<T>& y) {
  auto output = ksj::array::make_pooled_vector<T>(x.size());
  axpy(alpha, x, y, output);
  return output;
}

template <typename T> void transpose(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output) {
  if (output.rows() != matrix.cols() || output.cols() != matrix.rows()) {
    throw std::invalid_argument("transpose output dimension mismatch");
  }
  as_eigen(output) = as_eigen(matrix).transpose();
}

template <typename T> [[nodiscard]] ksj::array::PooledMatrix<T> transpose(const ksj::array::PooledMatrix<T>& matrix) {
  auto output = ksj::array::make_pooled_matrix<T>(matrix.cols(), matrix.rows());
  transpose(matrix, output);
  return output;
}

template <typename T> [[nodiscard]] T conjugate_if_complex(const T& value) {
  if constexpr (ksj::array::is_complex_v<T>) {
    return std::conj(value);
  } else {
    return value;
  }
}

template <typename T>
void hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                    const ksj::array::real_scalar_t<T> scale) {
  if (input.empty()) {
    throw std::invalid_argument("hermitian_gram input must not be empty");
  }
  if (output.rows() != input.cols() || output.cols() != input.cols()) {
    throw std::invalid_argument("hermitian_gram output dimension mismatch");
  }

  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      T sum{};
      for (std::size_t sample = 0; sample < input.rows(); ++sample) {
        sum += conjugate_if_complex(input(sample, row)) * input(sample, col);
      }
      output(row, col) = sum * scale;
    }
  }
}

template <typename T>
[[nodiscard]] ksj::array::real_scalar_t<T> diagonal_abs_sum(ksj::array::MatrixView<const T> matrix) {
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("diagonal_abs_sum requires a square matrix");
  }

  ksj::array::real_scalar_t<T> sum{};
  for (std::size_t index = 0; index < matrix.rows(); ++index) {
    sum += static_cast<ksj::array::real_scalar_t<T>>(std::abs(matrix(index, index)));
  }
  return sum;
}

template <typename T> void add_to_diagonal(ksj::array::MatrixView<T> matrix, const T& value) {
  if (matrix.rows() != matrix.cols()) {
    throw std::invalid_argument("add_to_diagonal requires a square matrix");
  }

  for (std::size_t index = 0; index < matrix.rows(); ++index) {
    matrix(index, index) += value;
  }
}

#define KSJ_LINALG_EIGEN_BLAS_WRAPPERS(T)                                                                              \
  void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output) {                                                                      \
    matmul<T>(lhs, rhs, output);                                                                                       \
  }                                                                                                                    \
  void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output, const T& alpha) {                                                      \
    matmul<T>(lhs, rhs, output, alpha);                                                                                \
  }                                                                                                                    \
  void gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,                            \
            ksj::array::VectorView<T> output) {                                                                        \
    gemv<T>(matrix, vector, output);                                                                                   \
  }                                                                                                                    \
  T dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs) {                                    \
    return dot<T>(lhs, rhs);                                                                                           \
  }                                                                                                                    \
  T dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs) {                                   \
    return dotu<T>(lhs, rhs);                                                                                          \
  }                                                                                                                    \
  ksj::array::real_scalar_t<T> squared_norm(const ksj::array::PooledVector<T>& input) {                                \
    return squared_norm<T>(input);                                                                                     \
  }                                                                                                                    \
  ksj::array::real_scalar_t<T> norm_l2(const ksj::array::PooledVector<T>& input) {                                     \
    return norm_l2<T>(input);                                                                                          \
  }                                                                                                                    \
  void transpose(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output) {                     \
    transpose<T>(matrix, output);                                                                                      \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> transpose(const ksj::array::PooledMatrix<T>& matrix) {                                   \
    return ksj::linalg::detail::eigen::transpose<T>(matrix);                                                           \
  }                                                                                                                    \
  void hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                         \
                      ksj::array::real_scalar_t<T> scale) {                                                            \
    hermitian_gram<T>(input, output, scale);                                                                           \
  }                                                                                                                    \
  ksj::array::real_scalar_t<T> diagonal_abs_sum(ksj::array::MatrixView<const T> matrix) {                              \
    return diagonal_abs_sum<T>(matrix);                                                                                \
  }                                                                                                                    \
  void add_to_diagonal(ksj::array::MatrixView<T> matrix, const T& value) {                                             \
    add_to_diagonal<T>(matrix, value);                                                                                 \
  }

#define KSJ_LINALG_EIGEN_SCALE_WRAPPERS(T, S)                                                                          \
  void scale(const ksj::array::PooledVector<T>& input, ksj::array::PooledVector<T>& output, const S& scalar) {         \
    scale<T, S>(input, output, scalar);                                                                                \
  }                                                                                                                    \
  void scale(const ksj::array::PooledMatrix<T>& input, ksj::array::PooledMatrix<T>& output, const S& scalar) {         \
    scale<T, S>(input, output, scalar);                                                                                \
  }                                                                                                                    \
  ksj::array::PooledVector<T> scale(const ksj::array::PooledVector<T>& input, const S& scalar) {                       \
    return scale<T, S>(input, scalar);                                                                                 \
  }                                                                                                                    \
  ksj::array::PooledMatrix<T> scale(const ksj::array::PooledMatrix<T>& input, const S& scalar) {                       \
    return scale<T, S>(input, scalar);                                                                                 \
  }                                                                                                                    \
  void axpy(const S& alpha, const ksj::array::PooledVector<T>& x, const ksj::array::PooledVector<T>& y,                \
            ksj::array::PooledVector<T>& output) {                                                                     \
    axpy<S, T>(alpha, x, y, output);                                                                                   \
  }                                                                                                                    \
  ksj::array::PooledVector<T> axpy(const S& alpha, const ksj::array::PooledVector<T>& x,                               \
                                   const ksj::array::PooledVector<T>& y) {                                             \
    return axpy<S, T>(alpha, x, y);                                                                                    \
  }

KSJ_LINALG_EIGEN_BLAS_WRAPPERS(float)
KSJ_LINALG_EIGEN_BLAS_WRAPPERS(double)
KSJ_LINALG_EIGEN_BLAS_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_EIGEN_BLAS_WRAPPERS(ksj::base::cf64)
KSJ_LINALG_EIGEN_SCALE_WRAPPERS(float, float)
KSJ_LINALG_EIGEN_SCALE_WRAPPERS(double, double)
KSJ_LINALG_EIGEN_SCALE_WRAPPERS(ksj::base::cf32, ksj::base::cf32)
KSJ_LINALG_EIGEN_SCALE_WRAPPERS(ksj::base::cf32, float)
KSJ_LINALG_EIGEN_SCALE_WRAPPERS(ksj::base::cf64, ksj::base::cf64)
KSJ_LINALG_EIGEN_SCALE_WRAPPERS(ksj::base::cf64, double)

#undef KSJ_LINALG_EIGEN_BLAS_WRAPPERS
#undef KSJ_LINALG_EIGEN_SCALE_WRAPPERS

} // namespace ksj::linalg::detail::eigen
