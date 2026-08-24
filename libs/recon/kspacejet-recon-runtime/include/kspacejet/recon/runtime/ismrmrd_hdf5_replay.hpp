#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/ismrmrd/dataset_reader.hpp"
#include "kspacejet/recon/runtime/ismrmrd_semantic_ingress.hpp"
#include "kspacejet/recon/runtime/serial_cartesian_pipeline.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>

namespace ksj::recon::runtime {

// The SerialCartesianPipeline adapter exposes normalized KSpaceJet runtime
// values. The route-neutral Source session below exposes an ISMRMRD
// AcquisitionView only for the duration of its callback; samples and
// trajectory must never cross that callback boundary.
struct IsmrmrdHdf5ReplayAcquisitionDescriptor {
  std::uint32_t acquisition_ordinal{0};
  AcquisitionClassificationInput classification_input{};
  AcquisitionClassification classification{};
  FrameSemanticKey frame_key{};
  CartesianLineCoordinate coordinate{};
  std::uint16_t number_of_samples{0};
  std::uint16_t active_channels{0};
  std::uint16_t trajectory_dimensions{0};
  IsmrmrdAcquisitionControlFlags control_flags{};
  NormalizedAcquisitionIngressFacts ingress_facts{};
};

// A compiled plan (or its caller) owns this mapping. It must be total and
// deterministic for every replayed acquisition, including non-imaging lanes
// retained by the runtime audit. The HDF5 adapter owns the normalized
// FrameSemanticKey and deliberately lets the plan resolve only ordering and
// placement; it cannot substitute a second frame identity.
using FrameSlotContextBindingResolver =
  std::function<ksj::base::Result<IsmrmrdFrameSlotContextBinding>(const IsmrmrdHdf5ReplayAcquisitionDescriptor&)>;

// Configuration owned by one runtime HDF5 replay source.  The source, rather
// than a CLI application or Provider, owns the input-file binding and the
// replay policy used to turn that file into normalized runtime frames.
struct IsmrmrdHdf5ReplaySourceConfig {
  std::filesystem::path input_file;
  std::string dataset_group{"dataset"};
  FrameSlotContextBindingResolver resolve_frame_slot_binding{};

  // Checked between DatasetReader callbacks.  A requested stop prevents
  // EndOfInput and drives the pipeline through its cancellation terminal path.
  std::stop_token stop_token{};
};

struct IsmrmrdHdf5ReplaySourceReport {
  std::uint32_t declared_acquisitions{0};
  std::uint32_t acquisitions_read{0};
  std::uint32_t routed_to_frame_slots{0};
  std::uint32_t classified_non_imaging{0};
  std::uint32_t explicitly_ignored{0};
};

// Stable facts observed when a standard ISMRMRD acquisition dataset is opened
// for one source pass. The XML is copied into host-owned storage; acquisition
// samples and trajectories are never materialized here.
struct IsmrmrdHdf5ReplaySourceMetadata {
  std::string dataset_group;
  std::string xml_header;
  std::uint32_t declared_acquisitions{0};
};

enum class IsmrmrdHdf5ReplayIterationResult {
  completed,
  stopped,
};

// The acquisition view is borrowed and valid only while this callback is
// executing. A consumer must synchronously copy any payload it needs beyond
// the invocation into a bounded, host-owned runtime object.
using IsmrmrdHdf5ReplayAcquisitionConsumer = std::function<bool(const ksj::ismrmrd::AcquisitionView&)>;

// One open pass through a standard ISMRMRD acquisition dataset. It is
// route-neutral: a caller can inspect XML metadata and synchronously replay
// acquisitions into any runtime ingress without involving
// SerialCartesianPipeline. The session is move-only because it owns the HDF5
// reader; one source may open separate sessions for preflight and replay.
class IsmrmrdHdf5ReplaySession final {
public:
  IsmrmrdHdf5ReplaySession(const IsmrmrdHdf5ReplaySession&) = delete;
  IsmrmrdHdf5ReplaySession& operator=(const IsmrmrdHdf5ReplaySession&) = delete;
  IsmrmrdHdf5ReplaySession(IsmrmrdHdf5ReplaySession&&) noexcept;
  IsmrmrdHdf5ReplaySession& operator=(IsmrmrdHdf5ReplaySession&&) noexcept;
  ~IsmrmrdHdf5ReplaySession();

  [[nodiscard]] const IsmrmrdHdf5ReplaySourceMetadata& metadata() const noexcept;

  // A false return is a normal early stop. Dataset I/O/format failures are
  // returned as io_error; a consumer exception is returned as internal_error.
  [[nodiscard]] ksj::base::Result<IsmrmrdHdf5ReplayIterationResult>
  for_each_acquisition(const IsmrmrdHdf5ReplayAcquisitionConsumer& consumer);

private:
  struct State;

  explicit IsmrmrdHdf5ReplaySession(std::unique_ptr<State> state) noexcept;

  std::unique_ptr<State> state_;

  friend class IsmrmrdHdf5ReplaySource;
};

// Runtime-owned source for one standard ISMRMRD HDF5 acquisition dataset.
// `open()` provides a route-neutral input boundary. `replay_into()` is the
// existing SerialCartesianPipeline adapter built on that boundary; it is not
// the only supported source consumer.
//
// `replay_into()` checks public header/layout facts and invokes the pipeline's
// allocation-free TargetEnvelope ingress validation before frame-context
// resolution. `SerialCartesianPipeline::submit()` then copies each payload
// into its preplanned FrameSlot synchronously. Consequently the ISMRMRD view
// and its byte span are used only within DatasetReader's callback and are
// never retained by the source or pipeline.
class IsmrmrdHdf5ReplaySource final {
public:
  explicit IsmrmrdHdf5ReplaySource(IsmrmrdHdf5ReplaySourceConfig config);

  // Opens one standard ISMRMRD acquisition source pass. The returned session
  // owns the reader, makes copied XML/count metadata available before replay,
  // and exposes borrowed acquisitions only through its callback API.
  [[nodiscard]] ksj::base::Result<IsmrmrdHdf5ReplaySession> open() const;

  [[nodiscard]] ksj::base::Result<IsmrmrdHdf5ReplaySourceReport> replay_into(SerialCartesianPipeline& pipeline) const;

private:
  IsmrmrdHdf5ReplaySourceConfig config_;
};

} // namespace ksj::recon::runtime
