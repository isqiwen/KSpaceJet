#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace ksj::recon::runtime {

class FrameDispatch;
class HostFrameAssembler;
class M3ReorderIngress;

namespace detail {
struct HostFrameAssemblerState;
} // namespace detail

// The M3 ReorderPlan charges a 16-byte durable record for every ordinal in
// its closed Cartesian domain and a 16-byte physical record for every
// simultaneously retained output. FixedReorderBuffer uses exactly that
// caller-owned byte layout through memcpy accessors. It therefore does not
// materialize typed objects in the slab, and byte alignment is sufficient.
inline constexpr std::size_t kFixedReorderBufferStorageAlignment = alignof(ksj::base::byte);

[[nodiscard]] constexpr std::size_t fixed_reorder_buffer_storage_alignment() noexcept {
  return kFixedReorderBufferStorageAlignment;
}

// Validates the frozen dense-v1 accounting identity and returns the precise
// number of metadata bytes the caller must provide. A raw plan must outlive a
// resulting mapper; FixedReorderBuffer copies its selected verified plan, but
// caller storage must still outlive that buffer.
[[nodiscard]] ksj::base::Result<std::size_t> required_storage_bytes(const ksj::recon::ReorderPlan& plan);

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

// The buffer receives and returns only opaque IDs. It neither dereferences a
// payload nor establishes BufferHandle ownership; those boundaries belong to
// the later BufferHandle/edge integration.
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

// A move-only reservation of one frozen M3 ahead item, byte charge, and
// descriptor credit. The credits are acquired before dispatch. commit()
// marks the Provider callback as started; complete() is intentionally owned
// by FixedReorderBuffer because it must bind one opaque output to this exact
// completed-frame ordinal. Destroying an uncommitted permit rolls it back.
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

  [[nodiscard]] ksj::base::Status acknowledge_published();

private:
  friend class FixedReorderBuffer;

  PublishLease(FixedReorderBuffer* owner, const std::uint64_t buffer_identity, DispatchPermit permit,
               FixedReorderOutput output) noexcept
      : owner_(owner), buffer_identity_(buffer_identity), permit_(std::move(permit)), output_(output) {}

  void release_noexcept() noexcept;
  void disarm() noexcept;

  // The embedded permit retains the same sealed plan identity, so a lease
  // cannot be acknowledged against another node/buffer/output domain.
  FixedReorderBuffer* owner_{nullptr};
  std::uint64_t buffer_identity_{0U};
  DispatchPermit permit_{};
  FixedReorderOutput output_{};
};

// Creation reserves the complete, immutable ReorderPlan pool in this shared
// ledger: metadata plus every declared ahead byte and descriptor credit. It
// is deliberately a conservative scan-local precommitted pool. Later M3
// BufferHandle/edge work may transfer credits between stages; this primitive
// does not claim that cross-stage reuse exists yet.
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

  FixedReorderBuffer(const FixedReorderBuffer&) = delete;
  FixedReorderBuffer& operator=(const FixedReorderBuffer&) = delete;
  FixedReorderBuffer(FixedReorderBuffer&& other) noexcept;
  FixedReorderBuffer& operator=(FixedReorderBuffer&& other) noexcept;
  ~FixedReorderBuffer() = default;

  // Input closure is a control fence, not a data item. It first blocks new
  // prepares, then returns unavailable while prepared/in-flight callbacks
  // remain. Only after they quiesce does it apply M3's fail-only gap rule.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Freezes every ordinary output path. An abort with outstanding permits or
  // leases enters failed_draining and retains the scan-local precommitted
  // ledger pool until those owners explicitly settle or destruct; it never
  // re-admits held credits while an opaque output may still be externally
  // visible. A quiescent abort transitions directly to failed and releases
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
                     ksj::base::byte* storage, std::size_t storage_bytes, std::uint64_t buffer_identity) noexcept;

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

  // The dispatch retains a completed permit and calls this only for the
  // current next_expected ordinal. A non-next permit returns Unavailable
  // without consuming it; a successful call moves it into a PublishLease.
  [[nodiscard]] ksj::base::Result<PublishLease> try_acquire_publish(DispatchPermit& completed_permit);

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
  ksj::base::byte* storage_{nullptr};
  std::size_t storage_bytes_{0U};
  std::uint64_t buffer_identity_{0U};
  std::uint64_t m3_reorder_ingress_identity_{0U};
  mutable std::mutex mutex_;
  ksj::recon::Quantity free_head_{0U};
  ksj::recon::Quantity next_expected_{0U};
  ksj::recon::Quantity completed_ordinals_{0U};
  ksj::recon::Quantity prepared_dispatches_{0U};
  ksj::recon::Quantity in_flight_dispatches_{0U};
  ksj::recon::Quantity publishing_outputs_{0U};
  ksj::recon::Quantity retained_items_{0U};
  ksj::recon::Quantity retained_charged_bytes_{0U};
  FixedReorderBufferState state_{FixedReorderBufferState::accepting};
};

} // namespace ksj::recon::runtime
