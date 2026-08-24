#pragma once

#include "kspacejet/recon/type_descriptor.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ksj::recon {

// Returns the one exact TypeDescriptor which promises immutable frame payload
// storage plus the completed FrameSlotContext semantic key. It is an
// in-process ABI boundary, not a KSpaceJet wire format.
[[nodiscard]] Result<TypeDescriptor> completed_frame_slot_context_type();

enum class PortDirection {
  input,
  output,
};

struct PortSpec {
  std::string name;
  // The authored contract deliberately names only a readable, registry-owned
  // TypeRef. It must not duplicate structural fields or hand-written digests.
  // OperatorContract::create resolves it to the immutable descriptor below.
  std::string type_ref;
  PortDirection direction = PortDirection::input;
};

// The resolved contract view consumed by graph compilation and verification.
// This is deliberately separate from PortSpec so Provider authors cannot
// smuggle a custom structural descriptor into an authored contract.
struct ResolvedPort final {
  std::string name;
  TypeDescriptor type_descriptor;
  PortDirection direction = PortDirection::input;

  [[nodiscard]] const TypeRef& type_ref() const noexcept { return type_descriptor.type_ref(); }
};

// Provider-owned, immutable interface declaration.  It deliberately stops at
// operator identity and typed ports: scheduling, resource, rate, and topology
// choices are node-owned NodePlanningRequirements in a PlanBuildRequest.
struct OperatorContractSpec {
  std::string operator_id;
  std::vector<PortSpec> ports;
};

class OperatorContract final {
public:
  [[nodiscard]] static Result<OperatorContract> create(const OperatorContractSpec& specification);

  [[nodiscard]] const std::string& operator_id() const noexcept { return operator_id_; }
  [[nodiscard]] const std::vector<ResolvedPort>& ports() const noexcept { return ports_; }
  [[nodiscard]] const ArtifactDigest& artifact_digest() const noexcept { return artifact_digest_; }

private:
  OperatorContract(std::string operator_id, std::vector<ResolvedPort> ports, ArtifactDigest artifact_digest) noexcept
      : operator_id_(std::move(operator_id)), ports_(std::move(ports)), artifact_digest_(std::move(artifact_digest)) {}

  std::string operator_id_;
  std::vector<ResolvedPort> ports_;
  ArtifactDigest artifact_digest_;
};

} // namespace ksj::recon
