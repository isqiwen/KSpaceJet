#include "kspacejet/base/types.hpp"
#include "kspacejet/nufft/detail/bart/bart_nufft2.hpp"

#include "kspacejet/array/array.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>

#if defined(KSJ_NUFFT_HAS_BART)
#include "bart.h"
#endif

namespace ksj::nufft::detail::bart {

#if defined(KSJ_NUFFT_HAS_BART)
namespace {

[[nodiscard]] ccx* as_bart_complex(ksj::base::cf32* value) noexcept {
  return reinterpret_cast<ccx*>(value);
}

[[nodiscard]] const ccx* as_bart_complex(const ksj::base::cf32* value) noexcept {
  return reinterpret_cast<const ccx*>(value);
}

template <typename T> void validate_common(const Grid2D grid, ksj::array::MatrixView<const T> trajectory) {
  if (grid.rows == 0U || grid.cols == 0U) {
    throw std::invalid_argument("BART NUFFT grid must be non-empty");
  }
  if (trajectory.cols() != 2U) {
    throw std::invalid_argument("BART NUFFT 2D trajectory must have two columns");
  }
}

[[nodiscard]] auto make_dims() {
  std::array<long, DIMS> dims{};
  dims.fill(1L);
  return dims;
}

void pack_bart_trajectory(const Grid2D grid, ksj::array::MatrixView<const float> trajectory,
                          ksj::array::PooledVector<ksj::base::cf32>& buffer) {
  buffer.resize(trajectory.rows() * 3U);
  const auto row_scale = static_cast<float>(grid.rows) / (2.0F * std::numbers::pi_v<float>);
  const auto col_scale = static_cast<float>(grid.cols) / (2.0F * std::numbers::pi_v<float>);
  for (std::size_t sample = 0; sample < trajectory.rows(); ++sample) {
    buffer(sample * 3U + 0U) = {trajectory(sample, 1U) * col_scale, 0.0F};
    buffer(sample * 3U + 1U) = {trajectory(sample, 0U) * row_scale, 0.0F};
    buffer(sample * 3U + 2U) = {0.0F, 0.0F};
  }
}

void pack_contiguous_image(ksj::array::MatrixView<const ksj::base::cf32> image,
                           ksj::array::PooledMatrix<ksj::base::cf32>& buffer) {
  buffer.resize(image.rows(), image.cols());
  ksj::array::copy(image, buffer.view());
}

void pack_contiguous_samples(ksj::array::VectorView<const ksj::base::cf32> samples,
                             ksj::array::PooledVector<ksj::base::cf32>& buffer) {
  buffer.resize(samples.size());
  ksj::array::copy(samples, buffer.view());
}

[[nodiscard]] const ksj::base::cf32* borrow_or_pack_image(ksj::array::MatrixView<const ksj::base::cf32> image,
                                                          ksj::array::PooledMatrix<ksj::base::cf32>& packed) {
  if (image.is_contiguous()) {
    return image.data();
  }

  pack_contiguous_image(image, packed);
  return packed.data();
}

[[nodiscard]] float origin_phase(const Grid2D grid, ksj::array::MatrixView<const float> trajectory,
                                 const std::size_t sample) noexcept {
  const auto row_offset = static_cast<float>(grid.rows) * 0.5F - static_cast<float>(grid.row_origin);
  const auto col_offset = static_cast<float>(grid.cols) * 0.5F - static_cast<float>(grid.col_origin);
  return trajectory(sample, 0U) * row_offset + trajectory(sample, 1U) * col_offset;
}

void correct_forward_phase(ksj::base::cf32* values, ksj::array::MatrixView<const float> trajectory,
                           const Grid2D grid) noexcept {
  for (std::size_t sample = 0U; sample < trajectory.rows(); ++sample) {
    const auto phase = origin_phase(grid, trajectory, sample);
    values[sample] *= ksj::base::cf32{std::cos(phase), -std::sin(phase)};
  }
}

[[nodiscard]] const ksj::base::cf32* pack_adjoint_samples(ksj::array::VectorView<const ksj::base::cf32> samples,
                                                          ksj::array::MatrixView<const float> trajectory,
                                                          const Grid2D grid,
                                                          ksj::array::PooledVector<ksj::base::cf32>& packed) {
  pack_contiguous_samples(samples, packed);
  for (std::size_t sample = 0U; sample < trajectory.rows(); ++sample) {
    const auto phase = origin_phase(grid, trajectory, sample);
    packed(sample) *= ksj::base::cf32{std::cos(phase), std::sin(phase)};
  }
  return packed.data();
}

[[nodiscard]] std::mutex& backend_mutex() noexcept {
  static std::mutex mutex;
  return mutex;
}

[[nodiscard]] float backend_normalization(const Grid2D grid) noexcept {
  return std::sqrt(static_cast<float>(grid.rows) * static_cast<float>(grid.cols));
}

void apply_backend_normalization(ksj::base::cf32* values, const std::size_t count, const Grid2D grid) noexcept {
  const auto scale = backend_normalization(grid);
  for (std::size_t index = 0U; index < count; ++index) {
    values[index] *= scale;
  }
}

} // namespace

struct Nufft2Plan {
  Grid2D grid;
  bool direct_dft{};
  bool forward{};
  ksj::array::PooledVector<ksj::base::cf32> trajectory;
  const linop_s* operator_handle{};

  ~Nufft2Plan() {
    if (operator_handle != nullptr) {
      std::lock_guard lock(backend_mutex());
      linop_free(operator_handle);
    }
  }
};

namespace {

[[nodiscard]] bool matches(const Nufft2Plan& plan, const Grid2D grid, const bool direct_dft, const bool forward,
                           const ksj::array::PooledVector<ksj::base::cf32>& packed_trajectory) noexcept {
  if (plan.grid.rows != grid.rows || plan.grid.cols != grid.cols || plan.direct_dft != direct_dft ||
      plan.forward != forward || plan.trajectory.size() != packed_trajectory.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < packed_trajectory.size(); ++index) {
    if (plan.trajectory(index) != packed_trajectory(index)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] const linop_s* prepare_plan(const Grid2D grid, const bool direct_dft, const bool forward,
                                          const std::array<long, DIMS>& kspace_dims,
                                          const std::array<long, DIMS>& image_dims,
                                          const std::array<long, DIMS>& trajectory_dims,
                                          Nufft2Workspace<float>& workspace) {
  if (workspace.bart_plan != nullptr &&
      matches(*workspace.bart_plan, grid, direct_dft, forward, workspace.bart_trajectory)) {
    return workspace.bart_plan->operator_handle;
  }

  auto plan = std::make_shared<Nufft2Plan>();
  plan->grid = grid;
  plan->direct_dft = direct_dft;
  plan->forward = forward;
  plan->trajectory.resize(workspace.bart_trajectory.size());
  ksj::array::copy(workspace.bart_trajectory.view(), plan->trajectory.view());
  {
    std::lock_guard lock(backend_mutex());
    num_init();
    plan->operator_handle = direct_dft
                              ? nudft_create(DIMS, FFT_FLAGS, kspace_dims.data(), image_dims.data(),
                                             trajectory_dims.data(), as_bart_complex(plan->trajectory.data()))
                              : nufft_create(DIMS, kspace_dims.data(), image_dims.data(), trajectory_dims.data(),
                                             as_bart_complex(plan->trajectory.data()), nullptr, nufft_conf_defaults);
  }
  workspace.bart_plan = std::move(plan);
  return workspace.bart_plan->operator_handle;
}

} // namespace
#endif

bool available() noexcept {
#if defined(KSJ_NUFFT_HAS_BART)
  return true;
#else
  return false;
#endif
}

bool nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                    const bool direct_dft) {
  auto workspace = Nufft2Workspace<float>{};
  return nufft2_forward(grid, image, trajectory, output, workspace, direct_dft);
}

bool nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf32> image,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::VectorView<ksj::base::cf32> output,
                    Nufft2Workspace<float>& workspace, const bool direct_dft) {
#if defined(KSJ_NUFFT_HAS_BART)
  validate_common(grid, trajectory);
  if (image.rows() != grid.rows || image.cols() != grid.cols || output.size() != trajectory.rows()) {
    throw std::invalid_argument("BART NUFFT forward dimension mismatch");
  }

  pack_bart_trajectory(grid, trajectory, workspace.bart_trajectory);
  const auto* image_data = borrow_or_pack_image(image, workspace.packed_image);

  auto* kspace_data = output.data();
  if (!output.is_contiguous()) {
    workspace.output_buffer.resize(trajectory.rows());
    kspace_data = workspace.output_buffer.data();
  }

  auto traj_dims = make_dims();
  traj_dims[0] = 3L;
  traj_dims[1] = static_cast<long>(trajectory.rows());

  auto image_dims = make_dims();
  image_dims[0] = static_cast<long>(grid.cols);
  image_dims[1] = static_cast<long>(grid.rows);

  auto kspace_dims = make_dims();
  md_select_dims(DIMS, PHS1_FLAG | PHS2_FLAG, kspace_dims.data(), traj_dims.data());

  const auto* op = prepare_plan(grid, direct_dft, true, kspace_dims, image_dims, traj_dims, workspace);
  {
    std::lock_guard lock(backend_mutex());
    linop_forward(op, DIMS, kspace_dims.data(), as_bart_complex(kspace_data), DIMS, image_dims.data(),
                  as_bart_complex(image_data));
  }
  apply_backend_normalization(kspace_data, trajectory.rows(), grid);
  correct_forward_phase(kspace_data, trajectory, grid);
  if (!output.is_contiguous()) {
    ksj::array::copy(workspace.output_buffer.view(), output);
  }
  return true;
#else
  (void)grid;
  (void)image;
  (void)trajectory;
  (void)output;
  (void)workspace;
  (void)direct_dft;
  return false;
#endif
}

bool nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                    const bool direct_dft) {
  auto workspace = Nufft2Workspace<double>{};
  return nufft2_forward(grid, image, trajectory, output, workspace, direct_dft);
}

bool nufft2_forward(const Grid2D grid, ksj::array::MatrixView<const ksj::base::cf64> image,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::VectorView<ksj::base::cf64> output,
                    Nufft2Workspace<double>& workspace, const bool direct_dft) {
  (void)grid;
  (void)image;
  (void)trajectory;
  (void)output;
  (void)workspace;
  (void)direct_dft;
  return false;
}

bool nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                    const bool direct_dft) {
  auto workspace = Nufft2Workspace<float>{};
  return nufft2_adjoint(grid, samples, trajectory, image, workspace, direct_dft);
}

bool nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf32> samples,
                    ksj::array::MatrixView<const float> trajectory, ksj::array::MatrixView<ksj::base::cf32> image,
                    Nufft2Workspace<float>& workspace, const bool direct_dft) {
#if defined(KSJ_NUFFT_HAS_BART)
  validate_common(grid, trajectory);
  if (image.rows() != grid.rows || image.cols() != grid.cols || samples.size() != trajectory.rows()) {
    throw std::invalid_argument("BART NUFFT adjoint dimension mismatch");
  }

  pack_bart_trajectory(grid, trajectory, workspace.bart_trajectory);
  const auto* kspace_data = pack_adjoint_samples(samples, trajectory, grid, workspace.packed_samples);

  auto* image_data = image.data();
  if (!image.is_contiguous()) {
    workspace.image_buffer.resize(grid.rows, grid.cols);
    image_data = workspace.image_buffer.data();
  }

  auto traj_dims = make_dims();
  traj_dims[0] = 3L;
  traj_dims[1] = static_cast<long>(trajectory.rows());

  auto image_dims = make_dims();
  image_dims[0] = static_cast<long>(grid.cols);
  image_dims[1] = static_cast<long>(grid.rows);

  auto kspace_dims = make_dims();
  kspace_dims[0] = 1L;
  kspace_dims[1] = static_cast<long>(trajectory.rows());

  const auto* op = prepare_plan(grid, direct_dft, false, kspace_dims, image_dims, traj_dims, workspace);
  {
    std::lock_guard lock(backend_mutex());
    linop_adjoint(op, DIMS, image_dims.data(), as_bart_complex(image_data), DIMS, kspace_dims.data(),
                  as_bart_complex(kspace_data));
  }
  apply_backend_normalization(image_data, grid.rows * grid.cols, grid);
  if (!image.is_contiguous()) {
    ksj::array::copy(workspace.image_buffer.view(), image);
  }
  return true;
#else
  (void)grid;
  (void)samples;
  (void)trajectory;
  (void)image;
  (void)workspace;
  (void)direct_dft;
  return false;
#endif
}

bool nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                    const bool direct_dft) {
  auto workspace = Nufft2Workspace<double>{};
  return nufft2_adjoint(grid, samples, trajectory, image, workspace, direct_dft);
}

bool nufft2_adjoint(const Grid2D grid, ksj::array::VectorView<const ksj::base::cf64> samples,
                    ksj::array::MatrixView<const double> trajectory, ksj::array::MatrixView<ksj::base::cf64> image,
                    Nufft2Workspace<double>& workspace, const bool direct_dft) {
  (void)grid;
  (void)samples;
  (void)trajectory;
  (void)image;
  (void)workspace;
  (void)direct_dft;
  return false;
}

} // namespace ksj::nufft::detail::bart
