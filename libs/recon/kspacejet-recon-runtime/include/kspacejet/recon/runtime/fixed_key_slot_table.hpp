#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"
#include "kspacejet/recon/execution_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>

namespace ksj::recon::runtime {

// The KeySlotTable plan charges a fixed 16-byte semantic record for every
// possible dense key and a fixed 16-byte physical record for every reusable
// live slot.  The runtime uses this exact byte layout through memcpy-based
// accessors, never by treating caller-owned byte storage as an array of C++
// objects.  Consequently arbitrary byte alignment is safe; alignment is
// nevertheless exposed and checked explicitly so callers have a stable
// storage contract.
inline constexpr std::size_t kFixedKeySlotTableStorageAlignment = alignof(ksj::base::byte);

[[nodiscard]] constexpr std::size_t fixed_key_slot_table_storage_alignment() noexcept {
  return kFixedKeySlotTableStorageAlignment;
}

// Returns the exact metadata byte count charged by `plan`.  It rejects a
// hand-constructed plan whose accounting fields do not match the frozen
// dense-v1 formula, and rejects plans that cannot be represented by a host
// ByteSpan on this platform.
[[nodiscard]] ksj::base::Result<std::size_t> required_storage_bytes(const ksj::recon::KeySlotTablePlan& plan);

// A token identifies one currently live physical slot generation in exactly
// one process-local table. Its raw (table identity, slot_id, generation)
// triple is intentionally opaque outside the runtime; callers can only
// retain, compare, and present it back to its originating table.
class KeySlotToken final {
public:
  constexpr KeySlotToken() noexcept = default;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return table_identity_ != 0U && slot_id_ != kInvalidSlotId && generation_ != 0U;
  }

  friend constexpr bool operator==(const KeySlotToken&, const KeySlotToken&) noexcept = default;

private:
  friend class FixedKeySlotTable;

  static constexpr ksj::recon::Quantity kInvalidSlotId = std::numeric_limits<ksj::recon::Quantity>::max();

  constexpr KeySlotToken(const std::uint64_t table_identity, const ksj::recon::Quantity slot_id,
                         const std::uint64_t generation) noexcept
      : table_identity_(table_identity), slot_id_(slot_id), generation_(generation) {}

  std::uint64_t table_identity_{0U};
  ksj::recon::Quantity slot_id_{kInvalidSlotId};
  std::uint64_t generation_{0U};
};

struct FixedKeySlotTableSnapshot {
  ksj::recon::Quantity key_domain_bound{0U};
  ksj::recon::Quantity slot_count{0U};
  ksj::recon::Quantity ever_bound_keys{0U};
  ksj::recon::Quantity live_keys{0U};
  ksj::recon::Quantity completed_tombstones{0U};
  ksj::recon::Quantity free_slots{0U};
  std::size_t storage_bytes{0U};
  bool new_keys_closed{false};
  bool aborted{false};
};

// Dense, bounded, scan-local KeySlot table for one frozen KeySlotTablePlan.
//
// `plan` and the caller-owned `storage` must outlive the table. create()
// initializes only the pre-reserved byte slab; every later operation uses
// fixed-size stack records and makes no dynamic allocation. The mutex is the
// single linearization point for all stateful operations. A table must not be
// moved after it has been made visible to concurrent callers.
class FixedKeySlotTable final {
public:
  [[nodiscard]] static ksj::base::Result<FixedKeySlotTable> create(const ksj::recon::KeySlotTablePlan& plan,
                                                                   ksj::base::ByteSpan storage);

  FixedKeySlotTable(const FixedKeySlotTable&) = delete;
  FixedKeySlotTable& operator=(const FixedKeySlotTable&) = delete;
  FixedKeySlotTable(FixedKeySlotTable&& other) noexcept;
  FixedKeySlotTable& operator=(FixedKeySlotTable&& other) noexcept;
  ~FixedKeySlotTable() = default;

  // Returns the frozen dense mixed-radix semantic index for a complete key.
  // This pure mapping is available for diagnostics and tests; bind_or_find()
  // applies the same checked mapping under the table linearization point.
  [[nodiscard]] ksj::base::Result<ksj::recon::Quantity> dense_index(std::span<const ksj::recon::Quantity> key) const;

  // Finds an existing active key or atomically binds a never-before-seen key
  // to one free physical slot. A completed semantic key is a durable
  // tombstone, so any late event for it fails even after its slot is reused.
  [[nodiscard]] ksj::base::Result<KeySlotToken> bind_or_find(std::span<const ksj::recon::Quantity> key);

  // Checks that token is still the active generation for this table. This is
  // the entry point used by later KeyShard/Provider code before operating on
  // key-local state; a stale, sealed, or evicted token is rejected. Tokens
  // are table-local values and must not be presented to another table.
  [[nodiscard]] ksj::base::Status validate_active(KeySlotToken token) const;

  // A completed key stops accepting events immediately, but keeps its physical
  // slot until evict_completed() performs the completed-only release.
  [[nodiscard]] ksj::base::Status seal_completed(KeySlotToken token);
  [[nodiscard]] ksj::base::Status evict_completed(KeySlotToken token);

  // EndOfInput closes only first binding. Existing active keys remain
  // findable so they can drain and seal normally.
  [[nodiscard]] ksj::base::Status close_new_keys();

  // Cancellation/failure is terminal for this table. It deliberately does
  // not recycle active slots: the enclosing scan owns teardown, and all later
  // token/key operations are rejected rather than risking post-abort reuse.
  [[nodiscard]] ksj::base::Status abort();

  [[nodiscard]] FixedKeySlotTableSnapshot snapshot() const;

private:
  enum class SemanticState : std::uint8_t {
    never_bound = 0U,
    active = 1U,
    completed = 2U,
  };

  struct SemanticRecord {
    SemanticState state{SemanticState::never_bound};
    ksj::recon::Quantity slot_id{0U};
    std::uint64_t generation{0U};
  };

  struct PhysicalRecord {
    bool free{true};
    ksj::recon::Quantity next_free_or_owner{std::numeric_limits<ksj::recon::Quantity>::max()};
    std::uint64_t generation{0U};
  };

  FixedKeySlotTable(const ksj::recon::KeySlotTablePlan* plan, ksj::base::byte* storage, std::size_t storage_bytes,
                    std::uint64_t table_identity) noexcept;

  [[nodiscard]] static ksj::base::Status validate_plan(const ksj::recon::KeySlotTablePlan& plan);
  [[nodiscard]] ksj::base::Result<ksj::recon::Quantity>
  dense_index_unlocked(std::span<const ksj::recon::Quantity> key) const;
  [[nodiscard]] ksj::base::Status validate_active_unlocked(KeySlotToken token) const;

  [[nodiscard]] SemanticRecord read_semantic(ksj::recon::Quantity semantic_index) const noexcept;
  void write_semantic(ksj::recon::Quantity semantic_index, SemanticRecord record) noexcept;
  [[nodiscard]] PhysicalRecord read_physical(ksj::recon::Quantity slot_id) const noexcept;
  void write_physical(ksj::recon::Quantity slot_id, PhysicalRecord record) noexcept;
  void initialize_storage_unlocked() noexcept;
  void move_from_unlocked(FixedKeySlotTable& other) noexcept;

  const ksj::recon::KeySlotTablePlan* plan_{nullptr};
  ksj::base::byte* storage_{nullptr};
  std::size_t storage_bytes_{0U};
  std::uint64_t table_identity_{0U};
  mutable std::mutex mutex_;
  ksj::recon::Quantity free_head_{std::numeric_limits<ksj::recon::Quantity>::max()};
  ksj::recon::Quantity ever_bound_keys_{0U};
  ksj::recon::Quantity live_keys_{0U};
  ksj::recon::Quantity completed_tombstones_{0U};
  bool new_keys_closed_{false};
  bool aborted_{false};
};

} // namespace ksj::recon::runtime
