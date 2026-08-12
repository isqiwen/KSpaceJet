#include "kspacejet/recon/runtime/key_shard.hpp"

#include <array>

namespace ksj::recon::runtime {

std::string_view to_string(const KeyShardState state) noexcept {
  static constexpr std::array names{
    "dormant", "idle", "scheduled", "running", "blocked_input", "blocked_dependency", "blocked_output",
    "blocked_resource", "flushing", "cancelling", "done", "failed",
  };
  return names.at(static_cast<std::size_t>(state));
}

KeyShardState KeyShard::state() const {
  std::lock_guard lock(mutex_);
  return state_;
}

bool KeyShard::cancellation_requested() const {
  std::lock_guard lock(mutex_);
  return cancellation_requested_;
}

bool KeyShard::notify_ready() {
  std::lock_guard lock(mutex_);
  if (state_ == KeyShardState::flushing || state_ == KeyShardState::cancelling || state_ == KeyShardState::done ||
      state_ == KeyShardState::failed) {
    return false;
  }
  if (state_ == KeyShardState::scheduled || state_ == KeyShardState::running) {
    readiness_pending_ = true;
    return false;
  }
  return schedule_unlocked();
}

ksj::base::Status KeyShard::begin_activation() {
  std::lock_guard lock(mutex_);
  if (state_ != KeyShardState::scheduled) {
    return ksj::base::Status::StateError("KeyShard activation may begin only from scheduled state");
  }
  state_ = KeyShardState::running;
  return ksj::base::Status::Ok();
}

ksj::base::Result<bool> KeyShard::complete_activation(const ActivationOutcome outcome) {
  std::lock_guard lock(mutex_);
  if (state_ != KeyShardState::running) {
    return ksj::base::Status::StateError("KeyShard activation completion requires running state");
  }
  if (failure_requested_) {
    state_ = KeyShardState::failed;
    readiness_pending_ = false;
    return false;
  }
  if (cancellation_requested_) {
    state_ = KeyShardState::cancelling;
    readiness_pending_ = false;
    return false;
  }

  if (outcome == ActivationOutcome::reschedule) {
    state_ = KeyShardState::idle;
    return schedule_unlocked();
  }
  state_ = state_for(outcome);
  if (state_ == KeyShardState::done || state_ == KeyShardState::failed || state_ == KeyShardState::flushing) {
    readiness_pending_ = false;
    return false;
  }
  if (readiness_pending_) {
    readiness_pending_ = false;
    return schedule_unlocked();
  }
  return false;
}

void KeyShard::request_cancel() {
  std::lock_guard lock(mutex_);
  cancellation_requested_ = true;
  if (state_ != KeyShardState::running && state_ != KeyShardState::done && state_ != KeyShardState::failed) {
    state_ = KeyShardState::cancelling;
    readiness_pending_ = false;
  }
}

void KeyShard::fail() {
  std::lock_guard lock(mutex_);
  failure_requested_ = true;
  if (state_ != KeyShardState::running && state_ != KeyShardState::done) {
    state_ = KeyShardState::failed;
    readiness_pending_ = false;
  }
}

ksj::base::Status KeyShard::begin_flush() {
  std::lock_guard lock(mutex_);
  if (state_ == KeyShardState::done || state_ == KeyShardState::failed || state_ == KeyShardState::cancelling) {
    return ksj::base::Status::StateError("cannot begin normal KeyShard flush after terminal cancellation/failure");
  }
  if (state_ == KeyShardState::running || state_ == KeyShardState::scheduled) {
    return ksj::base::Status::StateError("KeyShard ordinary activation must quiesce before normal flush");
  }
  state_ = KeyShardState::flushing;
  readiness_pending_ = false;
  return ksj::base::Status::Ok();
}

ksj::base::Status KeyShard::finish_flush(const bool success) {
  std::lock_guard lock(mutex_);
  if (state_ != KeyShardState::flushing) {
    return ksj::base::Status::StateError("KeyShard flush completion requires flushing state");
  }
  state_ = success ? KeyShardState::done : KeyShardState::failed;
  return ksj::base::Status::Ok();
}

bool KeyShard::schedule_unlocked() {
  if (state_ == KeyShardState::scheduled || state_ == KeyShardState::running || state_ == KeyShardState::flushing ||
      state_ == KeyShardState::cancelling || state_ == KeyShardState::done || state_ == KeyShardState::failed) {
    return false;
  }
  state_ = KeyShardState::scheduled;
  return true;
}

KeyShardState KeyShard::state_for(const ActivationOutcome outcome) noexcept {
  switch (outcome) {
    case ActivationOutcome::idle:
      return KeyShardState::idle;
    case ActivationOutcome::blocked_input:
      return KeyShardState::blocked_input;
    case ActivationOutcome::blocked_dependency:
      return KeyShardState::blocked_dependency;
    case ActivationOutcome::blocked_output:
      return KeyShardState::blocked_output;
    case ActivationOutcome::blocked_resource:
      return KeyShardState::blocked_resource;
    case ActivationOutcome::flushing:
      return KeyShardState::flushing;
    case ActivationOutcome::done:
      return KeyShardState::done;
    case ActivationOutcome::failed:
      return KeyShardState::failed;
    case ActivationOutcome::reschedule:
      return KeyShardState::scheduled;
  }
  return KeyShardState::failed;
}

} // namespace ksj::recon::runtime
