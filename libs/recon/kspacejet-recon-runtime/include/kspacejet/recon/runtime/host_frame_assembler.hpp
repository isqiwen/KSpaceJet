#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

namespace detail {
struct HostFrameAssemblerState;
} // namespace detail

class CompletedFrameLease;
class FrameDispatch;
class FixedReorderBuffer;
class M3PublishLease;
class M3ReorderIngress;

enum class CompletedFrameLeaseBindingStatus : std::uint8_t {
  match,
  foreign,
  stale_or_consumed,
};

// A writable capability for one FrameSlot while it is assembling exactly one
// Cartesian frame.  It cannot be copied or constructed by an adapter.  Dropping
// an unsettled lease fails the scan rather than silently recycling incomplete
// source data.
class FrameAssemblyLease final {
public:
  FrameAssemblyLease() = default;
  ~FrameAssemblyLease();

  FrameAssemblyLease(const FrameAssemblyLease&) = delete;
  FrameAssemblyLease& operator=(const FrameAssemblyLease&) = delete;
  FrameAssemblyLease(FrameAssemblyLease&& other) noexcept;
  FrameAssemblyLease& operator=(FrameAssemblyLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] ksj::base::Status scatter(CartesianLineCoordinate coordinate, ksj::base::ConstByteSpan payload);

  // Succeeds only after the exact configured coverage set is complete.  It
  // consumes this writable lease and produces the sole read-only capability
  // for the sealed slot generation.
  [[nodiscard]] ksj::base::Result<CompletedFrameLease> seal_complete();

private:
  friend class HostFrameAssembler;

  FrameAssemblyLease(std::shared_ptr<detail::HostFrameAssemblerState> state, std::size_t slot_index,
                     FrameSlotToken token, std::uint64_t lease_id) noexcept;

  void abandon_noexcept() noexcept;

  std::shared_ptr<detail::HostFrameAssemblerState> state_{};
  std::size_t slot_index_{0U};
  FrameSlotToken token_{};
  std::uint64_t lease_id_{0U};
};

// A move-only host capability for a fully sealed Cartesian FrameSlot.  The
// slot data and semantic context are valid only while this lease remains
// valid.  It is intentionally not a BufferHandle: it has one synchronous
// consumer, no retain/fan-out/async operation, and no cross-device transfer.
class CompletedFrameLease final {
public:
  CompletedFrameLease() = default;
  ~CompletedFrameLease();

  CompletedFrameLease(const CompletedFrameLease&) = delete;
  CompletedFrameLease& operator=(const CompletedFrameLease&) = delete;
  CompletedFrameLease(CompletedFrameLease&& other) noexcept;
  CompletedFrameLease& operator=(CompletedFrameLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  // The returned span is an immutable borrow of host-owned FrameSlot storage.
  // It becomes invalid when this lease is acknowledged, abandoned, or the
  // assembler is aborted.  Concurrent use of one lease is outside M3.5.
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> bytes() const;
  [[nodiscard]] ksj::base::Result<FrameSlotContext> context() const;
  [[nodiscard]] ksj::base::Result<FrameSlotToken> token() const;

  // Compares only opaque host-issued binding facts.  A match does not prove
  // that an arbitrary caller owns this lease; M3ReorderIngress additionally
  // binds to one HostFrameAssembler instance before accepting it.
  [[nodiscard]] CompletedFrameLeaseBindingStatus
  binding_status(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record,
                 std::string_view node_id, std::string_view completed_frame_input_port) const noexcept;

  // Marks the sealed input as in use by its single synchronous consumer.
  // The consumer must then call acknowledge_consumed() exactly once after it
  // has stopped reading the bytes, or abandon() to fail closed. Once this
  // host is bound to an M3ReorderIngress, these public source-consumption
  // controls reject: only the ingress's identity-gated private handoff may
  // transition the lease, so a caller cannot recycle a source slot around
  // the paired reorder permit.
  [[nodiscard]] ksj::base::Status begin_dispatch();
  [[nodiscard]] ksj::base::Status acknowledge_consumed();
  [[nodiscard]] ksj::base::Status abandon();

private:
  friend class HostFrameAssembler;
  friend class FrameAssemblyLease;
  friend class M3ReorderIngress;
  friend class FrameDispatch;
  friend class M3PublishLease;

  CompletedFrameLease(std::shared_ptr<detail::HostFrameAssemblerState> state, std::size_t slot_index,
                      FrameSlotToken token, std::uint64_t lease_id) noexcept;

  void abandon_noexcept() noexcept;
  // A last-resort terminal transition for the coupled downstream dispatch.
  // It works from the shared control state rather than a raw assembler
  // pointer, so it remains safe while a lease outlives its assembler object.
  void emergency_abandon_noexcept() noexcept;
  // Releases the retained shared-state terminal capability after the ordered
  // output has been durably acknowledged. It never changes host state.
  void release_terminal_authority_noexcept() noexcept;
  // Checks the coupled host terminal state through the retained shared
  // control block. It is deliberately conservative for M3 output paths:
  // absence of state or a lock/runtime exception is treated as failure, so a
  // later source-lease drop cannot let an older ordered output publish.
  [[nodiscard]] bool host_failed_noexcept() const noexcept;
  [[nodiscard]] ksj::base::Status begin_dispatch_from_m3_reorder_ingress(std::uint64_t ingress_identity);
  [[nodiscard]] ksj::base::Status acknowledge_consumed_from_m3_reorder_ingress(std::uint64_t ingress_identity);
  [[nodiscard]] ksj::base::Status abandon_from_m3_reorder_ingress(std::uint64_t ingress_identity);

  std::shared_ptr<detail::HostFrameAssemblerState> state_{};
  std::size_t slot_index_{0U};
  FrameSlotToken token_{};
  std::uint64_t lease_id_{0U};
};

struct HostFrameAssemblerConfig {
  // This is an opaque scan/run identity supplied by the outer runtime.  It is
  // copied into every completion authority so a future ingress can reject a
  // lease from another admitted scan even when the graph is otherwise equal.
  std::string scan_instance_id;

  // The caller supplies the finite, preallocated Cartesian FrameSlot pool.
  // M3.5 does not yet claim that this pool was derived from a serialized
  // FrameAssemblyPlan or charged by a compiler-owned ResourceVector; it is a
  // provenance/lifetime boundary only.
  std::vector<CartesianFrameSlotConfig> frame_slots;
};

struct HostFrameAssemblerSnapshot {
  bool ingress_closed{false};
  bool failed{false};
  std::size_t free_slots{0U};
  std::size_t filling_slots{0U};
  std::size_t ready_slots{0U};
  std::size_t dispatched_slots{0U};
  std::size_t quarantined_slots{0U};
  ksj::base::Status last_error{};
};

// A scan-owned host completion authority for the narrow M3.5 Cartesian path.
// It is deliberately separate from SerialCartesianPipeline: the latter is a
// serial M1 oracle, whereas this object establishes a non-forgeable sealed
// frame capability that a later compiled executor can hand to reorder.
class HostFrameAssembler final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<HostFrameAssembler>>
  create(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record, std::string_view node_id,
         HostFrameAssemblerConfig config);

  HostFrameAssembler(const HostFrameAssembler&) = delete;
  HostFrameAssembler& operator=(const HostFrameAssembler&) = delete;
  HostFrameAssembler(HostFrameAssembler&&) = delete;
  HostFrameAssembler& operator=(HostFrameAssembler&&) = delete;
  ~HostFrameAssembler();

  // Acquires one free physical slot and begins exactly one semantic frame.
  // Two live slots may not carry the same complete FrameSlotContext.
  [[nodiscard]] ksj::base::Result<FrameAssemblyLease> try_begin_frame(FrameSlotContext context);

  // Stops accepting new frames.  It returns Unavailable while already sealed
  // leases are still being consumed; it fails if any slot is still filling,
  // because M3.5 supports complete frames only. Once an M3ReorderIngress is
  // bound, this public terminal control rejects and the ingress owns the
  // coupled source-first EndOfInput transition.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Exceptional terminal path.  It invalidates every outstanding lease and
  // keeps its shared state alive until those move-only capabilities disappear.
  // Once an M3ReorderIngress is bound, the public path rejects so cancellation
  // cannot leave the paired reorder buffer accepting.
  [[nodiscard]] ksj::base::Status abort();

  [[nodiscard]] HostFrameAssemblerSnapshot snapshot() const;

  // This narrow identity check lets a compiled ingress reject a lease from a
  // different scan instance even if its plan artifacts happen to be equal.
  [[nodiscard]] bool owns(const CompletedFrameLease& lease) const noexcept;

private:
  friend class M3ReorderIngress;

  explicit HostFrameAssembler(std::shared_ptr<detail::HostFrameAssemblerState> state) noexcept;

  // Last-resort no-throw terminal transition for an owning runtime boundary.
  // It intentionally favors quarantine over recovering resources or producing
  // a diagnostic, because it may run while handling allocation failure.
  void emergency_abort_noexcept() noexcept;

  // M3.5 seals source terminal control to the same scan-local ingress that
  // owns the paired reorder buffer. Binding is one-way after create() has
  // completed and is admitted only before any source lease was issued;
  // public terminal control rejects while this identity is live.
  [[nodiscard]] ksj::base::Status bind_m3_reorder_ingress(std::uint64_t ingress_identity,
                                                          FixedReorderBuffer& reorder_buffer);
  [[nodiscard]] bool has_m3_reorder_ingress(std::uint64_t ingress_identity) const noexcept;
  [[nodiscard]] bool has_same_issuer_state(const CompletedFrameLease& lease) const noexcept;
  [[nodiscard]] ksj::base::Status end_of_input_from_m3_reorder_ingress(std::uint64_t ingress_identity);
  [[nodiscard]] ksj::base::Status abort_from_m3_reorder_ingress(std::uint64_t ingress_identity);
  // Once both M3 components are terminal, stop retaining the raw buffer
  // notifier address. The binding identity remains sealed, but a later Host
  // destructor or retained lease must not dereference a destroyed buffer.
  void detach_m3_reorder_failure_notifier_after_terminal(std::uint64_t ingress_identity) noexcept;

  // Verifies the immutable source binding and pristine pre-admission state
  // that create() must couple to a FixedReorderBuffer before it permanently
  // binds either component. This is intentionally not public: callers
  // receive only lease capabilities.
  [[nodiscard]] bool matches_m3_reorder_ingress_binding(const ExecutionPlan& execution_plan,
                                                        const VerificationRecord& verification_record,
                                                        std::string_view node_id,
                                                        std::string_view completed_frame_input_port) const noexcept;

  std::shared_ptr<detail::HostFrameAssemblerState> state_{};
};

} // namespace ksj::recon::runtime
