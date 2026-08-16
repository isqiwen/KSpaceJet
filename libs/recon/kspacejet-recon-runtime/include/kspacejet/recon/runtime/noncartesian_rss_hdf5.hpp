#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ksj::recon::runtime {

// The intentionally narrow offline entry point for the implemented direct
// non-Cartesian adjoint route. Provider modules and their authored contracts
// are caller supplied, so this path never scans a directory or silently
// selects a reconstruction algorithm.
struct NoncartesianRssHdf5ReconstructionConfig {
  std::filesystem::path input_file;
  std::filesystem::path output_image_file;
  std::filesystem::path output_metadata_file;
  std::filesystem::path noncartesian_provider_module;
  std::filesystem::path coil_combine_provider_module;
  std::filesystem::path noncartesian_operator_contract;
  std::filesystem::path coil_combine_operator_contract;
  std::string dataset_group{"dataset"};
};

struct NoncartesianRssHdf5ReconstructionReport {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t channels{0U};
  std::uint32_t acquisitions_read{0U};
  std::uint32_t samples_read{0U};
  std::uint64_t image_payload_bytes{0U};
  std::string execution_plan_digest;
  std::string verification_record_digest;
};

// Replays one complete 2-D non-Cartesian ISMRMRD/HDF5 semantic frame through
// the generic frozen graph:
//
//   noncartesian k-space ingress + trajectory ingress
//       -> noncartesian_adjoint_reconstruct -> coil_combine_rss -> image egress
//
// The route accepts exactly one XML encoding with radial, golden-angle,
// spiral, or other trajectory declaration; each acquisition must have an
// explicit finite two-coordinate trajectory, no flags or discarded samples,
// and the same active-channel count. It aggregates the acquisition sequence
// into one bounded frame without inferring trajectory units, density weights,
// sensitivity maps, or a SENSE model. The output is a native-endian,
// row-major float32 RSS image.
[[nodiscard]] ksj::base::Result<NoncartesianRssHdf5ReconstructionReport>
reconstruct_noncartesian_rss_hdf5(const NoncartesianRssHdf5ReconstructionConfig& config);

} // namespace ksj::recon::runtime
