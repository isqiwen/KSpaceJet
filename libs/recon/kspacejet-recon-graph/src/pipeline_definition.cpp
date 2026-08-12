#include "kspacejet/recon/graph/pipeline_definition.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
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

struct StableSemVer {
  std::string_view major;
  std::string_view minor;
  std::string_view patch;
};

[[nodiscard]] bool is_valid_numeric_identifier(const std::string_view value) noexcept {
  return !value.empty() && std::ranges::all_of(value, is_ascii_digit) && (value.size() == 1U || value.front() != '0');
}

[[nodiscard]] std::optional<StableSemVer> parse_stable_semver(const std::string_view value) noexcept {
  const auto first_dot = value.find('.');
  if (first_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const auto second_dot = value.find('.', first_dot + 1U);
  if (second_dot == std::string_view::npos || value.find('.', second_dot + 1U) != std::string_view::npos) {
    return std::nullopt;
  }
  const StableSemVer result{.major = value.substr(0U, first_dot),
                            .minor = value.substr(first_dot + 1U, second_dot - first_dot - 1U),
                            .patch = value.substr(second_dot + 1U)};
  if (!is_valid_numeric_identifier(result.major) || !is_valid_numeric_identifier(result.minor) ||
      !is_valid_numeric_identifier(result.patch)) {
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] bool is_valid_semver_identifier(const std::string_view value,
                                              const bool reject_leading_zero_numeric) noexcept {
  if (value.empty() || !std::ranges::all_of(value, [](const char character) {
        return is_ascii_alphanumeric(character) || character == '-';
      })) {
    return false;
  }
  const bool numeric = std::ranges::all_of(value, is_ascii_digit);
  return !reject_leading_zero_numeric || !numeric || value.size() == 1U || value.front() != '0';
}

[[nodiscard]] bool are_valid_dot_separated_semver_identifiers(const std::string_view value,
                                                              const bool reject_leading_zero_numeric) noexcept {
  std::size_t begin = 0U;
  while (begin < value.size()) {
    const auto dot = value.find('.', begin);
    const auto identifier = value.substr(begin, dot == std::string_view::npos ? std::string_view::npos : dot - begin);
    if (!is_valid_semver_identifier(identifier, reject_leading_zero_numeric)) {
      return false;
    }
    if (dot == std::string_view::npos) {
      return true;
    }
    begin = dot + 1U;
  }
  return false;
}

[[nodiscard]] bool is_strict_semver(const std::string_view value) noexcept {
  const auto plus = value.find('+');
  if (plus != std::string_view::npos && value.find('+', plus + 1U) != std::string_view::npos) {
    return false;
  }
  const auto core_and_prerelease = value.substr(0U, plus);
  if (plus != std::string_view::npos && !are_valid_dot_separated_semver_identifiers(value.substr(plus + 1U), false)) {
    return false;
  }
  const auto hyphen = core_and_prerelease.find('-');
  if (hyphen != std::string_view::npos &&
      !are_valid_dot_separated_semver_identifiers(core_and_prerelease.substr(hyphen + 1U), true)) {
    return false;
  }
  return parse_stable_semver(core_and_prerelease.substr(0U, hyphen)).has_value();
}

enum class VersionComparatorKind {
  equal,
  greater,
  greater_or_equal,
  less,
  less_or_equal,
};

struct VersionComparator {
  VersionComparatorKind kind;
  StableSemVer version;
};

[[nodiscard]] Status invalid_provider_version_requirement(const std::string_view path) {
  return validation_error(path, "must be an exact stable MAJOR.MINOR.PATCH version or whitespace-separated stable "
                                "SemVer comparator clauses");
}

[[nodiscard]] Result<std::vector<VersionComparator>> parse_provider_version_requirement(const std::string_view value,
                                                                                        const std::string_view path) {
  if (value.empty() || value.size() > 256U) {
    return invalid_provider_version_requirement(path);
  }
  if (const auto exact = parse_stable_semver(value); exact.has_value()) {
    return std::vector<VersionComparator>{{.kind = VersionComparatorKind::equal, .version = *exact}};
  }

  const auto is_separator = [](const char character) {
    return character == ' ' || character == '\t';
  };
  std::vector<VersionComparator> clauses;
  std::size_t begin = 0U;
  while (begin < value.size()) {
    if (is_separator(value[begin])) {
      return invalid_provider_version_requirement(path);
    }
    std::size_t end = begin;
    while (end < value.size() && !is_separator(value[end])) {
      ++end;
    }
    const auto clause = value.substr(begin, end - begin);
    VersionComparatorKind kind;
    std::size_t operator_size = 0U;
    if (clause.starts_with(">=")) {
      kind = VersionComparatorKind::greater_or_equal;
      operator_size = 2U;
    } else if (clause.starts_with("<=")) {
      kind = VersionComparatorKind::less_or_equal;
      operator_size = 2U;
    } else if (clause.starts_with('>')) {
      kind = VersionComparatorKind::greater;
      operator_size = 1U;
    } else if (clause.starts_with('<')) {
      kind = VersionComparatorKind::less;
      operator_size = 1U;
    } else if (clause.starts_with('=')) {
      kind = VersionComparatorKind::equal;
      operator_size = 1U;
    } else {
      return invalid_provider_version_requirement(path);
    }
    const auto version = parse_stable_semver(clause.substr(operator_size));
    if (!version.has_value()) {
      return invalid_provider_version_requirement(path);
    }
    clauses.push_back({.kind = kind, .version = *version});

    if (end == value.size()) {
      break;
    }
    begin = end;
    while (begin < value.size() && is_separator(value[begin])) {
      ++begin;
    }
    if (begin == value.size()) {
      return invalid_provider_version_requirement(path);
    }
  }
  return clauses;
}

[[nodiscard]] int compare_numeric_identifiers(const std::string_view left, const std::string_view right) noexcept {
  if (left.size() != right.size()) {
    return left.size() < right.size() ? -1 : 1;
  }
  if (left == right) {
    return 0;
  }
  return left < right ? -1 : 1;
}

[[nodiscard]] int compare_stable_semver(const StableSemVer& left, const StableSemVer& right) noexcept {
  for (const auto [left_component, right_component] :
       {std::pair{left.major, right.major}, std::pair{left.minor, right.minor}, std::pair{left.patch, right.patch}}) {
    const auto comparison = compare_numeric_identifiers(left_component, right_component);
    if (comparison != 0) {
      return comparison;
    }
  }
  return 0;
}

[[nodiscard]] bool satisfies(const StableSemVer& version, const VersionComparator& comparator) noexcept {
  const auto comparison = compare_stable_semver(version, comparator.version);
  switch (comparator.kind) {
    case VersionComparatorKind::equal:
      return comparison == 0;
    case VersionComparatorKind::greater:
      return comparison > 0;
    case VersionComparatorKind::greater_or_equal:
      return comparison >= 0;
    case VersionComparatorKind::less:
      return comparison < 0;
    case VersionComparatorKind::less_or_equal:
      return comparison <= 0;
  }
  return false;
}

[[nodiscard]] Result<std::uint64_t> require_positive_integer(const Json& object, const std::string_view key,
                                                             const std::string_view path) {
  const auto iterator = object.find(std::string(key));
  if (iterator == object.end() || !iterator->is_number_unsigned()) {
    return validation_error(path, "field '" + std::string(key) + "' must be a positive integer");
  }
  const auto value = iterator->get<std::uint64_t>();
  if (value == 0U || value > kMaxCanonicalJsonInteger) {
    return validation_error(path, "field '" + std::string(key) + "' is outside the canonical positive range");
  }
  return value;
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

[[nodiscard]] bool has_forbidden_runtime_field(const Json& value, std::string& path) {
  static const std::unordered_set<std::string> forbidden{
    "task_count",
    "tasks",
    "key_shards",
    "key_slot_tables",
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
  };
  if (value.is_object()) {
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      if (forbidden.contains(iterator.key())) {
        path = iterator.key();
        return true;
      }
      if (has_forbidden_runtime_field(iterator.value(), path)) {
        path = iterator.key() + "." + path;
        return true;
      }
    }
  } else if (value.is_array()) {
    for (std::size_t index = 0; index < value.size(); ++index) {
      if (has_forbidden_runtime_field(value[index], path)) {
        path = "[" + std::to_string(index) + "]" + path;
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] Result<ProviderSelection> parse_provider(const Json& value, const std::string_view path) {
  const auto status =
    validate_object_keys(value, path, {"alias", "provider_id", "version_requirement", "required_abi_major"},
                         {"alias", "provider_id", "version_requirement", "required_abi_major"});
  if (!status.ok())
    return status;
  auto alias = require_identifier(value, "alias", path);
  auto provider_id = require_qualified_identifier(value, "provider_id", path);
  auto version_requirement = require_bounded_string(value, "version_requirement", path, 256U);
  auto abi_major = require_positive_integer(value, "required_abi_major", path);
  if (!alias.ok())
    return alias.status();
  if (!provider_id.ok())
    return provider_id.status();
  if (!version_requirement.ok())
    return version_requirement.status();
  auto parsed_requirement =
    parse_provider_version_requirement(version_requirement.value(), std::string(path) + ".version_requirement");
  if (!parsed_requirement.ok())
    return parsed_requirement.status();
  if (!abi_major.ok())
    return abi_major.status();
  return ProviderSelection{.alias = std::move(alias).value(),
                           .provider_id = std::move(provider_id).value(),
                           .version_requirement = std::move(version_requirement).value(),
                           .required_abi_major = abi_major.value()};
}

[[nodiscard]] Result<InterfaceRequirement> parse_interface_requirement(const Json& object,
                                                                       const std::string_view path) {
  InterfaceRequirement result;
  if (const auto iterator = object.find("requires_interface_revision"); iterator != object.end()) {
    if (!iterator->is_string() || iterator->get_ref<const std::string&>().empty()) {
      return validation_error(path, "requires_interface_revision must be a non-empty string");
    }
    const auto& revision = iterator->get_ref<const std::string&>();
    auto characters = utf8_character_count(revision, path);
    if (!characters.ok()) {
      return characters.status();
    }
    if (characters.value() > 128U) {
      return validation_error(path, "requires_interface_revision exceeds its maximum character length");
    }
    result.revision = revision;
  }
  if (const auto iterator = object.find("requires_interface_digest"); iterator != object.end()) {
    if (!iterator->is_string()) {
      return validation_error(path, "requires_interface_digest must be a sha256 digest string");
    }
    auto digest = ArtifactDigest::parse(iterator->get<std::string>(), "requires_interface_digest");
    if (!digest.ok())
      return digest.status();
    result.digest = std::move(digest).value();
  }
  return result;
}

[[nodiscard]] Result<PipelineNode> parse_node(const Json& value, const std::string_view path) {
  const auto status = validate_object_keys(value, path, {"id", "operator", "config"}, {"id", "operator", "config"});
  if (!status.ok())
    return status;
  auto id = require_identifier(value, "id", path);
  if (!id.ok())
    return id.status();
  const auto operator_status =
    validate_object_keys(value.at("operator"), std::string(path) + ".operator", {"provider", "id"},
                         {"provider", "id", "requires_interface_revision", "requires_interface_digest"});
  if (!operator_status.ok())
    return operator_status;
  auto provider = require_identifier(value.at("operator"), "provider", std::string(path) + ".operator");
  auto operator_id = require_identifier(value.at("operator"), "id", std::string(path) + ".operator");
  auto interface_requirement = parse_interface_requirement(value.at("operator"), std::string(path) + ".operator");
  if (!provider.ok())
    return provider.status();
  if (!operator_id.ok())
    return operator_id.status();
  if (!interface_requirement.ok())
    return interface_requirement.status();
  const auto config_status = require_object(value.at("config"), std::string(path) + ".config");
  if (!config_status.ok())
    return config_status;
  auto config = canonicalize_json(value.at("config").dump());
  if (!config.ok())
    return config.status();
  return PipelineNode{.id = std::move(id).value(),
                      .provider_alias = std::move(provider).value(),
                      .operator_id = std::move(operator_id).value(),
                      .interface_requirement = std::move(interface_requirement).value(),
                      .canonical_config = std::move(config).value()};
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
                                                   const std::vector<EgressPort>& egress) {
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
  sort_array_by_member(root["provider_requirements"], "alias");
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
  if (providers.size() != definition.providers().size()) {
    return Status::ValidationError("resolver did not provide an exact resolution for every Provider requirement");
  }
  std::unordered_map<std::string, const ProviderSelection*> selections;
  for (const auto& selection : definition.providers())
    selections.emplace(selection.alias, &selection);
  std::unordered_set<std::string> aliases;
  for (auto& provider : providers) {
    const auto selection = selections.find(provider.alias);
    if (selection == selections.end() || !aliases.insert(provider.alias).second) {
      return Status::ValidationError("resolver returned unknown or duplicate Provider alias '" + provider.alias + "'");
    }
    if (provider.provider_id != selection->second->provider_id ||
        provider.abi_major != selection->second->required_abi_major) {
      return Status::ValidationError("resolved Provider does not match authored provider_id/ABI");
    }
    const auto provider_version = parse_stable_semver(provider.version);
    if (!provider_version.has_value()) {
      return Status::ValidationError("resolved Provider must carry an exact stable MAJOR.MINOR.PATCH version");
    }
    auto requirement = parse_provider_version_requirement(selection->second->version_requirement,
                                                          "resolved Provider version_requirement");
    if (!requirement.ok()) {
      return requirement.status();
    }
    const auto satisfies_requirement = std::ranges::all_of(requirement.value(), [&](const VersionComparator& clause) {
      return satisfies(*provider_version, clause);
    });
    if (!satisfies_requirement) {
      return Status::ValidationError("resolved Provider version '" + provider.version +
                                     "' does not satisfy authored version_requirement '" +
                                     selection->second->version_requirement + "'");
    }
    std::unordered_set<std::string> operators;
    for (const auto& operator_value : provider.operators) {
      if (operator_value.id.empty() || operator_value.interface_revision.empty() ||
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
    if (node.interface_requirement.revision.has_value() &&
        node.interface_requirement.revision.value() != operator_value->interface_revision) {
      return Status::ValidationError("resolved Provider interface revision does not satisfy node '" + node.id + "'");
    }
    if (node.interface_requirement.digest.has_value() &&
        node.interface_requirement.digest.value() != operator_value->contract_digest) {
      return Status::ValidationError("resolved Provider interface digest does not satisfy node '" + node.id + "'");
    }
  }
  std::ranges::sort(providers, {}, &ResolvedProvider::alias);
  for (auto& provider : providers)
    std::ranges::sort(provider.operators, {}, &ResolvedOperator::id);
  return providers;
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
  std::string forbidden_path;
  if (has_forbidden_runtime_field(root, forbidden_path)) {
    return Status::ValidationError("PipelineDefinition must not contain scan-specific runtime field '" +
                                   forbidden_path + "'");
  }
  const auto root_status =
    validate_object_keys(root, "$",
                         {"schema_version", "kind", "pipeline", "allowed_profiles", "parameters",
                          "provider_requirements", "nodes", "edges", "bindings", "annotations"},
                         {"$schema", "schema_version", "kind", "pipeline", "allowed_profiles", "parameters",
                          "provider_requirements", "nodes", "edges", "bindings", "annotations"});
  if (!root_status.ok())
    return root_status;
  if (const auto schema = root.find("$schema");
      schema != root.end() && (!schema->is_string() || schema->get_ref<const std::string&>() !=
                                                         "https://json-schema.org/draft/2020-12/schema")) {
    return validation_error("$.$schema", "must equal 'https://json-schema.org/draft/2020-12/schema' when present");
  }
  if (!root.at("schema_version").is_string() ||
      root.at("schema_version").get<std::string>() != kPipelineDefinitionSchemaVersion) {
    return validation_error("$.schema_version", "must equal 'kspacejet.pipeline/v1'");
  }
  if (!root.at("kind").is_string() || root.at("kind").get<std::string>() != "PipelineDefinition") {
    return validation_error("$.kind", "must equal 'PipelineDefinition'");
  }
  const auto pipeline_status = validate_object_keys(
    root.at("pipeline"), "$.pipeline", {"id", "revision", "display_name"}, {"id", "revision", "display_name"});
  if (!pipeline_status.ok())
    return pipeline_status;
  auto pipeline_id = require_qualified_identifier(root.at("pipeline"), "id", "$.pipeline");
  auto revision = require_string(root.at("pipeline"), "revision", "$.pipeline");
  auto display_name = require_bounded_string(root.at("pipeline"), "display_name", "$.pipeline", 256U);
  if (!pipeline_id.ok())
    return pipeline_id.status();
  if (!revision.ok())
    return revision.status();
  if (!is_strict_semver(revision.value())) {
    return validation_error("$.pipeline.revision", "must be a strict Semantic Version 2.0.0 value");
  }
  if (!display_name.ok())
    return display_name.status();
  const auto parameters_status = require_object(root.at("parameters"), "$.parameters");
  const auto annotations_status = require_object(root.at("annotations"), "$.annotations");
  if (!parameters_status.ok())
    return parameters_status;
  if (!annotations_status.ok())
    return annotations_status;

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
    auto node = parse_node(root.at("nodes")[index], "$.nodes[" + std::to_string(index) + "]");
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
    if (type.value() == "ksj.kspace-frame/v1") {
      return validation_error(path, "ksj.kspace-frame is an internal completed FrameSlotContext and must not be a "
                                    "public ingress; it must arrive through a resolved internal typed graph edge");
    }
    if (type.value() != "ismrmrd.acquisition/v1" && type.value() != "ismrmrd.waveform/v1") {
      return validation_error(path, "ingress type must be public ISMRMRD acquisition or waveform");
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
    if (type.value() != "ismrmrd.image/v1" && type.value() != "ismrmrd.waveform/v1") {
      return validation_error(path, "egress type must be public ISMRMRD image or waveform");
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
    std::vector<std::string> consumers;
    for (const auto& consumer : value.at("consumers")) {
      const auto consumer_status = validate_object_keys(consumer, path + ".consumers", {"node"}, {"node"});
      if (!consumer_status.ok())
        return consumer_status;
      auto node = require_identifier(consumer, "node", path + ".consumers");
      if (!node.ok())
        return node.status();
      if (find_node(nodes, node.value()) == nullptr || !consumers_seen.insert(node.value()).second) {
        return validation_error(path, "calibration consumer is unknown or duplicated");
      }
      consumers.push_back(std::move(node).value());
    }
    calibration_bindings.push_back(CalibrationBinding{
      .id = std::move(id).value(), .producer = std::move(producer).value(), .consumer_nodes = std::move(consumers)});
  }

  const auto merge_array_status = require_array(root.at("bindings").at("merge"), "$.bindings.merge");
  if (!merge_array_status.ok())
    return merge_array_status;
  if (!root.at("bindings").at("merge").empty()) {
    return validation_error("$.bindings.merge", "generic MergeBinding is deferred from the M0/M1 runtime");
  }
  std::vector<MergeBinding> merge_bindings;

  const auto graph_status = validate_dag_and_reachability(nodes, edges, ingress_ports, egress_ports);
  if (!graph_status.ok())
    return graph_status;

  normalize_pipeline_json(root);
  auto canonical_document = canonicalize_json(root.dump());
  if (!canonical_document.ok())
    return canonical_document.status();
  Json semantic_root = root;
  semantic_root["annotations"] = Json::object();
  auto semantic_document = canonicalize_json(semantic_root.dump());
  if (!semantic_document.ok())
    return semantic_document.status();
  auto artifact = domain_separated_sha256_digest("kspacejet:artifact:pipeline-definition:1", canonical_document.value(),
                                                 "PipelineDefinition artifact digest");
  if (!artifact.ok())
    return artifact.status();
  auto semantic = domain_separated_sha256_digest("kspacejet:semantic:pipeline-definition:1", semantic_document.value(),
                                                 "PipelineDefinition semantic digest");
  if (!semantic.ok())
    return semantic.status();
  return PipelineDefinition{std::move(pipeline_id).value(),
                            std::move(revision).value(),
                            std::move(display_name).value(),
                            std::move(profiles),
                            std::move(providers),
                            std::move(nodes),
                            std::move(edges),
                            std::move(ingress_ports),
                            std::move(egress_ports),
                            std::move(calibration_bindings),
                            std::move(merge_bindings),
                            std::move(canonical_document).value(),
                            std::move(artifact).value(),
                            std::move(semantic).value()};
}

Result<ResolvedPipeline> ResolvedPipeline::resolve(const PipelineDefinition& definition,
                                                   std::vector<ResolvedProvider> providers) {
  auto normalized_providers = normalize_resolved_providers(definition, std::move(providers));
  if (!normalized_providers.ok())
    return normalized_providers.status();
  Json resolved{
    {"schema_version", kResolvedPipelineSchemaVersion},
    {"kind", "ResolvedPipeline"},
    {"pipeline_definition_artifact_digest", definition.artifact_digest().value()},
    {"pipeline_definition_semantic_digest", definition.semantic_digest().value()},
    {"providers", Json::array()},
  };
  for (const auto& provider : normalized_providers.value()) {
    Json provider_json{{"alias", provider.alias},
                       {"provider_id", provider.provider_id},
                       {"version", provider.version},
                       {"abi_major", provider.abi_major},
                       {"bundle_digest", provider.bundle_digest.value()},
                       {"operators", Json::array()}};
    for (const auto& operator_value : provider.operators) {
      provider_json["operators"].push_back({{"id", operator_value.id},
                                            {"interface_revision", operator_value.interface_revision},
                                            {"contract_digest", operator_value.contract_digest.value()}});
    }
    resolved["providers"].push_back(std::move(provider_json));
  }
  auto canonical = canonicalize_json(resolved.dump());
  if (!canonical.ok())
    return canonical.status();
  auto digest = domain_separated_sha256_digest("kspacejet:artifact:resolved-pipeline:1", canonical.value(),
                                               "ResolvedPipeline artifact digest");
  if (!digest.ok())
    return digest.status();
  return ResolvedPipeline{definition, std::move(normalized_providers).value(), std::move(canonical).value(),
                          std::move(digest).value()};
}

} // namespace ksj::recon::graph
