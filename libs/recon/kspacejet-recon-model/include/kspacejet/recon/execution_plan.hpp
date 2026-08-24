#pragma once

#include "kspacejet/recon/artifact_digest.hpp"
#include "kspacejet/recon/execution_profile.hpp"
#include "kspacejet/recon/resource_vector.hpp"
#include "kspacejet/recon/type_descriptor.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

struct PlanInputDigestSpec {
  std::string resolved_pipeline;
  std::string scan_facts;
  std::string effective_pipeline_binding;
  std::string target_envelope;
  std::string machine_policy;
};

class PlanInputDigests final {
public:
  [[nodiscard]] const ArtifactDigest& resolved_pipeline() const noexcept { return resolved_pipeline_; }
  [[nodiscard]] const ArtifactDigest& scan_facts() const noexcept { return scan_facts_; }
  [[nodiscard]] const ArtifactDigest& effective_pipeline_binding() const noexcept {
    return effective_pipeline_binding_;
  }
  [[nodiscard]] const ArtifactDigest& target_envelope() const noexcept { return target_envelope_; }
  [[nodiscard]] const ArtifactDigest& machine_policy() const noexcept { return machine_policy_; }

  [[nodiscard]] static PlanInputDigests from_validated(ArtifactDigest resolved_pipeline, ArtifactDigest scan_facts,
                                                       ArtifactDigest effective_pipeline_binding,
                                                       ArtifactDigest target_envelope,
                                                       ArtifactDigest machine_policy) noexcept;

private:
  PlanInputDigests(ArtifactDigest resolved_pipeline, ArtifactDigest scan_facts,
                   ArtifactDigest effective_pipeline_binding, ArtifactDigest target_envelope,
                   ArtifactDigest machine_policy) noexcept
      : resolved_pipeline_(std::move(resolved_pipeline)), scan_facts_(std::move(scan_facts)),
        effective_pipeline_binding_(std::move(effective_pipeline_binding)),
        target_envelope_(std::move(target_envelope)), machine_policy_(std::move(machine_policy)) {}

  ArtifactDigest resolved_pipeline_;
  ArtifactDigest scan_facts_;
  ArtifactDigest effective_pipeline_binding_;
  ArtifactDigest target_envelope_;
  ArtifactDigest machine_policy_;
};

// The plan-level identity of one authored node instance.  Provider contracts
// describe an algorithm; this binding records the exact canonical config that
// selected and parameterized that algorithm for this scan-specific plan.
struct OperatorPlanBindingSpec {
  std::string node_id;
  std::string canonical_config_digest;
};

class OperatorPlanBinding final {
public:
  [[nodiscard]] const std::string& node_id() const noexcept { return node_id_; }
  [[nodiscard]] const ArtifactDigest& canonical_config_digest() const noexcept { return canonical_config_digest_; }

  [[nodiscard]] static OperatorPlanBinding from_validated(std::string node_id,
                                                          ArtifactDigest canonical_config_digest) noexcept;

private:
  OperatorPlanBinding(std::string node_id, ArtifactDigest canonical_config_digest) noexcept
      : node_id_(std::move(node_id)), canonical_config_digest_(std::move(canonical_config_digest)) {}

  std::string node_id_;
  ArtifactDigest canonical_config_digest_;
};

// Accounting rules for the generic synchronous graph's fixed caller-owned
// host-normal slabs. These are abstract resource charges, not C++ object
// layout assertions.
inline constexpr std::string_view kSynchronousBufferPoolStorageAccountingId =
  "kspacejet.buffer-pool-storage/host-normal";
inline constexpr std::string_view kSynchronousDataEdgeStorageAccountingId = "kspacejet.data-edge-storage/fixed-fifo";
inline constexpr std::string_view kSynchronousNormalEoiDrainCancellationFailTerminalPolicy =
  "normal-eoi-drain-cancellation-fail";
inline constexpr Quantity kSynchronousBufferPoolControlChargedBytesPerSlot = 40U;
inline constexpr Quantity kSynchronousDataEdgeControlChargedBytesPerItem = 128U;

[[nodiscard]] Result<Quantity> synchronous_buffer_pool_host_metadata_charged_bytes(Quantity slot_count,
                                                                                   std::string_view field_name);
[[nodiscard]] Result<Quantity> synchronous_buffer_pool_physical_charge_bytes(Quantity slot_count,
                                                                             Quantity payload_capacity_bytes,
                                                                             Quantity metadata_capacity_bytes,
                                                                             std::string_view field_name);
[[nodiscard]] Result<Quantity> synchronous_data_edge_host_metadata_charged_bytes(Quantity max_items,
                                                                                 std::string_view field_name);

// The synchronous graph executor owns one finite, scan-local data plane.  A
// graph endpoint is never inferred from a string convention: ingress, an
// Operator node, and egress are explicit endpoint kinds in the frozen plan.
// The current executor profile deliberately admits one consumer for every
// dynamic edge.  It therefore has no implicit fan-out/refcount semantics.
enum class SynchronousDataEndpointKind : std::uint8_t {
  ingress,
  node,
  egress,
};

enum class SynchronousInputSourceKind : std::uint8_t {
  data_edge,
  calibration_artifact,
};

enum class SynchronousOutputDestinationKind : std::uint8_t {
  data_edge,
  calibration_artifact,
};

// A synchronous firing consumes the head item of every dynamic input edge as
// one cohort.  The current executor has no keyed join table: each head must
// carry the same complete item identity (semantic key hash, order key, and
// ordinal), otherwise the graph fails closed.  This is deliberately frozen in
// the plan instead of being an executor-local convention.
enum class SynchronousDynamicInputJoinPolicy : std::uint8_t {
  exact_item_identity,
};

// The executor reserves its claim vector in fixed plan-owned storage.  Four
// dynamic inputs cover the current k-space + trajectory reconstruction case
// while keeping the transactional claim protocol statically bounded.
inline constexpr Quantity kSynchronousMaximumDynamicInputEdgesPerNode = 4U;

struct SynchronousBufferPoolPlanSpec {
  std::string pool_id;
  SynchronousDataEndpointKind owner_kind{SynchronousDataEndpointKind::node};
  std::string owner_id;
  std::string owner_port_name;
  TypeDescriptor type_descriptor;
  TypeMemoryDomain memory_domain{TypeMemoryDomain::host_normal};
  Quantity slot_count{0U};
  Quantity payload_capacity_bytes{0U};
  Quantity metadata_capacity_bytes{0U};
  Quantity payload_alignment_bytes{0U};
  std::string storage_accounting_id{"kspacejet.buffer-pool-storage/host-normal"};
  Quantity host_metadata_charged_bytes{0U};
  Quantity descriptor_charged_count{0U};
  Quantity physical_charge_bytes{0U};
};

class SynchronousBufferPoolPlan final {
public:
  [[nodiscard]] const std::string& pool_id() const noexcept { return pool_id_; }
  [[nodiscard]] constexpr SynchronousDataEndpointKind owner_kind() const noexcept { return owner_kind_; }
  [[nodiscard]] const std::string& owner_id() const noexcept { return owner_id_; }
  [[nodiscard]] const std::string& owner_port_name() const noexcept { return owner_port_name_; }
  [[nodiscard]] const TypeDescriptor& type_descriptor() const noexcept { return type_descriptor_; }
  [[nodiscard]] constexpr TypeMemoryDomain memory_domain() const noexcept { return memory_domain_; }
  [[nodiscard]] constexpr Quantity slot_count() const noexcept { return slot_count_.value(); }
  [[nodiscard]] constexpr Quantity payload_capacity_bytes() const noexcept { return payload_capacity_bytes_.value(); }
  [[nodiscard]] constexpr Quantity metadata_capacity_bytes() const noexcept { return metadata_capacity_bytes_.value(); }
  [[nodiscard]] constexpr Quantity payload_alignment_bytes() const noexcept { return payload_alignment_bytes_.value(); }
  [[nodiscard]] const std::string& storage_accounting_id() const noexcept { return storage_accounting_id_; }
  [[nodiscard]] constexpr Quantity host_metadata_charged_bytes() const noexcept {
    return host_metadata_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity descriptor_charged_count() const noexcept {
    return descriptor_charged_count_.value();
  }
  [[nodiscard]] constexpr Quantity physical_charge_bytes() const noexcept { return physical_charge_bytes_.value(); }

  [[nodiscard]] static SynchronousBufferPoolPlan
  from_validated(std::string pool_id, SynchronousDataEndpointKind owner_kind, std::string owner_id,
                 std::string owner_port_name, TypeDescriptor type_descriptor, TypeMemoryDomain memory_domain,
                 CanonicalQuantity slot_count, CanonicalQuantity payload_capacity_bytes,
                 CanonicalQuantity metadata_capacity_bytes, CanonicalQuantity payload_alignment_bytes,
                 std::string storage_accounting_id, CanonicalQuantity host_metadata_charged_bytes,
                 CanonicalQuantity descriptor_charged_count, CanonicalQuantity physical_charge_bytes) noexcept;

private:
  SynchronousBufferPoolPlan(std::string pool_id, SynchronousDataEndpointKind owner_kind, std::string owner_id,
                            std::string owner_port_name, TypeDescriptor type_descriptor, TypeMemoryDomain memory_domain,
                            CanonicalQuantity slot_count, CanonicalQuantity payload_capacity_bytes,
                            CanonicalQuantity metadata_capacity_bytes, CanonicalQuantity payload_alignment_bytes,
                            std::string storage_accounting_id, CanonicalQuantity host_metadata_charged_bytes,
                            CanonicalQuantity descriptor_charged_count,
                            CanonicalQuantity physical_charge_bytes) noexcept
      : pool_id_(std::move(pool_id)), owner_kind_(owner_kind), owner_id_(std::move(owner_id)),
        owner_port_name_(std::move(owner_port_name)), type_descriptor_(std::move(type_descriptor)),
        memory_domain_(memory_domain), slot_count_(slot_count), payload_capacity_bytes_(payload_capacity_bytes),
        metadata_capacity_bytes_(metadata_capacity_bytes), payload_alignment_bytes_(payload_alignment_bytes),
        storage_accounting_id_(std::move(storage_accounting_id)),
        host_metadata_charged_bytes_(host_metadata_charged_bytes), descriptor_charged_count_(descriptor_charged_count),
        physical_charge_bytes_(physical_charge_bytes) {}

  std::string pool_id_;
  SynchronousDataEndpointKind owner_kind_;
  std::string owner_id_;
  std::string owner_port_name_;
  TypeDescriptor type_descriptor_;
  TypeMemoryDomain memory_domain_;
  CanonicalQuantity slot_count_;
  CanonicalQuantity payload_capacity_bytes_;
  CanonicalQuantity metadata_capacity_bytes_;
  CanonicalQuantity payload_alignment_bytes_;
  std::string storage_accounting_id_;
  CanonicalQuantity host_metadata_charged_bytes_;
  CanonicalQuantity descriptor_charged_count_;
  CanonicalQuantity physical_charge_bytes_;
};

struct SynchronousDataEdgePlanSpec {
  std::string edge_id;
  std::string source_pool_id;
  SynchronousDataEndpointKind producer_kind{SynchronousDataEndpointKind::node};
  std::string producer_id;
  std::string producer_port_name;
  Quantity producer_abi_port{0U};
  SynchronousDataEndpointKind consumer_kind{SynchronousDataEndpointKind::node};
  std::string consumer_id;
  std::string consumer_port_name;
  Quantity consumer_abi_port{0U};
  TypeDescriptor type_descriptor;
  Quantity max_items{0U};
  Quantity max_logical_bytes{0U};
  std::string storage_accounting_id{"kspacejet.data-edge-storage/fixed-fifo"};
  Quantity host_metadata_charged_bytes{0U};
  Quantity descriptor_charged_count{0U};
  std::string terminal_policy{"normal-eoi-drain-cancellation-fail"};
};

class SynchronousDataEdgePlan final {
public:
  [[nodiscard]] const std::string& edge_id() const noexcept { return edge_id_; }
  [[nodiscard]] const std::string& source_pool_id() const noexcept { return source_pool_id_; }
  [[nodiscard]] constexpr SynchronousDataEndpointKind producer_kind() const noexcept { return producer_kind_; }
  [[nodiscard]] const std::string& producer_id() const noexcept { return producer_id_; }
  [[nodiscard]] const std::string& producer_port_name() const noexcept { return producer_port_name_; }
  [[nodiscard]] constexpr Quantity producer_abi_port() const noexcept { return producer_abi_port_.value(); }
  [[nodiscard]] constexpr SynchronousDataEndpointKind consumer_kind() const noexcept { return consumer_kind_; }
  [[nodiscard]] const std::string& consumer_id() const noexcept { return consumer_id_; }
  [[nodiscard]] const std::string& consumer_port_name() const noexcept { return consumer_port_name_; }
  [[nodiscard]] constexpr Quantity consumer_abi_port() const noexcept { return consumer_abi_port_.value(); }
  [[nodiscard]] const TypeDescriptor& type_descriptor() const noexcept { return type_descriptor_; }
  [[nodiscard]] constexpr Quantity max_items() const noexcept { return max_items_.value(); }
  [[nodiscard]] constexpr Quantity max_logical_bytes() const noexcept { return max_logical_bytes_.value(); }
  [[nodiscard]] const std::string& storage_accounting_id() const noexcept { return storage_accounting_id_; }
  [[nodiscard]] constexpr Quantity host_metadata_charged_bytes() const noexcept {
    return host_metadata_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity descriptor_charged_count() const noexcept {
    return descriptor_charged_count_.value();
  }
  [[nodiscard]] const std::string& terminal_policy() const noexcept { return terminal_policy_; }

  [[nodiscard]] static SynchronousDataEdgePlan
  from_validated(std::string edge_id, std::string source_pool_id, SynchronousDataEndpointKind producer_kind,
                 std::string producer_id, std::string producer_port_name, CanonicalQuantity producer_abi_port,
                 SynchronousDataEndpointKind consumer_kind, std::string consumer_id, std::string consumer_port_name,
                 CanonicalQuantity consumer_abi_port, TypeDescriptor type_descriptor, CanonicalQuantity max_items,
                 CanonicalQuantity max_logical_bytes, std::string storage_accounting_id,
                 CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
                 std::string terminal_policy) noexcept;

private:
  SynchronousDataEdgePlan(std::string edge_id, std::string source_pool_id, SynchronousDataEndpointKind producer_kind,
                          std::string producer_id, std::string producer_port_name, CanonicalQuantity producer_abi_port,
                          SynchronousDataEndpointKind consumer_kind, std::string consumer_id,
                          std::string consumer_port_name, CanonicalQuantity consumer_abi_port,
                          TypeDescriptor type_descriptor, CanonicalQuantity max_items,
                          CanonicalQuantity max_logical_bytes, std::string storage_accounting_id,
                          CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
                          std::string terminal_policy) noexcept
      : edge_id_(std::move(edge_id)), source_pool_id_(std::move(source_pool_id)), producer_kind_(producer_kind),
        producer_id_(std::move(producer_id)), producer_port_name_(std::move(producer_port_name)),
        producer_abi_port_(producer_abi_port), consumer_kind_(consumer_kind), consumer_id_(std::move(consumer_id)),
        consumer_port_name_(std::move(consumer_port_name)), consumer_abi_port_(consumer_abi_port),
        type_descriptor_(std::move(type_descriptor)), max_items_(max_items), max_logical_bytes_(max_logical_bytes),
        storage_accounting_id_(std::move(storage_accounting_id)),
        host_metadata_charged_bytes_(host_metadata_charged_bytes), descriptor_charged_count_(descriptor_charged_count),
        terminal_policy_(std::move(terminal_policy)) {}

  std::string edge_id_;
  std::string source_pool_id_;
  SynchronousDataEndpointKind producer_kind_;
  std::string producer_id_;
  std::string producer_port_name_;
  CanonicalQuantity producer_abi_port_;
  SynchronousDataEndpointKind consumer_kind_;
  std::string consumer_id_;
  std::string consumer_port_name_;
  CanonicalQuantity consumer_abi_port_;
  TypeDescriptor type_descriptor_;
  CanonicalQuantity max_items_;
  CanonicalQuantity max_logical_bytes_;
  std::string storage_accounting_id_;
  CanonicalQuantity host_metadata_charged_bytes_;
  CanonicalQuantity descriptor_charged_count_;
  std::string terminal_policy_;
};

struct SynchronousNodeInputBindingPlanSpec {
  std::string port_name;
  Quantity abi_port{0U};
  SynchronousInputSourceKind source_kind{SynchronousInputSourceKind::data_edge};
  std::string source_id;
  TypeDescriptor type_descriptor;
  // Each current transactional firing claims the head item from this exact
  // port.  The aggregate maximum_input_items is therefore auditable rather
  // than inferred by the executor.
  Quantity maximum_item_count{1U};
};

class SynchronousNodeInputBindingPlan final {
public:
  [[nodiscard]] const std::string& port_name() const noexcept { return port_name_; }
  [[nodiscard]] constexpr Quantity abi_port() const noexcept { return abi_port_.value(); }
  [[nodiscard]] constexpr SynchronousInputSourceKind source_kind() const noexcept { return source_kind_; }
  [[nodiscard]] const std::string& source_id() const noexcept { return source_id_; }
  [[nodiscard]] const TypeDescriptor& type_descriptor() const noexcept { return type_descriptor_; }
  [[nodiscard]] constexpr Quantity maximum_item_count() const noexcept { return maximum_item_count_.value(); }

  [[nodiscard]] static SynchronousNodeInputBindingPlan
  from_validated(std::string port_name, CanonicalQuantity abi_port, SynchronousInputSourceKind source_kind,
                 std::string source_id, TypeDescriptor type_descriptor, CanonicalQuantity maximum_item_count) noexcept;

private:
  SynchronousNodeInputBindingPlan(std::string port_name, CanonicalQuantity abi_port,
                                  SynchronousInputSourceKind source_kind, std::string source_id,
                                  TypeDescriptor type_descriptor, CanonicalQuantity maximum_item_count) noexcept
      : port_name_(std::move(port_name)), abi_port_(abi_port), source_kind_(source_kind),
        source_id_(std::move(source_id)), type_descriptor_(std::move(type_descriptor)),
        maximum_item_count_(maximum_item_count) {}

  std::string port_name_;
  CanonicalQuantity abi_port_;
  SynchronousInputSourceKind source_kind_;
  std::string source_id_;
  TypeDescriptor type_descriptor_;
  CanonicalQuantity maximum_item_count_;
};

struct SynchronousNodeOutputBindingPlanSpec {
  std::string port_name;
  Quantity abi_port{0U};
  SynchronousOutputDestinationKind destination_kind{SynchronousOutputDestinationKind::data_edge};
  std::string destination_id;
  std::string pool_id;
  TypeDescriptor type_descriptor;
  Quantity maximum_item_count{0U};
};

class SynchronousNodeOutputBindingPlan final {
public:
  [[nodiscard]] const std::string& port_name() const noexcept { return port_name_; }
  [[nodiscard]] constexpr Quantity abi_port() const noexcept { return abi_port_.value(); }
  [[nodiscard]] constexpr SynchronousOutputDestinationKind destination_kind() const noexcept {
    return destination_kind_;
  }
  [[nodiscard]] const std::string& destination_id() const noexcept { return destination_id_; }
  [[nodiscard]] const std::string& pool_id() const noexcept { return pool_id_; }
  [[nodiscard]] const TypeDescriptor& type_descriptor() const noexcept { return type_descriptor_; }
  [[nodiscard]] constexpr Quantity maximum_item_count() const noexcept { return maximum_item_count_.value(); }

  [[nodiscard]] static SynchronousNodeOutputBindingPlan
  from_validated(std::string port_name, CanonicalQuantity abi_port, SynchronousOutputDestinationKind destination_kind,
                 std::string destination_id, std::string pool_id, TypeDescriptor type_descriptor,
                 CanonicalQuantity maximum_item_count) noexcept;

private:
  SynchronousNodeOutputBindingPlan(std::string port_name, CanonicalQuantity abi_port,
                                   SynchronousOutputDestinationKind destination_kind, std::string destination_id,
                                   std::string pool_id, TypeDescriptor type_descriptor,
                                   CanonicalQuantity maximum_item_count) noexcept
      : port_name_(std::move(port_name)), abi_port_(abi_port), destination_kind_(destination_kind),
        destination_id_(std::move(destination_id)), pool_id_(std::move(pool_id)),
        type_descriptor_(std::move(type_descriptor)), maximum_item_count_(maximum_item_count) {}

  std::string port_name_;
  CanonicalQuantity abi_port_;
  SynchronousOutputDestinationKind destination_kind_;
  std::string destination_id_;
  std::string pool_id_;
  TypeDescriptor type_descriptor_;
  CanonicalQuantity maximum_item_count_;
};

struct SynchronousFiringPlanSpec {
  Quantity maximum_input_batches{0U};
  Quantity maximum_input_items{0U};
  Quantity maximum_output_grants{0U};
  Quantity maximum_input_payload_bytes{0U};
  Quantity maximum_scratch_bytes{0U};
  Quantity maximum_metadata_bytes{0U};
  Quantity staging_charged_bytes{0U};
  Quantity staging_descriptor_count{0U};
  // Dynamic callback resources. Output pool bytes, node scratch slabs, and
  // persistent ABI staging are owned by frozen graph storage, so they are not
  // charged again by each firing.
  ResourceVectorSpec firing_reservation;
};

class SynchronousFiringPlan final {
public:
  [[nodiscard]] constexpr Quantity maximum_input_batches() const noexcept { return maximum_input_batches_.value(); }
  [[nodiscard]] constexpr Quantity maximum_input_items() const noexcept { return maximum_input_items_.value(); }
  [[nodiscard]] constexpr Quantity maximum_output_grants() const noexcept { return maximum_output_grants_.value(); }
  [[nodiscard]] constexpr Quantity maximum_input_payload_bytes() const noexcept {
    return maximum_input_payload_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity maximum_scratch_bytes() const noexcept { return maximum_scratch_bytes_.value(); }
  [[nodiscard]] constexpr Quantity maximum_metadata_bytes() const noexcept { return maximum_metadata_bytes_.value(); }
  [[nodiscard]] constexpr Quantity staging_charged_bytes() const noexcept { return staging_charged_bytes_.value(); }
  [[nodiscard]] constexpr Quantity staging_descriptor_count() const noexcept {
    return staging_descriptor_count_.value();
  }
  [[nodiscard]] const ResourceVector& firing_reservation() const noexcept { return firing_reservation_; }

  [[nodiscard]] static SynchronousFiringPlan
  from_validated(CanonicalQuantity maximum_input_batches, CanonicalQuantity maximum_input_items,
                 CanonicalQuantity maximum_output_grants, CanonicalQuantity maximum_input_payload_bytes,
                 CanonicalQuantity maximum_scratch_bytes, CanonicalQuantity maximum_metadata_bytes,
                 CanonicalQuantity staging_charged_bytes, CanonicalQuantity staging_descriptor_count,
                 ResourceVector firing_reservation) noexcept;

private:
  SynchronousFiringPlan(CanonicalQuantity maximum_input_batches, CanonicalQuantity maximum_input_items,
                        CanonicalQuantity maximum_output_grants, CanonicalQuantity maximum_input_payload_bytes,
                        CanonicalQuantity maximum_scratch_bytes, CanonicalQuantity maximum_metadata_bytes,
                        CanonicalQuantity staging_charged_bytes, CanonicalQuantity staging_descriptor_count,
                        ResourceVector firing_reservation) noexcept
      : maximum_input_batches_(maximum_input_batches), maximum_input_items_(maximum_input_items),
        maximum_output_grants_(maximum_output_grants), maximum_input_payload_bytes_(maximum_input_payload_bytes),
        maximum_scratch_bytes_(maximum_scratch_bytes), maximum_metadata_bytes_(maximum_metadata_bytes),
        staging_charged_bytes_(staging_charged_bytes), staging_descriptor_count_(staging_descriptor_count),
        firing_reservation_(std::move(firing_reservation)) {}

  CanonicalQuantity maximum_input_batches_;
  CanonicalQuantity maximum_input_items_;
  CanonicalQuantity maximum_output_grants_;
  CanonicalQuantity maximum_input_payload_bytes_;
  CanonicalQuantity maximum_scratch_bytes_;
  CanonicalQuantity maximum_metadata_bytes_;
  CanonicalQuantity staging_charged_bytes_;
  CanonicalQuantity staging_descriptor_count_;
  ResourceVector firing_reservation_;
};

struct SynchronousTerminalPlanSpec {
  Quantity normal_max_output_items{0U};
  Quantity normal_max_output_charged_bytes{0U};
  Quantity normal_max_async_tokens{0U};
  Quantity cancel_max_async_tokens{0U};
};

class SynchronousTerminalPlan final {
public:
  [[nodiscard]] constexpr Quantity normal_max_output_items() const noexcept { return normal_max_output_items_.value(); }
  [[nodiscard]] constexpr Quantity normal_max_output_charged_bytes() const noexcept {
    return normal_max_output_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity normal_max_async_tokens() const noexcept { return normal_max_async_tokens_.value(); }
  [[nodiscard]] constexpr Quantity cancel_max_async_tokens() const noexcept { return cancel_max_async_tokens_.value(); }

  [[nodiscard]] static SynchronousTerminalPlan from_validated(CanonicalQuantity normal_max_output_items,
                                                              CanonicalQuantity normal_max_output_charged_bytes,
                                                              CanonicalQuantity normal_max_async_tokens,
                                                              CanonicalQuantity cancel_max_async_tokens) noexcept;

private:
  SynchronousTerminalPlan(CanonicalQuantity normal_max_output_items, CanonicalQuantity normal_max_output_charged_bytes,
                          CanonicalQuantity normal_max_async_tokens, CanonicalQuantity cancel_max_async_tokens) noexcept
      : normal_max_output_items_(normal_max_output_items),
        normal_max_output_charged_bytes_(normal_max_output_charged_bytes),
        normal_max_async_tokens_(normal_max_async_tokens), cancel_max_async_tokens_(cancel_max_async_tokens) {}

  CanonicalQuantity normal_max_output_items_;
  CanonicalQuantity normal_max_output_charged_bytes_;
  CanonicalQuantity normal_max_async_tokens_;
  CanonicalQuantity cancel_max_async_tokens_;
};

struct SynchronousNodePlanSpec {
  std::string node_id;
  std::string provider_id;
  std::string provider_bundle_digest;
  std::string operator_id;
  SynchronousDynamicInputJoinPolicy dynamic_input_join_policy{SynchronousDynamicInputJoinPolicy::exact_item_identity};
  std::vector<SynchronousNodeInputBindingPlanSpec> inputs;
  std::vector<SynchronousNodeOutputBindingPlanSpec> outputs;
  SynchronousFiringPlanSpec firing;
  SynchronousTerminalPlanSpec terminal;
};

class SynchronousNodePlan final {
public:
  [[nodiscard]] const std::string& node_id() const noexcept { return node_id_; }
  [[nodiscard]] const std::string& provider_id() const noexcept { return provider_id_; }
  [[nodiscard]] const ArtifactDigest& provider_bundle_digest() const noexcept { return provider_bundle_digest_; }
  [[nodiscard]] const std::string& operator_id() const noexcept { return operator_id_; }
  [[nodiscard]] constexpr SynchronousDynamicInputJoinPolicy dynamic_input_join_policy() const noexcept {
    return dynamic_input_join_policy_;
  }
  [[nodiscard]] const std::vector<SynchronousNodeInputBindingPlan>& inputs() const noexcept { return inputs_; }
  [[nodiscard]] const std::vector<SynchronousNodeOutputBindingPlan>& outputs() const noexcept { return outputs_; }
  [[nodiscard]] const SynchronousFiringPlan& firing() const noexcept { return firing_; }
  [[nodiscard]] const SynchronousTerminalPlan& terminal() const noexcept { return terminal_; }

  [[nodiscard]] static SynchronousNodePlan
  from_validated(std::string node_id, std::string provider_id, ArtifactDigest provider_bundle_digest,
                 std::string operator_id, SynchronousDynamicInputJoinPolicy dynamic_input_join_policy,
                 std::vector<SynchronousNodeInputBindingPlan> inputs,
                 std::vector<SynchronousNodeOutputBindingPlan> outputs, SynchronousFiringPlan firing,
                 SynchronousTerminalPlan terminal) noexcept;

private:
  SynchronousNodePlan(std::string node_id, std::string provider_id, ArtifactDigest provider_bundle_digest,
                      std::string operator_id, SynchronousDynamicInputJoinPolicy dynamic_input_join_policy,
                      std::vector<SynchronousNodeInputBindingPlan> inputs,
                      std::vector<SynchronousNodeOutputBindingPlan> outputs, SynchronousFiringPlan firing,
                      SynchronousTerminalPlan terminal) noexcept
      : node_id_(std::move(node_id)), provider_id_(std::move(provider_id)),
        provider_bundle_digest_(std::move(provider_bundle_digest)), operator_id_(std::move(operator_id)),
        dynamic_input_join_policy_(dynamic_input_join_policy), inputs_(std::move(inputs)), outputs_(std::move(outputs)),
        firing_(std::move(firing)), terminal_(std::move(terminal)) {}

  std::string node_id_;
  std::string provider_id_;
  ArtifactDigest provider_bundle_digest_;
  std::string operator_id_;
  SynchronousDynamicInputJoinPolicy dynamic_input_join_policy_;
  std::vector<SynchronousNodeInputBindingPlan> inputs_;
  std::vector<SynchronousNodeOutputBindingPlan> outputs_;
  SynchronousFiringPlan firing_;
  SynchronousTerminalPlan terminal_;
};

struct CalibrationArtifactBindingPlanSpec {
  std::string binding_id;
  std::string producer_node_id;
  std::string producer_port_name;
  Quantity producer_abi_port{0U};
  std::string producer_pool_id;
  TypeDescriptor type_descriptor;
  Quantity host_metadata_charged_bytes{0U};
  Quantity descriptor_charged_count{0U};
};

class CalibrationArtifactBindingPlan final {
public:
  [[nodiscard]] const std::string& binding_id() const noexcept { return binding_id_; }
  [[nodiscard]] const std::string& producer_node_id() const noexcept { return producer_node_id_; }
  [[nodiscard]] const std::string& producer_port_name() const noexcept { return producer_port_name_; }
  [[nodiscard]] constexpr Quantity producer_abi_port() const noexcept { return producer_abi_port_.value(); }
  [[nodiscard]] const std::string& producer_pool_id() const noexcept { return producer_pool_id_; }
  [[nodiscard]] const TypeDescriptor& type_descriptor() const noexcept { return type_descriptor_; }
  [[nodiscard]] constexpr Quantity host_metadata_charged_bytes() const noexcept {
    return host_metadata_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity descriptor_charged_count() const noexcept {
    return descriptor_charged_count_.value();
  }

  [[nodiscard]] static CalibrationArtifactBindingPlan
  from_validated(std::string binding_id, std::string producer_node_id, std::string producer_port_name,
                 CanonicalQuantity producer_abi_port, std::string producer_pool_id, TypeDescriptor type_descriptor,
                 CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count) noexcept;

private:
  CalibrationArtifactBindingPlan(std::string binding_id, std::string producer_node_id, std::string producer_port_name,
                                 CanonicalQuantity producer_abi_port, std::string producer_pool_id,
                                 TypeDescriptor type_descriptor, CanonicalQuantity host_metadata_charged_bytes,
                                 CanonicalQuantity descriptor_charged_count) noexcept
      : binding_id_(std::move(binding_id)), producer_node_id_(std::move(producer_node_id)),
        producer_port_name_(std::move(producer_port_name)), producer_abi_port_(producer_abi_port),
        producer_pool_id_(std::move(producer_pool_id)), type_descriptor_(std::move(type_descriptor)),
        host_metadata_charged_bytes_(host_metadata_charged_bytes), descriptor_charged_count_(descriptor_charged_count) {
  }

  std::string binding_id_;
  std::string producer_node_id_;
  std::string producer_port_name_;
  CanonicalQuantity producer_abi_port_;
  std::string producer_pool_id_;
  TypeDescriptor type_descriptor_;
  CanonicalQuantity host_metadata_charged_bytes_;
  CanonicalQuantity descriptor_charged_count_;
};

struct ExecutionPlanSpec {
  PlanInputDigestSpec inputs;
  std::vector<OperatorPlanBindingSpec> operator_plan_bindings;
  ExecutionProfile execution_profile = ExecutionProfile::bounded_reconstruction_graph;
  // Each node has named input and output bindings; dynamic in-process handoff
  // and static calibration are explicit distinct source kinds.
  std::vector<SynchronousNodePlanSpec> synchronous_node_plans;
  std::vector<SynchronousBufferPoolPlanSpec> synchronous_buffer_pool_plans;
  std::vector<SynchronousDataEdgePlanSpec> synchronous_data_edge_plans;
  std::vector<CalibrationArtifactBindingPlanSpec> calibration_artifact_binding_plans;
  ResourceVectorSpec resource_vector;
  Quantity terminal_occurrences = 0;
  // The compiler's finite set of facts attested by this exact plan artifact.
  // This field is part of the schema payload and therefore participates in its
  // detached digest; it must not be reconstructed only as a serializer-local
  // constant.
  std::vector<std::string> proof_obligations;
};

// Scan-specific, immutable input to the runtime.  It contains compiler output
// only; dynamic admission outcome and observed metrics live elsewhere.
class ExecutionPlan final {
public:
  // `digest` is detached integrity metadata supplied by the content-addressed
  // artifact store/serializer.  It is intentionally not a field in the
  // ExecutionPlan payload and therefore can never be self-hashed.
  [[nodiscard]] static Result<ExecutionPlan> create(ArtifactDigest digest, const ExecutionPlanSpec& specification);

  [[nodiscard]] const ArtifactDigest& digest() const noexcept { return digest_; }
  [[nodiscard]] const PlanInputDigests& inputs() const noexcept { return inputs_; }
  [[nodiscard]] const std::vector<OperatorPlanBinding>& operator_plan_bindings() const noexcept {
    return operator_plan_bindings_;
  }
  [[nodiscard]] constexpr ExecutionProfile execution_profile() const noexcept { return execution_profile_; }
  [[nodiscard]] const std::vector<SynchronousNodePlan>& synchronous_node_plans() const noexcept {
    return synchronous_node_plans_;
  }
  [[nodiscard]] const std::vector<SynchronousBufferPoolPlan>& synchronous_buffer_pool_plans() const noexcept {
    return synchronous_buffer_pool_plans_;
  }
  [[nodiscard]] const std::vector<SynchronousDataEdgePlan>& synchronous_data_edge_plans() const noexcept {
    return synchronous_data_edge_plans_;
  }
  [[nodiscard]] const std::vector<CalibrationArtifactBindingPlan>& calibration_artifact_binding_plans() const noexcept {
    return calibration_artifact_binding_plans_;
  }
  [[nodiscard]] const ResourceVector& resources() const noexcept { return resources_; }
  [[nodiscard]] constexpr Quantity terminal_occurrences() const noexcept { return terminal_occurrences_.value(); }
  [[nodiscard]] const std::vector<std::string>& proof_obligations() const noexcept { return proof_obligations_; }

private:
  [[nodiscard]] static Result<ExecutionPlan>
  create_synchronous(ArtifactDigest digest, const ExecutionPlanSpec& specification, PlanInputDigests inputs,
                     std::vector<OperatorPlanBinding> operator_plan_bindings);

  ExecutionPlan(ArtifactDigest digest, PlanInputDigests inputs, std::vector<OperatorPlanBinding> operator_plan_bindings,
                ExecutionProfile execution_profile, std::vector<SynchronousNodePlan> synchronous_node_plans,
                std::vector<SynchronousBufferPoolPlan> synchronous_buffer_pool_plans,
                std::vector<SynchronousDataEdgePlan> synchronous_data_edge_plans,
                std::vector<CalibrationArtifactBindingPlan> calibration_artifact_binding_plans,
                ResourceVector resources, CanonicalQuantity terminal_occurrences,
                std::vector<std::string> proof_obligations) noexcept
      : digest_(std::move(digest)), inputs_(std::move(inputs)),
        operator_plan_bindings_(std::move(operator_plan_bindings)), execution_profile_(execution_profile),
        synchronous_node_plans_(std::move(synchronous_node_plans)),
        synchronous_buffer_pool_plans_(std::move(synchronous_buffer_pool_plans)),
        synchronous_data_edge_plans_(std::move(synchronous_data_edge_plans)),
        calibration_artifact_binding_plans_(std::move(calibration_artifact_binding_plans)),
        resources_(std::move(resources)), terminal_occurrences_(terminal_occurrences),
        proof_obligations_(std::move(proof_obligations)) {}

  ArtifactDigest digest_;
  PlanInputDigests inputs_;
  std::vector<OperatorPlanBinding> operator_plan_bindings_;
  ExecutionProfile execution_profile_;
  std::vector<SynchronousNodePlan> synchronous_node_plans_;
  std::vector<SynchronousBufferPoolPlan> synchronous_buffer_pool_plans_;
  std::vector<SynchronousDataEdgePlan> synchronous_data_edge_plans_;
  std::vector<CalibrationArtifactBindingPlan> calibration_artifact_binding_plans_;
  ResourceVector resources_;
  CanonicalQuantity terminal_occurrences_;
  std::vector<std::string> proof_obligations_;
};

struct VerificationRecordSpec {
  std::string execution_plan_digest;
  ExecutionProfile execution_profile = ExecutionProfile::bounded_reconstruction_graph;
  ResourceVectorSpec verified_resource_vector;
  Quantity verified_terminal_occurrences = 0;
  std::vector<std::string> verified_obligations;
};

// The independent verifier emits an immutable result about an ExecutionPlan.
// It does not own a second graph, does not make admission decisions, and must
// not be generated by blindly trusting the compiler's witness.
class VerificationRecord final {
public:
  // As for ExecutionPlan, identity is supplied detached from the immutable
  // record payload rather than embedded in its own digest view.
  [[nodiscard]] static Result<VerificationRecord> create(ArtifactDigest digest,
                                                         const VerificationRecordSpec& specification);

  [[nodiscard]] const ArtifactDigest& digest() const noexcept { return digest_; }
  [[nodiscard]] const ArtifactDigest& execution_plan_digest() const noexcept { return execution_plan_digest_; }
  [[nodiscard]] constexpr ExecutionProfile execution_profile() const noexcept { return execution_profile_; }
  [[nodiscard]] const ResourceVector& verified_resource_vector() const noexcept { return verified_resource_vector_; }
  [[nodiscard]] constexpr Quantity verified_terminal_occurrences() const noexcept {
    return verified_terminal_occurrences_.value();
  }
  [[nodiscard]] const std::vector<std::string>& verified_obligations() const noexcept { return verified_obligations_; }

private:
  VerificationRecord(ArtifactDigest digest, ArtifactDigest execution_plan_digest, ExecutionProfile execution_profile,
                     ResourceVector verified_resource_vector, CanonicalQuantity verified_terminal_occurrences,
                     std::vector<std::string> verified_obligations) noexcept
      : digest_(std::move(digest)), execution_plan_digest_(std::move(execution_plan_digest)),
        execution_profile_(execution_profile), verified_resource_vector_(std::move(verified_resource_vector)),
        verified_terminal_occurrences_(verified_terminal_occurrences),
        verified_obligations_(std::move(verified_obligations)) {}

  ArtifactDigest digest_;
  ArtifactDigest execution_plan_digest_;
  ExecutionProfile execution_profile_;
  ResourceVector verified_resource_vector_;
  CanonicalQuantity verified_terminal_occurrences_;
  std::vector<std::string> verified_obligations_;
};

enum class AdmissionOutcome {
  admitted,
  rejected,
};

struct AdmissionRecordSpec {
  std::string execution_plan_digest;
  std::string verification_record_digest;
  AdmissionOutcome outcome = AdmissionOutcome::rejected;
  ResourceVectorSpec reservation;
  std::optional<std::string> reason;
};

// Admission is deliberately separate from the plan/verification record.  A rejected
// record cannot carry a process reservation; pre-admission cancellation is a
// run-manifest event and is not represented as a fake rejected record.
class AdmissionRecord final {
public:
  [[nodiscard]] static Result<AdmissionRecord> create(const AdmissionRecordSpec& specification);

  [[nodiscard]] const ArtifactDigest& execution_plan_digest() const noexcept { return execution_plan_digest_; }
  [[nodiscard]] const ArtifactDigest& verification_record_digest() const noexcept {
    return verification_record_digest_;
  }
  [[nodiscard]] constexpr AdmissionOutcome outcome() const noexcept { return outcome_; }
  [[nodiscard]] const ResourceVector& reservation() const noexcept { return reservation_; }
  [[nodiscard]] const std::optional<std::string>& reason() const noexcept { return reason_; }

private:
  AdmissionRecord(ArtifactDigest execution_plan_digest, ArtifactDigest verification_record_digest,
                  AdmissionOutcome outcome, ResourceVector reservation, std::optional<std::string> reason) noexcept
      : execution_plan_digest_(std::move(execution_plan_digest)),
        verification_record_digest_(std::move(verification_record_digest)), outcome_(outcome),
        reservation_(std::move(reservation)), reason_(std::move(reason)) {}

  ArtifactDigest execution_plan_digest_;
  ArtifactDigest verification_record_digest_;
  AdmissionOutcome outcome_;
  ResourceVector reservation_;
  std::optional<std::string> reason_;
};

} // namespace ksj::recon
