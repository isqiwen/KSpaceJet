#include "kspacejet/recon/runtime/host_frame_assembler.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace ksj::recon::runtime {
namespace detail {

enum class HostFrameSlotPhase : std::uint8_t {
  free,
  filling,
  ready,
  dispatched,
  quarantined,
};

struct HostFrameSlotRecord {
  CartesianFrameSlot frame_slot;
  HostFrameSlotPhase phase{HostFrameSlotPhase::free};
  FrameSlotToken token{};
  std::uint64_t lease_id{0U};
};

struct HostFrameAssemblerState {
  HostFrameAssemblerState(std::string scan_instance_id_value, std::vector<HostFrameSlotRecord> slots_value) noexcept
      : scan_instance_id(std::move(scan_instance_id_value)), slots(std::move(slots_value)) {}

  mutable std::mutex mutex;
  const std::string scan_instance_id;
  std::vector<HostFrameSlotRecord> slots;
  bool ingress_closed{false};
  bool failed{false};
  ksj::base::Status last_error{ksj::base::Status::Ok()};
  std::uint64_t next_lease_id{0U};
};

} // namespace detail

namespace {

using HostFrameAssemblerState = detail::HostFrameAssemblerState;
using HostFrameSlotPhase = detail::HostFrameSlotPhase;
using HostFrameSlotRecord = detail::HostFrameSlotRecord;

[[nodiscard]] bool same_semantic_key(const FrameSlotContext& lhs, const FrameSlotContext& rhs) noexcept {
  return lhs.semantic_key == rhs.semantic_key;
}

[[nodiscard]] bool is_live_phase(const HostFrameSlotPhase phase) noexcept {
  return phase == HostFrameSlotPhase::filling || phase == HostFrameSlotPhase::ready ||
         phase == HostFrameSlotPhase::dispatched;
}

[[nodiscard]] bool is_completed_phase(const HostFrameSlotPhase phase) noexcept {
  return phase == HostFrameSlotPhase::ready || phase == HostFrameSlotPhase::dispatched;
}

[[nodiscard]] ksj::base::Status current_failure_locked(const HostFrameAssemblerState& state,
                                                       const std::string_view operation) {
  if (!state.last_error.ok()) {
    return state.last_error;
  }
  return ksj::base::Status::StateError(std::string(operation) + " is unavailable after HostFrameAssembler failure");
}

void quarantine_record_noexcept(HostFrameSlotRecord& record) noexcept {
  if (record.phase == HostFrameSlotPhase::free || record.phase == HostFrameSlotPhase::quarantined) {
    return;
  }
  try {
    static_cast<void>(record.frame_slot.quarantine(record.token));
  } catch (...) {
    // The host phase below remains authoritative if lower-level diagnostics
    // cannot be emitted while failing closed.
  }
  record.phase = HostFrameSlotPhase::quarantined;
}

void fail_closed_locked(HostFrameAssemblerState& state, ksj::base::Status cause) noexcept {
  if (!state.failed) {
    state.failed = true;
    state.ingress_closed = true;
    state.last_error = std::move(cause);
  }
  for (auto& record : state.slots) {
    quarantine_record_noexcept(record);
  }
}

[[nodiscard]] ksj::base::Status end_of_input_locked(HostFrameAssemblerState& state) {
  if (state.failed) {
    return current_failure_locked(state, "end_of_input");
  }
  state.ingress_closed = true;
  for (auto& record : state.slots) {
    if (record.phase != HostFrameSlotPhase::filling) {
      continue;
    }
    const auto disposition = record.frame_slot.end_of_input(record.token);
    if (!disposition.ok()) {
      fail_closed_locked(state, disposition.status());
      return disposition.status();
    }
    fail_closed_locked(
      state, ksj::base::Status::InternalError("HostFrameAssembler received a non-complete frame at EndOfInput"));
    return current_failure_locked(state, "end_of_input");
  }
  if (std::any_of(state.slots.begin(), state.slots.end(), [](const HostFrameSlotRecord& record) {
        return record.phase == HostFrameSlotPhase::ready || record.phase == HostFrameSlotPhase::dispatched;
      })) {
    return ksj::base::Status::Unavailable("HostFrameAssembler is draining live CompletedFrameLease values");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status abort_locked(HostFrameAssemblerState& state) {
  if (!state.failed) {
    fail_closed_locked(state, ksj::base::Status::StateError("HostFrameAssembler aborted"));
  }
  return ksj::base::Status::Ok();
}

void emergency_fail_closed_noexcept(const std::shared_ptr<HostFrameAssemblerState>& state) noexcept {
  if (state == nullptr) {
    return;
  }
  try {
    std::scoped_lock lock(state->mutex);
    state->failed = true;
    state->ingress_closed = true;
    for (auto& record : state->slots) {
      if (record.phase == HostFrameSlotPhase::free || record.phase == HostFrameSlotPhase::quarantined) {
        continue;
      }
      try {
        static_cast<void>(record.frame_slot.quarantine(record.token));
      } catch (...) {}
      record.phase = HostFrameSlotPhase::quarantined;
    }
  } catch (...) {
    // Destruction has no error channel and must not throw.
  }
}

[[nodiscard]] ksj::base::Result<std::uint64_t> next_lease_id_locked(HostFrameAssemblerState& state) {
  if (state.next_lease_id == std::numeric_limits<std::uint64_t>::max()) {
    return ksj::base::Status::Unavailable("HostFrameAssembler lease identity space is exhausted");
  }
  ++state.next_lease_id;
  return state.next_lease_id;
}

[[nodiscard]] ksj::base::Status validate_assembly_lease_locked(const HostFrameAssemblerState& state,
                                                               const std::size_t slot_index, const FrameSlotToken token,
                                                               const std::uint64_t lease_id,
                                                               const std::string_view operation) {
  if (state.failed) {
    return current_failure_locked(state, operation);
  }
  if (slot_index >= state.slots.size()) {
    return ksj::base::Status::StateError(std::string(operation) + " received a foreign FrameAssemblyLease");
  }
  const auto& record = state.slots[slot_index];
  if (record.phase != HostFrameSlotPhase::filling || record.token != token || record.lease_id != lease_id) {
    return ksj::base::Status::StateError(std::string(operation) + " received a stale or consumed FrameAssemblyLease");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status validate_completed_lease_locked(const HostFrameAssemblerState& state,
                                                                const std::size_t slot_index,
                                                                const FrameSlotToken token,
                                                                const std::uint64_t lease_id,
                                                                const std::string_view operation) {
  if (state.failed) {
    return current_failure_locked(state, operation);
  }
  if (slot_index >= state.slots.size()) {
    return ksj::base::Status::StateError(std::string(operation) + " received a foreign CompletedFrameLease");
  }
  const auto& record = state.slots[slot_index];
  if (!is_completed_phase(record.phase) || record.token != token || record.lease_id != lease_id) {
    return ksj::base::Status::StateError(std::string(operation) + " received a stale or consumed CompletedFrameLease");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status begin_completed_dispatch_locked(HostFrameAssemblerState& state,
                                                                const std::size_t slot_index,
                                                                const FrameSlotToken token,
                                                                const std::uint64_t lease_id,
                                                                const std::string_view operation) {
  const auto validation = validate_completed_lease_locked(state, slot_index, token, lease_id, operation);
  if (!validation.ok()) {
    return validation;
  }
  auto& record = state.slots[slot_index];
  if (record.phase != HostFrameSlotPhase::ready) {
    fail_closed_locked(state, ksj::base::Status::StateError("CompletedFrameLease dispatch was attempted twice"));
    return current_failure_locked(state, operation);
  }
  const auto status = record.frame_slot.begin_compute(token);
  if (!status.ok()) {
    fail_closed_locked(state, status);
    return current_failure_locked(state, operation);
  }
  record.phase = HostFrameSlotPhase::dispatched;
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status acknowledge_completed_consumption_locked(HostFrameAssemblerState& state,
                                                                         const std::size_t slot_index,
                                                                         const FrameSlotToken token,
                                                                         const std::uint64_t lease_id,
                                                                         const std::string_view operation) {
  const auto validation = validate_completed_lease_locked(state, slot_index, token, lease_id, operation);
  if (!validation.ok()) {
    return validation;
  }
  auto& record = state.slots[slot_index];
  if (record.phase != HostFrameSlotPhase::dispatched) {
    fail_closed_locked(state, ksj::base::Status::StateError(
                                "CompletedFrameLease cannot be acknowledged before its consumer begins dispatch"));
    return current_failure_locked(state, operation);
  }
  auto status = record.frame_slot.begin_emit(token);
  if (!status.ok()) {
    fail_closed_locked(state, status);
    return current_failure_locked(state, operation);
  }
  status = record.frame_slot.recycle(token);
  if (!status.ok()) {
    fail_closed_locked(state, status);
    return current_failure_locked(state, operation);
  }
  record.phase = HostFrameSlotPhase::free;
  record.lease_id = 0U;
  return ksj::base::Status::Ok();
}

void abandon_assembly_lease_noexcept(const std::shared_ptr<HostFrameAssemblerState>& state,
                                     const std::size_t slot_index, const FrameSlotToken token,
                                     const std::uint64_t lease_id) noexcept {
  try {
    if (state == nullptr || lease_id == 0U) {
      return;
    }
    std::scoped_lock lock(state->mutex);
    if (slot_index >= state->slots.size()) {
      return;
    }
    const auto validation = validate_assembly_lease_locked(*state, slot_index, token, lease_id, "lease destruction");
    if (!validation.ok()) {
      return;
    }
    fail_closed_locked(*state,
                       ksj::base::Status::StateError("FrameAssemblyLease was dropped before exact frame completion"));
  } catch (...) {
    emergency_fail_closed_noexcept(state);
  }
}

void abandon_completed_lease_noexcept(const std::shared_ptr<HostFrameAssemblerState>& state,
                                      const std::size_t slot_index, const FrameSlotToken token,
                                      const std::uint64_t lease_id, const std::string_view reason) noexcept {
  try {
    if (state == nullptr || lease_id == 0U) {
      return;
    }
    std::scoped_lock lock(state->mutex);
    if (slot_index >= state->slots.size()) {
      return;
    }
    const auto validation = validate_completed_lease_locked(*state, slot_index, token, lease_id, "lease destruction");
    if (!validation.ok()) {
      return;
    }
    fail_closed_locked(*state, ksj::base::Status::StateError(std::string(reason)));
  } catch (...) {
    emergency_fail_closed_noexcept(state);
  }
}

} // namespace

FrameAssemblyLease::FrameAssemblyLease(std::shared_ptr<detail::HostFrameAssemblerState> state,
                                       const std::size_t slot_index, const FrameSlotToken token,
                                       const std::uint64_t lease_id) noexcept
    : state_(std::move(state)), slot_index_(slot_index), token_(token), lease_id_(lease_id) {}

FrameAssemblyLease::~FrameAssemblyLease() {
  abandon_noexcept();
}

FrameAssemblyLease::FrameAssemblyLease(FrameAssemblyLease&& other) noexcept
    : state_(std::move(other.state_)), slot_index_(other.slot_index_), token_(other.token_),
      lease_id_(std::exchange(other.lease_id_, 0U)) {}

FrameAssemblyLease& FrameAssemblyLease::operator=(FrameAssemblyLease&& other) noexcept {
  if (this != &other) {
    abandon_noexcept();
    state_ = std::move(other.state_);
    slot_index_ = other.slot_index_;
    token_ = other.token_;
    lease_id_ = std::exchange(other.lease_id_, 0U);
  }
  return *this;
}

bool FrameAssemblyLease::valid() const noexcept {
  return state_ != nullptr && lease_id_ != 0U;
}

ksj::base::Status FrameAssemblyLease::scatter(const CartesianLineCoordinate coordinate,
                                              const ksj::base::ConstByteSpan payload) {
  if (!valid()) {
    return ksj::base::Status::StateError("scatter requires a live FrameAssemblyLease");
  }
  std::scoped_lock lock(state_->mutex);
  const auto validation = validate_assembly_lease_locked(*state_, slot_index_, token_, lease_id_, "scatter");
  if (!validation.ok()) {
    return validation;
  }
  return state_->slots[slot_index_].frame_slot.scatter(token_, coordinate, payload);
}

ksj::base::Result<CompletedFrameLease> FrameAssemblyLease::seal_complete() {
  if (!valid()) {
    return ksj::base::Status::StateError("seal_complete requires a live FrameAssemblyLease");
  }
  std::scoped_lock lock(state_->mutex);
  const auto validation = validate_assembly_lease_locked(*state_, slot_index_, token_, lease_id_, "seal_complete");
  if (!validation.ok()) {
    return validation;
  }
  auto& record = state_->slots[slot_index_];
  const auto snapshot = record.frame_slot.snapshot();
  if (snapshot.state != FrameSlotState::ready || snapshot.completion != FrameCompletion::complete) {
    return ksj::base::Status::Unavailable("FrameSlot exact coverage is not complete yet");
  }
  record.phase = HostFrameSlotPhase::ready;
  auto result = CompletedFrameLease{state_, slot_index_, token_, lease_id_};
  lease_id_ = 0U;
  state_.reset();
  return result;
}

void FrameAssemblyLease::abandon_noexcept() noexcept {
  abandon_assembly_lease_noexcept(state_, slot_index_, token_, lease_id_);
  lease_id_ = 0U;
  state_.reset();
}

CompletedFrameLease::CompletedFrameLease(std::shared_ptr<detail::HostFrameAssemblerState> state,
                                         const std::size_t slot_index, const FrameSlotToken token,
                                         const std::uint64_t lease_id) noexcept
    : state_(std::move(state)), slot_index_(slot_index), token_(token), lease_id_(lease_id) {}

CompletedFrameLease::~CompletedFrameLease() {
  abandon_noexcept();
}

CompletedFrameLease::CompletedFrameLease(CompletedFrameLease&& other) noexcept
    : state_(std::move(other.state_)), slot_index_(other.slot_index_), token_(other.token_),
      lease_id_(std::exchange(other.lease_id_, 0U)) {}

CompletedFrameLease& CompletedFrameLease::operator=(CompletedFrameLease&& other) noexcept {
  if (this != &other) {
    abandon_noexcept();
    state_ = std::move(other.state_);
    slot_index_ = other.slot_index_;
    token_ = other.token_;
    lease_id_ = std::exchange(other.lease_id_, 0U);
  }
  return *this;
}

bool CompletedFrameLease::valid() const noexcept {
  return state_ != nullptr && lease_id_ != 0U;
}

ksj::base::Result<ksj::base::ConstByteSpan> CompletedFrameLease::bytes() const {
  if (!valid()) {
    return ksj::base::Status::StateError("bytes requires a live CompletedFrameLease");
  }
  std::scoped_lock lock(state_->mutex);
  const auto validation = validate_completed_lease_locked(*state_, slot_index_, token_, lease_id_, "bytes");
  if (!validation.ok()) {
    return validation;
  }
  return state_->slots[slot_index_].frame_slot.frame_bytes(token_);
}

ksj::base::Result<FrameSlotContext> CompletedFrameLease::context() const {
  if (!valid()) {
    return ksj::base::Status::StateError("context requires a live CompletedFrameLease");
  }
  std::scoped_lock lock(state_->mutex);
  const auto validation = validate_completed_lease_locked(*state_, slot_index_, token_, lease_id_, "context");
  if (!validation.ok()) {
    return validation;
  }
  return state_->slots[slot_index_].frame_slot.snapshot().context;
}

ksj::base::Result<FrameSlotToken> CompletedFrameLease::token() const {
  if (!valid()) {
    return ksj::base::Status::StateError("token requires a live CompletedFrameLease");
  }
  std::scoped_lock lock(state_->mutex);
  const auto validation = validate_completed_lease_locked(*state_, slot_index_, token_, lease_id_, "token");
  if (!validation.ok()) {
    return validation;
  }
  return token_;
}

ksj::base::Status CompletedFrameLease::begin_dispatch() {
  if (!valid()) {
    return ksj::base::Status::StateError("begin_dispatch requires a live CompletedFrameLease");
  }
  std::scoped_lock lock(state_->mutex);
  return begin_completed_dispatch_locked(*state_, slot_index_, token_, lease_id_, "begin_dispatch");
}

ksj::base::Status CompletedFrameLease::acknowledge_consumed() {
  if (!valid()) {
    return ksj::base::Status::StateError("acknowledge_consumed requires a live CompletedFrameLease");
  }
  {
    std::scoped_lock lock(state_->mutex);
    const auto acknowledged =
      acknowledge_completed_consumption_locked(*state_, slot_index_, token_, lease_id_, "acknowledge_consumed");
    if (!acknowledged.ok()) {
      return acknowledged;
    }
    lease_id_ = 0U;
  }
  state_.reset();
  return ksj::base::Status::Ok();
}

ksj::base::Status CompletedFrameLease::abandon() {
  if (!valid()) {
    return ksj::base::Status::StateError("abandon requires a live CompletedFrameLease");
  }
  abandon_completed_lease_noexcept(state_, slot_index_, token_, lease_id_,
                                   "CompletedFrameLease was abandoned before downstream settlement");
  lease_id_ = 0U;
  state_.reset();
  return ksj::base::Status::Ok();
}

void CompletedFrameLease::abandon_noexcept() noexcept {
  abandon_completed_lease_noexcept(state_, slot_index_, token_, lease_id_,
                                   "CompletedFrameLease was dropped before downstream settlement");
  lease_id_ = 0U;
  state_.reset();
}

ksj::base::Result<std::unique_ptr<HostFrameAssembler>> HostFrameAssembler::create(HostFrameAssemblerConfig config) {
  if (config.scan_instance_id.empty()) {
    return ksj::base::Status::InvalidArgument("HostFrameAssembler requires a non-empty scan_instance_id");
  }
  if (config.frame_slots.empty()) {
    return ksj::base::Status::InvalidArgument("HostFrameAssembler requires at least one preallocated FrameSlot");
  }
  for (std::size_t index = 0U; index < config.frame_slots.size(); ++index) {
    for (std::size_t prior = 0U; prior < index; ++prior) {
      if (config.frame_slots[prior].slot_id == config.frame_slots[index].slot_id) {
        return ksj::base::Status::ValidationError("HostFrameAssembler FrameSlot ids must be unique");
      }
    }
  }

  std::vector<HostFrameSlotRecord> slots;
  slots.reserve(config.frame_slots.size());
  for (auto& slot_config : config.frame_slots) {
    auto slot = CartesianFrameSlot::create(std::move(slot_config));
    if (!slot.ok()) {
      return slot.status();
    }
    slots.push_back({.frame_slot = std::move(slot).value()});
  }

  try {
    auto state = std::make_shared<HostFrameAssemblerState>(std::move(config.scan_instance_id), std::move(slots));
    return std::unique_ptr<HostFrameAssembler>(new HostFrameAssembler(std::move(state)));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate HostFrameAssembler control state");
  }
}

HostFrameAssembler::HostFrameAssembler(std::shared_ptr<detail::HostFrameAssemblerState> state) noexcept
    : state_(std::move(state)) {}

void HostFrameAssembler::emergency_abort_noexcept() noexcept {
  emergency_fail_closed_noexcept(state_);
}

HostFrameAssembler::~HostFrameAssembler() {
  try {
    if (state_ != nullptr) {
      std::scoped_lock lock(state_->mutex);
      static_cast<void>(abort_locked(*state_));
    }
  } catch (...) {
    emergency_abort_noexcept();
  }
}

ksj::base::Result<FrameAssemblyLease> HostFrameAssembler::try_begin_frame(const FrameSlotContext context) {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("HostFrameAssembler was moved from or destroyed");
  }
  std::scoped_lock lock(state_->mutex);
  if (state_->failed) {
    return current_failure_locked(*state_, "try_begin_frame");
  }
  if (state_->ingress_closed) {
    return ksj::base::Status::StateError("HostFrameAssembler no longer accepts frames after EndOfInput");
  }
  for (const auto& record : state_->slots) {
    if (is_live_phase(record.phase) && same_semantic_key(record.frame_slot.snapshot().context, context)) {
      return ksj::base::Status::AlreadyExists(
        "HostFrameAssembler cannot begin a second live FrameSlot for one semantic key");
    }
  }
  const auto available =
    std::find_if(state_->slots.begin(), state_->slots.end(), [](const HostFrameSlotRecord& record) {
      return record.phase == HostFrameSlotPhase::free;
    });
  if (available == state_->slots.end()) {
    return ksj::base::Status::Unavailable("HostFrameAssembler has no free preallocated FrameSlot");
  }
  const auto token = available->frame_slot.begin_frame(context);
  if (!token.ok()) {
    return token.status();
  }
  const auto lease_id = next_lease_id_locked(*state_);
  if (!lease_id.ok()) {
    static_cast<void>(available->frame_slot.quarantine(token.value()));
    available->phase = HostFrameSlotPhase::quarantined;
    fail_closed_locked(*state_, lease_id.status());
    return lease_id.status();
  }
  available->phase = HostFrameSlotPhase::filling;
  available->token = token.value();
  available->lease_id = lease_id.value();
  const auto slot_index = static_cast<std::size_t>(std::distance(state_->slots.begin(), available));
  return FrameAssemblyLease{state_, slot_index, token.value(), lease_id.value()};
}

ksj::base::Status HostFrameAssembler::end_of_input() {
  if (state_ == nullptr) {
    return ksj::base::Status::StateError("HostFrameAssembler was moved from or destroyed");
  }
  std::scoped_lock lock(state_->mutex);
  return end_of_input_locked(*state_);
}

ksj::base::Status HostFrameAssembler::abort() {
  if (state_ == nullptr) {
    return ksj::base::Status::Ok();
  }
  std::scoped_lock lock(state_->mutex);
  return abort_locked(*state_);
}

HostFrameAssemblerSnapshot HostFrameAssembler::snapshot() const {
  HostFrameAssemblerSnapshot snapshot;
  if (state_ == nullptr) {
    snapshot.failed = true;
    snapshot.last_error = ksj::base::Status::StateError("HostFrameAssembler was moved from or destroyed");
    return snapshot;
  }
  std::scoped_lock lock(state_->mutex);
  snapshot.ingress_closed = state_->ingress_closed;
  snapshot.failed = state_->failed;
  snapshot.last_error = state_->last_error;
  for (const auto& record : state_->slots) {
    switch (record.phase) {
      case HostFrameSlotPhase::free:
        ++snapshot.free_slots;
        break;
      case HostFrameSlotPhase::filling:
        ++snapshot.filling_slots;
        break;
      case HostFrameSlotPhase::ready:
        ++snapshot.ready_slots;
        break;
      case HostFrameSlotPhase::dispatched:
        ++snapshot.dispatched_slots;
        break;
      case HostFrameSlotPhase::quarantined:
        ++snapshot.quarantined_slots;
        break;
    }
  }
  return snapshot;
}

bool HostFrameAssembler::owns(const CompletedFrameLease& lease) const noexcept {
  return state_ != nullptr && state_ == lease.state_ && lease.lease_id_ != 0U;
}

} // namespace ksj::recon::runtime
