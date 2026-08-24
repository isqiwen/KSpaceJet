#pragma once

#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace ksj::recon::graph {

// Parsing limits are measured in decoded JSON bytes/elements.  They bound
// untrusted artifact admission before a DOM is materialized, rather than
// merely limiting a later validation pass.  A value of zero is invalid for
// every field.
struct JsonParseLimits {
  std::size_t max_document_bytes;
  std::size_t max_depth;
  std::size_t max_array_elements;
  std::size_t max_object_members;
  std::size_t max_string_bytes;
};

// Generic artifact generation can accommodate larger generated plans.  The
// authored PipelineDefinition boundary deliberately uses the tighter limits
// below: it is a control-plane document, never a payload container.
inline constexpr JsonParseLimits kDefaultJsonParseLimits{
  .max_document_bytes = 16U * 1024U * 1024U,
  .max_depth = 64U,
  .max_array_elements = 65'536U,
  .max_object_members = 65'536U,
  .max_string_bytes = 1U * 1024U * 1024U,
};

inline constexpr JsonParseLimits kPipelineDefinitionJsonParseLimits{
  .max_document_bytes = 1U * 1024U * 1024U,
  .max_depth = 32U,
  .max_array_elements = 4'096U,
  .max_object_members = 4'096U,
  .max_string_bytes = 64U * 1024U,
};

// Canonicalization is deliberately constrained to the current JSON value domain:
// object, array, string, boolean, null and signed/unsigned integers exactly
// representable by IEEE-754 binary64.  Floating point JSON is rejected before
// hashing so no platform-specific formatting can alter a pipeline digest.
// The parser rejects duplicate decoded object keys before materializing its
// DOM; nlohmann's default DOM parser would otherwise silently keep one value.
[[nodiscard]] Result<std::string> canonicalize_json(std::string_view document,
                                                    const JsonParseLimits& limits = kDefaultJsonParseLimits);
[[nodiscard]] Result<ArtifactDigest> sha256_digest(std::string_view canonical_document,
                                                   std::string_view field_name = "artifact digest");
[[nodiscard]] Result<ArtifactDigest> canonical_json_digest(std::string_view document,
                                                           std::string_view field_name = "artifact digest");

} // namespace ksj::recon::graph
