#include "kspacejet/linalg/detail/intel/intel_linalg_blas.hpp"
#include "intel_linalg_common.hpp"

namespace ksj::linalg::detail::intel {

template <typename T>
[[nodiscard]] bool matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,
                          ksj::array::MatrixView<T> output) {
  if constexpr (blas_gemm_scalar_v<T>) {
    if (lhs.cols() != rhs.rows() || output.rows() != lhs.rows() || output.cols() != rhs.cols() ||
        lhs.col_stride() != 1U || rhs.col_stride() != 1U || output.col_stride() != 1U || !fits_blas_int(lhs.rows()) ||
        !fits_blas_int(lhs.cols()) || !fits_blas_int(rhs.cols()) || !fits_blas_int(lhs.row_stride()) ||
        !fits_blas_int(rhs.row_stride()) || !fits_blas_int(output.row_stride())) {
      return false;
    }
    gemm(lhs.data(), rhs.data(), output.data(), lhs.rows(), lhs.cols(), rhs.cols(), lhs.row_stride(), rhs.row_stride(),
         output.row_stride());
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,
                          ksj::array::MatrixView<T> output, const T& alpha) {
  if constexpr (blas_gemm_scalar_v<T>) {
    if (lhs.cols() != rhs.rows() || output.rows() != lhs.rows() || output.cols() != rhs.cols() ||
        lhs.col_stride() != 1U || rhs.col_stride() != 1U || output.col_stride() != 1U || !fits_blas_int(lhs.rows()) ||
        !fits_blas_int(lhs.cols()) || !fits_blas_int(rhs.cols()) || !fits_blas_int(lhs.row_stride()) ||
        !fits_blas_int(rhs.row_stride()) || !fits_blas_int(output.row_stride())) {
      return false;
    }
    gemm_scaled(lhs.data(), rhs.data(), output.data(), lhs.rows(), lhs.cols(), rhs.cols(), lhs.row_stride(),
                rhs.row_stride(), output.row_stride(), alpha);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool matmul(const ksj::array::PooledMatrix<T>& lhs, const ksj::array::PooledMatrix<T>& rhs,
                          ksj::array::PooledMatrix<T>& output) {
  return matmul(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), output.view());
}

template <typename T>
[[nodiscard]] bool hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,
                                  const ksj::array::real_scalar_t<T> scale) {
  if constexpr (blas_gemm_scalar_v<T>) {
    if (input.empty() || output.rows() != input.cols() || output.cols() != input.cols() || !input.is_contiguous() ||
        !output.is_contiguous() || !fits_blas_int(input.rows()) || !fits_blas_int(input.cols()) ||
        !fits_blas_int(input.row_stride()) || !fits_blas_int(output.row_stride())) {
      return false;
    }

    gram(input.data(), output.data(), input.rows(), input.cols(), scale);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,
                        ksj::array::VectorView<T> output) {
  if constexpr (blas_gemv_scalar_v<T>) {
    if (matrix.cols() != vector.size() || matrix.rows() != output.size() || matrix.col_stride() != 1U ||
        !fits_blas_int(matrix.rows()) || !fits_blas_int(matrix.cols()) || !fits_blas_int(matrix.row_stride()) ||
        !fits_blas_int(vector.stride()) || !fits_blas_int(output.stride())) {
      return false;
    }
    gemv(matrix.data(), vector.data(), output.data(), matrix.rows(), matrix.cols(), matrix.row_stride(),
         vector.stride(), output.stride());
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool gemv(const ksj::array::PooledMatrix<T>& matrix, const ksj::array::PooledVector<T>& vector,
                        ksj::array::PooledVector<T>& output) {
  return gemv(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(vector.view()), output.view());
}

template <typename T>
[[nodiscard]] bool dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs, T& output) {
  if constexpr (blas_dot_scalar_v<T>) {
    if (lhs.size() != rhs.size() || !lhs.is_contiguous() || !rhs.is_contiguous() ||
        !dot_length_supported<T>(lhs.size())) {
      return false;
    }
    return dot(lhs.data(), rhs.data(), lhs.size(), output);
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool dot(const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs, T& output) {
  return dot(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), output);
}

template <typename T>
[[nodiscard]] bool dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs, T& output) {
  if constexpr (blas_dot_scalar_v<T>) {
    if (lhs.size() != rhs.size() || !lhs.is_contiguous() || !rhs.is_contiguous() ||
        !dotu_length_supported<T>(lhs.size())) {
      return false;
    }
    return dotu(lhs.data(), rhs.data(), lhs.size(), output);
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool dotu(const ksj::array::PooledVector<T>& lhs, const ksj::array::PooledVector<T>& rhs, T& output) {
  return dotu(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()), output);
}

template <typename T> [[nodiscard]] bool copy(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) {
  if constexpr (blas_gemm_scalar_v<T>) {
    if (input.size() != output.size() || !input.is_contiguous() || !output.is_contiguous() ||
        !fits_blas_int(input.size())) {
      return false;
    }
    copy(input.data(), output.data(), input.size());
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool scale(ksj::array::VectorView<const T> input, const T& scalar, ksj::array::VectorView<T> output) {
  if constexpr (blas_gemm_scalar_v<T>) {
    if (input.size() != output.size() || !input.is_contiguous() || !output.is_contiguous() ||
        !fits_blas_int(input.size())) {
      return false;
    }
    if (input.data() != output.data()) {
      copy(input.data(), output.data(), input.size());
    }
    scale_in_place(output.data(), output.size(), scalar);
    return true;
  } else {
    return false;
  }
}

template <typename T>
[[nodiscard]] bool axpy(const T& alpha, ksj::array::VectorView<const T> x, ksj::array::VectorView<const T> y,
                        ksj::array::VectorView<T> output) {
  if constexpr (blas_gemm_scalar_v<T>) {
    if (x.size() != y.size() || x.size() != output.size() || !x.is_contiguous() || !y.is_contiguous() ||
        !output.is_contiguous() || !fits_blas_int(x.size())) {
      return false;
    }
    if (output.data() == x.data() && output.data() != y.data()) {
      return false;
    }
    if (output.data() != y.data()) {
      copy(y.data(), output.data(), y.size());
    }
    axpy_in_place(alpha, x.data(), output.data(), x.size());
    return true;
  } else {
    return false;
  }
}

#define KSJ_LINALG_INTEL_BLAS_WRAPPERS(T)                                                                              \
  bool matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output) {                                                                      \
    return matmul<T>(lhs, rhs, output);                                                                                \
  }                                                                                                                    \
  bool matmul(ksj::array::MatrixView<const T> lhs, ksj::array::MatrixView<const T> rhs,                                \
              ksj::array::MatrixView<T> output, const T& alpha) {                                                      \
    return matmul<T>(lhs, rhs, output, alpha);                                                                         \
  }                                                                                                                    \
  bool hermitian_gram(ksj::array::MatrixView<const T> input, ksj::array::MatrixView<T> output,                         \
                      ksj::array::real_scalar_t<T> scale) {                                                            \
    return hermitian_gram<T>(input, output, scale);                                                                    \
  }                                                                                                                    \
  bool gemv(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> vector,                            \
            ksj::array::VectorView<T> output) {                                                                        \
    return gemv<T>(matrix, vector, output);                                                                            \
  }                                                                                                                    \
  bool dot(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs, T& output) {                      \
    return dot<T>(lhs, rhs, output);                                                                                   \
  }                                                                                                                    \
  bool dotu(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs, T& output) {                     \
    return dotu<T>(lhs, rhs, output);                                                                                  \
  }                                                                                                                    \
  bool copy(ksj::array::VectorView<const T> input, ksj::array::VectorView<T> output) {                                 \
    return copy<T>(input, output);                                                                                     \
  }                                                                                                                    \
  bool scale(ksj::array::VectorView<const T> input, const T& scalar, ksj::array::VectorView<T> output) {               \
    return scale<T>(input, scalar, output);                                                                            \
  }                                                                                                                    \
  bool axpy(const T& alpha, ksj::array::VectorView<const T> x, ksj::array::VectorView<const T> y,                      \
            ksj::array::VectorView<T> output) {                                                                        \
    return axpy<T>(alpha, x, y, output);                                                                               \
  }

KSJ_LINALG_INTEL_BLAS_WRAPPERS(float)
KSJ_LINALG_INTEL_BLAS_WRAPPERS(double)
KSJ_LINALG_INTEL_BLAS_WRAPPERS(ksj::base::cf32)
KSJ_LINALG_INTEL_BLAS_WRAPPERS(ksj::base::cf64)

#undef KSJ_LINALG_INTEL_BLAS_WRAPPERS

} // namespace ksj::linalg::detail::intel
