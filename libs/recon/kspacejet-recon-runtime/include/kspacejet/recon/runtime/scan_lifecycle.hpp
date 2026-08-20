#pragma once

#include "kspacejet/base/status.hpp"

#include <string_view>

namespace ksj::recon::runtime {

// This is the scan-level state machine. Source acquisition belongs to the
// caller; this object begins once an in-process candidate scan exists.
enum class ScanState {
  input_candidate,
  describing,
  planning,
  verifying,
  admitting,
  starting,
  running,
  ingress_closed,
  draining,
  finalizing,
  sink_flushing,
  cancelling,
  failing,
  terminal_cleanup,
  completed,
  rejected,
  cancelled,
  failed,
};

enum class TerminalCause {
  none,
  cancel,
  failure,
  invariant,
};

[[nodiscard]] std::string_view to_string(ScanState state) noexcept;
[[nodiscard]] std::string_view to_string(TerminalCause cause) noexcept;

class ScanLifecycle {
public:
  [[nodiscard]] ScanState state() const noexcept;
  [[nodiscard]] TerminalCause terminal_cause() const noexcept;
  [[nodiscard]] bool admitted() const noexcept;
  [[nodiscard]] bool terminal() const noexcept;

  [[nodiscard]] ksj::base::Status begin_describing();
  [[nodiscard]] ksj::base::Status begin_planning();
  [[nodiscard]] ksj::base::Status begin_verifying();
  [[nodiscard]] ksj::base::Status begin_admitting();
  [[nodiscard]] ksj::base::Status admit();
  [[nodiscard]] ksj::base::Status start();
  [[nodiscard]] ksj::base::Status close_ingress();
  [[nodiscard]] ksj::base::Status begin_draining();
  [[nodiscard]] ksj::base::Status begin_finalizing();
  [[nodiscard]] ksj::base::Status begin_sink_flush();
  [[nodiscard]] ksj::base::Status complete();

  // Planning and validation faults are rejections only before admission.  A
  // failure after admission is never relabelled as a rejection.
  [[nodiscard]] ksj::base::Status reject();

  // A cancellation before admission becomes Cancelled immediately.  During an
  // admitted scan it starts terminal cleanup.  A higher-rank failure may still
  // upgrade it to Failed before cleanup completes.
  [[nodiscard]] ksj::base::Status request_cancel();
  [[nodiscard]] ksj::base::Status fail(bool invariant_violation = false);
  [[nodiscard]] ksj::base::Status begin_terminal_cleanup();
  [[nodiscard]] ksj::base::Status finish_terminal_cleanup();

private:
  [[nodiscard]] ksj::base::Status require_state(ScanState expected, std::string_view operation) const;
  [[nodiscard]] ksj::base::Status require_one_of(ScanState first, ScanState second, std::string_view operation) const;
  void raise_cause(TerminalCause cause) noexcept;

  ScanState state_{ScanState::input_candidate};
  TerminalCause terminal_cause_{TerminalCause::none};
  bool admitted_{false};
};

} // namespace ksj::recon::runtime
