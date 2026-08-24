#include "kspacejet/recon/graph/pipeline_definition.hpp"

#include "kspacejet/recon/type_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Status validation_error(const std::string_view path, const std::string_view message) {
  return Status::ValidationError(std::string(path) + ": " + std::string(message));
}

[[nodiscard]] Status require_object(const Json& value, const std::string_view path) {
  return value.is_object() ? Status::Ok() : validation_error(path, "must be an object");
}

[[nodiscard]] Status require_array(const Json& value, const std::string_view path) {
  return value.is_array() ? Status::Ok() : validation_error(path, "must be an array");
}

[[nodiscard]] Status validate_object_keys(const Json& object, const std::string_view path,
                                          const std::initializer_list<std::string_view> required,
                                          const std::initializer_list<std::string_view> allowed) {
  const auto object_status = require_object(object, path);
  if (!object_status.ok())
    return object_status;
  for (const auto key : required) {
    if (!object.contains(std::string(key))) {
      return validation_error(path, "is missing required field '" + std::string(key) + "'");
    }
  }
  for (auto member = object.begin(); member != object.end(); ++member) {
    const auto known = std::ranges::any_of(allowed, [&](const std::string_view candidate) {
      return member.key() == candidate;
    });
    if (!known)
      return validation_error(path, "contains unknown field '" + member.key() + "'");
  }
  return Status::Ok();
}

[[nodiscard]] Result<std::string> require_string(const Json& object, const std::string_view key,
                                                 const std::string_view path) {
  const auto iterator = object.find(std::string(key));
  if (iterator == object.end() || !iterator->is_string() || iterator->get_ref<const std::string&>().empty()) {
    return validation_error(path, "field '" + std::string(key) + "' must be a non-empty string");
  }
  return iterator->get<std::string>();
}

[[nodiscard]] bool is_ascii_letter(const char value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] bool is_ascii_digit(const char value) noexcept {
  return value >= '0' && value <= '9';
}

[[nodiscard]] bool is_ascii_alphanumeric(const char value) noexcept {
  return is_ascii_letter(value) || is_ascii_digit(value);
}

[[nodiscard]] bool is_identifier(const std::string_view value) noexcept {
  if (value.empty() || value.size() > 128U || !is_ascii_letter(value.front())) {
    return false;
  }
  return std::ranges::all_of(value.substr(1U), [](const char character) {
    return is_ascii_alphanumeric(character) || character == '.' || character == '_' || character == '-';
  });
}

[[nodiscard]] bool is_qualified_identifier(const std::string_view value) noexcept {
  if (value.empty() || value.size() > 255U || !is_ascii_alphanumeric(value.front())) {
    return false;
  }
  return std::ranges::all_of(value.substr(1U), [](const char character) {
    return is_ascii_alphanumeric(character) || character == '.' || character == '_' || character == '-';
  });
}

[[nodiscard]] Result<std::size_t> utf8_character_count(const std::string_view value, const std::string_view path) {
  std::size_t count = 0U;
  for (std::size_t index = 0U; index < value.size();) {
    const auto first = static_cast<unsigned char>(value[index]);
    if (first <= 0x7FU) {
      ++index;
      ++count;
      continue;
    }

    std::size_t continuation_count = 0U;
    if (first >= 0xC2U && first <= 0xDFU) {
      continuation_count = 1U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuation_count = 2U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuation_count = 3U;
    } else {
      return validation_error(path, "contains invalid UTF-8");
    }
    if (index + continuation_count >= value.size()) {
      return validation_error(path, "contains truncated UTF-8");
    }
    const auto second = static_cast<unsigned char>(value[index + 1U]);
    if ((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU) ||
        (first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU)) {
      return validation_error(path, "contains invalid UTF-8");
    }
    for (std::size_t offset = 1U; offset <= continuation_count; ++offset) {
      const auto continuation = static_cast<unsigned char>(value[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return validation_error(path, "contains invalid UTF-8");
      }
    }
    index += continuation_count + 1U;
    ++count;
  }
  return count;
}

[[nodiscard]] Result<std::string> require_identifier(const Json& object, const std::string_view key,
                                                     const std::string_view path) {
  auto value = require_string(object, key, path);
  if (!value.ok()) {
    return value.status();
  }
  if (!is_identifier(value.value())) {
    return validation_error(path, "field '" + std::string(key) +
                                    "' must be a 1..128 character identifier beginning with an ASCII letter");
  }
  return std::move(value).value();
}

[[nodiscard]] Result<std::string> require_qualified_identifier(const Json& object, const std::string_view key,
                                                               const std::string_view path) {
  auto value = require_string(object, key, path);
  if (!value.ok()) {
    return value.status();
  }
  if (!is_qualified_identifier(value.value())) {
    return validation_error(
      path, "field '" + std::string(key) +
              "' must be a 1..255 character qualified identifier beginning with an ASCII letter or digit");
  }
  return std::move(value).value();
}

[[nodiscard]] Result<std::string> require_bounded_string(const Json& object, const std::string_view key,
                                                         const std::string_view path,
                                                         const std::size_t max_characters) {
  auto value = require_string(object, key, path);
  if (!value.ok()) {
    return value.status();
  }
  auto count = utf8_character_count(value.value(), path);
  if (!count.ok()) {
    return count.status();
  }
  if (count.value() > max_characters) {
    return validation_error(path, "field '" + std::string(key) + "' exceeds its maximum character length");
  }
  return std::move(value).value();
}

[[nodiscard]] Result<std::string> require_bounded_json_string(const Json& value, const std::string_view path,
                                                              const std::size_t max_characters,
                                                              const bool allow_empty = false) {
  if (!value.is_string()) {
    return validation_error(path, "must be a string");
  }
  const auto& string_value = value.get_ref<const std::string&>();
  if (!allow_empty && string_value.empty()) {
    return validation_error(path, "must be a non-empty string");
  }
  auto count = utf8_character_count(string_value, path);
  if (!count.ok()) {
    return count.status();
  }
  if (count.value() > max_characters) {
    return validation_error(path, "exceeds its maximum character length");
  }
  return string_value;
}

constexpr std::int64_t kMaximumExactJsonInteger = 9'007'199'254'740'991LL;

[[nodiscard]] Result<std::int64_t> require_exact_json_integer(const Json& value, const std::string_view path) {
  if (value.is_number_integer()) {
    const auto integer = value.get<std::int64_t>();
    if (integer < -kMaximumExactJsonInteger || integer > kMaximumExactJsonInteger) {
      return validation_error(path, "must be an exactly representable JSON integer");
    }
    return integer;
  }
  if (value.is_number_unsigned()) {
    const auto integer = value.get<std::uint64_t>();
    if (integer > static_cast<std::uint64_t>(kMaximumExactJsonInteger)) {
      return validation_error(path, "must be an exactly representable JSON integer");
    }
    return static_cast<std::int64_t>(integer);
  }
  return validation_error(path, "must be an integer");
}

[[nodiscard]] Result<std::string> canonical_scalar_json(const Json& value, const std::string_view path) {
  auto canonical = canonicalize_json(value.dump(), kPipelineDefinitionJsonParseLimits);
  if (!canonical.ok()) {
    return validation_error(path, "could not be canonicalized");
  }
  return std::move(canonical).value();
}

[[nodiscard]] Result<NodePortReference> parse_node_port_reference(const Json& value, const std::string_view path) {
  const auto status = validate_object_keys(value, path, {"node", "port"}, {"node", "port"});
  if (!status.ok())
    return status;
  auto node = require_identifier(value, "node", path);
  if (!node.ok())
    return node.status();
  auto port = require_identifier(value, "port", path);
  if (!port.ok())
    return port.status();
  return NodePortReference{.node = std::move(node).value(), .port = std::move(port).value()};
}

[[nodiscard]] bool is_external_path_field(const std::string_view name) {
  return name == "path" || name == "file" || name == "directory" || name == "uri" || name == "url" ||
         name.ends_with("_path") || name.ends_with("_file") || name.ends_with("_directory") || name.ends_with("_uri") ||
         name.ends_with("_url");
}

[[nodiscard]] bool has_forbidden_authored_field(const Json& value, std::string& path) {
  static const std::unordered_set<std::string> forbidden{
    // Runtime/compiler-owned physical scheduling and resource policy.
    "task_count",
    "tasks",
    "key_shards",
    "dense_dimensions",
    "slot_count",
    "max_live_keys",
    "key_domain_bound",
    "host_metadata_charged_bytes",
    "shard_count",
    "queue_capacity",
    "queue_bytes",
    "edge_capacity",
    "worker_count",
    "num_workers",
    "threads",
    "num_threads",
    "numa_home",
    "runtime_queue",
    "runtime_threads",
    "gpu_stream_count",
    "batch_size",
    "memory_reservation",
    "allocator",
    "cpu_permits",
    "device_bytes",
    "memory_bytes",
    "memory_limit",
    "queue_items",
    "scheduler",
    "thread_affinity",
    "thread_count",
    // Pipeline selection is symbolic. Dynamic loading, contract discovery and
    // bundle/catalog lookup are resolver-owned and cannot be smuggled through
    // a node's authored algorithm configuration.
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
    "script",
    "shared_library",
    "shell",
    // ISMRMRD-derived facts.  Algorithm choices may be authored, but concrete
    // scan shape, counts and physical channel observations are host-owned.
    "acquisition_count",
    "channel_count",
    "channels",
    "coil_count",
    "columns",
    "encoded_columns",
    "encoded_matrix",
    "encoded_rows",
    "encoding_count",
    "field_of_view",
    "fov",
    "image_columns",
    "image_rows",
    "matrix",
    "matrix_columns",
    "matrix_rows",
    "matrix_size",
    "maximum_samples_per_acquisition",
    "physical_channel_count",
    "readout_samples",
    "recon_columns",
    "recon_rows",
    "reconstruction_columns",
    "reconstruction_rows",
    "rows",
    "sample_count",
    "samples_per_acquisition",
    "scan_descriptor",
    "trajectory_dimension",
    "trajectory_dimensions",
  };
  if (value.is_object()) {
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      if (forbidden.contains(iterator.key()) || is_external_path_field(iterator.key())) {
        path = iterator.key();
        return true;
      }
      if (has_forbidden_authored_field(iterator.value(), path)) {
        path = iterator.key() + "." + path;
        return true;
      }
    }
  } else if (value.is_array()) {
    for (std::size_t index = 0; index < value.size(); ++index) {
      if (has_forbidden_authored_field(value[index], path)) {
        path = "[" + std::to_string(index) + "]" + path;
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] Result<ProviderSelection> parse_provider(const Json& value, const std::string_view path) {
  const auto status = validate_object_keys(value, path, {"alias", "provider_id"}, {"alias", "provider_id"});
  if (!status.ok())
    return status;
  auto alias = require_identifier(value, "alias", path);
  auto provider_id = require_qualified_identifier(value, "provider_id", path);
  if (!alias.ok())
    return alias.status();
  if (!provider_id.ok())
    return provider_id.status();
  return ProviderSelection{.alias = std::move(alias).value(), .provider_id = std::move(provider_id).value()};
}

[[nodiscard]] Result<PipelineInputProfile> parse_input_profile(const Json& value, const std::string_view path) {
  const auto status = validate_object_keys(value, path, {"kind", "dataset_group"}, {"kind", "dataset_group"});
  if (!status.ok()) {
    return status;
  }
  auto kind = require_string(value, "kind", path);
  auto dataset_group = require_bounded_string(value, "dataset_group", path, 128U);
  if (!kind.ok()) {
    return kind.status();
  }
  if (!dataset_group.ok()) {
    return dataset_group.status();
  }
  if (kind.value() != "ismrmrd-hdf5") {
    return validation_error(std::string(path) + ".kind", "must equal 'ismrmrd-hdf5'");
  }
  if (dataset_group.value() != "dataset") {
    return validation_error(std::string(path) + ".dataset_group",
                            "must equal the standard ISMRMRD HDF5 dataset group 'dataset'");
  }
  return PipelineInputProfile{.kind = PipelineInputProfileKind::ismrmrd_hdf5,
                              .dataset_group = std::move(dataset_group).value()};
}

[[nodiscard]] Result<PipelineParameterType> parse_parameter_type(const Json& value, const std::string_view path) {
  auto type = require_string(value, "type", path);
  if (!type.ok()) {
    return type.status();
  }
  if (type.value() == "boolean") {
    return PipelineParameterType::boolean;
  }
  if (type.value() == "integer") {
    return PipelineParameterType::integer;
  }
  if (type.value() == "string") {
    return PipelineParameterType::string;
  }
  if (type.value() == "enum") {
    return PipelineParameterType::enumeration;
  }
  return validation_error(std::string(path) + ".type", "must be one of 'boolean', 'integer', 'string', or 'enum'");
}

[[nodiscard]] Result<PipelineParameter> parse_parameter(const std::string_view name, const Json& value,
                                                        const std::string_view path) {
  const auto object_status = require_object(value, path);
  if (!object_status.ok()) {
    return object_status;
  }
  auto type = parse_parameter_type(value, path);
  if (!type.ok()) {
    return type.status();
  }

  PipelineParameter parameter{.name = std::string(name), .type = type.value()};
  switch (type.value()) {
    case PipelineParameterType::boolean:
      {
        const auto status = validate_object_keys(value, path, {"type", "default"}, {"type", "default"});
        if (!status.ok()) {
          return status;
        }
        if (!value.at("default").is_boolean()) {
          return validation_error(std::string(path) + ".default", "must be a boolean");
        }
        break;
      }
    case PipelineParameterType::integer:
      {
        const auto status = validate_object_keys(value, path, {"type", "minimum", "maximum", "default"},
                                                 {"type", "minimum", "maximum", "default"});
        if (!status.ok()) {
          return status;
        }
        auto minimum = require_exact_json_integer(value.at("minimum"), std::string(path) + ".minimum");
        auto maximum = require_exact_json_integer(value.at("maximum"), std::string(path) + ".maximum");
        auto default_value = require_exact_json_integer(value.at("default"), std::string(path) + ".default");
        if (!minimum.ok()) {
          return minimum.status();
        }
        if (!maximum.ok()) {
          return maximum.status();
        }
        if (!default_value.ok()) {
          return default_value.status();
        }
        if (minimum.value() > maximum.value()) {
          return validation_error(path, "minimum must not exceed maximum");
        }
        if (default_value.value() < minimum.value() || default_value.value() > maximum.value()) {
          return validation_error(std::string(path) + ".default", "must lie within [minimum, maximum]");
        }
        parameter.minimum = minimum.value();
        parameter.maximum = maximum.value();
        break;
      }
    case PipelineParameterType::string:
      {
        const auto status = validate_object_keys(value, path, {"type", "default"}, {"type", "default"});
        if (!status.ok()) {
          return status;
        }
        auto default_value =
          require_bounded_json_string(value.at("default"), std::string(path) + ".default", 4'096U, true);
        if (!default_value.ok()) {
          return default_value.status();
        }
        break;
      }
    case PipelineParameterType::enumeration:
      {
        const auto status =
          validate_object_keys(value, path, {"type", "values", "default"}, {"type", "values", "default"});
        if (!status.ok()) {
          return status;
        }
        const auto values_status = require_array(value.at("values"), std::string(path) + ".values");
        if (!values_status.ok() || value.at("values").empty()) {
          return !values_status.ok() ? values_status
                                     : validation_error(std::string(path) + ".values", "must not be empty");
        }
        std::unordered_set<std::string> distinct_values;
        for (std::size_t index = 0U; index < value.at("values").size(); ++index) {
          auto enum_value = require_bounded_json_string(
            value.at("values")[index], std::string(path) + ".values[" + std::to_string(index) + "]", 4'096U);
          if (!enum_value.ok()) {
            return enum_value.status();
          }
          if (!distinct_values.insert(enum_value.value()).second) {
            return validation_error(std::string(path) + ".values", "contains a duplicate value");
          }
          parameter.enum_values.push_back(std::move(enum_value).value());
        }
        auto default_value = require_bounded_json_string(value.at("default"), std::string(path) + ".default", 4'096U);
        if (!default_value.ok()) {
          return default_value.status();
        }
        if (!distinct_values.contains(default_value.value())) {
          return validation_error(std::string(path) + ".default", "must be one of the declared enum values");
        }
        std::ranges::sort(parameter.enum_values);
        break;
      }
  }

  auto canonical_default = canonical_scalar_json(value.at("default"), std::string(path) + ".default");
  if (!canonical_default.ok()) {
    return canonical_default.status();
  }
  parameter.canonical_default_json = std::move(canonical_default).value();
  return parameter;
}

[[nodiscard]] Result<std::vector<PipelineParameter>> parse_parameters(const Json& value, const std::string_view path) {
  const auto object_status = require_object(value, path);
  if (!object_status.ok()) {
    return object_status;
  }
  std::vector<PipelineParameter> parameters;
  parameters.reserve(value.size());
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!is_identifier(iterator.key())) {
      return validation_error(path, "contains a parameter name that is not an identifier");
    }
    auto parameter = parse_parameter(iterator.key(), iterator.value(), std::string(path) + "." + iterator.key());
    if (!parameter.ok()) {
      return parameter.status();
    }
    parameters.push_back(std::move(parameter).value());
  }
  std::ranges::sort(parameters, {}, &PipelineParameter::name);
  return parameters;
}

[[nodiscard]] Result<ScanFactSelector> parse_scan_fact_selector(const Json& value, const std::string_view path) {
  auto selector = require_bounded_json_string(value, path, 128U);
  if (!selector.ok()) {
    return selector.status();
  }
  static const std::unordered_map<std::string, ScanFactSelector> selectors{
    {"acquisition_count", ScanFactSelector::acquisition_count},
    {"physical_channel_count", ScanFactSelector::physical_channel_count},
    {"maximum_samples_per_acquisition", ScanFactSelector::maximum_samples_per_acquisition},
    {"trajectory_dimensions", ScanFactSelector::trajectory_dimensions},
    {"encoded_matrix_x", ScanFactSelector::encoded_matrix_x},
    {"encoded_matrix_y", ScanFactSelector::encoded_matrix_y},
    {"encoded_matrix_z", ScanFactSelector::encoded_matrix_z},
    {"recon_matrix_x", ScanFactSelector::recon_matrix_x},
    {"recon_matrix_y", ScanFactSelector::recon_matrix_y},
    {"recon_matrix_z", ScanFactSelector::recon_matrix_z},
  };
  const auto found = selectors.find(selector.value());
  if (found == selectors.end()) {
    return validation_error(path, "names an unknown scan fact selector");
  }
  return found->second;
}

[[nodiscard]] bool selector_requires_encoding(const ScanFactSelector selector) noexcept {
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

[[nodiscard]] Result<std::vector<ScanFactBinding>> parse_scan_fact_bindings(const Json& value, const Json& config,
                                                                            const std::string_view path) {
  const auto object_status = require_object(value, path);
  if (!object_status.ok()) {
    return object_status;
  }
  std::vector<ScanFactBinding> bindings;
  bindings.reserve(value.size());
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    const auto binding_path = std::string(path) + "." + iterator.key();
    if (!is_identifier(iterator.key())) {
      return validation_error(path, "contains a configuration key that is not an identifier");
    }
    if (config.contains(iterator.key())) {
      return validation_error(binding_path, "collides with a static authored config key");
    }
    const auto status =
      validate_object_keys(iterator.value(), binding_path, {"$scan_fact"}, {"$scan_fact", "encoding"});
    if (!status.ok()) {
      return status;
    }
    auto selector = parse_scan_fact_selector(iterator.value().at("$scan_fact"), binding_path + ".$scan_fact");
    if (!selector.ok()) {
      return selector.status();
    }
    std::optional<std::uint32_t> encoding;
    if (selector_requires_encoding(selector.value())) {
      if (!iterator.value().contains("encoding")) {
        return validation_error(binding_path, "requires an encoding index for this geometry selector");
      }
      auto encoding_value = require_exact_json_integer(iterator.value().at("encoding"), binding_path + ".encoding");
      if (!encoding_value.ok()) {
        return encoding_value.status();
      }
      if (encoding_value.value() < 0 ||
          encoding_value.value() > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        return validation_error(binding_path + ".encoding", "must be a non-negative 32-bit integer");
      }
      encoding = static_cast<std::uint32_t>(encoding_value.value());
    } else if (iterator.value().contains("encoding")) {
      return validation_error(binding_path + ".encoding", "is only valid for a geometry selector");
    }
    bindings.push_back(
      ScanFactBinding{.config_key = iterator.key(), .selector = selector.value(), .encoding = encoding});
  }
  std::ranges::sort(bindings, {}, &ScanFactBinding::config_key);
  return bindings;
}

[[nodiscard]] Status validate_parameter_references(const Json& value,
                                                   const std::unordered_set<std::string>& parameter_names,
                                                   const std::string_view path) {
  if (value.is_object()) {
    if (value.contains("$param")) {
      const auto status = validate_object_keys(value, path, {"$param"}, {"$param"});
      if (!status.ok()) {
        return status;
      }
      auto parameter = require_bounded_json_string(value.at("$param"), std::string(path) + ".$param", 128U);
      if (!parameter.ok()) {
        return parameter.status();
      }
      if (!is_identifier(parameter.value())) {
        return validation_error(std::string(path) + ".$param", "must name a parameter identifier");
      }
      if (!parameter_names.contains(parameter.value())) {
        return validation_error(std::string(path) + ".$param", "references an undeclared parameter");
      }
      return Status::Ok();
    }
    if (value.contains("$scan_fact")) {
      return validation_error(path, "'$scan_fact' is only valid in node scan_fact_bindings");
    }
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      const auto status =
        validate_parameter_references(iterator.value(), parameter_names, std::string(path) + "." + iterator.key());
      if (!status.ok()) {
        return status;
      }
    }
  } else if (value.is_array()) {
    for (std::size_t index = 0U; index < value.size(); ++index) {
      const auto status = validate_parameter_references(value[index], parameter_names,
                                                        std::string(path) + "[" + std::to_string(index) + "]");
      if (!status.ok()) {
        return status;
      }
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status reject_special_references(const Json& value, const std::string_view path) {
  if (value.is_object()) {
    if (value.contains("$param") || value.contains("$scan_fact")) {
      return validation_error(path, "'$param' and '$scan_fact' are only valid in node configuration declarations");
    }
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      const auto status = reject_special_references(iterator.value(), std::string(path) + "." + iterator.key());
      if (!status.ok()) {
        return status;
      }
    }
  } else if (value.is_array()) {
    for (std::size_t index = 0U; index < value.size(); ++index) {
      const auto status =
        reject_special_references(value[index], std::string(path) + "[" + std::to_string(index) + "]");
      if (!status.ok()) {
        return status;
      }
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<PipelineNode> parse_node(const Json& value, const std::string_view path,
                                              const std::unordered_set<std::string>& parameter_names) {
  const auto status =
    validate_object_keys(value, path, {"id", "operator", "config"}, {"id", "operator", "config", "scan_fact_bindings"});
  if (!status.ok())
    return status;
  auto id = require_identifier(value, "id", path);
  if (!id.ok())
    return id.status();
  const auto operator_status =
    validate_object_keys(value.at("operator"), std::string(path) + ".operator", {"provider", "id"}, {"provider", "id"});
  if (!operator_status.ok())
    return operator_status;
  auto provider = require_identifier(value.at("operator"), "provider", std::string(path) + ".operator");
  auto operator_id = require_identifier(value.at("operator"), "id", std::string(path) + ".operator");
  if (!provider.ok())
    return provider.status();
  if (!operator_id.ok())
    return operator_id.status();
  const auto config_status = require_object(value.at("config"), std::string(path) + ".config");
  if (!config_status.ok())
    return config_status;
  if (value.at("config").contains("$param")) {
    return validation_error(std::string(path) + ".config",
                            "must remain an object; '$param' may only be a member value");
  }
  std::string forbidden_path;
  if (has_forbidden_authored_field(value.at("config"), forbidden_path)) {
    return validation_error(std::string(path) + ".config",
                            "must not contain non-authored field '" + forbidden_path + "'");
  }
  const auto parameter_status =
    validate_parameter_references(value.at("config"), parameter_names, std::string(path) + ".config");
  if (!parameter_status.ok()) {
    return parameter_status;
  }
  std::vector<ScanFactBinding> scan_fact_bindings;
  if (value.contains("scan_fact_bindings")) {
    auto parsed_bindings = parse_scan_fact_bindings(value.at("scan_fact_bindings"), value.at("config"),
                                                    std::string(path) + ".scan_fact_bindings");
    if (!parsed_bindings.ok()) {
      return parsed_bindings.status();
    }
    scan_fact_bindings = std::move(parsed_bindings).value();
  }
  auto config = canonicalize_json(value.at("config").dump());
  if (!config.ok())
    return config.status();
  return PipelineNode{.id = std::move(id).value(),
                      .provider_alias = std::move(provider).value(),
                      .operator_id = std::move(operator_id).value(),
                      .canonical_config = std::move(config).value(),
                      .scan_fact_bindings = std::move(scan_fact_bindings)};
}

[[nodiscard]] const PipelineNode* find_node(const std::vector<PipelineNode>& nodes,
                                            const std::string_view id) noexcept {
  const auto found = std::ranges::find(nodes, id, &PipelineNode::id);
  return found == nodes.end() ? nullptr : &*found;
}

[[nodiscard]] Status validate_node_reference(const std::vector<PipelineNode>& nodes, const NodePortReference& endpoint,
                                             const std::string_view path) {
  if (find_node(nodes, endpoint.node) == nullptr) {
    return validation_error(path, "references unknown node '" + endpoint.node + "'");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_dag_and_reachability(const std::vector<PipelineNode>& nodes,
                                                   const std::vector<PipelineEdge>& edges,
                                                   const std::vector<IngressPort>& ingress,
                                                   const std::vector<EgressPort>& egress,
                                                   const std::vector<CalibrationBinding>& calibration_bindings) {
  std::unordered_map<std::string, std::vector<std::string>> forward;
  std::unordered_map<std::string, std::vector<std::string>> reverse;
  std::unordered_map<std::string, std::size_t> indegree;
  for (const auto& node : nodes)
    indegree.emplace(node.id, 0U);
  for (const auto& edge : edges) {
    forward[edge.from.node].push_back(edge.to.node);
    reverse[edge.to.node].push_back(edge.from.node);
    ++indegree.at(edge.to.node);
  }
  // An explicit calibration artifact is a real producer/consumer dependency,
  // even though it is stored outside ordinary data-edge transport.  Include it
  // in both cycle and reachability checks so calibration branches cannot be
  // accidentally treated as disconnected side work.
  for (const auto& binding : calibration_bindings) {
    for (const auto& consumer : binding.consumers) {
      forward[binding.producer.node].push_back(consumer.node);
      reverse[consumer.node].push_back(binding.producer.node);
      ++indegree.at(consumer.node);
    }
  }
  std::deque<std::string> ready;
  for (const auto& [node, degree] : indegree) {
    if (degree == 0U)
      ready.push_back(node);
  }
  std::size_t ordered = 0;
  while (!ready.empty()) {
    auto node = std::move(ready.front());
    ready.pop_front();
    ++ordered;
    for (const auto& destination : forward[node]) {
      auto& degree = indegree.at(destination);
      if (--degree == 0U)
        ready.push_back(destination);
    }
  }
  if (ordered != nodes.size())
    return Status::ValidationError("pipeline graph contains a cycle");

  std::unordered_set<std::string> from_ingress;
  std::deque<std::string> queue;
  for (const auto& port : ingress) {
    if (from_ingress.insert(port.to.node).second)
      queue.push_back(port.to.node);
  }
  while (!queue.empty()) {
    auto node = std::move(queue.front());
    queue.pop_front();
    for (const auto& destination : forward[node]) {
      if (from_ingress.insert(destination).second)
        queue.push_back(destination);
    }
  }
  for (const auto& node : nodes) {
    if (!from_ingress.contains(node.id)) {
      return Status::ValidationError("pipeline node '" + node.id + "' is unreachable from ingress");
    }
  }

  std::unordered_set<std::string> to_egress;
  for (const auto& port : egress) {
    if (to_egress.insert(port.from.node).second)
      queue.push_back(port.from.node);
  }
  while (!queue.empty()) {
    auto node = std::move(queue.front());
    queue.pop_front();
    for (const auto& source : reverse[node]) {
      if (to_egress.insert(source).second)
        queue.push_back(source);
    }
  }
  for (const auto& node : nodes) {
    if (!to_egress.contains(node.id)) {
      return Status::ValidationError("pipeline node '" + node.id + "' cannot reach egress");
    }
  }
  return Status::Ok();
}

void sort_array_by_member(Json& array, const std::string_view member_name) {
  if (!array.is_array())
    return;
  std::vector<Json> values;
  values.reserve(array.size());
  for (const auto& value : array)
    values.push_back(value);
  std::ranges::sort(values, {}, [member_name](const Json& value) {
    return value.value(std::string(member_name), std::string{});
  });
  array = Json::array();
  for (auto& value : values)
    array.push_back(std::move(value));
}

void sort_string_array(Json& array) {
  if (!array.is_array())
    return;
  std::vector<std::string> values;
  values.reserve(array.size());
  for (const auto& value : array)
    values.push_back(value.get<std::string>());
  std::ranges::sort(values);
  array = Json::array();
  for (auto& value : values)
    array.push_back(std::move(value));
}

void normalize_pipeline_json(Json& root) {
  sort_string_array(root["allowed_profiles"]);
  for (auto parameter = root["parameters"].begin(); parameter != root["parameters"].end(); ++parameter) {
    if (parameter.value().value("type", std::string{}) == "enum") {
      sort_string_array(parameter.value()["values"]);
    }
  }
  sort_array_by_member(root["provider_requirements"], "alias");
  for (auto& node : root["nodes"]) {
    if (!node.contains("scan_fact_bindings")) {
      node["scan_fact_bindings"] = Json::object();
    }
  }
  sort_array_by_member(root["nodes"], "id");
  sort_array_by_member(root["edges"], "id");
  auto& bindings = root["bindings"];
  sort_array_by_member(bindings["ingress"], "id");
  sort_array_by_member(bindings["egress"], "id");
  sort_array_by_member(bindings["calibration"], "id");
  sort_array_by_member(bindings["merge"], "id");
}

[[nodiscard]] Result<std::vector<ResolvedProvider>>
normalize_resolved_providers(const PipelineDefinition& definition, std::vector<ResolvedProvider> providers) {
  if (providers.size() != definition.provider_requirements().size()) {
    return Status::ValidationError("resolver did not provide an exact resolution for every Provider requirement");
  }
  std::unordered_map<std::string, const ProviderSelection*> selections;
  for (const auto& selection : definition.provider_requirements())
    selections.emplace(selection.alias, &selection);
  std::unordered_set<std::string> aliases;
  for (auto& provider : providers) {
    const auto selection = selections.find(provider.alias);
    if (selection == selections.end() || !aliases.insert(provider.alias).second) {
      return Status::ValidationError("resolver returned unknown or duplicate Provider alias '" + provider.alias + "'");
    }
    if (provider.provider_id != selection->second->provider_id) {
      return Status::ValidationError("resolved Provider does not match authored provider_id");
    }
    std::unordered_set<std::string> operators;
    for (const auto& operator_value : provider.operators) {
      if (operator_value.id.empty() || operator_value.contract_digest.value().empty() ||
          !operators.insert(operator_value.id).second) {
        return Status::ValidationError("resolved Provider contains an invalid or duplicate operator");
      }
    }
  }
  for (const auto& node : definition.nodes()) {
    const auto provider = std::ranges::find(providers, node.provider_alias, &ResolvedProvider::alias);
    if (provider == providers.end())
      return Status::ValidationError("resolved Provider is missing node alias");
    const auto operator_value = std::ranges::find(provider->operators, node.operator_id, &ResolvedOperator::id);
    if (operator_value == provider->operators.end()) {
      return Status::ValidationError("resolved Provider does not expose node operator '" + node.operator_id + "'");
    }
  }
  std::ranges::sort(providers, {}, &ResolvedProvider::alias);
  for (auto& provider : providers)
    std::ranges::sort(provider.operators, {}, &ResolvedOperator::id);
  return providers;
}

[[nodiscard]] Status resolve_parameter_references(Json& value, const std::unordered_map<std::string, Json>& defaults,
                                                  const std::string_view path) {
  if (value.is_object()) {
    if (value.contains("$param")) {
      const auto status = validate_object_keys(value, path, {"$param"}, {"$param"});
      if (!status.ok()) {
        return status;
      }
      auto parameter = require_bounded_json_string(value.at("$param"), std::string(path) + ".$param", 128U);
      if (!parameter.ok()) {
        return parameter.status();
      }
      const auto default_value = defaults.find(parameter.value());
      if (default_value == defaults.end()) {
        return validation_error(std::string(path) + ".$param", "references an undeclared parameter");
      }
      value = default_value->second;
      return Status::Ok();
    }
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      const auto status =
        resolve_parameter_references(iterator.value(), defaults, std::string(path) + "." + iterator.key());
      if (!status.ok()) {
        return status;
      }
    }
  } else if (value.is_array()) {
    for (std::size_t index = 0U; index < value.size(); ++index) {
      const auto status =
        resolve_parameter_references(value[index], defaults, std::string(path) + "[" + std::to_string(index) + "]");
      if (!status.ok()) {
        return status;
      }
    }
  }
  return Status::Ok();
}

[[nodiscard]] Result<std::vector<ResolvedNodeConfig>> resolve_node_configs(const PipelineDefinition& definition) {
  std::unordered_map<std::string, Json> defaults;
  defaults.reserve(definition.parameters().size());
  for (const auto& parameter : definition.parameters()) {
    try {
      defaults.emplace(parameter.name, Json::parse(parameter.canonical_default_json));
    } catch (const Json::exception& exception) {
      return Status::InternalError("PipelineDefinition retained an invalid canonical parameter default for '" +
                                   parameter.name + "': " + exception.what());
    }
  }

  std::vector<ResolvedNodeConfig> node_configs;
  node_configs.reserve(definition.nodes().size());
  for (const auto& node : definition.nodes()) {
    Json config;
    try {
      config = Json::parse(node.canonical_config);
    } catch (const Json::exception& exception) {
      return Status::InternalError("PipelineDefinition retained an invalid canonical node config for '" + node.id +
                                   "': " + exception.what());
    }
    const auto status = resolve_parameter_references(config, defaults, "$.nodes." + node.id + ".config");
    if (!status.ok()) {
      return status;
    }
    if (!config.is_object()) {
      return Status::InternalError("resolved node config for '" + node.id + "' is not an object");
    }
    auto canonical_config = canonicalize_json(config.dump(), kPipelineDefinitionJsonParseLimits);
    if (!canonical_config.ok()) {
      return canonical_config.status();
    }
    node_configs.push_back(
      ResolvedNodeConfig{.node_id = node.id, .canonical_config = std::move(canonical_config).value()});
  }
  std::ranges::sort(node_configs, {}, &ResolvedNodeConfig::node_id);
  return node_configs;
}

} // namespace

Result<PipelineDefinition> PipelineDefinition::parse_json(const std::string_view document) {
  Json root;
  auto bounded_canonical = canonicalize_json(document, kPipelineDefinitionJsonParseLimits);
  if (!bounded_canonical.ok()) {
    return bounded_canonical.status();
  }
  try {
    // canonicalize_json performs the untrusted-input parse.  Its SAX parser
    // has already rejected duplicate keys and bounded bytes/depth/containers
    // before this local DOM conversion; parsing these generated bytes simply
    // gives the existing structural validator its JSON view.
    root = Json::parse(bounded_canonical.value().begin(), bounded_canonical.value().end());
  } catch (const Json::exception& exception) {
    return Status::InternalError("bounded PipelineDefinition canonicalization produced invalid JSON: " +
                                 std::string(exception.what()));
  }
  const auto root_status =
    validate_object_keys(root, "$",
                         {"kind", "pipeline", "input_profile", "allowed_profiles", "parameters",
                          "provider_requirements", "nodes", "edges", "bindings", "annotations"},
                         {"$schema", "kind", "pipeline", "input_profile", "allowed_profiles", "parameters",
                          "provider_requirements", "nodes", "edges", "bindings", "annotations"});
  if (!root_status.ok())
    return root_status;
  if (const auto schema = root.find("$schema");
      schema != root.end() && (!schema->is_string() || schema->get_ref<const std::string&>() !=
                                                         "https://json-schema.org/draft/2020-12/schema")) {
    return validation_error("$.$schema", "must equal 'https://json-schema.org/draft/2020-12/schema' when present");
  }
  if (!root.at("kind").is_string() || root.at("kind").get<std::string>() != "PipelineDefinition") {
    return validation_error("$.kind", "must equal 'PipelineDefinition'");
  }
  const auto pipeline_status =
    validate_object_keys(root.at("pipeline"), "$.pipeline", {"id", "display_name"}, {"id", "display_name"});
  if (!pipeline_status.ok())
    return pipeline_status;
  auto pipeline_id = require_qualified_identifier(root.at("pipeline"), "id", "$.pipeline");
  auto display_name = require_bounded_string(root.at("pipeline"), "display_name", "$.pipeline", 256U);
  if (!pipeline_id.ok())
    return pipeline_id.status();
  if (!display_name.ok())
    return display_name.status();
  auto input_profile = parse_input_profile(root.at("input_profile"), "$.input_profile");
  if (!input_profile.ok()) {
    return input_profile.status();
  }
  const auto parameters_status = require_object(root.at("parameters"), "$.parameters");
  const auto annotations_status = require_object(root.at("annotations"), "$.annotations");
  if (!parameters_status.ok())
    return parameters_status;
  if (!annotations_status.ok())
    return annotations_status;
  auto parameters = parse_parameters(root.at("parameters"), "$.parameters");
  if (!parameters.ok()) {
    return parameters.status();
  }
  std::unordered_set<std::string> parameter_names;
  for (const auto& parameter : parameters.value()) {
    parameter_names.insert(parameter.name);
  }
  std::string annotation_forbidden_path;
  if (has_forbidden_authored_field(root.at("annotations"), annotation_forbidden_path)) {
    return validation_error("$.annotations", "must not contain non-authored field '" + annotation_forbidden_path + "'");
  }
  const auto annotation_references = reject_special_references(root.at("annotations"), "$.annotations");
  if (!annotation_references.ok()) {
    return annotation_references;
  }

  const auto profiles_status = require_array(root.at("allowed_profiles"), "$.allowed_profiles");
  if (!profiles_status.ok() || root.at("allowed_profiles").empty()) {
    return !profiles_status.ok() ? profiles_status : validation_error("$.allowed_profiles", "must not be empty");
  }
  std::vector<ExecutionProfile> profiles;
  std::set<ExecutionProfile> seen_profiles;
  for (const auto& value : root.at("allowed_profiles")) {
    if (!value.is_string())
      return validation_error("$.allowed_profiles", "must contain profile strings");
    auto profile = parse_execution_profile(value.get<std::string>());
    if (!profile.ok())
      return profile.status();
    if (!seen_profiles.insert(profile.value()).second) {
      return validation_error("$.allowed_profiles", "contains a duplicate profile");
    }
    profiles.push_back(profile.value());
  }

  const auto providers_status = require_array(root.at("provider_requirements"), "$.provider_requirements");
  if (!providers_status.ok() || root.at("provider_requirements").empty()) {
    return !providers_status.ok() ? providers_status : validation_error("$.provider_requirements", "must not be empty");
  }
  std::vector<ProviderSelection> providers;
  std::unordered_set<std::string> provider_aliases;
  for (std::size_t index = 0; index < root.at("provider_requirements").size(); ++index) {
    auto provider =
      parse_provider(root.at("provider_requirements")[index], "$.provider_requirements[" + std::to_string(index) + "]");
    if (!provider.ok())
      return provider.status();
    if (!provider_aliases.insert(provider.value().alias).second) {
      return validation_error("$.provider_requirements",
                              "contains duplicate Provider alias '" + provider.value().alias + "'");
    }
    providers.push_back(std::move(provider).value());
  }

  const auto nodes_status = require_array(root.at("nodes"), "$.nodes");
  if (!nodes_status.ok() || root.at("nodes").empty()) {
    return !nodes_status.ok() ? nodes_status : validation_error("$.nodes", "must not be empty");
  }
  std::vector<PipelineNode> nodes;
  std::unordered_set<std::string> node_ids;
  for (std::size_t index = 0; index < root.at("nodes").size(); ++index) {
    auto node = parse_node(root.at("nodes")[index], "$.nodes[" + std::to_string(index) + "]", parameter_names);
    if (!node.ok())
      return node.status();
    if (!node_ids.insert(node.value().id).second) {
      return validation_error("$.nodes", "contains duplicate node id '" + node.value().id + "'");
    }
    if (std::ranges::find(providers, node.value().provider_alias, &ProviderSelection::alias) == providers.end()) {
      return validation_error("$.nodes", "references an unknown Provider alias");
    }
    nodes.push_back(std::move(node).value());
  }

  const auto edges_status = require_array(root.at("edges"), "$.edges");
  if (!edges_status.ok())
    return edges_status;
  std::vector<PipelineEdge> edges;
  std::unordered_set<std::string> edge_ids;
  std::unordered_set<std::string> destination_endpoints;
  for (std::size_t index = 0; index < root.at("edges").size(); ++index) {
    const auto& value = root.at("edges")[index];
    const auto path = "$.edges[" + std::to_string(index) + "]";
    const auto status = validate_object_keys(value, path, {"id", "from", "to"}, {"id", "from", "to"});
    if (!status.ok())
      return status;
    auto id = require_identifier(value, "id", path);
    auto from = parse_node_port_reference(value.at("from"), path + ".from");
    auto to = parse_node_port_reference(value.at("to"), path + ".to");
    if (!id.ok())
      return id.status();
    if (!from.ok())
      return from.status();
    if (!to.ok())
      return to.status();
    if (!edge_ids.insert(id.value()).second)
      return validation_error(path, "edge id is duplicated");
    const auto from_status = validate_node_reference(nodes, from.value(), path + ".from");
    const auto to_status = validate_node_reference(nodes, to.value(), path + ".to");
    if (!from_status.ok())
      return from_status;
    if (!to_status.ok())
      return to_status;
    const auto destination_id = to.value().node + "." + to.value().port;
    if (!destination_endpoints.insert(destination_id).second) {
      return validation_error(path, "multiple edge producers require a future explicit MergeBinding");
    }
    edges.push_back(
      PipelineEdge{.id = std::move(id).value(), .from = std::move(from).value(), .to = std::move(to).value()});
  }

  const auto bindings_status =
    validate_object_keys(root.at("bindings"), "$.bindings", {"ingress", "egress", "calibration", "merge"},
                         {"ingress", "egress", "calibration", "merge"});
  if (!bindings_status.ok())
    return bindings_status;
  const auto ingress_array_status = require_array(root.at("bindings").at("ingress"), "$.bindings.ingress");
  if (!ingress_array_status.ok() || root.at("bindings").at("ingress").empty()) {
    return !ingress_array_status.ok() ? ingress_array_status
                                      : validation_error("$.bindings.ingress", "must not be empty");
  }
  std::vector<IngressPort> ingress_ports;
  std::unordered_set<std::string> ingress_ids;
  for (std::size_t index = 0; index < root.at("bindings").at("ingress").size(); ++index) {
    const auto& value = root.at("bindings").at("ingress")[index];
    const auto path = "$.bindings.ingress[" + std::to_string(index) + "]";
    const auto status = validate_object_keys(value, path, {"id", "type", "to"}, {"id", "type", "to"});
    if (!status.ok())
      return status;
    auto id = require_identifier(value, "id", path);
    auto type = require_string(value, "type", path);
    auto to = parse_node_port_reference(value.at("to"), path + ".to");
    if (!id.ok())
      return id.status();
    if (!type.ok())
      return type.status();
    if (!to.ok())
      return to.status();
    if (!ingress_ids.insert(id.value()).second)
      return validation_error(path, "ingress id is duplicated");
    if (!types::resolve(type.value()).ok()) {
      return validation_error(path, "ingress type must name a checked-in TypeRef");
    }
    const auto node_status = validate_node_reference(nodes, to.value(), path + ".to");
    if (!node_status.ok())
      return node_status;
    const auto destination_id = to.value().node + "." + to.value().port;
    if (!destination_endpoints.insert(destination_id).second) {
      return validation_error(path, "input has multiple producers; insert an explicit merge Operator");
    }
    ingress_ports.push_back(
      IngressPort{.id = std::move(id).value(), .type = std::move(type).value(), .to = std::move(to).value()});
  }

  const auto egress_array_status = require_array(root.at("bindings").at("egress"), "$.bindings.egress");
  if (!egress_array_status.ok() || root.at("bindings").at("egress").empty()) {
    return !egress_array_status.ok() ? egress_array_status : validation_error("$.bindings.egress", "must not be empty");
  }
  std::vector<EgressPort> egress_ports;
  std::unordered_set<std::string> egress_ids;
  for (std::size_t index = 0; index < root.at("bindings").at("egress").size(); ++index) {
    const auto& value = root.at("bindings").at("egress")[index];
    const auto path = "$.bindings.egress[" + std::to_string(index) + "]";
    const auto status = validate_object_keys(value, path, {"id", "type", "from"}, {"id", "type", "from"});
    if (!status.ok())
      return status;
    auto id = require_identifier(value, "id", path);
    auto type = require_string(value, "type", path);
    auto from = parse_node_port_reference(value.at("from"), path + ".from");
    if (!id.ok())
      return id.status();
    if (!type.ok())
      return type.status();
    if (!from.ok())
      return from.status();
    if (!egress_ids.insert(id.value()).second)
      return validation_error(path, "egress id is duplicated");
    if (!types::resolve(type.value()).ok()) {
      return validation_error(path, "egress type must name a checked-in TypeRef");
    }
    const auto node_status = validate_node_reference(nodes, from.value(), path + ".from");
    if (!node_status.ok())
      return node_status;
    egress_ports.push_back(
      EgressPort{.id = std::move(id).value(), .type = std::move(type).value(), .from = std::move(from).value()});
  }

  const auto calibration_array_status = require_array(root.at("bindings").at("calibration"), "$.bindings.calibration");
  if (!calibration_array_status.ok())
    return calibration_array_status;
  std::vector<CalibrationBinding> calibration_bindings;
  std::unordered_set<std::string> calibration_ids;
  for (std::size_t index = 0; index < root.at("bindings").at("calibration").size(); ++index) {
    const auto& value = root.at("bindings").at("calibration")[index];
    const auto path = "$.bindings.calibration[" + std::to_string(index) + "]";
    const auto status =
      validate_object_keys(value, path, {"id", "producer", "consumers"}, {"id", "producer", "consumers"});
    if (!status.ok())
      return status;
    auto id = require_identifier(value, "id", path);
    auto producer = parse_node_port_reference(value.at("producer"), path + ".producer");
    if (!id.ok())
      return id.status();
    if (!producer.ok())
      return producer.status();
    if (!calibration_ids.insert(id.value()).second)
      return validation_error(path, "calibration binding id is duplicated");
    const auto producer_status = validate_node_reference(nodes, producer.value(), path + ".producer");
    if (!producer_status.ok())
      return producer_status;
    const auto consumers_status = require_array(value.at("consumers"), path + ".consumers");
    if (!consumers_status.ok() || value.at("consumers").empty()) {
      return !consumers_status.ok() ? consumers_status : validation_error(path, "consumers must not be empty");
    }
    std::unordered_set<std::string> consumers_seen;
    std::vector<NodePortReference> consumers;
    for (std::size_t consumer_index = 0U; consumer_index < value.at("consumers").size(); ++consumer_index) {
      const auto consumer_path = path + ".consumers[" + std::to_string(consumer_index) + "]";
      auto consumer = parse_node_port_reference(value.at("consumers")[consumer_index], consumer_path);
      if (!consumer.ok())
        return consumer.status();
      const auto consumer_status = validate_node_reference(nodes, consumer.value(), consumer_path);
      if (!consumer_status.ok())
        return consumer_status;
      const auto endpoint_key = consumer.value().node + "\x1f" + consumer.value().port;
      if (!consumers_seen.insert(endpoint_key).second) {
        return validation_error(consumer_path, "calibration consumer endpoint is duplicated");
      }
      consumers.push_back(std::move(consumer).value());
    }
    calibration_bindings.push_back(CalibrationBinding{
      .id = std::move(id).value(), .producer = std::move(producer).value(), .consumers = std::move(consumers)});
  }

  const auto merge_array_status = require_array(root.at("bindings").at("merge"), "$.bindings.merge");
  if (!merge_array_status.ok())
    return merge_array_status;
  if (!root.at("bindings").at("merge").empty()) {
    return validation_error("$.bindings.merge", "generic MergeBinding is deferred from the M0/M1 runtime");
  }
  std::vector<MergeBinding> merge_bindings;

  const auto graph_status =
    validate_dag_and_reachability(nodes, edges, ingress_ports, egress_ports, calibration_bindings);
  if (!graph_status.ok())
    return graph_status;

  normalize_pipeline_json(root);
  auto canonical_document = canonicalize_json(root.dump());
  if (!canonical_document.ok())
    return canonical_document.status();
  auto artifact = derive_domain_separated_sha256_digest(
    "kspacejet:artifact:pipeline-definition", canonical_document.value(), "PipelineDefinition artifact digest");
  if (!artifact.ok())
    return artifact.status();
  return PipelineDefinition{std::move(pipeline_id).value(),
                            std::move(display_name).value(),
                            std::move(input_profile).value(),
                            std::move(profiles),
                            std::move(parameters).value(),
                            std::move(providers),
                            std::move(nodes),
                            std::move(edges),
                            std::move(ingress_ports),
                            std::move(egress_ports),
                            std::move(calibration_bindings),
                            std::move(merge_bindings),
                            std::move(canonical_document).value(),
                            std::move(artifact).value()};
}

Result<ResolvedPipeline> ResolvedPipeline::resolve(const PipelineDefinition& definition,
                                                   std::vector<ResolvedProvider> providers) {
  auto normalized_providers = normalize_resolved_providers(definition, std::move(providers));
  if (!normalized_providers.ok())
    return normalized_providers.status();
  auto node_configs = resolve_node_configs(definition);
  if (!node_configs.ok()) {
    return node_configs.status();
  }
  Json resolved{
    {"kind", "ResolvedPipeline"},
    {"pipeline_definition_artifact_digest", definition.artifact_digest().value()},
    {"providers", Json::array()},
    {"nodes", Json::array()},
  };
  for (const auto& provider : normalized_providers.value()) {
    Json provider_json{{"alias", provider.alias},
                       {"provider_id", provider.provider_id},
                       {"bundle_digest", provider.bundle_digest.value()},
                       {"operators", Json::array()}};
    for (const auto& operator_value : provider.operators) {
      provider_json["operators"].push_back(
        {{"id", operator_value.id}, {"contract_digest", operator_value.contract_digest.value()}});
    }
    resolved["providers"].push_back(std::move(provider_json));
  }
  for (const auto& node_config : node_configs.value()) {
    try {
      resolved["nodes"].push_back({{"id", node_config.node_id}, {"config", Json::parse(node_config.canonical_config)}});
    } catch (const Json::exception& exception) {
      return Status::InternalError("resolved node config for '" + node_config.node_id +
                                   "' could not be serialized: " + exception.what());
    }
  }
  auto canonical = canonicalize_json(resolved.dump());
  if (!canonical.ok())
    return canonical.status();
  auto digest = derive_domain_separated_sha256_digest("kspacejet:artifact:resolved-pipeline", canonical.value(),
                                                      "ResolvedPipeline artifact digest");
  if (!digest.ok())
    return digest.status();
  return ResolvedPipeline{definition, std::move(normalized_providers).value(), std::move(node_configs).value(),
                          std::move(canonical).value(), std::move(digest).value()};
}

Result<std::string_view> ResolvedPipeline::config_for(const std::string_view node_id) const {
  const auto found = std::ranges::find(node_configs_, node_id, &ResolvedNodeConfig::node_id);
  if (found == node_configs_.end()) {
    return Status::NotFound("ResolvedPipeline has no configuration for node '" + std::string(node_id) + "'.");
  }
  return std::string_view(found->canonical_config);
}

} // namespace ksj::recon::graph
