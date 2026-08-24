#include "kspacejet/recon/model.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ksj::recon;

[[nodiscard]] ScanDescriptor scan_descriptor(const std::string_view receiver_channels = "8") {
  const std::string xml =
    "<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\">"
    "<experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>"
    "<acquisitionSystemInformation><receiverChannels>" +
    std::string(receiver_channels) +
    "</receiverChannels></acquisitionSystemInformation>"
    "<encoding>"
    "<trajectory>radial</trajectory>"
    "<encodedSpace><matrixSize><x>64</x><y>32</y><z>1</z></matrixSize>"
    "<fieldOfView_mm><x>220</x><y>180</y><z>5</z></fieldOfView_mm></encodedSpace>"
    "<reconSpace><matrixSize><x>128</x><y>96</y><z>1</z></matrixSize>"
    "<fieldOfView_mm><x>220</x><y>180</y><z>5</z></fieldOfView_mm></reconSpace>"
    "<encodingLimits>"
    "<kspace_encoding_step_1><minimum>0</minimum><maximum>31</maximum><center>16</center></kspace_encoding_step_1>"
    "<slice><minimum>0</minimum><maximum>1</maximum><center>0</center></slice>"
    "</encodingLimits>"
    "</encoding>"
    "</ismrmrdHeader>";
  auto parsed = ScanDescriptor::parse_ismrmrd_xml(xml);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] TargetEnvelope target_envelope(const Quantity max_xml_bytes = 16U * 1024U) {
  auto result = TargetEnvelope::create({
    .max_xml_bytes = max_xml_bytes,
    .max_frame_charged_bytes = 4096U,
    .max_image_charged_bytes = 8192U,
    .max_decoder_staging_bytes = 512U,
    .max_samples_per_acquisition = 512U,
    .max_trajectory_dimensions = 3U,
    .max_active_channels = 8U,
    .max_channel_groups = 2U,
    .max_dynamic_keys_per_scan = 4U,
    .max_active_scans = 1U,
    .calibration_horizon_items = 7U,
    .calibration_horizon_charged_bytes = 1024U,
    .arrival_envelope = {.max_acquisitions_per_second = 100U, .max_burst_acquisitions = 9U},
    .sink_service_assumption = {.minimum_drain_items_per_second = 25U,
                                .max_pause_us = 42U,
                                .slow_sink_policy = SlowSinkPolicy::externally_blocked,
                                .transport_staging_bytes = 64U},
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

[[nodiscard]] MachinePolicy machine_policy(const bool reverse_sets = false,
                                           const SchedulerPolicy scheduler = SchedulerPolicy::fair) {
  ResourceVectorSpec resources{
    .host_normal_bytes = 4096U,
    .host_pinned_bytes = 1024U,
    .host_hugepage_bytes = 0U,
    .shared_host_bytes = 512U,
    .spool_bytes = 2048U,
    .transport_bytes = 256U,
    .descriptor_count = 32U,
    .async_token_count = 2U,
    .cpu_leaf_permits = 4U,
    .backend_gang_permits = 2U,
    .provider_private_permits = 1U,
    .io_slots = 3U,
    .devices = {{.device_id = "cuda:1", .device_bytes = 1024U, .gpu_stream_slots = 2U, .copy_engine_slots = 1U},
                {.device_id = "cuda:0", .device_bytes = 2048U, .gpu_stream_slots = 3U, .copy_engine_slots = 2U}},
  };
  auto result = MachinePolicy::create({
    .resource_capacity = {.domains = std::move(resources), .host_total_cap_bytes = 16U * 1024U},
    .numa_domain_count = 1U,
    .allowed_memory_domains = reverse_sets ? std::vector<MemoryDomain>{MemoryDomain::pinned_host, MemoryDomain::host}
                                           : std::vector<MemoryDomain>{MemoryDomain::host, MemoryDomain::pinned_host},
    .allowed_profiles = reverse_sets ? std::vector<ExecutionProfile>{ExecutionProfile::provider_development,
                                                                     ExecutionProfile::bounded_reconstruction_graph}
                                     : std::vector<ExecutionProfile>{ExecutionProfile::bounded_reconstruction_graph,
                                                                     ExecutionProfile::provider_development},
    .scheduler_policy = scheduler,
  });
  EXPECT_TRUE(result.ok()) << result.status();
  return std::move(result).value();
}

TEST(KSpaceJetReconModelPlanningInputArtifacts, ScanDescriptorUsesStableFiniteFieldAndIeee754Identity) {
  const auto descriptor = scan_descriptor();
  const auto first = serialize_scan_descriptor_canonical_json(descriptor);
  const auto second = serialize_scan_descriptor_canonical_json(descriptor);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first.value(), second.value());
  EXPECT_NE(std::string::npos, first.value().find("\"kind\":\"ScanDescriptor\""));
  EXPECT_NE(std::string::npos,
            first.value().find("\"encoded_field_of_view_mm_ieee754_binary64\":{\"x\":\"0x406b800000000000\""));
  EXPECT_NE(std::string::npos, first.value().find("\"y\":\"0x4066800000000000\""));
  EXPECT_NE(std::string::npos, first.value().find("\"z\":\"0x4014000000000000\""));
  EXPECT_EQ(std::string::npos, first.value().find("\"x\":220.0"));

  const auto digest = derive_scan_descriptor_artifact_digest(descriptor);
  const auto changed_digest = derive_scan_descriptor_artifact_digest(scan_descriptor("9"));
  ASSERT_TRUE(digest.ok()) << digest.status();
  ASSERT_TRUE(changed_digest.ok()) << changed_digest.status();
  EXPECT_NE(digest.value(), changed_digest.value());
}

TEST(KSpaceJetReconModelPlanningInputArtifacts, TargetEnvelopeIdentityCoversEveryCurrentValueField) {
  const auto envelope = target_envelope();
  const auto document = serialize_target_envelope_canonical_json(envelope);
  const auto digest = derive_target_envelope_artifact_digest(envelope);
  const auto changed_digest = derive_target_envelope_artifact_digest(target_envelope(16U * 1024U + 1U));
  ASSERT_TRUE(document.ok()) << document.status();
  ASSERT_TRUE(digest.ok()) << digest.status();
  ASSERT_TRUE(changed_digest.ok()) << changed_digest.status();
  EXPECT_NE(std::string::npos, document.value().find("\"kind\":\"TargetEnvelope\""));
  EXPECT_NE(std::string::npos, document.value().find("\"slow_sink_policy\":\"externally_blocked\""));
  EXPECT_NE(digest.value(), changed_digest.value());
}

TEST(KSpaceJetReconModelPlanningInputArtifacts, MachinePolicyCanonicalizesSetOrderAndPreservesAllCapacityInputs) {
  const auto first = machine_policy(false);
  const auto reordered = machine_policy(true);
  const auto first_document = serialize_machine_policy_canonical_json(first);
  const auto reordered_document = serialize_machine_policy_canonical_json(reordered);
  const auto first_digest = derive_machine_policy_artifact_digest(first);
  const auto reordered_digest = derive_machine_policy_artifact_digest(reordered);
  const auto changed_digest = derive_machine_policy_artifact_digest(machine_policy(false, SchedulerPolicy::fifo));
  ASSERT_TRUE(first_document.ok()) << first_document.status();
  ASSERT_TRUE(reordered_document.ok()) << reordered_document.status();
  ASSERT_TRUE(first_digest.ok()) << first_digest.status();
  ASSERT_TRUE(reordered_digest.ok()) << reordered_digest.status();
  ASSERT_TRUE(changed_digest.ok()) << changed_digest.status();
  EXPECT_EQ(first_document.value(), reordered_document.value());
  EXPECT_EQ(first_digest.value(), reordered_digest.value());
  EXPECT_NE(first_digest.value(), changed_digest.value());
  EXPECT_NE(std::string::npos, first_document.value().find("\"device_id\":\"cuda:0\""));
  EXPECT_EQ(std::string::npos, first_document.value().find("\"host_total_bytes\""));
}

TEST(KSpaceJetReconModelPlanningInputArtifacts, GenericDomainHasherPreservesOperatorConfigAndSeparatesDomains) {
  const auto config = derive_canonical_config_digest("{}");
  const auto generic = derive_domain_separated_sha256_digest(kOperatorConfigDigestDomain, "{}");
  const auto other = derive_domain_separated_sha256_digest("kspacejet:artifact:other", "{}");
  ASSERT_TRUE(config.ok()) << config.status();
  ASSERT_TRUE(generic.ok()) << generic.status();
  ASSERT_TRUE(other.ok()) << other.status();
  EXPECT_EQ(config.value(), generic.value());
  EXPECT_NE(generic.value(), other.value());
  EXPECT_FALSE(derive_domain_separated_sha256_digest("", "{}").ok());
}

} // namespace
