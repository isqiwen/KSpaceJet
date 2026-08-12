#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/buffer_pool.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"
#include "kspacejet/recon/runtime/detail/slab_range_claim.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ksj::recon::runtime {

class FrameDispatch;
class HostFrameAssembler;
class M3ReorderIngress;
class M3PublishLease;

namespace detail {
struct HostFrameAssemblerState;
} // namespace detail

// The M3.7 ReorderPlan charges a 16-byte durable record for every ordinal in
// its closed Cartesian domain, a 16-byte physical record for every
// simultaneously retained *ahead* output, and a 64-byte immutable-handle
// sidecar per such output. The current next_expected output is direct and is
// carried by its capability rather than the reorder slab. The durable/physical
// records use caller-owned byte storage through memcpy accessors. Handle
// sidecars are deliberately separate typed, aligned caller storage:
// materializing an ImmutableBufferHandle in a raw byte-aligned slab would be
// undefined.
inline constexpr std::size_t kFixedReorderBufferStorageAlignment = alignof(ksj::base::byte);

[[nodiscard]] constexpr std::size_t fixed_reorder_buffer_storage_alignment() noexcept {
  return kFixedReorderBufferStorageAlignment;
}

// This is the concrete runtime representation of the M3.7 fixed handle
// sidecar. The artifact reserves 64 bytes per sidecar. Keep this proof at the
// ABI boundary so a future handle representation cannot silently exceed the
// frozen plan charge.
struct FixedReorderBufferHandleSidecar final {
  std::optional<ImmutableBufferHandle> handle{};
};

static_assert(sizeof(FixedReorderBufferHandleSidecar) <= ksj::recon::kDenseCartesianReorderHandleSidecarChargedBytes);
static_assert(alignof(FixedReorderBufferHandleSidecar) >= alignof(ImmutableBufferHandle));

// Product M3.7 creation uses this split storage form. `bookkeeping` owns the
// 16-byte ordinal and 16-byte physical-slot records; `handle_sidecars` owns
// the separately aligned slots for move-only ImmutableBufferHandle objects.
// Both allocations are caller-owned and must outlive the buffer plus every
// issued dispatch or publish lease. Creation claims both byte ranges
// exclusively against all participating Pool/Edge/Reorder slabs in this
// runtime image for that same lifetime.
struct FixedReorderBufferStorage {
  ksj::base::ByteSpan bookkeeping{};
  std::span<FixedReorderBufferHandleSidecar> handle_sidecars{};
};

// Validates the frozen dense-v1 accounting identity and returns the precise
// number of metadata bytes the caller must provide. A raw plan must outlive a
// resulting mapper; FixedReorderBuffer copies its selected verified plan, but
// caller storage must still outlive that buffer.
[[nodiscard]] ksj::base::Result<std::size_t> required_storage_bytes(const ksj::recon::ReorderPlan& plan);

// Returns only the raw memcpy bookkeeping prefix. M3.7 product creation uses
// this alongside `max_ahead_items()` typed FixedReorderBufferHandleSidecars;
// their charged capacity remains part of `required_storage_bytes(plan)`.
[[nodiscard]] ksj::base::Result<std::size_t>
fixed_reorder_buffer_required_bookkeeping_storage_bytes(const ksj::recon::ReorderPlan& plan);

// Converts the M3 plan binding -- the semantic key of a claimed completed
// FrameSlotContext -- to the frozen dense mixed-radix ordinal. This primitive
// checks the closed semantic domain but cannot itself prove HostFrameAssembler
// provenance; that typed-edge boundary is future runtime integration. The
// mapper owns no dynamic state and performs no allocation on its successful
// hot path. `segment` is intentionally absent until it is part of
// FrameSemanticKey and the compiler permits it.
class DenseCartesianOrdinalMapper final {
public:
  // Exposed only as the immutable interpretation of a frozen plan axis.
  // Callers do not construct mappings from this enum; create() remains the
  // sole construction path.
  enum class Axis : std::uint8_t {
    encoding,
    average,
    slice,
    contrast,
    phase,
    repetition,
    set,
  };

  [[nodiscard]] static ksj::base::Result<DenseCartesianOrdinalMapper> create(const ksj::recon::ReorderPlan& plan);

  [[nodiscard]] ksj::base::Result<ksj::recon::Quantity> ordinal(const FrameSlotContext& context) const;

  [[nodiscard]] const ksj::recon::ReorderPlan& plan() const noexcept { return *plan_; }

private:
  friend class FixedReorderBuffer;

  struct Dimension {
    Axis axis{Axis::encoding};
    ksj::recon::Quantity minimum{0U};
    ksj::recon::Quantity cardinality{0U};
  };

  static constexpr std::size_t kMaximumDimensions = 7U;

  DenseCartesianOrdinalMapper(const ksj::recon::ReorderPlan* plan, std::array<Dimension, kMaximumDimensions> dimensions,
                              std::size_t dimension_count) noexcept
      : plan_(plan), dimensions_(dimensions), dimension_count_(dimension_count) {}

  const ksj::recon::ReorderPlan* plan_{nullptr};
  std::array<Dimension, kMaximumDimensions> dimensions_{};
  std::size_t dimension_count_{0U};
};

// Legacy M3.5 compatibility payload. New plan-bound M3.7 creation rejects
// this route and moves ImmutableBufferHandle objects through typed sidecars.
class OpaqueReorderPayloadHandle final {
public:
  [[nodiscard]] static constexpr OpaqueReorderPayloadHandle from_opaque_id(const std::uint64_t opaque_id) noexcept {
    return OpaqueReorderPayloadHandle{opaque_id};
  }

  [[nodiscard]] constexpr std::uint64_t opaque_id() const noexcept { return opaque_id_; }

  friend constexpr bool operator==(const OpaqueReorderPayloadHandle&,
                                   const OpaqueReorderPayloadHandle&) noexcept = default;

private:
  explicit constexpr OpaqueReorderPayloadHandle(const std::uint64_t opaque_id) noexcept : opaque_id_(opaque_id) {}

  std::uint64_t opaque_id_{0U};
};

struct FixedReorderOutput {
  ksj::recon::Quantity ordinal{0U};
  OpaqueReorderPayloadHandle payload{OpaqueReorderPayloadHandle::from_opaque_id(0U)};

  [[nodiscard]] friend constexpr bool operator==(const FixedReorderOutput&,
                                                 const FixedReorderOutput&) noexcept = default;
};

class FixedReorderBuffer;
class PublishLease;

// A move-only reservation of one frozen M3 dispatch. Ordinals strictly ahead
// of next_expected consume one ReorderPlan ahead item, byte charge, and
// descriptor credit. The current head is deliberately direct: it travels in
// this capability and consumes no reorder slot, while still retaining its
// one pool-backed payload until ordered handoff. commit() marks the Provider
// callback as started; complete() is intentionally owned by
// FixedReorderBuffer because it must bind one opaque compatibility output or
// one typed ImmutableBufferHandle to this exact completed-frame ordinal.
// Destroying an uncommitted permit rolls it back.
// Destroying a started/completed permit is a runtime failure and aborts the
// buffer rather than allowing an ambiguous retry.
class DispatchPermit final {
public:
  DispatchPermit() = default;
  ~DispatchPermit();

  DispatchPermit(const DispatchPermit&) = delete;
  DispatchPermit& operator=(const DispatchPermit&) = delete;
  DispatchPermit(DispatchPermit&& other) noexcept;
  DispatchPermit& operator=(DispatchPermit&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ksj::recon::Quantity ordinal() const noexcept { return ordinal_; }

private:
  friend class FixedReorderBuffer;
  friend class FrameDispatch;
  friend class M3ReorderIngress;
  friend class PublishLease;

  enum class Phase : std::uint8_t {
    invalid,
    prepared,
    in_flight,
    completed,
    publishing,
  };

  DispatchPermit(FixedReorderBuffer* owner, const ksj::recon::ReorderPlan* plan, std::uint64_t buffer_identity,
                 ksj::recon::Quantity ordinal, ksj::recon::Quantity slot_id) noexcept
      : owner_(owner), plan_(plan), buffer_identity_(buffer_identity), ordinal_(ordinal), slot_id_(slot_id),
        phase_(Phase::prepared) {}

  void release_noexcept() noexcept;
  void disarm() noexcept;

  [[nodiscard]] ksj::base::Status commit();
  [[nodiscard]] ksj::base::Status abort();

  FixedReorderBuffer* owner_{nullptr};
  // The buffer-owned plan seals node, order-domain, completed-frame input,
  // and ordered-output identities into this otherwise opaque permit.
  const ksj::recon::ReorderPlan* plan_{nullptr};
  std::uint64_t buffer_identity_{0U};
  ksj::recon::Quantity ordinal_{0U};
  ksj::recon::Quantity slot_id_{0U};
  // Only the direct current-head path uses this inline capability. Ahead
  // outputs remain in the fixed plan-charged sidecar slab.
  std::optional<ImmutableBufferHandle> direct_head_handle_{};
  Phase phase_{Phase::invalid};
};

// A move-only sink-visible output. Its held dispatch credit is returned only
// by acknowledge_published(). An already exposed lease may acknowledge while
// the buffer is failed_draining solely to settle that external visibility; it
// never restores normal output or a Completed result. Dropping it without
// acknowledgement aborts the buffer and suppresses every remaining ordinary
// output.
class PublishLease final {
public:
  PublishLease() = default;
  ~PublishLease();

  PublishLease(const PublishLease&) = delete;
  PublishLease& operator=(const PublishLease&) = delete;
  PublishLease(PublishLease&& other) noexcept;
  PublishLease& operator=(PublishLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] const FixedReorderOutput& output() const noexcept { return output_; }
  [[nodiscard]] bool has_buffer_handle() const;

  [[nodiscard]] ksj::base::Status acknowledge_published();

private:
  friend class FixedReorderBuffer;
  friend class M3PublishLease;

  PublishLease(FixedReorderBuffer* owner, const std::uint64_t buffer_identity, DispatchPermit permit,
               FixedReorderOutput output, FixedReorderBufferHandleSidecar* handle_sidecar) noexcept
      : owner_(owner), buffer_identity_(buffer_identity), permit_(std::move(permit)), output_(output),
        handle_sidecar_(handle_sidecar) {}

  void release_noexcept() noexcept;
  void disarm() noexcept;
  // This private transfer exists solely for M3PublishLease::commit_to_edge(),
  // which immediately commits the handle into a pre-reserved internal edge
  // before settling the publish credit. It is intentionally not a public
  // escape hatch for a bound M3.7 product path.
  [[nodiscard]] ksj::base::Result<ImmutableBufferHandle> take_buffer_handle_for_ordered_edge_commit();

  // The embedded permit retains the same sealed plan identity, so a lease
  // cannot be acknowledged against another node/buffer/output domain.
  FixedReorderBuffer* owner_{nullptr};
  std::uint64_t buffer_identity_{0U};
  DispatchPermit permit_{};
  FixedReorderOutput output_{};
  FixedReorderBufferHandleSidecar* handle_sidecar_{nullptr};
};

// Creation reserves the complete, immutable ReorderPlan metadata and
// descriptor pool in this shared ledger. `max_ahead_charged_bytes` is a
// logical dataflow credit in M3.7, so it is intentionally not a second host
// payload reservation here; the source BufferPoolPlan owns the one physical
// payload charge.
struct FixedReorderBufferConfig {
  std::shared_ptr<ResourceVectorLedger> resource_ledger;
};

enum class FixedReorderBufferState : std::uint8_t {
  accepting,
  close_pending,
  draining,
  completed,
  // A terminal failure has frozen new ordinary work, but at least one
  // prepared/in-flight/completed/publishing permit still owns local credit
  // that must be explicitly settled before the scan-local ledger pool may be
  // released.
  failed_draining,
  failed,
};

struct FixedReorderBufferSnapshot {
  FixedReorderBufferState state{FixedReorderBufferState::accepting};
  ksj::recon::Quantity next_expected_ordinal{0U};
  ksj::recon::Quantity completed_ordinals{0U};
  ksj::recon::Quantity prepared_dispatches{0U};
  ksj::recon::Quantity in_flight_dispatches{0U};
  ksj::recon::Quantity publishing_outputs{0U};
  // A current-head dispatch bypasses the ahead slab but remains live through
  // prepare/complete/publish until it is acknowledged or failed.
  ksj::recon::Quantity direct_head_items{0U};
  ksj::recon::Quantity retained_items{0U};
  ksj::recon::Quantity retained_charged_bytes{0U};
  ksj::recon::Quantity free_slots{0U};
  std::size_t storage_bytes{0U};
  bool credit_pool_committed{false};
};

// Fixed-capacity, scan-local M3 reorder state for one immutable ReorderPlan
// slice. Creation validates one ExecutionPlan + independently produced
// VerificationRecord, then copies the selected immutable plan slice and its
// artifact digests; arbitrary ReorderPlan values cannot create runtime state
// and caller-artifact lifetimes do not leak into permits. Normal completed-frame
// ingress is private to M3ReorderIngress, which supplies a host-issued
// capability rather than a forgeable FrameSlotContext. All state changes are
// mutex-linearized; after create(), the hot path allocates no memory.
//
// M3 permits a sink to observe and acknowledge an ordered prefix before a
// later EndOfInput detects an absent tuple. In that case the buffer becomes
// failed; previously acknowledged outputs remain externally visible and this
// primitive makes no atomic-scan-output or rollback claim. RunRecord-visible
// output semantics are a later integration boundary.
//
// The buffer must outlive every DispatchPermit and PublishLease created from
// it, and must not be moved while any permit or lease exists. Those handles
// retain the buffer address as part of their ownership identity.
class FixedReorderBuffer final {
public:
  [[nodiscard]] static ksj::base::Result<FixedReorderBuffer>
  create(const ksj::recon::ExecutionPlan& execution_plan, const ksj::recon::VerificationRecord& verification_record,
         std::string_view node_id, ksj::base::ByteSpan storage, FixedReorderBufferConfig config);

  // M3.7 product creation. The typed sidecar span is required before a
  // BufferHandle can enter reorder; raw `ByteSpan` creation above remains an
  // opaque-payload compatibility seam for M3.5 tests only.
  [[nodiscard]] static ksj::base::Result<FixedReorderBuffer>
  create(const ksj::recon::ExecutionPlan& execution_plan, const ksj::recon::VerificationRecord& verification_record,
         std::string_view node_id, FixedReorderBufferStorage storage, FixedReorderBufferConfig config);

  FixedReorderBuffer(const FixedReorderBuffer&) = delete;
  FixedReorderBuffer& operator=(const FixedReorderBuffer&) = delete;
  FixedReorderBuffer(FixedReorderBuffer&& other) noexcept;
  FixedReorderBuffer& operator=(FixedReorderBuffer&& other) noexcept;
  ~FixedReorderBuffer();

  // Input closure is a control fence, not a data item. It first blocks new
  // prepares, then returns unavailable while prepared/in-flight callbacks
  // remain. Only after they quiesce does it apply M3's fail-only gap rule.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Freezes every ordinary output path. An abort with outstanding permits or
  // leases enters failed_draining and retains the scan-local precommitted
  // ledger pool until those owners explicitly settle or destruct; it never
  // re-admits held credits while an output may still be externally visible.
  // A quiescent abort transitions directly to failed and releases
  // the pool.
  [[nodiscard]] ksj::base::Status abort();

  [[nodiscard]] FixedReorderBufferSnapshot snapshot() const;

private:
  friend class DispatchPermit;
  friend class FrameDispatch;
  friend class HostFrameAssembler;
  friend class M3ReorderIngress;
  friend class PublishLease;
  friend struct detail::HostFrameAssemblerState;

  enum class OrdinalState : std::uint8_t {
    never_seen = 0U,
    prepared = 1U,
    in_flight = 2U,
    completed = 3U,
    publishing = 4U,
    acknowledged = 5U,
    discarded = 6U,
  };

  struct OrdinalRecord {
    OrdinalState state{OrdinalState::never_seen};
    ksj::recon::Quantity slot_id{0U};
    OpaqueReorderPayloadHandle payload{OpaqueReorderPayloadHandle::from_opaque_id(0U)};
  };

  struct PhysicalSlotRecord {
    bool free{true};
    ksj::recon::Quantity next_free_or_owner{0U};
    ksj::recon::Quantity stable_slot_id{0U};
  };

  FixedReorderBuffer(ksj::recon::ReorderPlan plan, std::string execution_plan_digest,
                     std::string verification_record_digest, std::shared_ptr<ResourceVectorLedger> resource_ledger,
                     detail::SlabRangeClaim slab_claim, ksj::base::byte* bookkeeping_storage,
                     std::size_t charged_storage_bytes, FixedReorderBufferHandleSidecar* handle_sidecars,
                     Quantity handle_sidecar_count, std::uint64_t buffer_identity) noexcept;

  [[nodiscard]] static ksj::base::Status validate_plan(const ksj::recon::ReorderPlan& plan);
  [[nodiscard]] static ksj::base::Result<ksj::recon::ResourceVector>
  credit_pool_reservation(const ksj::recon::ReorderPlan& plan);
  // M3.5 binds exactly one ingress authority to a buffer. It is deliberately
  // never released or rebound: a scan-local buffer cannot be reused for a
  // different host completion authority. The one exception is the private
  // create-time rollback below, before the ingress capability is exposed.
  [[nodiscard]] ksj::base::Status bind_m3_reorder_ingress(std::uint64_t ingress_identity);
  [[nodiscard]] ksj::base::Status unbind_m3_reorder_ingress_on_create_failure(std::uint64_t ingress_identity);
  [[nodiscard]] bool has_m3_reorder_ingress(std::uint64_t ingress_identity) const;

  // Once a buffer has a live M3.5 ingress authority, terminal control must
  // remain coupled to that ingress and its HostFrameAssembler.  These paths
  // are intentionally private so a holder of the buffer cannot close or
  // abort reorder independently of source completion authority.
  [[nodiscard]] ksj::base::Status end_of_input_from_m3_reorder_ingress(std::uint64_t ingress_identity);
  [[nodiscard]] ksj::base::Status abort_from_m3_reorder_ingress(std::uint64_t ingress_identity);

  // Last-resort no-throw terminal transition used only by owning M3.5 lease
  // cleanup. It deliberately preserves any committed pool rather than risk a
  // throwing release while handling an earlier exception; normal abort()
  // remains the accounting-complete terminal path.
  void emergency_abort_noexcept() noexcept;

  // The bound HostFrameAssembler invokes this no-throw path whenever any
  // source lease fails. Taking this buffer's mutex linearizes source failure
  // against the next ordered publish transition: a failure that wins the
  // mutex prevents a later output from becoming publishable.
  void fail_from_bound_host_noexcept(std::uint64_t ingress_identity) noexcept;

  // Only M3ReorderIngress may call this after it has validated a live
  // CompletedFrameLease from the exact HostFrameAssembler/plan/port binding.
  // A mapper/domain/duplicate/behind-next violation is strict-dense semantic
  // failure; ordinary ahead-window or local-credit exhaustion is retryable
  // Unavailable and leaves the caller-owned frame lease untouched.
  [[nodiscard]] ksj::base::Result<DispatchPermit>
  try_prepare_dispatch_from_trusted_completed_frame(std::uint64_t ingress_identity,
                                                    const FrameSlotContext& completed_frame_context);

  [[nodiscard]] ksj::base::Status complete(DispatchPermit& permit, OpaqueReorderPayloadHandle payload);
  [[nodiscard]] ksj::base::Status complete(DispatchPermit& permit, ImmutableBufferHandle& payload);

  // The dispatch retains a completed permit and calls this only for the
  // current next_expected ordinal. A non-next permit returns Unavailable
  // without consuming it; a successful call moves it into a PublishLease.
  [[nodiscard]] ksj::base::Result<PublishLease> try_acquire_publish(DispatchPermit& completed_permit);
  [[nodiscard]] ksj::base::Result<ImmutableBufferHandle> take_published_buffer_handle(PublishLease& lease);
  [[nodiscard]] bool publish_lease_has_buffer_handle(const PublishLease& lease) const;

  // Called with mutex_ held after the trusted semantic-key mapping has
  // succeeded. Keeping the ordinal form private prevents bypassing the
  // frozen HostFrameAssembler capability binding.
  [[nodiscard]] ksj::base::Result<DispatchPermit> try_prepare_dispatch_ordinal_unlocked(ksj::recon::Quantity ordinal);
  [[nodiscard]] ksj::base::Status commit_dispatch(DispatchPermit& permit);
  [[nodiscard]] ksj::base::Status abort_dispatch(DispatchPermit& permit);
  [[nodiscard]] ksj::base::Status acknowledge_published(PublishLease& lease);
  void abandon_dispatch_noexcept(DispatchPermit& permit) noexcept;
  void abandon_publish_noexcept(PublishLease& lease) noexcept;

  [[nodiscard]] bool permit_matches_unlocked(const DispatchPermit& permit) const noexcept;
  [[nodiscard]] bool can_run_or_complete_unlocked() const noexcept;
  [[nodiscard]] ksj::base::Status require_accepting_unlocked() const;
  [[nodiscard]] ksj::base::Status validate_ordinal_unlocked(ksj::recon::Quantity ordinal) const;
  [[nodiscard]] ksj::base::Status validate_local_credit_invariants_unlocked() const;
  [[nodiscard]] ksj::base::Status release_slot_unlocked(ksj::recon::Quantity slot_id,
                                                        ksj::recon::Quantity expected_ordinal);
  [[nodiscard]] FixedReorderBufferHandleSidecar* handle_sidecar_unlocked(ksj::recon::Quantity slot_id) noexcept;
  [[nodiscard]] const FixedReorderBufferHandleSidecar*
  handle_sidecar_unlocked(ksj::recon::Quantity slot_id) const noexcept;
  void clear_handle_sidecar_unlocked(ksj::recon::Quantity slot_id) noexcept;
  void clear_all_handle_sidecars_unlocked() noexcept;
  // Preserves an operation's diagnostic while freezing a currently valid
  // buffer on a strict-dense semantic or internal invariant violation.
  [[nodiscard]] ksj::base::Status fail_closed_unlocked(ksj::base::Status cause);
  [[nodiscard]] ksj::base::Status fail_unlocked();
  [[nodiscard]] ksj::base::Status finalize_failed_if_quiescent_unlocked();
  [[nodiscard]] ksj::base::Status discard_failed_dispatch_unlocked(DispatchPermit& permit);
  [[nodiscard]] ksj::base::Status discard_failed_publish_unlocked(PublishLease& lease);
  [[nodiscard]] ksj::base::Status release_credit_pool_unlocked();
  [[nodiscard]] ksj::base::Status end_of_input_unlocked();
  [[nodiscard]] ksj::base::Status abort_unlocked();
  [[nodiscard]] OrdinalRecord read_ordinal(ksj::recon::Quantity ordinal) const noexcept;
  void write_ordinal(ksj::recon::Quantity ordinal, OrdinalRecord record) noexcept;
  [[nodiscard]] PhysicalSlotRecord read_slot(ksj::recon::Quantity slot_id) const noexcept;
  void write_slot(ksj::recon::Quantity slot_id, PhysicalSlotRecord record) noexcept;
  void initialize_storage_unlocked() noexcept;
  void move_from_unlocked(FixedReorderBuffer& other) noexcept;

  std::optional<ksj::recon::ReorderPlan> owned_plan_;
  const ksj::recon::ReorderPlan* plan_{nullptr};
  // Stored by value so ingress identity checks remain valid after caller
  // artifacts have been destroyed. They are control-plane values copied only
  // during create(), never consulted through caller references on the hot
  // path.
  std::string execution_plan_digest_;
  std::string verification_record_digest_;
  DenseCartesianOrdinalMapper mapper_{nullptr, {}, 0U};
  // Keep the ledger before the reservation so the RAII token releases while
  // its shared ledger state is still alive during destruction.
  std::shared_ptr<ResourceVectorLedger> resource_ledger_;
  std::optional<ResourceVectorLedgerReservation> credit_pool_reservation_;
  // Retained through every dispatch/publish capability so bookkeeping and
  // typed handle sidecars cannot overlap another live runtime slab.
  detail::SlabRangeClaim slab_claim_{};
  // `storage_` contains only raw ordinal/physical-slot bookkeeping. Typed
  // sidecars are externally aligned objects and are never overlaid on it.
  ksj::base::byte* storage_{nullptr};
  std::size_t storage_bytes_{0U};
  FixedReorderBufferHandleSidecar* handle_sidecars_{nullptr};
  Quantity handle_sidecar_count_{0U};
  std::uint64_t buffer_identity_{0U};
  std::uint64_t m3_reorder_ingress_identity_{0U};
  mutable std::mutex mutex_;
  ksj::recon::Quantity free_head_{0U};
  ksj::recon::Quantity next_expected_{0U};
  ksj::recon::Quantity completed_ordinals_{0U};
  ksj::recon::Quantity prepared_dispatches_{0U};
  ksj::recon::Quantity in_flight_dispatches_{0U};
  ksj::recon::Quantity publishing_outputs_{0U};
  // At most one ordinal can be the direct head at a time. It is intentionally
  // separate from retained_items_: the latter is exactly the frozen
  // `max_ahead_items` future-output capacity.
  ksj::recon::Quantity direct_head_items_{0U};
  ksj::recon::Quantity retained_items_{0U};
  ksj::recon::Quantity retained_charged_bytes_{0U};
  FixedReorderBufferState state_{FixedReorderBufferState::accepting};
};

} // namespace ksj::recon::runtime
