#include "benchmark_common.hpp"
#include "kspacejet/optimization/optimization.hpp"
#include "kspacejet/linalg/detail/eigen/eigen_linalg_solvers.hpp"
#include "kspacejet/linalg/detail/intel/intel_linalg_solvers.hpp"
#include "kspacejet/linalg/detail/linalg_policy.hpp"

#include <string_view>
#include <type_traits>

namespace {

template <typename T>
void fill_least_squares_problem(ksj::array::PooledMatrix<T>& matrix, ksj::array::PooledVector<T>& rhs) {
  for (std::size_t col = 0; col < matrix.cols(); ++col) {
    for (std::size_t row = 0; row < matrix.rows(); ++row) {
      const auto smooth = static_cast<double>((row + 1U) * (col + 3U) % 97U) * 0.001;
      const auto diagonal = row == col ? 2.0 : 0.0;
      matrix(row, col) = static_cast<T>(diagonal + smooth);
    }
  }
  for (std::size_t row = 0; row < rhs.size(); ++row) {
    rhs(row) = static_cast<T>(static_cast<double>((row % 251U) + 1U) * 0.125);
  }
}

template <typename T> void run_for_type(std::string_view type_name, const ksj::benchmarks::Config& config) {
  constexpr auto absolute_tolerance = std::is_same_v<T, float> ? 1.0e-4 : 1.0e-10;
  constexpr auto relative_tolerance = std::is_same_v<T, float> ? 1.0e-5 : 1.0e-10;
  for (const auto size : config.sizes) {
    const auto rows = size * 2U;
    const auto cols = size;
    auto matrix = ksj::array::make_pooled_matrix<T>(rows, cols);
    auto rhs = ksj::array::make_pooled_vector<T>(rows);
    ksj::benchmarks::require_pooled_storage("matrix", matrix);
    ksj::benchmarks::require_pooled_storage("rhs", rhs);
    fill_least_squares_problem(matrix, rhs);
    const auto matrix_view = ksj::array::as_const_view(matrix.view());
    const auto rhs_view = ksj::array::as_const_view(rhs.view());

    auto eigen_output = ksj::array::make_pooled_vector<T>(cols);
    auto eigen_workspace = ksj::linalg::LeastSquaresQrWorkspace<T>{};
    const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::linalg::detail::eigen::solve_qr(matrix_view, rhs_view, eigen_output.view(), eigen_workspace)) {
        throw std::runtime_error("Eigen QR least-squares backend failed");
      }
      ksj::benchmarks::do_not_optimize(eigen_output.data()[0]);
    });
    ksj::benchmarks::print_row(
      "least_squares_qr", "eigen", type_name, size, config, eigen_ns, ksj::benchmarks::checksum(eigen_output),
      ksj::benchmarks::reference_row("least_squares_qr", "output_reuse", absolute_tolerance, relative_tolerance));

    auto intel_output = ksj::array::make_pooled_vector<T>(cols);
    auto intel_workspace = ksj::linalg::LeastSquaresQrWorkspace<T>{};
    const auto intel_ns = ksj::benchmarks::measure(config, [&] {
      if (!ksj::linalg::detail::intel::solve_qr(matrix_view, rhs_view, intel_output.view(), intel_workspace)) {
        throw std::runtime_error("Intel LAPACKE least_squares_qr backend failed");
      }
      ksj::benchmarks::do_not_optimize(intel_output.data()[0]);
    });
    ksj::benchmarks::print_row(
      "least_squares_qr", "intel_lapacke", type_name, size, config, intel_ns, ksj::benchmarks::checksum(intel_output),
      ksj::benchmarks::candidate_row("least_squares_qr", "output_reuse", absolute_tolerance, relative_tolerance));

    auto public_output = ksj::array::make_pooled_vector<T>(cols);
    auto public_workspace = ksj::optimization::LeastSquaresWorkspace<T>{};
    const auto public_ns = ksj::benchmarks::measure(config, [&] {
      ksj::optimization::least_squares(matrix_view, rhs_view, public_output.view(), public_workspace,
                                       ksj::optimization::LeastSquaresMethod::qr);
      ksj::benchmarks::do_not_optimize(public_output.data()[0]);
    });
    ksj::benchmarks::print_row(
      "least_squares_qr", "public_policy", type_name, size, config, public_ns, ksj::benchmarks::checksum(public_output),
      ksj::benchmarks::policy_row("least_squares_qr", "output_reuse",
                                  ksj::linalg::detail::prefer_intel_solve_qr_workspace<T>(cols) ? "intel_lapacke"
                                                                                                : "eigen",
                                  absolute_tolerance, relative_tolerance));
  }
}

} // namespace

int main(int argc, char** argv) {
  ksj::benchmarks::Config config;
  ksj::benchmarks::parse_args(argc, argv, config,
                              "usage: ksj_optimization_backend_benchmark [--iterations N] [--sizes 16,32,64]");
  ksj::benchmarks::initialize_numerics_runtime();
  ksj::benchmarks::print_header();
  run_for_type<float>("float", config);
  run_for_type<double>("double", config);
  return 0;
}
