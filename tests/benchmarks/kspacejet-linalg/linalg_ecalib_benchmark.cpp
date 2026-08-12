#include "linalg_benchmark_common.hpp"

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <stdexcept>

namespace ksj::benchmarks::linalg_benchmarks {
namespace {

using complex_type = std::complex<float>;

inline constexpr std::size_t kKernelFactor = 8U;
inline constexpr std::size_t kVoxelBatch = 128U;
inline constexpr std::array<std::size_t, 4U> kFixedCoilCounts{4U, 8U, 16U, 32U};

[[nodiscard]] bool fixed_coil_count_supported(const std::size_t coils) noexcept {
  return std::find(kFixedCoilCounts.begin(), kFixedCoilCounts.end(), coils) != kFixedCoilCounts.end();
}

void fill_voxel_matrices(ksj::array::PooledVector<complex_type>& source, const std::size_t coils,
                         const std::size_t kernels) {
  for (std::size_t voxel = 0U; voxel < kVoxelBatch; ++voxel) {
    const auto voxel_offset = voxel * coils * kernels;
    for (std::size_t row = 0U; row < coils; ++row) {
      for (std::size_t col = 0U; col < kernels; ++col) {
        const auto real_seed = (voxel * 17U + row * 31U + col * 7U + 3U) % 251U;
        const auto imag_seed = (voxel * 11U + row * 13U + col * 19U + 5U) % 127U;
        source[voxel_offset + row * kernels + col] = {
          static_cast<float>(static_cast<double>(real_seed) * 0.001),
          static_cast<float>(static_cast<double>(imag_seed) * 0.0005),
        };
      }
    }
  }
}

void copy_voxel_matrix(const ksj::array::PooledVector<complex_type>& source, const std::size_t voxel,
                       ksj::array::MatrixView<complex_type> output) {
  const auto kernels = output.cols();
  const auto source_offset = voxel * output.rows() * kernels;
  auto input = ksj::array::MatrixView<const complex_type>(source.data() + source_offset, output.rows(), kernels);
  ksj::array::copy(input, output);
}

[[nodiscard]] double checksum_left_vectors(ksj::array::MatrixView<const complex_type> matrix) {
  double sum = 0.0;
  for (std::size_t index = 0U; index < matrix.size(); ++index) {
    sum += static_cast<double>(std::abs(matrix[index]));
  }
  return sum;
}

template <std::size_t Coils>
void fixed_size_left_vectors(const complex_type* source, ksj::array::MatrixView<complex_type> output) {
  constexpr auto kernels = Coils * kKernelFactor;
  using input_matrix = Eigen::Matrix<complex_type, static_cast<int>(Coils), static_cast<int>(kernels), Eigen::RowMajor>;
  using gram_matrix = Eigen::Matrix<complex_type, static_cast<int>(Coils), static_cast<int>(Coils), Eigen::RowMajor>;

  input_matrix matrix;
  for (std::size_t row = 0U; row < Coils; ++row) {
    for (std::size_t col = 0U; col < kernels; ++col) {
      matrix(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)) = source[row * kernels + col];
    }
  }

  const gram_matrix gram = matrix * matrix.adjoint();
  Eigen::SelfAdjointEigenSolver<gram_matrix> solver(gram);
  if (solver.info() != Eigen::Success) {
    throw std::runtime_error("ecalib fixed-size eigensolve failed");
  }

  const auto eigenvectors = solver.eigenvectors();
  auto output_map = as_eigen(output);
  for (std::size_t col = 0U; col < Coils; ++col) {
    output_map.col(static_cast<Eigen::Index>(col)) = eigenvectors.col(static_cast<Eigen::Index>(Coils - 1U - col));
  }
}

void fixed_size_left_vectors(const ksj::array::PooledVector<complex_type>& source, const std::size_t voxel,
                             const std::size_t coils, ksj::array::MatrixView<complex_type> output) {
  const auto kernels = coils * kKernelFactor;
  const auto* voxel_source = source.data() + voxel * coils * kernels;
  switch (coils) {
    case 4U:
      fixed_size_left_vectors<4U>(voxel_source, output);
      return;
    case 8U:
      fixed_size_left_vectors<8U>(voxel_source, output);
      return;
    case 16U:
      fixed_size_left_vectors<16U>(voxel_source, output);
      return;
    case 32U:
      fixed_size_left_vectors<32U>(voxel_source, output);
      return;
    default:
      throw std::invalid_argument("unsupported ecalib fixed-size coil count");
  }
}

void run_for_coil_count(const std::size_t coils, const ksj::benchmarks::Config& config) {
  if (!fixed_coil_count_supported(coils)) {
    return;
  }

  const auto kernels = coils * kKernelFactor;
  auto source = ksj::array::make_pooled_vector<complex_type>(kVoxelBatch * coils * kernels);
  auto matrix = ksj::array::make_pooled_matrix<complex_type>(coils, kernels);
  auto left_vectors = ksj::array::make_pooled_matrix<complex_type>(coils, coils);
  ksj::benchmarks::require_pooled_storage("ecalib_source", source);
  ksj::benchmarks::require_pooled_storage("ecalib_matrix", matrix);
  ksj::benchmarks::require_pooled_storage("ecalib_left_vectors", left_vectors);
  fill_voxel_matrices(source, coils, kernels);

  double public_svd_checksum = 0.0;
  const auto public_svd_ns = ksj::benchmarks::measure(config, [&] {
    public_svd_checksum = 0.0;
    for (std::size_t voxel = 0U; voxel < kVoxelBatch; ++voxel) {
      copy_voxel_matrix(source, voxel, matrix.view());
      const auto decomposition = ksj::linalg::svd(matrix);
      public_svd_checksum += checksum_left_vectors(ksj::array::as_const_view(decomposition.u.view()));
    }
    ksj::benchmarks::do_not_optimize(public_svd_checksum);
  });
  ksj::benchmarks::print_row("ecalib_voxel_left_singular_vectors", "public_svd_allocating_u", "complex_float", coils,
                             config, public_svd_ns, public_svd_checksum,
                             ksj::benchmarks::candidate_row("ecalib_voxel_left_singular_vectors", "allocating"));

  double scratch_left_u_checksum = 0.0;
  const auto scratch_left_u_ns = ksj::benchmarks::measure(config, [&] {
    scratch_left_u_checksum = 0.0;
    for (std::size_t voxel = 0U; voxel < kVoxelBatch; ++voxel) {
      copy_voxel_matrix(source, voxel, matrix.view());
      ksj::linalg::left_singular_vectors(ksj::array::as_const_view(matrix.view()), left_vectors.view());
      scratch_left_u_checksum += checksum_left_vectors(ksj::array::as_const_view(left_vectors.view()));
    }
    ksj::benchmarks::do_not_optimize(scratch_left_u_checksum);
  });
  ksj::benchmarks::print_row("ecalib_voxel_left_singular_vectors", "reused_matrix_scratch", "complex_float", coils,
                             config, scratch_left_u_ns, scratch_left_u_checksum,
                             ksj::benchmarks::reference_row("ecalib_voxel_left_singular_vectors", "output_reuse"));

  double fixed_size_checksum = 0.0;
  const auto fixed_size_ns = ksj::benchmarks::measure(config, [&] {
    fixed_size_checksum = 0.0;
    for (std::size_t voxel = 0U; voxel < kVoxelBatch; ++voxel) {
      fixed_size_left_vectors(source, voxel, coils, left_vectors.view());
      fixed_size_checksum += checksum_left_vectors(ksj::array::as_const_view(left_vectors.view()));
    }
    ksj::benchmarks::do_not_optimize(fixed_size_checksum);
  });
  ksj::benchmarks::print_row("ecalib_voxel_left_singular_vectors", "fixed_size_gram_eigen", "complex_float", coils,
                             config, fixed_size_ns, fixed_size_checksum,
                             ksj::benchmarks::candidate_row("ecalib_voxel_left_singular_vectors", "output_reuse"));
}

} // namespace

void run_ecalib_svd_benchmarks_complex_float(const ksj::benchmarks::Config& config) {
  for (const auto coils : config.sizes) {
    run_for_coil_count(coils, config);
  }
}

} // namespace ksj::benchmarks::linalg_benchmarks
