#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/fixed_buffer_edge.hpp"
#include "kspacejet/recon/runtime/m3_reorder_ingress.hpp"
#include "kspacejet/recon/runtime/synchronous_firing_lease.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace ksj::recon::runtime {

namespace detail {
struct FrozenAbiTypeDescriptor;
struct PlanBoundAdmissionLifetime;
} // namespace detail

// The raw slabs remain caller-owned and must outlive this data-plane context,
// every bridge made from it, and every edge consumer lease.  The context
// proves only that the supplied slabs fit the selected frozen plans; it does
// not allocate memory or establish OS allocation provenance.
struct PlanBoundDataPlaneStorage {
  FixedBufferPoolStorage pool{};
  FixedBufferEdgeStorage edge{};
};

// This is deliberately not SynchronousFiringLeaseConfig.  A product bridge
// cannot choose a resource ledger or supply arbitrary output spans: its only
// output grant is constructed from the selected BufferPoolPlan.  The dynamic
// reservation is scratch/CPU work only; output-pool bytes are already held by
// the admitted plan-bound pool and must not appear here a second time.
struct PlanBoundSynchronousFiringConfig {
  ResourceVector firing_reservation;
  std::uint32_t maximum_input_batches{0U};
  std::uint32_t maximum_input_items{0U};
  std::uint64_t maximum_input_payload_bytes{0U};
  std::uint64_t maximum_scratch_bytes{0U};
  std::uint64_t maximum_metadata_bytes{64U * 1024U};
};

// The production path obtains its sole input from a non-forgeable prepared
// FrameDispatch. It deliberately has no raw input spans, ABI descriptors, or
// output fields for callers to choose.
struct PlanBoundReorderFiringRequest {
  std::uint64_t resource_occurrence_id{0U};
  std::uint64_t slot_generation{0U};
  std::uint64_t terminal_epoch{0U};
  ksj::base::ByteSpan scratch{};
};

class PlanBoundSynchronousOutputBridge;
class AdmittedPlanBoundDataPlane;
class PlanBoundFrameDispatch;
class PlanBoundSinkLease;
struct PlanBoundSinkPoll;

// The context-owned M3 ingress capability. It is deliberately a facade: the
// context retains the FixedReorderBuffer, M3ReorderIngress identity, and
// private local ledger for their entire joint lifetime. This prevents a
// second/free reorder buffer from consuming the same admitted sub-budget.
// Normal EndOfInput belongs to PlanBoundSynchronousOutputBridge so the source,
// reorder, and edge fence in that order. The context must outlive this facade
// and every PlanBoundFrameDispatch returned from it.
class PlanBoundM3ReorderIngress final {
public:
  PlanBoundM3ReorderIngress() = default;
  ~PlanBoundM3ReorderIngress();

  PlanBoundM3ReorderIngress(const PlanBoundM3ReorderIngress&) = delete;
  PlanBoundM3ReorderIngress& operator=(const PlanBoundM3ReorderIngress&) = delete;
  PlanBoundM3ReorderIngress(PlanBoundM3ReorderIngress&& other) noexcept;
  PlanBoundM3ReorderIngress& operator=(PlanBoundM3ReorderIngress&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  // On success the returned capability encloses the raw FrameDispatch. Its
  // public surface intentionally cannot complete, abort, or publish a raw
  // reorder payload: process_reorder() is its only M3.7 consumption route.
  [[nodiscard]] ksj::base::Result<PlanBoundFrameDispatch> try_prepare(CompletedFrameLease& lease);

private:
  friend class AdmittedPlanBoundDataPlane;

  explicit PlanBoundM3ReorderIngress(AdmittedPlanBoundDataPlane* owner) noexcept;

  void release_noexcept() noexcept;

  AdmittedPlanBoundDataPlane* owner_{nullptr};
};

// A context-bound prepared completed frame. It deliberately exposes no raw
// FrameDispatch and no `complete`/`abort`/`try_acquire_publish` operation,
// because those could bypass the mandatory Provider→typed reorder→edge route.
// Dropping it fails the owning context closed. The context and ingress facade
// must outlive it.
class PlanBoundFrameDispatch final {
public:
  PlanBoundFrameDispatch() = default;
  ~PlanBoundFrameDispatch();

  PlanBoundFrameDispatch(const PlanBoundFrameDispatch&) = delete;
  PlanBoundFrameDispatch& operator=(const PlanBoundFrameDispatch&) = delete;
  PlanBoundFrameDispatch(PlanBoundFrameDispatch&& other) noexcept;
  PlanBoundFrameDispatch& operator=(PlanBoundFrameDispatch&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] Quantity ordinal() const noexcept;

private:
  friend class AdmittedPlanBoundDataPlane;
  friend class PlanBoundM3ReorderIngress;
  friend class PlanBoundSynchronousOutputBridge;

  PlanBoundFrameDispatch(AdmittedPlanBoundDataPlane* owner, FrameDispatch dispatch) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  AdmittedPlanBoundDataPlane* owner_{nullptr};
  FrameDispatch dispatch_{};
};

// A completed Provider firing whose output is retained by reorder while its
// detached edge credit remains reserved. It is the only production path from
// a plan-bound Provider output to the edge. `try_publish()` is retryable only
// while reorder reports Unavailable; success atomically uses
// M3PublishLease::commit_to_edge(), which transfers the same BufferHandle and
// then acknowledges reorder. The context and its bridge must outlive this
// move-only capability.
class PlanBoundOrderedOutput final {
public:
  PlanBoundOrderedOutput() = default;
  ~PlanBoundOrderedOutput();

  PlanBoundOrderedOutput(const PlanBoundOrderedOutput&) = delete;
  PlanBoundOrderedOutput& operator=(const PlanBoundOrderedOutput&) = delete;
  PlanBoundOrderedOutput(PlanBoundOrderedOutput&& other) noexcept;
  PlanBoundOrderedOutput& operator=(PlanBoundOrderedOutput&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] Quantity ordinal() const noexcept;

  // Returns Unavailable without consuming this capability when an earlier
  // ordinal has not reached ordered publish yet. Any other error is
  // fail-closed and settles this capability.
  [[nodiscard]] ksj::base::Status try_publish();
  [[nodiscard]] ksj::base::Status abort();

private:
  friend class PlanBoundSynchronousOutputBridge;

  PlanBoundOrderedOutput(AdmittedPlanBoundDataPlane* owner, FrameDispatch dispatch,
                         FixedBufferEdgeProducerReservation edge_reservation) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  AdmittedPlanBoundDataPlane* owner_{nullptr};
  FrameDispatch dispatch_{};
  FixedBufferEdgeProducerReservation edge_reservation_{};
};

// A successful Result means the Provider callback was entered. A `done`
// firing has one valid ordered output; failure outcomes have no output and
// the coupled data plane has already been failed closed.
struct PlanBoundReorderFiringResult {
  SynchronousFiringResult firing{};
  PlanBoundOrderedOutput ordered_output{};
};

// The only sink-visible M3.7 handle capability. It preserves the global
// admission reservation after its owning context has been destroyed, until
// the fixed edge lease has either acknowledged its BufferHandle or failed
// closed on drop. Raw FixedBufferEdgeConsumerLease is deliberately not
// returned by a plan-bound context.
class PlanBoundSinkLease final {
public:
  PlanBoundSinkLease() = default;
  ~PlanBoundSinkLease();

  PlanBoundSinkLease(const PlanBoundSinkLease&) = delete;
  PlanBoundSinkLease& operator=(const PlanBoundSinkLease&) = delete;
  PlanBoundSinkLease(PlanBoundSinkLease&& other) noexcept;
  PlanBoundSinkLease& operator=(PlanBoundSinkLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] const ImmutableBufferHandle& buffer() const noexcept { return edge_lease_.buffer(); }
  [[nodiscard]] ksj::base::Status acknowledge_consumed();

private:
  friend class AdmittedPlanBoundDataPlane;

  PlanBoundSinkLease(std::shared_ptr<detail::PlanBoundAdmissionLifetime> admission_lifetime,
                     FixedBufferEdgeConsumerLease edge_lease) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  // This field must precede edge_lease_: C++ destroys members in reverse
  // declaration order, so an unacknowledged edge/pool handle settles before
  // the global admission reservation can release.
  std::shared_ptr<detail::PlanBoundAdmissionLifetime> admission_lifetime_{};
  FixedBufferEdgeConsumerLease edge_lease_{};
};

struct PlanBoundSinkPoll {
  FixedBufferEdgePollKind kind{FixedBufferEdgePollKind::empty};
  std::optional<PlanBoundSinkLease> lease{};
};

// A scan-owned, admitted M3.7 single-pool/single-edge data plane.  It owns
// the one already-committed global admission token for its lifetime and shares
// that token with any outstanding PlanBoundSinkLease, while a distinct local
// mirror ledger enforces sub-budget accounting for the pool, edge, and ABI
// staging. The global ledger is never handed to those components, so the pool
// payload has one global physical charge only.
class AdmittedPlanBoundDataPlane final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<AdmittedPlanBoundDataPlane>>
  create(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record,
         const AdmissionRecord& admission_record, ResourceVectorLedgerReservation admission_reservation,
         PlanBoundDataPlaneStorage storage);

  AdmittedPlanBoundDataPlane(const AdmittedPlanBoundDataPlane&) = delete;
  AdmittedPlanBoundDataPlane& operator=(const AdmittedPlanBoundDataPlane&) = delete;
  AdmittedPlanBoundDataPlane(AdmittedPlanBoundDataPlane&&) = delete;
  AdmittedPlanBoundDataPlane& operator=(AdmittedPlanBoundDataPlane&&) = delete;
  ~AdmittedPlanBoundDataPlane();

  // There can be only one producer bridge because this M3.7 context freezes
  // one serial source pool and one FIFO edge.  Destroying a live bridge
  // without normal EndOfInput aborts the data plane fail-closed.
  [[nodiscard]] ksj::base::Result<PlanBoundSynchronousOutputBridge>
  create_synchronous_one_output_bridge(PlanBoundSynchronousFiringConfig config);

  // Product M3.7 reorder construction is exclusive and context-owned. The
  // caller receives a non-owning ingress capability, while the context keeps
  // the FixedReorderBuffer and M3ReorderIngress identity alive and binds both
  // to this private local ledger. A second creation attempt is rejected.
  // The AdmittedPlanBoundDataPlane must outlive every returned ingress facade,
  // FrameDispatch capability, bridge, and ordered output: those producer-side
  // capabilities intentionally retain raw context identity rather than extend
  // a scan's control lifetime implicitly. `assembler` must outlive this context
  // and every returned capability.
  [[nodiscard]] ksj::base::Result<PlanBoundM3ReorderIngress>
  create_m3_reorder_ingress(std::string_view producer_node_id, HostFrameAssembler& assembler,
                            FixedReorderBufferStorage storage);

  // The sole sink-side path. It exposes a plan-bound consumer capability,
  // never a raw edge lease, pool ownership, or producer credit. Unlike the
  // producer-side raw-owner capabilities, a live sink lease retains the one
  // global admission lifetime through its final acknowledge/drop so payload
  // physical charge cannot disappear while the handle remains observable.
  [[nodiscard]] PlanBoundSinkPoll try_acquire_for_sink();
  [[nodiscard]] FixedBufferEdgeSnapshot edge_snapshot() const;

private:
  friend class PlanBoundSynchronousOutputBridge;
  friend class PlanBoundOrderedOutput;
  friend class PlanBoundM3ReorderIngress;
  friend class PlanBoundFrameDispatch;

  AdmittedPlanBoundDataPlane(ExecutionPlan execution_plan, VerificationRecord verification_record,
                             BufferPoolPlan pool_plan, DataEdgePlan edge_plan,
                             std::shared_ptr<detail::PlanBoundAdmissionLifetime> admission_lifetime,
                             std::shared_ptr<ResourceVectorLedger> local_ledger,
                             ResourceVectorLedgerReservation firing_lease_staging_reservation,
                             std::unique_ptr<FixedBufferPool> pool, std::unique_ptr<FixedBufferEdge> edge,
                             std::shared_ptr<const detail::FrozenAbiTypeDescriptor> input_type,
                             std::shared_ptr<const detail::FrozenAbiTypeDescriptor> output_type) noexcept;

  [[nodiscard]] ksj::base::Status abort_no_bridge_check();
  [[nodiscard]] ksj::base::Status end_of_input_from_bridge();
  [[nodiscard]] ksj::base::Result<FrameDispatch> try_prepare_from_context_ingress(CompletedFrameLease& lease);
  [[nodiscard]] bool owns_context_dispatch(const FrameDispatch& dispatch) const noexcept;
  [[nodiscard]] bool has_live_context_ingress() const noexcept;
  [[nodiscard]] ksj::base::Status retain_pending_ordered_output();
  void release_pending_ordered_output_noexcept() noexcept;
  void release_bridge_noexcept(bool normal_end_of_input) noexcept;

  // Declaration order is intentional: edge/pool/local sub-ledger settle
  // before the shared global admission lifetime releases its one reservation.
  // A PlanBoundSinkLease may retain this object after context destruction.
  std::shared_ptr<detail::PlanBoundAdmissionLifetime> admission_lifetime_{};
  std::shared_ptr<ResourceVectorLedger> local_ledger_{};
  ResourceVectorLedgerReservation firing_lease_staging_reservation_{};
  std::unique_ptr<FixedBufferPool> pool_{};
  std::unique_ptr<FixedBufferEdge> edge_{};
  // Declared in this order so the ingress releases its raw buffer authority
  // before the context-owned FixedReorderBuffer, edge, pool, local ledger,
  // and finally the one global admission reservation are torn down.
  std::unique_ptr<FixedReorderBuffer> reorder_buffer_{};
  std::optional<M3ReorderIngress> reorder_ingress_{};
  std::shared_ptr<const detail::FrozenAbiTypeDescriptor> input_type_{};
  std::shared_ptr<const detail::FrozenAbiTypeDescriptor> output_type_{};
  ExecutionPlan execution_plan_;
  VerificationRecord verification_record_;
  BufferPoolPlan pool_plan_;
  DataEdgePlan edge_plan_;
  bool bridge_active_{false};
  bool reorder_input_closed_{false};
  Quantity pending_ordered_outputs_{0U};
};

// The M3.7 Provider-output bridge processes a prepared context-bound
// FrameDispatch and retains its detached edge credit through reorder. The
// context must outlive this bridge and every PlanBoundOrderedOutput it returns.
class PlanBoundSynchronousOutputBridge final {
public:
  PlanBoundSynchronousOutputBridge(const PlanBoundSynchronousOutputBridge&) = delete;
  PlanBoundSynchronousOutputBridge& operator=(const PlanBoundSynchronousOutputBridge&) = delete;
  PlanBoundSynchronousOutputBridge(PlanBoundSynchronousOutputBridge&& other) noexcept;
  PlanBoundSynchronousOutputBridge& operator=(PlanBoundSynchronousOutputBridge&& other) noexcept;
  ~PlanBoundSynchronousOutputBridge();

  [[nodiscard]] bool valid() const noexcept;

  // Production M3.7 route. Before the Provider callback it acquires one
  // detached edge credit and one pool lease, then commits the FrameDispatch.
  // `dispatch` must have come from this context's PlanBoundM3ReorderIngress;
  // an external/second reorder identity is rejected before Provider code. On
  // success this consumes `dispatch` into the returned ordered output. On
  // pre-callback capacity pressure `dispatch` remains prepared for retry.
  // The caller must supply an already-created trusted in-process Provider
  // lifecycle (operator handle, execution context, key state, and canonical
  // node config). M3.7 verifies the loaded module bundle, named operator, and
  // contract digest against the frozen BufferPoolPlan, but intentionally does
  // not reconstruct or attest opaque ABI handles/configuration; that is a
  // later lifecycle/provenance boundary.
  [[nodiscard]] ksj::base::Result<PlanBoundReorderFiringResult>
  process_reorder(const SynchronousProviderInvocation& invocation, PlanBoundFrameDispatch& dispatch,
                  const PlanBoundReorderFiringRequest& request);

  // Normal closure fences new producer work while allowing the edge's queued
  // item(s) to drain.  It is distinct from abort/cancellation.
  [[nodiscard]] ksj::base::Status end_of_input();
  [[nodiscard]] ksj::base::Status abort();

private:
  friend class AdmittedPlanBoundDataPlane;

  PlanBoundSynchronousOutputBridge(AdmittedPlanBoundDataPlane* owner, SynchronousFiringLeaseHost host) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  AdmittedPlanBoundDataPlane* owner_{nullptr};
  SynchronousFiringLeaseHost host_;
  bool normal_end_of_input_{false};
};

} // namespace ksj::recon::runtime
