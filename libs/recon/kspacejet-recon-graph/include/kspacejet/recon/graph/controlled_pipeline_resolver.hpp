#pragma once

#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/operator_contract.hpp"
#include "kspacejet/recon/result.hpp"

#include <string>
#include <vector>

namespace ksj::recon::graph {

// This is the host-owned result of selecting executable Provider bundles and
// parsing their Provider-owned OperatorContract artifacts.  It intentionally
// carries no catalog entry, directory, DLL/SO path, or loader handle: those
// belong to the installation/loader boundary, not a user-authored pipeline.
struct ControlledProviderSnapshot final {
  std::string provider_id;
  ArtifactDigest bundle_digest;
  std::vector<OperatorContract> operator_contracts;
};

// The resolver returns the exact Provider/contract set that later planning
// consumes.  It is separate from EffectivePipelineBinding: this freezes
// executable interface identity, whereas EffectivePipelineBinding only
// materializes host-derived scan facts into declared algorithm config keys.
struct ResolvedNodeContract final {
  std::string node_id;
  OperatorContract contract;
};

struct ControlledPipelineResolution final {
  ResolvedPipeline pipeline;
  std::vector<ResolvedNodeContract> node_contracts;
};

// Resolves an authored PipelineDefinition solely against an explicit,
// host-supplied controlled snapshot.  It neither discovers Providers nor
// opens a Provider binary.  Resolver failure is atomic: no partial pipeline
// or contract set is returned.
class ControlledPipelineResolver final {
public:
  [[nodiscard]] static Result<ControlledPipelineResolver> create(std::vector<ControlledProviderSnapshot> providers);

  [[nodiscard]] Result<ControlledPipelineResolution> resolve(const PipelineDefinition& definition) const;

private:
  explicit ControlledPipelineResolver(std::vector<ControlledProviderSnapshot> providers) noexcept
      : providers_(std::move(providers)) {}

  std::vector<ControlledProviderSnapshot> providers_;
};

} // namespace ksj::recon::graph
