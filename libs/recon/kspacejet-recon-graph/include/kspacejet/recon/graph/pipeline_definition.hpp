#pragma once

#include "kspacejet/recon/execution_profile.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/result.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::recon::graph {

// A graph endpoint is an authored reference only.  The direction and exact
// TypeDescriptor are expanded from the frozen Provider contract during
// resolution/plan compilation; authored nodes never copy a second port
// declaration.
struct NodePortReference {
  std::string node;
  std::string port;

  friend bool operator==(const NodePortReference&, const NodePortReference&) noexcept = default;
};

struct ProviderSelection {
  std::string alias;
  std::string provider_id;
};

struct PipelineNode {
  std::string id;
  std::string provider_alias;
  std::string operator_id;
  // Exact canonical JSON for this node's user-authored configuration.  The
  // Provider may resolve declared parameters but must not inject runtime
  // thread/shard/queue values.
  std::string canonical_config;
};

struct PipelineEdge {
  std::string id;
  NodePortReference from;
  NodePortReference to;
};

struct IngressPort {
  std::string id;
  // Only public ISMRMRD acquisition/waveform types are legal here.  The
  // completed ksj.kspace-frame ABI is an internal typed-node output and must
  // cross a resolved graph edge before a downstream node can consume it.
  std::string type;
  NodePortReference to;
};

struct EgressPort {
  std::string id;
  std::string type;
  NodePortReference from;
};

// CalibrationReady remains an internal dependency event.  This binding names
// the producer output port and every consuming input port, but it is never a
// transport wire message.
struct CalibrationBinding {
  std::string id;
  NodePortReference producer;
  std::vector<NodePortReference> consumers;
};

// Generic same-port merging is intentionally outside the M0/M1 runtime.  The
// authored schema still reserves an explicit binding array so a future
// MergeCapability/MergePlan extension cannot become an implicit edge rule.
struct MergeBinding {
  std::string id;
};

// The hardened Provider loader produces these values after checking a trusted
// bundle, manifest and C ABI.  A PipelineDefinition never stores a DLL path;
// only this resolver may turn an authored Provider reference into an exact
// Provider-bundle runtime input.
struct ResolvedOperator {
  std::string id;
};

struct ResolvedProvider {
  std::string alias;
  std::string provider_id;
  ArtifactDigest bundle_digest;
  std::vector<ResolvedOperator> operators;
};

class PipelineDefinition final {
public:
  [[nodiscard]] static Result<PipelineDefinition> parse_json(std::string_view document);

  [[nodiscard]] const std::string& id() const noexcept { return id_; }
  [[nodiscard]] const std::string& display_name() const noexcept { return display_name_; }
  [[nodiscard]] const std::vector<ExecutionProfile>& allowed_profiles() const noexcept { return allowed_profiles_; }
  [[nodiscard]] const std::vector<ProviderSelection>& providers() const noexcept { return providers_; }
  [[nodiscard]] const std::vector<ProviderSelection>& provider_requirements() const noexcept { return providers_; }
  [[nodiscard]] const std::vector<PipelineNode>& nodes() const noexcept { return nodes_; }
  [[nodiscard]] const std::vector<PipelineEdge>& edges() const noexcept { return edges_; }
  [[nodiscard]] const std::vector<IngressPort>& ingress_ports() const noexcept { return ingress_ports_; }
  [[nodiscard]] const std::vector<EgressPort>& egress_ports() const noexcept { return egress_ports_; }
  [[nodiscard]] const std::vector<CalibrationBinding>& calibration_bindings() const noexcept {
    return calibration_bindings_;
  }
  [[nodiscard]] const std::vector<MergeBinding>& merge_bindings() const noexcept { return merge_bindings_; }
  [[nodiscard]] const std::string& canonical_json() const noexcept { return canonical_json_; }
  [[nodiscard]] const ArtifactDigest& artifact_digest() const noexcept { return artifact_digest_; }
  // Kept as a short convenience for existing callers; it always means the
  // complete artifact identity, never the semantic cache identity.
  [[nodiscard]] const ArtifactDigest& digest() const noexcept { return artifact_digest_; }
  [[nodiscard]] const ArtifactDigest& semantic_digest() const noexcept { return semantic_digest_; }

private:
  PipelineDefinition(std::string id, std::string display_name, std::vector<ExecutionProfile> allowed_profiles,
                     std::vector<ProviderSelection> providers, std::vector<PipelineNode> nodes,
                     std::vector<PipelineEdge> edges, std::vector<IngressPort> ingress_ports,
                     std::vector<EgressPort> egress_ports, std::vector<CalibrationBinding> calibration_bindings,
                     std::vector<MergeBinding> merge_bindings, std::string canonical_json,
                     ArtifactDigest artifact_digest, ArtifactDigest semantic_digest) noexcept
      : id_(std::move(id)), display_name_(std::move(display_name)), allowed_profiles_(std::move(allowed_profiles)),
        providers_(std::move(providers)), nodes_(std::move(nodes)), edges_(std::move(edges)),
        ingress_ports_(std::move(ingress_ports)), egress_ports_(std::move(egress_ports)),
        calibration_bindings_(std::move(calibration_bindings)), merge_bindings_(std::move(merge_bindings)),
        canonical_json_(std::move(canonical_json)), artifact_digest_(std::move(artifact_digest)),
        semantic_digest_(std::move(semantic_digest)) {}

  std::string id_;
  std::string display_name_;
  std::vector<ExecutionProfile> allowed_profiles_;
  std::vector<ProviderSelection> providers_;
  std::vector<PipelineNode> nodes_;
  std::vector<PipelineEdge> edges_;
  std::vector<IngressPort> ingress_ports_;
  std::vector<EgressPort> egress_ports_;
  std::vector<CalibrationBinding> calibration_bindings_;
  std::vector<MergeBinding> merge_bindings_;
  std::string canonical_json_;
  ArtifactDigest artifact_digest_;
  ArtifactDigest semantic_digest_;
};

// Exact resolution is profile-neutral. It freezes the Provider-bundle
// snapshot that a later PlanBuildRequest attests and uses.
class ResolvedPipeline final {
public:
  [[nodiscard]] static Result<ResolvedPipeline> resolve(const PipelineDefinition& definition,
                                                        std::vector<ResolvedProvider> providers);

  [[nodiscard]] const PipelineDefinition& definition() const noexcept { return definition_; }
  [[nodiscard]] const std::vector<ResolvedProvider>& providers() const noexcept { return providers_; }
  [[nodiscard]] const std::string& canonical_json() const noexcept { return canonical_json_; }
  [[nodiscard]] const ArtifactDigest& digest() const noexcept { return digest_; }

private:
  ResolvedPipeline(PipelineDefinition definition, std::vector<ResolvedProvider> providers, std::string canonical_json,
                   ArtifactDigest digest) noexcept
      : definition_(std::move(definition)), providers_(std::move(providers)),
        canonical_json_(std::move(canonical_json)), digest_(std::move(digest)) {}

  PipelineDefinition definition_;
  std::vector<ResolvedProvider> providers_;
  std::string canonical_json_;
  ArtifactDigest digest_;
};

} // namespace ksj::recon::graph
