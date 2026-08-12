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

inline constexpr std::string_view kExecutionPlanSchemaVersion = "kspacejet.execution-plan/v1";
inline constexpr std::string_view kVerificationRecordSchemaVersion = "kspacejet.verification-record/v1";
inline constexpr std::string_view kAdmissionRecordSchemaVersion = "kspacejet.admission-record/v1";

struct PlanInputDigestSpec {
  std::string resolved_pipeline;
  std::string scan_descriptor;
  std::string target_envelope;
  std::string machine_policy;
  std::vector<std::string> provider_contracts;
};

class PlanInputDigests final {
public:
  [[nodiscard]] const ArtifactDigest& resolved_pipeline() const noexcept { return resolved_pipeline_; }
  [[nodiscard]] const ArtifactDigest& scan_descriptor() const noexcept { return scan_descriptor_; }
  [[nodiscard]] const ArtifactDigest& target_envelope() const noexcept { return target_envelope_; }
  [[nodiscard]] const ArtifactDigest& machine_policy() const noexcept { return machine_policy_; }
  [[nodiscard]] const std::vector<ArtifactDigest>& provider_contracts() const noexcept { return provider_contracts_; }

  [[nodiscard]] static PlanInputDigests from_validated(ArtifactDigest resolved_pipeline, ArtifactDigest scan_descriptor,
                                                       ArtifactDigest target_envelope, ArtifactDigest machine_policy,
                                                       std::vector<ArtifactDigest> provider_contracts) noexcept;

private:
  PlanInputDigests(ArtifactDigest resolved_pipeline, ArtifactDigest scan_descriptor, ArtifactDigest target_envelope,
                   ArtifactDigest machine_policy, std::vector<ArtifactDigest> provider_contracts) noexcept
      : resolved_pipeline_(std::move(resolved_pipeline)), scan_descriptor_(std::move(scan_descriptor)),
        target_envelope_(std::move(target_envelope)), machine_policy_(std::move(machine_policy)),
        provider_contracts_(std::move(provider_contracts)) {}

  ArtifactDigest resolved_pipeline_;
  ArtifactDigest scan_descriptor_;
  ArtifactDigest target_envelope_;
  ArtifactDigest machine_policy_;
  std::vector<ArtifactDigest> provider_contracts_;
};

// The v1 KeySlot table deliberately separates the finite semantic key domain
// from the smaller reusable physical-slot pool.  The mapping is a dense
// mixed-radix bijection over `dense_dimensions`; it never silently falls back
// to an envelope-sized sparse/lazy table.  A later runtime implementation uses
// the frozen slot_count and generation semantics to prevent slot ABA and to
// reject events for completed keys.
inline constexpr std::string_view kDenseMixedRadixKeySlotMappingAlgorithmId = "dense-mixed-radix/v1";
inline constexpr std::string_view kDenseKeySlotStorageAccountingId = "kspacejet.key-slot-storage/dense-v1";
inline constexpr std::string_view kMonotonicU64KeySlotGenerationPolicy = "monotonic-u64/v1";
inline constexpr std::string_view kCompletedOnlyKeySlotEvictionPolicy = "completed-only";
inline constexpr std::string_view kFailKeySlotLateEventPolicy = "fail";
inline constexpr Quantity kInitialKeySlotGeneration = 1U;
inline constexpr Quantity kDenseKeySlotSemanticRecordChargedBytes = 16U;
inline constexpr Quantity kDenseKeySlotPhysicalSlotChargedBytes = 16U;

// This is an abstract accounting rule, not a C++ object-layout assertion.
// It charges one durable semantic/tombstone record for every key in the dense
// domain and one reusable physical-slot/freelist/generation record for every
// live slot.  Provider per-key state is deliberately outside this formula and
// is charged by the compiler in the Provider's declared memory domain.
[[nodiscard]] Result<Quantity>
dense_key_slot_host_metadata_charged_bytes(Quantity key_domain_bound, Quantity slot_count, std::string_view field_name);

struct DenseKeySlotDimensionSpec {
  std::string field;
  Quantity minimum = 0;
  Quantity cardinality = 0;
};

class DenseKeySlotDimension final {
public:
  [[nodiscard]] const std::string& field() const noexcept { return field_; }
  [[nodiscard]] constexpr Quantity minimum() const noexcept { return minimum_.value(); }
  [[nodiscard]] constexpr Quantity cardinality() const noexcept { return cardinality_.value(); }

  [[nodiscard]] static DenseKeySlotDimension from_validated(std::string field, CanonicalQuantity minimum,
                                                            CanonicalQuantity cardinality) noexcept;

private:
  DenseKeySlotDimension(std::string field, CanonicalQuantity minimum, CanonicalQuantity cardinality) noexcept
      : field_(std::move(field)), minimum_(minimum), cardinality_(cardinality) {}

  std::string field_;
  CanonicalQuantity minimum_;
  CanonicalQuantity cardinality_;
};

struct KeySlotTablePlanSpec {
  std::string node_id;
  std::vector<DenseKeySlotDimensionSpec> dense_dimensions;
  std::string mapping_algorithm_id = std::string(kDenseMixedRadixKeySlotMappingAlgorithmId);
  std::string storage_accounting_id = std::string(kDenseKeySlotStorageAccountingId);
  Quantity key_domain_bound = 0;
  Quantity max_distinct_keys = 0;
  Quantity max_live_keys = 0;
  Quantity slot_count = 0;
  std::string generation_policy = std::string(kMonotonicU64KeySlotGenerationPolicy);
  Quantity initial_generation = kInitialKeySlotGeneration;
  bool seal_on_completion = true;
  std::string eviction_policy = std::string(kCompletedOnlyKeySlotEvictionPolicy);
  std::string late_event_policy = std::string(kFailKeySlotLateEventPolicy);
  Quantity host_metadata_charged_bytes = 0;
  Quantity max_items_per_activation = 0;
  Quantity max_charged_bytes_per_activation = 0;
};

class KeySlotTablePlan final {
public:
  [[nodiscard]] const std::string& node_id() const noexcept { return node_id_; }
  [[nodiscard]] const std::vector<DenseKeySlotDimension>& dense_dimensions() const noexcept {
    return dense_dimensions_;
  }
  [[nodiscard]] const std::string& mapping_algorithm_id() const noexcept { return mapping_algorithm_id_; }
  [[nodiscard]] const std::string& storage_accounting_id() const noexcept { return storage_accounting_id_; }
  [[nodiscard]] constexpr Quantity key_domain_bound() const noexcept { return key_domain_bound_.value(); }
  [[nodiscard]] constexpr Quantity max_distinct_keys() const noexcept { return max_distinct_keys_.value(); }
  [[nodiscard]] constexpr Quantity max_live_keys() const noexcept { return max_live_keys_.value(); }
  [[nodiscard]] constexpr Quantity slot_count() const noexcept { return slot_count_.value(); }
  [[nodiscard]] const std::string& generation_policy() const noexcept { return generation_policy_; }
  [[nodiscard]] constexpr Quantity initial_generation() const noexcept { return initial_generation_.value(); }
  [[nodiscard]] constexpr bool seal_on_completion() const noexcept { return seal_on_completion_; }
  [[nodiscard]] const std::string& eviction_policy() const noexcept { return eviction_policy_; }
  [[nodiscard]] const std::string& late_event_policy() const noexcept { return late_event_policy_; }
  [[nodiscard]] constexpr Quantity host_metadata_charged_bytes() const noexcept {
    return host_metadata_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity max_items_per_activation() const noexcept {
    return max_items_per_activation_.value();
  }
  [[nodiscard]] constexpr Quantity max_charged_bytes_per_activation() const noexcept {
    return max_charged_bytes_per_activation_.value();
  }

  [[nodiscard]] static KeySlotTablePlan from_validated(
    std::string node_id, std::vector<DenseKeySlotDimension> dense_dimensions, std::string mapping_algorithm_id,
    std::string storage_accounting_id, CanonicalQuantity key_domain_bound, CanonicalQuantity max_distinct_keys,
    CanonicalQuantity max_live_keys, CanonicalQuantity slot_count, std::string generation_policy,
    CanonicalQuantity initial_generation, bool seal_on_completion, std::string eviction_policy,
    std::string late_event_policy, CanonicalQuantity host_metadata_charged_bytes,
    CanonicalQuantity max_items_per_activation, CanonicalQuantity max_charged_bytes_per_activation) noexcept;

private:
  KeySlotTablePlan(std::string node_id, std::vector<DenseKeySlotDimension> dense_dimensions,
                   std::string mapping_algorithm_id, std::string storage_accounting_id,
                   CanonicalQuantity key_domain_bound, CanonicalQuantity max_distinct_keys,
                   CanonicalQuantity max_live_keys, CanonicalQuantity slot_count, std::string generation_policy,
                   CanonicalQuantity initial_generation, bool seal_on_completion, std::string eviction_policy,
                   std::string late_event_policy, CanonicalQuantity host_metadata_charged_bytes,
                   CanonicalQuantity max_items_per_activation,
                   CanonicalQuantity max_charged_bytes_per_activation) noexcept
      : node_id_(std::move(node_id)), dense_dimensions_(std::move(dense_dimensions)),
        mapping_algorithm_id_(std::move(mapping_algorithm_id)),
        storage_accounting_id_(std::move(storage_accounting_id)), key_domain_bound_(key_domain_bound),
        max_distinct_keys_(max_distinct_keys), max_live_keys_(max_live_keys), slot_count_(slot_count),
        generation_policy_(std::move(generation_policy)), initial_generation_(initial_generation),
        seal_on_completion_(seal_on_completion), eviction_policy_(std::move(eviction_policy)),
        late_event_policy_(std::move(late_event_policy)), host_metadata_charged_bytes_(host_metadata_charged_bytes),
        max_items_per_activation_(max_items_per_activation),
        max_charged_bytes_per_activation_(max_charged_bytes_per_activation) {}

  std::string node_id_;
  std::vector<DenseKeySlotDimension> dense_dimensions_;
  std::string mapping_algorithm_id_;
  std::string storage_accounting_id_;
  CanonicalQuantity key_domain_bound_;
  CanonicalQuantity max_distinct_keys_;
  CanonicalQuantity max_live_keys_;
  CanonicalQuantity slot_count_;
  std::string generation_policy_;
  CanonicalQuantity initial_generation_;
  bool seal_on_completion_ = true;
  std::string eviction_policy_;
  std::string late_event_policy_;
  CanonicalQuantity host_metadata_charged_bytes_;
  CanonicalQuantity max_items_per_activation_;
  CanonicalQuantity max_charged_bytes_per_activation_;
};

// M3 freezes only one reorder model: a finite Cartesian output-order domain
// whose ordinal is the dense mixed-radix index of XML-derived index fields.
// The first dimension is most significant and the last dimension advances
// fastest.  There is deliberately no Provider-defined ordinal source or
// sparse/lazy fallback in this v1 plan model.
inline constexpr std::string_view kDenseCartesianReorderMappingAlgorithmId = "dense-cartesian-ordinal/v1";
inline constexpr std::string_view kDenseCartesianReorderStorageAccountingId =
  "kspacejet.reorder-storage/dense-cartesian-v1";
// M3 ordinal assignment is host-owned.  It binds the dense Cartesian
// projection to the semantic key of a *completed* FrameSlotContext; an
// acquisition callback, Provider firing counter, or Provider-supplied ordinal
// is not an admissible source for this plan version.
inline constexpr std::string_view kCompletedFrameSlotContextSemanticKeyOrdinalBindingId =
  "completed-frame-slot-context-semantic-key/v1";
inline constexpr std::string_view kM3CompletedFrameSlotBindingProofObligation = "PO-07.m3_completed_frame_slot_binding";
// XML encoding limits define the expected Cartesian tuple universe.  They do
// not prove that the acquisition stream will actually contain every tuple.
// M3 therefore freezes a fail-closed runtime obligation: the future host
// frame-completion binding and fixed reorder buffer must observe one completed
// FrameSlotContext for every ordinal by EndOfInput, or fail the scan.  M3 does
// not yet attest that completion provenance.  A future strict profile may
// replace this assumption with a separately attested occurrence/schedule
// artifact; it must not silently weaken this v1 policy.
inline constexpr std::string_view kStrictDenseAllTuplesReorderOccurrencePolicy = "strict-dense-all-tuples-eoi-fail";
inline constexpr std::string_view kM3StrictDenseAllTuplesEoiRuntimeAssumption = "RA-01.m3_strict_dense_all_tuples_eoi";
// VerificationRecord uses its own stable verdict identifiers.  These do not
// replace the plan's PO-07/RA-01 obligations: the former records what the
// independent verifier checked, while the latter remains part of the plan
// artifact consumed by the runtime.
inline constexpr std::string_view kM3CompletedFrameSlotBindingVerificationObligation =
  "M3.completed_frame_slot_ordinal_binding";
inline constexpr std::string_view kM3StrictDenseAllTuplesEoiVerificationObligation =
  "M3.strict_dense_all_tuples_eoi_runtime_assumption";
inline constexpr std::string_view kNextExpectedOnlyReorderPublishPolicy = "next-expected-only";
inline constexpr std::string_view kFailReorderEndOfInputPolicy = "fail";
inline constexpr Quantity kFirstExpectedReorderOrdinal = 0U;
inline constexpr Quantity kDenseCartesianReorderOrdinalRecordChargedBytes = 16U;
inline constexpr Quantity kDenseCartesianReorderBufferedSlotChargedBytes = 16U;
// M3.7 reserves a fixed caller-slab sidecar for a move-only immutable buffer
// handle at every ahead position.  This is an abstract ABI/storage charge,
// not a claim about `sizeof(ImmutableBufferHandle)`: the future runtime must
// statically prove its concrete sidecar representation fits this frozen
// budget before binding a plan.
inline constexpr Quantity kDenseCartesianReorderHandleSidecarChargedBytes = 64U;

// This abstract charge owns one durable state/seen record for every ordinal
// in the closed dense domain, one physical buffered-slot record, and one
// fixed immutable-handle sidecar for every concurrently retained ahead item.
// `max_ahead_charged_bytes` is a logical credit only in M3.7: the backing
// payload is charged exactly once by its BufferPoolPlan.
[[nodiscard]] Result<Quantity> dense_cartesian_reorder_host_metadata_charged_bytes(Quantity ordinal_domain_bound,
                                                                                   Quantity max_ahead_items,
                                                                                   std::string_view field_name);

struct DenseCartesianOrdinalDimensionSpec {
  std::string field;
  Quantity minimum = 0;
  Quantity cardinality = 0;
};

class DenseCartesianOrdinalDimension final {
public:
  [[nodiscard]] const std::string& field() const noexcept { return field_; }
  [[nodiscard]] constexpr Quantity minimum() const noexcept { return minimum_.value(); }
  [[nodiscard]] constexpr Quantity cardinality() const noexcept { return cardinality_.value(); }

  [[nodiscard]] static DenseCartesianOrdinalDimension from_validated(std::string field, CanonicalQuantity minimum,
                                                                     CanonicalQuantity cardinality) noexcept;

private:
  DenseCartesianOrdinalDimension(std::string field, CanonicalQuantity minimum, CanonicalQuantity cardinality) noexcept
      : field_(std::move(field)), minimum_(minimum), cardinality_(cardinality) {}

  std::string field_;
  CanonicalQuantity minimum_;
  CanonicalQuantity cardinality_;
};

struct ReorderPlanSpec {
  std::string node_id;
  // M3 has exactly one node-local output order domain.  It is explicitly
  // serialized so the runtime never infers which node state owns an ordinal.
  std::string order_domain_id;
  // Fixed M3 host binding.  This is intentionally not an extensible
  // Provider-selected source: ordinals derive only from a completed
  // FrameSlotContext semantic key.
  std::string ordinal_binding_id = std::string(kCompletedFrameSlotContextSemanticKeyOrdinalBindingId);
  // Exact operator input port carrying the completed FrameSlot whose semantic
  // key is used by ordinal_binding_id.  This prevents an artifact from
  // claiming a one-frame/one-output mapping without naming its frame ABI
  // boundary.
  std::string completed_frame_input_port;
  // The selected Provider output stream is part of the frozen plan rather
  // than inferred from a node's potentially many output ports.
  std::string ordered_output_port;
  Quantity outputs_per_ordinal = 0;
  // The full reservation bound for the one M3 OutputEnvelope at an ordinal.
  // It is independently derived from the selected PortRateSpec.
  Quantity charged_bytes_per_ordinal = 0;
  std::vector<DenseCartesianOrdinalDimensionSpec> ordinal_dimensions;
  std::string mapping_algorithm_id = std::string(kDenseCartesianReorderMappingAlgorithmId);
  std::string storage_accounting_id = std::string(kDenseCartesianReorderStorageAccountingId);
  Quantity ordinal_domain_bound = 0;
  Quantity first_expected_ordinal = kFirstExpectedReorderOrdinal;
  Quantity last_expected_ordinal = 0;
  Quantity max_ahead_items = 0;
  // Logical bytes retained by ahead-order credit.  In a plan-bound M3.7
  // path this does not create a second physical payload charge: the source
  // BufferPoolPlan owns it.  A legacy opaque M3 ReorderPlan retains the
  // historical physical reservation only in an ExecutionPlan with no M3.7
  // BufferPoolPlan/DataEdgePlan artifacts at all.
  Quantity max_ahead_charged_bytes = 0;
  // This is a closed-domain arithmetic fact, always
  // ordinal_domain_bound - 1.  It is not a runtime dispatch window, a skip
  // allowance, or a scheduling policy.
  Quantity max_gap_ordinals = 0;
  // M3 treats the XML-derived Cartesian domain as the expected complete tuple
  // set, not as proof of observed acquisition coverage.  The exact policy
  // requires a complete set of completed FrameSlotContexts at EndOfInput.
  std::string occurrence_policy = std::string(kStrictDenseAllTuplesReorderOccurrencePolicy);
  std::string publish_policy = std::string(kNextExpectedOnlyReorderPublishPolicy);
  // M3 has no certificate-mediated skip rule.  The empty set is explicit in
  // the artifact so a later extension cannot silently reinterpret a gap.
  std::vector<Quantity> certified_skipped_ordinals;
  std::string end_of_input_policy = std::string(kFailReorderEndOfInputPolicy);
  // Must equal max_ahead_items * kDenseCartesianReorderHandleSidecarChargedBytes.
  // It is included in host_metadata_charged_bytes under the M3.7 storage id.
  Quantity handle_storage_charged_bytes = 0;
  Quantity host_metadata_charged_bytes = 0;
  Quantity descriptor_charged_count = 0;
};

// Scan-specific, finite reorder state for one output-producing node.  A
// future runtime must accept only ordinals in [0, ordinal_domain_bound), hold
// at most the frozen ahead item/byte capacities, publish exactly the current
// next expected ordinal, and fail rather than skip any gap at EndOfInput.
class ReorderPlan final {
public:
  [[nodiscard]] const std::string& node_id() const noexcept { return node_id_; }
  [[nodiscard]] const std::string& order_domain_id() const noexcept { return order_domain_id_; }
  [[nodiscard]] const std::string& ordinal_binding_id() const noexcept { return ordinal_binding_id_; }
  [[nodiscard]] const std::string& completed_frame_input_port() const noexcept { return completed_frame_input_port_; }
  [[nodiscard]] const std::string& ordered_output_port() const noexcept { return ordered_output_port_; }
  [[nodiscard]] constexpr Quantity outputs_per_ordinal() const noexcept { return outputs_per_ordinal_.value(); }
  [[nodiscard]] constexpr Quantity charged_bytes_per_ordinal() const noexcept {
    return charged_bytes_per_ordinal_.value();
  }
  [[nodiscard]] const std::vector<DenseCartesianOrdinalDimension>& ordinal_dimensions() const noexcept {
    return ordinal_dimensions_;
  }
  [[nodiscard]] const std::string& mapping_algorithm_id() const noexcept { return mapping_algorithm_id_; }
  [[nodiscard]] const std::string& storage_accounting_id() const noexcept { return storage_accounting_id_; }
  [[nodiscard]] constexpr Quantity ordinal_domain_bound() const noexcept { return ordinal_domain_bound_.value(); }
  [[nodiscard]] constexpr Quantity first_expected_ordinal() const noexcept { return first_expected_ordinal_.value(); }
  [[nodiscard]] constexpr Quantity last_expected_ordinal() const noexcept { return last_expected_ordinal_.value(); }
  [[nodiscard]] constexpr Quantity max_ahead_items() const noexcept { return max_ahead_items_.value(); }
  [[nodiscard]] constexpr Quantity max_ahead_charged_bytes() const noexcept { return max_ahead_charged_bytes_.value(); }
  // Closed-domain arithmetic upper bound only; it grants no dispatch,
  // buffering, skip, or EndOfInput behavior.
  [[nodiscard]] constexpr Quantity max_gap_ordinals() const noexcept { return max_gap_ordinals_.value(); }
  [[nodiscard]] const std::string& occurrence_policy() const noexcept { return occurrence_policy_; }
  [[nodiscard]] const std::string& publish_policy() const noexcept { return publish_policy_; }
  [[nodiscard]] const std::vector<Quantity>& certified_skipped_ordinals() const noexcept {
    return certified_skipped_ordinals_;
  }
  [[nodiscard]] const std::string& end_of_input_policy() const noexcept { return end_of_input_policy_; }
  [[nodiscard]] constexpr Quantity handle_storage_charged_bytes() const noexcept {
    return handle_storage_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity host_metadata_charged_bytes() const noexcept {
    return host_metadata_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity descriptor_charged_count() const noexcept {
    return descriptor_charged_count_.value();
  }

  [[nodiscard]] static ReorderPlan from_validated(
    std::string node_id, std::vector<DenseCartesianOrdinalDimension> ordinal_dimensions, std::string order_domain_id,
    std::string ordinal_binding_id, std::string completed_frame_input_port, std::string ordered_output_port,
    CanonicalQuantity outputs_per_ordinal, CanonicalQuantity charged_bytes_per_ordinal,
    std::string mapping_algorithm_id, std::string storage_accounting_id, CanonicalQuantity ordinal_domain_bound,
    CanonicalQuantity first_expected_ordinal, CanonicalQuantity last_expected_ordinal,
    CanonicalQuantity max_ahead_items, CanonicalQuantity max_ahead_charged_bytes, CanonicalQuantity max_gap_ordinals,
    std::string occurrence_policy, std::string publish_policy, std::vector<Quantity> certified_skipped_ordinals,
    std::string end_of_input_policy, CanonicalQuantity handle_storage_charged_bytes,
    CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count) noexcept;

private:
  ReorderPlan(std::string node_id, std::vector<DenseCartesianOrdinalDimension> ordinal_dimensions,
              std::string order_domain_id, std::string ordinal_binding_id, std::string completed_frame_input_port,
              std::string ordered_output_port, CanonicalQuantity outputs_per_ordinal,
              CanonicalQuantity charged_bytes_per_ordinal, std::string mapping_algorithm_id,
              std::string storage_accounting_id, CanonicalQuantity ordinal_domain_bound,
              CanonicalQuantity first_expected_ordinal, CanonicalQuantity last_expected_ordinal,
              CanonicalQuantity max_ahead_items, CanonicalQuantity max_ahead_charged_bytes,
              CanonicalQuantity max_gap_ordinals, std::string occurrence_policy, std::string publish_policy,
              std::vector<Quantity> certified_skipped_ordinals, std::string end_of_input_policy,
              CanonicalQuantity handle_storage_charged_bytes, CanonicalQuantity host_metadata_charged_bytes,
              CanonicalQuantity descriptor_charged_count) noexcept
      : node_id_(std::move(node_id)), order_domain_id_(std::move(order_domain_id)),
        ordinal_binding_id_(std::move(ordinal_binding_id)),
        completed_frame_input_port_(std::move(completed_frame_input_port)),
        ordered_output_port_(std::move(ordered_output_port)), outputs_per_ordinal_(outputs_per_ordinal),
        charged_bytes_per_ordinal_(charged_bytes_per_ordinal), ordinal_dimensions_(std::move(ordinal_dimensions)),
        mapping_algorithm_id_(std::move(mapping_algorithm_id)),
        storage_accounting_id_(std::move(storage_accounting_id)), ordinal_domain_bound_(ordinal_domain_bound),
        first_expected_ordinal_(first_expected_ordinal), last_expected_ordinal_(last_expected_ordinal),
        max_ahead_items_(max_ahead_items), max_ahead_charged_bytes_(max_ahead_charged_bytes),
        max_gap_ordinals_(max_gap_ordinals), occurrence_policy_(std::move(occurrence_policy)),
        publish_policy_(std::move(publish_policy)), certified_skipped_ordinals_(std::move(certified_skipped_ordinals)),
        end_of_input_policy_(std::move(end_of_input_policy)),
        handle_storage_charged_bytes_(handle_storage_charged_bytes),
        host_metadata_charged_bytes_(host_metadata_charged_bytes), descriptor_charged_count_(descriptor_charged_count) {
  }

  std::string node_id_;
  std::string order_domain_id_;
  std::string ordinal_binding_id_;
  std::string completed_frame_input_port_;
  std::string ordered_output_port_;
  CanonicalQuantity outputs_per_ordinal_;
  CanonicalQuantity charged_bytes_per_ordinal_;
  std::vector<DenseCartesianOrdinalDimension> ordinal_dimensions_;
  std::string mapping_algorithm_id_;
  std::string storage_accounting_id_;
  CanonicalQuantity ordinal_domain_bound_;
  CanonicalQuantity first_expected_ordinal_;
  CanonicalQuantity last_expected_ordinal_;
  CanonicalQuantity max_ahead_items_;
  CanonicalQuantity max_ahead_charged_bytes_;
  CanonicalQuantity max_gap_ordinals_;
  std::string occurrence_policy_;
  std::string publish_policy_;
  std::vector<Quantity> certified_skipped_ordinals_;
  std::string end_of_input_policy_;
  CanonicalQuantity handle_storage_charged_bytes_;
  CanonicalQuantity host_metadata_charged_bytes_;
  CanonicalQuantity descriptor_charged_count_;
};

// M3.7 freezes one narrow host-normal immutable data plane.  The pool is the
// only owner of payload physical memory; all downstream structures retain
// only move-only handles, logical byte credits, and descriptor/control state.
inline constexpr std::string_view kM37BufferPoolStorageAccountingId = "kspacejet.buffer-pool-storage/host-normal-v1";
inline constexpr std::string_view kM37DataEdgeStorageAccountingId = "kspacejet.data-edge-storage/fixed-fifo-v1";
inline constexpr std::string_view kM37NormalEoiDrainCancellationFailTerminalPolicy =
  "normal-eoi-drain-cancellation-fail";
inline constexpr std::string_view kM37PlanBoundDataPlaneProofObligation = "PO-13.m3_7_plan_bound_data_plane";
inline constexpr std::string_view kM37SinglePhysicalPayloadChargeRuntimeAssumption =
  "RA-02.m3_7_single_physical_payload_charge";
inline constexpr std::string_view kM37PlanBoundDataPlaneVerificationObligation = "M3.7.plan_bound_data_plane";
inline constexpr std::string_view kM37SinglePhysicalPayloadChargeVerificationObligation =
  "M3.7.single_physical_payload_charge_runtime_assumption";

// These are stable artifact accounting units, not implementation object-layout
// assertions.  Concrete caller-slab runtime storage must prove it fits the
// frozen charge before instantiating the corresponding plan.
inline constexpr Quantity kM37BufferPoolControlChargedBytesPerSlot = 40U;
inline constexpr Quantity kM37DataEdgeControlChargedBytesPerItem = 96U;
// Stable abstract staging for the one M3.7 synchronous Provider ABI endpoint:
// one lease-control record, one input batch view, one input item view, one
// output grant state, and one sealed-output view.  It is intentionally not a
// C/C++ object-layout assertion; the runtime must prove its concrete staging
// fits this frozen charge before it creates the bridge.
inline constexpr Quantity kM37FiringLeaseHostStagingChargedBytes = 4096U;
inline constexpr Quantity kM37FiringLeaseHostStagingDescriptorCount = 5U;
// `ksj_output_grant` names an output port with a uint32_t.  The plan freezes
// the exact zero-based position in the resolved OperatorContract output-port
// array so the runtime never guesses or lexically re-sorts Provider ports.
inline constexpr Quantity kM37MaximumProducerAbiPort = static_cast<Quantity>(std::numeric_limits<std::uint32_t>::max());

[[nodiscard]] Result<Quantity> m37_buffer_pool_host_metadata_charged_bytes(Quantity slot_count,
                                                                           std::string_view field_name);
[[nodiscard]] Result<Quantity> m37_buffer_pool_physical_charge_bytes(Quantity slot_count,
                                                                     Quantity payload_capacity_bytes,
                                                                     Quantity metadata_capacity_bytes,
                                                                     std::string_view field_name);
[[nodiscard]] Result<Quantity> m37_data_edge_host_metadata_charged_bytes(Quantity max_items,
                                                                         std::string_view field_name);

struct BufferPoolPlanSpec {
  std::string pool_id;
  std::string producer_node_id;
  std::string producer_port_name;
  // Frozen provider provenance for the one producer endpoint.  Node/port
  // names alone identify graph topology, not the Provider ABI descriptor
  // authorized to fill this pool.
  std::string producer_provider_id;
  std::string producer_bundle_digest;
  std::string producer_operator_id;
  std::string producer_contract_digest;
  TypeDescriptor type_descriptor;
  TypeMemoryDomain memory_domain{TypeMemoryDomain::host_normal};
  Quantity slot_count = 0;
  Quantity payload_capacity_bytes = 0;
  Quantity metadata_capacity_bytes = 0;
  Quantity payload_alignment_bytes = 0;
  std::string storage_accounting_id = std::string(kM37BufferPoolStorageAccountingId);
  Quantity host_metadata_charged_bytes = 0;
  Quantity descriptor_charged_count = 0;
  // Exact fixed caller-slab bytes: payload + metadata + pool control.  It is
  // an accounting scalar and not a claim that the runtime allocated slabs.
  Quantity physical_charge_bytes = 0;
};

class BufferPoolPlan final {
public:
  [[nodiscard]] const std::string& pool_id() const noexcept { return pool_id_; }
  [[nodiscard]] const std::string& producer_node_id() const noexcept { return producer_node_id_; }
  [[nodiscard]] const std::string& producer_port_name() const noexcept { return producer_port_name_; }
  [[nodiscard]] const std::string& producer_provider_id() const noexcept { return producer_provider_id_; }
  [[nodiscard]] const ArtifactDigest& producer_bundle_digest() const noexcept { return producer_bundle_digest_; }
  [[nodiscard]] const std::string& producer_operator_id() const noexcept { return producer_operator_id_; }
  [[nodiscard]] const ArtifactDigest& producer_contract_digest() const noexcept { return producer_contract_digest_; }
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

  [[nodiscard]] static BufferPoolPlan
  from_validated(std::string pool_id, std::string producer_node_id, std::string producer_port_name,
                 std::string producer_provider_id, ArtifactDigest producer_bundle_digest,
                 std::string producer_operator_id, ArtifactDigest producer_contract_digest,
                 TypeDescriptor type_descriptor, TypeMemoryDomain memory_domain, CanonicalQuantity slot_count,
                 CanonicalQuantity payload_capacity_bytes, CanonicalQuantity metadata_capacity_bytes,
                 CanonicalQuantity payload_alignment_bytes, std::string storage_accounting_id,
                 CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
                 CanonicalQuantity physical_charge_bytes) noexcept;

private:
  BufferPoolPlan(std::string pool_id, std::string producer_node_id, std::string producer_port_name,
                 std::string producer_provider_id, ArtifactDigest producer_bundle_digest,
                 std::string producer_operator_id, ArtifactDigest producer_contract_digest,
                 TypeDescriptor type_descriptor, TypeMemoryDomain memory_domain, CanonicalQuantity slot_count,
                 CanonicalQuantity payload_capacity_bytes, CanonicalQuantity metadata_capacity_bytes,
                 CanonicalQuantity payload_alignment_bytes, std::string storage_accounting_id,
                 CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
                 CanonicalQuantity physical_charge_bytes) noexcept
      : pool_id_(std::move(pool_id)), producer_node_id_(std::move(producer_node_id)),
        producer_port_name_(std::move(producer_port_name)), producer_provider_id_(std::move(producer_provider_id)),
        producer_bundle_digest_(std::move(producer_bundle_digest)),
        producer_operator_id_(std::move(producer_operator_id)),
        producer_contract_digest_(std::move(producer_contract_digest)), type_descriptor_(std::move(type_descriptor)),
        memory_domain_(memory_domain), slot_count_(slot_count), payload_capacity_bytes_(payload_capacity_bytes),
        metadata_capacity_bytes_(metadata_capacity_bytes), payload_alignment_bytes_(payload_alignment_bytes),
        storage_accounting_id_(std::move(storage_accounting_id)),
        host_metadata_charged_bytes_(host_metadata_charged_bytes), descriptor_charged_count_(descriptor_charged_count),
        physical_charge_bytes_(physical_charge_bytes) {}

  std::string pool_id_;
  std::string producer_node_id_;
  std::string producer_port_name_;
  std::string producer_provider_id_;
  ArtifactDigest producer_bundle_digest_;
  std::string producer_operator_id_;
  ArtifactDigest producer_contract_digest_;
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

struct DataEdgePlanSpec {
  std::string edge_id;
  std::string source_pool_id;
  std::string producer_node_id;
  std::string producer_port_name;
  // Zero-based position among the resolved producer OperatorContract's output
  // ports, preserving the contract array order.  It is an ABI-facing value;
  // zero is valid and therefore this is a canonical (not positive) quantity.
  Quantity producer_abi_port = 0;
  std::string consumer_node_id;
  std::string consumer_port_name;
  TypeDescriptor type_descriptor;
  // Full logical edge-credit capacity: downstream FIFO capacity plus the
  // matching ReorderPlan's ahead-held handles.  One credit follows a payload
  // from a pre-callback lease, through reorder, into the FIFO.
  Quantity max_items = 0;
  // Logical payload + metadata credit only.  It must never be added to a
  // physical-memory reservation because source_pool_id already owns bytes.
  Quantity max_logical_bytes = 0;
  std::string storage_accounting_id = std::string(kM37DataEdgeStorageAccountingId);
  Quantity host_metadata_charged_bytes = 0;
  Quantity descriptor_charged_count = 0;
  // Persistent M3.7 Provider ABI view storage, distinct from FIFO control
  // metadata and charged once per plan-owned producer/edge bridge.
  Quantity firing_lease_staging_charged_bytes = 0;
  Quantity firing_lease_staging_descriptor_count = 0;
  std::string terminal_policy = std::string(kM37NormalEoiDrainCancellationFailTerminalPolicy);
};

class DataEdgePlan final {
public:
  [[nodiscard]] const std::string& edge_id() const noexcept { return edge_id_; }
  [[nodiscard]] const std::string& source_pool_id() const noexcept { return source_pool_id_; }
  [[nodiscard]] const std::string& producer_node_id() const noexcept { return producer_node_id_; }
  [[nodiscard]] const std::string& producer_port_name() const noexcept { return producer_port_name_; }
  [[nodiscard]] constexpr Quantity producer_abi_port() const noexcept { return producer_abi_port_.value(); }
  [[nodiscard]] const std::string& consumer_node_id() const noexcept { return consumer_node_id_; }
  [[nodiscard]] const std::string& consumer_port_name() const noexcept { return consumer_port_name_; }
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
  [[nodiscard]] constexpr Quantity firing_lease_staging_charged_bytes() const noexcept {
    return firing_lease_staging_charged_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity firing_lease_staging_descriptor_count() const noexcept {
    return firing_lease_staging_descriptor_count_.value();
  }
  [[nodiscard]] const std::string& terminal_policy() const noexcept { return terminal_policy_; }

  [[nodiscard]] static DataEdgePlan
  from_validated(std::string edge_id, std::string source_pool_id, std::string producer_node_id,
                 std::string producer_port_name, CanonicalQuantity producer_abi_port, std::string consumer_node_id,
                 std::string consumer_port_name, TypeDescriptor type_descriptor, CanonicalQuantity max_items,
                 CanonicalQuantity max_logical_bytes, std::string storage_accounting_id,
                 CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
                 CanonicalQuantity firing_lease_staging_charged_bytes,
                 CanonicalQuantity firing_lease_staging_descriptor_count, std::string terminal_policy) noexcept;

private:
  DataEdgePlan(std::string edge_id, std::string source_pool_id, std::string producer_node_id,
               std::string producer_port_name, CanonicalQuantity producer_abi_port, std::string consumer_node_id,
               std::string consumer_port_name, TypeDescriptor type_descriptor, CanonicalQuantity max_items,
               CanonicalQuantity max_logical_bytes, std::string storage_accounting_id,
               CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
               CanonicalQuantity firing_lease_staging_charged_bytes,
               CanonicalQuantity firing_lease_staging_descriptor_count, std::string terminal_policy) noexcept
      : edge_id_(std::move(edge_id)), source_pool_id_(std::move(source_pool_id)),
        producer_node_id_(std::move(producer_node_id)), producer_port_name_(std::move(producer_port_name)),
        producer_abi_port_(producer_abi_port), consumer_node_id_(std::move(consumer_node_id)),
        consumer_port_name_(std::move(consumer_port_name)), type_descriptor_(std::move(type_descriptor)),
        max_items_(max_items), max_logical_bytes_(max_logical_bytes),
        storage_accounting_id_(std::move(storage_accounting_id)),
        host_metadata_charged_bytes_(host_metadata_charged_bytes), descriptor_charged_count_(descriptor_charged_count),
        firing_lease_staging_charged_bytes_(firing_lease_staging_charged_bytes),
        firing_lease_staging_descriptor_count_(firing_lease_staging_descriptor_count),
        terminal_policy_(std::move(terminal_policy)) {}

  std::string edge_id_;
  std::string source_pool_id_;
  std::string producer_node_id_;
  std::string producer_port_name_;
  CanonicalQuantity producer_abi_port_;
  std::string consumer_node_id_;
  std::string consumer_port_name_;
  TypeDescriptor type_descriptor_;
  CanonicalQuantity max_items_;
  CanonicalQuantity max_logical_bytes_;
  std::string storage_accounting_id_;
  CanonicalQuantity host_metadata_charged_bytes_;
  CanonicalQuantity descriptor_charged_count_;
  CanonicalQuantity firing_lease_staging_charged_bytes_;
  CanonicalQuantity firing_lease_staging_descriptor_count_;
  std::string terminal_policy_;
};

struct EdgeCapacitySpec {
  std::string edge_id;
  Quantity max_items = 0;
  Quantity max_charged_bytes = 0;
};

class EdgeCapacity final {
public:
  [[nodiscard]] const std::string& edge_id() const noexcept { return edge_id_; }
  [[nodiscard]] const Capacity& capacity() const noexcept { return capacity_; }

  [[nodiscard]] static EdgeCapacity from_validated(std::string edge_id, Capacity capacity) noexcept;

private:
  EdgeCapacity(std::string edge_id, Capacity capacity) noexcept
      : edge_id_(std::move(edge_id)), capacity_(std::move(capacity)) {}

  std::string edge_id_;
  Capacity capacity_;
};

struct ExecutionPlanSpec {
  std::string schema_version = std::string(kExecutionPlanSchemaVersion);
  PlanInputDigestSpec inputs;
  ExecutionProfile execution_profile = ExecutionProfile::bounded_online;
  std::vector<KeySlotTablePlanSpec> key_slot_tables;
  std::vector<ReorderPlanSpec> reorder_plans;
  std::vector<BufferPoolPlanSpec> buffer_pool_plans;
  std::vector<DataEdgePlanSpec> data_edge_plans;
  std::vector<EdgeCapacitySpec> edge_capacities;
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
  [[nodiscard]] constexpr ExecutionProfile execution_profile() const noexcept { return execution_profile_; }
  [[nodiscard]] const std::vector<KeySlotTablePlan>& key_slot_tables() const noexcept { return key_slot_tables_; }
  [[nodiscard]] const std::vector<ReorderPlan>& reorder_plans() const noexcept { return reorder_plans_; }
  [[nodiscard]] const std::vector<BufferPoolPlan>& buffer_pool_plans() const noexcept { return buffer_pool_plans_; }
  [[nodiscard]] const std::vector<DataEdgePlan>& data_edge_plans() const noexcept { return data_edge_plans_; }
  [[nodiscard]] const std::vector<EdgeCapacity>& edge_capacities() const noexcept { return edge_capacities_; }
  [[nodiscard]] const ResourceVector& resources() const noexcept { return resources_; }
  [[nodiscard]] constexpr Quantity terminal_occurrences() const noexcept { return terminal_occurrences_.value(); }
  [[nodiscard]] const std::vector<std::string>& proof_obligations() const noexcept { return proof_obligations_; }

private:
  ExecutionPlan(ArtifactDigest digest, PlanInputDigests inputs, ExecutionProfile execution_profile,
                std::vector<KeySlotTablePlan> key_slot_tables, std::vector<ReorderPlan> reorder_plans,
                std::vector<BufferPoolPlan> buffer_pool_plans, std::vector<DataEdgePlan> data_edge_plans,
                std::vector<EdgeCapacity> edge_capacities, ResourceVector resources,
                CanonicalQuantity terminal_occurrences, std::vector<std::string> proof_obligations) noexcept
      : digest_(std::move(digest)), inputs_(std::move(inputs)), execution_profile_(execution_profile),
        key_slot_tables_(std::move(key_slot_tables)), reorder_plans_(std::move(reorder_plans)),
        buffer_pool_plans_(std::move(buffer_pool_plans)), data_edge_plans_(std::move(data_edge_plans)),
        edge_capacities_(std::move(edge_capacities)), resources_(std::move(resources)),
        terminal_occurrences_(terminal_occurrences), proof_obligations_(std::move(proof_obligations)) {}

  ArtifactDigest digest_;
  PlanInputDigests inputs_;
  ExecutionProfile execution_profile_;
  std::vector<KeySlotTablePlan> key_slot_tables_;
  std::vector<ReorderPlan> reorder_plans_;
  std::vector<BufferPoolPlan> buffer_pool_plans_;
  std::vector<DataEdgePlan> data_edge_plans_;
  std::vector<EdgeCapacity> edge_capacities_;
  ResourceVector resources_;
  CanonicalQuantity terminal_occurrences_;
  std::vector<std::string> proof_obligations_;
};

struct VerificationRecordSpec {
  std::string schema_version = std::string(kVerificationRecordSchemaVersion);
  std::string execution_plan_digest;
  ExecutionProfile execution_profile = ExecutionProfile::bounded_online;
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
  std::string schema_version = std::string(kAdmissionRecordSchemaVersion);
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
