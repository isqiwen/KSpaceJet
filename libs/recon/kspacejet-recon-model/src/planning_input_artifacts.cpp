#include "kspacejet/recon/planning_input_artifacts.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon {
namespace {

using Json = nlohmann::json;

struct EncodingLimitName final {
  EncodingLimitDimension dimension;
  std::string_view name;
};

constexpr std::array kEncodingLimitNames{
  EncodingLimitName{EncodingLimitDimension::kspace_encode_step_0, "kspace_encode_step_0"},
  EncodingLimitName{EncodingLimitDimension::kspace_encode_step_1, "kspace_encode_step_1"},
  EncodingLimitName{EncodingLimitDimension::kspace_encode_step_2, "kspace_encode_step_2"},
  EncodingLimitName{EncodingLimitDimension::average, "average"},
  EncodingLimitName{EncodingLimitDimension::slice, "slice"},
  EncodingLimitName{EncodingLimitDimension::contrast, "contrast"},
  EncodingLimitName{EncodingLimitDimension::phase, "phase"},
  EncodingLimitName{EncodingLimitDimension::repetition, "repetition"},
  EncodingLimitName{EncodingLimitDimension::set, "set"},
  EncodingLimitName{EncodingLimitDimension::segment, "segment"},
  EncodingLimitName{EncodingLimitDimension::user_0, "user_0"},
  EncodingLimitName{EncodingLimitDimension::user_1, "user_1"},
  EncodingLimitName{EncodingLimitDimension::user_2, "user_2"},
  EncodingLimitName{EncodingLimitDimension::user_3, "user_3"},
  EncodingLimitName{EncodingLimitDimension::user_4, "user_4"},
  EncodingLimitName{EncodingLimitDimension::user_5, "user_5"},
  EncodingLimitName{EncodingLimitDimension::user_6, "user_6"},
  EncodingLimitName{EncodingLimitDimension::user_7, "user_7"},
};

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] Result<std::string> canonical_json(const Json& value, const std::string_view artifact_name) {
  try {
    return value.dump(-1, ' ', false, Json::error_handler_t::strict);
  } catch (const Json::exception& exception) {
    return validation(std::string(artifact_name) + " could not be serialized as canonical JSON: " + exception.what());
  }
}

[[nodiscard]] std::string ieee754_binary64_hex(const double value) {
  const auto bits = std::bit_cast<std::uint64_t>(value);
  std::ostringstream stream;
  stream << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << bits;
  return stream.str();
}

[[nodiscard]] Result<Json> field_of_view_json(const FieldOfViewMm& field_of_view, const std::string_view field_name) {
  const auto valid = [](const double value) {
    return std::isfinite(value) && value > 0.0;
  };
  if (!valid(field_of_view.x) || !valid(field_of_view.y) || !valid(field_of_view.z)) {
    return validation(std::string(field_name) + " must contain finite positive values.");
  }
  return Json{{"x", ieee754_binary64_hex(field_of_view.x)},
              {"y", ieee754_binary64_hex(field_of_view.y)},
              {"z", ieee754_binary64_hex(field_of_view.z)}};
}

[[nodiscard]] Json matrix_dimensions_json(const MatrixDimensions& matrix) {
  return Json{{"x", matrix.x}, {"y", matrix.y}, {"z", matrix.z}};
}

[[nodiscard]] Result<std::string_view> trajectory_name(const TrajectoryType trajectory) {
  switch (trajectory) {
    case TrajectoryType::cartesian:
      return std::string_view{"cartesian"};
    case TrajectoryType::epi:
      return std::string_view{"epi"};
    case TrajectoryType::radial:
      return std::string_view{"radial"};
    case TrajectoryType::golden_angle:
      return std::string_view{"golden_angle"};
    case TrajectoryType::spiral:
      return std::string_view{"spiral"};
    case TrajectoryType::other:
      return std::string_view{"other"};
  }
  return validation("ScanDescriptor contains an invalid trajectory type.");
}

[[nodiscard]] Json encoding_limits_json(const EncodingLimits& limits) {
  Json result = Json::object();
  for (const auto& entry : kEncodingLimitNames) {
    const auto& limit = limits.at(entry.dimension);
    if (!limit.has_value()) {
      result[std::string(entry.name)] = nullptr;
      continue;
    }
    result[std::string(entry.name)] = {
      {"center", limit->center()},
      {"maximum", limit->maximum()},
      {"minimum", limit->minimum()},
    };
  }
  return result;
}

[[nodiscard]] Result<Json> encoding_descriptor_json(const EncodingDescriptor& encoding, const std::size_t index) {
  auto encoded_field_of_view =
    field_of_view_json(encoding.encoded_field_of_view_mm(),
                       "ScanDescriptor.encodings[" + std::to_string(index) + "].encoded_field_of_view_mm");
  if (!encoded_field_of_view.ok()) {
    return encoded_field_of_view.status();
  }
  auto recon_field_of_view =
    field_of_view_json(encoding.recon_field_of_view_mm(),
                       "ScanDescriptor.encodings[" + std::to_string(index) + "].recon_field_of_view_mm");
  if (!recon_field_of_view.ok()) {
    return recon_field_of_view.status();
  }
  auto trajectory = trajectory_name(encoding.trajectory());
  if (!trajectory.ok()) {
    return trajectory.status();
  }
  return Json{{"encoded_field_of_view_mm_ieee754_binary64", std::move(encoded_field_of_view).value()},
              {"encoded_matrix", matrix_dimensions_json(encoding.encoded_matrix())},
              {"limits", encoding_limits_json(encoding.limits())},
              {"recon_field_of_view_mm_ieee754_binary64", std::move(recon_field_of_view).value()},
              {"recon_matrix", matrix_dimensions_json(encoding.recon_matrix())},
              {"trajectory", trajectory.value()}};
}

[[nodiscard]] Result<Json> scan_descriptor_json(const ScanDescriptor& descriptor) {
  Json encodings = Json::array();
  for (std::size_t index = 0U; index < descriptor.encodings().size(); ++index) {
    auto encoding = encoding_descriptor_json(descriptor.encodings()[index], index);
    if (!encoding.ok()) {
      return encoding.status();
    }
    encodings.push_back(std::move(encoding).value());
  }
  Json result{{"encodings", std::move(encodings)},
              {"kind", "ScanDescriptor"},
              {"source_xml_bytes", descriptor.source_xml_bytes()}};
  if (descriptor.declared_receiver_channels().has_value()) {
    result["declared_receiver_channels"] = *descriptor.declared_receiver_channels();
  } else {
    result["declared_receiver_channels"] = nullptr;
  }
  return result;
}

[[nodiscard]] Result<std::string_view> slow_sink_policy_name(const SlowSinkPolicy policy) {
  switch (policy) {
    case SlowSinkPolicy::fail:
      return std::string_view{"fail"};
    case SlowSinkPolicy::spool:
      return std::string_view{"spool"};
    case SlowSinkPolicy::externally_blocked:
      return std::string_view{"externally_blocked"};
  }
  return validation("TargetEnvelope contains an invalid slow sink policy.");
}

[[nodiscard]] Result<Json> target_envelope_json(const TargetEnvelope& envelope) {
  auto slow_sink_policy = slow_sink_policy_name(envelope.sink_service_assumption().slow_sink_policy());
  if (!slow_sink_policy.ok()) {
    return slow_sink_policy.status();
  }
  return Json{{"arrival_envelope",
               {{"max_acquisitions_per_second", envelope.arrival_envelope().max_acquisitions_per_second()},
                {"max_burst_acquisitions", envelope.arrival_envelope().max_burst_acquisitions()}}},
              {"calibration_horizon_charged_bytes", envelope.calibration_horizon_charged_bytes()},
              {"calibration_horizon_items", envelope.calibration_horizon_items()},
              {"kind", "TargetEnvelope"},
              {"max_active_channels", envelope.max_active_channels()},
              {"max_active_scans", envelope.max_active_scans()},
              {"max_channel_groups", envelope.max_channel_groups()},
              {"max_decoder_staging_bytes", envelope.max_decoder_staging_bytes()},
              {"max_dynamic_keys_per_scan", envelope.max_dynamic_keys_per_scan()},
              {"max_frame_charged_bytes", envelope.max_frame_charged_bytes()},
              {"max_image_charged_bytes", envelope.max_image_charged_bytes()},
              {"max_samples_per_acquisition", envelope.max_samples_per_acquisition()},
              {"max_trajectory_dimensions", envelope.max_trajectory_dimensions()},
              {"max_xml_bytes", envelope.max_xml_bytes()},
              {"sink_service_assumption",
               {{"max_pause_us", envelope.sink_service_assumption().max_pause_us()},
                {"minimum_drain_items_per_second", envelope.sink_service_assumption().minimum_drain_items_per_second()},
                {"slow_sink_policy", slow_sink_policy.value()},
                {"transport_staging_bytes", envelope.sink_service_assumption().transport_staging_bytes()}}}};
}

[[nodiscard]] Result<std::string_view> memory_domain_name(const MemoryDomain domain) {
  switch (domain) {
    case MemoryDomain::host:
      return std::string_view{"host"};
    case MemoryDomain::pinned_host:
      return std::string_view{"pinned_host"};
    case MemoryDomain::device:
      return std::string_view{"device"};
    case MemoryDomain::shared:
      return std::string_view{"shared"};
  }
  return validation("MachinePolicy contains an invalid allowed memory domain.");
}

[[nodiscard]] Result<std::string_view> scheduler_policy_name(const SchedulerPolicy policy) {
  switch (policy) {
    case SchedulerPolicy::fifo:
      return std::string_view{"fifo"};
    case SchedulerPolicy::fair:
      return std::string_view{"fair"};
    case SchedulerPolicy::deadline_aware:
      return std::string_view{"deadline_aware"};
  }
  return validation("MachinePolicy contains an invalid scheduler policy.");
}

template <typename T, typename Name>
[[nodiscard]] Result<Json> canonical_name_set_json(const std::vector<T>& values, Name&& name,
                                                   const std::string_view field_name) {
  std::vector<std::string> names;
  names.reserve(values.size());
  for (const auto value : values) {
    auto value_name = name(value);
    if (!value_name.ok()) {
      return value_name.status();
    }
    names.emplace_back(value_name.value());
  }
  std::sort(names.begin(), names.end());
  if (std::adjacent_find(names.begin(), names.end()) != names.end()) {
    return validation(std::string(field_name) + " contains duplicate values.");
  }
  return Json(std::move(names));
}

[[nodiscard]] Json resource_vector_json(const ResourceVector& resources) {
  Json devices = Json::array();
  for (const auto& device : resources.devices()) {
    devices.push_back({{"copy_engine_slots", device.copy_engine_slots()},
                       {"device_bytes", device.device_bytes()},
                       {"device_id", device.device_id()},
                       {"gpu_stream_slots", device.gpu_stream_slots()}});
  }
  // host_total_bytes is derived from the four host domains and deliberately
  // omitted: it is not an independently-owned MachinePolicy input.
  return Json{{"async_token_count", resources.async_token_count()},
              {"backend_gang_permits", resources.backend_gang_permits()},
              {"cpu_leaf_permits", resources.cpu_leaf_permits()},
              {"descriptor_count", resources.descriptor_count()},
              {"devices", std::move(devices)},
              {"host_hugepage_bytes", resources.host_hugepage_bytes()},
              {"host_normal_bytes", resources.host_normal_bytes()},
              {"host_pinned_bytes", resources.host_pinned_bytes()},
              {"io_slots", resources.io_slots()},
              {"provider_private_permits", resources.provider_private_permits()},
              {"shared_host_bytes", resources.shared_host_bytes()},
              {"spool_bytes", resources.spool_bytes()},
              {"transport_bytes", resources.transport_bytes()}};
}

[[nodiscard]] Result<Json> machine_policy_json(const MachinePolicy& policy) {
  auto memory_domains = canonical_name_set_json(policy.allowed_memory_domains(), memory_domain_name,
                                                "MachinePolicy.allowed_memory_domains");
  if (!memory_domains.ok()) {
    return memory_domains.status();
  }
  const auto execution_profile_name = [](const ExecutionProfile profile) -> Result<std::string_view> {
    const auto name = to_string(profile);
    if (name == "unknown") {
      return validation("MachinePolicy contains an invalid allowed execution profile.");
    }
    return name;
  };
  auto allowed_profiles =
    canonical_name_set_json(policy.allowed_profiles(), execution_profile_name, "MachinePolicy.allowed_profiles");
  if (!allowed_profiles.ok()) {
    return allowed_profiles.status();
  }
  auto scheduler_policy = scheduler_policy_name(policy.scheduler_policy());
  if (!scheduler_policy.ok()) {
    return scheduler_policy.status();
  }
  return Json{{"allowed_memory_domains", std::move(memory_domains).value()},
              {"allowed_profiles", std::move(allowed_profiles).value()},
              {"kind", "MachinePolicy"},
              {"numa_domain_count", policy.numa_domain_count()},
              {"resource_capacity",
               {{"domains", resource_vector_json(policy.resource_capacity().domains())},
                {"host_total_cap_bytes", policy.resource_capacity().host_total_cap_bytes()}}},
              {"scheduler_policy", scheduler_policy.value()}};
}

template <typename Serializer>
[[nodiscard]] Result<ArtifactDigest> derive_artifact_digest(const std::string_view domain, Serializer&& serializer,
                                                            const std::string_view field_name) {
  auto canonical = serializer();
  if (!canonical.ok()) {
    return canonical.status();
  }
  return derive_domain_separated_sha256_digest(domain, canonical.value(), field_name);
}

} // namespace

Result<std::string> serialize_scan_descriptor_canonical_json(const ScanDescriptor& descriptor) {
  auto value = scan_descriptor_json(descriptor);
  if (!value.ok()) {
    return value.status();
  }
  return canonical_json(value.value(), "ScanDescriptor");
}

Result<ArtifactDigest> derive_scan_descriptor_artifact_digest(const ScanDescriptor& descriptor) {
  return derive_artifact_digest(
    kScanDescriptorArtifactDigestDomain,
    [&descriptor] {
      return serialize_scan_descriptor_canonical_json(descriptor);
    },
    "ScanDescriptor artifact digest");
}

Result<std::string> serialize_target_envelope_canonical_json(const TargetEnvelope& envelope) {
  auto value = target_envelope_json(envelope);
  if (!value.ok()) {
    return value.status();
  }
  return canonical_json(value.value(), "TargetEnvelope");
}

Result<ArtifactDigest> derive_target_envelope_artifact_digest(const TargetEnvelope& envelope) {
  return derive_artifact_digest(
    kTargetEnvelopeArtifactDigestDomain,
    [&envelope] {
      return serialize_target_envelope_canonical_json(envelope);
    },
    "TargetEnvelope artifact digest");
}

Result<std::string> serialize_machine_policy_canonical_json(const MachinePolicy& policy) {
  auto value = machine_policy_json(policy);
  if (!value.ok()) {
    return value.status();
  }
  return canonical_json(value.value(), "MachinePolicy");
}

Result<ArtifactDigest> derive_machine_policy_artifact_digest(const MachinePolicy& policy) {
  return derive_artifact_digest(
    kMachinePolicyArtifactDigestDomain,
    [&policy] {
      return serialize_machine_policy_canonical_json(policy);
    },
    "MachinePolicy artifact digest");
}

} // namespace ksj::recon
