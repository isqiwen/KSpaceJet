#include "kspacejet/recon/runtime/scan_lifecycle.hpp"

#include <array>
#include <string>

namespace ksj::recon::runtime {
namespace {

constexpr bool is_pre_admission(const ScanState state) noexcept {
  return state == ScanState::session_candidate || state == ScanState::describing || state == ScanState::planning ||
         state == ScanState::verifying || state == ScanState::admitting;
}

constexpr bool is_terminal(const ScanState state) noexcept {
  return state == ScanState::completed || state == ScanState::rejected || state == ScanState::cancelled ||
         state == ScanState::failed;
}

} // namespace

std::string_view to_string(const ScanState state) noexcept {
  static constexpr std::array names{
    "session_candidate", "describing",  "planning",      "verifying", "admitting", "starting",
    "running",           "ingress_closed", "draining", "finalizing", "sink_flushing", "cancelling",
    "failing",           "terminal_cleanup", "completed", "rejected", "cancelled", "failed",
  };
  return names.at(static_cast<std::size_t>(state));
}

std::string_view to_string(const TerminalCause cause) noexcept {
  static constexpr std::array names{"none", "cancel", "failure", "invariant"};
  return names.at(static_cast<std::size_t>(cause));
}

ScanState ScanLifecycle::state() const noexcept {
  return state_;
}

TerminalCause ScanLifecycle::terminal_cause() const noexcept {
  return terminal_cause_;
}

bool ScanLifecycle::admitted() const noexcept {
  return admitted_;
}

bool ScanLifecycle::terminal() const noexcept {
  return is_terminal(state_);
}

ksj::base::Status ScanLifecycle::begin_describing() {
  const auto status = require_state(ScanState::session_candidate, "begin_describing");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::describing;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_planning() {
  const auto status = require_state(ScanState::describing, "begin_planning");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::planning;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_verifying() {
  const auto status = require_state(ScanState::planning, "begin_verifying");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::verifying;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_admitting() {
  const auto status = require_state(ScanState::verifying, "begin_admitting");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::admitting;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::admit() {
  const auto status = require_state(ScanState::admitting, "admit");
  if (!status.ok()) {
    return status;
  }
  admitted_ = true;
  state_ = ScanState::starting;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::start() {
  const auto status = require_state(ScanState::starting, "start");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::running;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::close_ingress() {
  const auto status = require_state(ScanState::running, "close_ingress");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::ingress_closed;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_draining() {
  const auto status = require_state(ScanState::ingress_closed, "begin_draining");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::draining;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_finalizing() {
  const auto status = require_state(ScanState::draining, "begin_finalizing");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::finalizing;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_sink_flush() {
  const auto status = require_state(ScanState::finalizing, "begin_sink_flush");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::sink_flushing;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::complete() {
  const auto status = require_state(ScanState::sink_flushing, "complete");
  if (!status.ok()) {
    return status;
  }
  if (terminal_cause_ != TerminalCause::none) {
    return ksj::base::Status::StateError("cannot complete a scan with a terminal cause");
  }
  state_ = ScanState::completed;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::reject() {
  if (!is_pre_admission(state_)) {
    return ksj::base::Status::StateError("reject is valid only before scan admission");
  }
  state_ = ScanState::rejected;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::request_cancel() {
  if (is_terminal(state_)) {
    return ksj::base::Status::StateError("cannot cancel a terminal scan");
  }
  if (is_pre_admission(state_)) {
    state_ = ScanState::cancelled;
    return ksj::base::Status::Ok();
  }
  raise_cause(TerminalCause::cancel);
  // Terminal cleanup is already in progress.  A late cancellation records the
  // cause but must not move the state backwards and resurrect ordinary
  // terminal callbacks.  A later failure can still win through fail().
  if (state_ != ScanState::failing && state_ != ScanState::terminal_cleanup) {
    state_ = ScanState::cancelling;
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::fail(const bool invariant_violation) {
  if (is_terminal(state_)) {
    return ksj::base::Status::StateError("cannot fail a terminal scan");
  }
  raise_cause(invariant_violation ? TerminalCause::invariant : TerminalCause::failure);
  // Cleanup has a single owner.  Preserve that phase while upgrading the
  // monotonic terminal cause, so finish_terminal_cleanup() remains the only
  // transition that selects the final scan outcome.
  if (state_ != ScanState::terminal_cleanup) {
    state_ = ScanState::failing;
  }
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::begin_terminal_cleanup() {
  const auto status = require_one_of(ScanState::cancelling, ScanState::failing, "begin_terminal_cleanup");
  if (!status.ok()) {
    return status;
  }
  state_ = ScanState::terminal_cleanup;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::finish_terminal_cleanup() {
  const auto status = require_state(ScanState::terminal_cleanup, "finish_terminal_cleanup");
  if (!status.ok()) {
    return status;
  }
  state_ = terminal_cause_ == TerminalCause::cancel ? ScanState::cancelled : ScanState::failed;
  return ksj::base::Status::Ok();
}

ksj::base::Status ScanLifecycle::require_state(const ScanState expected, const std::string_view operation) const {
  if (state_ == expected) {
    return ksj::base::Status::Ok();
  }
  return ksj::base::Status::StateError(std::string(operation) + " is invalid in state " +
                                       std::string(to_string(state_)));
}

ksj::base::Status ScanLifecycle::require_one_of(const ScanState first, const ScanState second,
                                                 const std::string_view operation) const {
  if (state_ == first || state_ == second) {
    return ksj::base::Status::Ok();
  }
  return ksj::base::Status::StateError(std::string(operation) + " is invalid in state " +
                                       std::string(to_string(state_)));
}

void ScanLifecycle::raise_cause(const TerminalCause cause) noexcept {
  if (static_cast<int>(cause) > static_cast<int>(terminal_cause_)) {
    terminal_cause_ = cause;
  }
}

} // namespace ksj::recon::runtime
