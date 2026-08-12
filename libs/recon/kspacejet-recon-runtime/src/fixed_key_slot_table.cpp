#include "kspacejet/recon/runtime/fixed_key_slot_table.hpp"

#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace ksj::recon::runtime {
namespace {

constexpr std::size_t kSemanticRecordBytes = 16U;
constexpr std::size_t kPhysicalRecordBytes = 16U;
constexpr std::size_t kFirstWordOffset = 0U;
constexpr std::size_t kSecondWordOffset = sizeof(std::uint64_t);
// Two state bits are sufficient for never-bound/active/completed. The
// remaining 62 bits cover every RFC 8785 canonical Quantity used by plans.
constexpr std::uint64_t kSemanticStateShift = 62U;
constexpr std::uint64_t kSemanticSlotMask = (std::uint64_t{1} << kSemanticStateShift) - 1U;
constexpr std::uint64_t kPhysicalFreeFlag = std::uint64_t{1} << 63U;
constexpr ksj::recon::Quantity kNoSlot = std::numeric_limits<ksj::recon::Quantity>::max();

static_assert(ksj::recon::kMaxCanonicalJsonInteger <= kSemanticSlotMask);
static_assert(ksj::recon::kMaxCanonicalJsonInteger < kPhysicalFreeFlag);

std::atomic<std::uint64_t> g_next_table_identity{1U};

[[nodiscard]] ksj::base::Result<std::uint64_t> allocate_table_identity() {
  auto identity = g_next_table_identity.load(std::memory_order_relaxed);
  for (;;) {
    // Leave the counter saturated at max rather than allowing an ABA-style
    // wrap to zero or a previously assigned process-local identity.
    if (identity == 0U || identity == std::numeric_limits<std::uint64_t>::max()) {
      return ksj::base::Status::Unavailable("KeySlotTable process-local identity space is exhausted");
    }
    if (g_next_table_identity.compare_exchange_weak(identity, identity + 1U, std::memory_order_relaxed,
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

[[nodiscard]] bool checked_multiply(const ksj::recon::Quantity lhs, const ksj::recon::Quantity rhs,
                                    ksj::recon::Quantity& result) noexcept {
  if (lhs != 0U && rhs > std::numeric_limits<ksj::recon::Quantity>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

} // namespace

ksj::base::Result<std::size_t> required_storage_bytes(const ksj::recon::KeySlotTablePlan& plan) {
  const auto expected = ksj::recon::dense_key_slot_host_metadata_charged_bytes(
    plan.key_domain_bound(), plan.slot_count(), "FixedKeySlotTable storage");
  if (!expected.ok()) {
    return expected.status();
  }
  if (expected.value() != plan.host_metadata_charged_bytes()) {
    return ksj::base::Status::ValidationError(
      "KeySlotTable host_metadata_charged_bytes does not match the dense-v1 storage layout");
  }
  if (expected.value() > std::numeric_limits<std::size_t>::max()) {
    return ksj::base::Status::ValidationError("KeySlotTable storage exceeds this host's ByteSpan size");
  }
  return static_cast<std::size_t>(expected.value());
}

ksj::base::Result<FixedKeySlotTable> FixedKeySlotTable::create(const ksj::recon::KeySlotTablePlan& plan,
                                                               const ksj::base::ByteSpan storage) {
  const auto plan_status = validate_plan(plan);
  if (!plan_status.ok()) {
    return plan_status;
  }
  const auto required = required_storage_bytes(plan);
  if (!required.ok()) {
    return required.status();
  }
  if (storage.size() < required.value()) {
    return ksj::base::Status::InvalidArgument("KeySlotTable caller storage is smaller than the frozen plan bound");
  }
  if (required.value() != 0U && storage.data() == nullptr) {
    return ksj::base::Status::InvalidArgument("KeySlotTable caller storage must not be null");
  }
  const auto storage_address = reinterpret_cast<std::uintptr_t>(storage.data());
  if (storage_address % fixed_key_slot_table_storage_alignment() != 0U) {
    return ksj::base::Status::InvalidArgument("KeySlotTable caller storage has insufficient byte-layout alignment");
  }
  const auto table_identity = allocate_table_identity();
  if (!table_identity.ok()) {
    return table_identity.status();
  }

  FixedKeySlotTable table{&plan, storage.data(), required.value(), table_identity.value()};
  {
    std::lock_guard lock(table.mutex_);
    table.initialize_storage_unlocked();
  }
  return std::move(table);
}

FixedKeySlotTable::FixedKeySlotTable(const ksj::recon::KeySlotTablePlan* const plan, ksj::base::byte* const storage,
                                     const std::size_t storage_bytes, const std::uint64_t table_identity) noexcept
    : plan_(plan), storage_(storage), storage_bytes_(storage_bytes), table_identity_(table_identity) {}

FixedKeySlotTable::FixedKeySlotTable(FixedKeySlotTable&& other) noexcept {
  std::lock_guard lock(other.mutex_);
  move_from_unlocked(other);
}

FixedKeySlotTable& FixedKeySlotTable::operator=(FixedKeySlotTable&& other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(mutex_, other.mutex_);
    move_from_unlocked(other);
  }
  return *this;
}

ksj::base::Result<ksj::recon::Quantity>
FixedKeySlotTable::dense_index(const std::span<const ksj::recon::Quantity> key) const {
  std::lock_guard lock(mutex_);
  return dense_index_unlocked(key);
}

ksj::base::Result<KeySlotToken> FixedKeySlotTable::bind_or_find(const std::span<const ksj::recon::Quantity> key) {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("KeySlotTable was moved from");
  }
  if (aborted_) {
    return ksj::base::Status::StateError("KeySlotTable is aborted");
  }

  const auto semantic_index = dense_index_unlocked(key);
  if (!semantic_index.ok()) {
    return semantic_index.status();
  }
  auto semantic = read_semantic(semantic_index.value());
  if (semantic.state != SemanticState::never_bound && semantic.state != SemanticState::active &&
      semantic.state != SemanticState::completed) {
    return ksj::base::Status::InternalError("KeySlotTable semantic record has an invalid state");
  }

  if (semantic.state == SemanticState::active) {
    if (semantic.slot_id >= plan_->slot_count()) {
      return ksj::base::Status::InternalError("KeySlotTable active semantic record has an invalid slot");
    }
    const auto physical = read_physical(semantic.slot_id);
    if (physical.free || physical.next_free_or_owner != semantic_index.value() ||
        physical.generation != semantic.generation) {
      return ksj::base::Status::InternalError("KeySlotTable semantic and physical records disagree");
    }
    return KeySlotToken{table_identity_, semantic.slot_id, semantic.generation};
  }
  if (semantic.state == SemanticState::completed) {
    return ksj::base::Status::ValidationError("late event for a completed KeySlot semantic key");
  }
  if (new_keys_closed_) {
    return ksj::base::Status::StateError("KeySlotTable does not admit new keys after close");
  }
  if (ever_bound_keys_ >= plan_->max_distinct_keys()) {
    return ksj::base::Status::Unavailable("KeySlotTable distinct-key bound is exhausted");
  }
  if (live_keys_ >= plan_->max_live_keys() || free_head_ == kNoSlot) {
    return ksj::base::Status::Unavailable("KeySlotTable live-slot capacity is exhausted");
  }
  if (free_head_ >= plan_->slot_count()) {
    return ksj::base::Status::InternalError("KeySlotTable free-list head is outside the physical slot range");
  }

  const auto slot_id = free_head_;
  auto physical = read_physical(slot_id);
  if (!physical.free || physical.generation == 0U ||
      (physical.next_free_or_owner != kNoSlot && physical.next_free_or_owner >= plan_->slot_count())) {
    return ksj::base::Status::InternalError("KeySlotTable free-list record is invalid");
  }
  free_head_ = physical.next_free_or_owner;
  physical.free = false;
  physical.next_free_or_owner = semantic_index.value();
  write_physical(slot_id, physical);
  write_semantic(semantic_index.value(),
                 {.state = SemanticState::active, .slot_id = slot_id, .generation = physical.generation});
  ++ever_bound_keys_;
  ++live_keys_;
  return KeySlotToken{table_identity_, slot_id, physical.generation};
}

ksj::base::Status FixedKeySlotTable::validate_active(const KeySlotToken token) const {
  std::lock_guard lock(mutex_);
  return validate_active_unlocked(token);
}

ksj::base::Status FixedKeySlotTable::seal_completed(const KeySlotToken token) {
  std::lock_guard lock(mutex_);
  const auto active_status = validate_active_unlocked(token);
  if (!active_status.ok()) {
    return active_status;
  }
  const auto physical = read_physical(token.slot_id_);
  auto semantic = read_semantic(physical.next_free_or_owner);
  semantic.state = SemanticState::completed;
  write_semantic(physical.next_free_or_owner, semantic);
  ++completed_tombstones_;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedKeySlotTable::evict_completed(const KeySlotToken token) {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("KeySlotTable was moved from");
  }
  if (aborted_) {
    return ksj::base::Status::StateError("KeySlotTable is aborted");
  }
  if (!token.valid() || token.table_identity_ != table_identity_ || token.slot_id_ >= plan_->slot_count()) {
    return ksj::base::Status::StateError("KeySlotTable token is invalid for this table");
  }
  auto physical = read_physical(token.slot_id_);
  if (physical.free || physical.generation != token.generation_ ||
      physical.next_free_or_owner >= plan_->key_domain_bound()) {
    return ksj::base::Status::StateError("KeySlotTable token is stale or no longer active");
  }
  const auto semantic_index = physical.next_free_or_owner;
  const auto semantic = read_semantic(semantic_index);
  if (semantic.state != SemanticState::completed || semantic.slot_id != token.slot_id_ ||
      semantic.generation != token.generation_) {
    return ksj::base::Status::StateError("KeySlotTable may evict only its completed token generation");
  }
  if (physical.generation == std::numeric_limits<std::uint64_t>::max()) {
    return ksj::base::Status::Unavailable("KeySlotTable slot generation is exhausted and cannot wrap");
  }
  if (live_keys_ == 0U) {
    return ksj::base::Status::InternalError("KeySlotTable live-slot accounting underflow");
  }
  if (free_head_ != kNoSlot && free_head_ >= plan_->slot_count()) {
    return ksj::base::Status::InternalError("KeySlotTable free-list head is outside the physical slot range");
  }

  ++physical.generation;
  physical.free = true;
  physical.next_free_or_owner = free_head_;
  write_physical(token.slot_id_, physical);
  free_head_ = token.slot_id_;
  --live_keys_;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedKeySlotTable::close_new_keys() {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("KeySlotTable was moved from");
  }
  if (aborted_) {
    return ksj::base::Status::StateError("KeySlotTable is aborted");
  }
  if (new_keys_closed_) {
    return ksj::base::Status::StateError("KeySlotTable new keys are already closed");
  }
  new_keys_closed_ = true;
  return ksj::base::Status::Ok();
}

ksj::base::Status FixedKeySlotTable::abort() {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("KeySlotTable was moved from");
  }
  if (aborted_) {
    return ksj::base::Status::Ok();
  }
  new_keys_closed_ = true;
  aborted_ = true;
  return ksj::base::Status::Ok();
}

FixedKeySlotTableSnapshot FixedKeySlotTable::snapshot() const {
  std::lock_guard lock(mutex_);
  if (plan_ == nullptr) {
    return {.aborted = true};
  }
  return {
    .key_domain_bound = plan_->key_domain_bound(),
    .slot_count = plan_->slot_count(),
    .ever_bound_keys = ever_bound_keys_,
    .live_keys = live_keys_,
    .completed_tombstones = completed_tombstones_,
    .free_slots = plan_->slot_count() - live_keys_,
    .storage_bytes = storage_bytes_,
    .new_keys_closed = new_keys_closed_,
    .aborted = aborted_,
  };
}

ksj::base::Status FixedKeySlotTable::validate_plan(const ksj::recon::KeySlotTablePlan& plan) {
  if (plan.mapping_algorithm_id() != ksj::recon::kDenseMixedRadixKeySlotMappingAlgorithmId ||
      plan.storage_accounting_id() != ksj::recon::kDenseKeySlotStorageAccountingId ||
      plan.generation_policy() != ksj::recon::kMonotonicU64KeySlotGenerationPolicy ||
      plan.eviction_policy() != ksj::recon::kCompletedOnlyKeySlotEvictionPolicy ||
      plan.late_event_policy() != ksj::recon::kFailKeySlotLateEventPolicy || !plan.seal_on_completion()) {
    return ksj::base::Status::ValidationError("KeySlotTable plan does not use the required dense-v1 policies");
  }
  if (plan.key_domain_bound() == 0U || plan.key_domain_bound() > kSemanticSlotMask ||
      plan.max_distinct_keys() != plan.key_domain_bound() || plan.max_live_keys() == 0U ||
      plan.max_live_keys() > plan.key_domain_bound() || plan.slot_count() != plan.max_live_keys() ||
      plan.slot_count() > kSemanticSlotMask || plan.initial_generation() != ksj::recon::kInitialKeySlotGeneration) {
    return ksj::base::Status::ValidationError("KeySlotTable plan has invalid dense domain or physical slot bounds");
  }

  ksj::recon::Quantity dense_product = 1U;
  const auto& dimensions = plan.dense_dimensions();
  for (std::size_t dimension_index = 0U; dimension_index < dimensions.size(); ++dimension_index) {
    const auto& dimension = dimensions[dimension_index];
    if (dimension.field().empty() || dimension.cardinality() == 0U ||
        !checked_multiply(dense_product, dimension.cardinality(), dense_product)) {
      return ksj::base::Status::ValidationError("KeySlotTable plan has an invalid dense dimension product");
    }
    for (std::size_t other_index = dimension_index + 1U; other_index < dimensions.size(); ++other_index) {
      if (dimension.field() == dimensions[other_index].field()) {
        return ksj::base::Status::ValidationError("KeySlotTable plan has duplicate dense dimension fields");
      }
    }
  }
  if (dense_product != plan.key_domain_bound()) {
    return ksj::base::Status::ValidationError("KeySlotTable plan dense dimensions do not match key_domain_bound");
  }
  const auto required = required_storage_bytes(plan);
  if (!required.ok()) {
    return required.status();
  }
  return ksj::base::Status::Ok();
}

ksj::base::Result<ksj::recon::Quantity>
FixedKeySlotTable::dense_index_unlocked(const std::span<const ksj::recon::Quantity> key) const {
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("KeySlotTable was moved from");
  }
  const auto& dimensions = plan_->dense_dimensions();
  if (key.size() != dimensions.size()) {
    return ksj::base::Status::InvalidArgument("KeySlotTable key arity does not match the frozen dense dimensions");
  }

  ksj::recon::Quantity index = 0U;
  for (std::size_t dimension_index = 0U; dimension_index < dimensions.size(); ++dimension_index) {
    const auto& dimension = dimensions[dimension_index];
    const auto value = key[dimension_index];
    if (value < dimension.minimum()) {
      return ksj::base::Status::ValidationError("KeySlotTable key value is below the frozen dimension minimum");
    }
    const auto offset = value - dimension.minimum();
    if (offset >= dimension.cardinality()) {
      return ksj::base::Status::ValidationError("KeySlotTable key value exceeds the frozen dimension cardinality");
    }
    if (index > (std::numeric_limits<ksj::recon::Quantity>::max() - offset) / dimension.cardinality()) {
      return ksj::base::Status::ValidationError("KeySlotTable mixed-radix key index overflows");
    }
    index = index * dimension.cardinality() + offset;
  }
  if (index >= plan_->key_domain_bound()) {
    return ksj::base::Status::InternalError("KeySlotTable dense mapping escaped its frozen domain");
  }
  return index;
}

ksj::base::Status FixedKeySlotTable::validate_active_unlocked(const KeySlotToken token) const {
  if (plan_ == nullptr) {
    return ksj::base::Status::StateError("KeySlotTable was moved from");
  }
  if (aborted_) {
    return ksj::base::Status::StateError("KeySlotTable is aborted");
  }
  if (!token.valid() || token.table_identity_ != table_identity_ || token.slot_id_ >= plan_->slot_count()) {
    return ksj::base::Status::StateError("KeySlotTable token is invalid for this table");
  }
  const auto physical = read_physical(token.slot_id_);
  if (physical.free || physical.generation != token.generation_ ||
      physical.next_free_or_owner >= plan_->key_domain_bound()) {
    return ksj::base::Status::StateError("KeySlotTable token is stale or no longer active");
  }
  const auto semantic = read_semantic(physical.next_free_or_owner);
  if (semantic.state != SemanticState::active || semantic.slot_id != token.slot_id_ ||
      semantic.generation != token.generation_) {
    return ksj::base::Status::StateError("KeySlotTable token does not identify an active semantic key");
  }
  return ksj::base::Status::Ok();
}

FixedKeySlotTable::SemanticRecord
FixedKeySlotTable::read_semantic(const ksj::recon::Quantity semantic_index) const noexcept {
  const auto* const record = storage_ + static_cast<std::size_t>(semantic_index) * kSemanticRecordBytes;
  const auto packed_state_slot = read_u64(record + kFirstWordOffset);
  const auto state = static_cast<SemanticState>(packed_state_slot >> kSemanticStateShift);
  return {
    .state = state,
    .slot_id = packed_state_slot & kSemanticSlotMask,
    .generation = read_u64(record + kSecondWordOffset),
  };
}

void FixedKeySlotTable::write_semantic(const ksj::recon::Quantity semantic_index,
                                       const SemanticRecord record) noexcept {
  auto* const destination = storage_ + static_cast<std::size_t>(semantic_index) * kSemanticRecordBytes;
  const auto packed_state_slot =
    (static_cast<std::uint64_t>(record.state) << kSemanticStateShift) | (record.slot_id & kSemanticSlotMask);
  write_u64(destination + kFirstWordOffset, packed_state_slot);
  write_u64(destination + kSecondWordOffset, record.generation);
}

FixedKeySlotTable::PhysicalRecord FixedKeySlotTable::read_physical(const ksj::recon::Quantity slot_id) const noexcept {
  const auto semantic_bytes = static_cast<std::size_t>(plan_->key_domain_bound()) * kSemanticRecordBytes;
  const auto* const record = storage_ + semantic_bytes + static_cast<std::size_t>(slot_id) * kPhysicalRecordBytes;
  const auto packed_owner_or_next = read_u64(record + kFirstWordOffset);
  const auto generation = read_u64(record + kSecondWordOffset);
  if ((packed_owner_or_next & kPhysicalFreeFlag) == 0U) {
    return {.free = false, .next_free_or_owner = packed_owner_or_next, .generation = generation};
  }
  return {
    .free = true,
    .next_free_or_owner = packed_owner_or_next == kNoSlot ? kNoSlot : packed_owner_or_next & ~kPhysicalFreeFlag,
    .generation = generation,
  };
}

void FixedKeySlotTable::write_physical(const ksj::recon::Quantity slot_id, const PhysicalRecord record) noexcept {
  const auto semantic_bytes = static_cast<std::size_t>(plan_->key_domain_bound()) * kSemanticRecordBytes;
  auto* const destination = storage_ + semantic_bytes + static_cast<std::size_t>(slot_id) * kPhysicalRecordBytes;
  const auto packed_owner_or_next =
    record.free ? (record.next_free_or_owner == kNoSlot ? kNoSlot : kPhysicalFreeFlag | record.next_free_or_owner)
                : record.next_free_or_owner;
  write_u64(destination + kFirstWordOffset, packed_owner_or_next);
  write_u64(destination + kSecondWordOffset, record.generation);
}

void FixedKeySlotTable::initialize_storage_unlocked() noexcept {
  for (ksj::recon::Quantity semantic_index = 0U; semantic_index < plan_->key_domain_bound(); ++semantic_index) {
    write_semantic(semantic_index, {});
  }
  for (ksj::recon::Quantity slot_id = 0U; slot_id < plan_->slot_count(); ++slot_id) {
    const auto next = slot_id + 1U == plan_->slot_count() ? kNoSlot : slot_id + 1U;
    write_physical(slot_id, {.free = true, .next_free_or_owner = next, .generation = plan_->initial_generation()});
  }
  free_head_ = 0U;
  ever_bound_keys_ = 0U;
  live_keys_ = 0U;
  completed_tombstones_ = 0U;
  new_keys_closed_ = false;
  aborted_ = false;
}

void FixedKeySlotTable::move_from_unlocked(FixedKeySlotTable& other) noexcept {
  plan_ = other.plan_;
  storage_ = other.storage_;
  storage_bytes_ = other.storage_bytes_;
  table_identity_ = other.table_identity_;
  free_head_ = other.free_head_;
  ever_bound_keys_ = other.ever_bound_keys_;
  live_keys_ = other.live_keys_;
  completed_tombstones_ = other.completed_tombstones_;
  new_keys_closed_ = other.new_keys_closed_;
  aborted_ = other.aborted_;

  other.plan_ = nullptr;
  other.storage_ = nullptr;
  other.storage_bytes_ = 0U;
  other.table_identity_ = 0U;
  other.free_head_ = kNoSlot;
  other.ever_bound_keys_ = 0U;
  other.live_keys_ = 0U;
  other.completed_tombstones_ = 0U;
  other.new_keys_closed_ = true;
  other.aborted_ = true;
}

} // namespace ksj::recon::runtime
