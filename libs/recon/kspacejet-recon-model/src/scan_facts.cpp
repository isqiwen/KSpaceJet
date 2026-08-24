#include "kspacejet/recon/scan_facts.hpp"

#include "kspacejet/recon/planning_input_artifacts.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace ksj::recon {
namespace {

inline constexpr std::string_view kScanFactsDigestDomain = "kspacejet:artifact:scan-facts";

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

} // namespace

Result<ArtifactDigest> derive_ismrmrd_source_xml_artifact_digest(const std::string_view source_xml,
                                                                 const std::string_view field_name) {
  if (source_xml.empty()) {
    return validation("ISMRMRD source XML must not be empty.");
  }
  try {
    // Do not normalize XML syntax or vendor extensions.  The JSON wrapper has
    // one deterministic representation for the exact validated XML bytes.
    const nlohmann::json xml_string{std::string(source_xml)};
    const std::string canonical_document =
      "{\"ismrmrd_xml\":" + xml_string.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict) + "}";
    return derive_domain_separated_sha256_digest(kIsmrmrdSourceXmlArtifactDigestDomain, canonical_document, field_name);
  } catch (const nlohmann::json::exception& exception) {
    return validation("ISMRMRD source XML cannot be represented as canonical UTF-8 JSON: " +
                      std::string(exception.what()));
  }
}

Result<ScanFacts> ScanFacts::create(ScanFactsSpec specification) {
  if (specification.source_xml.empty()) {
    return validation("ScanFacts.source_xml must not be empty.");
  }
  if (specification.descriptor.source_xml_bytes() != specification.source_xml.size()) {
    return validation("ScanFacts.descriptor.source_xml_bytes must match the runtime-owned source_xml byte count.");
  }
  if (specification.acquisition_count == 0U) {
    return validation("ScanFacts.acquisition_count must be greater than zero.");
  }
  if (specification.physical_channel_count == 0U) {
    return validation("ScanFacts.physical_channel_count must be greater than zero.");
  }
  if (specification.maximum_samples_per_acquisition == 0U) {
    return validation("ScanFacts.maximum_samples_per_acquisition must be greater than zero.");
  }
  // ISMRMRD acquisition trajectory dimensions are bounded to the physical
  // x/y/z coordinate domain.  Zero is valid for Cartesian acquisitions.
  if (specification.trajectory_dimensions > 3U) {
    return validation("ScanFacts.trajectory_dimensions must be in [0,3].");
  }

  auto acquisition_count = CanonicalQuantity::create(specification.acquisition_count, "ScanFacts.acquisition_count");
  if (!acquisition_count.ok()) {
    return acquisition_count.status();
  }
  auto physical_channel_count =
    CanonicalQuantity::create(specification.physical_channel_count, "ScanFacts.physical_channel_count");
  if (!physical_channel_count.ok()) {
    return physical_channel_count.status();
  }
  auto maximum_samples_per_acquisition = CanonicalQuantity::create(specification.maximum_samples_per_acquisition,
                                                                   "ScanFacts.maximum_samples_per_acquisition");
  if (!maximum_samples_per_acquisition.ok()) {
    return maximum_samples_per_acquisition.status();
  }
  auto trajectory_dimensions =
    CanonicalQuantity::create(specification.trajectory_dimensions, "ScanFacts.trajectory_dimensions");
  if (!trajectory_dimensions.ok()) {
    return trajectory_dimensions.status();
  }

  auto descriptor_digest = derive_scan_descriptor_artifact_digest(specification.descriptor);
  if (!descriptor_digest.ok()) {
    return descriptor_digest.status();
  }
  auto source_descriptor = ScanDescriptor::parse_ismrmrd_xml(specification.source_xml);
  if (!source_descriptor.ok()) {
    return source_descriptor.status();
  }
  auto source_descriptor_digest = derive_scan_descriptor_artifact_digest(source_descriptor.value());
  if (!source_descriptor_digest.ok()) {
    return source_descriptor_digest.status();
  }
  if (source_descriptor_digest.value() != descriptor_digest.value()) {
    return validation("ScanFacts.descriptor does not match the runtime-owned source_xml.");
  }
  auto source_xml_digest =
    derive_ismrmrd_source_xml_artifact_digest(specification.source_xml, "ScanFacts source XML digest");
  if (!source_xml_digest.ok()) {
    return source_xml_digest.status();
  }

  const std::string canonical_json =
    "{\"acquisition_count\":" + std::to_string(specification.acquisition_count) + ",\"kind\":\"ScanFacts\"" +
    ",\"maximum_samples_per_acquisition\":" + std::to_string(specification.maximum_samples_per_acquisition) +
    ",\"physical_channel_count\":" + std::to_string(specification.physical_channel_count) +
    ",\"scan_descriptor_digest\":\"" + descriptor_digest.value().value() + "\",\"source_xml_digest\":\"" +
    source_xml_digest.value().value() +
    "\",\"trajectory_dimensions\":" + std::to_string(specification.trajectory_dimensions) + "}";
  auto digest = derive_domain_separated_sha256_digest(kScanFactsDigestDomain, canonical_json, "ScanFacts digest");
  if (!digest.ok()) {
    return digest.status();
  }

  return ScanFacts{std::move(specification.descriptor),
                   std::move(descriptor_digest).value(),
                   std::move(source_xml_digest).value(),
                   std::move(acquisition_count).value(),
                   std::move(physical_channel_count).value(),
                   std::move(maximum_samples_per_acquisition).value(),
                   std::move(trajectory_dimensions).value(),
                   canonical_json,
                   std::move(digest).value()};
}

} // namespace ksj::recon
