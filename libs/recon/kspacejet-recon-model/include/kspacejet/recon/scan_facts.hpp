#pragma once

#include "kspacejet/recon/artifact_digest.hpp"
#include "kspacejet/recon/result.hpp"
#include "kspacejet/recon/scan_descriptor.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ksj::recon {

// Runtime-owned, immutable observations about one ISMRMRD input.  This is
// deliberately not a user-editable PipelineDefinition fragment: every field
// is copied or derived by the host while admitting a concrete input scan.
//
// `ScanDescriptor` retains XML-declared geometry and encoding semantics;
// ScanFacts adds only observations that become known while inspecting the
// input.  It never stores an input/output path, Provider identity, route,
// algorithm option, virtual channel count, or runtime resource policy.
// XML identity intentionally preserves the exact validated ISMRMRD XML bytes
// through a canonical JSON-string wrapper.  It is not XML C14N: reformatting
// vendor metadata must produce a distinct raw-input artifact identity.
inline constexpr std::string_view kIsmrmrdSourceXmlArtifactDigestDomain = "kspacejet:artifact:ismrmrd-source-xml";

// Derive the only source-XML identity accepted by ScanFacts.  Callers provide
// raw XML bytes; they cannot inject an arbitrary digest claim.
[[nodiscard]] Result<ArtifactDigest>
derive_ismrmrd_source_xml_artifact_digest(std::string_view source_xml,
                                          std::string_view field_name = "ISMRMRD source XML artifact digest");

struct ScanFactsSpec {
  ScanDescriptor descriptor;
  std::string source_xml;
  Quantity acquisition_count{0U};
  Quantity physical_channel_count{0U};
  Quantity maximum_samples_per_acquisition{0U};
  Quantity trajectory_dimensions{0U};
};

class ScanFacts final {
public:
  [[nodiscard]] static Result<ScanFacts> create(ScanFactsSpec specification);

  [[nodiscard]] const ScanDescriptor& descriptor() const noexcept { return descriptor_; }
  [[nodiscard]] const ArtifactDigest& scan_descriptor_digest() const noexcept { return scan_descriptor_digest_; }
  [[nodiscard]] const ArtifactDigest& source_xml_digest() const noexcept { return source_xml_digest_; }
  [[nodiscard]] constexpr Quantity acquisition_count() const noexcept { return acquisition_count_.value(); }
  [[nodiscard]] constexpr Quantity physical_channel_count() const noexcept { return physical_channel_count_.value(); }
  [[nodiscard]] constexpr Quantity maximum_samples_per_acquisition() const noexcept {
    return maximum_samples_per_acquisition_.value();
  }
  [[nodiscard]] constexpr Quantity trajectory_dimensions() const noexcept { return trajectory_dimensions_.value(); }
  [[nodiscard]] const std::string& canonical_json() const noexcept { return canonical_json_; }
  [[nodiscard]] const ArtifactDigest& digest() const noexcept { return digest_; }

private:
  ScanFacts(ScanDescriptor descriptor, ArtifactDigest scan_descriptor_digest, ArtifactDigest source_xml_digest,
            CanonicalQuantity acquisition_count, CanonicalQuantity physical_channel_count,
            CanonicalQuantity maximum_samples_per_acquisition, CanonicalQuantity trajectory_dimensions,
            std::string canonical_json, ArtifactDigest digest) noexcept
      : descriptor_(std::move(descriptor)), scan_descriptor_digest_(std::move(scan_descriptor_digest)),
        source_xml_digest_(std::move(source_xml_digest)), acquisition_count_(acquisition_count),
        physical_channel_count_(physical_channel_count),
        maximum_samples_per_acquisition_(maximum_samples_per_acquisition),
        trajectory_dimensions_(trajectory_dimensions), canonical_json_(std::move(canonical_json)),
        digest_(std::move(digest)) {}

  ScanDescriptor descriptor_;
  ArtifactDigest scan_descriptor_digest_;
  ArtifactDigest source_xml_digest_;
  CanonicalQuantity acquisition_count_;
  CanonicalQuantity physical_channel_count_;
  CanonicalQuantity maximum_samples_per_acquisition_;
  CanonicalQuantity trajectory_dimensions_;
  std::string canonical_json_;
  ArtifactDigest digest_;
};

} // namespace ksj::recon
