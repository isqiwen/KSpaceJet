#include "kspacejet/recon/runtime/buffer_pool.hpp"
#include "kspacejet/recon/runtime/detail/slab_range_claim.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

constexpr std::size_t kControlRecordBytes = 5U * sizeof(std::uint64_t);
constexpr std::size_t kGenerationOffset = 0U;
constexpr std::size_t kPayloadBytesOffset = sizeof(std::uint64_t);
constexpr std::size_t kMetadataBytesOffset = 2U * sizeof(std::uint64_t);
constexpr std::size_t kStateOffset = 3U * sizeof(std::uint64_t);
constexpr std::size_t kNextFreeOffset = 4U * sizeof(std::uint64_t);
constexpr Quantity kNoSlot = std::numeric_limits<Quantity>::max();
constexpr std::uint64_t kInitialGeneration = 1U;

enum class SlotState : std::uint64_t {
  free = 0U,
  writable = 1U,
  sealed = 2U,
  retired = 3U,
};

struct ControlRecord {
  std::uint64_t generation{0U};
  Quantity payload_bytes{0U};
  Quantity metadata_bytes{0U};
  SlotState state{SlotState::retired};
  Quantity next_free{kNoSlot};
};

std::atomic<std::uint64_t> g_next_pool_identity{1U};

[[nodiscard]] ksj::base::Result<std::uint64_t> allocate_pool_identity() {
  auto identity = g_next_pool_identity.load(std::memory_order_relaxed);
  for (;;) {
    // Keep the counter saturated rather than letting a pool identity wrap and
    // turn an old generation token into a valid capability for a new pool.
    if (identity == 0U || identity == std::numeric_limits<std::uint64_t>::max()) {
      return ksj::base::Status::Unavailable("FixedBufferPool process-local identity space is exhausted");
    }
    if (g_next_pool_identity.compare_exchange_weak(identity, identity + 1U, std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
      return identity;
    }
  }
}

[[nodiscard]] std::uint64_t read_u64(const ksj::base::byte* const address) noexcept {
  std::uint64_t value = 0U;
  std::memcpy(&value, address, sizeof(value));
  return value;
}

void write_u64(ksj::base::byte* const address, const std::uint64_t value) noexcept {
  std::memcpy(address, &value, sizeof(value));
}

[[nodiscard]] bool has_host_normal_domain(const TypeDescriptor& type_descriptor) noexcept {
  const auto& domains = type_descriptor.allowed_memory_domains();
  return std::find(domains.begin(), domains.end(), TypeMemoryDomain::host_normal) != domains.end();
}

[[nodiscard]] ksj::base::Result<std::size_t>
checked_slot_bytes(const Quantity slot_count, const Quantity bytes_per_slot, const char* const field_name) {
  if (slot_count != 0U && bytes_per_slot > std::numeric_limits<std::size_t>::max() / slot_count) {
    return ksj::base::Status::InvalidArgument(std::string("FixedBufferPool ") + field_name +
                                              " exceeds this host's ByteSpan size");
  }
  return static_cast<std::size_t>(slot_count * bytes_per_slot);
}

[[nodiscard]] ksj::base::Result<std::size_t> checked_add_bytes(const std::size_t lhs, const std::size_t rhs,
                                                               const char* const field_name) {
  if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
    return ksj::base::Status::InvalidArgument(std::string("FixedBufferPool ") + field_name +
                                              " exceeds this host's ByteSpan size");
  }
  return lhs + rhs;
}

[[nodiscard]] ksj::base::Status verify_exact_storage(const ksj::base::ByteSpan storage,
                                                     const std::size_t expected_bytes, const char* const field_name) {
  if (storage.size() != expected_bytes) {
    return ksj::base::Status::InvalidArgument(std::string("FixedBufferPool ") + field_name +
                                              " slab must exactly match its fixed bound");
  }
  if (expected_bytes != 0U && storage.data() == nullptr) {
    return ksj::base::Status::InvalidArgument(std::string("FixedBufferPool ") + field_name + " slab must not be null");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] bool fits_size_t(const Quantity value) noexcept {
  return value <= std::numeric_limits<std::size_t>::max();
}

} // namespace

namespace detail {

struct FixedBufferPoolState final : std::enable_shared_from_this<FixedBufferPoolState> {
  FixedBufferPoolState(std::shared_ptr<ResourceVectorLedger> occupancy_ledger_value,
                       std::optional<ResourceVectorLedgerReservation> occupancy_credit_value,
                       SlabRangeClaim slab_claim_value, TypeDescriptor type_descriptor_value,
                       FixedBufferPoolStorage storage, const std::uint64_t pool_identity_value,
                       const Quantity slot_count_value, const std::size_t payload_capacity_bytes_value,
                       const std::size_t metadata_capacity_bytes_value)
      : occupancy_ledger(std::move(occupancy_ledger_value)), occupancy_credit(std::move(occupancy_credit_value)),
        slab_claim(std::move(slab_claim_value)), type_descriptor(std::move(type_descriptor_value)),
        payload_storage(storage.payload), metadata_storage(storage.metadata), control_storage(storage.control),
        pool_identity(pool_identity_value), slot_count(slot_count_value),
        payload_capacity_bytes(payload_capacity_bytes_value), metadata_capacity_bytes(metadata_capacity_bytes_value) {}

  void initialize() noexcept {
    free_head = 0U;
    free_slots = slot_count;
    for (Quantity slot_index = 0U; slot_index < slot_count; ++slot_index) {
      const auto next_free = slot_index + 1U == slot_count ? kNoSlot : slot_index + 1U;
      write_record(slot_index, {.generation = kInitialGeneration,
                                .payload_bytes = 0U,
                                .metadata_bytes = 0U,
                                .state = SlotState::free,
                                .next_free = next_free});
    }
  }

  [[nodiscard]] ksj::base::Status commit_occupancy_credit() {
    if (!occupancy_credit.has_value()) {
      return ksj::base::Status::Ok();
    }
    return occupancy_credit->commit();
  }

  [[nodiscard]] ksj::base::Result<MutableBufferLease> try_acquire() {
    std::lock_guard lock(mutex);
    if (failed) {
      return failure_status_locked();
    }
    if (!accepting) {
      if (generation_exhausted) {
        return ksj::base::Status::Unavailable("FixedBufferPool closed after slot generation exhaustion");
      }
      return ksj::base::Status::StateError("FixedBufferPool no longer accepts new buffers");
    }
    if (occupancy_credit.has_value() && !occupancy_credit->committed()) {
      return fail_closed_locked(ksj::base::Status::StateError("FixedBufferPool occupancy credit is not committed"));
    }
    if (free_slots == 0U || free_head == kNoSlot) {
      if (free_slots != 0U || free_head != kNoSlot) {
        return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferPool free-list accounting disagrees"));
      }
      return ksj::base::Status::Unavailable("FixedBufferPool has no free slot");
    }
    if (free_head >= slot_count) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferPool free-list head is out of range"));
    }

    auto record = read_record(free_head);
    if (record.state != SlotState::free || record.generation == 0U ||
        (record.next_free != kNoSlot && record.next_free >= slot_count)) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferPool free-slot record is invalid"));
    }

    const auto slot_index = free_head;
    free_head = record.next_free;
    record.state = SlotState::writable;
    record.payload_bytes = 0U;
    record.metadata_bytes = 0U;
    record.next_free = kNoSlot;
    write_record(slot_index, record);
    --free_slots;
    ++writable_slots;
    return MutableBufferLease{shared_from_this(), pool_identity, slot_index, record.generation};
  }

  [[nodiscard]] ksj::base::Result<ksj::base::ByteSpan> writable_payload(const std::uint64_t expected_pool_identity,
                                                                        const Quantity slot_index,
                                                                        const std::uint64_t generation) {
    std::lock_guard lock(mutex);
    if (failed) {
      return failure_status_locked();
    }
    const auto validation =
      validate_slot_locked(expected_pool_identity, slot_index, generation, SlotState::writable, "MutableBufferLease");
    if (!validation.ok()) {
      return validation;
    }
    return ksj::base::ByteSpan{payload_storage.data() + payload_offset(slot_index), payload_capacity_bytes};
  }

  [[nodiscard]] ksj::base::Result<ImmutableBufferHandle> seal(const std::uint64_t expected_pool_identity,
                                                              const Quantity slot_index, const std::uint64_t generation,
                                                              const TypeDescriptor& sealed_type_descriptor,
                                                              const Quantity payload_bytes,
                                                              const ksj::base::ConstByteSpan metadata) {
    std::lock_guard lock(mutex);
    if (failed) {
      return failure_status_locked();
    }
    const auto validation = validate_slot_locked(expected_pool_identity, slot_index, generation, SlotState::writable,
                                                 "MutableBufferLease::seal");
    if (!validation.ok()) {
      return validation;
    }
    if (!type_descriptor.exactly_matches(sealed_type_descriptor)) {
      return ksj::base::Status::ValidationError(
        "FixedBufferPool seal TypeDescriptor does not exactly match the pool type");
    }
    if (payload_bytes > payload_capacity_bytes) {
      return ksj::base::Status::InvalidArgument("FixedBufferPool seal payload length exceeds its fixed slot bound");
    }
    if (metadata.size() > metadata_capacity_bytes) {
      return ksj::base::Status::InvalidArgument("FixedBufferPool seal metadata length exceeds its fixed slot bound");
    }

    auto record = read_record(slot_index);
    if (writable_slots == 0U || record.state != SlotState::writable || record.generation != generation) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferPool writable-slot accounting disagrees"));
    }
    if (!metadata.empty()) {
      std::memmove(metadata_storage.data() + metadata_offset(slot_index), metadata.data(), metadata.size());
    }
    record.payload_bytes = payload_bytes;
    record.metadata_bytes = static_cast<Quantity>(metadata.size());
    record.state = SlotState::sealed;
    write_record(slot_index, record);
    --writable_slots;
    ++sealed_slots;
    return ImmutableBufferHandle{shared_from_this(), pool_identity,        slot_index,
                                 generation,         record.payload_bytes, record.metadata_bytes};
  }

  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan>
  payload(const std::uint64_t expected_pool_identity, const Quantity slot_index, const std::uint64_t generation) {
    std::lock_guard lock(mutex);
    if (failed) {
      return failure_status_locked();
    }
    const auto validation = validate_slot_locked(expected_pool_identity, slot_index, generation, SlotState::sealed,
                                                 "ImmutableBufferHandle::payload");
    if (!validation.ok()) {
      return validation;
    }
    const auto record = read_record(slot_index);
    if (record.payload_bytes > payload_capacity_bytes) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferPool sealed payload length is invalid"));
    }
    return ksj::base::ConstByteSpan{payload_storage.data() + payload_offset(slot_index),
                                    static_cast<std::size_t>(record.payload_bytes)};
  }

  [[nodiscard]] ksj::base::Result<ksj::base::ConstByteSpan>
  metadata(const std::uint64_t expected_pool_identity, const Quantity slot_index, const std::uint64_t generation) {
    std::lock_guard lock(mutex);
    if (failed) {
      return failure_status_locked();
    }
    const auto validation = validate_slot_locked(expected_pool_identity, slot_index, generation, SlotState::sealed,
                                                 "ImmutableBufferHandle::metadata");
    if (!validation.ok()) {
      return validation;
    }
    const auto record = read_record(slot_index);
    if (record.metadata_bytes > metadata_capacity_bytes) {
      return fail_closed_locked(ksj::base::Status::InternalError("FixedBufferPool sealed metadata length is invalid"));
    }
    const auto* const metadata_begin =
      metadata_capacity_bytes == 0U ? metadata_storage.data() : metadata_storage.data() + metadata_offset(slot_index);
    return ksj::base::ConstByteSpan{metadata_begin, static_cast<std::size_t>(record.metadata_bytes)};
  }

  void release_writable_noexcept(const std::uint64_t expected_pool_identity, const Quantity slot_index,
                                 const std::uint64_t generation) noexcept {
    try {
      std::lock_guard lock(mutex);
      if (!matches_slot_locked(expected_pool_identity, slot_index, generation, SlotState::writable) ||
          writable_slots == 0U) {
        emergency_fail_closed_locked();
        return;
      }
      recycle_slot_locked(slot_index, SlotState::writable);
    } catch (...) {
      // A destroyed exclusive lease has no retry channel. Do not let cleanup
      // escape a destructor or accidentally expose a potentially stale slot.
    }
  }

  void release_sealed_noexcept(const std::uint64_t expected_pool_identity, const Quantity slot_index,
                               const std::uint64_t generation) noexcept {
    try {
      std::lock_guard lock(mutex);
      if (!matches_slot_locked(expected_pool_identity, slot_index, generation, SlotState::sealed) ||
          sealed_slots == 0U) {
        emergency_fail_closed_locked();
        return;
      }
      recycle_slot_locked(slot_index, SlotState::sealed);
    } catch (...) {
      // See release_writable_noexcept().
    }
  }

  void close_owner_noexcept() noexcept {
    try {
      std::lock_guard lock(mutex);
      accepting = false;
    } catch (...) {
      // There is no safe way to revive a destroyed owner. Outstanding shared
      // state remains retained by its leases and eventually releases its
      // optional occupancy credit and caller-slab claim when the last one settles.
    }
  }

  [[nodiscard]] FixedBufferPoolSnapshot snapshot() const {
    std::lock_guard lock(mutex);
    return {
      .slot_count = slot_count,
      .free_slots = free_slots,
      .writable_slots = writable_slots,
      .sealed_slots = sealed_slots,
      .retired_slots = retired_slots,
      .payload_capacity_bytes = static_cast<Quantity>(payload_capacity_bytes),
      .metadata_capacity_bytes = static_cast<Quantity>(metadata_capacity_bytes),
      .payload_storage_bytes = payload_storage.size(),
      .metadata_storage_bytes = metadata_storage.size(),
      .control_storage_bytes = control_storage.size(),
      .accepting = accepting,
      .failed = failed,
      .generation_exhausted = generation_exhausted,
      .occupancy_credit_enabled = occupancy_credit.has_value(),
      .occupancy_credit_committed = occupancy_credit.has_value() && occupancy_credit->committed(),
      .last_error = last_error,
    };
  }

  [[nodiscard]] const TypeDescriptor* handle_type_descriptor(const std::uint64_t expected_pool_identity,
                                                             const std::uint64_t generation) const noexcept {
    if (expected_pool_identity == 0U || expected_pool_identity != pool_identity || generation == 0U) {
      return nullptr;
    }
    return &type_descriptor;
  }

  [[nodiscard]] ControlRecord read_record(const Quantity slot_index) const noexcept {
    const auto* const record = control_storage.data() + control_offset(slot_index);
    return {
      .generation = read_u64(record + kGenerationOffset),
      .payload_bytes = read_u64(record + kPayloadBytesOffset),
      .metadata_bytes = read_u64(record + kMetadataBytesOffset),
      .state = static_cast<SlotState>(read_u64(record + kStateOffset)),
      .next_free = read_u64(record + kNextFreeOffset),
    };
  }

  void write_record(const Quantity slot_index, const ControlRecord& record) noexcept {
    auto* const destination = control_storage.data() + control_offset(slot_index);
    write_u64(destination + kGenerationOffset, record.generation);
    write_u64(destination + kPayloadBytesOffset, record.payload_bytes);
    write_u64(destination + kMetadataBytesOffset, record.metadata_bytes);
    write_u64(destination + kStateOffset, static_cast<std::uint64_t>(record.state));
    write_u64(destination + kNextFreeOffset, record.next_free);
  }

  [[nodiscard]] std::size_t payload_offset(const Quantity slot_index) const noexcept {
    return static_cast<std::size_t>(slot_index) * payload_capacity_bytes;
  }

  [[nodiscard]] std::size_t metadata_offset(const Quantity slot_index) const noexcept {
    return static_cast<std::size_t>(slot_index) * metadata_capacity_bytes;
  }

  [[nodiscard]] std::size_t control_offset(const Quantity slot_index) const noexcept {
    return static_cast<std::size_t>(slot_index) * kControlRecordBytes;
  }

  [[nodiscard]] ksj::base::Status validate_slot_locked(const std::uint64_t expected_pool_identity,
                                                       const Quantity slot_index, const std::uint64_t generation,
                                                       const SlotState expected_state,
                                                       const char* const capability_name) const {
    if (expected_pool_identity == 0U || expected_pool_identity != pool_identity || generation == 0U ||
        slot_index >= slot_count) {
      return ksj::base::Status::StateError(std::string(capability_name) + " is foreign or invalid");
    }
    const auto record = read_record(slot_index);
    if (record.state != expected_state || record.generation != generation) {
      return ksj::base::Status::StateError(std::string(capability_name) + " is stale or already consumed");
    }
    return ksj::base::Status::Ok();
  }

  [[nodiscard]] bool matches_slot_locked(const std::uint64_t expected_pool_identity, const Quantity slot_index,
                                         const std::uint64_t generation,
                                         const SlotState expected_state) const noexcept {
    if (expected_pool_identity == 0U || expected_pool_identity != pool_identity || generation == 0U ||
        slot_index >= slot_count) {
      return false;
    }
    const auto record = read_record(slot_index);
    return record.state == expected_state && record.generation == generation;
  }

  [[nodiscard]] ksj::base::Status failure_status_locked() const {
    if (!last_error.ok()) {
      return last_error;
    }
    return ksj::base::Status::StateError("FixedBufferPool is failed closed");
  }

  [[nodiscard]] ksj::base::Status fail_closed_locked(ksj::base::Status cause) {
    failed = true;
    accepting = false;
    last_error = std::move(cause);
    return last_error;
  }

  void emergency_fail_closed_locked() noexcept {
    failed = true;
    accepting = false;
  }

  void recycle_slot_locked(const Quantity slot_index, const SlotState previous_state) noexcept {
    auto record = read_record(slot_index);
    if (record.state != previous_state) {
      emergency_fail_closed_locked();
      return;
    }
    if (previous_state == SlotState::writable) {
      --writable_slots;
    } else {
      --sealed_slots;
    }
    record.payload_bytes = 0U;
    record.metadata_bytes = 0U;
    if (record.generation == std::numeric_limits<std::uint64_t>::max()) {
      record.state = SlotState::retired;
      record.next_free = kNoSlot;
      write_record(slot_index, record);
      ++retired_slots;
      accepting = false;
      generation_exhausted = true;
      return;
    }
    if (free_head != kNoSlot && free_head >= slot_count) {
      record.state = SlotState::retired;
      record.next_free = kNoSlot;
      write_record(slot_index, record);
      ++retired_slots;
      emergency_fail_closed_locked();
      return;
    }
    ++record.generation;
    record.state = SlotState::free;
    record.next_free = free_head;
    write_record(slot_index, record);
    free_head = slot_index;
    ++free_slots;
  }

  // Keep the ledger before its optional credit: credit teardown may still
  // consult ledger shared state while tearing down the final pool state.
  std::shared_ptr<ResourceVectorLedger> occupancy_ledger;
  std::optional<ResourceVectorLedgerReservation> occupancy_credit;
  // This move-only runtime-image claim remains alive through every issued
  // lease/handle because they retain this shared state.
  SlabRangeClaim slab_claim;
  const TypeDescriptor type_descriptor;
  const ksj::base::ByteSpan payload_storage;
  const ksj::base::ByteSpan metadata_storage;
  const ksj::base::ByteSpan control_storage;
  const std::uint64_t pool_identity;
  const Quantity slot_count;
  const std::size_t payload_capacity_bytes;
  const std::size_t metadata_capacity_bytes;
  mutable std::mutex mutex;
  Quantity free_head{0U};
  Quantity free_slots{0U};
  Quantity writable_slots{0U};
  Quantity sealed_slots{0U};
  Quantity retired_slots{0U};
  bool accepting{true};
  bool failed{false};
  bool generation_exhausted{false};
  ksj::base::Status last_error{};
};

} // namespace detail

ksj::base::Result<std::size_t> fixed_buffer_pool_required_control_storage_bytes(const Quantity slot_count) {
  if (slot_count == 0U) {
    return ksj::base::Status::InvalidArgument("FixedBufferPool slot_count must be greater than zero");
  }
  return checked_slot_bytes(slot_count, kControlRecordBytes, "control slab");
}

ImmutableBufferHandle::ImmutableBufferHandle(std::shared_ptr<detail::FixedBufferPoolState> state,
                                             const std::uint64_t pool_identity, const Quantity slot_index,
                                             const std::uint64_t generation, const Quantity payload_bytes,
                                             const Quantity metadata_bytes) noexcept
    : state_(std::move(state)), pool_identity_(pool_identity), slot_index_(slot_index), generation_(generation),
      payload_bytes_(payload_bytes), metadata_bytes_(metadata_bytes) {}

ImmutableBufferHandle::~ImmutableBufferHandle() {
  release_noexcept();
}

ImmutableBufferHandle::ImmutableBufferHandle(ImmutableBufferHandle&& other) noexcept
    : state_(std::move(other.state_)), pool_identity_(std::exchange(other.pool_identity_, 0U)),
      slot_index_(std::exchange(other.slot_index_, 0U)), generation_(std::exchange(other.generation_, 0U)),
      payload_bytes_(std::exchange(other.payload_bytes_, 0U)),
      metadata_bytes_(std::exchange(other.metadata_bytes_, 0U)) {}

ImmutableBufferHandle& ImmutableBufferHandle::operator=(ImmutableBufferHandle&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    state_ = std::move(other.state_);
    pool_identity_ = std::exchange(other.pool_identity_, 0U);
    slot_index_ = std::exchange(other.slot_index_, 0U);
    generation_ = std::exchange(other.generation_, 0U);
    payload_bytes_ = std::exchange(other.payload_bytes_, 0U);
    metadata_bytes_ = std::exchange(other.metadata_bytes_, 0U);
  }
  return *this;
}

bool ImmutableBufferHandle::valid() const noexcept {
  return state_ != nullptr && pool_identity_ != 0U && generation_ != 0U;
}

ksj::base::Result<ksj::base::ConstByteSpan> ImmutableBufferHandle::payload() const {
  if (!valid()) {
    return ksj::base::Status::StateError("ImmutableBufferHandle is invalid or moved from");
  }
  return state_->payload(pool_identity_, slot_index_, generation_);
}

ksj::base::Result<ksj::base::ConstByteSpan> ImmutableBufferHandle::metadata() const {
  if (!valid()) {
    return ksj::base::Status::StateError("ImmutableBufferHandle is invalid or moved from");
  }
  return state_->metadata(pool_identity_, slot_index_, generation_);
}

const TypeDescriptor* ImmutableBufferHandle::type_descriptor() const noexcept {
  if (!valid()) {
    return nullptr;
  }
  return state_->handle_type_descriptor(pool_identity_, generation_);
}

void ImmutableBufferHandle::release_noexcept() noexcept {
  auto state = std::move(state_);
  const auto pool_identity = std::exchange(pool_identity_, 0U);
  const auto slot_index = std::exchange(slot_index_, 0U);
  const auto generation = std::exchange(generation_, 0U);
  payload_bytes_ = 0U;
  metadata_bytes_ = 0U;
  if (state != nullptr && pool_identity != 0U && generation != 0U) {
    state->release_sealed_noexcept(pool_identity, slot_index, generation);
  }
}

void ImmutableBufferHandle::disarm() noexcept {
  state_.reset();
  pool_identity_ = 0U;
  slot_index_ = 0U;
  generation_ = 0U;
  payload_bytes_ = 0U;
  metadata_bytes_ = 0U;
}

MutableBufferLease::MutableBufferLease(std::shared_ptr<detail::FixedBufferPoolState> state,
                                       const std::uint64_t pool_identity, const Quantity slot_index,
                                       const std::uint64_t generation) noexcept
    : state_(std::move(state)), pool_identity_(pool_identity), slot_index_(slot_index), generation_(generation) {}

MutableBufferLease::~MutableBufferLease() {
  release_noexcept();
}

MutableBufferLease::MutableBufferLease(MutableBufferLease&& other) noexcept
    : state_(std::move(other.state_)), pool_identity_(std::exchange(other.pool_identity_, 0U)),
      slot_index_(std::exchange(other.slot_index_, 0U)), generation_(std::exchange(other.generation_, 0U)) {}

MutableBufferLease& MutableBufferLease::operator=(MutableBufferLease&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    state_ = std::move(other.state_);
    pool_identity_ = std::exchange(other.pool_identity_, 0U);
    slot_index_ = std::exchange(other.slot_index_, 0U);
    generation_ = std::exchange(other.generation_, 0U);
  }
  return *this;
}

bool MutableBufferLease::valid() const noexcept {
  return state_ != nullptr && pool_identity_ != 0U && generation_ != 0U;
}

ksj::base::Result<ksj::base::ByteSpan> MutableBufferLease::writable_payload() {
  if (!valid()) {
    return ksj::base::Status::StateError("MutableBufferLease is invalid or moved from");
  }
  return state_->writable_payload(pool_identity_, slot_index_, generation_);
}

ksj::base::Result<ImmutableBufferHandle> MutableBufferLease::seal(const TypeDescriptor& type_descriptor,
                                                                  const Quantity payload_bytes,
                                                                  const ksj::base::ConstByteSpan metadata) {
  if (!valid()) {
    return ksj::base::Status::StateError("MutableBufferLease is invalid or moved from");
  }
  auto sealed = state_->seal(pool_identity_, slot_index_, generation_, type_descriptor, payload_bytes, metadata);
  if (sealed.ok()) {
    disarm();
  }
  return sealed;
}

void MutableBufferLease::release_noexcept() noexcept {
  auto state = std::move(state_);
  const auto pool_identity = std::exchange(pool_identity_, 0U);
  const auto slot_index = std::exchange(slot_index_, 0U);
  const auto generation = std::exchange(generation_, 0U);
  if (state != nullptr && pool_identity != 0U && generation != 0U) {
    state->release_writable_noexcept(pool_identity, slot_index, generation);
  }
}

void MutableBufferLease::disarm() noexcept {
  state_.reset();
  pool_identity_ = 0U;
  slot_index_ = 0U;
  generation_ = 0U;
}

ksj::base::Result<std::unique_ptr<FixedBufferPool>> FixedBufferPool::create(FixedBufferPoolConfig config,
                                                                            const FixedBufferPoolStorage storage) {
  if (config.slot_count == 0U || config.payload_capacity_bytes == 0U) {
    return ksj::base::Status::InvalidArgument("FixedBufferPool requires non-zero slot_count and payload capacity");
  }
  if (!fits_size_t(config.slot_count) || !fits_size_t(config.payload_capacity_bytes) ||
      !fits_size_t(config.metadata_capacity_bytes)) {
    return ksj::base::Status::InvalidArgument("FixedBufferPool bounds exceed this host's ByteSpan size");
  }
  if (config.type_descriptor.payload_kind() != PayloadKind::buffer_handle ||
      config.type_descriptor.mutability() != PayloadMutability::immutable_after_publish ||
      !has_host_normal_domain(config.type_descriptor)) {
    return ksj::base::Status::ValidationError(
      "FixedBufferPool requires a caller-asserted host-normal immutable_after_publish buffer_handle TypeDescriptor");
  }

  const auto required_payload = checked_slot_bytes(config.slot_count, config.payload_capacity_bytes, "payload slab");
  if (!required_payload.ok()) {
    return required_payload.status();
  }
  const auto required_metadata = checked_slot_bytes(config.slot_count, config.metadata_capacity_bytes, "metadata slab");
  if (!required_metadata.ok()) {
    return required_metadata.status();
  }
  const auto required_control = fixed_buffer_pool_required_control_storage_bytes(config.slot_count);
  if (!required_control.ok()) {
    return required_control.status();
  }
  // ImmutableBufferHandle::logical_bytes() reports payload plus metadata.  Validate
  // that sum independently of optional occupancy accounting, so a caller that
  // already accounts for its external slabs cannot create an unrepresentable
  // handle.  Also retain a representable total for the three participating slabs.
  const auto payload_and_metadata =
    checked_add_bytes(required_payload.value(), required_metadata.value(), "external slab total");
  if (!payload_and_metadata.ok()) {
    return payload_and_metadata.status();
  }
  if (payload_and_metadata.value() > std::numeric_limits<Quantity>::max()) {
    return ksj::base::Status::InvalidArgument("FixedBufferPool payload and metadata bounds exceed Quantity accounting");
  }
  const auto external_slab_total =
    checked_add_bytes(payload_and_metadata.value(), required_control.value(), "external slab total");
  if (!external_slab_total.ok()) {
    return external_slab_total.status();
  }
  if (external_slab_total.value() > std::numeric_limits<Quantity>::max()) {
    return ksj::base::Status::InvalidArgument("FixedBufferPool external slab total exceeds Quantity accounting");
  }
  const auto payload_storage_status = verify_exact_storage(storage.payload, required_payload.value(), "payload");
  if (!payload_storage_status.ok()) {
    return payload_storage_status;
  }
  const auto metadata_storage_status = verify_exact_storage(storage.metadata, required_metadata.value(), "metadata");
  if (!metadata_storage_status.ok()) {
    return metadata_storage_status;
  }
  const auto control_storage_status = verify_exact_storage(storage.control, required_control.value(), "control");
  if (!control_storage_status.ok()) {
    return control_storage_status;
  }

  const auto alignment = config.type_descriptor.min_alignment_bytes();
  if (alignment == 0U || alignment > std::numeric_limits<std::size_t>::max() ||
      config.payload_capacity_bytes % alignment != 0U ||
      reinterpret_cast<std::uintptr_t>(storage.payload.data()) % alignment != 0U) {
    return ksj::base::Status::InvalidArgument(
      "FixedBufferPool payload slab base and slot stride must satisfy TypeDescriptor alignment");
  }

  const std::array<ksj::base::ByteSpan, 3U> slabs{storage.payload, storage.metadata, storage.control};
  auto slab_claim = detail::claim_exclusive_slab_ranges(slabs);
  if (!slab_claim.ok()) {
    return slab_claim.status();
  }

  try {
    std::optional<ResourceVectorLedgerReservation> occupancy_credit;
    if (config.occupancy_ledger != nullptr) {
      const auto occupancy_vector =
        ResourceVector::create({.host_normal_bytes = static_cast<Quantity>(external_slab_total.value()),
                                .host_pinned_bytes = 0U,
                                .host_hugepage_bytes = 0U,
                                .shared_host_bytes = 0U,
                                .spool_bytes = 0U,
                                .transport_bytes = 0U,
                                .descriptor_count = config.slot_count,
                                .async_token_count = 0U,
                                .cpu_leaf_permits = 0U,
                                .backend_gang_permits = 0U,
                                .provider_private_permits = 0U,
                                .io_slots = 0U,
                                .devices = {}},
                               "FixedBufferPool external slab occupancy credit");
      if (!occupancy_vector.ok()) {
        return occupancy_vector.status();
      }
      auto reserved_credit = config.occupancy_ledger->try_reserve(occupancy_vector.value());
      if (!reserved_credit.ok()) {
        return reserved_credit.status();
      }
      occupancy_credit.emplace(std::move(reserved_credit).value());
    }

    const auto pool_identity = allocate_pool_identity();
    if (!pool_identity.ok()) {
      return pool_identity.status();
    }
    auto state = std::make_shared<detail::FixedBufferPoolState>(
      std::move(config.occupancy_ledger), std::move(occupancy_credit), std::move(slab_claim).value(),
      std::move(config.type_descriptor), storage, pool_identity.value(), config.slot_count,
      static_cast<std::size_t>(config.payload_capacity_bytes),
      static_cast<std::size_t>(config.metadata_capacity_bytes));
    state->initialize();
    const auto committed = state->commit_occupancy_credit();
    if (!committed.ok()) {
      return committed;
    }
    return std::unique_ptr<FixedBufferPool>(new FixedBufferPool(std::move(state)));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate FixedBufferPool control state");
  }
}

FixedBufferPool::FixedBufferPool(std::shared_ptr<detail::FixedBufferPoolState> state) noexcept
    : state_(std::move(state)) {}

FixedBufferPool::~FixedBufferPool() {
  if (state_ != nullptr) {
    state_->close_owner_noexcept();
  }
}

ksj::base::Result<MutableBufferLease> FixedBufferPool::try_acquire() {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("FixedBufferPool is invalid");
  }
  return state_->try_acquire();
}

FixedBufferPoolSnapshot FixedBufferPool::snapshot() const {
  if (state_ == nullptr) {
    return {};
  }
  return state_->snapshot();
}

std::uint64_t FixedBufferPool::pool_identity() const noexcept {
  return state_ == nullptr ? 0U : state_->pool_identity;
}

const TypeDescriptor* FixedBufferPool::type_descriptor() const noexcept {
  return state_ == nullptr ? nullptr : &state_->type_descriptor;
}

} // namespace ksj::recon::runtime
