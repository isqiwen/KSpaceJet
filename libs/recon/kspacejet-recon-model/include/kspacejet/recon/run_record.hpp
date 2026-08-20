#pragma once

#include "kspacejet/recon/bounded_value.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/execution_profile.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

// Keep externally supplied run identities and the retained failure history
// bounded.  A RunRecord is an audit artifact, not a place for unbounded logs
// or potentially identifying error text.
inline constexpr std::size_t kMaxRunRecordIdentityLength = 255U;
inline constexpr std::size_t kMaxRunCauseCodeLength = 128U;
inline constexpr std::size_t kMaxRunSecondaryCauses = 16U;

// current deliberately does not offer a durable-checkpointed value.  That claim
// requires a journal, checkpoint protocol, and idempotent sink contract that
// are outside this runtime's current guarantees.
enum class RecoveryClass {
  fail_stop_no_resume,
  source_replay_new_run,
};

// These are final RunRecord outcomes, not transient runtime states.
// In particular, cancellation and failure before an admission decision are
// intentionally different from their admitted-run counterparts.
enum class RunOutcome {
  rejected,
  cancelled_before_admission,
  failed_pre_admission,
  cancelled,
  failed,
  completed,
};

enum class EgressVisibility {
  none,
  partial,
  flushed,
};

// A compact, stable cause classification.  `code` carries a bounded,
// identifier-shaped implementation reason; it must not contain arbitrary
// error text, PHI, or an unbounded provider log.
enum class RunCauseKind {
  cancellation,
  rejection,
  failure,
  invariant,
};

[[nodiscard]] std::string_view to_string(RecoveryClass value) noexcept;
[[nodiscard]] std::string_view to_string(RunOutcome value) noexcept;
[[nodiscard]] std::string_view to_string(EgressVisibility value) noexcept;
[[nodiscard]] std::string_view to_string(RunCauseKind value) noexcept;

struct RunCauseSpec {
  RunCauseKind kind = RunCauseKind::failure;
  std::string code;
};

class RunCause final {
public:
  [[nodiscard]] static Result<RunCause> create(const RunCauseSpec& specification, std::string_view field_name);

  [[nodiscard]] constexpr RunCauseKind kind() const noexcept { return kind_; }
  [[nodiscard]] const std::string& code() const noexcept { return code_; }

  friend bool operator==(const RunCause&, const RunCause&) noexcept = default;

private:
  RunCause(RunCauseKind kind, std::string code) noexcept : kind_(kind), code_(std::move(code)) {}

  RunCauseKind kind_;
  std::string code_;
};

struct RunRecordSpec {
  std::string run_id;
  ExecutionProfile execution_profile = ExecutionProfile::bounded_reconstruction_graph;

  // A pre-admission terminal record may legitimately be created before the
  // corresponding plan or verifier artifact exists.  Once a later identity
  // exists, the artifact chain must remain ordered: plan -> verification ->
  // admission.
  std::optional<std::string> execution_plan_digest;
  std::optional<std::string> verification_record_digest;
  std::optional<std::string> admission_record_digest;

  RunOutcome outcome = RunOutcome::failed_pre_admission;
  RecoveryClass recovery_class = RecoveryClass::fail_stop_no_resume;
  EgressVisibility egress_visibility = EgressVisibility::none;
  std::optional<Quantity> last_committed_ordinal;

  // Completed has no primary or secondary cause.  Every other final outcome
  // has one primary cause and at most kMaxRunSecondaryCauses retained causes.
  std::optional<RunCauseSpec> primary_cause;
  std::vector<RunCauseSpec> secondary_causes;

  // A source replay is always a distinct run.  This link records lineage only;
  // it neither creates a checkpoint nor claims cross-run exactly-once output.
  std::optional<std::string> replay_of_run_id;
};

// Immutable terminal audit artifact for one scan run.  It intentionally does
// not report durable recovery or exactly-once semantics: current provides only a
// bounded representation of the final state, egress visibility, and replay
// lineage.
class RunRecord final {
public:
  [[nodiscard]] static Result<RunRecord> create(const RunRecordSpec& specification);

  [[nodiscard]] const std::string& run_id() const noexcept { return run_id_; }
  [[nodiscard]] constexpr ExecutionProfile execution_profile() const noexcept { return execution_profile_; }
  [[nodiscard]] const std::optional<ArtifactDigest>& execution_plan_digest() const noexcept {
    return execution_plan_digest_;
  }
  [[nodiscard]] const std::optional<ArtifactDigest>& verification_record_digest() const noexcept {
    return verification_record_digest_;
  }
  [[nodiscard]] const std::optional<ArtifactDigest>& admission_record_digest() const noexcept {
    return admission_record_digest_;
  }
  [[nodiscard]] constexpr RunOutcome outcome() const noexcept { return outcome_; }
  [[nodiscard]] constexpr RecoveryClass recovery_class() const noexcept { return recovery_class_; }
  [[nodiscard]] constexpr EgressVisibility egress_visibility() const noexcept { return egress_visibility_; }
  [[nodiscard]] std::optional<Quantity> last_committed_ordinal() const noexcept {
    if (!last_committed_ordinal_.has_value()) {
      return std::nullopt;
    }
    return last_committed_ordinal_->value();
  }
  [[nodiscard]] const std::optional<RunCause>& primary_cause() const noexcept { return primary_cause_; }
  [[nodiscard]] const std::vector<RunCause>& secondary_causes() const noexcept { return secondary_causes_; }
  [[nodiscard]] const std::optional<std::string>& replay_of_run_id() const noexcept { return replay_of_run_id_; }

private:
  RunRecord(std::string run_id, ExecutionProfile execution_profile, std::optional<ArtifactDigest> execution_plan_digest,
            std::optional<ArtifactDigest> verification_record_digest,
            std::optional<ArtifactDigest> admission_record_digest, RunOutcome outcome, RecoveryClass recovery_class,
            EgressVisibility egress_visibility, std::optional<CanonicalQuantity> last_committed_ordinal,
            std::optional<RunCause> primary_cause, std::vector<RunCause> secondary_causes,
            std::optional<std::string> replay_of_run_id) noexcept
      : run_id_(std::move(run_id)), execution_profile_(execution_profile),
        execution_plan_digest_(std::move(execution_plan_digest)),
        verification_record_digest_(std::move(verification_record_digest)),
        admission_record_digest_(std::move(admission_record_digest)), outcome_(outcome),
        recovery_class_(recovery_class), egress_visibility_(egress_visibility),
        last_committed_ordinal_(last_committed_ordinal), primary_cause_(std::move(primary_cause)),
        secondary_causes_(std::move(secondary_causes)), replay_of_run_id_(std::move(replay_of_run_id)) {}

  std::string run_id_;
  ExecutionProfile execution_profile_;
  std::optional<ArtifactDigest> execution_plan_digest_;
  std::optional<ArtifactDigest> verification_record_digest_;
  std::optional<ArtifactDigest> admission_record_digest_;
  RunOutcome outcome_;
  RecoveryClass recovery_class_;
  EgressVisibility egress_visibility_;
  std::optional<CanonicalQuantity> last_committed_ordinal_;
  std::optional<RunCause> primary_cause_;
  std::vector<RunCause> secondary_causes_;
  std::optional<std::string> replay_of_run_id_;
};

} // namespace ksj::recon
