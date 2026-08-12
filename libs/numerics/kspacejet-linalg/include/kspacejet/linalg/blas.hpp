#pragma once

/// Dense BLAS-style vector and matrix operations with explicit View input and output contracts.

#include "kspacejet/array/array.hpp"
#include "kspacejet/linalg/detail/eigen/eigen_linalg_blas.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_blas.hpp"
#include "kspacejet/linalg/detail/linalg_types.hpp"
#include "kspacejet/linalg/detail/linalg_policy.hpp"
#include "kspacejet/linalg/types.hpp"

#include <cmath>
#include <stdexcept>

namespace ksj::linalg {

template <typename T>
void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,
            ksj::array::MatrixView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (lhs.cols() != rhs.rows()) {
    throw std::invalid_argument("matmul dimension mismatch");
  }
  if (output.rows() != lhs.rows() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("matmul output dimension mismatch");
  }

  if (detail::prefer_intel_matmul(lhs.rows(), lhs.cols(), rhs.cols()) && detail::intel::matmul(lhs, rhs, output)) {
    return;
  }

  detail::eigen::matmul(lhs, rhs, output);
}

template <typename T>
void matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs, ksj::array::MatrixView<T> output,
            const T& alpha) {
  detail::require_supported_linalg_scalar<T>();
  if (lhs.cols() != rhs.rows()) {
    throw std::invalid_argument("matmul dimension mismatch");
  }
  if (output.rows() != lhs.rows() || output.cols() != rhs.cols()) {
    throw std::invalid_argument("matmul output dimension mismatch");
  }
  if (detail::prefer_intel_matmul(lhs.rows(), lhs.cols(), rhs.cols()) &&
      detail::intel::matmul(lhs, rhs, output, alpha)) {
    return;
  }
  detail::eigen::matmul(lhs, rhs, output, alpha);
}

template <typename T>
[[nodiscard]] Matrix<T> matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs) {
  auto output = ksj::array::make_pooled_matrix<T>(lhs.rows(), rhs.cols());
  matmul(lhs, rhs, output.view());
  return output;
}

template <typename T>
[[nodiscard]] Matrix<T> matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,
                               const T& alpha) {
  auto output = ksj::array::make_pooled_matrix<T>(lhs.rows(), rhs.cols());
  matmul(lhs, rhs, output.view(), alpha);
  return output;
}

template <typename T> void matmul(const Matrix<T>& lhs, const Matrix<T>& rhs, Matrix<T>& output) {
  matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), output.view());
}

template <typename T> [[nodiscard]] Matrix<T> matmul(const Matrix<T>& lhs, const Matrix<T>& rhs) {
  return matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T> [[nodiscard]] Matrix<T> matmul(const Matrix<T>& lhs, const Matrix<T>& rhs, const T& alpha) {
  return matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), alpha);
}

template <typename T>
void gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,
          ksj::array::VectorView<T> output) {
  detail::require_supported_linalg_scalar<T>();
  if (matrix.cols() != vector.size() || matrix.rows() != output.size()) {
    throw std::invalid_argument("gemv dimension mismatch");
  }

  if (detail::prefer_intel_gemv(matrix.rows(), matrix.cols()) && detail::intel::gemv(matrix, vector, output)) {
    return;
  }

  detail::eigen::gemv(matrix, vector, output);
}

template <typename T>
[[nodiscard]] Vector<T> gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.rows());
  gemv(matrix, vector, output.view());
  return output;
}

template <typename T> void gemv(const Matrix<T>& matrix, const Vector<T>& vector, Vector<T>& output) {
  gemv(matrix.view(), vector.view(), output.view());
}

template <typename T> [[nodiscard]] Vector<T> gemv(const Matrix<T>& matrix, const Vector<T>& vector) {
  auto output = ksj::array::make_pooled_vector<T>(matrix.rows());
  gemv(matrix, vector, output);
  return output;
}

template <typename T> [[nodiscard]] T dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs) {
  detail::require_supported_linalg_scalar<T>();
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("dot dimension mismatch");
  }

  T output{};
  if (detail::prefer_intel_dot<T>(lhs.size()) && detail::intel::dot(lhs, rhs, output)) {
    return output;
  }

  return detail::eigen::dot(lhs, rhs);
}

template <typename T> [[nodiscard]] T dot(const Vector<T>& lhs, const Vector<T>& rhs) {
  return dot(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T> [[nodiscard]] T dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs) {
  detail::require_supported_linalg_scalar<T>();
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("dotu dimension mismatch");
  }

  T output{};
  if (detail::prefer_intel_dot<T>(lhs.size()) && detail::intel::dotu(lhs, rhs, output)) {
    return output;
  }

  return detail::eigen::dotu(lhs, rhs);
}

template <typename T> [[nodiscard]] T dotu(const Vector<T>& lhs, const Vector<T>& rhs) {
  return dotu(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T> [[nodiscard]] ksj::array::real_scalar_t<T> squared_norm(const Vector<T>& input) {
  detail::require_supported_linalg_scalar<T>();
  return ksj::array::squared_norm(input.view());
}

template <typename T> [[nodiscard]] ksj::array::real_scalar_t<T> norm_l2(const Vector<T>& input) {
  detail::require_supported_linalg_scalar<T>();
  using real_type = ksj::array::real_scalar_t<T>;
  return static_cast<real_type>(std::sqrt(squared_norm(input)));
}

template <typename T, typename Scalar> [[nodiscard]] Vector<T> scale(const Vector<T>& input, const Scalar& scalar) {
  detail::require_supported_linalg_scalar<T>();
  auto output = ksj::array::make_pooled_vector<T>(input.size());
  const auto converted = static_cast<T>(scalar);
  if (detail::prefer_intel_blas1<T>(input.size()) &&
      detail::intel::scale(ksj::array::as_const_view(input.view()), converted, output.view())) {
    return output;
  }
  ksj::array::scale(ksj::array::as_const_view(input.view()), converted, output.view());
  return output;
}

template <typename T, typename Scalar> [[nodiscard]] Matrix<T> scale(const Matrix<T>& input, const Scalar& scalar) {
  detail::require_supported_linalg_scalar<T>();
  auto output = ksj::array::make_pooled_matrix<T>(input.rows(), input.cols());
  ksj::array::scale(ksj::array::as_const_view(input.view()), static_cast<T>(scalar), output.view());
  return output;
}

template <typename T, typename Scalar> void scale(const Vector<T>& input, Vector<T>& output, const Scalar& scalar) {
  detail::require_supported_linalg_scalar<T>();
  if (input.size() != output.size()) {
    throw std::invalid_argument("scale output dimension mismatch");
  }
  if (detail::prefer_intel_blas1<T>(input.size()) &&
      detail::intel::scale(ksj::array::as_const_view(input.view()), static_cast<T>(scalar), output.view())) {
    return;
  }
  ksj::array::scale(ksj::array::as_const_view(input.view()), static_cast<T>(scalar), output.view());
}

template <typename T, typename Scalar> void scale(const Matrix<T>& input, Matrix<T>& output, const Scalar& scalar) {
  detail::require_supported_linalg_scalar<T>();
  ksj::array::scale(ksj::array::as_const_view(input.view()), static_cast<T>(scalar), output.view());
}

template <typename Scalar, typename T>
void axpy(const Scalar& alpha, const Vector<T>& x, const Vector<T>& y, Vector<T>& output) {
  detail::require_supported_linalg_scalar<T>();
  if (x.size() != y.size() || x.size() != output.size()) {
    throw std::invalid_argument("axpy dimension mismatch");
  }
  if (detail::prefer_intel_blas1<T>(x.size()) &&
      detail::intel::axpy(static_cast<T>(alpha), ksj::array::as_const_view(x.view()),
                          ksj::array::as_const_view(y.view()), output.view())) {
    return;
  }
  ksj::array::scale_add(ksj::array::as_const_view(x.view()), static_cast<T>(alpha), ksj::array::as_const_view(y.view()),
                        output.view());
}

template <typename Scalar, typename T>
[[nodiscard]] Vector<T> axpy(const Scalar& alpha, const Vector<T>& x, const Vector<T>& y) {
  detail::require_supported_linalg_scalar<T>();
  auto output = ksj::array::make_pooled_vector<T>(x.size());
  axpy(alpha, x, y, output);
  return output;
}

template <typename T> [[nodiscard]] Matrix<T> transpose(const Matrix<T>& matrix) {
  return ksj::array::transpose(matrix);
}

template <typename T> [[nodiscard]] Matrix<T> transpose_rotated_180(const Matrix<T>& matrix) {
  return ksj::array::transpose_rotated_180(matrix);
}

template <typename T>
void hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                    const ksj::array::real_scalar_t<T> scale = ksj::array::real_scalar_t<T>{1}) {
  detail::require_supported_linalg_scalar<T>();
  if (detail::prefer_intel_hermitian_gram<T>(input.rows(), input.cols()) &&
      detail::intel::hermitian_gram(input, output, scale)) {
    return;
  }

  detail::eigen::hermitian_gram(input, output, scale);
}

template <typename T>
void hermitian_gram(const Matrix<T>& input, Matrix<T>& output,
                    const ksj::array::real_scalar_t<T> scale = ksj::array::real_scalar_t<T>{1}) {
  hermitian_gram(ksj::array::as_const_view(input.view()), output.view(), scale);
}

template <typename T>
[[nodiscard]] Matrix<T> hermitian_gram(ksj::array::MatrixView<const T> input,
                                       const ksj::array::real_scalar_t<T> scale = ksj::array::real_scalar_t<T>{1}) {
  auto output = ksj::array::make_pooled_matrix<T>(input.cols(), input.cols());
  hermitian_gram(input, output.view(), scale);
  return output;
}

template <typename T>
[[nodiscard]] Matrix<T> hermitian_gram(const Matrix<T>& input,
                                       const ksj::array::real_scalar_t<T> scale = ksj::array::real_scalar_t<T>{1}) {
  return hermitian_gram(ksj::array::as_const_view(input.view()), scale);
}

template <typename T>
[[nodiscard]] ksj::array::real_scalar_t<T> diagonal_abs_sum(ksj::array::MatrixView<const T> matrix) {
  detail::require_supported_linalg_scalar<T>();
  return detail::eigen::diagonal_abs_sum(matrix);
}

template <typename T> [[nodiscard]] ksj::array::real_scalar_t<T> diagonal_abs_sum(const Matrix<T>& matrix) {
  return diagonal_abs_sum(ksj::array::as_const_view(matrix.view()));
}

template <typename T> void add_to_diagonal(ksj::array::MatrixView<T> matrix, const T& value) {
  detail::require_supported_linalg_scalar<T>();
  detail::eigen::add_to_diagonal(matrix, value);
}

template <typename T> void add_to_diagonal(Matrix<T>& matrix, const T& value) {
  add_to_diagonal(matrix.view(), value);
}

} // namespace ksj::linalg
