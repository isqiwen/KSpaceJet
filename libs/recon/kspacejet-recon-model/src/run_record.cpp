#include "kspacejet/recon/run_record.hpp"

#include <string>
#include <utility>

namespace ksj::recon {
namespace {

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] constexpr bool is_ascii_alpha(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) ||
         (value >= static_cast<unsigned char>('a') && value <= static_cast<unsigned char>('z'));
}

[[nodiscard]] constexpr bool is_ascii_digit(const unsigned char value) noexcept {
  return value >= static_cast<unsigned char>('0') && value <= static_cast<unsigned char>('9');
}

[[nodiscard]] constexpr bool is_run_identity_character(const unsigned char value) noexcept {
  return is_ascii_alpha(value) || is_ascii_digit(value) || value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('_') || value == static_cast<unsigned char>(':') ||
         value == static_cast<unsigned char>('-');
}

[[nodiscard]] bool is_run_identity(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaxRunRecordIdentityLength ||
      !(is_ascii_alpha(static_cast<unsigned char>(value.front())) ||
        is_ascii_digit(static_cast<unsigned char>(value.front())))) {
    return false;
  }
  for (const unsigned char character : value) {
    if (!is_run_identity_character(character)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_cause_code(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaxRunCauseCodeLength ||
      !is_ascii_alpha(static_cast<unsigned char>(value.front()))) {
    return false;
  }
  for (const unsigned char character : value) {
    if (!is_run_identity_character(character)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr bool is_valid(const ExecutionProfile value) noexcept {
  switch (value) {
    case ExecutionProfile::offline_reference:
    case ExecutionProfile::bounded_reconstruction_graph:
    case ExecutionProfile::provider_development:
    case ExecutionProfile::embedded_incremental:
    case ExecutionProfile::isolated_provider_runtime:
      return true;
  }
  return false;
}

[[nodiscard]] constexpr bool is_valid(const RecoveryClass value) noexcept {
  return value == RecoveryClass::fail_stop_no_resume || value == RecoveryClass::source_replay_new_run;
}

[[nodiscard]] constexpr bool is_valid(const RunOutcome value) noexcept {
  switch (value) {
    case RunOutcome::rejected:
    case RunOutcome::cancelled_before_admission:
    case RunOutcome::failed_pre_admission:
    case RunOutcome::cancelled:
    case RunOutcome::failed:
    case RunOutcome::completed:
      return true;
  }
  return false;
}

[[nodiscard]] constexpr bool is_valid(const EgressVisibility value) noexcept {
  return value == EgressVisibility::none || value == EgressVisibility::partial || value == EgressVisibility::flushed;
}

[[nodiscard]] constexpr bool is_valid(const RunCauseKind value) noexcept {
  return value == RunCauseKind::cancellation || value == RunCauseKind::rejection || value == RunCauseKind::failure ||
         value == RunCauseKind::invariant;
}

[[nodiscard]] constexpr bool is_pre_admission_outcome(const RunOutcome outcome) noexcept {
  return outcome == RunOutcome::cancelled_before_admission || outcome == RunOutcome::failed_pre_admission;
}

[[nodiscard]] constexpr bool is_admitted_outcome(const RunOutcome outcome) noexcept {
  return outcome == RunOutcome::cancelled || outcome == RunOutcome::failed || outcome == RunOutcome::completed;
}

[[nodiscard]] constexpr bool primary_cause_matches(const RunOutcome outcome, const RunCauseKind cause) noexcept {
  switch (outcome) {
    case RunOutcome::rejected:
      return cause == RunCauseKind::rejection;
    case RunOutcome::cancelled_before_admission:
    case RunOutcome::cancelled:
      return cause == RunCauseKind::cancellation;
    case RunOutcome::failed_pre_admission:
    case RunOutcome::failed:
      return cause == RunCauseKind::failure || cause == RunCauseKind::invariant;
    case RunOutcome::completed:
      return false;
  }
  return false;
}

[[nodiscard]] constexpr bool secondary_cause_matches(const RunCauseKind primary,
                                                     const RunCauseKind secondary) noexcept {
  switch (primary) {
    case RunCauseKind::cancellation:
      return secondary == RunCauseKind::cancellation;
    case RunCauseKind::rejection:
      return secondary == RunCauseKind::rejection;
    case RunCauseKind::failure:
      return secondary == RunCauseKind::cancellation || secondary == RunCauseKind::failure;
    case RunCauseKind::invariant:
      return secondary == RunCauseKind::cancellation || secondary == RunCauseKind::failure ||
             secondary == RunCauseKind::invariant;
  }
  return false;
}

[[nodiscard]] Result<std::optional<ArtifactDigest>> optional_digest(const std::optional<std::string>& value,
                                                                    const std::string_view field_name) {
  if (!value.has_value()) {
    return std::optional<ArtifactDigest>{};
  }
  auto digest = ArtifactDigest::parse(*value, field_name);
  if (!digest.ok()) {
    return digest.status();
  }
  return std::optional<ArtifactDigest>{std::move(digest).value()};
}

} // namespace

std::string_view to_string(const RecoveryClass value) noexcept {
  switch (value) {
    case RecoveryClass::fail_stop_no_resume:
      return "fail_stop_no_resume";
    case RecoveryClass::source_replay_new_run:
      return "source_replay_new_run";
  }
  return "invalid";
}

std::string_view to_string(const RunOutcome value) noexcept {
  switch (value) {
    case RunOutcome::rejected:
      return "rejected";
    case RunOutcome::cancelled_before_admission:
      return "cancelled_before_admission";
    case RunOutcome::failed_pre_admission:
      return "failed_pre_admission";
    case RunOutcome::cancelled:
      return "cancelled";
    case RunOutcome::failed:
      return "failed";
    case RunOutcome::completed:
      return "completed";
  }
  return "invalid";
}

std::string_view to_string(const EgressVisibility value) noexcept {
  switch (value) {
    case EgressVisibility::none:
      return "none";
    case EgressVisibility::partial:
      return "partial";
    case EgressVisibility::flushed:
      return "flushed";
  }
  return "invalid";
}

std::string_view to_string(const RunCauseKind value) noexcept {
  switch (value) {
    case RunCauseKind::cancellation:
      return "cancellation";
    case RunCauseKind::rejection:
      return "rejection";
    case RunCauseKind::failure:
      return "failure";
    case RunCauseKind::invariant:
      return "invariant";
  }
  return "invalid";
}

Result<RunCause> RunCause::create(const RunCauseSpec& specification, const std::string_view field_name) {
  if (!is_valid(specification.kind)) {
    return validation(std::string(field_name) + ".kind is invalid.");
  }
  if (!is_cause_code(specification.code)) {
    return validation(std::string(field_name) + ".code must be an identifier-like value no longer than " +
                      std::to_string(kMaxRunCauseCodeLength) + " bytes.");
  }
  return RunCause{specification.kind, specification.code};
}

Result<RunRecord> RunRecord::create(const RunRecordSpec& specification) {
  if (!is_run_identity(specification.run_id)) {
    return validation("RunRecord run_id must be an identifier-like value no longer than " +
                      std::to_string(kMaxRunRecordIdentityLength) + " bytes.");
  }
  if (!is_valid(specification.execution_profile)) {
    return validation("RunRecord execution_profile is invalid.");
  }
  if (!is_valid(specification.outcome)) {
    return validation("RunRecord outcome is invalid.");
  }
  if (!is_valid(specification.recovery_class)) {
    return validation("RunRecord recovery_class is invalid.");
  }
  if (!is_valid(specification.egress_visibility)) {
    return validation("RunRecord egress_visibility is invalid.");
  }

  auto plan_digest = optional_digest(specification.execution_plan_digest, "execution_plan_digest");
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }
  auto verification_digest = optional_digest(specification.verification_record_digest, "verification_record_digest");
  if (!verification_digest.ok()) {
    return verification_digest.status();
  }
  auto admission_digest = optional_digest(specification.admission_record_digest, "admission_record_digest");
  if (!admission_digest.ok()) {
    return admission_digest.status();
  }
  if (verification_digest.value().has_value() && !plan_digest.value().has_value()) {
    return validation("verification_record_digest requires execution_plan_digest.");
  }
  if (admission_digest.value().has_value() &&
      (!plan_digest.value().has_value() || !verification_digest.value().has_value())) {
    return validation("admission_record_digest requires execution_plan_digest and verification_record_digest.");
  }
  if (is_pre_admission_outcome(specification.outcome) && admission_digest.value().has_value()) {
    return validation("pre-admission RunRecord outcomes must not carry an admission_record_digest.");
  }
  if (is_admitted_outcome(specification.outcome) &&
      (!plan_digest.value().has_value() || !verification_digest.value().has_value() ||
       !admission_digest.value().has_value())) {
    return validation(
      "admitted RunRecord outcomes require execution plan, verification record, and admission record identities.");
  }

  std::optional<CanonicalQuantity> ordinal;
  if (specification.last_committed_ordinal.has_value()) {
    auto parsed_ordinal = CanonicalQuantity::create(*specification.last_committed_ordinal, "last_committed_ordinal");
    if (!parsed_ordinal.ok()) {
      return parsed_ordinal.status();
    }
    ordinal = std::move(parsed_ordinal).value();
  }
  if (specification.egress_visibility == EgressVisibility::none && ordinal.has_value()) {
    return validation("egress_visibility none must not carry last_committed_ordinal.");
  }
  if (specification.egress_visibility == EgressVisibility::partial && !ordinal.has_value()) {
    return validation("egress_visibility partial requires last_committed_ordinal.");
  }
  if ((specification.outcome == RunOutcome::rejected || is_pre_admission_outcome(specification.outcome)) &&
      (specification.egress_visibility != EgressVisibility::none || ordinal.has_value())) {
    return validation(
      "rejected and pre-admission RunRecord outcomes must have no egress visibility or committed ordinal.");
  }
  if (specification.outcome == RunOutcome::completed && specification.egress_visibility != EgressVisibility::flushed) {
    return validation("completed RunRecord outcome requires flushed egress visibility.");
  }

  if (specification.replay_of_run_id.has_value() && !is_run_identity(*specification.replay_of_run_id)) {
    return validation("replay_of_run_id must be an identifier-like value no longer than " +
                      std::to_string(kMaxRunRecordIdentityLength) + " bytes.");
  }
  if (specification.recovery_class == RecoveryClass::fail_stop_no_resume &&
      specification.replay_of_run_id.has_value()) {
    return validation("fail_stop_no_resume must not carry replay_of_run_id.");
  }
  if (specification.recovery_class == RecoveryClass::source_replay_new_run) {
    if (!specification.replay_of_run_id.has_value()) {
      return validation("source_replay_new_run requires replay_of_run_id.");
    }
    if (*specification.replay_of_run_id == specification.run_id) {
      return validation("source_replay_new_run must use a new run_id distinct from replay_of_run_id.");
    }
  }

  if (specification.secondary_causes.size() > kMaxRunSecondaryCauses) {
    return validation("secondary_causes exceeds the bounded current limit of " +
                      std::to_string(kMaxRunSecondaryCauses) + ".");
  }
  std::optional<RunCause> primary;
  std::vector<RunCause> secondary;
  if (specification.outcome == RunOutcome::completed) {
    if (specification.primary_cause.has_value() || !specification.secondary_causes.empty()) {
      return validation("completed RunRecord must not carry primary or secondary causes.");
    }
  } else {
    if (!specification.primary_cause.has_value()) {
      return validation("non-completed RunRecord requires a primary_cause.");
    }
    auto parsed_primary = RunCause::create(*specification.primary_cause, "primary_cause");
    if (!parsed_primary.ok()) {
      return parsed_primary.status();
    }
    if (!primary_cause_matches(specification.outcome, parsed_primary.value().kind())) {
      return validation("primary_cause.kind is incompatible with RunRecord outcome.");
    }
    primary = std::move(parsed_primary).value();
    secondary.reserve(specification.secondary_causes.size());
    for (std::size_t index = 0; index < specification.secondary_causes.size(); ++index) {
      const std::string cause_field = "secondary_causes[" + std::to_string(index) + "]";
      auto parsed_secondary = RunCause::create(specification.secondary_causes[index], cause_field);
      if (!parsed_secondary.ok()) {
        return parsed_secondary.status();
      }
      if (!secondary_cause_matches(primary->kind(), parsed_secondary.value().kind())) {
        return validation("secondary_causes[" + std::to_string(index) + "] has a higher or incompatible cause class.");
      }
      if (parsed_secondary.value() == *primary) {
        return validation("secondary_causes must not repeat primary_cause.");
      }
      for (const auto& existing : secondary) {
        if (parsed_secondary.value() == existing) {
          return validation("secondary_causes must not contain duplicate causes.");
        }
      }
      secondary.push_back(std::move(parsed_secondary).value());
    }
  }

  return RunRecord{specification.run_id,
                   specification.execution_profile,
                   std::move(plan_digest).value(),
                   std::move(verification_digest).value(),
                   std::move(admission_digest).value(),
                   specification.outcome,
                   specification.recovery_class,
                   specification.egress_visibility,
                   ordinal,
                   std::move(primary),
                   std::move(secondary),
                   specification.replay_of_run_id};
}

} // namespace ksj::recon
