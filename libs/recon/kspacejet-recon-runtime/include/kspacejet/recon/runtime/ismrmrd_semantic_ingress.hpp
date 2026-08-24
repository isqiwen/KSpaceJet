#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/runtime/acquisition_classification.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"

#include <cstdint>
#include <span>

namespace ksj::ismrmrd {
struct AcquisitionView;
}

namespace ksj::recon::runtime {

// Facts observed at ISMRMRD materialization. These values are independent of
// an HDF5 reader, a future in-process feed, and any transport.
struct NormalizedAcquisitionIngressFacts {
  std::uint64_t samples_per_acquisition{0};
  std::uint64_t active_channels{0};
  std::uint64_t trajectory_dimensions{0};
  bool complete{false};
};

// These flags describe control/order facts only. They never select an imaging
// lane and must not be used as an implicit frame-completion predicate.
struct IsmrmrdAcquisitionControlFlags {
  bool first_in_encode_step_1{false};
  bool last_in_encode_step_1{false};
  bool first_in_encode_step_2{false};
  bool last_in_encode_step_2{false};
  bool first_in_average{false};
  bool last_in_average{false};
  bool first_in_slice{false};
  bool last_in_slice{false};
  bool first_in_contrast{false};
  bool last_in_contrast{false};
  bool first_in_phase{false};
  bool last_in_phase{false};
  bool first_in_repetition{false};
  bool last_in_repetition{false};
  bool first_in_set{false};
  bool last_in_set{false};
  bool first_in_segment{false};
  bool last_in_segment{false};
  bool last_in_measurement{false};
};

// Normalized semantic facts for one borrowed ISMRMRD acquisition. The sample
// and trajectory views remain valid only for the source callback that supplied
// the input. A route must synchronously copy them into a pre-admitted,
// host-owned FrameSlot or other bounded storage before an asynchronous edge.
struct NormalizedIsmrmrdAcquisition {
  AcquisitionClassificationInput classification_input{};
  AcquisitionClassification classification{};
  FrameSemanticKey frame_key{};
  CartesianLineCoordinate cartesian_coordinate{};
  IsmrmrdAcquisitionControlFlags control_flags{};
  NormalizedAcquisitionIngressFacts ingress_facts{};
  ksj::base::ConstByteSpan sample_bytes{};
  std::span<const float> trajectory{};
};

// The compiled plan/route, not an acquisition header, owns ordering and
// placement policy. This explicit binding prevents scan/frame/order/placement
// identities from being mixed by a source adapter.
struct IsmrmrdFrameSlotContextBinding {
  std::uint64_t order_key{0};
  std::uint64_t placement_key{0};
};

// Performs source-independent structural/layout/finite-value validation,
// interprets known ISMRMRD control and lane flags, and invokes the supplied
// immutable classifier. Unsupported auxiliary semantic flags and unknown bits
// fail closed rather than being guessed as imaging.
[[nodiscard]] ksj::base::Result<NormalizedIsmrmrdAcquisition>
normalize_ismrmrd_acquisition(const ksj::ismrmrd::AcquisitionView& acquisition,
                              const AcquisitionClassifier& classifier);

// Projects only the normalized frame identity. Order and placement remain
// explicit caller/plan inputs.
[[nodiscard]] FrameSlotContext make_ismrmrd_frame_slot_context(const NormalizedIsmrmrdAcquisition& acquisition,
                                                               IsmrmrdFrameSlotContextBinding binding) noexcept;

} // namespace ksj::recon::runtime
