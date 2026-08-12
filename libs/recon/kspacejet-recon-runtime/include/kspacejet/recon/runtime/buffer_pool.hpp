#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"
#include "kspacejet/recon/type_descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ksj::recon::runtime {

namespace detail {
struct FixedBufferPoolState;
} // namespace detail

// Every control record is accessed with memcpy, so caller-owned control
// storage needs only byte alignment and never contains a materialised C++
// object.
inline constexpr std::size_t kFixedBufferPoolStorageAlignment = alignof(ksj::base::byte);

[[nodiscard]] constexpr std::size_t fixed_buffer_pool_storage_alignment() noexcept {
  return kFixedBufferPoolStorageAlignment;
}

// Returns the exact control-slab byte count for `slot_count` slots. The
// payload and metadata slab sizes are separately fixed by FixedBufferPoolConfig.
[[nodiscard]] ksj::base::Result<std::size_t> fixed_buffer_pool_required_control_storage_bytes(Quantity slot_count);

// The caller owns physical allocation, upstream admission, and lifetime of
// these caller-asserted host-normal slabs; they must outlive FixedBufferPool
// and every resulting MutableBufferLease or ImmutableBufferHandle. A raw
// ByteSpan establishes neither allocation nor memory-domain provenance. This
// runtime does not allocate payload, metadata, or control storage and does not
// act as a PhysicalMemory ledger. After successful create(), callers must not
// mutate the slabs directly; writable payload is borrowed only through a live
// lease. Each non-empty slab range is exclusive within this ksj_recon_runtime
// image across participating FixedBufferPool/FixedBufferEdge instances;
// create() rejects local or cross-component overlap until all handles/leases
// retaining the prior pool state have settled. The caller remains responsible
// for overlap with raw-slab primitives outside that facility.
struct FixedBufferPoolStorage {
  ksj::base::ByteSpan payload{};
  ksj::base::ByteSpan metadata{};
  ksj::base::ByteSpan control{};
};

// This narrow initial slice accepts one exact immutable-after-publish buffer
// type which permits host_normal. The caller asserts that raw slabs meet that
// domain; the pool cannot establish allocation or domain provenance. A later
// compiled edge may carry a sealed handle, but this pool intentionally has no
// graph, fan-out, retain, transfer, or asynchronous ownership semantics.
struct FixedBufferPoolConfig {
  // Optional runtime occupancy credit for this externally allocated slab set.
  // It does not allocate or identify physical memory. Leave null when an
  // outer runtime already admitted/accounted these same slabs in that ledger;
  // passing that ledger here would double count the external allocation.
  std::shared_ptr<ResourceVectorLedger> occupancy_ledger;
  TypeDescriptor type_descriptor;
  Quantity slot_count{0U};
  Quantity payload_capacity_bytes{0U};
  Quantity metadata_capacity_bytes{0U};
};

struct FixedBufferPoolSnapshot {
  Quantity slot_count{0U};
  Quantity free_slots{0U};
  Quantity writable_slots{0U};
  Quantity sealed_slots{0U};
  Quantity retired_slots{0U};
  Quantity payload_capacity_bytes{0U};
  Quantity metadata_capacity_bytes{0U};
  std::size_t payload_storage_bytes{0U};
  std::size_t metadata_storage_bytes{0U};
  std::size_t control_storage_bytes{0U};
  bool accepting{false};
  bool failed{false};
  bool generation_exhausted{false};
  bool occupancy_credit_enabled{false};
  bool occupancy_credit_committed{false};
  ksj::base::Status last_error{};
};

class ImmutableBufferHandle final {
public:
  ImmutableBufferHandle() = default;
  ~ImmutableBufferHandle();

  ImmutableBufferHandle(const ImmutableBufferHandle&) = delete;
  ImmutableBufferHandle& operator=(const ImmutableBufferHandle&) = delete;
  ImmutableBufferHandle(ImmutableBufferHandle&& other) noexcept;
  ImmutableBufferHandle& operator=(ImmutableBufferHandle&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  // The spans are immutable borrows. They remain valid only while this sole
  // handle remains valid; there is deliberately no retain or fan-out API.
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> payload() const;
  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan> metadata() const;
  [[nodiscard]] const TypeDescriptor* type_descriptor() const noexcept;
  [[nodiscard]] Quantity payload_bytes() const noexcept { return payload_bytes_; }
  [[nodiscard]] Quantity metadata_bytes() const noexcept { return metadata_bytes_; }
  [[nodiscard]] Quantity logical_bytes() const noexcept { return payload_bytes_ + metadata_bytes_; }

  // These are diagnostics/binding facts only. No public operation accepts a
  // caller-supplied token, so they cannot be used to forge ownership.
  [[nodiscard]] std::uint64_t pool_identity() const noexcept { return pool_identity_; }
  [[nodiscard]] Quantity slot_index() const noexcept { return slot_index_; }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
  friend struct detail::FixedBufferPoolState;

  ImmutableBufferHandle(std::shared_ptr<detail::FixedBufferPoolState> state, std::uint64_t pool_identity,
                        Quantity slot_index, std::uint64_t generation, Quantity payload_bytes,
                        Quantity metadata_bytes) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  std::shared_ptr<detail::FixedBufferPoolState> state_{};
  std::uint64_t pool_identity_{0U};
  Quantity slot_index_{0U};
  std::uint64_t generation_{0U};
  Quantity payload_bytes_{0U};
  Quantity metadata_bytes_{0U};
};

class MutableBufferLease final {
public:
  MutableBufferLease() = default;
  ~MutableBufferLease();

  MutableBufferLease(const MutableBufferLease&) = delete;
  MutableBufferLease& operator=(const MutableBufferLease&) = delete;
  MutableBufferLease(MutableBufferLease&& other) noexcept;
  MutableBufferLease& operator=(MutableBufferLease&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

  // Returns the complete preallocated payload slot. This is a non-retainable
  // borrow: it must not be used after this lease is sealed, moved, or dropped.
  // Only the prefix declared by seal() becomes visible through the immutable
  // handle.
  [[nodiscard]] ksj::base::Result<ksj::base::ByteSpan> writable_payload();

  // Copies bounded metadata into the caller-provided metadata slab, verifies
  // exact TypeDescriptor equality, and consumes this lease on success.
  [[nodiscard]] ksj::base::Result<ImmutableBufferHandle>
  seal(const TypeDescriptor& type_descriptor, Quantity payload_bytes, ksj::base::ConstByteSpan metadata);

  [[nodiscard]] std::uint64_t pool_identity() const noexcept { return pool_identity_; }
  [[nodiscard]] Quantity slot_index() const noexcept { return slot_index_; }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
  friend struct detail::FixedBufferPoolState;

  MutableBufferLease(std::shared_ptr<detail::FixedBufferPoolState> state, std::uint64_t pool_identity,
                     Quantity slot_index, std::uint64_t generation) noexcept;

  void release_noexcept() noexcept;
  void disarm() noexcept;

  std::shared_ptr<detail::FixedBufferPoolState> state_{};
  std::uint64_t pool_identity_{0U};
  Quantity slot_index_{0U};
  std::uint64_t generation_{0U};
};

// A scan-owned fixed pool of caller-asserted host-normal buffers. Raw ByteSpan
// does not establish allocation or domain provenance. create() validates exact
// slab dimensions and, when configured, commits one long-lived runtime
// occupancy credit for all three externally allocated slabs plus one descriptor
// credit per slot. That credit is neither allocation provenance nor an
// admission replacement; callers must not double count the same slab set in an
// outer use of the same ledger.
//
// Dropping an unsealed MutableBufferLease recycles its unpublished slot.
// Dropping an ImmutableBufferHandle also recycles its sole-owned sealed slot:
// this pool has no external publish acknowledgement, so a future edge/runtime
// must own fail-closed behaviour after external visibility. Destroying the
// pool stops new acquisitions but permits already-issued handles to settle;
// its optional occupancy credit and slab claim remain held until the last
// shared state is released.
class FixedBufferPool final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<FixedBufferPool>> create(FixedBufferPoolConfig config,
                                                                                  FixedBufferPoolStorage storage);

  FixedBufferPool(const FixedBufferPool&) = delete;
  FixedBufferPool& operator=(const FixedBufferPool&) = delete;
  FixedBufferPool(FixedBufferPool&&) = delete;
  FixedBufferPool& operator=(FixedBufferPool&&) = delete;
  ~FixedBufferPool();

  [[nodiscard]] ksj::base::Result<MutableBufferLease> try_acquire();
  [[nodiscard]] FixedBufferPoolSnapshot snapshot() const;

  // Immutable binding facts for a future bounded edge. They do not grant slot
  // ownership; only a lease or handle returned by this pool can do that.
  [[nodiscard]] std::uint64_t pool_identity() const noexcept;
  [[nodiscard]] const TypeDescriptor* type_descriptor() const noexcept;

private:
  explicit FixedBufferPool(std::shared_ptr<detail::FixedBufferPoolState> state) noexcept;

  std::shared_ptr<detail::FixedBufferPoolState> state_{};
};

} // namespace ksj::recon::runtime
