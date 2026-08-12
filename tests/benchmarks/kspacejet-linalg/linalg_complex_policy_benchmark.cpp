#include "linalg_benchmark_common.hpp"

#include "kspacejet/linalg/detail/linalg_policy.hpp"

#include <algorithm>
#include <complex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ksj::benchmarks::linalg_benchmarks {
namespace {

inline constexpr std::string_view kEigenBackend = "eigen";
inline constexpr std::string_view kIntelBackend = "intel_lapacke";
inline constexpr std::string_view kAllocatingScope = "allocating";
inline constexpr std::string_view kWorkspaceReuseScope = "workspace_reuse";

[[nodiscard]] constexpr std::string_view selected_backend(const bool prefers_intel) noexcept {
  return prefers_intel ? kIntelBackend : kEigenBackend;
}

template <typename Real> [[nodiscard]] constexpr double complex_relative_tolerance() noexcept {
  if constexpr (std::is_same_v<Real, float>) {
    return 1.0e-3;
  }
  return 1.0e-10;
}

inline void require_success(const bool success, const std::string_view operation) {
  if (!success) {
    throw std::runtime_error("linalg policy benchmark backend failed: " + std::string(operation));
  }
}

template <typename EigenOperation, typename IntelOperation, typename PolicyOperation, typename Probe, typename Checksum>
void emit_allocating_policy_rows(const std::string_view case_name, const std::string_view type_name,
                                 const std::size_t size, const ksj::benchmarks::Config& config,
                                 const std::string_view policy_backend, const double relative_tolerance,
                                 EigenOperation&& eigen_operation, IntelOperation&& intel_operation,
                                 PolicyOperation&& policy_operation, Probe&& probe, Checksum&& checksum) {
  auto eigen_result = eigen_operation();
  const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
    eigen_result = eigen_operation();
    ksj::benchmarks::do_not_optimize(probe(eigen_result));
  });
  ksj::benchmarks::print_row(case_name, kEigenBackend, type_name, size, config, eigen_ns, checksum(eigen_result),
                             ksj::benchmarks::reference_row(case_name, kAllocatingScope, -1.0, relative_tolerance));

  auto intel_result = intel_operation();
  const auto intel_ns = ksj::benchmarks::measure(config, [&] {
    intel_result = intel_operation();
    ksj::benchmarks::do_not_optimize(probe(intel_result));
  });
  ksj::benchmarks::print_row(case_name, kIntelBackend, type_name, size, config, intel_ns, checksum(intel_result),
                             ksj::benchmarks::candidate_row(case_name, kAllocatingScope, -1.0, relative_tolerance));

  auto policy_result = policy_operation();
  const auto policy_ns = ksj::benchmarks::measure(config, [&] {
    policy_result = policy_operation();
    ksj::benchmarks::do_not_optimize(probe(policy_result));
  });
  ksj::benchmarks::print_row(
    case_name, "public_policy", type_name, size, config, policy_ns, checksum(policy_result),
    ksj::benchmarks::policy_row(case_name, kAllocatingScope, policy_backend, -1.0, relative_tolerance));
}

template <typename EigenOperation, typename IntelOperation, typename PolicyOperation, typename EigenChecksum,
          typename IntelChecksum, typename PolicyChecksum>
void emit_workspace_policy_rows(const std::string_view case_name, const std::string_view type_name,
                                const std::size_t size, const ksj::benchmarks::Config& config,
                                const std::string_view policy_backend, const double relative_tolerance,
                                EigenOperation&& eigen_operation, IntelOperation&& intel_operation,
                                PolicyOperation&& policy_operation, EigenChecksum&& eigen_checksum,
                                IntelChecksum&& intel_checksum, PolicyChecksum&& policy_checksum) {
  (void)eigen_operation();
  const auto eigen_ns = ksj::benchmarks::measure(config, [&] {
    ksj::benchmarks::do_not_optimize(eigen_operation());
  });
  ksj::benchmarks::print_row(case_name, kEigenBackend, type_name, size, config, eigen_ns, eigen_checksum(),
                             ksj::benchmarks::reference_row(case_name, kWorkspaceReuseScope, -1.0, relative_tolerance));

  (void)intel_operation();
  const auto intel_ns = ksj::benchmarks::measure(config, [&] {
    ksj::benchmarks::do_not_optimize(intel_operation());
  });
  ksj::benchmarks::print_row(case_name, kIntelBackend, type_name, size, config, intel_ns, intel_checksum(),
                             ksj::benchmarks::candidate_row(case_name, kWorkspaceReuseScope, -1.0, relative_tolerance));

  (void)policy_operation();
  const auto policy_ns = ksj::benchmarks::measure(config, [&] {
    ksj::benchmarks::do_not_optimize(policy_operation());
  });
  ksj::benchmarks::print_row(
    case_name, "public_policy", type_name, size, config, policy_ns, policy_checksum(),
    ksj::benchmarks::policy_row(case_name, kWorkspaceReuseScope, policy_backend, -1.0, relative_tolerance));
}

template <typename Real>
void run_for_complex_type(const std::string_view type_name, const ksj::benchmarks::Config& config) {
  using complex_type = std::complex<Real>;
  using decomposition_type = ksj::linalg::SingularValueDecomposition<complex_type>;
  using eigen_type = ksj::linalg::EigenDecomposition<complex_type>;

  constexpr std::string_view inverse_case = "complex_inverse";
  constexpr std::string_view lu_vector_case = "complex_solve_vector_lu";
  constexpr std::string_view lu_matrix_case = "complex_solve_matrix_lu";
  constexpr std::string_view qr_vector_case = "complex_solve_vector_qr";
  constexpr std::string_view qr_matrix_case = "complex_solve_matrix_qr";
  constexpr std::string_view svd_case = "complex_svd_full_uv";
  constexpr std::string_view general_eigen_case = "complex_general_eigen";

  const auto relative_tolerance = complex_relative_tolerance<Real>();
  for (const auto size : config.sizes) {
    if (size > kMaxDecompositionSize) {
      continue;
    }

    auto matrix = ksj::array::make_pooled_matrix<complex_type>(size, size);
    auto vector_rhs = ksj::array::make_pooled_vector<complex_type>(size);
    const auto rhs_cols = std::min(kMatrixRhsCols, size);
    auto matrix_rhs = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    const auto sample_rows = size * 2U;
    auto qr_matrix = ksj::array::make_pooled_matrix<complex_type>(sample_rows, size);
    auto qr_vector_rhs = ksj::array::make_pooled_vector<complex_type>(sample_rows);
    auto qr_matrix_rhs = ksj::array::make_pooled_matrix<complex_type>(sample_rows, rhs_cols);
    ksj::benchmarks::require_pooled_storage("policy_gate_matrix", matrix);
    ksj::benchmarks::require_pooled_storage("policy_gate_vector_rhs", vector_rhs);
    ksj::benchmarks::require_pooled_storage("policy_gate_matrix_rhs", matrix_rhs);
    ksj::benchmarks::require_pooled_storage("policy_gate_qr_matrix", qr_matrix);
    ksj::benchmarks::require_pooled_storage("policy_gate_qr_vector_rhs", qr_vector_rhs);
    ksj::benchmarks::require_pooled_storage("policy_gate_qr_matrix_rhs", qr_matrix_rhs);
    fill_complex_matrix(matrix);
    fill_complex_vector(vector_rhs);
    fill_complex_matrix(matrix_rhs);
    fill_complex_matrix(qr_matrix);
    fill_complex_vector(qr_vector_rhs);
    fill_complex_matrix(qr_matrix_rhs);

    const auto matrix_view = const_matrix_view(matrix);
    const auto vector_rhs_view = const_vector_view(vector_rhs);
    const auto matrix_rhs_view = const_matrix_view(matrix_rhs);
    const auto qr_matrix_view = const_matrix_view(qr_matrix);
    const auto qr_vector_rhs_view = const_vector_view(qr_vector_rhs);
    const auto qr_matrix_rhs_view = const_matrix_view(qr_matrix_rhs);

    const auto inverse_backend = selected_backend(ksj::linalg::detail::prefer_intel_inverse<complex_type>(size));
    const auto inverse_workspace_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_inverse_workspace<complex_type>(size));
    emit_allocating_policy_rows(
      inverse_case, type_name, size, config, inverse_backend, relative_tolerance,
      [&] {
        return ksj::linalg::detail::eigen::inverse(matrix_view);
      },
      [&] {
        auto output = ksj::array::make_pooled_matrix<complex_type>(size, size);
        require_success(ksj::linalg::detail::intel::inverse(matrix_view, output.view()), inverse_case);
        return output;
      },
      [&] {
        return ksj::linalg::inverse(matrix);
      },
      [](const auto& result) {
        return result.data()[0];
      },
      [](const auto& result) {
        return checksum(result);
      });

    auto eigen_inverse_output = ksj::array::make_pooled_matrix<complex_type>(size, size);
    auto intel_inverse_output = ksj::array::make_pooled_matrix<complex_type>(size, size);
    auto policy_inverse_output = ksj::array::make_pooled_matrix<complex_type>(size, size);
    ksj::linalg::LuFactorWorkspace<complex_type> eigen_inverse_workspace;
    ksj::linalg::LuFactorWorkspace<complex_type> intel_inverse_workspace;
    ksj::linalg::LuFactorWorkspace<complex_type> policy_inverse_workspace;
    emit_workspace_policy_rows(
      inverse_case, type_name, size, config, inverse_workspace_backend, relative_tolerance,
      [&] {
        require_success(
          ksj::linalg::detail::eigen::inverse(matrix_view, eigen_inverse_output.view(), eigen_inverse_workspace),
          inverse_case);
        return eigen_inverse_output.data()[0];
      },
      [&] {
        require_success(
          ksj::linalg::detail::intel::inverse(matrix_view, intel_inverse_output.view(), intel_inverse_workspace),
          inverse_case);
        return intel_inverse_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::inverse(matrix_view, policy_inverse_output.view(), policy_inverse_workspace),
                        inverse_case);
        return policy_inverse_output.data()[0];
      },
      [&] {
        return checksum(eigen_inverse_output);
      },
      [&] {
        return checksum(intel_inverse_output);
      },
      [&] {
        return checksum(policy_inverse_output);
      });

    const auto lu_vector_backend = selected_backend(ksj::linalg::detail::prefer_intel_solve_lu<complex_type>(size));
    const auto lu_vector_workspace_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_solve_lu_workspace<complex_type>(size));
    emit_allocating_policy_rows(
      lu_vector_case, type_name, size, config, lu_vector_backend, relative_tolerance,
      [&] {
        return ksj::linalg::detail::eigen::solve(matrix, vector_rhs);
      },
      [&] {
        auto output = ksj::array::make_pooled_vector<complex_type>(size);
        ksj::linalg::LuSolveWorkspace<complex_type> workspace;
        require_success(ksj::linalg::detail::intel::solve_lu(matrix_view, vector_rhs_view, output.view(), workspace),
                        lu_vector_case);
        return output;
      },
      [&] {
        return ksj::linalg::solve(matrix, vector_rhs);
      },
      [](const auto& result) {
        return result.data()[0];
      },
      [](const auto& result) {
        return checksum(result);
      });

    auto eigen_lu_vector_output = ksj::array::make_pooled_vector<complex_type>(size);
    auto intel_lu_vector_output = ksj::array::make_pooled_vector<complex_type>(size);
    auto policy_lu_vector_output = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::linalg::LuSolveWorkspace<complex_type> eigen_lu_vector_workspace;
    ksj::linalg::LuSolveWorkspace<complex_type> intel_lu_vector_workspace;
    ksj::linalg::LuSolveWorkspace<complex_type> policy_lu_vector_workspace;
    emit_workspace_policy_rows(
      lu_vector_case, type_name, size, config, lu_vector_workspace_backend, relative_tolerance,
      [&] {
        require_success(ksj::linalg::detail::eigen::solve_lu(matrix_view, vector_rhs_view,
                                                             eigen_lu_vector_output.view(), eigen_lu_vector_workspace),
                        lu_vector_case);
        return eigen_lu_vector_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::detail::intel::solve_lu(matrix_view, vector_rhs_view,
                                                             intel_lu_vector_output.view(), intel_lu_vector_workspace),
                        lu_vector_case);
        return intel_lu_vector_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::solve_lu(matrix_view, vector_rhs_view, policy_lu_vector_output.view(),
                                              policy_lu_vector_workspace),
                        lu_vector_case);
        return policy_lu_vector_output.data()[0];
      },
      [&] {
        return checksum(eigen_lu_vector_output);
      },
      [&] {
        return checksum(intel_lu_vector_output);
      },
      [&] {
        return checksum(policy_lu_vector_output);
      });

    const auto lu_matrix_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_solve_lu_matrix<complex_type>(size, rhs_cols));
    const auto lu_matrix_workspace_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_solve_lu_matrix_workspace<complex_type>(size, rhs_cols));
    emit_allocating_policy_rows(
      lu_matrix_case, type_name, size, config, lu_matrix_backend, relative_tolerance,
      [&] {
        return ksj::linalg::detail::eigen::solve(matrix, matrix_rhs);
      },
      [&] {
        auto output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
        ksj::linalg::LuSolveWorkspace<complex_type> workspace;
        require_success(ksj::linalg::detail::intel::solve_lu(matrix_view, matrix_rhs_view, output.view(), workspace),
                        lu_matrix_case);
        return output;
      },
      [&] {
        return ksj::linalg::solve(matrix, matrix_rhs);
      },
      [](const auto& result) {
        return result.data()[0];
      },
      [](const auto& result) {
        return checksum(result);
      });

    auto eigen_lu_matrix_output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    auto intel_lu_matrix_output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    auto policy_lu_matrix_output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::linalg::LuSolveWorkspace<complex_type> eigen_lu_matrix_workspace;
    ksj::linalg::LuSolveWorkspace<complex_type> intel_lu_matrix_workspace;
    ksj::linalg::LuSolveWorkspace<complex_type> policy_lu_matrix_workspace;
    emit_workspace_policy_rows(
      lu_matrix_case, type_name, size, config, lu_matrix_workspace_backend, relative_tolerance,
      [&] {
        require_success(ksj::linalg::detail::eigen::solve_lu(matrix_view, matrix_rhs_view,
                                                             eigen_lu_matrix_output.view(), eigen_lu_matrix_workspace),
                        lu_matrix_case);
        return eigen_lu_matrix_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::detail::intel::solve_lu(matrix_view, matrix_rhs_view,
                                                             intel_lu_matrix_output.view(), intel_lu_matrix_workspace),
                        lu_matrix_case);
        return intel_lu_matrix_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::solve_lu(matrix_view, matrix_rhs_view, policy_lu_matrix_output.view(),
                                              policy_lu_matrix_workspace),
                        lu_matrix_case);
        return policy_lu_matrix_output.data()[0];
      },
      [&] {
        return checksum(eigen_lu_matrix_output);
      },
      [&] {
        return checksum(intel_lu_matrix_output);
      },
      [&] {
        return checksum(policy_lu_matrix_output);
      });

    const auto qr_vector_backend = selected_backend(ksj::linalg::detail::prefer_intel_solve_qr<complex_type>(size));
    const auto qr_vector_workspace_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_solve_qr_workspace<complex_type>(size));
    emit_allocating_policy_rows(
      qr_vector_case, type_name, size, config, qr_vector_backend, relative_tolerance,
      [&] {
        return ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_vector_rhs);
      },
      [&] {
        auto output = ksj::array::make_pooled_vector<complex_type>(size);
        ksj::linalg::LeastSquaresQrWorkspace<complex_type> workspace;
        require_success(
          ksj::linalg::detail::intel::solve_qr(qr_matrix_view, qr_vector_rhs_view, output.view(), workspace),
          qr_vector_case);
        return output;
      },
      [&] {
        return ksj::linalg::solve_qr(qr_matrix, qr_vector_rhs);
      },
      [](const auto& result) {
        return result.data()[0];
      },
      [](const auto& result) {
        return checksum(result);
      });

    auto eigen_qr_vector_output = ksj::array::make_pooled_vector<complex_type>(size);
    auto intel_qr_vector_output = ksj::array::make_pooled_vector<complex_type>(size);
    auto policy_qr_vector_output = ksj::array::make_pooled_vector<complex_type>(size);
    ksj::linalg::LeastSquaresQrWorkspace<complex_type> eigen_qr_vector_workspace;
    ksj::linalg::LeastSquaresQrWorkspace<complex_type> intel_qr_vector_workspace;
    ksj::linalg::LeastSquaresQrWorkspace<complex_type> policy_qr_vector_workspace;
    emit_workspace_policy_rows(
      qr_vector_case, type_name, size, config, qr_vector_workspace_backend, relative_tolerance,
      [&] {
        require_success(ksj::linalg::detail::eigen::solve_qr(qr_matrix_view, qr_vector_rhs_view,
                                                             eigen_qr_vector_output.view(), eigen_qr_vector_workspace),
                        qr_vector_case);
        return eigen_qr_vector_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::detail::intel::solve_qr(qr_matrix_view, qr_vector_rhs_view,
                                                             intel_qr_vector_output.view(), intel_qr_vector_workspace),
                        qr_vector_case);
        return intel_qr_vector_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::solve_qr(qr_matrix_view, qr_vector_rhs_view, policy_qr_vector_output.view(),
                                              policy_qr_vector_workspace),
                        qr_vector_case);
        return policy_qr_vector_output.data()[0];
      },
      [&] {
        return checksum(eigen_qr_vector_output);
      },
      [&] {
        return checksum(intel_qr_vector_output);
      },
      [&] {
        return checksum(policy_qr_vector_output);
      });

    const auto qr_matrix_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_solve_qr_matrix<complex_type>(size, rhs_cols));
    const auto qr_matrix_workspace_backend =
      selected_backend(ksj::linalg::detail::prefer_intel_solve_qr_matrix_workspace<complex_type>(size, rhs_cols));
    emit_allocating_policy_rows(
      qr_matrix_case, type_name, size, config, qr_matrix_backend, relative_tolerance,
      [&] {
        return ksj::linalg::detail::eigen::solve_qr(qr_matrix, qr_matrix_rhs);
      },
      [&] {
        auto output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
        ksj::linalg::LeastSquaresQrWorkspace<complex_type> workspace;
        require_success(
          ksj::linalg::detail::intel::solve_qr(qr_matrix_view, qr_matrix_rhs_view, output.view(), workspace),
          qr_matrix_case);
        return output;
      },
      [&] {
        return ksj::linalg::solve_qr(qr_matrix, qr_matrix_rhs);
      },
      [](const auto& result) {
        return result.data()[0];
      },
      [](const auto& result) {
        return checksum(result);
      });

    auto eigen_qr_matrix_output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    auto intel_qr_matrix_output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    auto policy_qr_matrix_output = ksj::array::make_pooled_matrix<complex_type>(size, rhs_cols);
    ksj::linalg::LeastSquaresQrWorkspace<complex_type> eigen_qr_matrix_workspace;
    ksj::linalg::LeastSquaresQrWorkspace<complex_type> intel_qr_matrix_workspace;
    ksj::linalg::LeastSquaresQrWorkspace<complex_type> policy_qr_matrix_workspace;
    emit_workspace_policy_rows(
      qr_matrix_case, type_name, size, config, qr_matrix_workspace_backend, relative_tolerance,
      [&] {
        require_success(ksj::linalg::detail::eigen::solve_qr(qr_matrix_view, qr_matrix_rhs_view,
                                                             eigen_qr_matrix_output.view(), eigen_qr_matrix_workspace),
                        qr_matrix_case);
        return eigen_qr_matrix_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::detail::intel::solve_qr(qr_matrix_view, qr_matrix_rhs_view,
                                                             intel_qr_matrix_output.view(), intel_qr_matrix_workspace),
                        qr_matrix_case);
        return intel_qr_matrix_output.data()[0];
      },
      [&] {
        require_success(ksj::linalg::solve_qr(qr_matrix_view, qr_matrix_rhs_view, policy_qr_matrix_output.view(),
                                              policy_qr_matrix_workspace),
                        qr_matrix_case);
        return policy_qr_matrix_output.data()[0];
      },
      [&] {
        return checksum(eigen_qr_matrix_output);
      },
      [&] {
        return checksum(intel_qr_matrix_output);
      },
      [&] {
        return checksum(policy_qr_matrix_output);
      });

    if (size <= kMaxFullSvdSize) {
      const auto svd_allocating_backend =
        selected_backend(ksj::linalg::detail::prefer_intel_svd<complex_type>(size, size));
      const auto svd_workspace_backend =
        selected_backend(ksj::linalg::detail::prefer_intel_svd_workspace<complex_type>(size, size));
      emit_allocating_policy_rows(
        svd_case, type_name, size, config, svd_allocating_backend, relative_tolerance,
        [&] {
          return ksj::linalg::detail::eigen::svd(matrix_view, true);
        },
        [&] {
          auto u = ksj::array::make_pooled_matrix<complex_type>(size, size);
          auto values = ksj::array::make_pooled_vector<Real>(size);
          auto v_adjoint = ksj::array::make_pooled_matrix<complex_type>(size, size);
          require_success(ksj::linalg::detail::intel::svd(matrix_view, u, values, v_adjoint, true), svd_case);
          return decomposition_type{std::move(u), std::move(values), std::move(v_adjoint)};
        },
        [&] {
          return ksj::linalg::full_svd(matrix);
        },
        [](const auto& result) {
          return result.u.data()[0];
        },
        [](const auto& result) {
          return checksum(result.singular_values);
        });

      auto eigen_svd_u = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto eigen_svd_values = ksj::array::make_pooled_vector<Real>(size);
      auto eigen_svd_v_adjoint = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto intel_svd_u = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto intel_svd_values = ksj::array::make_pooled_vector<Real>(size);
      auto intel_svd_v_adjoint = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto policy_svd_u = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto policy_svd_values = ksj::array::make_pooled_vector<Real>(size);
      auto policy_svd_v_adjoint = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::linalg::SvdWorkspace<complex_type> intel_svd_workspace;
      ksj::linalg::SvdWorkspace<complex_type> policy_svd_workspace;
      intel_svd_workspace.resize(size, size);
      emit_workspace_policy_rows(
        svd_case, type_name, size, config, svd_workspace_backend, relative_tolerance,
        [&] {
          const auto decomposition = ksj::linalg::detail::eigen::svd(matrix_view, true);
          ksj::array::copy(decomposition.u.view(), eigen_svd_u.view());
          ksj::array::copy(decomposition.singular_values.view(), eigen_svd_values.view());
          ksj::array::copy(decomposition.v_adjoint.view(), eigen_svd_v_adjoint.view());
          return eigen_svd_u.data()[0];
        },
        [&] {
          require_success(ksj::linalg::detail::intel::svd(matrix_view, intel_svd_u.view(), intel_svd_values.view(),
                                                          intel_svd_v_adjoint.view(), intel_svd_workspace, true),
                          svd_case);
          return intel_svd_u.data()[0];
        },
        [&] {
          require_success(ksj::linalg::full_svd(matrix_view, policy_svd_u.view(), policy_svd_values.view(),
                                                policy_svd_v_adjoint.view(), policy_svd_workspace),
                          svd_case);
          return policy_svd_u.data()[0];
        },
        [&] {
          return checksum(eigen_svd_values);
        },
        [&] {
          return checksum(intel_svd_values);
        },
        [&] {
          return checksum(policy_svd_values);
        });
    }

    if (size <= kMaxGeneralEigenSize) {
      const auto general_eigen_backend =
        selected_backend(ksj::linalg::detail::prefer_intel_general_eigen<complex_type>(size));
      const auto general_eigen_workspace_backend =
        selected_backend(ksj::linalg::detail::prefer_intel_general_eigen_workspace<complex_type>(size));
      emit_allocating_policy_rows(
        general_eigen_case, type_name, size, config, general_eigen_backend, relative_tolerance,
        [&] {
          return ksj::linalg::detail::eigen::eigen_decomposition(matrix);
        },
        [&] {
          auto values = ksj::array::make_pooled_vector<complex_type>(size);
          auto vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
          require_success(ksj::linalg::detail::intel::eigen_decomposition(matrix, values, vectors), general_eigen_case);
          return eigen_type{std::move(values), std::move(vectors)};
        },
        [&] {
          return ksj::linalg::eigen_decomposition(matrix);
        },
        [](const auto& result) {
          return result.eigenvalues.data()[0];
        },
        [](const auto& result) {
          return checksum(result.eigenvalues);
        });

      auto eigen_general_values = ksj::array::make_pooled_vector<complex_type>(size);
      auto eigen_general_vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto intel_general_values = ksj::array::make_pooled_vector<complex_type>(size);
      auto intel_general_vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
      auto policy_general_values = ksj::array::make_pooled_vector<complex_type>(size);
      auto policy_general_vectors = ksj::array::make_pooled_matrix<complex_type>(size, size);
      ksj::linalg::GeneralEigenWorkspace<complex_type> eigen_general_workspace;
      ksj::linalg::GeneralEigenWorkspace<complex_type> intel_general_workspace;
      ksj::linalg::GeneralEigenWorkspace<complex_type> policy_general_workspace;
      emit_workspace_policy_rows(
        general_eigen_case, type_name, size, config, general_eigen_workspace_backend, relative_tolerance,
        [&] {
          require_success(ksj::linalg::detail::eigen::eigen_decomposition(matrix_view, eigen_general_values.view(),
                                                                          eigen_general_vectors.view(),
                                                                          eigen_general_workspace),
                          general_eigen_case);
          return eigen_general_values.data()[0];
        },
        [&] {
          require_success(ksj::linalg::detail::intel::eigen_decomposition(matrix_view, intel_general_values.view(),
                                                                          intel_general_vectors.view(),
                                                                          intel_general_workspace),
                          general_eigen_case);
          return intel_general_values.data()[0];
        },
        [&] {
          require_success(ksj::linalg::eigen_decomposition(matrix_view, policy_general_values.view(),
                                                           policy_general_vectors.view(), policy_general_workspace),
                          general_eigen_case);
          return policy_general_values.data()[0];
        },
        [&] {
          return checksum(eigen_general_values);
        },
        [&] {
          return checksum(intel_general_values);
        },
        [&] {
          return checksum(policy_general_values);
        });
    }
  }
}

} // namespace

void run_complex_policy_gate_benchmarks_float(const ksj::benchmarks::Config& config) {
  run_for_complex_type<float>("complex_float", config);
}

void run_complex_policy_gate_benchmarks_double(const ksj::benchmarks::Config& config) {
  run_for_complex_type<double>("complex_double", config);
}

} // namespace ksj::benchmarks::linalg_benchmarks
