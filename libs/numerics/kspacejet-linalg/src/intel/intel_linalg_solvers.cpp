#include "kspacejet/linalg/detail/intel/intel_linalg_solvers.hpp"
#include "intel_linalg_common.hpp"

namespace ksj::linalg::detail::intel {

template <typename T>
[[nodiscard]] bool solve_lu(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
                            ksj::array::PooledVector<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != rhs.size() ||
        !fits_lapack_int(matrix.rows())) {
      return false;
    }

    const auto size = matrix.rows();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto rhs_work = ksj::array::make_pooled_vector<T>(size);
    auto pivots = ksj::array::make_pooled_vector<lapack_int>(size);
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(size);
    const auto info = gesv(n, 1, lapack_data(matrix_work.data()), n, pivots.data(), lapack_data(rhs_work.data()), 1);
    if (info != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(rhs_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_lu(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,
                            ksj::array::PooledMatrix<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() || output.rows() != matrix.rows() ||
        output.cols() != rhs.cols() || !fits_lapack_int(matrix.rows()) || !fits_lapack_int(rhs.cols())) {
      return false;
    }

    const auto size = matrix.rows();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto rhs_work = ksj::array::make_pooled_matrix<T>(size, rhs.cols());
    auto pivots = ksj::array::make_pooled_vector<lapack_int>(size);
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(size);
    const auto nrhs = static_cast<lapack_int>(rhs.cols());
    const auto info =
      gesv(n, nrhs, lapack_data(matrix_work.data()), n, pivots.data(), lapack_data(rhs_work.data()), nrhs);
    if (info != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(rhs_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_refined(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,
                                 ksj::array::PooledMatrix<T>& output,
                                 ksj::array::real_scalar_t<T>& reciprocal_condition) {
  reciprocal_condition = ksj::array::real_scalar_t<T>{};
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.empty() || rhs.empty() || matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() ||
        output.rows() != matrix.rows() || output.cols() != rhs.cols() || !fits_lapack_int(matrix.rows()) ||
        !fits_lapack_int(rhs.cols())) {
      return false;
    }

    using real_type = ksj::array::real_scalar_t<T>;
    const auto size = matrix.rows();
    const auto rhs_cols = rhs.cols();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto factor_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto rhs_work = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    auto output_work = ksj::array::make_pooled_matrix<T>(size, rhs_cols);
    auto pivots = ksj::array::make_pooled_vector<lapack_int>(size);
    auto row_scale = ksj::array::make_pooled_vector<real_type>(size);
    auto column_scale = ksj::array::make_pooled_vector<real_type>(size);
    auto forward_error = ksj::array::make_pooled_vector<real_type>(rhs_cols);
    auto backward_error = ksj::array::make_pooled_vector<real_type>(rhs_cols);
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(size);
    const auto nrhs = static_cast<lapack_int>(rhs_cols);
    char equed = 'N';
    real_type rcond{};
    real_type reciprocal_pivot_growth{};
    const auto info = gesvx(n, nrhs, matrix_work.data(), n, factor_work.data(), n, pivots.data(), &equed,
                            row_scale.data(), column_scale.data(), rhs_work.data(), nrhs, output_work.data(), nrhs,
                            &rcond, forward_error.data(), backward_error.data(), &reciprocal_pivot_growth);
    reciprocal_condition = rcond;
    if (info != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(output_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    static_assert(std::is_same_v<lapack_int, ksj::base::i32>, "LuSolveWorkspace requires the MKL LP64 ABI");
    if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != matrix.cols() ||
        !fits_lapack_int(matrix.rows())) {
      return false;
    }

    workspace.resize(matrix.rows(), 1U);
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    as_eigen(workspace.rhs_work).col(0) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(matrix.rows());
    const auto info = gesv(n, 1, lapack_data(workspace.matrix_work.data()), n, workspace.pivots.data(),
                           lapack_data(workspace.rhs_work.data()), 1);
    if (info != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(workspace.rhs_work).col(0);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    static_assert(std::is_same_v<lapack_int, ksj::base::i32>, "LuSolveWorkspace requires the MKL LP64 ABI");
    if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() || output.rows() != matrix.rows() ||
        output.cols() != rhs.cols() || !fits_lapack_int(matrix.rows()) || !fits_lapack_int(rhs.cols())) {
      return false;
    }

    workspace.resize(matrix.rows(), rhs.cols());
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    as_eigen(workspace.rhs_work) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(matrix.rows());
    const auto nrhs = static_cast<lapack_int>(rhs.cols());
    const auto info = gesv(n, nrhs, lapack_data(workspace.matrix_work.data()), n, workspace.pivots.data(),
                           lapack_data(workspace.rhs_work.data()), nrhs);
    if (info != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(workspace.rhs_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (input.rows() != input.cols() || output.rows() != input.rows() || output.cols() != input.cols() ||
        !fits_lapack_int(input.rows())) {
      return false;
    }

    const auto size = input.rows();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto pivots = ksj::array::make_pooled_vector<lapack_int>(size);
    as_eigen(matrix_work) = as_eigen(input);

    const auto n = static_cast<lapack_int>(size);
    if (getrf(n, n, lapack_data(matrix_work.data()), n, pivots.data()) != 0) {
      return false;
    }
    if (getri(n, lapack_data(matrix_work.data()), n, pivots.data()) != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(matrix_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                           LuFactorWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    static_assert(std::is_same_v<lapack_int, ksj::base::i32>, "LuFactorWorkspace requires the MKL LP64 ABI");
    if (input.rows() != input.cols() || output.rows() != input.rows() || output.cols() != input.cols() ||
        !fits_lapack_int(input.rows())) {
      return false;
    }

    workspace.resize(input.rows());
    as_eigen(workspace.matrix_work) = as_eigen(input);
    const auto n = static_cast<lapack_int>(input.rows());
    if (getrf(n, n, lapack_data(workspace.matrix_work.data()), n, workspace.pivots.data()) != 0 ||
        getri(n, lapack_data(workspace.matrix_work.data()), n, workspace.pivots.data()) != 0) {
      return false;
    }
    as_eigen(output) = as_eigen(workspace.matrix_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool cholesky_lower(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || output.rows() != matrix.rows() || output.cols() != matrix.cols() ||
        !fits_lapack_int(matrix.rows())) {
      return false;
    }

    as_eigen(output) = as_eigen(matrix);
    const auto n = static_cast<lapack_int>(matrix.rows());
    const auto info = potrf(n, lapack_data(output.data()), n);
    if (info != 0) {
      return false;
    }

    for (std::size_t col = 1; col < output.cols(); ++col) {
      for (std::size_t row = 0; row < col; ++row) {
        output(row, col) = T{};
      }
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_cholesky(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
                                  ksj::array::PooledVector<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.size() || output.size() != rhs.size() ||
        !fits_lapack_int(matrix.rows())) {
      return false;
    }

    const auto size = matrix.rows();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto rhs_work = ksj::array::make_pooled_vector<T>(size);
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(size);
    if (potrf(n, lapack_data(matrix_work.data()), n) != 0) {
      return false;
    }
    if (potrs(n, 1, lapack_data(matrix_work.data()), n, lapack_data(rhs_work.data()), 1) != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(rhs_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_cholesky(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,
                                  ksj::array::PooledMatrix<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.rows() != rhs.rows() || output.rows() != rhs.rows() ||
        output.cols() != rhs.cols() || !fits_lapack_int(matrix.rows()) || !fits_lapack_int(rhs.cols())) {
      return false;
    }

    const auto size = matrix.rows();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto rhs_work = ksj::array::make_pooled_matrix<T>(rhs.rows(), rhs.cols());
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work) = as_eigen(rhs);

    const auto n = static_cast<lapack_int>(size);
    const auto nrhs = static_cast<lapack_int>(rhs.cols());
    if (potrf(n, lapack_data(matrix_work.data()), n) != 0) {
      return false;
    }
    if (potrs(n, nrhs, lapack_data(matrix_work.data()), n, lapack_data(rhs_work.data()), nrhs) != 0) {
      return false;
    }

    as_eigen(output) = as_eigen(rhs_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                            ksj::array::VectorView<T> output, LeastSquaresQrWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.rows() < matrix.cols() ||
        !fits_lapack_int(matrix.rows()) || !fits_lapack_int(matrix.cols())) {
      return false;
    }

    const auto rows = matrix.rows();
    const auto cols = matrix.cols();
    workspace.resize_vector_rhs(rows, cols);
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    as_eigen(workspace.rhs_vector_work).setZero();
    for (std::size_t row = 0; row < rhs.size(); ++row) {
      workspace.rhs_vector_work(row) = rhs(row);
    }

    const auto m = static_cast<lapack_int>(rows);
    const auto n = static_cast<lapack_int>(cols);
    const auto ldb = lapack_int{1};
    if (gels(m, n, 1, lapack_data(workspace.matrix_work.data()), n, lapack_data(workspace.rhs_vector_work.data()),
             ldb) != 0) {
      return false;
    }

    for (std::size_t row = 0; row < output.size(); ++row) {
      output(row) = workspace.rhs_vector_work(row);
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_qr(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,
                            ksj::array::PooledVector<T>& output) {
  LeastSquaresQrWorkspace<T> workspace;
  return solve_qr<T>(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                     workspace);
}

template <typename T>
[[nodiscard]] bool solve_qr(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                            ksj::array::MatrixView<T> output, LeastSquaresQrWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() ||
        matrix.rows() < matrix.cols() || !fits_lapack_int(matrix.rows()) || !fits_lapack_int(matrix.cols()) ||
        !fits_lapack_int(rhs.cols())) {
      return false;
    }

    const auto rows = matrix.rows();
    const auto cols = matrix.cols();
    workspace.resize_matrix_rhs(rows, cols, rhs.cols());
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    as_eigen(workspace.rhs_matrix_work).setZero();
    for (std::size_t col = 0; col < rhs.cols(); ++col) {
      for (std::size_t row = 0; row < rhs.rows(); ++row) {
        workspace.rhs_matrix_work(row, col) = rhs(row, col);
      }
    }

    const auto m = static_cast<lapack_int>(rows);
    const auto n = static_cast<lapack_int>(cols);
    const auto nrhs = static_cast<lapack_int>(rhs.cols());
    const auto ldb = nrhs;
    if (gels(m, n, nrhs, lapack_data(workspace.matrix_work.data()), n, lapack_data(workspace.rhs_matrix_work.data()),
             ldb) != 0) {
      return false;
    }

    for (std::size_t col = 0; col < output.cols(); ++col) {
      for (std::size_t row = 0; row < output.rows(); ++row) {
        output(row, col) = workspace.rhs_matrix_work(row, col);
      }
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_qr(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,
                            ksj::array::PooledMatrix<T>& output) {
  LeastSquaresQrWorkspace<T> workspace;
  return solve_qr<T>(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()), output.view(),
                     workspace);
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                                           ksj::array::VectorView<T> output, LeastSquaresSvdWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    using real_type = ksj::array::real_scalar_t<T>;
    if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty() ||
        !fits_lapack_int(matrix.rows()) || !fits_lapack_int(matrix.cols())) {
      return false;
    }

    workspace.resize_vector_rhs(matrix.rows(), matrix.cols());
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    as_eigen(workspace.rhs_vector_work).setZero();
    for (std::size_t row = 0; row < rhs.size(); ++row) {
      workspace.rhs_vector_work(row) = rhs(row);
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto ldb = lapack_int{1};
    auto rank = lapack_int{};
    const auto rcond = static_cast<real_type>(-1);
    if (gelss(rows, cols, 1, lapack_data(workspace.matrix_work.data()), cols,
              lapack_data(workspace.rhs_vector_work.data()), ldb, workspace.singular_values_work.data(), rcond,
              &rank) != 0) {
      return false;
    }

    for (std::size_t row = 0; row < output.size(); ++row) {
      output(row) = workspace.rhs_vector_work(row);
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix,
                                           const ksj::array::PooledVector<T>& rhs,
                                           ksj::array::PooledVector<T>& output) {
  LeastSquaresSvdWorkspace<T> workspace;
  return solve_least_squares_svd<T>(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace);
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,
                                           ksj::array::MatrixView<T> output, LeastSquaresSvdWorkspace<T>& workspace) {
  if constexpr (lapack_solve_scalar_v<T>) {
    using real_type = ksj::array::real_scalar_t<T>;
    if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() ||
        matrix.empty() || !fits_lapack_int(matrix.rows()) || !fits_lapack_int(matrix.cols()) ||
        !fits_lapack_int(rhs.cols())) {
      return false;
    }

    workspace.resize_matrix_rhs(matrix.rows(), matrix.cols(), rhs.cols());
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    as_eigen(workspace.rhs_matrix_work).setZero();
    for (std::size_t col = 0; col < rhs.cols(); ++col) {
      for (std::size_t row = 0; row < rhs.rows(); ++row) {
        workspace.rhs_matrix_work(row, col) = rhs(row, col);
      }
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto nrhs = static_cast<lapack_int>(rhs.cols());
    const auto ldb = nrhs;
    auto rank = lapack_int{};
    const auto rcond = static_cast<real_type>(-1);
    if (gelss(rows, cols, nrhs, lapack_data(workspace.matrix_work.data()), cols,
              lapack_data(workspace.rhs_matrix_work.data()), ldb, workspace.singular_values_work.data(), rcond,
              &rank) != 0) {
      return false;
    }

    for (std::size_t col = 0; col < output.cols(); ++col) {
      for (std::size_t row = 0; row < output.rows(); ++row) {
        output(row, col) = workspace.rhs_matrix_work(row, col);
      }
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_least_squares_svd(const ksj::array::PooledMatrix<T>& matrix,
                                           const ksj::array::PooledMatrix<T>& rhs,
                                           ksj::array::PooledMatrix<T>& output) {
  LeastSquaresSvdWorkspace<T> workspace;
  return solve_least_squares_svd<T>(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                    output.view(), workspace);
}

template <typename T>
[[nodiscard]] bool solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,
                                                         const ksj::array::PooledVector<T>& rhs,
                                                         ksj::array::PooledVector<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    using real_type = ksj::array::real_scalar_t<T>;
    if (matrix.rows() != rhs.size() || matrix.cols() != output.size() || matrix.empty() ||
        !fits_lapack_int(matrix.rows()) || !fits_lapack_int(matrix.cols())) {
      return false;
    }

    const auto rhs_rows = std::max(matrix.rows(), matrix.cols());
    auto matrix_work = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
    auto rhs_work = ksj::array::make_pooled_vector<T>(rhs_rows);
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work).setZero();
    for (std::size_t row = 0; row < rhs.size(); ++row) {
      rhs_work(row) = rhs(row);
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto ldb = lapack_int{1};
    auto pivots = ksj::array::make_pooled_vector<lapack_int>(matrix.cols());
    ksj::array::fill(pivots.view(), lapack_int{});
    auto rank = lapack_int{};
    const auto rcond = std::sqrt(std::numeric_limits<real_type>::epsilon());
    if (gelsy(rows, cols, 1, lapack_data(matrix_work.data()), cols, lapack_data(rhs_work.data()), ldb, pivots.data(),
              rcond, &rank) != 0) {
      return false;
    }

    for (std::size_t row = 0; row < output.size(); ++row) {
      output(row) = rhs_work(row);
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,
                                                         const ksj::array::PooledMatrix<T>& rhs,
                                                         ksj::array::PooledMatrix<T>& output) {
  if constexpr (lapack_solve_scalar_v<T>) {
    using real_type = ksj::array::real_scalar_t<T>;
    if (matrix.rows() != rhs.rows() || matrix.cols() != output.rows() || rhs.cols() != output.cols() ||
        matrix.empty() || !fits_lapack_int(matrix.rows()) || !fits_lapack_int(matrix.cols()) ||
        !fits_lapack_int(rhs.cols())) {
      return false;
    }

    const auto rhs_rows = std::max(matrix.rows(), matrix.cols());
    auto matrix_work = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
    auto rhs_work = ksj::array::make_pooled_matrix<T>(rhs_rows, rhs.cols());
    as_eigen(matrix_work) = as_eigen(matrix);
    as_eigen(rhs_work).setZero();
    for (std::size_t col = 0; col < rhs.cols(); ++col) {
      for (std::size_t row = 0; row < rhs.rows(); ++row) {
        rhs_work(row, col) = rhs(row, col);
      }
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto nrhs = static_cast<lapack_int>(rhs.cols());
    const auto ldb = nrhs;
    auto pivots = ksj::array::make_pooled_vector<lapack_int>(matrix.cols());
    ksj::array::fill(pivots.view(), lapack_int{});
    auto rank = lapack_int{};
    const auto rcond = std::sqrt(std::numeric_limits<real_type>::epsilon());
    if (gelsy(rows, cols, nrhs, lapack_data(matrix_work.data()), cols, lapack_data(rhs_work.data()), ldb, pivots.data(),
              rcond, &rank) != 0) {
      return false;
    }

    for (std::size_t col = 0; col < output.cols(); ++col) {
      for (std::size_t row = 0; row < output.rows(); ++row) {
        output(row, col) = rhs_work(row, col);
      }
    }
    return true;
  } else {
    return false;
  }
}

#define KSJ_LINALG_INTEL_SOLVER_WRAPPERS(T)                                                                            \
  bool solve_lu(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,                     \
                ksj::array::PooledVector<T>& output) {                                                                 \
    return solve_lu<T>(matrix, rhs, output);                                                                           \
  }                                                                                                                    \
  bool solve_lu(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                     \
                ksj::array::PooledMatrix<T>& output) {                                                                 \
    return solve_lu<T>(matrix, rhs, output);                                                                           \
  }                                                                                                                    \
  bool solve_refined(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                \
                     ksj::array::PooledMatrix<T>& output, ksj::array::real_scalar_t<T>& reciprocal_condition) {        \
    return solve_refined<T>(matrix, rhs, output, reciprocal_condition);                                                \
  }                                                                                                                    \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,                           \
                ksj::array::VectorView<T> output, LuSolveWorkspace<T>& workspace) {                                    \
    return solve_lu<T>(matrix, rhs, output, workspace);                                                                \
  }                                                                                                                    \
  bool solve_lu(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,                           \
                ksj::array::MatrixView<T> output, LuSolveWorkspace<T>& workspace) {                                    \
    return solve_lu<T>(matrix, rhs, output, workspace);                                                                \
  }                                                                                                                    \
  bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output) {                              \
    return inverse<T>(input, output);                                                                                  \
  }                                                                                                                    \
  bool inverse(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                                \
               LuFactorWorkspace<T>& workspace) {                                                                      \
    return inverse<T>(input, output, workspace);                                                                       \
  }                                                                                                                    \
  bool cholesky_lower(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output) {                \
    return cholesky_lower<T>(matrix, output);                                                                          \
  }                                                                                                                    \
  bool solve_cholesky(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,               \
                      ksj::array::PooledVector<T>& output) {                                                           \
    return solve_cholesky<T>(matrix, rhs, output);                                                                     \
  }                                                                                                                    \
  bool solve_cholesky(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,               \
                      ksj::array::PooledMatrix<T>& output) {                                                           \
    return solve_cholesky<T>(matrix, rhs, output);                                                                     \
  }                                                                                                                    \
  bool solve_qr(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& rhs,                     \
                ksj::array::PooledVector<T>& output) {                                                                 \
    return solve_qr<T>(matrix, rhs, output);                                                                           \
  }                                                                                                                    \
  bool solve_qr(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledMatrix<T>& rhs,                     \
                ksj::array::PooledMatrix<T>& output) {                                                                 \
    return solve_qr<T>(matrix, rhs, output);                                                                           \
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
                               ksj::array::VectorView<T> output, LeastSquaresSvdWorkspace<T>& workspace) {             \
    return solve_least_squares_svd<T>(matrix, rhs, output, workspace);                                                 \
  }                                                                                                                    \
  bool solve_least_squares_svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<const T> rhs,            \
                               ksj::array::MatrixView<T> output, LeastSquaresSvdWorkspace<T>& workspace) {             \
    return solve_least_squares_svd<T>(matrix, rhs, output, workspace);                                                 \
  }                                                                                                                    \
  bool solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledVector<T>& rhs,                                   \
                                             ksj::array::PooledVector<T>& output) {                                    \
    return solve_least_squares_rank_revealing_qr<T>(matrix, rhs, output);                                              \
  }                                                                                                                    \
  bool solve_least_squares_rank_revealing_qr(const ksj::array::PooledMatrix<T>& matrix,                                \
                                             const ksj::array::PooledMatrix<T>& rhs,                                   \
                                             ksj::array::PooledMatrix<T>& output) {                                    \
    return solve_least_squares_rank_revealing_qr<T>(matrix, rhs, output);                                              \
  }

KSJ_LINALG_INTEL_SOLVER_WRAPPERS(float)
KSJ_LINALG_INTEL_SOLVER_WRAPPERS(double)
KSJ_LINALG_INTEL_SOLVER_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_INTEL_SOLVER_WRAPPERS(ksj::base::cf64)

#undef KSJ_LINALG_INTEL_SOLVER_WRAPPERS

} // namespace ksj::linalg::detail::intel
