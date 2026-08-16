#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/synchronous_firing_lease.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::recon::runtime {

// One scan-local Provider key-state identity.  The bytes are copied into the
// instance before Provider ABI calls and remain stable until its destruction.
struct ProviderNodeKeyStateIdentity {
  std::vector<ksj::base::byte> semantic_key;
  std::uint64_t placement_key{0U};
  std::uint64_t generation{1U};
  std::uint64_t home_shard{0U};
};

// The facts passed to operator_on_start.  They are explicit rather than
// reconstructed by the runtime so a scan driver cannot accidentally start a
// node with identities from a different admitted scan.
struct ProviderNodeStartFacts {
  ArtifactDigest normalized_scan_facts_digest;
  ArtifactDigest execution_plan_digest;
  std::string run_id;
  std::string scan_instance_id;
  std::uint64_t terminal_epoch{0U};
};

// All fields are one node-instance's frozen runtime inputs.  `canonical_config`
// is already canonical JSON; create() derives its digest and requires the
// matching ExecutionPlan OperatorPlanBinding rather than accepting a caller
// supplied digest.
struct ProviderNodeInstanceConfig {
  std::filesystem::path module_path;
  std::string node_id;
  std::string canonical_config;
  ProviderNodeStartFacts start_facts;
  std::uint64_t execution_context_id{0U};
  std::uint64_t resource_domain_id{0U};
  std::uint64_t max_backend_concurrency{1U};
  std::uint32_t numa_node{0U};
  std::uint32_t device_ordinal{0U};
  ProviderNodeKeyStateIdentity key_state;
};

enum class ProviderNodeLifecycle : std::uint8_t {
  started,
  normal_terminal_completed,
  cancelled,
};

struct ProviderNodeInstanceSnapshot {
  ProviderNodeLifecycle lifecycle{ProviderNodeLifecycle::started};
  bool started{false};
  bool normal_terminal_completed{false};
  bool cancellation_invoked{false};
};

// Owns precisely one loaded Provider operator, execution context, and key
// state for one node in a synchronous ExecutionPlan.  The instance must
// outlive every SynchronousProviderInvocation copied from invocation() and
// every callback that uses one; the class deliberately does not invent an
// asynchronous Provider lifetime model.
class ProviderNodeInstance final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<ProviderNodeInstance>>
  create(const ExecutionPlan& execution_plan, ProviderNodeInstanceConfig config);

  ProviderNodeInstance(const ProviderNodeInstance&) = delete;
  ProviderNodeInstance& operator=(const ProviderNodeInstance&) = delete;
  ProviderNodeInstance(ProviderNodeInstance&&) = delete;
  ProviderNodeInstance& operator=(ProviderNodeInstance&&) = delete;
  ~ProviderNodeInstance();

  // Returns the exact node/config attestation consumed by SynchronousGraphExecutor.
  // It is unavailable after a normal terminal completion or cancellation.
  [[nodiscard]] ksj::base::Result<SynchronousProviderInvocation> invocation() const;

  // Call this only after SynchronousGraphExecutor::try_finish_node returned a
  // normal DONE result for this exact node.  It prevents destructor cleanup
  // from issuing a cancellation after the normal terminal callback.
  [[nodiscard]] ksj::base::Status complete_normal_terminal(const SynchronousFiringResult& terminal_result);

  // Sends one Provider cancellation for an abnormal scan end.  A failed
  // cancellation callback is still considered invoked and is never retried by
  // this object or its destructor.
  [[nodiscard]] ksj::base::Status cancel(std::string_view reason, std::uint64_t cancellation_generation = 1U);

  [[nodiscard]] ProviderNodeInstanceSnapshot snapshot() const noexcept;

private:
  ProviderNodeInstance(ProviderNodeInstanceConfig config, ArtifactDigest canonical_config_digest,
                       std::string operator_id) noexcept;

  [[nodiscard]] ksj::base::Status initialize(const ExecutionPlan& execution_plan, const SynchronousNodePlan& node_plan);
  [[nodiscard]] ksj::base::Status cancel_impl(std::string_view reason, std::uint64_t cancellation_generation) noexcept;
  void destroy_noexcept() noexcept;

  ProviderNodeInstanceConfig config_;
  ArtifactDigest canonical_config_digest_;
  std::string operator_id_;
  ksj::provider::loader::ProviderModule module_{};
  ksj::provider::loader::ProviderLease lease_{};
  ksj_provider_operator* operator_handle_{nullptr};
  ksj_execution_context* execution_context_{nullptr};
  ksj_key_state* key_state_{nullptr};
  bool started_{false};
  bool normal_terminal_completed_{false};
  bool cancellation_invoked_{false};
};

} // namespace ksj::recon::runtime
