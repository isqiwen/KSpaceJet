#include "kspacejet/sparse/sparse.hpp"
#include "kspacejet/sparse/detail/intel/intel_sparse_operations.hpp"

#include <memory>
#include <vector>

#include <mkl_spblas.h>

namespace ksj::sparse::detail::intel {
namespace {

[[nodiscard]] bool check_status(const sparse_status_t status) noexcept {
  return status == SPARSE_STATUS_SUCCESS;
}

[[nodiscard]] MKL_INT* mkl_index_data(const ksj::array::PooledVector<int>& values) noexcept {
  static_assert(sizeof(MKL_INT) == sizeof(int), "KSpaceJet sparse currently expects LP64 MKL_INT");
  return const_cast<MKL_INT*>(values.data());
}

bool create_csr(sparse_matrix_t* handle, const CsrMatrix<float>& matrix) {
  auto* row_offsets = mkl_index_data(matrix.row_offsets());
  return check_status(mkl_sparse_s_create_csr(
    handle, SPARSE_INDEX_BASE_ZERO, static_cast<MKL_INT>(matrix.rows()), static_cast<MKL_INT>(matrix.cols()),
    row_offsets, row_offsets + 1, mkl_index_data(matrix.column_indices()), const_cast<float*>(matrix.values().data())));
}

bool create_csr(sparse_matrix_t* handle, const CsrMatrix<double>& matrix) {
  auto* row_offsets = mkl_index_data(matrix.row_offsets());
  return check_status(mkl_sparse_d_create_csr(handle, SPARSE_INDEX_BASE_ZERO, static_cast<MKL_INT>(matrix.rows()),
                                              static_cast<MKL_INT>(matrix.cols()), row_offsets, row_offsets + 1,
                                              mkl_index_data(matrix.column_indices()),
                                              const_cast<double*>(matrix.values().data())));
}

bool create_csr(sparse_matrix_t* handle, const CsrMatrix<ksj::base::cf32>& matrix) {
  auto* row_offsets = mkl_index_data(matrix.row_offsets());
  return check_status(mkl_sparse_c_create_csr(
    handle, SPARSE_INDEX_BASE_ZERO, static_cast<MKL_INT>(matrix.rows()), static_cast<MKL_INT>(matrix.cols()),
    row_offsets, row_offsets + 1, mkl_index_data(matrix.column_indices()),
    reinterpret_cast<MKL_Complex8*>(const_cast<ksj::base::cf32*>(matrix.values().data()))));
}

bool create_csr(sparse_matrix_t* handle, const CsrMatrix<ksj::base::cf64>& matrix) {
  auto* row_offsets = mkl_index_data(matrix.row_offsets());
  return check_status(mkl_sparse_z_create_csr(
    handle, SPARSE_INDEX_BASE_ZERO, static_cast<MKL_INT>(matrix.rows()), static_cast<MKL_INT>(matrix.cols()),
    row_offsets, row_offsets + 1, mkl_index_data(matrix.column_indices()),
    reinterpret_cast<MKL_Complex16*>(const_cast<ksj::base::cf64*>(matrix.values().data()))));
}

void optimize_handle(const sparse_matrix_t handle) noexcept {
  (void)mkl_sparse_optimize(handle);
}

template <typename T> [[nodiscard]] bool make_handle_impl(const CsrMatrix<T>& matrix, SparseHandle& output) {
  sparse_matrix_t handle = nullptr;
  if (!create_csr(&handle, matrix)) {
    return false;
  }

  optimize_handle(handle);
  output = SparseHandle(static_cast<void*>(handle), [](void* value) {
    if (value != nullptr) {
      (void)mkl_sparse_destroy(static_cast<sparse_matrix_t>(value));
    }
  });
  return true;
}

[[nodiscard]] sparse_matrix_t native_handle(const SparseHandle& handle) noexcept {
  return static_cast<sparse_matrix_t>(handle.get());
}

void order_handle(const sparse_matrix_t handle) noexcept {
  (void)mkl_sparse_order(handle);
}

template <typename T, typename MklValue>
[[nodiscard]] bool assign_exported_csr(const sparse_index_base_t indexing, const MKL_INT rows, const MKL_INT cols,
                                       const MKL_INT* rows_start, const MKL_INT* rows_end, const MKL_INT* col_indices,
                                       const MklValue* values, CsrMatrix<T>& output) {
  static_assert(sizeof(MKL_INT) == sizeof(int), "KSpaceJet sparse currently expects LP64 MKL_INT");
  if (indexing != SPARSE_INDEX_BASE_ZERO || rows < 0 || cols < 0) {
    return false;
  }

  auto row_offsets = std::vector<int>(static_cast<std::size_t>(rows) + 1U, 0);
  for (MKL_INT row = 0; row < rows; ++row) {
    row_offsets[static_cast<std::size_t>(row)] = static_cast<int>(rows_start[row]);
  }
  if (rows > 0) {
    row_offsets[static_cast<std::size_t>(rows)] = static_cast<int>(rows_end[rows - 1]);
  }
  const auto nonzeros = static_cast<std::size_t>(row_offsets.back());
  output = CsrMatrix<T>(static_cast<std::size_t>(rows), static_cast<std::size_t>(cols),
                        ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
                        ksj::array::VectorView<const int>(reinterpret_cast<const int*>(col_indices), nonzeros),
                        ksj::array::VectorView<const T>(reinterpret_cast<const T*>(values), nonzeros));
  return true;
}

[[nodiscard]] bool export_csr(const sparse_matrix_t source, CsrMatrix<float>& output) {
  auto indexing = sparse_index_base_t{};
  auto rows = MKL_INT{};
  auto cols = MKL_INT{};
  auto* rows_start = static_cast<MKL_INT*>(nullptr);
  auto* rows_end = static_cast<MKL_INT*>(nullptr);
  auto* col_indices = static_cast<MKL_INT*>(nullptr);
  auto* values = static_cast<float*>(nullptr);
  if (!check_status(
        mkl_sparse_s_export_csr(source, &indexing, &rows, &cols, &rows_start, &rows_end, &col_indices, &values))) {
    return false;
  }
  return assign_exported_csr(indexing, rows, cols, rows_start, rows_end, col_indices, values, output);
}

[[nodiscard]] bool export_csr(const sparse_matrix_t source, CsrMatrix<double>& output) {
  auto indexing = sparse_index_base_t{};
  auto rows = MKL_INT{};
  auto cols = MKL_INT{};
  auto* rows_start = static_cast<MKL_INT*>(nullptr);
  auto* rows_end = static_cast<MKL_INT*>(nullptr);
  auto* col_indices = static_cast<MKL_INT*>(nullptr);
  auto* values = static_cast<double*>(nullptr);
  if (!check_status(
        mkl_sparse_d_export_csr(source, &indexing, &rows, &cols, &rows_start, &rows_end, &col_indices, &values))) {
    return false;
  }
  return assign_exported_csr(indexing, rows, cols, rows_start, rows_end, col_indices, values, output);
}

[[nodiscard]] bool export_csr(const sparse_matrix_t source, CsrMatrix<ksj::base::cf32>& output) {
  auto indexing = sparse_index_base_t{};
  auto rows = MKL_INT{};
  auto cols = MKL_INT{};
  auto* rows_start = static_cast<MKL_INT*>(nullptr);
  auto* rows_end = static_cast<MKL_INT*>(nullptr);
  auto* col_indices = static_cast<MKL_INT*>(nullptr);
  auto* values = static_cast<MKL_Complex8*>(nullptr);
  if (!check_status(
        mkl_sparse_c_export_csr(source, &indexing, &rows, &cols, &rows_start, &rows_end, &col_indices, &values))) {
    return false;
  }
  return assign_exported_csr(indexing, rows, cols, rows_start, rows_end, col_indices, values, output);
}

[[nodiscard]] bool export_csr(const sparse_matrix_t source, CsrMatrix<ksj::base::cf64>& output) {
  auto indexing = sparse_index_base_t{};
  auto rows = MKL_INT{};
  auto cols = MKL_INT{};
  auto* rows_start = static_cast<MKL_INT*>(nullptr);
  auto* rows_end = static_cast<MKL_INT*>(nullptr);
  auto* col_indices = static_cast<MKL_INT*>(nullptr);
  auto* values = static_cast<MKL_Complex16*>(nullptr);
  if (!check_status(
        mkl_sparse_z_export_csr(source, &indexing, &rows, &cols, &rows_start, &rows_end, &col_indices, &values))) {
    return false;
  }
  return assign_exported_csr(indexing, rows, cols, rows_start, rows_end, col_indices, values, output);
}

[[nodiscard]] matrix_descr general_matrix_descriptor() noexcept {
  matrix_descr descriptor{};
  descriptor.type = SPARSE_MATRIX_TYPE_GENERAL;
  descriptor.mode = SPARSE_FILL_MODE_LOWER;
  descriptor.diag = SPARSE_DIAG_NON_UNIT;
  return descriptor;
}

[[nodiscard]] matrix_descr triangular_matrix_descriptor(const SparseTriangle triangle,
                                                        const SparseDiagonal diagonal) noexcept {
  matrix_descr descriptor{};
  descriptor.type = SPARSE_MATRIX_TYPE_TRIANGULAR;
  descriptor.mode = triangle == SparseTriangle::lower ? SPARSE_FILL_MODE_LOWER : SPARSE_FILL_MODE_UPPER;
  descriptor.diag = diagonal == SparseDiagonal::unit ? SPARSE_DIAG_UNIT : SPARSE_DIAG_NON_UNIT;
  return descriptor;
}

bool mv(const sparse_matrix_t handle, const float* x, float* y) {
  return check_status(
    mkl_sparse_s_mv(SPARSE_OPERATION_NON_TRANSPOSE, 1.0F, handle, general_matrix_descriptor(), x, 0.0F, y));
}

bool mv(const sparse_matrix_t handle, const double* x, double* y) {
  return check_status(
    mkl_sparse_d_mv(SPARSE_OPERATION_NON_TRANSPOSE, 1.0, handle, general_matrix_descriptor(), x, 0.0, y));
}

bool mv(const sparse_matrix_t handle, const ksj::base::cf32* x, ksj::base::cf32* y) {
  const MKL_Complex8 alpha{1.0F, 0.0F};
  const MKL_Complex8 beta{0.0F, 0.0F};
  return check_status(mkl_sparse_c_mv(SPARSE_OPERATION_NON_TRANSPOSE, alpha, handle, general_matrix_descriptor(),
                                      reinterpret_cast<const MKL_Complex8*>(x), beta,
                                      reinterpret_cast<MKL_Complex8*>(y)));
}

bool mv(const sparse_matrix_t handle, const ksj::base::cf64* x, ksj::base::cf64* y) {
  const MKL_Complex16 alpha{1.0, 0.0};
  const MKL_Complex16 beta{0.0, 0.0};
  return check_status(mkl_sparse_z_mv(SPARSE_OPERATION_NON_TRANSPOSE, alpha, handle, general_matrix_descriptor(),
                                      reinterpret_cast<const MKL_Complex16*>(x), beta,
                                      reinterpret_cast<MKL_Complex16*>(y)));
}

template <typename T> [[nodiscard]] bool matrix_view_is_row_major_contiguous(ksj::array::MatrixView<T> view) noexcept {
  return view.row_stride() == view.cols() && view.col_stride() == 1U;
}

[[nodiscard]] sparse_operation_t to_mkl_operation(const SparseOperation operation) noexcept {
  switch (operation) {
    case SparseOperation::transpose:
      return SPARSE_OPERATION_TRANSPOSE;
    case SparseOperation::conjugate_transpose:
      return SPARSE_OPERATION_CONJUGATE_TRANSPOSE;
    case SparseOperation::none:
    default:
      return SPARSE_OPERATION_NON_TRANSPOSE;
  }
}

[[nodiscard]] bool mm(const sparse_operation_t operation, const sparse_matrix_t matrix, const sparse_layout_t layout,
                      const float* x, const MKL_INT columns, const MKL_INT ldx, float* y, const MKL_INT ldy) {
  return check_status(
    mkl_sparse_s_mm(operation, 1.0F, matrix, general_matrix_descriptor(), layout, x, columns, ldx, 0.0F, y, ldy));
}

[[nodiscard]] bool mm(const sparse_operation_t operation, const sparse_matrix_t matrix, const sparse_layout_t layout,
                      const double* x, const MKL_INT columns, const MKL_INT ldx, double* y, const MKL_INT ldy) {
  return check_status(
    mkl_sparse_d_mm(operation, 1.0, matrix, general_matrix_descriptor(), layout, x, columns, ldx, 0.0, y, ldy));
}

[[nodiscard]] bool mm(const sparse_operation_t operation, const sparse_matrix_t matrix, const sparse_layout_t layout,
                      const ksj::base::cf32* x, const MKL_INT columns, const MKL_INT ldx, ksj::base::cf32* y,
                      const MKL_INT ldy) {
  const MKL_Complex8 alpha{1.0F, 0.0F};
  const MKL_Complex8 beta{0.0F, 0.0F};
  return check_status(mkl_sparse_c_mm(operation, alpha, matrix, general_matrix_descriptor(), layout,
                                      reinterpret_cast<const MKL_Complex8*>(x), columns, ldx, beta,
                                      reinterpret_cast<MKL_Complex8*>(y), ldy));
}

[[nodiscard]] bool mm(const sparse_operation_t operation, const sparse_matrix_t matrix, const sparse_layout_t layout,
                      const ksj::base::cf64* x, const MKL_INT columns, const MKL_INT ldx, ksj::base::cf64* y,
                      const MKL_INT ldy) {
  const MKL_Complex16 alpha{1.0, 0.0};
  const MKL_Complex16 beta{0.0, 0.0};
  return check_status(mkl_sparse_z_mm(operation, alpha, matrix, general_matrix_descriptor(), layout,
                                      reinterpret_cast<const MKL_Complex16*>(x), columns, ldx, beta,
                                      reinterpret_cast<MKL_Complex16*>(y), ldy));
}

[[nodiscard]] bool sparse_add(const sparse_operation_t operation, const sparse_matrix_t lhs, const float alpha,
                              const sparse_matrix_t rhs, sparse_matrix_t* output) {
  return check_status(mkl_sparse_s_add(operation, lhs, alpha, rhs, output));
}

[[nodiscard]] bool sparse_add(const sparse_operation_t operation, const sparse_matrix_t lhs, const double alpha,
                              const sparse_matrix_t rhs, sparse_matrix_t* output) {
  return check_status(mkl_sparse_d_add(operation, lhs, alpha, rhs, output));
}

[[nodiscard]] bool sparse_add(const sparse_operation_t operation, const sparse_matrix_t lhs,
                              const ksj::base::cf32 alpha, const sparse_matrix_t rhs, sparse_matrix_t* output) {
  const auto mkl_alpha = MKL_Complex8{alpha.real(), alpha.imag()};
  return check_status(mkl_sparse_c_add(operation, lhs, mkl_alpha, rhs, output));
}

[[nodiscard]] bool sparse_add(const sparse_operation_t operation, const sparse_matrix_t lhs,
                              const ksj::base::cf64 alpha, const sparse_matrix_t rhs, sparse_matrix_t* output) {
  const auto mkl_alpha = MKL_Complex16{alpha.real(), alpha.imag()};
  return check_status(mkl_sparse_z_add(operation, lhs, mkl_alpha, rhs, output));
}

[[nodiscard]] bool trsv(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const float* rhs, float* output) {
  return check_status(mkl_sparse_s_trsv(operation, 1.0F, matrix, descriptor, rhs, output));
}

[[nodiscard]] bool trsv(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const double* rhs, double* output) {
  return check_status(mkl_sparse_d_trsv(operation, 1.0, matrix, descriptor, rhs, output));
}

[[nodiscard]] bool trsv(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const ksj::base::cf32* rhs, ksj::base::cf32* output) {
  const auto alpha = MKL_Complex8{1.0F, 0.0F};
  return check_status(mkl_sparse_c_trsv(operation, alpha, matrix, descriptor,
                                        reinterpret_cast<const MKL_Complex8*>(rhs),
                                        reinterpret_cast<MKL_Complex8*>(output)));
}

[[nodiscard]] bool trsv(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const ksj::base::cf64* rhs, ksj::base::cf64* output) {
  const auto alpha = MKL_Complex16{1.0, 0.0};
  return check_status(mkl_sparse_z_trsv(operation, alpha, matrix, descriptor,
                                        reinterpret_cast<const MKL_Complex16*>(rhs),
                                        reinterpret_cast<MKL_Complex16*>(output)));
}

[[nodiscard]] bool trsm(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const sparse_layout_t layout, const float* rhs, const MKL_INT columns, const MKL_INT ldx,
                        float* output, const MKL_INT ldy) {
  return check_status(mkl_sparse_s_trsm(operation, 1.0F, matrix, descriptor, layout, rhs, columns, ldx, output, ldy));
}

[[nodiscard]] bool trsm(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const sparse_layout_t layout, const double* rhs, const MKL_INT columns, const MKL_INT ldx,
                        double* output, const MKL_INT ldy) {
  return check_status(mkl_sparse_d_trsm(operation, 1.0, matrix, descriptor, layout, rhs, columns, ldx, output, ldy));
}

[[nodiscard]] bool trsm(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const sparse_layout_t layout, const ksj::base::cf32* rhs, const MKL_INT columns,
                        const MKL_INT ldx, ksj::base::cf32* output, const MKL_INT ldy) {
  const auto alpha = MKL_Complex8{1.0F, 0.0F};
  return check_status(mkl_sparse_c_trsm(operation, alpha, matrix, descriptor, layout,
                                        reinterpret_cast<const MKL_Complex8*>(rhs), columns, ldx,
                                        reinterpret_cast<MKL_Complex8*>(output), ldy));
}

[[nodiscard]] bool trsm(const sparse_operation_t operation, const sparse_matrix_t matrix, const matrix_descr descriptor,
                        const sparse_layout_t layout, const ksj::base::cf64* rhs, const MKL_INT columns,
                        const MKL_INT ldx, ksj::base::cf64* output, const MKL_INT ldy) {
  const auto alpha = MKL_Complex16{1.0, 0.0};
  return check_status(mkl_sparse_z_trsm(operation, alpha, matrix, descriptor, layout,
                                        reinterpret_cast<const MKL_Complex16*>(rhs), columns, ldx,
                                        reinterpret_cast<MKL_Complex16*>(output), ldy));
}

template <typename T>
[[nodiscard]] bool spmv_handle_impl(const SparseHandle& handle, ksj::array::VectorView<const T> vector,
                                    ksj::array::VectorView<T> output) {
  if (!handle || !vector.is_contiguous() || !output.is_contiguous() ||
      ksj::array::detail::views_may_overlap(vector, output)) {
    return false;
  }
  return mv(native_handle(handle), vector.data(), output.data());
}

template <typename T>
[[nodiscard]] bool spmm_handle_impl(const SparseHandle& handle, ksj::array::MatrixView<const T> dense,
                                    ksj::array::MatrixView<T> output, const SparseOperation operation) {
  if (!handle || !matrix_view_is_row_major_contiguous(dense) || !matrix_view_is_row_major_contiguous(output) ||
      ksj::array::detail::views_may_overlap(dense, output)) {
    return false;
  }
  const auto columns = static_cast<MKL_INT>(dense.cols());
  return mm(to_mkl_operation(operation), native_handle(handle), SPARSE_LAYOUT_ROW_MAJOR, dense.data(), columns, columns,
            output.data(), static_cast<MKL_INT>(output.cols()));
}

template <typename T>
[[nodiscard]] bool spsv_handle_impl(const SparseHandle& handle, ksj::array::VectorView<const T> rhs,
                                    ksj::array::VectorView<T> output, const SparseTriangle triangle,
                                    const SparseDiagonal diagonal, const SparseOperation operation) {
  if (!handle || !rhs.is_contiguous() || !output.is_contiguous() ||
      ksj::array::detail::views_may_overlap(rhs, output)) {
    return false;
  }
  return trsv(to_mkl_operation(operation), native_handle(handle), triangular_matrix_descriptor(triangle, diagonal),
              rhs.data(), output.data());
}

template <typename T>
[[nodiscard]] bool spsm_handle_impl(const SparseHandle& handle, ksj::array::MatrixView<const T> rhs,
                                    ksj::array::MatrixView<T> output, const SparseTriangle triangle,
                                    const SparseDiagonal diagonal, const SparseOperation operation) {
  if (!handle || !matrix_view_is_row_major_contiguous(rhs) || !matrix_view_is_row_major_contiguous(output) ||
      ksj::array::detail::views_may_overlap(rhs, output)) {
    return false;
  }
  const auto columns = static_cast<MKL_INT>(rhs.cols());
  return trsm(to_mkl_operation(operation), native_handle(handle), triangular_matrix_descriptor(triangle, diagonal),
              SPARSE_LAYOUT_ROW_MAJOR, rhs.data(), columns, columns, output.data(),
              static_cast<MKL_INT>(output.cols()));
}

template <typename T>
[[nodiscard]] bool spmv_impl(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> vector,
                             ksj::array::VectorView<T> output) {
  if (matrix.cols() != vector.size() || matrix.rows() != output.size() || !vector.is_contiguous() ||
      !output.is_contiguous() || ksj::array::detail::views_may_overlap(vector, output)) {
    return false;
  }

  sparse_matrix_t handle = nullptr;
  if (!create_csr(&handle, matrix)) {
    return false;
  }

  optimize_handle(handle);
  const auto ok = mv(handle, vector.data(), output.data());
  const auto destroy_status = mkl_sparse_destroy(handle);
  return ok && check_status(destroy_status);
}

template <typename T>
[[nodiscard]] bool spmm_impl(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> dense,
                             ksj::array::MatrixView<T> output, const SparseOperation operation) {
  if (!matrix_view_is_row_major_contiguous(dense) || !matrix_view_is_row_major_contiguous(output) ||
      ksj::array::detail::views_may_overlap(dense, output)) {
    return false;
  }

  sparse_matrix_t handle = nullptr;
  if (!create_csr(&handle, matrix)) {
    return false;
  }

  optimize_handle(handle);
  const auto layout = SPARSE_LAYOUT_ROW_MAJOR;
  const auto ldx = static_cast<MKL_INT>(dense.cols());
  const auto ldy = static_cast<MKL_INT>(output.cols());
  const auto ok = mm(to_mkl_operation(operation), handle, layout, dense.data(), static_cast<MKL_INT>(dense.cols()), ldx,
                     output.data(), ldy);
  const auto destroy_status = mkl_sparse_destroy(handle);
  return ok && check_status(destroy_status);
}

template <typename T>
[[nodiscard]] bool convert_csr_impl(const CsrMatrix<T>& matrix, CsrMatrix<T>& output, const SparseOperation operation) {
  sparse_matrix_t source = nullptr;
  if (!create_csr(&source, matrix)) {
    return false;
  }

  sparse_matrix_t converted = nullptr;
  const auto converted_ok = check_status(mkl_sparse_convert_csr(source, to_mkl_operation(operation), &converted));
  if (converted_ok) {
    order_handle(converted);
  }
  const auto exported_ok = converted_ok && export_csr(converted, output);
  const auto converted_destroy_status = converted != nullptr ? mkl_sparse_destroy(converted) : SPARSE_STATUS_SUCCESS;
  const auto source_destroy_status = mkl_sparse_destroy(source);
  return exported_ok && check_status(converted_destroy_status) && check_status(source_destroy_status);
}

template <typename T>
[[nodiscard]] bool add_impl(const CsrMatrix<T>& lhs, const T& alpha, const CsrMatrix<T>& rhs, CsrMatrix<T>& output,
                            const SparseOperation operation) {
  sparse_matrix_t lhs_handle = nullptr;
  sparse_matrix_t rhs_handle = nullptr;
  if (!create_csr(&lhs_handle, lhs)) {
    return false;
  }
  if (!create_csr(&rhs_handle, rhs)) {
    (void)mkl_sparse_destroy(lhs_handle);
    return false;
  }

  sparse_matrix_t sum = nullptr;
  const auto add_ok = sparse_add(to_mkl_operation(operation), lhs_handle, alpha, rhs_handle, &sum);
  if (add_ok) {
    order_handle(sum);
  }
  const auto exported_ok = add_ok && export_csr(sum, output);
  const auto sum_destroy_status = sum != nullptr ? mkl_sparse_destroy(sum) : SPARSE_STATUS_SUCCESS;
  const auto rhs_destroy_status = mkl_sparse_destroy(rhs_handle);
  const auto lhs_destroy_status = mkl_sparse_destroy(lhs_handle);
  return exported_ok && check_status(sum_destroy_status) && check_status(rhs_destroy_status) &&
         check_status(lhs_destroy_status);
}

template <typename T>
[[nodiscard]] bool spsv_impl(const CsrMatrix<T>& matrix, ksj::array::VectorView<const T> rhs,
                             ksj::array::VectorView<T> output, const SparseTriangle triangle,
                             const SparseDiagonal diagonal, const SparseOperation operation) {
  const auto operation_rows = operation == SparseOperation::none ? matrix.rows() : matrix.cols();
  const auto operation_cols = operation == SparseOperation::none ? matrix.cols() : matrix.rows();
  if (operation_rows != operation_cols || rhs.size() != operation_cols || output.size() != operation_rows ||
      !rhs.is_contiguous() || !output.is_contiguous() || ksj::array::detail::views_may_overlap(rhs, output)) {
    return false;
  }

  sparse_matrix_t handle = nullptr;
  if (!create_csr(&handle, matrix)) {
    return false;
  }

  optimize_handle(handle);
  const auto ok = trsv(to_mkl_operation(operation), handle, triangular_matrix_descriptor(triangle, diagonal),
                       rhs.data(), output.data());
  const auto destroy_status = mkl_sparse_destroy(handle);
  return ok && check_status(destroy_status);
}

template <typename T>
[[nodiscard]] bool spsm_impl(const CsrMatrix<T>& matrix, ksj::array::MatrixView<const T> rhs,
                             ksj::array::MatrixView<T> output, const SparseTriangle triangle,
                             const SparseDiagonal diagonal, const SparseOperation operation) {
  const auto operation_rows = operation == SparseOperation::none ? matrix.rows() : matrix.cols();
  const auto operation_cols = operation == SparseOperation::none ? matrix.cols() : matrix.rows();
  if (operation_rows != operation_cols || rhs.rows() != operation_cols || output.rows() != operation_rows ||
      output.cols() != rhs.cols() || !matrix_view_is_row_major_contiguous(rhs) ||
      !matrix_view_is_row_major_contiguous(output) || ksj::array::detail::views_may_overlap(rhs, output)) {
    return false;
  }

  sparse_matrix_t handle = nullptr;
  if (!create_csr(&handle, matrix)) {
    return false;
  }

  optimize_handle(handle);
  const auto layout = SPARSE_LAYOUT_ROW_MAJOR;
  const auto ldx = static_cast<MKL_INT>(rhs.cols());
  const auto ldy = static_cast<MKL_INT>(output.cols());
  const auto ok = trsm(to_mkl_operation(operation), handle, triangular_matrix_descriptor(triangle, diagonal), layout,
                       rhs.data(), static_cast<MKL_INT>(rhs.cols()), ldx, output.data(), ldy);
  const auto destroy_status = mkl_sparse_destroy(handle);
  return ok && check_status(destroy_status);
}

} // namespace

bool make_handle(const CsrMatrix<float>& matrix, SparseHandle& handle) {
  return make_handle_impl(matrix, handle);
}

bool make_handle(const CsrMatrix<double>& matrix, SparseHandle& handle) {
  return make_handle_impl(matrix, handle);
}

bool make_handle(const CsrMatrix<ksj::base::cf32>& matrix, SparseHandle& handle) {
  return make_handle_impl(matrix, handle);
}

bool make_handle(const CsrMatrix<ksj::base::cf64>& matrix, SparseHandle& handle) {
  return make_handle_impl(matrix, handle);
}

bool spmv(const SparseHandle& handle, ksj::array::VectorView<const float> vector,
          ksj::array::VectorView<float> output) {
  return spmv_handle_impl(handle, vector, output);
}

bool spmv(const SparseHandle& handle, ksj::array::VectorView<const double> vector,
          ksj::array::VectorView<double> output) {
  return spmv_handle_impl(handle, vector, output);
}

bool spmv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf32> vector,
          ksj::array::VectorView<ksj::base::cf32> output) {
  return spmv_handle_impl(handle, vector, output);
}

bool spmv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf64> vector,
          ksj::array::VectorView<ksj::base::cf64> output) {
  return spmv_handle_impl(handle, vector, output);
}

bool spmv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> vector,
          ksj::array::VectorView<float> output) {
  return spmv_impl(matrix, vector, output);
}

bool spmv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> vector,
          ksj::array::VectorView<double> output) {
  return spmv_impl(matrix, vector, output);
}

bool spmv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> vector,
          ksj::array::VectorView<ksj::base::cf32> output) {
  return spmv_impl(matrix, vector, output);
}

bool spmv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> vector,
          ksj::array::VectorView<ksj::base::cf64> output) {
  return spmv_impl(matrix, vector, output);
}

bool spmm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> dense,
          ksj::array::MatrixView<float> output, const SparseOperation operation) {
  return spmm_impl(matrix, dense, output, operation);
}

bool spmm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> dense,
          ksj::array::MatrixView<double> output, const SparseOperation operation) {
  return spmm_impl(matrix, dense, output, operation);
}

bool spmm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> dense,
          ksj::array::MatrixView<ksj::base::cf32> output, const SparseOperation operation) {
  return spmm_impl(matrix, dense, output, operation);
}

bool spmm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> dense,
          ksj::array::MatrixView<ksj::base::cf64> output, const SparseOperation operation) {
  return spmm_impl(matrix, dense, output, operation);
}

bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const float> dense, ksj::array::MatrixView<float> output,
          const SparseOperation operation) {
  return spmm_handle_impl(handle, dense, output, operation);
}

bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const double> dense, ksj::array::MatrixView<double> output,
          const SparseOperation operation) {
  return spmm_handle_impl(handle, dense, output, operation);
}

bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf32> dense,
          ksj::array::MatrixView<ksj::base::cf32> output, const SparseOperation operation) {
  return spmm_handle_impl(handle, dense, output, operation);
}

bool spmm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf64> dense,
          ksj::array::MatrixView<ksj::base::cf64> output, const SparseOperation operation) {
  return spmm_handle_impl(handle, dense, output, operation);
}

bool convert_csr(const CsrMatrix<float>& matrix, CsrMatrix<float>& output, const SparseOperation operation) {
  return convert_csr_impl(matrix, output, operation);
}

bool convert_csr(const CsrMatrix<double>& matrix, CsrMatrix<double>& output, const SparseOperation operation) {
  return convert_csr_impl(matrix, output, operation);
}

bool convert_csr(const CsrMatrix<ksj::base::cf32>& matrix, CsrMatrix<ksj::base::cf32>& output,
                 const SparseOperation operation) {
  return convert_csr_impl(matrix, output, operation);
}

bool convert_csr(const CsrMatrix<ksj::base::cf64>& matrix, CsrMatrix<ksj::base::cf64>& output,
                 const SparseOperation operation) {
  return convert_csr_impl(matrix, output, operation);
}

bool add(const CsrMatrix<float>& lhs, const float alpha, const CsrMatrix<float>& rhs, CsrMatrix<float>& output,
         const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, output, operation);
}

bool add(const CsrMatrix<double>& lhs, const double alpha, const CsrMatrix<double>& rhs, CsrMatrix<double>& output,
         const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, output, operation);
}

bool add(const CsrMatrix<ksj::base::cf32>& lhs, const ksj::base::cf32 alpha, const CsrMatrix<ksj::base::cf32>& rhs,
         CsrMatrix<ksj::base::cf32>& output, const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, output, operation);
}

bool add(const CsrMatrix<ksj::base::cf64>& lhs, const ksj::base::cf64 alpha, const CsrMatrix<ksj::base::cf64>& rhs,
         CsrMatrix<ksj::base::cf64>& output, const SparseOperation operation) {
  return add_impl(lhs, alpha, rhs, output, operation);
}

bool spsv(const CsrMatrix<float>& matrix, ksj::array::VectorView<const float> rhs, ksj::array::VectorView<float> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  return spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsv(const CsrMatrix<double>& matrix, ksj::array::VectorView<const double> rhs,
          ksj::array::VectorView<double> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsv(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::VectorView<const ksj::base::cf32> rhs,
          ksj::array::VectorView<ksj::base::cf32> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsv(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::VectorView<const ksj::base::cf64> rhs,
          ksj::array::VectorView<ksj::base::cf64> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsv_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsv(const SparseHandle& handle, ksj::array::VectorView<const float> rhs, ksj::array::VectorView<float> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  return spsv_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsv(const SparseHandle& handle, ksj::array::VectorView<const double> rhs, ksj::array::VectorView<double> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  return spsv_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf32> rhs,
          ksj::array::VectorView<ksj::base::cf32> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsv_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsv(const SparseHandle& handle, ksj::array::VectorView<const ksj::base::cf64> rhs,
          ksj::array::VectorView<ksj::base::cf64> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsv_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsm(const CsrMatrix<float>& matrix, ksj::array::MatrixView<const float> rhs, ksj::array::MatrixView<float> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  return spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsm(const CsrMatrix<double>& matrix, ksj::array::MatrixView<const double> rhs,
          ksj::array::MatrixView<double> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsm(const CsrMatrix<ksj::base::cf32>& matrix, ksj::array::MatrixView<const ksj::base::cf32> rhs,
          ksj::array::MatrixView<ksj::base::cf32> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsm(const CsrMatrix<ksj::base::cf64>& matrix, ksj::array::MatrixView<const ksj::base::cf64> rhs,
          ksj::array::MatrixView<ksj::base::cf64> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsm_impl(matrix, rhs, output, triangle, diagonal, operation);
}

bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const float> rhs, ksj::array::MatrixView<float> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  return spsm_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const double> rhs, ksj::array::MatrixView<double> output,
          const SparseTriangle triangle, const SparseDiagonal diagonal, const SparseOperation operation) {
  return spsm_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf32> rhs,
          ksj::array::MatrixView<ksj::base::cf32> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsm_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

bool spsm(const SparseHandle& handle, ksj::array::MatrixView<const ksj::base::cf64> rhs,
          ksj::array::MatrixView<ksj::base::cf64> output, const SparseTriangle triangle, const SparseDiagonal diagonal,
          const SparseOperation operation) {
  return spsm_handle_impl(handle, rhs, output, triangle, diagonal, operation);
}

} // namespace ksj::sparse::detail::intel
