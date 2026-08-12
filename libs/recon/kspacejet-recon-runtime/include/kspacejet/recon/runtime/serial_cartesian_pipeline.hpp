#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/resource_contracts.hpp"
#include "kspacejet/recon/runtime/acquisition_classification.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"
#include "kspacejet/recon/runtime/scan_lifecycle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

inline constexpr std::size_t kSerialCartesianAcquisitionLaneCount =
  static_cast<std::size_t>(AcquisitionLane::ignored_explicitly) + 1U;

// Facts observed at public-ingress materialization.  These values are kept
// vendor-free so every public MRD/ISMRMRD adapter can present the same runtime
// boundary.  `complete` distinguishes a real zero-valued field from an
// adapter that did not obtain the public acquisition header facts at all.
struct NormalizedAcquisitionIngressFacts {
  std::uint64_t samples_per_acquisition{0};
  std::uint64_t active_channels{0};
  std::uint64_t trajectory_dimensions{0};
  bool complete{false};
};

// A normalized Cartesian acquisition is an in-process runtime value, not a
// wire message.  Public ISMRMRD/MRD adapters decode their input into this
// view before calling submit().  `payload` is copied synchronously into a
// FrameSlot and is never retained by the pipeline.
struct NormalizedCartesianAcquisitionFrame {
  AcquisitionClassificationInput classification_input{};
  FrameSlotContext frame_context{};
  CartesianLineCoordinate coordinate{};
  NormalizedAcquisitionIngressFacts ingress_facts{};
  ksj::base::ConstByteSpan payload{};
};

enum class SerialIngressDisposition : std::uint8_t {
  routed_to_frame_slot,
  classified_non_imaging,
  recorded_explicitly_ignored,
};

[[nodiscard]] std::string_view to_string(SerialIngressDisposition disposition) noexcept;

struct SerialIngressReceipt {
  AcquisitionClassification classification{};
  SerialIngressDisposition disposition{SerialIngressDisposition::classified_non_imaging};
};

// An auditable record for a classification rule that explicitly excluded an
// acquisition.  The payload is intentionally not retained: retaining it would
// turn a routing audit into an unbounded secondary acquisition queue.
struct ExplicitlyIgnoredAcquisitionRecord {
  AcquisitionClassification classification{};
  FrameSlotContext frame_context{};
  CartesianLineCoordinate coordinate{};
  std::size_t payload_bytes{0};
};

// This view is valid only for the duration of SerialFrameCallback.  A callback
// must copy data that it needs after returning.  The callback is synchronous
// and executes on the thread that sealed or drained the FrameSlot; it is the
// serial M1 reference seam, not the Provider ABI.
struct SealedCartesianFrame {
  FrameSlotToken token{};
  FrameSlotContext context{};
  FrameCompletion completion{FrameCompletion::not_sealed};
  ksj::base::ConstByteSpan bytes{};
  std::uint64_t imaging_arrivals{0};
  std::uint64_t calibration_and_imaging_arrivals{0};
};

using SerialFrameCallback = std::function<ksj::base::Status(const SealedCartesianFrame&)>;

// Retaining every completed identity up to the scan's planned finite bound
// lets M1 reject an old acquisition after a reusable slot has advanced its
// generation.  Do not evict this history: eviction would make a late event
// indistinguishable from a legitimate new frame.
struct SerialFrameTerminalRecord {
  FrameSlotToken token{};
  FrameSlotContext context{};
  FrameCompletion completion{FrameCompletion::not_sealed};
  bool delivered_to_callback{false};
};

struct SerialCartesianPipelineConfig {
  AcquisitionClassifierConfig classifier{};

  // An admitted plan supplies its scanner/site-owned envelope here.  It is
  // optional only to retain the standalone M1 primitive for tests and for
  // callers that have not yet constructed a plan.  When present, create()
  // validates FrameSlot storage before allocation and submit() requires
  // complete observed ingress facts for every acquisition.
  std::optional<ksj::recon::TargetEnvelope> target_envelope{};

  // This is a bounded pool of interchangeable FrameSlots.  All entries must
  // have the same Cartesian assembly contract except for slot_id, because an
  // input frame does not select a slot implementation.
  //
  // M1 admits new frame contexts only in nondecreasing first-seen order_key
  // order.  This keeps synchronous output ordering provable without an
  // unbounded "future frame" set.  A later compiled ReorderSpec/ExecutionPlan
  // replaces this baseline restriction for deliberately interleaved scans.
  std::vector<CartesianFrameSlotConfig> frame_slots;

  // Both limits are plan-time finite resource bounds.  completed frame
  // history is deliberately bounded by admission instead of evicted at run
  // time so stale input remains detectable for the entire scan.
  std::size_t max_terminal_frame_records{1024U};
  std::size_t max_explicitly_ignored_records{1024U};

  SerialFrameCallback on_sealed_frame{};
};

struct SerialCartesianPipelineSnapshot {
  ScanState state{ScanState::session_candidate};
  TerminalCause terminal_cause{TerminalCause::none};
  std::size_t active_frames{0};
  std::size_t terminal_frames{0};
  std::size_t explicitly_ignored_records{0};
  std::array<std::uint64_t, kSerialCartesianAcquisitionLaneCount> arrivals_by_lane{};
  std::uint64_t callbacks_completed{0};
  std::uint64_t certified_skips{0};
  ksj::base::Status last_error{};
};

// A deterministic, single-threaded M1 reference runner for Cartesian raw
// frames.  It deliberately contains no reconstruction algorithm, Provider
// loader, HDF5 reader, transport protocol, or worker scheduling.  It exists
// to make acquisition routing, exact completion, reusable-slot generations,
// callback ordering, and EndOfInput terminal semantics executable before the
// concurrent runtime is introduced.
class SerialCartesianPipeline final {
public:
  [[nodiscard]] static ksj::base::Result<SerialCartesianPipeline> create(SerialCartesianPipelineConfig config);

  SerialCartesianPipeline(const SerialCartesianPipeline&) = delete;
  SerialCartesianPipeline& operator=(const SerialCartesianPipeline&) = delete;
  SerialCartesianPipeline(SerialCartesianPipeline&&) noexcept = default;
  SerialCartesianPipeline& operator=(SerialCartesianPipeline&&) noexcept = default;
  ~SerialCartesianPipeline() = default;

  // Validates the fully constructed serial runner through the same scan-level
  // lifecycle used by later runtimes, then enters Running.
  [[nodiscard]] ksj::base::Status start();

  // Classifies one normalized acquisition.  Only imaging and
  // calibration-and-imaging lanes reach a FrameSlot.  Other semantic lanes
  // are surfaced in the receipt, while ignored-explicitly lanes additionally
  // produce an auditable bounded record.
  [[nodiscard]] ksj::base::Result<SerialIngressReceipt> submit(const NormalizedCartesianAcquisitionFrame& frame);

  // Allocation-free ingress validation for an already materialized public
  // acquisition.  It does not change scan lifecycle state or reserve a
  // FrameSlot, so an adapter may invoke it before resolving a frame context.
  // submit() invokes the same check before classification, audit retention,
  // slot acquisition, or payload copy.  A configured TargetEnvelope requires
  // complete facts; an unconfigured standalone M1 pipeline accepts them as
  // opaque input facts.  This serial path owns neither a network decoder
  // staging buffer nor an image egress buffer, so their envelope fields are
  // intentionally not claimed as checked here.
  [[nodiscard]] ksj::base::Status validate_ingress(const NormalizedAcquisitionIngressFacts& facts,
                                                   std::size_t payload_bytes) const;

  // EndOfInput closes ingress, resolves every active FrameSlot's explicit
  // missing-data policy, drains synchronous callbacks in OrderKey order, and
  // reaches Completed only after the normal finalization/flush boundary.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Abort paths never impersonate normal EndOfInput.  They quarantine active
  // slots and complete terminal cleanup as Cancelled or Failed respectively.
  [[nodiscard]] ksj::base::Status cancel();
  [[nodiscard]] ksj::base::Status fail(ksj::base::Status cause);

  [[nodiscard]] SerialCartesianPipelineSnapshot snapshot() const;
  [[nodiscard]] const std::vector<ExplicitlyIgnoredAcquisitionRecord>& explicitly_ignored_records() const noexcept;
  [[nodiscard]] const std::vector<SerialFrameTerminalRecord>& terminal_frame_records() const noexcept;

private:
  struct SlotRecord {
    CartesianFrameSlot frame_slot;
    FrameSlotToken token{};
    FrameSlotContext context{};
    bool active{false};
    std::uint64_t imaging_arrivals{0};
    std::uint64_t calibration_and_imaging_arrivals{0};
  };

  SerialCartesianPipeline(AcquisitionClassifier classifier, std::vector<SlotRecord> slots,
                          SerialFrameCallback on_sealed_frame, std::size_t max_terminal_frame_records,
                          std::size_t max_explicitly_ignored_records,
                          std::optional<ksj::recon::TargetEnvelope> target_envelope) noexcept;

  [[nodiscard]] ksj::base::Status require_not_in_callback(std::string_view operation) const;
  [[nodiscard]] ksj::base::Status require_running(std::string_view operation) const;
  [[nodiscard]] ksj::base::Status fail_internal(ksj::base::Status cause);
  [[nodiscard]] ksj::base::Status finish_abort_cleanup();
  void quarantine_active_slots();

  [[nodiscard]] SlotRecord* find_active_slot(const FrameSlotContext& context) noexcept;
  [[nodiscard]] ksj::base::Result<SlotRecord*> acquire_slot(const FrameSlotContext& context);
  [[nodiscard]] bool context_is_terminal(const FrameSlotContext& context) const noexcept;
  [[nodiscard]] bool order_key_is_in_use(std::uint64_t order_key) const noexcept;
  [[nodiscard]] SlotRecord* next_active_slot_in_order() noexcept;
  [[nodiscard]] ksj::base::Status finalize_available_slots();
  [[nodiscard]] ksj::base::Status dispatch_ready_slot(SlotRecord& slot);
  [[nodiscard]] ksj::base::Status finalize_skipped_slot(SlotRecord& slot);
  [[nodiscard]] ksj::base::Status append_terminal_record(SlotRecord& slot, bool delivered_to_callback);

  AcquisitionClassifier classifier_;
  std::vector<SlotRecord> slots_;
  SerialFrameCallback on_sealed_frame_;
  std::size_t max_terminal_frame_records_{0};
  std::size_t max_explicitly_ignored_records_{0};
  std::optional<ksj::recon::TargetEnvelope> target_envelope_;
  ScanLifecycle lifecycle_;
  std::vector<ExplicitlyIgnoredAcquisitionRecord> explicitly_ignored_records_;
  std::vector<SerialFrameTerminalRecord> terminal_frame_records_;
  std::array<std::uint64_t, kSerialCartesianAcquisitionLaneCount> arrivals_by_lane_{};
  std::uint64_t callbacks_completed_{0};
  std::uint64_t certified_skips_{0};
  std::uint64_t greatest_started_order_key_{0};
  bool has_started_order_key_{false};
  bool callback_active_{false};
  ksj::base::Status last_error_{};
};

} // namespace ksj::recon::runtime
