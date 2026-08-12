#pragma once

#include "kspacejet/recon/result.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ksj::recon {

// A detached, lower-case sha256 identity used by immutable control-plane
// artifacts and ABI descriptors.  It deliberately lives in its own header:
// TypeDescriptor is an execution-plan value in M3.7, while a descriptor also
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

} // namespace ksj::recon
