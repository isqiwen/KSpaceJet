#pragma once

#include "kspacejet/base/result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ksj::recon::runtime {

// One exact Provider/OperatorContract pair.  The route does not discover
// Providers or infer a contract from a module; every enabled Operator remains
// a caller-selected, independently verified graph node.
struct CartesianRssHdf5ProviderOperator {
  std::filesystem::path provider_module;
  std::filesystem::path operator_contract;
};

struct CartesianRssHdf5NoisePrewhitenBranch {
  CartesianRssHdf5ProviderOperator noise_model_estimate;
  CartesianRssHdf5ProviderOperator noise_prewhiten;
};

struct CartesianRssHdf5PhaseCorrectionBranch {
  CartesianRssHdf5ProviderOperator phase_correction_estimate;
  CartesianRssHdf5ProviderOperator phase_correct;
};

struct CartesianRssHdf5CoilCompressionBranch {
  CartesianRssHdf5ProviderOperator coil_compression_basis_estimate;
  CartesianRssHdf5ProviderOperator coil_compress;
  std::uint32_t virtual_channel_count{0U};
};

struct CartesianRssHdf5ReadoutOversamplingRemoval {
  CartesianRssHdf5ProviderOperator readout_oversampling_remove;
  std::uint32_t readout_offset{0U};
};

// The intentionally narrow offline entry point for the currently implemented
// Cartesian route.  Provider modules and their authored contracts are caller
// supplied, so this path never scans a directory or makes a plugin choice on
// the caller's behalf.
struct CartesianRssHdf5ReconstructionConfig {
  std::filesystem::path input_file;
  std::filesystem::path output_image_file;
  std::filesystem::path output_metadata_file;
  std::filesystem::path cartesian_provider_module;
  std::filesystem::path coil_combine_provider_module;
  std::filesystem::path cartesian_operator_contract;
  std::filesystem::path coil_combine_operator_contract;
  std::string dataset_group{"dataset"};
  std::optional<CartesianRssHdf5NoisePrewhitenBranch> noise_prewhiten;
  std::optional<CartesianRssHdf5PhaseCorrectionBranch> phase_correction;
  std::optional<CartesianRssHdf5CoilCompressionBranch> coil_compression;
  std::optional<CartesianRssHdf5ReadoutOversamplingRemoval> readout_oversampling_removal;
};

struct CartesianRssHdf5ReconstructionReport {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t channels{0U};
  std::uint32_t acquisitions_read{0U};
  std::uint64_t image_payload_bytes{0U};
  std::string execution_plan_digest;
  std::string verification_record_digest;
};

// Replays one complete 2-D Cartesian ISMRMRD/HDF5 imaging frame. A preflight
// pass accepts normal imaging plus only the explicitly enabled ISMRMRD
// calibration lanes: noise, phase reference, and parallel calibration (the
// combined parallel-calibration-and-imaging lane feeds both its roles). It
// rejects trajectories, 3-D coordinates, duplicate/missing imaging ky lines,
// channel changes, and inconsistent geometry. Rows/readout come from the HDF5
// XML and channels from acquisition headers; callers cannot invent a second
// shape source. A second pass assembles every enabled lane and executes the
// generic frozen graph:
//
//   optional calibration ingress -> estimate -> explicit artifact
//   kspace ingress -> [prewhiten] -> [phase correct] -> [coil compress]
//                  -> [readout crop] -> cartesian_ifft2_coil_images
//                  -> coil_combine_rss -> image egress
//
// The result is a native-endian, row-major float32 RSS image.
[[nodiscard]] ksj::base::Result<CartesianRssHdf5ReconstructionReport>
reconstruct_cartesian_rss_hdf5(const CartesianRssHdf5ReconstructionConfig& config);

} // namespace ksj::recon::runtime
