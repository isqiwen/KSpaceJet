#pragma once

#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/result.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {
class ScanFacts;
}

namespace ksj::recon::graph {

// One runtime-host-derived, canonical configuration for one resolved
// PipelineDefinition node.  This is an in-memory assembly input, never a CLI
// or PipelineDefinition payload.  The bytes must already be canonical JSON;
// that keeps the value usable by both the compiler and Provider startup without
// a second interpretation or serializer.
struct HostDerivedNodeConfig {
  std::string node_id;
  std::string canonical_config;
};

// Immutable binding between a resolved Provider/Operator graph, facts observed
// from one admitted ISMRMRD scan, and the final configuration for every node.
// It deliberately stores artifact identities rather than paths, handles, or
// mutable runtime policy. Every effective config must preserve the complete
// resolved authored config of its node; it may add only host-derived fields
// named by that node's declared scan_fact_bindings.
class EffectivePipelineBinding final {
public:
  // The normal P2-002 path. It copies the resolved parameter-expanded static
  // config for every node and adds only that node's explicitly declared
  // scan_fact_bindings from the supplied runtime-owned ScanFacts.
  [[nodiscard]] static Result<EffectivePipelineBinding>
  create_from_declared_scan_fact_bindings(const ResolvedPipeline& resolved_pipeline, const ScanFacts& scan_facts);

  // Temporary reference-route assembly bridge retained until P2-007 replaces
  // the three fixed C++ routes with the root --input/--pipeline/--output
  // command. New generic pipeline execution must use the declared-selector
  // factory above, not caller-provided effective config maps.
  [[nodiscard]] static Result<EffectivePipelineBinding>
  create_from_host_derived_configs(const ResolvedPipeline& resolved_pipeline, const ScanFacts& scan_facts,
                                   std::vector<HostDerivedNodeConfig> node_configs);

  // Reads the portable artifact only in the context of its resolved pipeline
  // and scan facts. The artifact never re-introduces caller-chosen parent
  // identities; both references must attest exactly to these supplied values.
  [[nodiscard]] static Result<EffectivePipelineBinding>
  parse_json(std::string_view document, const ResolvedPipeline& resolved_pipeline, const ScanFacts& scan_facts);

  [[nodiscard]] const ArtifactDigest& resolved_pipeline_digest() const noexcept { return resolved_pipeline_digest_; }
  [[nodiscard]] const ArtifactDigest& scan_facts_digest() const noexcept { return scan_facts_digest_; }
  [[nodiscard]] const ArtifactDigest& digest() const noexcept { return digest_; }
  [[nodiscard]] const std::string& canonical_json() const noexcept { return canonical_json_; }
  [[nodiscard]] const std::vector<HostDerivedNodeConfig>& node_configs() const noexcept { return node_configs_; }
  [[nodiscard]] Result<std::string_view> config_for(std::string_view node_id) const;

private:
  EffectivePipelineBinding(ArtifactDigest resolved_pipeline_digest, ArtifactDigest scan_facts_digest,
                           std::vector<HostDerivedNodeConfig> node_configs, std::string canonical_json,
                           ArtifactDigest digest) noexcept
      : resolved_pipeline_digest_(std::move(resolved_pipeline_digest)),
        scan_facts_digest_(std::move(scan_facts_digest)), node_configs_(std::move(node_configs)),
        canonical_json_(std::move(canonical_json)), digest_(std::move(digest)) {}

  ArtifactDigest resolved_pipeline_digest_;
  ArtifactDigest scan_facts_digest_;
  std::vector<HostDerivedNodeConfig> node_configs_;
  std::string canonical_json_;
  ArtifactDigest digest_;
};

} // namespace ksj::recon::graph
