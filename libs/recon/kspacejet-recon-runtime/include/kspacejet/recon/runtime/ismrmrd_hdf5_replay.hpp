#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/recon/runtime/serial_cartesian_pipeline.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>

namespace ksj::recon::runtime {

// The source adapter intentionally exposes only normalized KSpaceJet runtime
// values.  The borrowed ISMRMRD AcquisitionView and its upstream C++ types
// never cross this boundary.
struct IsmrmrdHdf5ReplayAcquisitionDescriptor {
  std::uint32_t acquisition_ordinal{0};
  AcquisitionClassificationInput classification_input{};
  CartesianLineCoordinate coordinate{};
  std::uint16_t number_of_samples{0};
  std::uint16_t active_channels{0};
  std::uint16_t trajectory_dimensions{0};
};

// A compiled plan (or its caller) owns this mapping.  It must be total and
// deterministic for every replayed acquisition, including non-imaging lanes
// retained by the runtime audit: the HDF5 adapter deliberately never invents
// frame identities, ordering keys, placement keys, or task counts.
using FrameSlotContextResolver =
  std::function<ksj::base::Result<FrameSlotContext>(const IsmrmrdHdf5ReplayAcquisitionDescriptor&)>;

struct IsmrmrdHdf5ReplayConfig {
  std::filesystem::path input_file;
  std::string dataset_group{"dataset"};
  FrameSlotContextResolver resolve_frame_context{};

  // Checked between DatasetReader callbacks.  A requested stop prevents
  // EndOfInput and drives the pipeline through its cancellation terminal path.
  std::stop_token stop_token{};
};

struct IsmrmrdHdf5ReplayReport {
  std::uint32_t declared_acquisitions{0};
  std::uint32_t acquisitions_read{0};
  std::uint32_t routed_to_frame_slots{0};
  std::uint32_t classified_non_imaging{0};
  std::uint32_t explicitly_ignored{0};
};

// Runs one bounded HDF5 replay through the serial Cartesian reference
// pipeline.  It opens and validates the HDF5 source before admitting/starting
// the pipeline, invokes EndOfInput only after a completed reader traversal,
// and maps reader interruption/failure to a cancellation/failure terminal
// path instead of a normal completion.
//
// The adapter checks public header/layout facts and invokes the pipeline's
// allocation-free TargetEnvelope ingress validation before frame-context
// resolution.  `SerialCartesianPipeline::submit()` then copies each payload
// into its preplanned FrameSlot synchronously.  Consequently the ISMRMRD view
// and its byte span are used only within DatasetReader's callback and are
// never retained here.
[[nodiscard]] ksj::base::Result<IsmrmrdHdf5ReplayReport> replay_ismrmrd_hdf5(const IsmrmrdHdf5ReplayConfig& config,
                                                                             SerialCartesianPipeline& pipeline);

} // namespace ksj::recon::runtime
