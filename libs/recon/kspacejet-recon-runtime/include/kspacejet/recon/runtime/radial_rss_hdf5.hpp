#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ksj::recon::runtime {

// The input unit is an explicit route contract.  ISMRMRD carries numerical
// trajectory coordinates but does not make their physical/unit convention an
// executable payload-type property.  The radial route normalizes the selected
// input convention to radians-per-pixel before the Provider firing. Raw
// two-dimensional ISMRMRD input is `[kx, ky]`; the route emits the Provider's
// canonical `[row=ky, column=kx]` payload order.
enum class RadialHdf5TrajectoryUnits {
  unspecified,
  cycles_per_fov,
  radians_per_pixel,
  // One raw coordinate unit equals one index in the declared encoded matrix.
  // The route normalizes raw kx with XML encoded width and raw ky with XML
  // encoded height before placing them into canonical Provider order.
  encoded_matrix_index,
};

struct RadialRssHdf5ReconstructionConfig {
  std::filesystem::path input_file;
  std::filesystem::path output_image_file;
  std::filesystem::path radial_provider_module;
  std::filesystem::path coil_combine_provider_module;
  std::filesystem::path radial_operator_contract;
  std::filesystem::path coil_combine_operator_contract;
  std::string dataset_group{"dataset"};
  RadialHdf5TrajectoryUnits input_trajectory_units{RadialHdf5TrajectoryUnits::unspecified};
};

struct RadialRssHdf5ReconstructionReport {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t channels{0U};
  std::uint32_t acquisitions_read{0U};
  std::uint32_t samples_read{0U};
  std::uint64_t image_payload_bytes{0U};
  std::string execution_plan_digest;
  std::string verification_record_digest;
};

// Reconstructs one declared 2-D ISMRMRD radial frame through the explicit
// bounded graph:
//
//   normalized radial k-space + radians-per-pixel trajectory
//       -> radial_gridding_reconstruct -> coil_combine_rss -> image egress
//
// It requires an explicit input trajectory unit choice and accepts only
// declared 2-D radial geometry with one imaging semantic frame and
// power-of-two reconstructed axes. It applies only the contract-selected
// analytic radial-ramp density compensation and linear Cartesian gridding. It
// is not trajectory correction, SENSE, coil compression, spiral, 3-D, cine,
// EPI, partial-Fourier, GRAPPA, or a clinical reconstruction claim. The output
// is one standard ISMRMRD HDF5 magnitude image artifact, not `.f32` plus a
// JSON sidecar.
[[nodiscard]] ksj::base::Result<RadialRssHdf5ReconstructionReport>
reconstruct_radial_rss_hdf5(const RadialRssHdf5ReconstructionConfig& config);

[[nodiscard]] const char* to_string(RadialHdf5TrajectoryUnits value) noexcept;

} // namespace ksj::recon::runtime
