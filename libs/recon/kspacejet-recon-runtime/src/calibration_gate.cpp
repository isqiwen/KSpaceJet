#include "kspacejet/recon/runtime/calibration_gate.hpp"

#include <algorithm>
#include <utility>

namespace ksj::recon::runtime {

CalibrationGate::CalibrationGate(const CalibrationGateLimits limits) : limits_(limits) {}

ksj::base::Result<CalibrationDisposition> CalibrationGate::await_or_pass(std::string key,
                                                                         const PendingCalibrationFrame frame) {
  std::lock_guard lock(mutex_);
  if (key.empty()) {
    return ksj::base::Status::InvalidArgument("calibration key must not be empty");
  }
  auto found = keys_.find(key);
  if (found != keys_.end() && found->second.closed_missing) {
    return ksj::base::Status::StateError("calibration key was closed without a token");
  }
  if (found != keys_.end() && found->second.ready) {
    return CalibrationDisposition::ready;
  }
  if (found == keys_.end()) {
    if (!can_add_key(key)) {
      return ksj::base::Status::Unavailable("calibration active-key capacity is exhausted");
    }
    found = keys_.emplace(std::move(key), KeyState{}).first;
  }
  if (!can_retain(found->second, frame)) {
    return ksj::base::Status::Unavailable("CALIBRATION_HORIZON_EXCEEDED");
  }
  found->second.waiting.push_back(frame);
  found->second.waiting_bytes += frame.charged_bytes;
  ++waiting_items_;
  waiting_bytes_ += frame.charged_bytes;
  return CalibrationDisposition::waiting;
}

ksj::base::Result<std::vector<PendingCalibrationFrame>> CalibrationGate::publish_ready(std::string key,
                                                                                       CalibrationToken token) {
  std::lock_guard lock(mutex_);
  if (key.empty()) {
    return ksj::base::Status::InvalidArgument("calibration key must not be empty");
  }
  if (token.epoch != 0U) {
    return ksj::base::Status::ValidationError("calibration tokens require epoch zero");
  }
  if (token.digest.empty()) {
    return ksj::base::Status::InvalidArgument("calibration token digest must not be empty");
  }
  auto found = keys_.find(key);
  if (found == keys_.end()) {
    if (!can_add_key(key)) {
      return ksj::base::Status::Unavailable("calibration active-key capacity is exhausted");
    }
    found = keys_.emplace(std::move(key), KeyState{}).first;
  }
  if (found->second.ready) {
    return ksj::base::Status::ValidationError("duplicate CalibrationReady token");
  }
  if (found->second.closed_missing) {
    return ksj::base::Status::StateError("cannot publish CalibrationReady after EndOfInput missing state");
  }

  auto waiting = std::move(found->second.waiting);
  waiting_items_ -= waiting.size();
  waiting_bytes_ -= found->second.waiting_bytes;
  found->second.waiting_bytes = 0;
  found->second.ready = true;
  found->second.token = std::move(token);
  return waiting;
}

ksj::base::Result<CalibrationToken> CalibrationGate::token_for(const std::string& key) const {
  std::lock_guard lock(mutex_);
  const auto found = keys_.find(key);
  if (found == keys_.end() || !found->second.ready) {
    return ksj::base::Status::NotFound("CalibrationReady token is not available for key");
  }
  return found->second.token;
}

std::vector<std::string> CalibrationGate::close_missing() {
  std::lock_guard lock(mutex_);
  std::vector<std::string> missing;
  for (auto& [key, state] : keys_) {
    if (!state.ready && !state.waiting.empty()) {
      state.closed_missing = true;
      missing.push_back(key);
    }
  }
  std::sort(missing.begin(), missing.end());
  return missing;
}

std::uint64_t CalibrationGate::waiting_items() const {
  std::lock_guard lock(mutex_);
  return waiting_items_;
}

std::uint64_t CalibrationGate::waiting_bytes() const {
  std::lock_guard lock(mutex_);
  return waiting_bytes_;
}

bool CalibrationGate::can_add_key(const std::string& key) const noexcept {
  return keys_.contains(key) || keys_.size() < limits_.max_active_keys;
}

bool CalibrationGate::can_retain(const KeyState& state, const PendingCalibrationFrame frame) const noexcept {
  return state.waiting.size() < limits_.max_waiting_items_per_key &&
         frame.charged_bytes <= limits_.max_waiting_bytes_per_key - state.waiting_bytes &&
         waiting_items_ < limits_.max_waiting_items_total &&
         frame.charged_bytes <= limits_.max_waiting_bytes_total - waiting_bytes_;
}

} // namespace ksj::recon::runtime
