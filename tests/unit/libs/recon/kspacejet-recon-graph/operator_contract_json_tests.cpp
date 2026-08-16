#include "kspacejet/recon/graph/operator_contract_json.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr std::string_view kCurrentContract = R"json(
{
  "ports": [
    {
      "type_ref": "ksj.kspace-frame",
      "name": "acquisition",
      "direction": "input"
    },
    {
      "type_ref": "ksj.image-frame",
      "direction": "output",
      "name": "image"
    }
  ],
  "operator_id": "reference_reconstruct",
  "kind": "OperatorContract"
}
)json";

[[nodiscard]] std::string replace_once(std::string document, const std::string_view needle,
                                       const std::string_view replacement) {
  const auto position = document.find(needle);
  EXPECT_NE(position, std::string::npos) << "Expected test fragment was absent: " << needle;
  if (position == std::string::npos) {
    return {};
  }
  document.replace(position, needle.size(), replacement);
  return document;
}

TEST(KSpaceJetReconGraphOperatorContractJson, ParsesCurrentTypedContract) {
  auto parsed = ksj::recon::graph::parse_operator_contract_json(kCurrentContract);
  ASSERT_TRUE(parsed.ok()) << parsed.status();

  EXPECT_EQ(parsed.value().operator_id(), "reference_reconstruct");
  ASSERT_EQ(parsed.value().ports().size(), 2U);
  EXPECT_EQ(parsed.value().ports()[0].name, "acquisition");
  EXPECT_EQ(parsed.value().ports()[0].type_ref().value(), "ksj.kspace-frame");
  EXPECT_EQ(parsed.value().ports()[0].direction, ksj::recon::PortDirection::input);
  EXPECT_EQ(parsed.value().ports()[1].name, "image");
  EXPECT_EQ(parsed.value().ports()[1].type_ref().value(), "ksj.image-frame");
  EXPECT_EQ(parsed.value().ports()[1].direction, ksj::recon::PortDirection::output);
}

TEST(KSpaceJetReconGraphOperatorContractJson, RejectsDeletedPlanningRevisionAndCapabilityFields) {
  const auto stale_execution = replace_once(std::string(kCurrentContract), "\n  \"kind\": \"OperatorContract\"\n",
                                            "\n  \"kind\": \"OperatorContract\",\n  \"execution\": {}\n");
  const auto stale_revision = replace_once(std::string(kCurrentContract), "\n  \"kind\": \"OperatorContract\"\n",
                                           "\n  \"kind\": \"OperatorContract\",\n  \"revision\": 1\n");
  const auto stale_planning = replace_once(std::string(kCurrentContract), "\"direction\": \"input\"",
                                           "\"direction\": \"input\", \"planning\": {}");
  const auto stale_capability = replace_once(std::string(kCurrentContract), "\"direction\": \"input\"",
                                             "\"direction\": \"input\", \"layout_capabilities\": []");
  const auto stale_required = replace_once(std::string(kCurrentContract), "\"direction\": \"input\"",
                                           "\"direction\": \"input\", \"required\": true");

  for (const auto& document : {stale_execution, stale_revision, stale_planning, stale_capability, stale_required}) {
    auto parsed = ksj::recon::graph::parse_operator_contract_json(document);
    ASSERT_FALSE(parsed.ok());
    EXPECT_NE(parsed.status().message().find("unknown field"), std::string::npos);
  }
}

TEST(KSpaceJetReconGraphOperatorContractJson, ResolvesTypeRefsThroughCheckedInRegistry) {
  const auto unknown_type =
    replace_once(std::string(kCurrentContract), "ksj.kspace-frame", "ksj.unknown-contract-payload");
  auto parsed = ksj::recon::graph::parse_operator_contract_json(unknown_type);
  ASSERT_FALSE(parsed.ok());
  EXPECT_NE(parsed.status().message().find("checked-in type registry"), std::string::npos);
}

TEST(KSpaceJetReconGraphOperatorContractJson, RejectsDuplicateJsonKeysAndFloatingPointBeforeValidation) {
  constexpr std::string_view duplicate = R"json(
{"kind":"OperatorContract","kind":"OperatorContract","operator_id":"test","ports":[{"name":"input","type_ref":"ksj.kspace-frame","direction":"input"}]}
)json";
  constexpr std::string_view floating_point = R"json(
{"kind":"OperatorContract","operator_id":"test","ports":[{"name":"input","type_ref":"ksj.kspace-frame","direction":"input","bogus":1.0}]}
)json";

  auto duplicate_result = ksj::recon::graph::parse_operator_contract_json(duplicate);
  ASSERT_FALSE(duplicate_result.ok());
  EXPECT_NE(duplicate_result.status().message().find("duplicate"), std::string::npos);

  auto floating_result = ksj::recon::graph::parse_operator_contract_json(floating_point);
  ASSERT_FALSE(floating_result.ok());
  EXPECT_NE(floating_result.status().message().find("floating-point"), std::string::npos);
}

} // namespace
