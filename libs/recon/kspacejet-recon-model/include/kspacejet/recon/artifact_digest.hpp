#pragma once

#include "kspacejet/recon/result.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ksj::recon {

// A node's authored configuration is canonical JSON before it crosses the
// planning boundary.  This domain separates its immutable content identity
// from every enclosing PipelineDefinition and ExecutionPlan artifact.
inline constexpr std::string_view kOperatorConfigDigestDomain = "kspacejet:artifact:operator-config";

// A Provider contract is a separately owned immutable interface artifact. Its
// identity is distinct from a node configuration and from a ResolvedPipeline:
// the latter attests the exact contract selected for each authored node.
inline constexpr std::string_view kOperatorContractDigestDomain = "kspacejet:artifact:operator-contract";

// A detached, lower-case sha256 identity used by immutable control-plane
// artifacts and ABI descriptors. It deliberately lives in its own header:
// a TypeDescriptor is frozen into an execution plan, while a descriptor also
// needs to refer to digests without depending on the whole plan model.
class ArtifactDigest final {
public:
  [[nodiscard]] static Result<ArtifactDigest> parse(std::string_view value, std::string_view field_name);

  [[nodiscard]] const std::string& value() const noexcept { return value_; }

  friend bool operator==(const ArtifactDigest&, const ArtifactDigest&) noexcept = default;

private:
  explicit ArtifactDigest(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

// Derive a detached SHA-256 identity from bytes that have already been
// canonicalized by the owning artifact model.  The NUL-delimited domain keeps
// independently-owned artifact kinds from sharing a digest namespace.
[[nodiscard]] Result<ArtifactDigest>
derive_domain_separated_sha256_digest(std::string_view domain, std::string_view canonical_document,
                                      std::string_view field_name = "artifact digest");

// Derive the immutable identity of already-canonical JSON configuration bytes.
// Callers that accept authored JSON must canonicalize it before invoking this
// helper; the runtime uses the same function to avoid a second hash format.
[[nodiscard]] Result<ArtifactDigest>
derive_canonical_config_digest(std::string_view canonical_config,
                               std::string_view field_name = "canonical_config_digest");

} // namespace ksj::recon
