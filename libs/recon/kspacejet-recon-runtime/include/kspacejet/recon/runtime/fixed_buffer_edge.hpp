#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/runtime/buffer_pool.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace ksj::recon::runtime {

namespace detail {
struct FixedBufferEdgeState;
} // namespace detail

// The edge constructs move-only ImmutableBufferHandle objects in the
// caller-owned control ring. max_align_t makes that placement well-defined
// without exposing the private control-record layout in the public ABI.
inline constexpr std::size_t kFixedBufferEdgeStorageAlignment = alignof(std::max_align_t);

[[nodiscard]] constexpr std::size_t fixed_buffer_edge_storage_alignment() noexcept {
  return kFixedBufferEdgeStorageAlignment;
}

// Returns the exact byte count of the caller-owned control ring. The ring
// contains descriptors and move-only handle storage only; it never owns a
// second payload slab.
[[nodiscard]] ksj::base::Result<std::size_t> fixed_buffer_edge_required_control_storage_bytes(Quantity max_items);

// The caller owns allocation, upstream admission, and lifetime of this raw
// control slab; ByteSpan establishes neither physical-allocation nor
// memory-domain provenance. The edge only materializes fixed descriptors in
// it and, when configured, keeps a runtime occupancy credit for the caller's
// asserted external slab. This control slab must not overlap a participating
// Pool/Edge slab while any capability can still access it.
struct FixedBufferEdgeStorage {
  ksj::base::ByteSpan control{};
};

// This first edge is intentionally narrow: one FIFO producer, one FIFO
// consumer, one already-created source pool, host-normal immutable buffers,
// and no retain, fan-out, transfer, or asynchronous hand-off. The edge copies
// the pool identity and exact TypeDescriptor at create time; it never accepts
// a handle from another pool merely because its type happens to match.
struct FixedBufferEdgeConfig {
  // Optional runtime occupancy credit for this externally allocated control
  // slab. It is not physical-allocation provenance. Leave null when an outer
  // runtime has already admitted/accounted this same slab in that ledger;
  // passing that ledger here as well would double count the external slab.
  std::shared_ptr<ResourceVectorLedger> occupancy_ledger;
  const FixedBufferPool* source_pool{nullptr};
  Quantity max_items{0U};
  // A dataflow credit, not a second physical payload allocation. A producer
  // reserves this declared bound before filling/sealing the source slot and
  // retains it until the consumer acknowledges the item.
  Quantity max_logical_bytes{0U};
};

enum class FixedBufferEdgeLifecycle : std::uint8_t {
  accepting,
  close_pending,
  completed,
  failed_draining,
  failed,
};

struct FixedBufferEdgeSnapshot {
  FixedBufferEdgeLifecycle lifecycle{FixedBufferEdgeLifecycle::accepting};
  Quantity max_items{0U};
  Quantity max_logical_bytes{0U};
  Quantity reserved_items{0U};
  Quantity queued_items{0U};
  Quantity leased_items{0U};
  Quantity occupied_items{0U};
  Quantity occupied_logical_bytes{0U};
  Quantity free_slots{0U};
  std::size_t control_storage_bytes{0U};
  bool occupancy_credit_enabled{false};
  bool occupancy_credit_committed{false};
  ksj::base::Status last_error{};
};

class FixedBufferEdge;

// A move-only item/descriptor/logical-byte reservation. Its destructor rolls
// back only unpublished edge credit; a firing wrapper is responsible for
// treating a missing required output as a contract failure rather than merely
// relying on this rollback.
class FixedBufferEdgeProducerReservation final {
public:
  FixedBufferEdgeProducerReservation() = default;
  ~FixedBufferEdgeProducerReservation();

  FixedBufferEdgeProducerReservation(const FixedBufferEdgeProducerReservation&) = delete;
  FixedBufferEdgeProducerReservation& operator=(const FixedBufferEdgeProducerReservation&) = delete;
  FixedBufferEdgeProducerReservation(FixedBufferEdgeProducerReservation&& other) noexcept;
  FixedBufferEdgeProducerReservation& operator=(FixedBufferEdgeProducerReservation&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] Quantity logical_bytes() const noexcept { return logical_bytes_; }

  // On success ownership moves into the fixed ring and `source` becomes
  // invalid. On any error source remains owned by the caller.
  [[nodiscard]] ksj::base::Status commit_from(ImmutableBufferHandle& source);
  [[nodiscard]] ksj::base::Status rollback();

private:
  friend class FixedBufferEdge;

  FixedBufferEdgeProducerReservation(std::shared_ptr<detail::FixedBufferEdgeState> state, Quantity slot_index,
                                     std::uint64_t token, Quantity logical_bytes) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  std::shared_ptr<detail::FixedBufferEdgeState> state_{};
  Quantity slot_index_{0U};
  std::uint64_t token_{0U};
  Quantity logical_bytes_{0U};
};

// A move-only synchronous consumer capability. It exposes only a const
// borrow of the immutable handle and retains edge credit until acknowledge.
// Abort cannot revoke a read already executing synchronously; it prevents
// new ordinary acquisition, and the active consumer must not publish ordinary
// downstream work after failure before it settles this lease. There is
// deliberately no take()/retain() escape hatch in this slice.
class FixedBufferEdgeConsumerLease final {
public:
  FixedBufferEdgeConsumerLease() = default;
  ~FixedBufferEdgeConsumerLease();

  FixedBufferEdgeConsumerLease(const FixedBufferEdgeConsumerLease&) = delete;
  FixedBufferEdgeConsumerLease& operator=(const FixedBufferEdgeConsumerLease&) = delete;
  FixedBufferEdgeConsumerLease(FixedBufferEdgeConsumerLease&& other) noexcept;
  FixedBufferEdgeConsumerLease& operator=(FixedBufferEdgeConsumerLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] const ImmutableBufferHandle& buffer() const noexcept { return buffer_; }

  // In FailedDraining this remains permitted solely to return the held credit
  // and source-pool slot; it never revives ordinary dataflow.
  [[nodiscard]] ksj::base::Status acknowledge_consumed();

private:
  friend class FixedBufferEdge;

  FixedBufferEdgeConsumerLease(std::shared_ptr<detail::FixedBufferEdgeState> state, Quantity slot_index,
                               std::uint64_t token, ImmutableBufferHandle buffer) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  std::shared_ptr<detail::FixedBufferEdgeState> state_{};
  Quantity slot_index_{0U};
  std::uint64_t token_{0U};
  ImmutableBufferHandle buffer_{};
};

enum class FixedBufferEdgePollKind : std::uint8_t {
  item,
  empty,
  completed,
  failed,
};

// Polling distinguishes a temporarily empty open/draining edge from normal
// EndOfInput completion and terminal failure without inventing a fake data
// item or overloading Status::Unavailable.
struct FixedBufferEdgePoll {
  FixedBufferEdgePollKind kind{FixedBufferEdgePollKind::empty};
  std::optional<FixedBufferEdgeConsumerLease> lease{};
};

// A caller-slab, fixed-ring edge for one immutable FixedBufferPool. Creation
// optionally holds an occupancy credit for its caller-owned descriptor/control
// storage in a shared ledger. That credit is not physical allocation
// provenance and must not duplicate an outer accounting claim. The pool
// independently owns its own external-slab occupancy semantics; edge
// item/logical-byte limits are local credits and must never create a second
// payload charge.
//
// The caller-owned control slab must outlive this edge and every producer
// reservation or consumer lease. The shared edge state keeps outstanding
// capabilities safe across owner destruction, but cannot extend raw slab
// lifetime. Its exclusive slab claim covers only Pool/Edge participants in
// this loaded ksj_recon_runtime image; callers retain responsibility for raw
// external users and independently loaded runtime images.
class FixedBufferEdge final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<FixedBufferEdge>> create(FixedBufferEdgeConfig config,
                                                                                  FixedBufferEdgeStorage storage);

  FixedBufferEdge(const FixedBufferEdge&) = delete;
  FixedBufferEdge& operator=(const FixedBufferEdge&) = delete;
  FixedBufferEdge(FixedBufferEdge&&) = delete;
  FixedBufferEdge& operator=(FixedBufferEdge&&) = delete;
  ~FixedBufferEdge();

  // Reserves one fixed ring record and its declared logical credit. This
  // operation never waits and returns Unavailable on ordinary capacity
  // pressure. Only one producer reservation may be outstanding at a time,
  // preserving FIFO order without a dynamic reservation journal.
  [[nodiscard]] ksj::base::Result<FixedBufferEdgeProducerReservation> try_reserve(Quantity logical_bytes);

  [[nodiscard]] FixedBufferEdgePoll try_acquire();

  // Closes only new reserves. Already-reserved producer work may still commit
  // or roll back; already-queued data must be acknowledged before Completed.
  [[nodiscard]] ksj::base::Status end_of_input();

  // Suppresses ordinary work, destroys edge-owned queued handles, and drains
  // only outstanding producer reservations/consumer leases to Failed.
  [[nodiscard]] ksj::base::Status abort();

  [[nodiscard]] FixedBufferEdgeSnapshot snapshot() const;

private:
  explicit FixedBufferEdge(std::shared_ptr<detail::FixedBufferEdgeState> state) noexcept;

  std::shared_ptr<detail::FixedBufferEdgeState> state_{};
};

} // namespace ksj::recon::runtime
