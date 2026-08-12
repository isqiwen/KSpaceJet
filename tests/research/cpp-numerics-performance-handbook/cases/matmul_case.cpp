#include "common.hpp"

#include <mkl_cblas.h>

namespace {

using namespace ksj::research::cpp_numerics_performance;

inline void mkl_gemm(const float* lhs, const float* rhs, float* output, const std::size_t rows, const std::size_t inner,
                     const std::size_t cols) {
  auto layout = CblasColMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(rows);
  auto ldb = static_cast<MKL_INT>(inner);
  auto ldc = static_cast<MKL_INT>(rows);
  auto alpha = 1.0F;
  auto beta = 0.0F;
  cblas_sgemm(layout, trans, trans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
}

inline void mkl_gemm(const double* lhs, const double* rhs, double* output, const std::size_t rows,
                     const std::size_t inner, const std::size_t cols) {
  auto layout = CblasColMajor;
  auto trans = CblasNoTrans;
  auto m = static_cast<MKL_INT>(rows);
  auto n = static_cast<MKL_INT>(cols);
  auto k = static_cast<MKL_INT>(inner);
  auto lda = static_cast<MKL_INT>(rows);
  auto ldb = static_cast<MKL_INT>(inner);
  auto ldc = static_cast<MKL_INT>(rows);
  auto alpha = 1.0;
  auto beta = 0.0;
  cblas_dgemm(layout, trans, trans, m, n, k, alpha, lhs, lda, rhs, ldb, beta, output, ldc);
}

template <typename T> void run_for_type(std::string_view type_name, const Config& config) {
  for (const auto size : config.sizes) {
    if (size > 2048U) {
      continue;
    }
    auto lhs = make_matrix<T>(size, size);
    auto rhs = make_matrix<T>(size, size);
    auto output = make_matrix<T>(size, size);
    ksj::research::cpp_numerics_performance::require_cache_line_aligned("matmul lhs", lhs.data());
    ksj::research::cpp_numerics_performance::require_cache_line_aligned("matmul rhs", rhs.data());
    ksj::research::cpp_numerics_performance::require_cache_line_aligned("matmul output", output.data());
    ksj::research::cpp_numerics_performance::fill_matrix(lhs);
    ksj::research::cpp_numerics_performance::fill_matrix(rhs);

    const auto eigen_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
      as_eigen(output) = as_eigen(lhs) * as_eigen(rhs);
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("matmul", "eigen_expression", type_name, size, 0, config,
                                                       eigen_measurement,
                                                       ksj::research::cpp_numerics_performance::checksum(output));

    const auto intel_measurement = ksj::research::cpp_numerics_performance::measure(config, [&] {
      mkl_gemm(lhs.data(), rhs.data(), output.data(), lhs.rows(), lhs.cols(), rhs.cols());
      ksj::research::cpp_numerics_performance::do_not_optimize(output.data()[0]);
    });
    ksj::research::cpp_numerics_performance::print_row("matmul", "intel_mkl_cblas", type_name, size, 0, config,
                                                       intel_measurement,
                                                       ksj::research::cpp_numerics_performance::checksum(output));
  }
}

} // namespace

int main(int argc, char** argv) {
  Config config;
  ksj::research::cpp_numerics_performance::parse_args(
    argc, argv, config, "usage: ksj_numerics_perf_matmul [--iterations N] [--trials N] [--sizes A,B,C]");
  ksj::research::cpp_numerics_performance::initialize_numerics_runtime();
  ksj::research::cpp_numerics_performance::print_header(config);
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
