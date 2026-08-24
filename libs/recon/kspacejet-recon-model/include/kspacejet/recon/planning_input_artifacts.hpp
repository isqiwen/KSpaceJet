#pragma once

#include "kspacejet/recon/artifact_digest.hpp"
#include "kspacejet/recon/planning_inputs.hpp"
#include "kspacejet/recon/scan_descriptor.hpp"

#include <string>
#include <string_view>

namespace ksj::recon {

// These domains identify the validated C++ planning-input values used by the
// compiler.  They deliberately do not make a deployment policy file or the
// legacy structural schemas the authority for TargetEnvelope/MachinePolicy.
inline constexpr std::string_view kScanDescriptorArtifactDigestDomain = "kspacejet:artifact:scan-descriptor";
inline constexpr std::string_view kTargetEnvelopeArtifactDigestDomain = "kspacejet:artifact:target-envelope";
inline constexpr std::string_view kMachinePolicyArtifactDigestDomain = "kspacejet:artifact:machine-policy";

// Serialize exactly the immutable ScanDescriptor value model.  Field-of-view
// values are lower-case hexadecimal IEEE-754 binary64 bit strings, rather than
// JSON floating-point numbers, so identity is independent of JSON number
// formatting and locale.
[[nodiscard]] Result<std::string> serialize_scan_descriptor_canonical_json(const ScanDescriptor& descriptor);
[[nodiscard]] Result<ArtifactDigest> derive_scan_descriptor_artifact_digest(const ScanDescriptor& descriptor);

// Serialize exactly the validated local TargetEnvelope value model used by
// planning.  This is compiler identity/provenance only; it is not a parser for
// a deployment-owned policy artifact.
[[nodiscard]] Result<std::string> serialize_target_envelope_canonical_json(const TargetEnvelope& envelope);
[[nodiscard]] Result<ArtifactDigest> derive_target_envelope_artifact_digest(const TargetEnvelope& envelope);

// Serialize exactly the validated local MachinePolicy value model used by
// planning.  Set-valued profile and memory-domain members are emitted in
// canonical lexical order; resource devices use their stable device identity.
[[nodiscard]] Result<std::string> serialize_machine_policy_canonical_json(const MachinePolicy& policy);
[[nodiscard]] Result<ArtifactDigest> derive_machine_policy_artifact_digest(const MachinePolicy& policy);

} // namespace ksj::recon
