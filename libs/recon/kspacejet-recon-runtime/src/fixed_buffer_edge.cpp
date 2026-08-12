#include "kspacejet/recon/runtime/fixed_buffer_edge.hpp"
#include "kspacejet/recon/runtime/detail/slab_range_claim.hpp"

#include "kspacejet/recon/execution_plan.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

enum class RingSlotPhase : std::uint8_t {
  free,
  pending,
  queued,
  leased,
};

constexpr Quantity kNoSlot = std::numeric_limits<Quantity>::max();

struct RingSlot {
  RingSlotPhase phase{RingSlotPhase::free};
  std::uint64_t token{0U};
  Quantity logical_bytes{0U};
  std::optional<ImmutableBufferHandle> handle{};
  // `next` is one field with phase-dependent ownership: it links free credit
  // records while phase==free and links the committed FIFO while queued. A
  // pending credit deliberately has no FIFO position, so out-of-order
  // Provider callbacks cannot create a head-of-line gap downstream.
  Quantity next{kNoSlot};
};

static_assert(alignof(RingSlot) <= kFixedBufferEdgeStorageAlignment);
// The artifact records a stable 96-byte sidecar accounting unit for every
// M3.7 FIFO item.  Keep the concrete caller-slab record within that frozen
// unit; a larger implementation must not silently instantiate an
// undercharged DataEdgePlan.
static_assert(sizeof(RingSlot) <= ksj::recon::kM37DataEdgeControlChargedBytesPerItem);

[[nodiscard]] bool checked_add(const Quantity lhs, const Quantity rhs, Quantity& result) noexcept {
  if (rhs > std::numeric_limits<Quantity>::max() - lhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

[[nodiscard]] ksj::base::Result<std::size_t> required_control_bytes(const Quantity max_items) {
  if (max_items == 0U) {
    return ksj::base::Status::InvalidArgument("FixedBufferEdge max_items must be greater than zero");
  }
  if (max_items > std::numeric_limits<std::size_t>::max() / sizeof(RingSlot)) {
    return ksj::base::Status::InvalidArgument("FixedBufferEdge control ring exceeds this host's ByteSpan size");
  }
  return static_cast<std::size_t>(max_items) * sizeof(RingSlot);
}

[[nodiscard]] bool is_aligned(const ksj::base::ByteSpan storage) noexcept {
  return storage.data() != nullptr &&
         reinterpret_cast<std::uintptr_t>(storage.data()) % kFixedBufferEdgeStorageAlignment == 0U;
}

} // namespace

namespace detail {

struct FixedBufferEdgeState final : std::enable_shared_from_this<FixedBufferEdgeState> {
  struct ReservationToken {
    Quantity slot_index{0U};
    std::uint64_t token{0U};
  };

  struct AcquiredItem {
    FixedBufferEdgePollKind kind{FixedBufferEdgePollKind::empty};
    Quantity slot_index{0U};
    std::uint64_t token{0U};
    ImmutableBufferHandle handle{};
  };

  FixedBufferEdgeState(std::shared_ptr<ResourceVectorLedger> occupancy_ledger_value,
                       std::optional<ResourceVectorLedgerReservation> occupancy_credit_value,
                       SlabRangeClaim slab_claim_value, const std::uint64_t source_pool_identity_value,
                       TypeDescriptor source_type_descriptor_value, const Quantity max_items_value,
                       const Quantity max_logical_bytes_value, const ksj::base::ByteSpan control_storage_value) noexcept
      : occupancy_ledger(std::move(occupancy_ledger_value)), occupancy_credit(std::move(occupancy_credit_value)),
        slab_claim(std::move(slab_claim_value)), source_pool_identity(source_pool_identity_value),
        source_type_descriptor(std::move(source_type_descriptor_value)), max_items(max_items_value),
        max_logical_bytes(max_logical_bytes_value), control_storage(control_storage_value) {}

  ~FixedBufferEdgeState() { destroy_slots_noexcept(); }

  void initialize() noexcept {
    auto* const slots = slots_begin();
    for (Quantity index = 0U; index < max_items; ++index) {
      std::construct_at(slots + static_cast<std::size_t>(index));
      auto& slot = slots[static_cast<std::size_t>(index)];
      slot.phase = RingSlotPhase::free;
      slot.token = 0U;
      slot.logical_bytes = 0U;
      slot.next = index + 1U == max_items ? kNoSlot : index + 1U;
    }
    free_head = 0U;
    queue_head = kNoSlot;
    queue_tail = kNoSlot;
    slots_constructed = true;
  }

  [[nodiscard]] ksj::base::Status commit_occupancy_credit() {
    if (!occupancy_credit.has_value()) {
      return ksj::base::Status::Ok();
    }
    return occupancy_credit->commit();
  }

  [[nodiscard]] ksj::base::Result<ReservationToken> try_reserve(const Quantity logical_bytes) {
    std::lock_guard lock(mutex);
    const auto accepting_status = require_accepting_locked();
    if (!accepting_status.ok()) {
      return accepting_status;
    }
    if (occupied_items > max_items || producer_reservations + queued_items + consumer_leases != occupied_items) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge credit accounting is inconsistent"));
    }
    if (occupied_items == max_items || free_head == kNoSlot) {
      if (occupied_items != max_items || free_head != kNoSlot) {
        return fail_closed_locked(
          ksj::base::Status::InternalError("FixedBufferEdge free-credit accounting is inconsistent"));
      }
      return ksj::base::Status::Unavailable("FixedBufferEdge item capacity is exhausted");
    }
    if (free_head >= max_items) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge free-credit head is out of range"));
    }
    if (occupied_logical_bytes > max_logical_bytes) {
      return fail_closed_locked(
        ksj::base::Status::InternalError("FixedBufferEdge logical-byte accounting exceeds its fixed capacity"));
    }
    if (logical_bytes > max_logical_bytes - occupied_logical_bytes) {
      return ksj::base::Status::Unavailable("FixedBufferEdge logical-byte capacity is exhausted");
    }
    if (next_token == 0U || next_token == std::numeric_limits<std::uint64_t>::max()) {
      return fail_closed_locked(ksj::base::Status::Unavailable("FixedBufferEdge token space is exhausted"));
    }

    const auto slot_index = free_head;
    auto& slot = slot_at(slot_index);
    if (slot.phase != RingSlotPhase::free || slot.handle.has_value()) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge free-credit record is not free"));
    }
    const auto token = next_token++;
    free_head = slot.next;
    slot.phase = RingSlotPhase::pending;
    slot.token = token;
    slot.logical_bytes = logical_bytes;
    slot.next = kNoSlot;
    ++producer_reservations;
    ++occupied_items;
    occupied_logical_bytes += logical_bytes;
    return ReservationToken{.slot_index = slot_index, .token = token};
  }

  [[nodiscard]] ksj::base::Status commit_from(const Quantity slot_index, const std::uint64_t token,
                                              const Quantity declared_logical_bytes, ImmutableBufferHandle& source) {
    const auto source_status = validate_source_handle(source, declared_logical_bytes);
    if (!source_status.ok()) {
      // The caller still owns `source` on validation failure. Preserve this
      // one pending credit so its explicit rollback (or RAII destructor) can
      // settle cleanly; all other edge-owned work is discarded as the edge
      // enters FailedDraining.
      return fail_and_discard_preserving_pending(source_status, slot_index, token);
    }

    std::lock_guard lock(mutex);
    if (!matches_slot_locked(slot_index, token, RingSlotPhase::pending)) {
      if (lifecycle == FixedBufferEdgeLifecycle::failed_draining || lifecycle == FixedBufferEdgeLifecycle::failed) {
        return ksj::base::Status::StateError("FixedBufferEdge producer reservation cannot commit after failure");
      }
      return fail_closed_locked(
        ksj::base::Status::StateError("FixedBufferEdge producer reservation is stale or already settled"));
    }
    if (lifecycle != FixedBufferEdgeLifecycle::accepting && lifecycle != FixedBufferEdgeLifecycle::close_pending) {
      return fail_closed_locked(
        ksj::base::Status::StateError("FixedBufferEdge cannot commit after terminal completion"));
    }
    auto& slot = slot_at(slot_index);
    if (producer_reservations == 0U || occupied_items == 0U || slot.logical_bytes != declared_logical_bytes ||
        slot.handle.has_value()) {
      return fail_closed_locked(
        ksj::base::Status::InternalError("FixedBufferEdge producer accounting is inconsistent"));
    }
    if ((queue_head == kNoSlot) != (queue_tail == kNoSlot)) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge FIFO endpoints disagree"));
    }
    if (queue_tail != kNoSlot) {
      if (queue_tail >= max_items) {
        return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge FIFO tail is out of range"));
      }
      auto& previous_tail = slot_at(queue_tail);
      if (previous_tail.phase != RingSlotPhase::queued || !previous_tail.handle.has_value() ||
          previous_tail.next != kNoSlot) {
        return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge FIFO tail record is invalid"));
      }
      previous_tail.next = slot_index;
    } else if (queued_items != 0U) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge FIFO is missing a tail record"));
    } else {
      queue_head = slot_index;
    }
    slot.handle.emplace(std::move(source));
    slot.phase = RingSlotPhase::queued;
    slot.next = kNoSlot;
    queue_tail = slot_index;
    --producer_reservations;
    ++queued_items;
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] ksj::base::Status rollback(const Quantity slot_index, const std::uint64_t token) {
    std::lock_guard lock(mutex);
    if (!matches_slot_locked(slot_index, token, RingSlotPhase::pending)) {
      if (lifecycle == FixedBufferEdgeLifecycle::failed_draining || lifecycle == FixedBufferEdgeLifecycle::failed) {
        return ksj::base::Status::StateError("FixedBufferEdge producer reservation is not pending");
      }
      return fail_closed_locked(
        ksj::base::Status::StateError("FixedBufferEdge producer reservation is stale or already settled"));
    }
    if (producer_reservations == 0U || occupied_items == 0U) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge reservation ring is inconsistent"));
    }
    const auto logical_bytes = slot_at(slot_index).logical_bytes;
    if (occupied_logical_bytes < logical_bytes) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge logical-byte accounting underflow"));
    }
    recycle_slot_locked(slot_index);
    --producer_reservations;
    --occupied_items;
    occupied_logical_bytes -= logical_bytes;
    return finalize_terminal_locked();
  }

  [[nodiscard]] AcquiredItem try_acquire() {
    std::lock_guard lock(mutex);
    if (lifecycle == FixedBufferEdgeLifecycle::failed_draining || lifecycle == FixedBufferEdgeLifecycle::failed) {
      return {.kind = FixedBufferEdgePollKind::failed};
    }
    if (consumer_leases != 0U) {
      return {.kind = FixedBufferEdgePollKind::empty};
    }
    if (queued_items == 0U) {
      const auto finalized = finalize_terminal_locked();
      if (!finalized.ok() || lifecycle == FixedBufferEdgeLifecycle::failed_draining ||
          lifecycle == FixedBufferEdgeLifecycle::failed) {
        return {.kind = FixedBufferEdgePollKind::failed};
      }
      // Detached pending credits have pre-admitted capacity but no ordered
      // handle yet; they are intentionally invisible to the FIFO sink.
      return {.kind = lifecycle == FixedBufferEdgeLifecycle::completed ? FixedBufferEdgePollKind::completed
                                                                       : FixedBufferEdgePollKind::empty};
    }
    if (queue_head == kNoSlot || queue_head >= max_items) {
      static_cast<void>(fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge head is out of range")));
      return {.kind = FixedBufferEdgePollKind::failed};
    }
    const auto slot_index = queue_head;
    auto& slot = slot_at(slot_index);
    if (slot.phase != RingSlotPhase::queued || !slot.handle.has_value() || queued_items == 0U) {
      static_cast<void>(
        fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge FIFO head is not a queued item")));
      return {.kind = FixedBufferEdgePollKind::failed};
    }
    const auto token = slot.token;
    const auto next = slot.next;
    if (next != kNoSlot && next >= max_items) {
      static_cast<void>(
        fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge FIFO next record is out of range")));
      return {.kind = FixedBufferEdgePollKind::failed};
    }
    auto handle = std::move(*slot.handle);
    slot.handle.reset();
    slot.phase = RingSlotPhase::leased;
    slot.next = kNoSlot;
    queue_head = next;
    if (queue_head == kNoSlot) {
      queue_tail = kNoSlot;
    }
    --queued_items;
    ++consumer_leases;
    return {
      .kind = FixedBufferEdgePollKind::item, .slot_index = slot_index, .token = token, .handle = std::move(handle)};
  }

  [[nodiscard]] ksj::base::Status acknowledge(const Quantity slot_index, const std::uint64_t token) {
    std::lock_guard lock(mutex);
    if (!matches_slot_locked(slot_index, token, RingSlotPhase::leased)) {
      return fail_closed_locked(
        ksj::base::Status::StateError("FixedBufferEdge consumer lease is stale or already settled"));
    }
    if (consumer_leases != 1U || occupied_items == 0U) {
      return fail_closed_locked(
        ksj::base::Status::InternalError("FixedBufferEdge consumer accounting is inconsistent"));
    }
    const auto logical_bytes = slot_at(slot_index).logical_bytes;
    if (occupied_logical_bytes < logical_bytes) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferEdge logical-byte accounting underflow"));
    }
    recycle_slot_locked(slot_index);
    --consumer_leases;
    --occupied_items;
    occupied_logical_bytes -= logical_bytes;
    return finalize_terminal_locked();
  }

  void abandon_consumer_noexcept(const Quantity slot_index, const std::uint64_t token) noexcept {
    try {
      std::lock_guard lock(mutex);
      if (!matches_slot_locked(slot_index, token, RingSlotPhase::leased)) {
        emergency_fail_locked();
        return;
      }
      static_cast<void>(
        fail_closed_locked(ksj::base::Status::StateError("FixedBufferEdge consumer lease was dropped")));
      if (consumer_leases != 1U || occupied_items == 0U) {
        emergency_fail_locked();
        return;
      }
      const auto logical_bytes = slot_at(slot_index).logical_bytes;
      if (occupied_logical_bytes < logical_bytes) {
        emergency_fail_locked();
        return;
      }
      recycle_slot_locked(slot_index);
      --consumer_leases;
      --occupied_items;
      occupied_logical_bytes -= logical_bytes;
      static_cast<void>(finalize_terminal_locked());
    } catch (...) {
      // A dropped lease cannot surface a recovery path. Its immutable handle
      // will still be released by the lease destructor.
    }
  }

  void rollback_noexcept(const Quantity slot_index, const std::uint64_t token) noexcept {
    try {
      static_cast<void>(rollback(slot_index, token));
    } catch (...) {
      try {
        std::lock_guard lock(mutex);
        emergency_fail_locked();
      } catch (...) {
        // Do not terminate from a move-only reservation destructor.
      }
    }
  }

  [[nodiscard]] ksj::base::Status end_of_input() {
    std::lock_guard lock(mutex);
    if (lifecycle != FixedBufferEdgeLifecycle::accepting) {
      return ksj::base::Status::StateError("FixedBufferEdge EndOfInput was already applied or edge failed");
    }
    lifecycle = FixedBufferEdgeLifecycle::close_pending;
    return finalize_terminal_locked();
  }

  [[nodiscard]] ksj::base::Status abort() {
    std::lock_guard lock(mutex);
    if (lifecycle == FixedBufferEdgeLifecycle::completed || lifecycle == FixedBufferEdgeLifecycle::failed) {
      return ksj::base::Status::StateError("FixedBufferEdge is already terminal");
    }
    lifecycle = FixedBufferEdgeLifecycle::failed_draining;
    last_error = ksj::base::Status::StateError("FixedBufferEdge was aborted");
    discard_queued_locked();
    discard_pending_locked();
    return finalize_terminal_locked();
  }

  void close_owner_noexcept() noexcept {
    try {
      std::lock_guard lock(mutex);
      if (lifecycle == FixedBufferEdgeLifecycle::accepting || lifecycle == FixedBufferEdgeLifecycle::close_pending) {
        static_cast<void>(fail_closed_locked(ksj::base::Status::StateError("FixedBufferEdge owner was destroyed")));
      }
    } catch (...) {
      // The shared state remains held by any live capabilities. There is no
      // safe ordinary-data path after owner destruction.
    }
  }

  [[nodiscard]] FixedBufferEdgeSnapshot snapshot() const {
    std::lock_guard lock(mutex);
    return {.lifecycle = lifecycle,
            .max_items = max_items,
            .max_logical_bytes = max_logical_bytes,
            .reserved_items = producer_reservations,
            .queued_items = queued_items,
            .leased_items = consumer_leases,
            .occupied_items = occupied_items,
            .occupied_logical_bytes = occupied_logical_bytes,
            .free_slots = max_items - occupied_items,
            .control_storage_bytes = control_storage.size(),
            .occupancy_credit_enabled = occupancy_credit.has_value(),
            .occupancy_credit_committed = occupancy_credit.has_value() && occupancy_credit->committed(),
            .last_error = last_error};
  }

  [[nodiscard]] ksj::base::Status fail_and_discard_preserving_pending(ksj::base::Status cause,
                                                                      const Quantity pending_slot,
                                                                      const std::uint64_t pending_token) {
    std::lock_guard lock(mutex);
    const auto preserve =
      matches_slot_locked(pending_slot, pending_token, RingSlotPhase::pending) ? pending_slot : kNoSlot;
    return fail_closed_locked(std::move(cause), preserve);
  }

private:
  [[nodiscard]] RingSlot* slots_begin() const noexcept { return reinterpret_cast<RingSlot*>(control_storage.data()); }

  [[nodiscard]] RingSlot& slot_at(const Quantity index) const noexcept {
    return slots_begin()[static_cast<std::size_t>(index)];
  }

  [[nodiscard]] bool matches_slot_locked(const Quantity slot_index, const std::uint64_t token,
                                         const RingSlotPhase expected_phase) const noexcept {
    return slot_index < max_items && token != 0U && slot_at(slot_index).phase == expected_phase &&
           slot_at(slot_index).token == token;
  }

  [[nodiscard]] ksj::base::Status require_accepting_locked() const {
    if (lifecycle == FixedBufferEdgeLifecycle::accepting) {
      if (!occupancy_credit.has_value() || occupancy_credit->committed()) {
        return ksj::base::Status::Ok();
      }
      return ksj::base::Status::StateError("FixedBufferEdge occupancy credit is not committed");
    }
    if (lifecycle == FixedBufferEdgeLifecycle::failed_draining || lifecycle == FixedBufferEdgeLifecycle::failed) {
      return failure_status_locked();
    }
    return ksj::base::Status::StateError("FixedBufferEdge no longer accepts producer reservations");
  }

  [[nodiscard]] ksj::base::Status validate_source_handle(const ImmutableBufferHandle& source,
                                                         const Quantity declared_logical_bytes) const {
    if (!source.valid() || source.pool_identity() != source_pool_identity || source.type_descriptor() == nullptr ||
        !source.type_descriptor()->exactly_matches(source_type_descriptor)) {
      return ksj::base::Status::ValidationError(
        "FixedBufferEdge requires an immutable handle from its exact source pool and TypeDescriptor");
    }
    Quantity actual_logical_bytes = 0U;
    if (!checked_add(source.payload_bytes(), source.metadata_bytes(), actual_logical_bytes) ||
        actual_logical_bytes > declared_logical_bytes) {
      return ksj::base::Status::ValidationError("FixedBufferEdge handle exceeds its reserved logical-byte envelope");
    }
    const auto payload = source.payload();
    if (!payload.ok()) {
      return payload.status();
    }
    const auto metadata = source.metadata();
    if (!metadata.ok()) {
      return metadata.status();
    }
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] ksj::base::Status fail_closed_locked(ksj::base::Status cause,
                                                     const Quantity preserved_pending_slot = kNoSlot) {
    if (lifecycle != FixedBufferEdgeLifecycle::failed && lifecycle != FixedBufferEdgeLifecycle::failed_draining) {
      lifecycle = FixedBufferEdgeLifecycle::failed_draining;
      last_error = std::move(cause);
    } else if (last_error.ok()) {
      last_error = std::move(cause);
    }
    discard_queued_locked();
    discard_pending_locked(preserved_pending_slot);
    const auto finalized = finalize_terminal_locked();
    return finalized.ok() ? failure_status_locked() : finalized;
  }

  void emergency_fail_locked() noexcept {
    lifecycle = FixedBufferEdgeLifecycle::failed_draining;
    discard_queued_locked();
    discard_pending_locked();
  }

  void discard_queued_locked() noexcept {
    bool accounting_failure = false;
    for (Quantity index = 0U; index < max_items; ++index) {
      auto& slot = slot_at(index);
      if (slot.phase != RingSlotPhase::queued) {
        continue;
      }
      const auto logical_bytes = slot.logical_bytes;
      unlink_queued_slot_locked(index);
      recycle_slot_locked(index);
      if (queued_items == 0U || occupied_items == 0U || occupied_logical_bytes < logical_bytes) {
        accounting_failure = true;
        continue;
      }
      --queued_items;
      --occupied_items;
      occupied_logical_bytes -= logical_bytes;
    }
    if (accounting_failure) {
      lifecycle = FixedBufferEdgeLifecycle::failed_draining;
      // Preserve an earlier root cause where available. Regardless of broken
      // accounting, every queued handle was released before control returns.
      if (last_error.ok()) {
        try {
          last_error = ksj::base::Status::InternalError("FixedBufferEdge queue accounting underflow during failure");
        } catch (...) {
          // This also runs from noexcept capability cleanup. The failed
          // lifecycle itself remains enough to keep the edge fail-closed.
        }
      }
    }
  }

  void discard_pending_locked(const Quantity preserved_pending_slot = kNoSlot) noexcept {
    bool accounting_failure = false;
    for (Quantity index = 0U; index < max_items; ++index) {
      auto& slot = slot_at(index);
      if (slot.phase != RingSlotPhase::pending || index == preserved_pending_slot) {
        continue;
      }
      const auto logical_bytes = slot.logical_bytes;
      recycle_slot_locked(index);
      if (producer_reservations == 0U || occupied_items == 0U || occupied_logical_bytes < logical_bytes) {
        accounting_failure = true;
        continue;
      }
      --producer_reservations;
      --occupied_items;
      occupied_logical_bytes -= logical_bytes;
    }
    if (accounting_failure) {
      lifecycle = FixedBufferEdgeLifecycle::failed_draining;
      if (last_error.ok()) {
        try {
          last_error =
            ksj::base::Status::InternalError("FixedBufferEdge pending-credit accounting underflow during failure");
        } catch (...) {}
      }
    }
  }

  [[nodiscard]] ksj::base::Status finalize_terminal_locked() {
    if (lifecycle == FixedBufferEdgeLifecycle::close_pending && occupied_items == 0U) {
      lifecycle = FixedBufferEdgeLifecycle::completed;
      return ksj::base::Status::Ok();
    }
    if (lifecycle == FixedBufferEdgeLifecycle::failed_draining && occupied_items == 0U) {
      lifecycle = FixedBufferEdgeLifecycle::failed;
    }
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] ksj::base::Status failure_status_locked() const {
    if (!last_error.ok()) {
      return last_error;
    }
    return ksj::base::Status::StateError("FixedBufferEdge is failed closed");
  }

  void recycle_slot_locked(const Quantity slot_index) noexcept {
    auto& slot = slot_at(slot_index);
    slot.handle.reset();
    slot.phase = RingSlotPhase::free;
    slot.token = 0U;
    slot.logical_bytes = 0U;
    slot.next = free_head;
    free_head = slot_index;
  }

  void unlink_queued_slot_locked(const Quantity slot_index) noexcept {
    if (queue_head == kNoSlot) {
      return;
    }
    if (queue_head == slot_index) {
      queue_head = slot_at(slot_index).next;
      if (queue_tail == slot_index) {
        queue_tail = queue_head;
      }
      return;
    }
    Quantity previous = queue_head;
    while (previous != kNoSlot && previous < max_items) {
      const auto next = slot_at(previous).next;
      if (next == slot_index) {
        slot_at(previous).next = slot_at(slot_index).next;
        if (queue_tail == slot_index) {
          queue_tail = previous;
        }
        return;
      }
      previous = next;
    }
  }

  void destroy_slots_noexcept() noexcept {
    if (!slots_constructed) {
      return;
    }
    try {
      for (Quantity index = 0U; index < max_items; ++index) {
        std::destroy_at(slots_begin() + static_cast<std::size_t>(index));
      }
    } catch (...) {
      // ImmutableBufferHandle destruction is noexcept. This catch preserves
      // the no-throw shared-state teardown contract if that ever changes.
    }
    slots_constructed = false;
  }

  // Keep the ledger before its optional credit, just like FixedBufferPool.
  std::shared_ptr<ResourceVectorLedger> occupancy_ledger;
  std::optional<ResourceVectorLedgerReservation> occupancy_credit;
  // This runtime-image-local exclusive claim must outlive every producer
  // reservation and consumer lease that could still touch control_storage.
  SlabRangeClaim slab_claim;
  const std::uint64_t source_pool_identity;
  const TypeDescriptor source_type_descriptor;
  const Quantity max_items;
  const Quantity max_logical_bytes;
  const ksj::base::ByteSpan control_storage;
  mutable std::mutex mutex;
  Quantity free_head{kNoSlot};
  Quantity queue_head{kNoSlot};
  Quantity queue_tail{kNoSlot};
  Quantity producer_reservations{0U};
  Quantity queued_items{0U};
  Quantity consumer_leases{0U};
  Quantity occupied_items{0U};
  Quantity occupied_logical_bytes{0U};
  std::uint64_t next_token{1U};
  FixedBufferEdgeLifecycle lifecycle{FixedBufferEdgeLifecycle::accepting};
  ksj::base::Status last_error{};
  bool slots_constructed{false};
};

} // namespace detail

ksj::base::Result<std::size_t> fixed_buffer_edge_required_control_storage_bytes(const Quantity max_items) {
  return required_control_bytes(max_items);
}

FixedBufferEdgeProducerReservation::FixedBufferEdgeProducerReservation(
  std::shared_ptr<detail::FixedBufferEdgeState> state, const Quantity slot_index, const std::uint64_t token,
  const Quantity logical_bytes) noexcept
    : state_(std::move(state)), slot_index_(slot_index), token_(token), logical_bytes_(logical_bytes) {}

FixedBufferEdgeProducerReservation::~FixedBufferEdgeProducerReservation() {
  release_noexcept();
}

FixedBufferEdgeProducerReservation::FixedBufferEdgeProducerReservation(
  FixedBufferEdgeProducerReservation&& other) noexcept
    : state_(std::move(other.state_)), slot_index_(std::exchange(other.slot_index_, 0U)),
      token_(std::exchange(other.token_, 0U)), logical_bytes_(std::exchange(other.logical_bytes_, 0U)),
      committed_(std::exchange(other.committed_, false)) {}

FixedBufferEdgeProducerReservation&
FixedBufferEdgeProducerReservation::operator=(FixedBufferEdgeProducerReservation&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    state_ = std::move(other.state_);
    slot_index_ = std::exchange(other.slot_index_, 0U);
    token_ = std::exchange(other.token_, 0U);
    logical_bytes_ = std::exchange(other.logical_bytes_, 0U);
    committed_ = std::exchange(other.committed_, false);
  }
  return *this;
}

bool FixedBufferEdgeProducerReservation::valid() const noexcept {
  return state_ != nullptr && token_ != 0U;
}

ksj::base::Status FixedBufferEdgeProducerReservation::commit_from(ImmutableBufferHandle& source) {
  if (!valid()) {
    return ksj::base::Status::StateError("FixedBufferEdge producer reservation is invalid or moved from");
  }
  const auto committed = state_->commit_from(slot_index_, token_, logical_bytes_, source);
  if (committed.ok()) {
    // Keep a private shared-state reference until destruction so the coupled
    // M3.7 reorder handoff can compensate by aborting this exact edge if the
    // subsequent upstream publish acknowledgement fails. `valid()` is still
    // false because the producer token is consumed; no ordinary caller can
    // commit or roll back this reservation again.
    token_ = 0U;
    logical_bytes_ = 0U;
    committed_ = true;
  }
  return committed;
}

ksj::base::Status FixedBufferEdgeProducerReservation::rollback() {
  if (!valid()) {
    return ksj::base::Status::StateError("FixedBufferEdge producer reservation is invalid or moved from");
  }
  const auto rolled_back = state_->rollback(slot_index_, token_);
  if (rolled_back.ok()) {
    disarm();
  }
  return rolled_back;
}

ksj::base::Status FixedBufferEdgeProducerReservation::abort_committed_edge_for_coupled_handoff() {
  if (state_ == nullptr || !committed_ || token_ != 0U) {
    return ksj::base::Status::StateError(
      "FixedBufferEdge producer reservation has no committed edge handoff to compensate");
  }
  const auto aborted = state_->abort();
  disarm();
  return aborted;
}

void FixedBufferEdgeProducerReservation::release_noexcept() noexcept {
  if (state_ != nullptr && token_ != 0U) {
    state_->rollback_noexcept(slot_index_, token_);
  }
  disarm();
}

void FixedBufferEdgeProducerReservation::disarm() noexcept {
  state_.reset();
  slot_index_ = 0U;
  token_ = 0U;
  logical_bytes_ = 0U;
  committed_ = false;
}

FixedBufferEdgeConsumerLease::FixedBufferEdgeConsumerLease(std::shared_ptr<detail::FixedBufferEdgeState> state,
                                                           const Quantity slot_index, const std::uint64_t token,
                                                           ImmutableBufferHandle buffer) noexcept
    : state_(std::move(state)), slot_index_(slot_index), token_(token), buffer_(std::move(buffer)) {}

FixedBufferEdgeConsumerLease::~FixedBufferEdgeConsumerLease() {
  release_noexcept();
}

FixedBufferEdgeConsumerLease::FixedBufferEdgeConsumerLease(FixedBufferEdgeConsumerLease&& other) noexcept
    : state_(std::move(other.state_)), slot_index_(std::exchange(other.slot_index_, 0U)),
      token_(std::exchange(other.token_, 0U)), buffer_(std::move(other.buffer_)) {}

FixedBufferEdgeConsumerLease& FixedBufferEdgeConsumerLease::operator=(FixedBufferEdgeConsumerLease&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    state_ = std::move(other.state_);
    slot_index_ = std::exchange(other.slot_index_, 0U);
    token_ = std::exchange(other.token_, 0U);
    buffer_ = std::move(other.buffer_);
  }
  return *this;
}

bool FixedBufferEdgeConsumerLease::valid() const noexcept {
  return state_ != nullptr && token_ != 0U && buffer_.valid();
}

ksj::base::Status FixedBufferEdgeConsumerLease::acknowledge_consumed() {
  if (!valid()) {
    return ksj::base::Status::StateError("FixedBufferEdge consumer lease is invalid or moved from");
  }
  const auto acknowledged = state_->acknowledge(slot_index_, token_);
  if (acknowledged.ok()) {
    buffer_ = ImmutableBufferHandle{};
    disarm();
  }
  return acknowledged;
}

void FixedBufferEdgeConsumerLease::release_noexcept() noexcept {
  if (state_ != nullptr && token_ != 0U) {
    state_->abandon_consumer_noexcept(slot_index_, token_);
  }
  disarm();
}

void FixedBufferEdgeConsumerLease::disarm() noexcept {
  state_.reset();
  slot_index_ = 0U;
  token_ = 0U;
}

ksj::base::Result<std::unique_ptr<FixedBufferEdge>> FixedBufferEdge::create(FixedBufferEdgeConfig config,
                                                                            const FixedBufferEdgeStorage storage) {
  if (config.source_pool == nullptr) {
    return ksj::base::Status::InvalidArgument("FixedBufferEdge requires a source FixedBufferPool");
  }
  if (config.max_items == 0U) {
    return ksj::base::Status::InvalidArgument("FixedBufferEdge requires a non-zero item capacity");
  }
  const auto source_pool_identity = config.source_pool->pool_identity();
  const auto* const source_type_descriptor = config.source_pool->type_descriptor();
  if (source_pool_identity == 0U || source_type_descriptor == nullptr) {
    return ksj::base::Status::StateError("FixedBufferEdge source FixedBufferPool is invalid");
  }
  const auto required_storage = required_control_bytes(config.max_items);
  if (!required_storage.ok()) {
    return required_storage.status();
  }
  if (storage.control.size() != required_storage.value() || !is_aligned(storage.control)) {
    return ksj::base::Status::InvalidArgument(
      "FixedBufferEdge control slab must have exact size and max_align_t alignment");
  }
  if (storage.control.size() > std::numeric_limits<Quantity>::max()) {
    return ksj::base::Status::InvalidArgument("FixedBufferEdge control slab exceeds Quantity accounting");
  }
  const auto concrete_control_bytes = static_cast<Quantity>(storage.control.size());
  const auto charged_control_bytes =
    config.charged_control_storage_bytes == 0U ? concrete_control_bytes : config.charged_control_storage_bytes;
  const auto charged_descriptor_count =
    config.charged_descriptor_count == 0U ? config.max_items : config.charged_descriptor_count;
  if (charged_control_bytes < concrete_control_bytes || charged_descriptor_count < config.max_items) {
    return ksj::base::Status::ValidationError(
      "FixedBufferEdge frozen control accounting is smaller than its concrete fixed representation");
  }
  try {
    const std::array<ksj::base::ByteSpan, 1U> slabs{storage.control};
    auto slab_claim = detail::claim_exclusive_slab_ranges(std::span<const ksj::base::ByteSpan>{slabs});
    if (!slab_claim.ok()) {
      return slab_claim.status();
    }

    std::optional<ResourceVectorLedgerReservation> occupancy_credit;
    if (config.occupancy_ledger != nullptr) {
      const auto occupancy_vector = ResourceVector::create({.host_normal_bytes = charged_control_bytes,
                                                            .host_pinned_bytes = 0U,
                                                            .host_hugepage_bytes = 0U,
                                                            .shared_host_bytes = 0U,
                                                            .spool_bytes = 0U,
                                                            .transport_bytes = 0U,
                                                            .descriptor_count = charged_descriptor_count,
                                                            .async_token_count = 0U,
                                                            .cpu_leaf_permits = 0U,
                                                            .backend_gang_permits = 0U,
                                                            .provider_private_permits = 0U,
                                                            .io_slots = 0U,
                                                            .devices = {}},
                                                           "FixedBufferEdge external control-slab occupancy credit");
      if (!occupancy_vector.ok()) {
        return occupancy_vector.status();
      }
      auto reserved_credit = config.occupancy_ledger->try_reserve(occupancy_vector.value());
      if (!reserved_credit.ok()) {
        return reserved_credit.status();
      }
      occupancy_credit.emplace(std::move(reserved_credit).value());
    }

    auto state = std::make_shared<detail::FixedBufferEdgeState>(
      std::move(config.occupancy_ledger), std::move(occupancy_credit), std::move(slab_claim).value(),
      source_pool_identity, *source_type_descriptor, config.max_items, config.max_logical_bytes, storage.control);
    state->initialize();
    const auto committed = state->commit_occupancy_credit();
    if (!committed.ok()) {
      return committed;
    }
    return std::unique_ptr<FixedBufferEdge>(new FixedBufferEdge(std::move(state)));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate FixedBufferEdge control state");
  }
}

FixedBufferEdge::FixedBufferEdge(std::shared_ptr<detail::FixedBufferEdgeState> state) noexcept
    : state_(std::move(state)) {}

FixedBufferEdge::~FixedBufferEdge() {
  if (state_ != nullptr) {
    state_->close_owner_noexcept();
  }
}

ksj::base::Result<FixedBufferEdgeProducerReservation> FixedBufferEdge::try_reserve(const Quantity logical_bytes) {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("FixedBufferEdge is invalid");
  }
  const auto reserved = state_->try_reserve(logical_bytes);
  if (!reserved.ok()) {
    return reserved.status();
  }
  return FixedBufferEdgeProducerReservation{state_, reserved.value().slot_index, reserved.value().token, logical_bytes};
}

FixedBufferEdgePoll FixedBufferEdge::try_acquire() {
  if (state_ == nullptr) {
    return {.kind = FixedBufferEdgePollKind::failed};
  }
  auto acquired = state_->try_acquire();
  if (acquired.kind != FixedBufferEdgePollKind::item) {
    return {.kind = acquired.kind};
  }
  return {.kind = FixedBufferEdgePollKind::item,
          .lease =
            FixedBufferEdgeConsumerLease{state_, acquired.slot_index, acquired.token, std::move(acquired.handle)}};
}

ksj::base::Status FixedBufferEdge::end_of_input() {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("FixedBufferEdge is invalid");
  }
  return state_->end_of_input();
}

ksj::base::Status FixedBufferEdge::abort() {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("FixedBufferEdge is invalid");
  }
  return state_->abort();
}

FixedBufferEdgeSnapshot FixedBufferEdge::snapshot() const {
  return state_ == nullptr ? FixedBufferEdgeSnapshot{} : state_->snapshot();
}

} // namespace ksj::recon::runtime
