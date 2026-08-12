#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/fixed_reorder_buffer.hpp"
#include "kspacejet/recon/runtime/host_frame_assembler.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace ksj::recon::runtime {

// The M3.5 ordered-output capability. It retains the completed frame's
// private host terminal authority after the source slot was recycled by
// FrameDispatch::complete(). There is still no BufferHandle, fan-out, async
// ownership, GPU operation, or generic scheduler: one synchronous sink must
// acknowledge this one output. Dropping it fails both coupled components.
class M3PublishLease final {
public:
  M3PublishLease() = default;
  ~M3PublishLease();

  M3PublishLease(const M3PublishLease&) = delete;
  M3PublishLease& operator=(const M3PublishLease&) = delete;
  M3PublishLease(M3PublishLease&& other) noexcept;
  M3PublishLease& operator=(M3PublishLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] const FixedReorderOutput& output() const noexcept;

  // Success settles the raw reorder credit and releases the retained host
  // terminal authority without failing the scan, but only while the coupled
  // HostFrameAssembler has not failed through another source lease. An error
  // or dropped lease fails both sides closed.
  [[nodiscard]] ksj::base::Status acknowledge_published();

private:
  friend class FrameDispatch;

  M3PublishLease(PublishLease publish, CompletedFrameLease completed_frame) noexcept;

  void fail_closed_noexcept() noexcept;
  void release_noexcept() noexcept;

  PublishLease publish_{};
  CompletedFrameLease completed_frame_{};
};

// A move-only capability for exactly one synchronous reconstruction firing.
// It couples the HostFrameAssembler input lease to the corresponding private
// FixedReorderBuffer dispatch permit.  There is deliberately no BufferHandle,
// fan-out, async retention, GPU operation, or generic scheduler in this M3.5
// seam.  The FrameSlot bytes are valid only between try_prepare() success and
// complete()/abort()/destruction.
class FrameDispatch final {
public:
  FrameDispatch() = default;
  ~FrameDispatch();

  FrameDispatch(const FrameDispatch&) = delete;
  FrameDispatch& operator=(const FrameDispatch&) = delete;
  FrameDispatch(FrameDispatch&& other) noexcept;
  FrameDispatch& operator=(FrameDispatch&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ksj::recon::Quantity ordinal() const noexcept;

  // These are immutable borrows of the host-owned FrameSlot. They are usable
  // only while this dispatch is in flight. A bad/stale source capability is a
  // same-scan invariant violation and fails the coupled dispatch closed.
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> input_bytes();
  [[nodiscard]] ksj::base::Result<FrameSlotContext> input_context();

  // Commits the private reorder reservation immediately before the synchronous
  // consumer starts using input_bytes(). There is no async completion path in
  // this slice.
  [[nodiscard]] ksj::base::Status commit();

  // Binds exactly one opaque output to this in-flight frame. On success the
  // input lease is acknowledged and its FrameSlot is recycled; the reorder
  // permit remains owned by this dispatch until ordered publish acquisition.
  [[nodiscard]] ksj::base::Status complete(OpaqueReorderPayloadHandle payload);

  // Acquires the next ordered output when this dispatch owns next_expected.
  // Unavailable preserves this dispatch. Success moves the reorder permit and
  // its retained post-ack host terminal authority to M3PublishLease, leaving
  // this FrameDispatch inert.
  [[nodiscard]] ksj::base::Result<M3PublishLease> try_acquire_publish();

  // Exceptional settlement. It fails the reorder buffer and abandons the
  // source FrameSlot; ordinary destruction follows the same fail-closed path.
  [[nodiscard]] ksj::base::Status abort();

private:
  friend class M3ReorderIngress;

  enum class Phase : std::uint8_t {
    invalid,
    prepared,
    in_flight,
    completed,
    published,
    settled,
  };

  FrameDispatch(FixedReorderBuffer* buffer, std::uint64_t ingress_identity, CompletedFrameLease completed_frame,
                DispatchPermit permit) noexcept;

  [[nodiscard]] bool has_live_authority() const noexcept;
  [[nodiscard]] bool has_active_dispatch() const noexcept;
  [[nodiscard]] ksj::base::Status require_phase(Phase expected, std::string_view operation);
  [[nodiscard]] ksj::base::Status fail_closed_and_settle();
  void release_noexcept() noexcept;
  void disarm_after_move() noexcept;

  FixedReorderBuffer* buffer_{nullptr};
  std::uint64_t ingress_identity_{0U};
  CompletedFrameLease completed_frame_{};
  DispatchPermit permit_{};
  Phase phase_{Phase::invalid};
};

// The sole M3.5 bridge from a HostFrameAssembler completion capability to the
// fixed ReorderPlan runtime. Creation binds one host instance, one verified
// plan/record pair, one node and one FixedReorderBuffer identity. A buffer is
// intentionally single-ingress after this binding. The caller must keep both
// HostFrameAssembler and FixedReorderBuffer alive and unmoved until this
// ingress and every returned FrameDispatch/M3PublishLease have settled.
class M3ReorderIngress final {
public:
  [[nodiscard]] static ksj::base::Result<M3ReorderIngress>
  create(const ksj::recon::ExecutionPlan& execution_plan, const ksj::recon::VerificationRecord& verification_record,
         std::string_view node_id, HostFrameAssembler& assembler, FixedReorderBuffer& reorder_buffer);

  M3ReorderIngress(const M3ReorderIngress&) = delete;
  M3ReorderIngress& operator=(const M3ReorderIngress&) = delete;
  M3ReorderIngress(M3ReorderIngress&& other) noexcept;
  M3ReorderIngress& operator=(M3ReorderIngress&& other) = delete;
  ~M3ReorderIngress();

  // `lease` remains wholly owned by the caller on retryable Unavailable.
  // Only a successful return moves it into FrameDispatch after both its host
  // binding and begin_dispatch transition have succeeded.
  [[nodiscard]] ksj::base::Result<FrameDispatch> try_prepare(CompletedFrameLease& lease);

  [[nodiscard]] ksj::base::Status end_of_input();
  [[nodiscard]] ksj::base::Status abort();
  [[nodiscard]] FixedReorderBufferSnapshot snapshot() const;

private:
  M3ReorderIngress(ksj::recon::ExecutionPlan execution_plan, ksj::recon::VerificationRecord verification_record,
                   std::string node_id, std::string completed_frame_input_port, HostFrameAssembler* assembler,
                   FixedReorderBuffer* reorder_buffer, std::uint64_t ingress_identity) noexcept;

  [[nodiscard]] bool has_live_authority() const noexcept;
  [[nodiscard]] ksj::base::Status fail_closed_for_same_scan(CompletedFrameLease& lease,
                                                            ksj::base::Status cause) noexcept;
  void disarm_after_move() noexcept;
  void detach_host_failure_notifier_noexcept() noexcept;
  void terminate_components_noexcept() noexcept;
  void release_noexcept() noexcept;

  ksj::recon::ExecutionPlan execution_plan_;
  ksj::recon::VerificationRecord verification_record_;
  std::string node_id_;
  std::string completed_frame_input_port_;
  HostFrameAssembler* assembler_{nullptr};
  FixedReorderBuffer* reorder_buffer_{nullptr};
  std::uint64_t ingress_identity_{0U};
  bool bound_{false};
  // Host and reorder EndOfInput have both accepted the input fence.  Existing
  // publish leases may still drain, so this is deliberately distinct from a
  // terminal failure/cancellation authority.
  bool input_closed_{false};
  bool terminal_{false};
};

} // namespace ksj::recon::runtime
