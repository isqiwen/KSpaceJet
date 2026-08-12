#pragma once

/// Caller-owned reusable workspaces for linear-algebra algorithms with temporary dense storage needs.

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>

namespace ksj::linalg {

// Caller-owned scratch for repeated LU factorization and inversion of square matrices.
template <typename T> struct LuFactorWorkspace {
  ksj::array::PooledMatrix<T> matrix_work;
  ksj::array::PooledVector<ksj::base::i32> pivots;

  void resize(const std::size_t size) {
    matrix_work.resize(size, size);
    pivots.resize(size);
  }
};

// Caller-owned scratch for repeatedly solving square systems with matrix right-hand sides.
template <typename T> struct LuSolveWorkspace : LuFactorWorkspace<T> {
  ksj::array::PooledMatrix<T> rhs_work;

  void resize(const std::size_t size, const std::size_t rhs_cols) {
    LuFactorWorkspace<T>::resize(size);
    rhs_work.resize(size, rhs_cols);
  }
};

// Caller-owned scratch for repeated QR least-squares solves.
template <typename T> struct LeastSquaresQrWorkspace {
  ksj::array::PooledMatrix<T> matrix_work;
  ksj::array::PooledVector<T> rhs_vector_work;
  ksj::array::PooledMatrix<T> rhs_matrix_work;

  void resize_vector_rhs(const std::size_t rows, const std::size_t cols) {
    matrix_work.resize(rows, cols);
    rhs_vector_work.resize(std::max(rows, cols));
  }

  void resize_matrix_rhs(const std::size_t rows, const std::size_t cols, const std::size_t rhs_cols) {
    matrix_work.resize(rows, cols);
    rhs_matrix_work.resize(std::max(rows, cols), rhs_cols);
  }
};

// Caller-owned scratch for repeated general eigendecomposition of square matrices.
template <typename T> struct GeneralEigenWorkspace {
  using real_type = ksj::array::real_scalar_t<T>;
  using complex_type = std::complex<real_type>;

  ksj::array::PooledMatrix<T> matrix_work;
  ksj::array::PooledVector<complex_type> eigenvalues_work;
  ksj::array::PooledMatrix<complex_type> eigenvectors_work;
  ksj::array::PooledVector<real_type> real_values_work;
  ksj::array::PooledVector<real_type> imag_values_work;
  ksj::array::PooledMatrix<real_type> real_vectors_work;

  void resize(const std::size_t size) {
    matrix_work.resize(size, size);
    eigenvalues_work.resize(size);
    eigenvectors_work.resize(size, size);
    if constexpr (!ksj::array::is_complex_v<T>) {
      real_values_work.resize(size);
      imag_values_work.resize(size);
      real_vectors_work.resize(size, size);
    }
  }
};

// Caller-owned scratch for repeated left singular-vector decompositions.
template <typename T> struct LeftSingularVectorsWorkspace {
  using real_type = ksj::array::real_scalar_t<T>;

  ksj::array::PooledMatrix<T> matrix_work;
  ksj::array::PooledVector<real_type> values_work;
  ksj::array::PooledVector<real_type> superb_work;

  void resize(const std::size_t rows, const std::size_t cols) {
    matrix_work.resize(rows, cols);
    const auto value_count = std::min(rows, cols);
    values_work.resize(value_count);
    superb_work.resize(value_count > 1U ? value_count - 1U : 1U);
  }
};

// Caller-owned scratch for repeated SVD while preserving the input matrix.
template <typename T> struct SvdWorkspace {
  using real_type = ksj::array::real_scalar_t<T>;

  ksj::array::PooledMatrix<T> matrix_work;
  ksj::array::PooledVector<real_type> superb_work;

  void resize(const std::size_t rows, const std::size_t cols) {
    matrix_work.resize(rows, cols);
    const auto value_count = std::min(rows, cols);
    superb_work.resize(value_count > 1U ? value_count - 1U : 1U);
  }
};

// Caller-owned scratch for repeated SVD-based least-squares solves.
template <typename T> struct LeastSquaresSvdWorkspace {
  using real_type = ksj::array::real_scalar_t<T>;

  ksj::array::PooledMatrix<T> matrix_work;
  ksj::array::PooledVector<T> rhs_vector_work;
  ksj::array::PooledMatrix<T> rhs_matrix_work;
  ksj::array::PooledVector<real_type> singular_values_work;

  void resize_vector_rhs(const std::size_t rows, const std::size_t cols) {
    matrix_work.resize(rows, cols);
    rhs_vector_work.resize(std::max(rows, cols));
    singular_values_work.resize(std::min(rows, cols));
  }

  void resize_matrix_rhs(const std::size_t rows, const std::size_t cols, const std::size_t rhs_cols) {
    matrix_work.resize(rows, cols);
    rhs_matrix_work.resize(std::max(rows, cols), rhs_cols);
    singular_values_work.resize(std::min(rows, cols));
  }
};

} // namespace ksj::linalg
