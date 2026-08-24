#include "kspacejet/recon/model.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace ksj::recon;

constexpr std::string_view kSourceXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>8</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace><matrixSize><x>64</x><y>64</y><z>1</z></matrixSize><fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm></encodedSpace>
    <reconSpace><matrixSize><x>64</x><y>64</y><z>1</z></matrixSize><fieldOfView_mm><x>220</x><y>220</y><z>5</z></fieldOfView_mm></reconSpace>
    <encodingLimits><average><minimum>0</minimum><maximum>0</maximum><center>0</center></average></encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";

[[nodiscard]] std::string read_fixture(const std::string_view relative_path) {
  const auto path = std::filesystem::path(KSJ_RECON_FIXTURE_DIR) / relative_path;
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << "Unable to open fixture: " << path;
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string replace_once(std::string document, const std::string_view needle,
                                       const std::string_view replacement) {
  const auto position = document.find(needle);
  EXPECT_NE(position, std::string::npos) << "Fixture must contain " << needle;
  if (position == std::string::npos) {
    return document;
  }
  document.replace(position, needle.size(), replacement);
  return document;
}

[[nodiscard]] std::string trim_fixture_line_terminators(std::string document) {
  while (!document.empty() && (document.back() == '\r' || document.back() == '\n')) {
    document.pop_back();
  }
  return document;
}

[[nodiscard]] ScanDescriptor descriptor(const std::string_view xml = kSourceXml) {
  auto parsed = ScanDescriptor::parse_ismrmrd_xml(xml);
  EXPECT_TRUE(parsed.ok()) << parsed.status();
  return std::move(parsed).value();
}

[[nodiscard]] ScanFacts make_scan_facts(const Quantity acquisition_count = 64U,
                                        const Quantity trajectory_dimensions = 0U,
                                        const std::string_view source_xml = kSourceXml) {
  auto facts = ScanFacts::create({.descriptor = descriptor(source_xml),
                                  .source_xml = std::string(source_xml),
                                  .acquisition_count = acquisition_count,
                                  .physical_channel_count = 8U,
                                  .maximum_samples_per_acquisition = 64U,
                                  .trajectory_dimensions = trajectory_dimensions});
  EXPECT_TRUE(facts.ok()) << facts.status();
  return std::move(facts).value();
}

[[nodiscard]] std::string changed_source_xml() {
  std::string changed(kSourceXml);
  const auto field_of_view = changed.find("<x>220</x>");
  EXPECT_NE(std::string::npos, field_of_view);
  changed.replace(field_of_view, std::string_view("<x>220</x>").size(), "<x>221</x>");
  return changed;
}

TEST(KSpaceJetReconModelScanFacts, HasOneRuntimeOwnedCanonicalIdentity) {
  const auto facts = make_scan_facts();
  const auto repeated = make_scan_facts();
  auto source_xml_digest = derive_ismrmrd_source_xml_artifact_digest(kSourceXml);
  ASSERT_TRUE(source_xml_digest.ok()) << source_xml_digest.status();

  EXPECT_EQ(facts.digest(), repeated.digest());
  EXPECT_EQ(facts.canonical_json(), repeated.canonical_json());
  EXPECT_EQ(facts.source_xml_digest(), source_xml_digest.value());
  EXPECT_EQ(64U, facts.acquisition_count());
  EXPECT_EQ(8U, facts.physical_channel_count());
  EXPECT_EQ(64U, facts.maximum_samples_per_acquisition());
  EXPECT_EQ(0U, facts.trajectory_dimensions());
  EXPECT_NE(std::string::npos, facts.canonical_json().find("\"kind\":\"ScanFacts\""));
  EXPECT_NE(std::string::npos, facts.canonical_json().find("\"scan_descriptor_digest\""));
  EXPECT_NE(std::string::npos, facts.canonical_json().find("\"source_xml_digest\""));
  EXPECT_EQ(std::string::npos, facts.canonical_json().find("route"));
  EXPECT_EQ(std::string::npos, facts.canonical_json().find("output"));

  const auto different_acquisition_count = make_scan_facts(65U);
  EXPECT_NE(facts.digest(), different_acquisition_count.digest());

  const auto different_source = make_scan_facts(64U, 0U, changed_source_xml());
  EXPECT_NE(facts.source_xml_digest(), different_source.source_xml_digest());
  EXPECT_NE(facts.digest(), different_source.digest());
}

TEST(KSpaceJetReconModelScanFacts, StrictArtifactParserRejectsAmbiguousOrSubstitutedArtifacts) {
  const auto facts = make_scan_facts();
  auto serialized = serialize_scan_facts_canonical_json(facts);
  ASSERT_TRUE(serialized.ok()) << serialized.status();
  EXPECT_EQ(facts.canonical_json(), serialized.value());

  auto parsed = parse_scan_facts_json(serialized.value(), descriptor(), kSourceXml, facts.digest());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(facts.digest(), parsed.value().digest());

  auto with_schema = serialized.value();
  with_schema.insert(1U, "\"$schema\":\"https://json-schema.org/draft/2020-12/schema\",");
  auto parsed_with_schema = parse_scan_facts_json(with_schema, descriptor(), kSourceXml, facts.digest());
  ASSERT_TRUE(parsed_with_schema.ok()) << parsed_with_schema.status();
  EXPECT_EQ(parsed_with_schema.value().canonical_json(), facts.canonical_json());

  const std::string duplicate =
    serialized.value().substr(0U, serialized.value().size() - 1U) + ",\"kind\":\"ScanFacts\"}";
  EXPECT_FALSE(parse_scan_facts_json(duplicate, descriptor(), kSourceXml, facts.digest()).ok());

  const std::string unknown = serialized.value().substr(0U, serialized.value().size() - 1U) + ",\"unexpected\":true}";
  EXPECT_FALSE(parse_scan_facts_json(unknown, descriptor(), kSourceXml, facts.digest()).ok());

  const std::string noncanonical = " " + serialized.value();
  EXPECT_FALSE(parse_scan_facts_json(noncanonical, descriptor(), kSourceXml, facts.digest()).ok());

  std::string wrong_kind = serialized.value();
  const auto kind = wrong_kind.find("\"kind\":\"ScanFacts\"");
  ASSERT_NE(std::string::npos, kind);
  wrong_kind.replace(kind, std::string_view("\"kind\":\"ScanFacts\"").size(), "\"kind\":\"Other\"");
  EXPECT_FALSE(parse_scan_facts_json(wrong_kind, descriptor(), kSourceXml, facts.digest()).ok());

  std::string tampered = serialized.value();
  const auto count = tampered.find("\"acquisition_count\":64");
  ASSERT_NE(std::string::npos, count);
  tampered.replace(count, std::string_view("\"acquisition_count\":64").size(), "\"acquisition_count\":65");
  const auto tampered_result = parse_scan_facts_json(tampered, descriptor(), kSourceXml, facts.digest());
  ASSERT_FALSE(tampered_result.ok());
  EXPECT_NE(std::string::npos, tampered_result.status().message().find("expected detached"));

  std::string source_digest_tampered = serialized.value();
  const auto source_digest = source_digest_tampered.find("\"source_xml_digest\":\"sha256:");
  ASSERT_NE(std::string::npos, source_digest);
  const auto source_digest_hex = source_digest + std::string_view("\"source_xml_digest\":\"sha256:").size();
  source_digest_tampered[source_digest_hex] = source_digest_tampered[source_digest_hex] == 'f' ? 'e' : 'f';
  EXPECT_FALSE(parse_scan_facts_json(source_digest_tampered, descriptor(), kSourceXml, facts.digest()).ok());

  EXPECT_FALSE(parse_scan_facts_json(serialized.value(), descriptor(), changed_source_xml(), facts.digest()).ok());
}

TEST(KSpaceJetReconModelScanFacts, CheckedInFixturesCoverOwnedInputAndStructuralNegativeCases) {
  const auto facts = make_scan_facts();
  auto valid = trim_fixture_line_terminators(read_fixture("valid/scan-facts-minimal.json"));
  valid = replace_once(valid, "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                       facts.scan_descriptor_digest().value());
  valid = replace_once(valid, "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                       facts.source_xml_digest().value());
  auto parsed = parse_scan_facts_json(valid, descriptor(), kSourceXml, facts.digest());
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(parsed.value().digest(), facts.digest());

  auto raw_xml_field = parse_scan_facts_json(read_fixture("invalid/scan-facts-source-xml-field.json"), descriptor(),
                                             kSourceXml, facts.digest());
  ASSERT_FALSE(raw_xml_field.ok());
  EXPECT_NE(raw_xml_field.status().message().find("source_xml"), std::string::npos);

  auto trajectory_overflow =
    parse_scan_facts_json(trim_fixture_line_terminators(read_fixture("invalid/scan-facts-trajectory-overflow.json")),
                          descriptor(), kSourceXml, facts.digest());
  ASSERT_FALSE(trajectory_overflow.ok());
  EXPECT_NE(trajectory_overflow.status().message().find("trajectory_dimensions"), std::string::npos);
}

TEST(KSpaceJetReconModelScanFacts, RejectsInvalidObservedFactsAndRawXmlMismatches) {
  auto zero_acquisitions = ScanFacts::create({.descriptor = descriptor(),
                                              .source_xml = std::string(kSourceXml),
                                              .acquisition_count = 0U,
                                              .physical_channel_count = 8U,
                                              .maximum_samples_per_acquisition = 64U,
                                              .trajectory_dimensions = 0U});
  EXPECT_FALSE(zero_acquisitions.ok());

  auto zero_channels = ScanFacts::create({.descriptor = descriptor(),
                                          .source_xml = std::string(kSourceXml),
                                          .acquisition_count = 64U,
                                          .physical_channel_count = 0U,
                                          .maximum_samples_per_acquisition = 64U,
                                          .trajectory_dimensions = 0U});
  EXPECT_FALSE(zero_channels.ok());

  auto unsupported_trajectory = ScanFacts::create({.descriptor = descriptor(),
                                                   .source_xml = std::string(kSourceXml),
                                                   .acquisition_count = 64U,
                                                   .physical_channel_count = 8U,
                                                   .maximum_samples_per_acquisition = 64U,
                                                   .trajectory_dimensions = 4U});
  EXPECT_FALSE(unsupported_trajectory.ok());

  auto missing_xml = ScanFacts::create({.descriptor = descriptor(),
                                        .source_xml = {},
                                        .acquisition_count = 64U,
                                        .physical_channel_count = 8U,
                                        .maximum_samples_per_acquisition = 64U,
                                        .trajectory_dimensions = 0U});
  EXPECT_FALSE(missing_xml.ok());

  auto mismatched_xml = ScanFacts::create({.descriptor = descriptor(),
                                           .source_xml = changed_source_xml(),
                                           .acquisition_count = 64U,
                                           .physical_channel_count = 8U,
                                           .maximum_samples_per_acquisition = 64U,
                                           .trajectory_dimensions = 0U});
  EXPECT_FALSE(mismatched_xml.ok());
}

} // namespace
