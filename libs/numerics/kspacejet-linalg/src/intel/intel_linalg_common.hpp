#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"
#include "kspacejet/linalg/workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <complex>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

#include <ipp.h>
#include <mkl_cblas.h>
#include <mkl_lapacke.h>

namespace ksj::linalg::detail::intel {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;
}

template <typename T> inline constexpr bool blas_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;
template <typename T>
inline constexpr bool blas_gemm_scalar_v =
  blas_scalar_v<T> || std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;
template <typename T>
inline constexpr bool blas_gemv_scalar_v =
  blas_scalar_v<T> || std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;
template <typename T>
inline constexpr bool blas_dot_scalar_v =
  blas_scalar_v<T> || std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;
template <typename T> inline constexpr bool lapack_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;
template <typename T>
inline constexpr bool lapack_complex_scalar_v =
  std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;
template <typename T> inline constexpr bool lapack_solve_scalar_v = lapack_scalar_v<T> || lapack_complex_scalar_v<T>;

[[nodiscard]] inline lapack_complex_float* lapack_complex_cast(ksj::base::cf32* data) noexcept {
  return reinterpret_cast<lapack_complex_float*>(data);
}

[[nodiscard]] inline const lapack_complex_float* lapack_complex_cast(const ksj::base::cf32* data) noexcept {
  return reinterpret_cast<const lapack_complex_float*>(data);
}

[[nodiscard]] inline lapack_complex_double* lapack_complex_cast(ksj::base::cf64* data) noexcept {
  return reinterpret_cast<lapack_complex_double*>(data);
}

[[nodiscard]] inline const lapack_complex_double* lapack_complex_cast(const ksj::base::cf64* data) noexcept {
  return reinterpret_cast<const lapack_complex_double*>(data);
}

[[nodiscard]] inline MKL_Complex8* mkl_complex_cast(ksj::base::cf32* data) noexcept {
  return reinterpret_cast<MKL_Complex8*>(data);
}

[[nodiscard]] inline const MKL_Complex8* mkl_complex_cast(const ksj::base::cf32* data) noexcept {
  return reinterpret_cast<const MKL_Complex8*>(data);
}

[[nodiscard]] inline MKL_Complex16* mkl_complex_cast(ksj::base::cf64* data) noexcept {
  return reinterpret_cast<MKL_Complex16*>(data);
}

[[nodiscard]] inline const MKL_Complex16* mkl_complex_cast(const ksj::base::cf64* data) noexcept {
  return reinterpret_cast<const MKL_Complex16*>(data);
}

[[nodiscard]] inline Ipp32fc* ipp_complex_cast(ksj::base::cf32* data) noexcept {
  return reinterpret_cast<Ipp32fc*>(data);
}

[[nodiscard]] inline const Ipp32fc* ipp_complex_cast(const ksj::base::cf32* data) noexcept {
  return reinterpret_cast<const Ipp32fc*>(data);
}

[[nodiscard]] inline Ipp64fc* ipp_complex_cast(ksj::base::cf64* data) noexcept {
  return reinterpret_cast<Ipp64fc*>(data);
}

[[nodiscard]] inline const Ipp64fc* ipp_complex_cast(const ksj::base::cf64* data) noexcept {
  return reinterpret_cast<const Ipp64fc*>(data);
}

template <typename T> [[nodiscard]] inline T* lapack_data(T* data) noexcept {
  return data;
}

template <typename T> [[nodiscard]] inline const T* lapack_data(const T* data) noexcept {
  return data;
}

[[nodiscard]] inline lapack_complex_float* lapack_data(ksj::base::cf32* data) noexcept {
  return lapack_complex_cast(data);
}

[[nodiscard]] inline const lapack_complex_float* lapack_data(const ksj::base::cf32* data) noexcept {
  return lapack_complex_cast(data);
}

[[nodiscard]] inline lapack_complex_double* lapack_data(ksj::base::cf64* data) noexcept {
  return lapack_complex_cast(data);
}

[[nodiscard]] inline const lapack_complex_double* lapack_data(const ksj::base::cf64* data) noexcept {
  return lapack_complex_cast(data);
}

[[nodiscard]] inline bool fits_lapack_int(const std::size_t value) noexcept {
  return value <= static_cast<std::size_t>(std::numeric_limits<lapack_int>::max());
}

[[nodiscard]] inline bool fits_blas_int(const std::size_t value) noexcept {
  return value <= static_cast<std::size_t>(std::numeric_limits<MKL_INT>::max());
}

[[nodiscard]] inline bool fits_ipp_int(const std::size_t value) noexcept {
  return value <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] inline bool ipp_ok(const IppStatus status) noexcept {
  return status == ippStsNoErr;
}

template <typename T> [[nodiscard]] inline bool dot_length_supported(const std::size_t value) noexcept {
  if constexpr (blas_scalar_v<T>) {
    return fits_ipp_int(value);
  } else {
    return fits_blas_int(value);
  }
}

template <typename T> [[nodiscard]] inline bool dotu_length_supported(const std::size_t value) noexcept {
  return fits_ipp_int(value);
}

inline void gemm(const float* lhs, const float* rhs, float* output, const std::size_t rows, const std::size_t inner,
                 const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(inner);
  auto ldb = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto alpha = 1.0F;
  auto beta = 0.0F;
  cblas_sgemm(layout, trans, trans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
}

inline void gemm(const double* lhs, const double* rhs, double* output, const std::size_t rows, const std::size_t inner,
                 const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(inner);
  auto ldb = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto alpha = 1.0;
  auto beta = 0.0;
  cblas_dgemm(layout, trans, trans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
}

inline void gemm(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, ksj::base::cf32* output,
                 const std::size_t rows, const std::size_t inner, const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(inner);
  auto ldb = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto alpha = ksj::base::cf32{1.0F, 0.0F};
  auto beta = ksj::base::cf32{0.0F, 0.0F};
  cblas_cgemm(layout, trans, trans, m, n, k, mkl_complex_cast(&alpha), mkl_complex_cast(lhs), lda,
              mkl_complex_cast(rhs), ldb, mkl_complex_cast(&beta), mkl_complex_cast(output), ldc);
}

inline void gemm(const ksj::base::cf64* lhs, const ksj::base::cf64* rhs, ksj::base::cf64* output,
                 const std::size_t rows, const std::size_t inner, const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(inner);
  auto ldb = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto alpha = ksj::base::cf64{1.0, 0.0};
  auto beta = ksj::base::cf64{0.0, 0.0};
  cblas_zgemm(layout, trans, trans, m, n, k, mkl_complex_cast(&alpha), mkl_complex_cast(lhs), lda,
              mkl_complex_cast(rhs), ldb, mkl_complex_cast(&beta), mkl_complex_cast(output), ldc);
}

template <typename T>
void gemm_scaled(const T* lhs, const T* rhs, T* output, const std::size_t rows, const std::size_t inner,
                 const std::size_t cols, const std::size_t lhs_row_stride, const std::size_t rhs_row_stride,
                 const std::size_t output_row_stride, const T& alpha) {
  const auto m = static_cast<MKL_INT>(rows);
  const auto n = static_cast<MKL_INT>(cols);
  const auto k = static_cast<MKL_INT>(inner);
  const auto lda = static_cast<MKL_INT>(lhs_row_stride);
  const auto ldb = static_cast<MKL_INT>(rhs_row_stride);
  const auto ldc = static_cast<MKL_INT>(output_row_stride);
  if constexpr (std::is_same_v<T, float>) {
    const auto beta = 0.0F;
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
  } else if constexpr (std::is_same_v<T, double>) {
    const auto beta = 0.0;
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    const auto beta = ksj::base::cf32{};
    cblas_cgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, m, n, k, mkl_complex_cast(&alpha), mkl_complex_cast(lhs),
                lda, mkl_complex_cast(rhs), ldb, mkl_complex_cast(&beta), mkl_complex_cast(output), ldc);
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    const auto beta = ksj::base::cf64{};
    cblas_zgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, m, n, k, mkl_complex_cast(&alpha), mkl_complex_cast(lhs),
                lda, mkl_complex_cast(rhs), ldb, mkl_complex_cast(&beta), mkl_complex_cast(output), ldc);
  }
}

inline void gemm(const float* lhs, const float* rhs, float* output, const std::size_t rows, const std::size_t inner,
                 const std::size_t cols, const std::size_t lhs_row_stride, const std::size_t rhs_row_stride,
                 const std::size_t output_row_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(lhs_row_stride);
  auto ldb = static_cast<MKL_INT>(rhs_row_stride);
  auto ldc = static_cast<MKL_INT>(output_row_stride);
  auto alpha = 1.0F;
  auto beta = 0.0F;
  cblas_sgemm(layout, trans, trans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
}

inline void gemm(const double* lhs, const double* rhs, double* output, const std::size_t rows, const std::size_t inner,
                 const std::size_t cols, const std::size_t lhs_row_stride, const std::size_t rhs_row_stride,
                 const std::size_t output_row_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(lhs_row_stride);
  auto ldb = static_cast<MKL_INT>(rhs_row_stride);
  auto ldc = static_cast<MKL_INT>(output_row_stride);
  auto alpha = 1.0;
  auto beta = 0.0;
  cblas_dgemm(layout, trans, trans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
}

inline void gemm(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, ksj::base::cf32* output,
                 const std::size_t rows, const std::size_t inner, const std::size_t cols,
                 const std::size_t lhs_row_stride, const std::size_t rhs_row_stride,
                 const std::size_t output_row_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(lhs_row_stride);
  auto ldb = static_cast<MKL_INT>(rhs_row_stride);
  auto ldc = static_cast<MKL_INT>(output_row_stride);
  auto alpha = ksj::base::cf32{1.0F, 0.0F};
  auto beta = ksj::base::cf32{0.0F, 0.0F};
  cblas_cgemm(layout, trans, trans, m, n, k, mkl_complex_cast(&alpha), mkl_complex_cast(lhs), lda,
              mkl_complex_cast(rhs), ldb, mkl_complex_cast(&beta), mkl_complex_cast(output), ldc);
}

inline void gemm(const ksj::base::cf64* lhs, const ksj::base::cf64* rhs, ksj::base::cf64* output,
                 const std::size_t rows, const std::size_t inner, const std::size_t cols,
                 const std::size_t lhs_row_stride, const std::size_t rhs_row_stride,
                 const std::size_t output_row_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(lhs_row_stride);
  auto ldb = static_cast<MKL_INT>(rhs_row_stride);
  auto ldc = static_cast<MKL_INT>(output_row_stride);
  auto alpha = ksj::base::cf64{1.0, 0.0};
  auto beta = ksj::base::cf64{0.0, 0.0};
  cblas_zgemm(layout, trans, trans, m, n, k, mkl_complex_cast(&alpha), mkl_complex_cast(lhs), lda,
              mkl_complex_cast(rhs), ldb, mkl_complex_cast(&beta), mkl_complex_cast(output), ldc);
}

inline void gram(const float* matrix, float* output, const std::size_t rows, const std::size_t cols,
                 const float scale) {
  auto layout = CblasRowMajor;
  auto uplo = CblasUpper;
  auto trans = CblasTrans;
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(rows);
  auto lda = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto beta = 0.0F;
  cblas_ssyrk(layout, uplo, trans, n, k, scale, matrix, lda, beta, output, ldc);
  for (std::size_t row = 0U; row < cols; ++row) {
    for (std::size_t col = row + 1U; col < cols; ++col) {
      output[col * cols + row] = output[row * cols + col];
    }
  }
}

inline void gram(const double* matrix, double* output, const std::size_t rows, const std::size_t cols,
                 const double scale) {
  auto layout = CblasRowMajor;
  auto uplo = CblasUpper;
  auto trans = CblasTrans;
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(rows);
  auto lda = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto beta = 0.0;
  cblas_dsyrk(layout, uplo, trans, n, k, scale, matrix, lda, beta, output, ldc);
  for (std::size_t row = 0U; row < cols; ++row) {
    for (std::size_t col = row + 1U; col < cols; ++col) {
      output[col * cols + row] = output[row * cols + col];
    }
  }
}

inline void gram(const ksj::base::cf32* matrix, ksj::base::cf32* output, const std::size_t rows, const std::size_t cols,
                 const float scale) {
  auto layout = CblasRowMajor;
  auto uplo = CblasUpper;
  auto trans = CblasConjTrans;
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(rows);
  auto lda = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto beta = 0.0F;
  cblas_cherk(layout, uplo, trans, n, k, scale, mkl_complex_cast(matrix), lda, beta, mkl_complex_cast(output), ldc);
  for (std::size_t row = 0U; row < cols; ++row) {
    output[row * cols + row] = ksj::base::cf32{output[row * cols + row].real(), 0.0F};
    for (std::size_t col = row + 1U; col < cols; ++col) {
      output[col * cols + row] = std::conj(output[row * cols + col]);
    }
  }
}

inline void gram(const ksj::base::cf64* matrix, ksj::base::cf64* output, const std::size_t rows, const std::size_t cols,
                 const double scale) {
  auto layout = CblasRowMajor;
  auto uplo = CblasUpper;
  auto trans = CblasConjTrans;
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(rows);
  auto lda = static_cast<MKL_INT>(cols);
  auto ldc = static_cast<MKL_INT>(cols);
  auto beta = 0.0;
  cblas_zherk(layout, uplo, trans, n, k, scale, mkl_complex_cast(matrix), lda, beta, mkl_complex_cast(output), ldc);
  for (std::size_t row = 0U; row < cols; ++row) {
    output[row * cols + row] = ksj::base::cf64{output[row * cols + row].real(), 0.0};
    for (std::size_t col = row + 1U; col < cols; ++col) {
      output[col * cols + row] = std::conj(output[row * cols + col]);
    }
  }
}

inline void gemv(const float* matrix, const float* vector, float* output, const std::size_t rows,
                 const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(cols);
  auto inc = MKL_INT{1};
  auto alpha = 1.0F;
  auto beta = 0.0F;
  cblas_sgemv(layout, trans, m, n, alpha, matrix, lda, vector, inc, beta, output, inc);
}

inline void gemv(const double* matrix, const double* vector, double* output, const std::size_t rows,
                 const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(cols);
  auto inc = MKL_INT{1};
  auto alpha = 1.0;
  auto beta = 0.0;
  cblas_dgemv(layout, trans, m, n, alpha, matrix, lda, vector, inc, beta, output, inc);
}

inline void gemv(const ksj::base::cf32* matrix, const ksj::base::cf32* vector, ksj::base::cf32* output,
                 const std::size_t rows, const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(cols);
  auto inc = MKL_INT{1};
  auto alpha = ksj::base::cf32{1.0F, 0.0F};
  auto beta = ksj::base::cf32{0.0F, 0.0F};
  cblas_cgemv(layout, trans, m, n, mkl_complex_cast(&alpha), mkl_complex_cast(matrix), lda, mkl_complex_cast(vector),
              inc, mkl_complex_cast(&beta), mkl_complex_cast(output), inc);
}

inline void gemv(const ksj::base::cf64* matrix, const ksj::base::cf64* vector, ksj::base::cf64* output,
                 const std::size_t rows, const std::size_t cols) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(cols);
  auto inc = MKL_INT{1};
  auto alpha = ksj::base::cf64{1.0, 0.0};
  auto beta = ksj::base::cf64{0.0, 0.0};
  cblas_zgemv(layout, trans, m, n, mkl_complex_cast(&alpha), mkl_complex_cast(matrix), lda, mkl_complex_cast(vector),
              inc, mkl_complex_cast(&beta), mkl_complex_cast(output), inc);
}

inline void gemv(const float* matrix, const float* vector, float* output, const std::size_t rows,
                 const std::size_t cols, const std::size_t matrix_row_stride, const std::size_t vector_stride,
                 const std::size_t output_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(matrix_row_stride);
  auto incx = static_cast<MKL_INT>(vector_stride);
  auto incy = static_cast<MKL_INT>(output_stride);
  auto alpha = 1.0F;
  auto beta = 0.0F;
  cblas_sgemv(layout, trans, m, n, alpha, matrix, lda, vector, incx, beta, output, incy);
}

inline void gemv(const double* matrix, const double* vector, double* output, const std::size_t rows,
                 const std::size_t cols, const std::size_t matrix_row_stride, const std::size_t vector_stride,
                 const std::size_t output_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(matrix_row_stride);
  auto incx = static_cast<MKL_INT>(vector_stride);
  auto incy = static_cast<MKL_INT>(output_stride);
  auto alpha = 1.0;
  auto beta = 0.0;
  cblas_dgemv(layout, trans, m, n, alpha, matrix, lda, vector, incx, beta, output, incy);
}

inline void gemv(const ksj::base::cf32* matrix, const ksj::base::cf32* vector, ksj::base::cf32* output,
                 const std::size_t rows, const std::size_t cols, const std::size_t matrix_row_stride,
                 const std::size_t vector_stride, const std::size_t output_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(matrix_row_stride);
  auto incx = static_cast<MKL_INT>(vector_stride);
  auto incy = static_cast<MKL_INT>(output_stride);
  auto alpha = ksj::base::cf32{1.0F, 0.0F};
  auto beta = ksj::base::cf32{0.0F, 0.0F};
  cblas_cgemv(layout, trans, m, n, mkl_complex_cast(&alpha), mkl_complex_cast(matrix), lda, mkl_complex_cast(vector),
              incx, mkl_complex_cast(&beta), mkl_complex_cast(output), incy);
}

inline void gemv(const ksj::base::cf64* matrix, const ksj::base::cf64* vector, ksj::base::cf64* output,
                 const std::size_t rows, const std::size_t cols, const std::size_t matrix_row_stride,
                 const std::size_t vector_stride, const std::size_t output_stride) {
  auto layout = CblasRowMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto lda = static_cast<MKL_INT>(matrix_row_stride);
  auto incx = static_cast<MKL_INT>(vector_stride);
  auto incy = static_cast<MKL_INT>(output_stride);
  auto alpha = ksj::base::cf64{1.0, 0.0};
  auto beta = ksj::base::cf64{0.0, 0.0};
  cblas_zgemv(layout, trans, m, n, mkl_complex_cast(&alpha), mkl_complex_cast(matrix), lda, mkl_complex_cast(vector),
              incx, mkl_complex_cast(&beta), mkl_complex_cast(output), incy);
}

inline bool dot(const float* lhs, const float* rhs, const std::size_t size, float& output) {
  output = 0.0F;
  return ipp_ok(ippsDotProd_32f(lhs, rhs, static_cast<int>(size), &output));
}

inline bool dot(const double* lhs, const double* rhs, const std::size_t size, double& output) {
  output = 0.0;
  return ipp_ok(ippsDotProd_64f(lhs, rhs, static_cast<int>(size), &output));
}

inline bool dot(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, const std::size_t size,
                ksj::base::cf32& output) {
  auto n = static_cast<MKL_INT>(size);
  auto inc = MKL_INT{1};
  output = {};
  cblas_cdotc_sub(n, mkl_complex_cast(lhs), inc, mkl_complex_cast(rhs), inc, mkl_complex_cast(&output));
  return true;
}

inline bool dot(const ksj::base::cf64* lhs, const ksj::base::cf64* rhs, const std::size_t size,
                ksj::base::cf64& output) {
  auto n = static_cast<MKL_INT>(size);
  auto inc = MKL_INT{1};
  output = {};
  cblas_zdotc_sub(n, mkl_complex_cast(lhs), inc, mkl_complex_cast(rhs), inc, mkl_complex_cast(&output));
  return true;
}

inline bool dotu(const float* lhs, const float* rhs, const std::size_t size, float& output) {
  return dot(lhs, rhs, size, output);
}

inline bool dotu(const double* lhs, const double* rhs, const std::size_t size, double& output) {
  return dot(lhs, rhs, size, output);
}

inline bool dotu(const ksj::base::cf32* lhs, const ksj::base::cf32* rhs, const std::size_t size,
                 ksj::base::cf32& output) {
  output = {};
  return ipp_ok(
    ippsDotProd_32fc(ipp_complex_cast(lhs), ipp_complex_cast(rhs), static_cast<int>(size), ipp_complex_cast(&output)));
}

inline bool dotu(const ksj::base::cf64* lhs, const ksj::base::cf64* rhs, const std::size_t size,
                 ksj::base::cf64& output) {
  output = {};
  return ipp_ok(
    ippsDotProd_64fc(ipp_complex_cast(lhs), ipp_complex_cast(rhs), static_cast<int>(size), ipp_complex_cast(&output)));
}

inline void copy(const float* input, float* output, const std::size_t size) {
  cblas_scopy(static_cast<MKL_INT>(size), input, 1, output, 1);
}

inline void copy(const double* input, double* output, const std::size_t size) {
  cblas_dcopy(static_cast<MKL_INT>(size), input, 1, output, 1);
}

inline void copy(const ksj::base::cf32* input, ksj::base::cf32* output, const std::size_t size) {
  cblas_ccopy(static_cast<MKL_INT>(size), mkl_complex_cast(input), 1, mkl_complex_cast(output), 1);
}

inline void copy(const ksj::base::cf64* input, ksj::base::cf64* output, const std::size_t size) {
  cblas_zcopy(static_cast<MKL_INT>(size), mkl_complex_cast(input), 1, mkl_complex_cast(output), 1);
}

inline void scale_in_place(float* data, const std::size_t size, const float scalar) {
  cblas_sscal(static_cast<MKL_INT>(size), scalar, data, 1);
}

inline void scale_in_place(double* data, const std::size_t size, const double scalar) {
  cblas_dscal(static_cast<MKL_INT>(size), scalar, data, 1);
}

inline void scale_in_place(ksj::base::cf32* data, const std::size_t size, const ksj::base::cf32 scalar) {
  cblas_cscal(static_cast<MKL_INT>(size), mkl_complex_cast(&scalar), mkl_complex_cast(data), 1);
}

inline void scale_in_place(ksj::base::cf64* data, const std::size_t size, const ksj::base::cf64 scalar) {
  cblas_zscal(static_cast<MKL_INT>(size), mkl_complex_cast(&scalar), mkl_complex_cast(data), 1);
}

inline void axpy_in_place(const float alpha, const float* x, float* y, const std::size_t size) {
  cblas_saxpy(static_cast<MKL_INT>(size), alpha, x, 1, y, 1);
}

inline void axpy_in_place(const double alpha, const double* x, double* y, const std::size_t size) {
  cblas_daxpy(static_cast<MKL_INT>(size), alpha, x, 1, y, 1);
}

inline void axpy_in_place(const ksj::base::cf32 alpha, const ksj::base::cf32* x, ksj::base::cf32* y,
                          const std::size_t size) {
  cblas_caxpy(static_cast<MKL_INT>(size), mkl_complex_cast(&alpha), mkl_complex_cast(x), 1, mkl_complex_cast(y), 1);
}

inline void axpy_in_place(const ksj::base::cf64 alpha, const ksj::base::cf64* x, ksj::base::cf64* y,
                          const std::size_t size) {
  cblas_zaxpy(static_cast<MKL_INT>(size), mkl_complex_cast(&alpha), mkl_complex_cast(x), 1, mkl_complex_cast(y), 1);
}

[[nodiscard]] inline lapack_int gesv(const lapack_int n, const lapack_int nrhs, float* matrix, const lapack_int lda,
                                     lapack_int* ipiv, float* rhs, const lapack_int ldb) {
  return LAPACKE_sgesv(LAPACK_ROW_MAJOR, n, nrhs, matrix, lda, ipiv, rhs, ldb);
}

[[nodiscard]] inline lapack_int gesv(const lapack_int n, const lapack_int nrhs, double* matrix, const lapack_int lda,
                                     lapack_int* ipiv, double* rhs, const lapack_int ldb) {
  return LAPACKE_dgesv(LAPACK_ROW_MAJOR, n, nrhs, matrix, lda, ipiv, rhs, ldb);
}

[[nodiscard]] inline lapack_int gesv(const lapack_int n, const lapack_int nrhs, lapack_complex_float* matrix,
                                     const lapack_int lda, lapack_int* ipiv, lapack_complex_float* rhs,
                                     const lapack_int ldb) {
  return LAPACKE_cgesv(LAPACK_ROW_MAJOR, n, nrhs, matrix, lda, ipiv, rhs, ldb);
}

[[nodiscard]] inline lapack_int gesv(const lapack_int n, const lapack_int nrhs, lapack_complex_double* matrix,
                                     const lapack_int lda, lapack_int* ipiv, lapack_complex_double* rhs,
                                     const lapack_int ldb) {
  return LAPACKE_zgesv(LAPACK_ROW_MAJOR, n, nrhs, matrix, lda, ipiv, rhs, ldb);
}

template <typename T>
[[nodiscard]] lapack_int
gesvx(const lapack_int n, const lapack_int nrhs, T* matrix, const lapack_int lda, T* factor, const lapack_int ldaf,
      lapack_int* pivots, char* equed, ksj::array::real_scalar_t<T>* row_scale,
      ksj::array::real_scalar_t<T>* column_scale, T* rhs, const lapack_int ldb, T* output, const lapack_int ldx,
      ksj::array::real_scalar_t<T>* reciprocal_condition, ksj::array::real_scalar_t<T>* forward_error,
      ksj::array::real_scalar_t<T>* backward_error, ksj::array::real_scalar_t<T>* reciprocal_pivot_growth) {
  if constexpr (std::is_same_v<T, float>) {
    return LAPACKE_sgesvx(LAPACK_ROW_MAJOR, 'N', 'N', n, nrhs, matrix, lda, factor, ldaf, pivots, equed, row_scale,
                          column_scale, rhs, ldb, output, ldx, reciprocal_condition, forward_error, backward_error,
                          reciprocal_pivot_growth);
  } else if constexpr (std::is_same_v<T, double>) {
    return LAPACKE_dgesvx(LAPACK_ROW_MAJOR, 'N', 'N', n, nrhs, matrix, lda, factor, ldaf, pivots, equed, row_scale,
                          column_scale, rhs, ldb, output, ldx, reciprocal_condition, forward_error, backward_error,
                          reciprocal_pivot_growth);
  } else if constexpr (std::is_same_v<T, ksj::base::cf32>) {
    return LAPACKE_cgesvx(LAPACK_ROW_MAJOR, 'N', 'N', n, nrhs, lapack_complex_cast(matrix), lda,
                          lapack_complex_cast(factor), ldaf, pivots, equed, row_scale, column_scale,
                          lapack_complex_cast(rhs), ldb, lapack_complex_cast(output), ldx, reciprocal_condition,
                          forward_error, backward_error, reciprocal_pivot_growth);
  } else if constexpr (std::is_same_v<T, ksj::base::cf64>) {
    return LAPACKE_zgesvx(LAPACK_ROW_MAJOR, 'N', 'N', n, nrhs, lapack_complex_cast(matrix), lda,
                          lapack_complex_cast(factor), ldaf, pivots, equed, row_scale, column_scale,
                          lapack_complex_cast(rhs), ldb, lapack_complex_cast(output), ldx, reciprocal_condition,
                          forward_error, backward_error, reciprocal_pivot_growth);
  } else {
    return lapack_int{-1};
  }
}

[[nodiscard]] inline lapack_int getrf(const lapack_int rows, const lapack_int cols, float* matrix, const lapack_int lda,
                                      lapack_int* ipiv) {
  return LAPACKE_sgetrf(LAPACK_ROW_MAJOR, rows, cols, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getrf(const lapack_int rows, const lapack_int cols, double* matrix,
                                      const lapack_int lda, lapack_int* ipiv) {
  return LAPACKE_dgetrf(LAPACK_ROW_MAJOR, rows, cols, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getrf(const lapack_int rows, const lapack_int cols, lapack_complex_float* matrix,
                                      const lapack_int lda, lapack_int* ipiv) {
  return LAPACKE_cgetrf(LAPACK_ROW_MAJOR, rows, cols, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getrf(const lapack_int rows, const lapack_int cols, lapack_complex_double* matrix,
                                      const lapack_int lda, lapack_int* ipiv) {
  return LAPACKE_zgetrf(LAPACK_ROW_MAJOR, rows, cols, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getri(const lapack_int n, float* matrix, const lapack_int lda, const lapack_int* ipiv) {
  return LAPACKE_sgetri(LAPACK_ROW_MAJOR, n, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getri(const lapack_int n, double* matrix, const lapack_int lda,
                                      const lapack_int* ipiv) {
  return LAPACKE_dgetri(LAPACK_ROW_MAJOR, n, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getri(const lapack_int n, lapack_complex_float* matrix, const lapack_int lda,
                                      const lapack_int* ipiv) {
  return LAPACKE_cgetri(LAPACK_ROW_MAJOR, n, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int getri(const lapack_int n, lapack_complex_double* matrix, const lapack_int lda,
                                      const lapack_int* ipiv) {
  return LAPACKE_zgetri(LAPACK_ROW_MAJOR, n, matrix, lda, ipiv);
}

[[nodiscard]] inline lapack_int potrf(const lapack_int n, float* matrix, const lapack_int lda) {
  return LAPACKE_spotrf(LAPACK_ROW_MAJOR, 'L', n, matrix, lda);
}

[[nodiscard]] inline lapack_int potrf(const lapack_int n, double* matrix, const lapack_int lda) {
  return LAPACKE_dpotrf(LAPACK_ROW_MAJOR, 'L', n, matrix, lda);
}

[[nodiscard]] inline lapack_int potrf(const lapack_int n, lapack_complex_float* matrix, const lapack_int lda) {
  return LAPACKE_cpotrf(LAPACK_ROW_MAJOR, 'L', n, matrix, lda);
}

[[nodiscard]] inline lapack_int potrf(const lapack_int n, lapack_complex_double* matrix, const lapack_int lda) {
  return LAPACKE_zpotrf(LAPACK_ROW_MAJOR, 'L', n, matrix, lda);
}

[[nodiscard]] inline lapack_int trtri(const lapack_int n, float* matrix, const lapack_int lda) {
  return LAPACKE_strtri(LAPACK_ROW_MAJOR, 'L', 'N', n, matrix, lda);
}

[[nodiscard]] inline lapack_int trtri(const lapack_int n, double* matrix, const lapack_int lda) {
  return LAPACKE_dtrtri(LAPACK_ROW_MAJOR, 'L', 'N', n, matrix, lda);
}

[[nodiscard]] inline lapack_int trtri(const lapack_int n, lapack_complex_float* matrix, const lapack_int lda) {
  return LAPACKE_ctrtri(LAPACK_ROW_MAJOR, 'L', 'N', n, matrix, lda);
}

[[nodiscard]] inline lapack_int trtri(const lapack_int n, lapack_complex_double* matrix, const lapack_int lda) {
  return LAPACKE_ztrtri(LAPACK_ROW_MAJOR, 'L', 'N', n, matrix, lda);
}

[[nodiscard]] inline lapack_int potrs(const lapack_int n, const lapack_int nrhs, const float* matrix,
                                      const lapack_int lda, float* rhs, const lapack_int ldb) {
  return LAPACKE_spotrs(LAPACK_ROW_MAJOR, 'L', n, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int potrs(const lapack_int n, const lapack_int nrhs, const double* matrix,
                                      const lapack_int lda, double* rhs, const lapack_int ldb) {
  return LAPACKE_dpotrs(LAPACK_ROW_MAJOR, 'L', n, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int potrs(const lapack_int n, const lapack_int nrhs, const lapack_complex_float* matrix,
                                      const lapack_int lda, lapack_complex_float* rhs, const lapack_int ldb) {
  return LAPACKE_cpotrs(LAPACK_ROW_MAJOR, 'L', n, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int potrs(const lapack_int n, const lapack_int nrhs, const lapack_complex_double* matrix,
                                      const lapack_int lda, lapack_complex_double* rhs, const lapack_int ldb) {
  return LAPACKE_zpotrs(LAPACK_ROW_MAJOR, 'L', n, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int gels(const lapack_int rows, const lapack_int cols, const lapack_int nrhs, float* matrix,
                                     const lapack_int lda, float* rhs, const lapack_int ldb) {
  return LAPACKE_sgels(LAPACK_ROW_MAJOR, 'N', rows, cols, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int gels(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                     double* matrix, const lapack_int lda, double* rhs, const lapack_int ldb) {
  return LAPACKE_dgels(LAPACK_ROW_MAJOR, 'N', rows, cols, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int gels(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                     lapack_complex_float* matrix, const lapack_int lda, lapack_complex_float* rhs,
                                     const lapack_int ldb) {
  return LAPACKE_cgels(LAPACK_ROW_MAJOR, 'N', rows, cols, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int gels(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                     lapack_complex_double* matrix, const lapack_int lda, lapack_complex_double* rhs,
                                     const lapack_int ldb) {
  return LAPACKE_zgels(LAPACK_ROW_MAJOR, 'N', rows, cols, nrhs, matrix, lda, rhs, ldb);
}

[[nodiscard]] inline lapack_int gelss(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      float* matrix, const lapack_int lda, float* rhs, const lapack_int ldb,
                                      float* values, const float rcond, lapack_int* rank) {
  return LAPACKE_sgelss(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, values, rcond, rank);
}

[[nodiscard]] inline lapack_int gelss(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      double* matrix, const lapack_int lda, double* rhs, const lapack_int ldb,
                                      double* values, const double rcond, lapack_int* rank) {
  return LAPACKE_dgelss(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, values, rcond, rank);
}

[[nodiscard]] inline lapack_int gelss(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      lapack_complex_float* matrix, const lapack_int lda, lapack_complex_float* rhs,
                                      const lapack_int ldb, float* values, const float rcond, lapack_int* rank) {
  return LAPACKE_cgelss(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, values, rcond, rank);
}

[[nodiscard]] inline lapack_int gelss(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      lapack_complex_double* matrix, const lapack_int lda, lapack_complex_double* rhs,
                                      const lapack_int ldb, double* values, const double rcond, lapack_int* rank) {
  return LAPACKE_zgelss(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, values, rcond, rank);
}

[[nodiscard]] inline lapack_int gelsy(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      float* matrix, const lapack_int lda, float* rhs, const lapack_int ldb,
                                      lapack_int* pivots, const float rcond, lapack_int* rank) {
  return LAPACKE_sgelsy(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, pivots, rcond, rank);
}

[[nodiscard]] inline lapack_int gelsy(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      double* matrix, const lapack_int lda, double* rhs, const lapack_int ldb,
                                      lapack_int* pivots, const double rcond, lapack_int* rank) {
  return LAPACKE_dgelsy(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, pivots, rcond, rank);
}

[[nodiscard]] inline lapack_int gelsy(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      lapack_complex_float* matrix, const lapack_int lda, lapack_complex_float* rhs,
                                      const lapack_int ldb, lapack_int* pivots, const float rcond, lapack_int* rank) {
  return LAPACKE_cgelsy(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, pivots, rcond, rank);
}

[[nodiscard]] inline lapack_int gelsy(const lapack_int rows, const lapack_int cols, const lapack_int nrhs,
                                      lapack_complex_double* matrix, const lapack_int lda, lapack_complex_double* rhs,
                                      const lapack_int ldb, lapack_int* pivots, const double rcond, lapack_int* rank) {
  return LAPACKE_zgelsy(LAPACK_ROW_MAJOR, rows, cols, nrhs, matrix, lda, rhs, ldb, pivots, rcond, rank);
}

[[nodiscard]] inline lapack_int gesvd(const lapack_int rows, const lapack_int cols, float* matrix, const lapack_int lda,
                                      float* values, float* superb) {
  return LAPACKE_sgesvd(LAPACK_ROW_MAJOR, 'N', 'N', rows, cols, matrix, lda, values, nullptr, rows, nullptr, cols,
                        superb);
}

[[nodiscard]] inline lapack_int gesvd(const lapack_int rows, const lapack_int cols, double* matrix,
                                      const lapack_int lda, double* values, double* superb) {
  return LAPACKE_dgesvd(LAPACK_ROW_MAJOR, 'N', 'N', rows, cols, matrix, lda, values, nullptr, rows, nullptr, cols,
                        superb);
}

[[nodiscard]] inline lapack_int gesvd(const char jobu, const char jobvt, const lapack_int rows, const lapack_int cols,
                                      float* matrix, const lapack_int lda, float* values, float* u,
                                      const lapack_int ldu, float* vt, const lapack_int ldvt, float* superb) {
  return LAPACKE_sgesvd(LAPACK_ROW_MAJOR, jobu, jobvt, rows, cols, matrix, lda, values, u, ldu, vt, ldvt, superb);
}

[[nodiscard]] inline lapack_int gesvd(const char jobu, const char jobvt, const lapack_int rows, const lapack_int cols,
                                      double* matrix, const lapack_int lda, double* values, double* u,
                                      const lapack_int ldu, double* vt, const lapack_int ldvt, double* superb) {
  return LAPACKE_dgesvd(LAPACK_ROW_MAJOR, jobu, jobvt, rows, cols, matrix, lda, values, u, ldu, vt, ldvt, superb);
}

[[nodiscard]] inline lapack_int gesvd(const char jobu, const char jobvt, const lapack_int rows, const lapack_int cols,
                                      lapack_complex_float* matrix, const lapack_int lda, float* values,
                                      lapack_complex_float* u, const lapack_int ldu, lapack_complex_float* vt,
                                      const lapack_int ldvt, float* superb) {
  return LAPACKE_cgesvd(LAPACK_ROW_MAJOR, jobu, jobvt, rows, cols, matrix, lda, values, u, ldu, vt, ldvt, superb);
}

[[nodiscard]] inline lapack_int gesvd(const char jobu, const char jobvt, const lapack_int rows, const lapack_int cols,
                                      lapack_complex_double* matrix, const lapack_int lda, double* values,
                                      lapack_complex_double* u, const lapack_int ldu, lapack_complex_double* vt,
                                      const lapack_int ldvt, double* superb) {
  return LAPACKE_zgesvd(LAPACK_ROW_MAJOR, jobu, jobvt, rows, cols, matrix, lda, values, u, ldu, vt, ldvt, superb);
}

[[nodiscard]] inline lapack_int syev(const lapack_int n, float* matrix, const lapack_int lda, float* values) {
  return LAPACKE_ssyev(LAPACK_ROW_MAJOR, 'V', 'L', n, matrix, lda, values);
}

[[nodiscard]] inline lapack_int syev(const lapack_int n, double* matrix, const lapack_int lda, double* values) {
  return LAPACKE_dsyev(LAPACK_ROW_MAJOR, 'V', 'L', n, matrix, lda, values);
}

[[nodiscard]] inline lapack_int heev(const lapack_int n, lapack_complex_float* matrix, const lapack_int lda,
                                     float* values) {
  return LAPACKE_cheev(LAPACK_ROW_MAJOR, 'V', 'L', n, matrix, lda, values);
}

[[nodiscard]] inline lapack_int heev(const lapack_int n, lapack_complex_double* matrix, const lapack_int lda,
                                     double* values) {
  return LAPACKE_zheev(LAPACK_ROW_MAJOR, 'V', 'L', n, matrix, lda, values);
}

[[nodiscard]] inline lapack_int geev(const lapack_int n, float* matrix, const lapack_int lda, float* real_values,
                                     float* imag_values, float* right_vectors, const lapack_int ldvr) {
  return LAPACKE_sgeev(LAPACK_ROW_MAJOR, 'N', 'V', n, matrix, lda, real_values, imag_values, nullptr, n, right_vectors,
                       ldvr);
}

[[nodiscard]] inline lapack_int geev(const lapack_int n, double* matrix, const lapack_int lda, double* real_values,
                                     double* imag_values, double* right_vectors, const lapack_int ldvr) {
  return LAPACKE_dgeev(LAPACK_ROW_MAJOR, 'N', 'V', n, matrix, lda, real_values, imag_values, nullptr, n, right_vectors,
                       ldvr);
}

[[nodiscard]] inline lapack_int geev(const lapack_int n, lapack_complex_float* matrix, const lapack_int lda,
                                     lapack_complex_float* values, lapack_complex_float* right_vectors,
                                     const lapack_int ldvr) {
  return LAPACKE_cgeev(LAPACK_ROW_MAJOR, 'N', 'V', n, matrix, lda, values, nullptr, n, right_vectors, ldvr);
}

[[nodiscard]] inline lapack_int geev(const lapack_int n, lapack_complex_double* matrix, const lapack_int lda,
                                     lapack_complex_double* values, lapack_complex_double* right_vectors,
                                     const lapack_int ldvr) {
  return LAPACKE_zgeev(LAPACK_ROW_MAJOR, 'N', 'V', n, matrix, lda, values, nullptr, n, right_vectors, ldvr);
}

} // namespace ksj::linalg::detail::intel
