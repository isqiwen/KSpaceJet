#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/status.hpp"

#include <mutex>
#include <string_view>

namespace ksj::recon::runtime {

// A KeyShard is runtime-private state inside one scan/node OperatorInstance.
// It is intentionally not a Provider instance or an ABI lifecycle object.
enum class KeyShardState {
  dormant,
  idle,
  scheduled,
  running,
  blocked_input,
  blocked_dependency,
  blocked_output,
  blocked_resource,
  flushing,
  cancelling,
  done,
  failed,
};

enum class ActivationOutcome {
  idle,
  blocked_input,
  blocked_dependency,
  blocked_output,
  blocked_resource,
  reschedule,
  flushing,
  done,
  failed,
};

[[nodiscard]] std::string_view to_string(KeyShardState state) noexcept;

// Coalesces arbitrary readiness notifications into at most one queued or
// running activation.  notify_ready() returning true is the only condition in
// which an executor should enqueue work.  It implements the check-register-
// recheck shape required to avoid lost wakeups without making a worker wait on
// a queue, dependency, socket, or permit.
class KeyShard final {
public:
  [[nodiscard]] KeyShardState state() const;
  [[nodiscard]] bool cancellation_requested() const;

  // Signals that input/dependency/output/permit state may have changed.  A
  // true return means the caller must enqueue one activation token.
  [[nodiscard]] bool notify_ready();
  [[nodiscard]] ksj::base::Status begin_activation();

  // Completes a bounded activation.  A true return means another activation
  // must be enqueued.  The caller owns Provider callback serialization; this
  // state machine enforces no concurrent activation for the same KeyShard.
  [[nodiscard]] ksj::base::Result<bool> complete_activation(ActivationOutcome outcome);

  // Cancellation/failure stop future ordinary activations.  If a callback is
  // currently running, its safe-point completion observes the pending cause;
  // the host must never try to kill an in-process Provider callback.
  void request_cancel();
  void fail();

  // Terminal callbacks run only after ordinary work is quiescent.  These two
  // transitions make the normal drain/flush path explicit for the owning
  // OperatorInstance.
  [[nodiscard]] ksj::base::Status begin_flush();
  [[nodiscard]] ksj::base::Status finish_flush(bool success);

private:
  [[nodiscard]] bool schedule_unlocked();
  [[nodiscard]] static KeyShardState state_for(ActivationOutcome outcome) noexcept;

  mutable std::mutex mutex_;
  KeyShardState state_{KeyShardState::dormant};
  bool readiness_pending_{false};
  bool cancellation_requested_{false};
  bool failure_requested_{false};
};

} // namespace ksj::recon::runtime
