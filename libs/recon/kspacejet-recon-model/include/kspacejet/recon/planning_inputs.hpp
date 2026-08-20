#pragma once

// Planning-input value types. TargetEnvelope and MachinePolicy are deployment
// constraints, not Provider/host OperatorContracts.

#include "kspacejet/recon/bounded_value.hpp"
#include "kspacejet/recon/execution_profile.hpp"
#include "kspacejet/recon/resource_vector.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {

enum class MemoryDomain {
  host,
  pinned_host,
  device,
  shared,
};

enum class SchedulerPolicy {
  fifo,
  fair,
  deadline_aware,
};

enum class SlowSinkPolicy {
  fail,
  spool,
  externally_blocked,
};

struct ArrivalEnvelopeSpec {
  Quantity max_acquisitions_per_second = 0;
  Quantity max_burst_acquisitions = 0;
};

class ArrivalEnvelope final {
public:
  [[nodiscard]] static Result<ArrivalEnvelope> create(const ArrivalEnvelopeSpec& specification,
                                                      std::string_view field_name);

  [[nodiscard]] constexpr Quantity max_acquisitions_per_second() const noexcept {
    return max_acquisitions_per_second_.value();
  }
  [[nodiscard]] constexpr Quantity max_burst_acquisitions() const noexcept { return max_burst_acquisitions_.value(); }

private:
  ArrivalEnvelope(CanonicalQuantity max_acquisitions_per_second, CanonicalQuantity max_burst_acquisitions) noexcept
      : max_acquisitions_per_second_(max_acquisitions_per_second), max_burst_acquisitions_(max_burst_acquisitions) {}

  CanonicalQuantity max_acquisitions_per_second_;
  CanonicalQuantity max_burst_acquisitions_;
};

struct SinkServiceAssumptionSpec {
  // These are local result-consumer assumptions. `transport_staging_bytes`
  // accounts only for an in-process materialized handoff; it does not define
  // a socket, session, relay, or external transport contract.
  Quantity minimum_drain_items_per_second = 0;
  Quantity max_pause_us = 0;
  SlowSinkPolicy slow_sink_policy = SlowSinkPolicy::externally_blocked;
  Quantity transport_staging_bytes = 0;
};

class SinkServiceAssumption final {
public:
  [[nodiscard]] static Result<SinkServiceAssumption> create(const SinkServiceAssumptionSpec& specification,
                                                            std::string_view field_name);

  [[nodiscard]] constexpr Quantity minimum_drain_items_per_second() const noexcept {
    return minimum_drain_items_per_second_.value();
  }
  [[nodiscard]] constexpr Quantity max_pause_us() const noexcept { return max_pause_us_.value(); }
  [[nodiscard]] constexpr SlowSinkPolicy slow_sink_policy() const noexcept { return slow_sink_policy_; }
  [[nodiscard]] constexpr Quantity transport_staging_bytes() const noexcept { return transport_staging_bytes_.value(); }

private:
  SinkServiceAssumption(CanonicalQuantity minimum_drain_items_per_second, CanonicalQuantity max_pause_us,
                        SlowSinkPolicy slow_sink_policy, CanonicalQuantity transport_staging_bytes) noexcept
      : minimum_drain_items_per_second_(minimum_drain_items_per_second), max_pause_us_(max_pause_us),
        slow_sink_policy_(slow_sink_policy), transport_staging_bytes_(transport_staging_bytes) {}

  CanonicalQuantity minimum_drain_items_per_second_;
  CanonicalQuantity max_pause_us_;
  SlowSinkPolicy slow_sink_policy_;
  CanonicalQuantity transport_staging_bytes_;
};

struct TargetEnvelopeSpec {
  Quantity max_xml_bytes = 0;
  Quantity max_frame_charged_bytes = 0;
  Quantity max_image_charged_bytes = 0;
  Quantity max_decoder_staging_bytes = 0;
  Quantity max_samples_per_acquisition = 0;
  Quantity max_trajectory_dimensions = 0;
  Quantity max_active_channels = 0;
  Quantity max_channel_groups = 0;
  Quantity max_dynamic_keys_per_scan = 0;
  Quantity max_active_scans = 0;
  Quantity calibration_horizon_items = 0;
  Quantity calibration_horizon_charged_bytes = 0;
  ArrivalEnvelopeSpec arrival_envelope;
  SinkServiceAssumptionSpec sink_service_assumption;
};

// Caller-declared local input and result-delivery bounds. These validation
// limits start after ISMRMRD input has been submitted; they are neither a
// scanner/acquisition contract nor a promise that every Provider contract can
// be admitted within them.
class TargetEnvelope final {
public:
  [[nodiscard]] static Result<TargetEnvelope> create(const TargetEnvelopeSpec& specification);

  [[nodiscard]] constexpr Quantity max_xml_bytes() const noexcept { return max_xml_bytes_.value(); }
  [[nodiscard]] constexpr Quantity max_frame_charged_bytes() const noexcept { return max_frame_charged_bytes_.value(); }
  [[nodiscard]] constexpr Quantity max_image_charged_bytes() const noexcept { return max_image_charged_bytes_.value(); }
  [[nodiscard]] constexpr Quantity max_decoder_staging_bytes() const noexcept {
    return max_decoder_staging_bytes_.value();
  }
  [[nodiscard]] constexpr Quantity max_samples_per_acquisition() const noexcept {
    return max_samples_per_acquisition_.value();
  }
  [[nodiscard]] constexpr Quantity max_trajectory_dimensions() const noexcept {
    return max_trajectory_dimensions_.value();
  }
  [[nodiscard]] constexpr Quantity max_active_channels() const noexcept { return max_active_channels_.value(); }
  [[nodiscard]] constexpr Quantity max_channel_groups() const noexcept { return max_channel_groups_.value(); }
  [[nodiscard]] constexpr Quantity max_dynamic_keys_per_scan() const noexcept {
    return max_dynamic_keys_per_scan_.value();
  }
  [[nodiscard]] constexpr Quantity max_active_scans() const noexcept { return max_active_scans_.value(); }
  [[nodiscard]] constexpr Quantity calibration_horizon_items() const noexcept {
    return calibration_horizon_items_.value();
  }
  [[nodiscard]] constexpr Quantity calibration_horizon_charged_bytes() const noexcept {
    return calibration_horizon_charged_bytes_.value();
  }
  [[nodiscard]] const ArrivalEnvelope& arrival_envelope() const noexcept { return arrival_envelope_; }
  [[nodiscard]] const SinkServiceAssumption& sink_service_assumption() const noexcept {
    return sink_service_assumption_;
  }

private:
  TargetEnvelope(CanonicalQuantity max_xml_bytes, CanonicalQuantity max_frame_charged_bytes,
                 CanonicalQuantity max_image_charged_bytes, CanonicalQuantity max_decoder_staging_bytes,
                 CanonicalQuantity max_samples_per_acquisition, CanonicalQuantity max_trajectory_dimensions,
                 CanonicalQuantity max_active_channels, CanonicalQuantity max_channel_groups,
                 CanonicalQuantity max_dynamic_keys_per_scan, CanonicalQuantity max_active_scans,
                 CanonicalQuantity calibration_horizon_items, CanonicalQuantity calibration_horizon_charged_bytes,
                 ArrivalEnvelope arrival_envelope, SinkServiceAssumption sink_service_assumption) noexcept
      : max_xml_bytes_(max_xml_bytes), max_frame_charged_bytes_(max_frame_charged_bytes),
        max_image_charged_bytes_(max_image_charged_bytes), max_decoder_staging_bytes_(max_decoder_staging_bytes),
        max_samples_per_acquisition_(max_samples_per_acquisition),
        max_trajectory_dimensions_(max_trajectory_dimensions), max_active_channels_(max_active_channels),
        max_channel_groups_(max_channel_groups), max_dynamic_keys_per_scan_(max_dynamic_keys_per_scan),
        max_active_scans_(max_active_scans), calibration_horizon_items_(calibration_horizon_items),
        calibration_horizon_charged_bytes_(calibration_horizon_charged_bytes),
        arrival_envelope_(std::move(arrival_envelope)), sink_service_assumption_(std::move(sink_service_assumption)) {}

  CanonicalQuantity max_xml_bytes_;
  CanonicalQuantity max_frame_charged_bytes_;
  CanonicalQuantity max_image_charged_bytes_;
  CanonicalQuantity max_decoder_staging_bytes_;
  CanonicalQuantity max_samples_per_acquisition_;
  CanonicalQuantity max_trajectory_dimensions_;
  CanonicalQuantity max_active_channels_;
  CanonicalQuantity max_channel_groups_;
  CanonicalQuantity max_dynamic_keys_per_scan_;
  CanonicalQuantity max_active_scans_;
  CanonicalQuantity calibration_horizon_items_;
  CanonicalQuantity calibration_horizon_charged_bytes_;
  ArrivalEnvelope arrival_envelope_;
  SinkServiceAssumption sink_service_assumption_;
};

struct MachinePolicySpec {
  // A deployment capacity is multi-domain.  A scalar process-memory number
  // cannot safely admit pinned/device/in-process handoff work, so it is
  // intentionally not part of the current policy surface.
  ResourceVectorCapacitySpec resource_capacity;
  Quantity numa_domain_count = 0;
  std::vector<MemoryDomain> allowed_memory_domains;
  std::vector<ExecutionProfile> allowed_profiles;
  SchedulerPolicy scheduler_policy = SchedulerPolicy::fair;
};

// Deployment-owned static policy.  It contains budgets and allowed execution
// classes, never graph topology or scan-specific task counts.
class MachinePolicy final {
public:
  [[nodiscard]] static Result<MachinePolicy> create(const MachinePolicySpec& specification);

  [[nodiscard]] const ResourceVectorCapacity& resource_capacity() const noexcept { return resource_capacity_; }
  [[nodiscard]] constexpr Quantity numa_domain_count() const noexcept { return numa_domain_count_.value(); }
  [[nodiscard]] const std::vector<MemoryDomain>& allowed_memory_domains() const noexcept {
    return allowed_memory_domains_;
  }
  [[nodiscard]] const std::vector<ExecutionProfile>& allowed_profiles() const noexcept { return allowed_profiles_; }
  [[nodiscard]] constexpr SchedulerPolicy scheduler_policy() const noexcept { return scheduler_policy_; }
  [[nodiscard]] bool allows(ExecutionProfile profile) const noexcept;

private:
  MachinePolicy(ResourceVectorCapacity resource_capacity, CanonicalQuantity numa_domain_count,
                std::vector<MemoryDomain> allowed_memory_domains, std::vector<ExecutionProfile> allowed_profiles,
                SchedulerPolicy scheduler_policy) noexcept
      : resource_capacity_(std::move(resource_capacity)), numa_domain_count_(numa_domain_count),
        allowed_memory_domains_(std::move(allowed_memory_domains)), allowed_profiles_(std::move(allowed_profiles)),
        scheduler_policy_(scheduler_policy) {}

  ResourceVectorCapacity resource_capacity_;
  CanonicalQuantity numa_domain_count_;
  std::vector<MemoryDomain> allowed_memory_domains_;
  std::vector<ExecutionProfile> allowed_profiles_;
  SchedulerPolicy scheduler_policy_;
};

} // namespace ksj::recon
