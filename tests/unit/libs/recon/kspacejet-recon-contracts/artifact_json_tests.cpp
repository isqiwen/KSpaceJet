#include "kspacejet/recon/artifact_json.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr auto kPlanDigest = "sha256:1111111111111111111111111111111111111111111111111111111111111111";
constexpr auto kVerificationDigest = "sha256:2222222222222222222222222222222222222222222222222222222222222222";
constexpr auto kAdmissionDigest = "sha256:3333333333333333333333333333333333333333333333333333333333333333";

[[nodiscard]] ksj::recon::ResourceVectorSpec representative_reservation() {
  return {
    .host_normal_bytes = 4096U,
    .host_pinned_bytes = 17U,
    .host_hugepage_bytes = 0U,
    .shared_host_bytes = 23U,
    .spool_bytes = 0U,
    .transport_bytes = 37U,
    .descriptor_count = 11U,
    .async_token_count = 7U,
    .cpu_leaf_permits = 3U,
    .backend_gang_permits = 0U,
    .provider_private_permits = 2U,
    .io_slots = 1U,
    // ResourceVector normalizes by device id.  The canonical JSON must emit
    // that model order rather than caller insertion order.
    .devices = {{.device_id = "cuda:1", .device_bytes = 256U, .gpu_stream_slots = 2U, .copy_engine_slots = 1U},
                {.device_id = "cuda:0", .device_bytes = 128U, .gpu_stream_slots = 1U, .copy_engine_slots = 0U}},
  };
}

[[nodiscard]] ksj::recon::AdmissionRecord representative_admission_record() {
  auto record = ksj::recon::AdmissionRecord::create({
    .execution_plan_digest = kPlanDigest,
    .verification_record_digest = kVerificationDigest,
    .outcome = ksj::recon::AdmissionOutcome::admitted,
    .reservation = representative_reservation(),
    .reason = "operator capacity reserved",
  });
  EXPECT_TRUE(record.ok()) << record.status();
  return std::move(record).value();
}

[[nodiscard]] ksj::recon::RunRecord representative_run_record() {
  auto record = ksj::recon::RunRecord::create({
    .run_id = "scan.20260812.0002",
    .execution_profile = ksj::recon::ExecutionProfile::bounded_online,
    .execution_plan_digest = kPlanDigest,
    .verification_record_digest = kVerificationDigest,
    .admission_record_digest = kAdmissionDigest,
    .outcome = ksj::recon::RunOutcome::failed,
    .recovery_class = ksj::recon::RecoveryClass::source_replay_new_run,
    .egress_visibility = ksj::recon::EgressVisibility::partial,
    .last_committed_ordinal = 7U,
    .primary_cause = {{.kind = ksj::recon::RunCauseKind::failure, .code = "SOURCE_IO_FAILURE"}},
    .secondary_causes = {{.kind = ksj::recon::RunCauseKind::cancellation, .code = "OPERATOR_CANCEL_REQUESTED"}},
    .replay_of_run_id = "scan.20260812.0001",
  });
  EXPECT_TRUE(record.ok()) << record.status();
  return std::move(record).value();
}

[[nodiscard]] std::string replace_once(std::string document, const std::string_view needle,
                                       const std::string_view replacement) {
  const auto position = document.find(needle);
  EXPECT_NE(std::string::npos, position) << "test fixture must contain " << needle;
  if (position != std::string::npos) {
    document.replace(position, needle.size(), replacement);
  }
  return document;
}

TEST(KSpaceJetReconContractsArtifactJson, AdmissionRecordRoundTripsToStableCanonicalJson) {
  const auto record = representative_admission_record();
  const auto first = ksj::recon::serialize_admission_record_canonical_json(record);
  const auto second = ksj::recon::serialize_admission_record_canonical_json(record);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();

  constexpr std::string_view kExpected =
    R"json({"execution_plan_digest":"sha256:1111111111111111111111111111111111111111111111111111111111111111","kind":"AdmissionRecord","outcome":"admitted","reason":"operator capacity reserved","reservation":{"async_token_count":7,"backend_gang_permits":0,"cpu_leaf_permits":3,"descriptor_count":11,"devices":[{"copy_engine_slots":0,"device_bytes":128,"device_id":"cuda:0","gpu_stream_slots":1},{"copy_engine_slots":1,"device_bytes":256,"device_id":"cuda:1","gpu_stream_slots":2}],"host_hugepage_bytes":0,"host_normal_bytes":4096,"host_pinned_bytes":17,"io_slots":1,"provider_private_permits":2,"shared_host_bytes":23,"spool_bytes":0,"transport_bytes":37},"schema_version":"kspacejet.admission-record/v1","verification_record_digest":"sha256:2222222222222222222222222222222222222222222222222222222222222222"})json";
  EXPECT_EQ(kExpected, first.value());
  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(std::string::npos, first.value().find("\"digest\":"));

  auto parsed = ksj::recon::parse_admission_record_json(first.value());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto reserialized = ksj::recon::serialize_admission_record_canonical_json(parsed.value());
  ASSERT_TRUE(reserialized.ok()) << reserialized.status();
  EXPECT_EQ(first.value(), reserialized.value());
  ASSERT_EQ(2U, parsed.value().reservation().devices().size());
  EXPECT_EQ("cuda:0", parsed.value().reservation().devices().front().device_id());

  std::string schema_decorated(kExpected);
  schema_decorated.insert(1U, "\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",");
  const auto decorated = ksj::recon::parse_admission_record_json(schema_decorated);
  ASSERT_TRUE(decorated.ok()) << decorated.status();
  const auto canonical = ksj::recon::serialize_admission_record_canonical_json(decorated.value());
  ASSERT_TRUE(canonical.ok()) << canonical.status();
  EXPECT_EQ(kExpected, canonical.value());
}

TEST(KSpaceJetReconContractsArtifactJson, RunRecordRoundTripsToStableCanonicalJson) {
  const auto record = representative_run_record();
  const auto first = ksj::recon::serialize_run_record_canonical_json(record);
  const auto second = ksj::recon::serialize_run_record_canonical_json(record);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();

  constexpr std::string_view kExpected =
    R"json({"admission_record_digest":"sha256:3333333333333333333333333333333333333333333333333333333333333333","egress_visibility":"partial","execution_plan_digest":"sha256:1111111111111111111111111111111111111111111111111111111111111111","execution_profile":"bounded-online","kind":"RunRecord","last_committed_ordinal":7,"outcome":"failed","primary_cause":{"code":"SOURCE_IO_FAILURE","kind":"failure"},"recovery_class":"source_replay_new_run","replay_of_run_id":"scan.20260812.0001","run_id":"scan.20260812.0002","schema_version":"kspacejet.run-record/v1","secondary_causes":[{"code":"OPERATOR_CANCEL_REQUESTED","kind":"cancellation"}],"verification_record_digest":"sha256:2222222222222222222222222222222222222222222222222222222222222222"})json";
  EXPECT_EQ(kExpected, first.value());
  EXPECT_EQ(first.value(), second.value());
  EXPECT_EQ(std::string::npos, first.value().find("\"digest\":"));

  const auto parsed = ksj::recon::parse_run_record_json(first.value());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(ksj::recon::RunOutcome::failed, parsed.value().outcome());
  EXPECT_EQ(7U, parsed.value().last_committed_ordinal());
  ASSERT_TRUE(parsed.value().replay_of_run_id().has_value());
  EXPECT_EQ("scan.20260812.0001", *parsed.value().replay_of_run_id());
  const auto reserialized = ksj::recon::serialize_run_record_canonical_json(parsed.value());
  ASSERT_TRUE(reserialized.ok()) << reserialized.status();
  EXPECT_EQ(first.value(), reserialized.value());
}

TEST(KSpaceJetReconContractsArtifactJson, AdmissionReasonUsesUnicodeCodePointSchemaBounds) {
  std::string reason;
  for (std::size_t index = 0U; index < 4096U; ++index) {
    reason.append("\xC3\xA9");
  }
  const auto record = ksj::recon::AdmissionRecord::create({
    .execution_plan_digest = kPlanDigest,
    .verification_record_digest = kVerificationDigest,
    .outcome = ksj::recon::AdmissionOutcome::admitted,
    .reservation = representative_reservation(),
    .reason = reason,
  });
  ASSERT_TRUE(record.ok()) << record.status();
  const auto serialized = ksj::recon::serialize_admission_record_canonical_json(record.value());
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  const auto reparsed = ksj::recon::parse_admission_record_json(serialized.value());
  ASSERT_TRUE(reparsed.ok()) << reparsed.status();
  ASSERT_TRUE(reparsed.value().reason().has_value());
  EXPECT_EQ(reason, *reparsed.value().reason());
}

TEST(KSpaceJetReconContractsArtifactJson, StrictParsersRejectAmbiguousOrSchemaInvalidInput) {
  const auto admission = ksj::recon::serialize_admission_record_canonical_json(representative_admission_record());
  ASSERT_TRUE(admission.ok()) << admission.status();
  const auto duplicate_outcome =
    replace_once(admission.value(), "\"outcome\":\"admitted\"", "\"outcome\":\"admitted\",\"outcome\":\"admitted\"");
  const auto duplicate = ksj::recon::parse_admission_record_json(duplicate_outcome);
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, duplicate.status().code());

  const auto unexpected_self_digest = replace_once(
    admission.value(), "{", "{\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",");
  EXPECT_FALSE(ksj::recon::parse_admission_record_json(unexpected_self_digest).ok());

  const auto run = ksj::recon::serialize_run_record_canonical_json(representative_run_record());
  ASSERT_TRUE(run.ok()) << run.status();
  const auto floating_ordinal =
    replace_once(run.value(), "\"last_committed_ordinal\":7", "\"last_committed_ordinal\":7.5");
  const auto floating = ksj::recon::parse_run_record_json(floating_ordinal);
  EXPECT_FALSE(floating.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, floating.status().code());

  const auto invalid_terminal_invariant =
    replace_once(run.value(), "\"egress_visibility\":\"partial\"", "\"egress_visibility\":\"none\"");
  EXPECT_FALSE(ksj::recon::parse_run_record_json(invalid_terminal_invariant).ok());
}

TEST(KSpaceJetReconContractsArtifactJson, SerializerRejectsRecordsOutsideThePublicParserResourceBounds) {
  auto reservation = representative_reservation();
  reservation.devices.clear();
  for (std::size_t index = 0U; index <= 4096U; ++index) {
    reservation.devices.push_back({.device_id = "device-" + std::to_string(index),
                                   .device_bytes = 1U,
                                   .gpu_stream_slots = 0U,
                                   .copy_engine_slots = 0U});
  }
  const auto record = ksj::recon::AdmissionRecord::create({
    .execution_plan_digest = kPlanDigest,
    .verification_record_digest = kVerificationDigest,
    .outcome = ksj::recon::AdmissionOutcome::admitted,
    .reservation = std::move(reservation),
  });
  ASSERT_TRUE(record.ok()) << record.status();

  const auto serialized = ksj::recon::serialize_admission_record_canonical_json(record.value());
  EXPECT_FALSE(serialized.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, serialized.status().code());
}

} // namespace
