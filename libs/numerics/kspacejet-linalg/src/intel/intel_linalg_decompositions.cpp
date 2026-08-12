#include "kspacejet/linalg/detail/intel/intel_linalg_decompositions.hpp"
#include "intel_linalg_common.hpp"

namespace ksj::linalg::detail::intel {

template <typename T> [[nodiscard]] bool can_svd_in_place_view(ksj::array::MatrixView<T> matrix) {
  if constexpr (lapack_solve_scalar_v<T>) {
    return !matrix.empty() && matrix.col_stride() == 1U && fits_lapack_int(matrix.rows()) &&
           fits_lapack_int(matrix.cols()) && fits_lapack_int(matrix.row_stride());
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool svd_output_supported(ksj::array::MatrixView<T> matrix, ksj::array::MatrixView<T> u,
                                        ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                                        ksj::array::MatrixView<T> v_adjoint, const bool full_matrices) {
  const auto value_count = std::min(matrix.rows(), matrix.cols());
  const auto expected_u_cols = full_matrices ? matrix.rows() : value_count;
  const auto expected_v_rows = full_matrices ? matrix.cols() : value_count;
  return value_count != 0U && u.rows() == matrix.rows() && u.cols() == expected_u_cols &&
         values.size() == value_count && v_adjoint.rows() == expected_v_rows && v_adjoint.cols() == matrix.cols() &&
         values.is_contiguous() && u.col_stride() == 1U && v_adjoint.col_stride() == 1U &&
         fits_lapack_int(u.row_stride()) && fits_lapack_int(v_adjoint.row_stride()) &&
         !ksj::array::detail::views_may_overlap(matrix, u) && !ksj::array::detail::views_may_overlap(matrix, v_adjoint);
}

template <typename T>
[[nodiscard]] bool
svd_in_place_impl(ksj::array::MatrixView<T> matrix, ksj::array::MatrixView<T> u,
                  ksj::array::VectorView<ksj::array::real_scalar_t<T>> values, ksj::array::MatrixView<T> v_adjoint,
                  ksj::array::VectorView<ksj::array::real_scalar_t<T>> superb, const bool full_matrices) {
  if constexpr (lapack_solve_scalar_v<T>) {
    if (!can_svd_in_place_view(matrix) || !svd_output_supported(matrix, u, values, v_adjoint, full_matrices) ||
        !superb.is_contiguous() || superb.size() != (values.size() > 1U ? values.size() - 1U : 1U)) {
      return false;
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto lda = static_cast<lapack_int>(matrix.row_stride());
    const auto ldu = static_cast<lapack_int>(u.row_stride());
    const auto ldvt = static_cast<lapack_int>(v_adjoint.row_stride());
    const auto job = full_matrices ? 'A' : 'S';
    const auto info = gesvd(job, job, rows, cols, lapack_data(matrix.data()), lda, values.data(), lapack_data(u.data()),
                            ldu, lapack_data(v_adjoint.data()), ldvt, superb.data());
    return info == 0;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool svd_in_place_impl(ksj::array::MatrixView<T> matrix, ksj::array::PooledMatrix<T>& u,
                                     ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,
                                     ksj::array::PooledMatrix<T>& v_adjoint, const bool full_matrices) {
  auto superb =
    ksj::array::make_pooled_vector<ksj::array::real_scalar_t<T>>(values.size() > 1U ? values.size() - 1U : 1U);
  return svd_in_place_impl(matrix, u.view(), values.view(), v_adjoint.view(), superb.view(), full_matrices);
}

template <typename T>
[[nodiscard]] bool singular_values(ksj::array::MatrixView<const T> matrix, ksj::array::PooledVector<T>& output) {
  if constexpr (lapack_scalar_v<T>) {
    const auto value_count = std::min(matrix.rows(), matrix.cols());
    if (value_count == 0U || output.size() != value_count || !fits_lapack_int(matrix.rows()) ||
        !fits_lapack_int(matrix.cols())) {
      return false;
    }

    auto matrix_work = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
    ksj::array::copy(matrix, matrix_work.view());
    auto superb = ksj::array::make_pooled_vector<T>(value_count > 1U ? value_count - 1U : 1U);

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto info = gesvd(rows, cols, matrix_work.data(), cols, output.data(), superb.data());
    return info == 0;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool singular_values(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledVector<T>& output) {
  return singular_values<T>(ksj::array::as_const_view(matrix.view()), output);
}

template <typename Real>
[[nodiscard]] bool singular_values(ksj::array::MatrixView<const std::complex<Real>> matrix,
                                   ksj::array::PooledVector<Real>& output) {
  if constexpr (std::is_same_v<Real, float> || std::is_same_v<Real, double>) {
    const auto value_count = std::min(matrix.rows(), matrix.cols());
    if (value_count == 0U || output.size() != value_count || !fits_lapack_int(matrix.rows()) ||
        !fits_lapack_int(matrix.cols())) {
      return false;
    }

    auto matrix_work = ksj::array::make_pooled_matrix<std::complex<Real>>(matrix.rows(), matrix.cols());
    ksj::array::copy(matrix, matrix_work.view());
    auto superb = ksj::array::make_pooled_vector<Real>(value_count > 1U ? value_count - 1U : 1U);

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto info = gesvd('N', 'N', rows, cols, lapack_complex_cast(matrix_work.data()), cols, output.data(), nullptr,
                            rows, nullptr, cols, superb.data());
    return info == 0;
  } else {
    return false;
  }
}

template <typename Real>
[[nodiscard]] bool singular_values(const ksj::array::PooledMatrix<std::complex<Real>>& matrix,
                                   ksj::array::PooledVector<Real>& output) {
  return singular_values<Real>(ksj::array::as_const_view(matrix.view()), output);
}

template <typename T>
[[nodiscard]] bool svd_in_place(ksj::array::MatrixView<T> matrix, ksj::array::PooledMatrix<T>& u,
                                ksj::array::PooledVector<T>& values, ksj::array::PooledMatrix<T>& v_adjoint,
                                const bool full_matrices) {
  return svd_in_place_impl<T>(matrix, u, values, v_adjoint, full_matrices);
}

template <typename T>
[[nodiscard]] bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::PooledMatrix<T>& u,
                       ksj::array::PooledVector<T>& values, ksj::array::PooledMatrix<T>& v_adjoint,
                       const bool full_matrices) {
  if constexpr (lapack_scalar_v<T>) {
    auto matrix_work = ksj::array::make_pooled_matrix<T>(matrix.rows(), matrix.cols());
    ksj::array::copy(matrix, matrix_work.view());
    return svd_in_place(matrix_work.view(), u, values, v_adjoint, full_matrices);
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                       ksj::array::VectorView<T> values, ksj::array::MatrixView<T> v_adjoint,
                       SvdWorkspace<T>& workspace, const bool full_matrices) {
  if constexpr (lapack_scalar_v<T>) {
    const auto value_count = std::min(matrix.rows(), matrix.cols());
    if (value_count == 0U || workspace.matrix_work.rows() != matrix.rows() ||
        workspace.matrix_work.cols() != matrix.cols() ||
        workspace.superb_work.size() != (value_count > 1U ? value_count - 1U : 1U)) {
      return false;
    }
    ksj::array::copy(matrix, workspace.matrix_work.view());
    return svd_in_place_impl(workspace.matrix_work.view(), u, values, v_adjoint, workspace.superb_work.view(),
                             full_matrices);
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool svd(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& u,
                       ksj::array::PooledVector<T>& values, ksj::array::PooledMatrix<T>& v_adjoint,
                       const bool full_matrices) {
  return svd<T>(ksj::array::as_const_view(matrix.view()), u, values, v_adjoint, full_matrices);
}

template <typename Real>
[[nodiscard]] bool svd_in_place(ksj::array::MatrixView<std::complex<Real>> matrix,
                                ksj::array::PooledMatrix<std::complex<Real>>& u, ksj::array::PooledVector<Real>& values,
                                ksj::array::PooledMatrix<std::complex<Real>>& v_adjoint, const bool full_matrices) {
  return svd_in_place_impl<std::complex<Real>>(matrix, u, values, v_adjoint, full_matrices);
}

template <typename Real>
[[nodiscard]] bool svd(ksj::array::MatrixView<const std::complex<Real>> matrix,
                       ksj::array::PooledMatrix<std::complex<Real>>& u, ksj::array::PooledVector<Real>& values,
                       ksj::array::PooledMatrix<std::complex<Real>>& v_adjoint, const bool full_matrices) {
  if constexpr (std::is_same_v<Real, float> || std::is_same_v<Real, double>) {
    auto matrix_work = ksj::array::make_pooled_matrix<std::complex<Real>>(matrix.rows(), matrix.cols());
    ksj::array::copy(matrix, matrix_work.view());
    return svd_in_place<Real>(matrix_work.view(), u, values, v_adjoint, full_matrices);
  } else {
    return false;
  }
}

template <typename Real>
[[nodiscard]] bool svd(ksj::array::MatrixView<const std::complex<Real>> matrix,
                       ksj::array::MatrixView<std::complex<Real>> u, ksj::array::VectorView<Real> values,
                       ksj::array::MatrixView<std::complex<Real>> v_adjoint,
                       SvdWorkspace<std::complex<Real>>& workspace, const bool full_matrices) {
  if constexpr (std::is_same_v<Real, float> || std::is_same_v<Real, double>) {
    const auto value_count = std::min(matrix.rows(), matrix.cols());
    if (value_count == 0U || workspace.matrix_work.rows() != matrix.rows() ||
        workspace.matrix_work.cols() != matrix.cols() ||
        workspace.superb_work.size() != (value_count > 1U ? value_count - 1U : 1U)) {
      return false;
    }
    ksj::array::copy(matrix, workspace.matrix_work.view());
    return svd_in_place_impl(workspace.matrix_work.view(), u, values, v_adjoint, workspace.superb_work.view(),
                             full_matrices);
  } else {
    return false;
  }
}

template <typename Real>
[[nodiscard]] bool svd(const ksj::array::PooledMatrix<std::complex<Real>>& matrix,
                       ksj::array::PooledMatrix<std::complex<Real>>& u, ksj::array::PooledVector<Real>& values,
                       ksj::array::PooledMatrix<std::complex<Real>>& v_adjoint, const bool full_matrices) {
  return svd<Real>(ksj::array::as_const_view(matrix.view()), u, values, v_adjoint, full_matrices);
}

template <typename T>
[[nodiscard]] bool left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output,
                                         LeftSingularVectorsWorkspace<T>& workspace) {
  if constexpr (lapack_scalar_v<T>) {
    const auto value_count = std::min(matrix.rows(), matrix.cols());
    if (value_count == 0U || output.rows() != matrix.rows() || output.cols() != value_count ||
        workspace.matrix_work.rows() != matrix.rows() || workspace.matrix_work.cols() != matrix.cols() ||
        workspace.values_work.size() != value_count ||
        workspace.superb_work.size() != (value_count > 1U ? value_count - 1U : 1U) || !fits_lapack_int(matrix.rows()) ||
        !fits_lapack_int(matrix.cols())) {
      return false;
    }

    if (!matrix.is_contiguous() || workspace.matrix_work.data() != matrix.data()) {
      ksj::array::copy(matrix, workspace.matrix_work.view());
    }

    auto output_work = ksj::array::PooledMatrix<T>{};
    auto lapack_output = output;
    const auto output_overlaps_input = ksj::array::detail::views_may_overlap(workspace.matrix_work.view(), output);
    const auto use_direct_output =
      output.col_stride() == 1U && fits_lapack_int(output.row_stride()) && !output_overlaps_input;
    if (!use_direct_output) {
      output_work.resize(output.rows(), output.cols());
      lapack_output = output_work.view();
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto ldu = static_cast<lapack_int>(lapack_output.row_stride());
    const auto info = gesvd('S', 'N', rows, cols, workspace.matrix_work.data(), cols, workspace.values_work.data(),
                            lapack_output.data(), ldu, nullptr, cols, workspace.superb_work.data());
    if (info != 0) {
      return false;
    }
    if (!use_direct_output) {
      ksj::array::copy(lapack_output, output);
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output,
                                         LeftSingularVectorsWorkspace<T>& workspace) {
  return left_singular_vectors<T>(ksj::array::as_const_view(matrix.view()), output.view(), workspace);
}

template <typename T>
[[nodiscard]] bool left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix,
                                         ksj::array::PooledMatrix<T>& output) {
  LeftSingularVectorsWorkspace<T> workspace;
  workspace.resize(matrix.rows(), matrix.cols());
  return left_singular_vectors(matrix, output, workspace);
}

template <typename Real>
[[nodiscard]] bool left_singular_vectors(ksj::array::MatrixView<const std::complex<Real>> matrix,
                                         ksj::array::MatrixView<std::complex<Real>> output,
                                         LeftSingularVectorsWorkspace<std::complex<Real>>& workspace) {
  if constexpr (std::is_same_v<Real, float> || std::is_same_v<Real, double>) {
    const auto value_count = std::min(matrix.rows(), matrix.cols());
    if (value_count == 0U || output.rows() != matrix.rows() || output.cols() != value_count ||
        workspace.matrix_work.rows() != matrix.rows() || workspace.matrix_work.cols() != matrix.cols() ||
        workspace.values_work.size() != value_count ||
        workspace.superb_work.size() != (value_count > 1U ? value_count - 1U : 1U) || !fits_lapack_int(matrix.rows()) ||
        !fits_lapack_int(matrix.cols())) {
      return false;
    }

    if (!matrix.is_contiguous() || workspace.matrix_work.data() != matrix.data()) {
      ksj::array::copy(matrix, workspace.matrix_work.view());
    }

    auto output_work = ksj::array::PooledMatrix<std::complex<Real>>{};
    auto lapack_output = output;
    const auto output_overlaps_input = ksj::array::detail::views_may_overlap(workspace.matrix_work.view(), output);
    const auto use_direct_output =
      output.col_stride() == 1U && fits_lapack_int(output.row_stride()) && !output_overlaps_input;
    if (!use_direct_output) {
      output_work.resize(output.rows(), output.cols());
      lapack_output = output_work.view();
    }

    const auto rows = static_cast<lapack_int>(matrix.rows());
    const auto cols = static_cast<lapack_int>(matrix.cols());
    const auto ldu = static_cast<lapack_int>(lapack_output.row_stride());
    const auto info =
      gesvd('S', 'N', rows, cols, lapack_complex_cast(workspace.matrix_work.data()), cols, workspace.values_work.data(),
            lapack_complex_cast(lapack_output.data()), ldu, nullptr, cols, workspace.superb_work.data());
    if (info != 0) {
      return false;
    }
    if (!use_direct_output) {
      ksj::array::copy(lapack_output, output);
    }
    return true;
  } else {
    return false;
  }
}

template <typename Real>
[[nodiscard]] bool left_singular_vectors(const ksj::array::PooledMatrix<std::complex<Real>>& matrix,
                                         ksj::array::PooledMatrix<std::complex<Real>>& output,
                                         LeftSingularVectorsWorkspace<std::complex<Real>>& workspace) {
  return left_singular_vectors<Real>(ksj::array::as_const_view(matrix.view()), output.view(), workspace);
}

template <typename Real>
[[nodiscard]] bool left_singular_vectors(const ksj::array::PooledMatrix<std::complex<Real>>& matrix,
                                         ksj::array::PooledMatrix<std::complex<Real>>& output) {
  LeftSingularVectorsWorkspace<std::complex<Real>> workspace;
  workspace.resize(matrix.rows(), matrix.cols());
  return left_singular_vectors(matrix, output, workspace);
}

template <typename T>
[[nodiscard]] bool self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                                                    ksj::array::VectorView<T> values,
                                                    ksj::array::MatrixView<T> vectors) {
  if constexpr (lapack_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.empty() || values.size() != matrix.rows() ||
        vectors.rows() != matrix.rows() || vectors.cols() != matrix.cols() || !fits_lapack_int(matrix.rows())) {
      return false;
    }

    auto values_work = ksj::array::PooledVector<T>{};
    auto lapack_values = values;
    const auto use_direct_values = values.is_contiguous();
    if (!use_direct_values) {
      values_work.resize(values.size());
      lapack_values = values_work.view();
    }

    auto vectors_work = ksj::array::PooledMatrix<T>{};
    auto lapack_vectors = vectors;
    const auto use_direct_vectors = vectors.col_stride() == 1U && fits_lapack_int(vectors.row_stride());
    if (!use_direct_vectors) {
      vectors_work.resize(vectors.rows(), vectors.cols());
      lapack_vectors = vectors_work.view();
    }

    ksj::array::copy(matrix, lapack_vectors);
    const auto n = static_cast<lapack_int>(matrix.rows());
    const auto info =
      syev(n, lapack_vectors.data(), static_cast<lapack_int>(lapack_vectors.row_stride()), lapack_values.data());
    if (info != 0) {
      return false;
    }
    if (!use_direct_values) {
      ksj::array::copy(lapack_values, values);
    }
    if (!use_direct_vectors) {
      ksj::array::copy(lapack_vectors, vectors);
    }
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool self_adjoint_eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix,
                                                    ksj::array::PooledVector<T>& values,
                                                    ksj::array::PooledMatrix<T>& vectors) {
  return self_adjoint_eigen_decomposition<T>(ksj::array::as_const_view(matrix.view()), values.view(), vectors.view());
}

template <typename Real>
[[nodiscard]] bool self_adjoint_eigen_decomposition(ksj::array::MatrixView<const std::complex<Real>> matrix,
                                                    ksj::array::VectorView<Real> values,
                                                    ksj::array::MatrixView<std::complex<Real>> vectors) {
  if constexpr (std::is_same_v<Real, float> || std::is_same_v<Real, double>) {
    if (matrix.rows() != matrix.cols() || matrix.empty() || values.size() != matrix.rows() ||
        vectors.rows() != matrix.rows() || vectors.cols() != matrix.cols() || !fits_lapack_int(matrix.rows())) {
      return false;
    }

    auto values_work = ksj::array::PooledVector<Real>{};
    auto lapack_values = values;
    const auto use_direct_values = values.is_contiguous();
    if (!use_direct_values) {
      values_work.resize(values.size());
      lapack_values = values_work.view();
    }

    auto vectors_work = ksj::array::PooledMatrix<std::complex<Real>>{};
    auto lapack_vectors = vectors;
    const auto use_direct_vectors = vectors.col_stride() == 1U && fits_lapack_int(vectors.row_stride());
    if (!use_direct_vectors) {
      vectors_work.resize(vectors.rows(), vectors.cols());
      lapack_vectors = vectors_work.view();
    }

    ksj::array::copy(matrix, lapack_vectors);
    const auto n = static_cast<lapack_int>(matrix.rows());
    const auto info = heev(n, lapack_complex_cast(lapack_vectors.data()),
                           static_cast<lapack_int>(lapack_vectors.row_stride()), lapack_values.data());
    if (info != 0) {
      return false;
    }
    if (!use_direct_values) {
      ksj::array::copy(lapack_values, values);
    }
    if (!use_direct_vectors) {
      ksj::array::copy(lapack_vectors, vectors);
    }
    return true;
  } else {
    return false;
  }
}

template <typename Real>
[[nodiscard]] bool self_adjoint_eigen_decomposition(const ksj::array::PooledMatrix<std::complex<Real>>& matrix,
                                                    ksj::array::PooledVector<Real>& values,
                                                    ksj::array::PooledMatrix<std::complex<Real>>& vectors) {
  return self_adjoint_eigen_decomposition<Real>(ksj::array::as_const_view(matrix.view()), values.view(),
                                                vectors.view());
}

template <typename T>
[[nodiscard]] bool eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix,
                                       ksj::array::PooledVector<std::complex<T>>& values,
                                       ksj::array::PooledMatrix<std::complex<T>>& vectors) {
  if constexpr (lapack_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.empty() || values.size() != matrix.rows() ||
        vectors.rows() != matrix.rows() || vectors.cols() != matrix.cols() || !fits_lapack_int(matrix.rows())) {
      return false;
    }

    const auto size = matrix.rows();
    auto matrix_work = ksj::array::make_pooled_matrix<T>(size, size);
    auto right_vectors = ksj::array::make_pooled_matrix<T>(size, size);
    auto real_values = ksj::array::make_pooled_vector<T>(size);
    auto imag_values = ksj::array::make_pooled_vector<T>(size);
    as_eigen(matrix_work) = as_eigen(matrix);

    const auto n = static_cast<lapack_int>(size);
    if (geev(n, matrix_work.data(), n, real_values.data(), imag_values.data(), right_vectors.data(), n) != 0) {
      return false;
    }

    for (std::size_t index = 0; index < size; ++index) {
      values(index) = std::complex<T>{real_values(index), imag_values(index)};
    }

    for (std::size_t col = 0; col < size; ++col) {
      if (imag_values(col) > T{}) {
        for (std::size_t row = 0; row < size; ++row) {
          vectors(row, col) = std::complex<T>{right_vectors(row, col), right_vectors(row, col + 1U)};
          vectors(row, col + 1U) = std::complex<T>{right_vectors(row, col), -right_vectors(row, col + 1U)};
        }
        ++col;
      } else if (imag_values(col) < T{}) {
        continue;
      } else {
        for (std::size_t row = 0; row < size; ++row) {
          vectors(row, col) = std::complex<T>{right_vectors(row, col), T{}};
        }
      }
    }
    return true;
  } else {
    return false;
  }
}

template <typename Real>
[[nodiscard]] bool eigen_decomposition(const ksj::array::PooledMatrix<std::complex<Real>>& matrix,
                                       ksj::array::PooledVector<std::complex<Real>>& values,
                                       ksj::array::PooledMatrix<std::complex<Real>>& vectors) {
  if constexpr (std::is_same_v<Real, float> || std::is_same_v<Real, double>) {
    if (matrix.rows() != matrix.cols() || matrix.empty() || values.size() != matrix.rows() ||
        vectors.rows() != matrix.rows() || vectors.cols() != matrix.cols() || !fits_lapack_int(matrix.rows())) {
      return false;
    }

    auto matrix_work = ksj::array::make_pooled_matrix<std::complex<Real>>(matrix.rows(), matrix.cols());
    as_eigen(matrix_work) = as_eigen(matrix);

    const auto n = static_cast<lapack_int>(matrix.rows());
    const auto info = geev(n, lapack_complex_cast(matrix_work.data()), n, lapack_complex_cast(values.data()),
                           lapack_complex_cast(vectors.data()), n);
    return info == 0;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool eigen_decomposition(ksj::array::MatrixView<const T> matrix,
                                       ksj::array::VectorView<typename GeneralEigenWorkspace<T>::complex_type> values,
                                       ksj::array::MatrixView<typename GeneralEigenWorkspace<T>::complex_type> vectors,
                                       GeneralEigenWorkspace<T>& workspace) {
  using real_type = typename GeneralEigenWorkspace<T>::real_type;
  using complex_type = typename GeneralEigenWorkspace<T>::complex_type;
  if constexpr (lapack_solve_scalar_v<T>) {
    if (matrix.rows() != matrix.cols() || matrix.empty() || values.size() != matrix.rows() ||
        vectors.rows() != matrix.rows() || vectors.cols() != matrix.cols() || !fits_lapack_int(matrix.rows())) {
      return false;
    }

    const auto size = matrix.rows();
    workspace.resize(size);
    as_eigen(workspace.matrix_work) = as_eigen(matrix);
    const auto n = static_cast<lapack_int>(size);

    if constexpr (lapack_complex_scalar_v<T>) {
      const auto info = geev(n, lapack_complex_cast(workspace.matrix_work.data()), n,
                             lapack_complex_cast(workspace.eigenvalues_work.data()),
                             lapack_complex_cast(workspace.eigenvectors_work.data()), n);
      if (info != 0) {
        return false;
      }
    } else {
      const auto info = geev(n, workspace.matrix_work.data(), n, workspace.real_values_work.data(),
                             workspace.imag_values_work.data(), workspace.real_vectors_work.data(), n);
      if (info != 0) {
        return false;
      }

      for (std::size_t index = 0; index < size; ++index) {
        workspace.eigenvalues_work(index) =
          complex_type{workspace.real_values_work(index), workspace.imag_values_work(index)};
      }

      for (std::size_t col = 0; col < size; ++col) {
        if (workspace.imag_values_work(col) > real_type{} && col + 1U < size) {
          for (std::size_t row = 0; row < size; ++row) {
            workspace.eigenvectors_work(row, col) =
              complex_type{workspace.real_vectors_work(row, col), workspace.real_vectors_work(row, col + 1U)};
            workspace.eigenvectors_work(row, col + 1U) =
              complex_type{workspace.real_vectors_work(row, col), -workspace.real_vectors_work(row, col + 1U)};
          }
          ++col;
        } else if (workspace.imag_values_work(col) == real_type{}) {
          for (std::size_t row = 0; row < size; ++row) {
            workspace.eigenvectors_work(row, col) = complex_type{workspace.real_vectors_work(row, col), real_type{}};
          }
        }
      }
    }

    as_eigen(values) = as_eigen(workspace.eigenvalues_work);
    as_eigen(vectors) = as_eigen(workspace.eigenvectors_work);
    return true;
  } else {
    return false;
  }
}

template <typename T>
bool svd_in_place_wrapper_call(ksj::array::MatrixView<T> matrix, ksj::array::PooledMatrix<T>& u,
                               ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,
                               ksj::array::PooledMatrix<T>& v_adjoint, const bool full_matrices) {
  return svd_in_place<T>(matrix, u, values, v_adjoint, full_matrices);
}

bool svd_in_place_wrapper_call(ksj::array::MatrixView<ksj::base::cf32> matrix,
                               ksj::array::PooledMatrix<ksj::base::cf32>& u, ksj::array::PooledVector<float>& values,
                               ksj::array::PooledMatrix<ksj::base::cf32>& v_adjoint, const bool full_matrices) {
  return svd_in_place<float>(matrix, u, values, v_adjoint, full_matrices);
}

bool svd_in_place_wrapper_call(ksj::array::MatrixView<ksj::base::cf64> matrix,
                               ksj::array::PooledMatrix<ksj::base::cf64>& u, ksj::array::PooledVector<double>& values,
                               ksj::array::PooledMatrix<ksj::base::cf64>& v_adjoint, const bool full_matrices) {
  return svd_in_place<double>(matrix, u, values, v_adjoint, full_matrices);
}

template <typename T>
bool svd_wrapper_call(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& u,
                      ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,
                      ksj::array::PooledMatrix<T>& v_adjoint, const bool full_matrices) {
  return svd<T>(matrix, u, values, v_adjoint, full_matrices);
}

template <typename T>
bool svd_wrapper_call(ksj::array::MatrixView<const T> matrix, ksj::array::PooledMatrix<T>& u,
                      ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,
                      ksj::array::PooledMatrix<T>& v_adjoint, const bool full_matrices) {
  return svd<T>(matrix, u, values, v_adjoint, full_matrices);
}

bool svd_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix,
                      ksj::array::PooledMatrix<ksj::base::cf32>& u, ksj::array::PooledVector<float>& values,
                      ksj::array::PooledMatrix<ksj::base::cf32>& v_adjoint, const bool full_matrices) {
  return svd<float>(matrix, u, values, v_adjoint, full_matrices);
}

bool svd_wrapper_call(ksj::array::MatrixView<const ksj::base::cf32> matrix,
                      ksj::array::PooledMatrix<ksj::base::cf32>& u, ksj::array::PooledVector<float>& values,
                      ksj::array::PooledMatrix<ksj::base::cf32>& v_adjoint, const bool full_matrices) {
  return svd<float>(matrix, u, values, v_adjoint, full_matrices);
}

bool svd_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix,
                      ksj::array::PooledMatrix<ksj::base::cf64>& u, ksj::array::PooledVector<double>& values,
                      ksj::array::PooledMatrix<ksj::base::cf64>& v_adjoint, const bool full_matrices) {
  return svd<double>(matrix, u, values, v_adjoint, full_matrices);
}

bool svd_wrapper_call(ksj::array::MatrixView<const ksj::base::cf64> matrix,
                      ksj::array::PooledMatrix<ksj::base::cf64>& u, ksj::array::PooledVector<double>& values,
                      ksj::array::PooledMatrix<ksj::base::cf64>& v_adjoint, const bool full_matrices) {
  return svd<double>(matrix, u, values, v_adjoint, full_matrices);
}

template <typename T>
bool svd_workspace_wrapper_call(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,
                                ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                                ksj::array::MatrixView<T> v_adjoint, SvdWorkspace<T>& workspace,
                                const bool full_matrices) {
  return svd<T>(matrix, u, values, v_adjoint, workspace, full_matrices);
}

bool svd_workspace_wrapper_call(ksj::array::MatrixView<const ksj::base::cf32> matrix,
                                ksj::array::MatrixView<ksj::base::cf32> u, ksj::array::VectorView<float> values,
                                ksj::array::MatrixView<ksj::base::cf32> v_adjoint,
                                SvdWorkspace<ksj::base::cf32>& workspace, const bool full_matrices) {
  return svd<float>(matrix, u, values, v_adjoint, workspace, full_matrices);
}

bool svd_workspace_wrapper_call(ksj::array::MatrixView<const ksj::base::cf64> matrix,
                                ksj::array::MatrixView<ksj::base::cf64> u, ksj::array::VectorView<double> values,
                                ksj::array::MatrixView<ksj::base::cf64> v_adjoint,
                                SvdWorkspace<ksj::base::cf64>& workspace, const bool full_matrices) {
  return svd<double>(matrix, u, values, v_adjoint, workspace, full_matrices);
}

template <typename T>
bool left_singular_vectors_wrapper_call(const ksj::array::PooledMatrix<T>& matrix,
                                        ksj::array::PooledMatrix<T>& output) {
  return left_singular_vectors<T>(matrix, output);
}

template <typename T>
bool left_singular_vectors_wrapper_call(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output,
                                        LeftSingularVectorsWorkspace<T>& workspace) {
  return left_singular_vectors<T>(matrix, output, workspace);
}

template <typename T>
bool left_singular_vectors_wrapper_call(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output,
                                        LeftSingularVectorsWorkspace<T>& workspace) {
  return left_singular_vectors<T>(matrix, output, workspace);
}

bool left_singular_vectors_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix,
                                        ksj::array::PooledMatrix<ksj::base::cf32>& output) {
  return left_singular_vectors<float>(matrix, output);
}

bool left_singular_vectors_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix,
                                        ksj::array::PooledMatrix<ksj::base::cf32>& output,
                                        LeftSingularVectorsWorkspace<ksj::base::cf32>& workspace) {
  return left_singular_vectors<float>(matrix, output, workspace);
}

bool left_singular_vectors_wrapper_call(ksj::array::MatrixView<const ksj::base::cf32> matrix,
                                        ksj::array::MatrixView<ksj::base::cf32> output,
                                        LeftSingularVectorsWorkspace<ksj::base::cf32>& workspace) {
  return left_singular_vectors<float>(matrix, output, workspace);
}

bool left_singular_vectors_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix,
                                        ksj::array::PooledMatrix<ksj::base::cf64>& output) {
  return left_singular_vectors<double>(matrix, output);
}

bool left_singular_vectors_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix,
                                        ksj::array::PooledMatrix<ksj::base::cf64>& output,
                                        LeftSingularVectorsWorkspace<ksj::base::cf64>& workspace) {
  return left_singular_vectors<double>(matrix, output, workspace);
}

bool left_singular_vectors_wrapper_call(ksj::array::MatrixView<const ksj::base::cf64> matrix,
                                        ksj::array::MatrixView<ksj::base::cf64> output,
                                        LeftSingularVectorsWorkspace<ksj::base::cf64>& workspace) {
  return left_singular_vectors<double>(matrix, output, workspace);
}

template <typename T>
bool self_adjoint_wrapper_call(const ksj::array::PooledMatrix<T>& matrix,
                               ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,
                               ksj::array::PooledMatrix<T>& vectors) {
  return self_adjoint_eigen_decomposition<T>(matrix, values, vectors);
}

template <typename T>
bool self_adjoint_wrapper_call(ksj::array::MatrixView<const T> matrix,
                               ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,
                               ksj::array::MatrixView<T> vectors) {
  return self_adjoint_eigen_decomposition<T>(matrix, values, vectors);
}

bool self_adjoint_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix,
                               ksj::array::PooledVector<float>& values,
                               ksj::array::PooledMatrix<ksj::base::cf32>& vectors) {
  return self_adjoint_eigen_decomposition<float>(matrix, values, vectors);
}

bool self_adjoint_wrapper_call(ksj::array::MatrixView<const ksj::base::cf32> matrix,
                               ksj::array::VectorView<float> values, ksj::array::MatrixView<ksj::base::cf32> vectors) {
  return self_adjoint_eigen_decomposition<float>(matrix, values, vectors);
}

bool self_adjoint_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix,
                               ksj::array::PooledVector<double>& values,
                               ksj::array::PooledMatrix<ksj::base::cf64>& vectors) {
  return self_adjoint_eigen_decomposition<double>(matrix, values, vectors);
}

bool self_adjoint_wrapper_call(ksj::array::MatrixView<const ksj::base::cf64> matrix,
                               ksj::array::VectorView<double> values, ksj::array::MatrixView<ksj::base::cf64> vectors) {
  return self_adjoint_eigen_decomposition<double>(matrix, values, vectors);
}

template <typename T>
bool general_eigen_wrapper_call(const ksj::array::PooledMatrix<T>& matrix,
                                ksj::array::PooledVector<typename EigenDecomposition<T>::complex_type>& values,
                                ksj::array::PooledMatrix<typename EigenDecomposition<T>::complex_type>& vectors) {
  return eigen_decomposition<T>(matrix, values, vectors);
}

bool general_eigen_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix,
                                ksj::array::PooledVector<ksj::base::cf32>& values,
                                ksj::array::PooledMatrix<ksj::base::cf32>& vectors) {
  return eigen_decomposition<float>(matrix, values, vectors);
}

bool general_eigen_wrapper_call(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix,
                                ksj::array::PooledVector<ksj::base::cf64>& values,
                                ksj::array::PooledMatrix<ksj::base::cf64>& vectors) {
  return eigen_decomposition<double>(matrix, values, vectors);
}

#define KSJ_LINALG_INTEL_DECOMPOSITION_WRAPPERS(T)                                                                     \
  bool can_svd_in_place(ksj::array::MatrixView<T> matrix) {                                                            \
    return can_svd_in_place_view(matrix);                                                                              \
  }                                                                                                                    \
  bool svd_in_place(ksj::array::MatrixView<T> matrix, ksj::array::PooledMatrix<T>& u,                                  \
                    ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,                                    \
                    ksj::array::PooledMatrix<T>& v_adjoint, bool full_matrices) {                                      \
    return svd_in_place_wrapper_call(matrix, u, values, v_adjoint, full_matrices);                                     \
  }                                                                                                                    \
  bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::PooledMatrix<T>& u,                                     \
           ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values, ksj::array::PooledMatrix<T>& v_adjoint,     \
           bool full_matrices) {                                                                                       \
    return svd_wrapper_call(matrix, u, values, v_adjoint, full_matrices);                                              \
  }                                                                                                                    \
  bool svd(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> u,                                        \
           ksj::array::VectorView<ksj::array::real_scalar_t<T>> values, ksj::array::MatrixView<T> v_adjoint,           \
           SvdWorkspace<T>& workspace, bool full_matrices) {                                                           \
    return svd_workspace_wrapper_call(matrix, u, values, v_adjoint, workspace, full_matrices);                         \
  }                                                                                                                    \
  bool svd(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& u,                                  \
           ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values, ksj::array::PooledMatrix<T>& v_adjoint,     \
           bool full_matrices) {                                                                                       \
    return svd_wrapper_call(matrix, u, values, v_adjoint, full_matrices);                                              \
  }                                                                                                                    \
  bool left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output) {         \
    return left_singular_vectors_wrapper_call(matrix, output);                                                         \
  }                                                                                                                    \
  bool left_singular_vectors(const ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledMatrix<T>& output,           \
                             LeftSingularVectorsWorkspace<T>& workspace) {                                             \
    return left_singular_vectors_wrapper_call(matrix, output, workspace);                                              \
  }                                                                                                                    \
  bool left_singular_vectors(ksj::array::MatrixView<const T> matrix, ksj::array::MatrixView<T> output,                 \
                             LeftSingularVectorsWorkspace<T>& workspace) {                                             \
    return left_singular_vectors_wrapper_call(matrix, output, workspace);                                              \
  }                                                                                                                    \
  bool self_adjoint_eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                        \
                                        ksj::array::VectorView<ksj::array::real_scalar_t<T>> values,                   \
                                        ksj::array::MatrixView<T> vectors) {                                           \
    return self_adjoint_wrapper_call(matrix, values, vectors);                                                         \
  }                                                                                                                    \
  bool self_adjoint_eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix,                                     \
                                        ksj::array::PooledVector<ksj::array::real_scalar_t<T>>& values,                \
                                        ksj::array::PooledMatrix<T>& vectors) {                                        \
    return self_adjoint_wrapper_call(matrix, values, vectors);                                                         \
  }                                                                                                                    \
  bool eigen_decomposition(const ksj::array::PooledMatrix<T>& matrix,                                                  \
                           ksj::array::PooledVector<typename EigenDecomposition<T>::complex_type>& values,             \
                           ksj::array::PooledMatrix<typename EigenDecomposition<T>::complex_type>& vectors) {          \
    return general_eigen_wrapper_call(matrix, values, vectors);                                                        \
  }                                                                                                                    \
  bool eigen_decomposition(ksj::array::MatrixView<const T> matrix,                                                     \
                           ksj::array::VectorView<typename GeneralEigenWorkspace<T>::complex_type> values,             \
                           ksj::array::MatrixView<typename GeneralEigenWorkspace<T>::complex_type> vectors,            \
                           GeneralEigenWorkspace<T>& workspace) {                                                      \
    return eigen_decomposition<T>(matrix, values, vectors, workspace);                                                 \
  }

KSJ_LINALG_INTEL_DECOMPOSITION_WRAPPERS(float)
KSJ_LINALG_INTEL_DECOMPOSITION_WRAPPERS(double)
KSJ_LINALG_INTEL_DECOMPOSITION_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_INTEL_DECOMPOSITION_WRAPPERS(ksj::base::cf64)

bool singular_values(ksj::array::MatrixView<const float> matrix, ksj::array::PooledVector<float>& output) {
  return singular_values<float>(matrix, output);
}

bool singular_values(ksj::array::MatrixView<const double> matrix, ksj::array::PooledVector<double>& output) {
  return singular_values<double>(matrix, output);
}

bool singular_values(ksj::array::MatrixView<const ksj::base::cf32> matrix, ksj::array::PooledVector<float>& output) {
  return singular_values<float>(matrix, output);
}

bool singular_values(ksj::array::MatrixView<const ksj::base::cf64> matrix, ksj::array::PooledVector<double>& output) {
  return singular_values<double>(matrix, output);
}

bool singular_values(const ksj::array::PooledMatrix<float>& matrix, ksj::array::PooledVector<float>& output) {
  return singular_values<float>(matrix, output);
}

bool singular_values(const ksj::array::PooledMatrix<double>& matrix, ksj::array::PooledVector<double>& output) {
  return singular_values<double>(matrix, output);
}

bool singular_values(const ksj::array::PooledMatrix<ksj::base::cf32>& matrix, ksj::array::PooledVector<float>& output) {
  return singular_values<float>(matrix, output);
}

bool singular_values(const ksj::array::PooledMatrix<ksj::base::cf64>& matrix,
                     ksj::array::PooledVector<double>& output) {
  return singular_values<double>(matrix, output);
}

#undef KSJ_LINALG_INTEL_DECOMPOSITION_WRAPPERS

} // namespace ksj::linalg::detail::intel
