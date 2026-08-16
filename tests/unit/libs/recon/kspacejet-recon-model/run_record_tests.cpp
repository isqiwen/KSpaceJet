#include "kspacejet/recon/run_record.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

constexpr auto kPlanDigest = "sha256:1111111111111111111111111111111111111111111111111111111111111111";
constexpr auto kVerificationDigest = "sha256:2222222222222222222222222222222222222222222222222222222222222222";
constexpr auto kAdmissionDigest = "sha256:3333333333333333333333333333333333333333333333333333333333333333";

ksj::recon::RunRecordSpec admitted_record(const ksj::recon::RunOutcome outcome) {
  return {
    .run_id = "scan.20260812.0001",
    .execution_profile = ksj::recon::ExecutionProfile::bounded_online,
    .execution_plan_digest = kPlanDigest,
    .verification_record_digest = kVerificationDigest,
    .admission_record_digest = kAdmissionDigest,
    .outcome = outcome,
    .recovery_class = ksj::recon::RecoveryClass::fail_stop_no_resume,
    .egress_visibility = ksj::recon::EgressVisibility::none,
  };
}

TEST(KSpaceJetRunRecord, AcceptsCompletedFlushedRecordWithArtifactChain) {
  auto specification = admitted_record(ksj::recon::RunOutcome::completed);
  specification.egress_visibility = ksj::recon::EgressVisibility::flushed;
  specification.last_committed_ordinal = 41U;

  const auto record = ksj::recon::RunRecord::create(specification);
  ASSERT_TRUE(record.ok()) << record.status();
  EXPECT_EQ("scan.20260812.0001", record.value().run_id());
  ASSERT_TRUE(record.value().execution_plan_digest().has_value());
  EXPECT_EQ(kPlanDigest, record.value().execution_plan_digest()->value());
  EXPECT_EQ(ksj::recon::RunOutcome::completed, record.value().outcome());
  EXPECT_EQ(ksj::recon::EgressVisibility::flushed, record.value().egress_visibility());
  EXPECT_EQ(41U, record.value().last_committed_ordinal());
  EXPECT_FALSE(record.value().primary_cause().has_value());
  EXPECT_TRUE(record.value().secondary_causes().empty());
}

TEST(KSpaceJetRunRecord, PreservesSourceReplayLineageWithoutClaimingCheckpointResume) {
  auto specification = admitted_record(ksj::recon::RunOutcome::failed);
  specification.run_id = "scan.20260812.0002";
  specification.recovery_class = ksj::recon::RecoveryClass::source_replay_new_run;
  specification.replay_of_run_id = "scan.20260812.0001";
  specification.egress_visibility = ksj::recon::EgressVisibility::partial;
  specification.last_committed_ordinal = 7U;
  specification.primary_cause = {
    .kind = ksj::recon::RunCauseKind::failure,
    .code = "SOURCE_IO_FAILURE",
  };
  specification.secondary_causes = {{
    .kind = ksj::recon::RunCauseKind::cancellation,
    .code = "OPERATOR_CANCEL_REQUESTED",
  }};

  const auto record = ksj::recon::RunRecord::create(specification);
  ASSERT_TRUE(record.ok()) << record.status();
  EXPECT_EQ(ksj::recon::RecoveryClass::source_replay_new_run, record.value().recovery_class());
  ASSERT_TRUE(record.value().replay_of_run_id().has_value());
  EXPECT_EQ("scan.20260812.0001", *record.value().replay_of_run_id());
  ASSERT_TRUE(record.value().primary_cause().has_value());
  EXPECT_EQ(ksj::recon::RunCauseKind::failure, record.value().primary_cause()->kind());
  ASSERT_EQ(1U, record.value().secondary_causes().size());
  EXPECT_EQ(ksj::recon::RunCauseKind::cancellation, record.value().secondary_causes().front().kind());
}

TEST(KSpaceJetRunRecord, SeparatesPreAdmissionCancellationAndFailureFromAdmittedOutcomes) {
  ksj::recon::RunRecordSpec cancelled{
    .run_id = "scan.pre-admission.cancelled",
    .execution_profile = ksj::recon::ExecutionProfile::bounded_online,
    .outcome = ksj::recon::RunOutcome::cancelled_before_admission,
    .primary_cause = {{
      .kind = ksj::recon::RunCauseKind::cancellation,
      .code = "USER_CANCELLED",
    }},
  };
  const auto cancelled_record = ksj::recon::RunRecord::create(cancelled);
  ASSERT_TRUE(cancelled_record.ok()) << cancelled_record.status();
  EXPECT_EQ(ksj::recon::RunOutcome::cancelled_before_admission, cancelled_record.value().outcome());
  EXPECT_FALSE(cancelled_record.value().admission_record_digest().has_value());

  auto failed = cancelled;
  failed.run_id = "scan.pre-admission.failed";
  failed.outcome = ksj::recon::RunOutcome::failed_pre_admission;
  failed.primary_cause = {{
    .kind = ksj::recon::RunCauseKind::failure,
    .code = "VERIFIER_INTERNAL_FAILURE",
  }};
  const auto failed_record = ksj::recon::RunRecord::create(failed);
  ASSERT_TRUE(failed_record.ok()) << failed_record.status();
  EXPECT_EQ(ksj::recon::RunOutcome::failed_pre_admission, failed_record.value().outcome());

  failed.admission_record_digest = kAdmissionDigest;
  const auto invalid_pre_admission = ksj::recon::RunRecord::create(failed);
  EXPECT_FALSE(invalid_pre_admission.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, invalid_pre_admission.status().code());
}

TEST(KSpaceJetRunRecord, RejectsInvalidEgressAndArtifactIdentityCombinations) {
  auto partial_without_ordinal = admitted_record(ksj::recon::RunOutcome::failed);
  partial_without_ordinal.egress_visibility = ksj::recon::EgressVisibility::partial;
  partial_without_ordinal.primary_cause = {{
    .kind = ksj::recon::RunCauseKind::failure,
    .code = "RUNTIME_FAILURE",
  }};
  EXPECT_FALSE(ksj::recon::RunRecord::create(partial_without_ordinal).ok());

  auto missing_admission = partial_without_ordinal;
  missing_admission.egress_visibility = ksj::recon::EgressVisibility::none;
  missing_admission.admission_record_digest.reset();
  EXPECT_FALSE(ksj::recon::RunRecord::create(missing_admission).ok());

  auto malformed_digest = partial_without_ordinal;
  malformed_digest.egress_visibility = ksj::recon::EgressVisibility::none;
  malformed_digest.execution_plan_digest = "sha256:bad";
  EXPECT_FALSE(ksj::recon::RunRecord::create(malformed_digest).ok());
}

TEST(KSpaceJetRunRecord, RequiresAnAlphanumericRunIdentityPrefix) {
  auto specification = admitted_record(ksj::recon::RunOutcome::failed);
  specification.primary_cause = {{
    .kind = ksj::recon::RunCauseKind::failure,
    .code = "RUNTIME_FAILURE",
  }};

  for (const auto* invalid_prefix : {".scan", "_scan", ":scan", "-scan"}) {
    specification.run_id = invalid_prefix;
    EXPECT_FALSE(ksj::recon::RunRecord::create(specification).ok()) << invalid_prefix;
  }
}

TEST(KSpaceJetRunRecord, EnforcesCauseRankAndSupportedRecoveryClasses) {
  auto cancelled = admitted_record(ksj::recon::RunOutcome::cancelled);
  cancelled.primary_cause = {{
    .kind = ksj::recon::RunCauseKind::failure,
    .code = "WRONG_PRIMARY_CLASS",
  }};
  EXPECT_FALSE(ksj::recon::RunRecord::create(cancelled).ok());

  auto failure = admitted_record(ksj::recon::RunOutcome::failed);
  failure.primary_cause = {{
    .kind = ksj::recon::RunCauseKind::failure,
    .code = "RUNTIME_FAILURE",
  }};
  failure.secondary_causes = {{
    .kind = ksj::recon::RunCauseKind::invariant,
    .code = "LEDGER_CONSERVATION",
  }};
  EXPECT_FALSE(ksj::recon::RunRecord::create(failure).ok());

  failure.secondary_causes.clear();
  failure.recovery_class = static_cast<ksj::recon::RecoveryClass>(999);
  EXPECT_FALSE(ksj::recon::RunRecord::create(failure).ok());

  failure.recovery_class = ksj::recon::RecoveryClass::source_replay_new_run;
  failure.replay_of_run_id = failure.run_id;
  EXPECT_FALSE(ksj::recon::RunRecord::create(failure).ok());
}

} // namespace
