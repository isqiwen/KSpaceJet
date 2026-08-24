#include "kspacejet/recon/graph/effective_pipeline_binding.hpp"

#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/scan_facts.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

using Json = nlohmann::json;

inline constexpr std::string_view kEffectivePipelineBindingDigestDomain =
  "kspacejet:artifact:effective-pipeline-binding";

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] Result<Json> parse_canonical_config(const std::string_view canonical_config,
                                                  const std::string_view node_id) {
  try {
    return Json::parse(canonical_config.begin(), canonical_config.end());
  } catch (const Json::exception& exception) {
    return Status::InternalError("EffectivePipelineBinding node '" + std::string(node_id) +
                                 "' accepted canonical JSON that could not be materialized: " + exception.what());
  }
}

[[nodiscard]] bool is_external_reference_field(const std::string_view name) {
  return name == "path" || name == "file" || name == "directory" || name == "uri" || name == "url" ||
         name.ends_with("_path") || name.ends_with("_file") || name.ends_with("_directory") || name.ends_with("_uri") ||
         name.ends_with("_url");
}

[[nodiscard]] bool has_forbidden_effective_field(const Json& value, std::string& path) {
  static const std::unordered_set<std::string> forbidden{
    // Provider selection and loader material belong to the controlled resolver,
    // not to an effective per-node algorithm configuration.
    "bundle",
    "bundle_digest",
    "catalog",
    "command",
    "contract",
    "dll",
    "environment",
    "executable",
    "library",
    "manifest",
    "module",
    "operator",
    "operator_id",
    "provider",
    "provider_id",
    "script",
    "shared_library",
    "shell",
    // Physical scheduling/resource policy remains compiler/runtime-owned.
    "allocator",
    "batch_size",
    "cpu_permits",
    "device_bytes",
    "edge_capacity",
    "gpu_stream_count",
    "memory_bytes",
    "memory_limit",
    "memory_reservation",
    "num_threads",
    "num_workers",
    "numa_home",
    "queue_bytes",
    "queue_capacity",
    "queue_items",
    "runtime_queue",
    "runtime_threads",
    "scheduler",
    "shard_count",
    "slot_count",
    "task_count",
    "tasks",
    "thread_affinity",
    "thread_count",
    "threads",
    "worker_count",
  };
  if (value.is_object()) {
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      if (forbidden.contains(iterator.key()) || is_external_reference_field(iterator.key())) {
        path = iterator.key();
        return true;
      }
      if (has_forbidden_effective_field(iterator.value(), path)) {
        path = iterator.key() + "." + path;
        return true;
      }
    }
  } else if (value.is_array()) {
    for (std::size_t index = 0U; index < value.size(); ++index) {
      if (has_forbidden_effective_field(value[index], path)) {
        path = "[" + std::to_string(index) + "]" + path;
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] bool preserves_authored_config(const Json& effective, const Json& authored) {
  if (authored.is_object()) {
    if (!effective.is_object()) {
      return false;
    }
    for (auto iterator = authored.begin(); iterator != authored.end(); ++iterator) {
      const auto effective_value = effective.find(iterator.key());
      if (effective_value == effective.end() || !preserves_authored_config(*effective_value, iterator.value())) {
        return false;
      }
    }
    return true;
  }
  // P2-002 will introduce declared scan-fact selectors. Until then, arrays
  // and scalar author choices are immutable in the effective configuration.
  return effective == authored;
}

[[nodiscard]] Status validate_artifact_members(const Json& value, const std::string_view path,
                                               const std::initializer_list<std::string_view> required,
                                               const std::initializer_list<std::string_view> allowed) {
  if (!value.is_object()) {
    return validation(std::string(path) + " must be an object.");
  }
  for (const auto member : required) {
    if (!value.contains(std::string(member))) {
      return validation(std::string(path) + " is missing required field '" + std::string(member) + "'.");
    }
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    const auto known = std::ranges::any_of(allowed, [&](const std::string_view member) {
      return iterator.key() == member;
    });
    if (!known) {
      return validation(std::string(path) + " contains unsupported field '" + iterator.key() + "'.");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<std::string> required_string(const Json& object, const std::string_view name,
                                                  const std::string_view path) {
  const auto found = object.find(std::string(name));
  if (found == object.end() || !found->is_string() || found->get_ref<const std::string&>().empty()) {
    return validation(std::string(path) + "." + std::string(name) + " must be a non-empty string.");
  }
  return found->get<std::string>();
}

[[nodiscard]] bool is_geometry_selector(const ScanFactSelector selector) noexcept {
  switch (selector) {
    case ScanFactSelector::encoded_matrix_x:
    case ScanFactSelector::encoded_matrix_y:
    case ScanFactSelector::encoded_matrix_z:
    case ScanFactSelector::recon_matrix_x:
    case ScanFactSelector::recon_matrix_y:
    case ScanFactSelector::recon_matrix_z:
      return true;
    case ScanFactSelector::acquisition_count:
    case ScanFactSelector::physical_channel_count:
    case ScanFactSelector::maximum_samples_per_acquisition:
    case ScanFactSelector::trajectory_dimensions:
      return false;
  }
  return false;
}

[[nodiscard]] Result<Quantity> materialize_scan_fact(const ScanFactBinding& binding, const ScanFacts& scan_facts,
                                                     const std::string_view node_id) {
  switch (binding.selector) {
    case ScanFactSelector::acquisition_count:
      if (binding.encoding.has_value()) {
        return validation("Node '" + std::string(node_id) + "' scan-fact binding '" + binding.config_key +
                          "' must not select an encoding for acquisition_count.");
      }
      return scan_facts.acquisition_count();
    case ScanFactSelector::physical_channel_count:
      if (binding.encoding.has_value()) {
        return validation("Node '" + std::string(node_id) + "' scan-fact binding '" + binding.config_key +
                          "' must not select an encoding for physical_channel_count.");
      }
      return scan_facts.physical_channel_count();
    case ScanFactSelector::maximum_samples_per_acquisition:
      if (binding.encoding.has_value()) {
        return validation("Node '" + std::string(node_id) + "' scan-fact binding '" + binding.config_key +
                          "' must not select an encoding for maximum_samples_per_acquisition.");
      }
      return scan_facts.maximum_samples_per_acquisition();
    case ScanFactSelector::trajectory_dimensions:
      if (binding.encoding.has_value()) {
        return validation("Node '" + std::string(node_id) + "' scan-fact binding '" + binding.config_key +
                          "' must not select an encoding for trajectory_dimensions.");
      }
      return scan_facts.trajectory_dimensions();
    case ScanFactSelector::encoded_matrix_x:
    case ScanFactSelector::encoded_matrix_y:
    case ScanFactSelector::encoded_matrix_z:
    case ScanFactSelector::recon_matrix_x:
    case ScanFactSelector::recon_matrix_y:
    case ScanFactSelector::recon_matrix_z:
      break;
  }

  if (!is_geometry_selector(binding.selector) || !binding.encoding.has_value()) {
    return Status::InternalError("PipelineDefinition lost a required geometry scan-fact encoding selector.");
  }
  const auto encoding_index = static_cast<std::size_t>(*binding.encoding);
  const auto& encodings = scan_facts.descriptor().encodings();
  if (encoding_index >= encodings.size()) {
    return validation("Node '" + std::string(node_id) + "' scan-fact binding '" + binding.config_key +
                      "' selects unavailable ISMRMRD encoding " + std::to_string(encoding_index) + ".");
  }
  const auto& encoding = encodings[encoding_index];
  switch (binding.selector) {
    case ScanFactSelector::encoded_matrix_x:
      return encoding.encoded_matrix().x;
    case ScanFactSelector::encoded_matrix_y:
      return encoding.encoded_matrix().y;
    case ScanFactSelector::encoded_matrix_z:
      return encoding.encoded_matrix().z;
    case ScanFactSelector::recon_matrix_x:
      return encoding.recon_matrix().x;
    case ScanFactSelector::recon_matrix_y:
      return encoding.recon_matrix().y;
    case ScanFactSelector::recon_matrix_z:
      return encoding.recon_matrix().z;
    case ScanFactSelector::acquisition_count:
    case ScanFactSelector::physical_channel_count:
    case ScanFactSelector::maximum_samples_per_acquisition:
    case ScanFactSelector::trajectory_dimensions:
      break;
  }
  return Status::InternalError("PipelineDefinition has an invalid scan-fact selector.");
}

} // namespace

Result<EffectivePipelineBinding>
EffectivePipelineBinding::create_from_declared_scan_fact_bindings(const ResolvedPipeline& resolved_pipeline,
                                                                  const ScanFacts& scan_facts) {
  std::vector<HostDerivedNodeConfig> configs;
  configs.reserve(resolved_pipeline.definition().nodes().size());
  for (const auto& node : resolved_pipeline.definition().nodes()) {
    auto resolved_config = resolved_pipeline.config_for(node.id);
    if (!resolved_config.ok()) {
      return Status::InternalError("ResolvedPipeline is missing static configuration for node '" + node.id + "'.");
    }
    auto config = parse_canonical_config(resolved_config.value(), node.id);
    if (!config.ok()) {
      return Status::InternalError("ResolvedPipeline static configuration for node '" + node.id +
                                   "' is not canonical JSON.");
    }
    if (!config.value().is_object()) {
      return Status::InternalError("ResolvedPipeline static configuration for node '" + node.id +
                                   "' is not a JSON object.");
    }
    for (const auto& binding : node.scan_fact_bindings) {
      if (config.value().contains(binding.config_key)) {
        return Status::InternalError("PipelineDefinition scan-fact binding for node '" + node.id +
                                     "' collides with "
                                     "its resolved static configuration key '" +
                                     binding.config_key + "'.");
      }
      auto fact = materialize_scan_fact(binding, scan_facts, node.id);
      if (!fact.ok()) {
        return fact.status();
      }
      config.value()[binding.config_key] = fact.value();
    }
    auto canonical = canonicalize_json(config.value().dump());
    if (!canonical.ok()) {
      return Status::InternalError(
        "Host materialization of node '" + node.id +
        "' scan-fact bindings did not produce canonical JSON: " + canonical.status().message());
    }
    configs.push_back({.node_id = node.id, .canonical_config = std::move(canonical).value()});
  }
  return create_from_host_derived_configs(resolved_pipeline, scan_facts, std::move(configs));
}

Result<EffectivePipelineBinding>
EffectivePipelineBinding::create_from_host_derived_configs(const ResolvedPipeline& resolved_pipeline,
                                                           const ScanFacts& scan_facts,
                                                           std::vector<HostDerivedNodeConfig> node_configs) {
  std::set<std::string> expected_node_ids;
  for (const auto& node : resolved_pipeline.definition().nodes()) {
    if (node.id.empty() || !expected_node_ids.insert(node.id).second) {
      return Status::InternalError("ResolvedPipeline contains an invalid or duplicate node identifier.");
    }
  }
  if (expected_node_ids.empty()) {
    return Status::ValidationError("EffectivePipelineBinding requires a ResolvedPipeline with at least one node.");
  }

  std::map<std::string, HostDerivedNodeConfig> by_node_id;
  for (auto& node_config : node_configs) {
    if (node_config.node_id.empty()) {
      return validation("EffectivePipelineBinding node configuration id must not be empty.");
    }
    if (!expected_node_ids.contains(node_config.node_id)) {
      return validation("EffectivePipelineBinding contains configuration for unknown resolved node '" +
                        node_config.node_id + "'.");
    }
    const std::string node_id = node_config.node_id;
    if (!by_node_id.emplace(node_id, std::move(node_config)).second) {
      return validation("EffectivePipelineBinding contains duplicate configuration for node '" + node_id + "'.");
    }
  }

  for (const auto& expected_node_id : expected_node_ids) {
    if (!by_node_id.contains(expected_node_id)) {
      return validation("EffectivePipelineBinding is missing configuration for resolved node '" + expected_node_id +
                        "'.");
    }
  }

  Json artifact{{"kind", "EffectivePipelineBinding"},
                {"resolved_pipeline_digest", resolved_pipeline.digest().value()},
                {"scan_facts_digest", scan_facts.digest().value()},
                {"node_configs", Json::array()}};
  std::vector<HostDerivedNodeConfig> normalized_configs;
  normalized_configs.reserve(by_node_id.size());
  for (auto& [node_id, node_config] : by_node_id) {
    auto canonical = canonicalize_json(node_config.canonical_config);
    if (!canonical.ok()) {
      return validation("EffectivePipelineBinding configuration for node '" + node_id +
                        "' is invalid JSON: " + canonical.status().message());
    }
    if (canonical.value() != node_config.canonical_config) {
      return validation("EffectivePipelineBinding configuration for node '" + node_id +
                        "' must already be canonical JSON bytes.");
    }
    auto config_json = parse_canonical_config(node_config.canonical_config, node_id);
    if (!config_json.ok()) {
      return config_json.status();
    }
    if (!config_json.value().is_object()) {
      return validation("EffectivePipelineBinding configuration for node '" + node_id + "' must be a JSON object.");
    }
    std::string forbidden_path;
    if (has_forbidden_effective_field(config_json.value(), forbidden_path)) {
      return validation("EffectivePipelineBinding configuration for node '" + node_id +
                        "' contains non-host-derived field '" + forbidden_path + "'.");
    }
    auto resolved_static_config = resolved_pipeline.config_for(node_id);
    if (!resolved_static_config.ok()) {
      return Status::InternalError("ResolvedPipeline lost static configuration for node '" + node_id +
                                   "' while binding effective configuration.");
    }
    auto authored_json = parse_canonical_config(resolved_static_config.value(), node_id);
    if (!authored_json.ok()) {
      return Status::InternalError("ResolvedPipeline static configuration for node '" + node_id +
                                   "' is not canonical JSON.");
    }
    if (!preserves_authored_config(config_json.value(), authored_json.value())) {
      return validation("EffectivePipelineBinding configuration for node '" + node_id +
                        "' does not preserve its resolved authored configuration.");
    }
    artifact["node_configs"].push_back({{"canonical_config", std::move(config_json).value()}, {"node_id", node_id}});
    normalized_configs.push_back(std::move(node_config));
  }

  auto canonical = canonicalize_json(artifact.dump());
  if (!canonical.ok()) {
    return canonical.status();
  }
  auto digest = derive_domain_separated_sha256_digest(kEffectivePipelineBindingDigestDomain, canonical.value(),
                                                      "EffectivePipelineBinding artifact digest");
  if (!digest.ok()) {
    return digest.status();
  }
  return EffectivePipelineBinding{resolved_pipeline.digest(), scan_facts.digest(), std::move(normalized_configs),
                                  std::move(canonical).value(), std::move(digest).value()};
}

Result<EffectivePipelineBinding> EffectivePipelineBinding::parse_json(const std::string_view document,
                                                                      const ResolvedPipeline& resolved_pipeline,
                                                                      const ScanFacts& scan_facts) {
  auto canonical = canonicalize_json(document);
  if (!canonical.ok()) {
    return canonical.status();
  }

  Json root;
  try {
    root = Json::parse(canonical.value().begin(), canonical.value().end());
  } catch (const Json::exception& exception) {
    return Status::InternalError("EffectivePipelineBinding canonicalization produced invalid JSON: " +
                                 std::string(exception.what()));
  }
  const auto root_status =
    validate_artifact_members(root, "$", {"kind", "resolved_pipeline_digest", "scan_facts_digest", "node_configs"},
                              {"$schema", "kind", "resolved_pipeline_digest", "scan_facts_digest", "node_configs"});
  if (!root_status.ok()) {
    return root_status;
  }
  if (root.contains("$schema")) {
    const auto schema = required_string(root, "$schema", "$");
    if (!schema.ok()) {
      return schema.status();
    }
    if (schema.value() != "https://json-schema.org/draft/2020-12/schema") {
      return validation("$.$schema must equal the JSON Schema 2020-12 URI when present.");
    }
    root.erase("$schema");
  }
  auto kind = required_string(root, "kind", "$");
  auto resolved_digest = required_string(root, "resolved_pipeline_digest", "$");
  auto facts_digest = required_string(root, "scan_facts_digest", "$");
  if (!kind.ok()) {
    return kind.status();
  }
  if (!resolved_digest.ok()) {
    return resolved_digest.status();
  }
  if (!facts_digest.ok()) {
    return facts_digest.status();
  }
  if (kind.value() != "EffectivePipelineBinding") {
    return validation("$.kind must equal 'EffectivePipelineBinding'.");
  }
  if (resolved_digest.value() != resolved_pipeline.digest().value()) {
    return validation(
      "EffectivePipelineBinding resolved_pipeline_digest does not match the supplied ResolvedPipeline.");
  }
  if (facts_digest.value() != scan_facts.digest().value()) {
    return validation("EffectivePipelineBinding scan_facts_digest does not match the supplied ScanFacts.");
  }
  const auto& node_configs = root.at("node_configs");
  if (!node_configs.is_array()) {
    return validation("$.node_configs must be an array.");
  }
  std::vector<HostDerivedNodeConfig> configs;
  configs.reserve(node_configs.size());
  for (std::size_t index = 0U; index < node_configs.size(); ++index) {
    const auto path = "$.node_configs[" + std::to_string(index) + "]";
    const auto node_status = validate_artifact_members(node_configs[index], path, {"node_id", "canonical_config"},
                                                       {"node_id", "canonical_config"});
    if (!node_status.ok()) {
      return node_status;
    }
    auto node_id = required_string(node_configs[index], "node_id", path);
    if (!node_id.ok()) {
      return node_id.status();
    }
    const auto& config = node_configs[index].at("canonical_config");
    if (!config.is_object()) {
      return validation(path + ".canonical_config must be a JSON object.");
    }
    configs.push_back({.node_id = std::move(node_id).value(),
                       .canonical_config = config.dump(-1, ' ', false, Json::error_handler_t::strict)});
  }
  auto binding = create_from_host_derived_configs(resolved_pipeline, scan_facts, std::move(configs));
  if (!binding.ok()) {
    return binding.status();
  }
  auto artifact_canonical = canonicalize_json(root.dump());
  if (!artifact_canonical.ok()) {
    return artifact_canonical.status();
  }
  if (binding.value().canonical_json() != artifact_canonical.value()) {
    return validation("EffectivePipelineBinding document does not match its canonical host-derived representation.");
  }
  return binding;
}

Result<std::string_view> EffectivePipelineBinding::config_for(const std::string_view node_id) const {
  const auto found = std::lower_bound(node_configs_.begin(), node_configs_.end(), node_id,
                                      [](const HostDerivedNodeConfig& node_config, const std::string_view wanted) {
                                        return node_config.node_id < wanted;
                                      });
  if (found == node_configs_.end() || found->node_id != node_id) {
    return Status::NotFound("EffectivePipelineBinding has no configuration for node '" + std::string(node_id) + "'.");
  }
  return std::string_view(found->canonical_config);
}

} // namespace ksj::recon::graph
