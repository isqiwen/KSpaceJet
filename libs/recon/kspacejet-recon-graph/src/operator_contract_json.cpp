#include "kspacejet/recon/graph/operator_contract_json.hpp"

#include "kspacejet/recon/graph/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::recon::graph {
namespace {

using Json = nlohmann::json;

// Provider-owned contract files are small control-plane artifacts. Keep their
// admission boundary deliberately tighter than the generic graph artifact
// limit: a contract declares an interface, never a payload or a runtime plan.
constexpr JsonParseLimits kOperatorContractJsonParseLimits{
  .max_document_bytes = 256U * 1024U,
  .max_depth = 16U,
  .max_array_elements = 1'024U,
  .max_object_members = 1'024U,
  .max_string_bytes = 16U * 1024U,
};

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
  if (!object_status.ok()) {
    return object_status;
  }
  for (const auto key : required) {
    if (!object.contains(std::string(key))) {
      return validation_error(path, "is missing required field '" + std::string(key) + "'");
    }
  }
  for (auto member = object.begin(); member != object.end(); ++member) {
    const auto known = std::ranges::any_of(allowed, [&](const std::string_view candidate) {
      return member.key() == candidate;
    });
    if (!known) {
      return validation_error(path, "contains unknown field '" + member.key() + "'");
    }
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

[[nodiscard]] Result<PortSpec> parse_port(const Json& value, const std::string_view path) {
  const auto object_status = require_object(value, path);
  if (!object_status.ok()) {
    return object_status;
  }

  const auto direction_iterator = value.find("direction");
  if (direction_iterator == value.end() || !direction_iterator->is_string()) {
    return validation_error(path, "field 'direction' must be 'input' or 'output'");
  }
  const auto direction_string = direction_iterator->get<std::string>();
  PortDirection direction;
  if (direction_string == "input") {
    direction = PortDirection::input;
  } else if (direction_string == "output") {
    direction = PortDirection::output;
  } else {
    return validation_error(path, "field 'direction' must be 'input' or 'output'");
  }

  const auto key_status =
    validate_object_keys(value, path, {"name", "type_ref", "direction"}, {"name", "type_ref", "direction"});
  if (!key_status.ok()) {
    return key_status;
  }

  auto name = require_identifier(value, "name", path);
  auto type_ref = require_string(value, "type_ref", path);
  if (!name.ok()) {
    return name.status();
  }
  if (!type_ref.ok()) {
    return type_ref.status();
  }

  return PortSpec{.name = std::move(name).value(), .type_ref = std::move(type_ref).value(), .direction = direction};
}

[[nodiscard]] Result<std::string> canonical_current_operator_contract_json(const std::string_view document) {
  auto canonical = canonicalize_json(document, kOperatorContractJsonParseLimits);
  if (!canonical.ok()) {
    return canonical.status();
  }
  return canonical;
}

[[nodiscard]] Result<Json> parse_canonical_document(const std::string_view canonical_document) {
  try {
    return Json::parse(canonical_document.begin(), canonical_document.end());
  } catch (const Json::exception& exception) {
    return Status::InternalError("bounded OperatorContract canonicalization produced invalid JSON: " +
                                 std::string(exception.what()));
  }
}

} // namespace

Result<OperatorContract> parse_operator_contract_json(const std::string_view document) {
  auto canonical = canonical_current_operator_contract_json(document);
  if (!canonical.ok()) {
    return canonical.status();
  }
  auto root = parse_canonical_document(canonical.value());
  if (!root.ok()) {
    return root.status();
  }

  const auto root_status =
    validate_object_keys(root.value(), "$", {"kind", "operator_id", "ports"}, {"kind", "operator_id", "ports"});
  if (!root_status.ok()) {
    return root_status;
  }
  const auto kind = root.value().find("kind");
  if (kind == root.value().end() || !kind->is_string() || kind->get_ref<const std::string&>() != "OperatorContract") {
    return validation_error("$.kind", "must equal 'OperatorContract'");
  }

  auto operator_id = require_identifier(root.value(), "operator_id", "$");
  if (!operator_id.ok()) {
    return operator_id.status();
  }
  const auto ports_status = require_array(root.value().at("ports"), "$.ports");
  if (!ports_status.ok()) {
    return ports_status;
  }
  if (root.value().at("ports").empty()) {
    return validation_error("$.ports", "must contain at least one port");
  }

  std::vector<PortSpec> ports;
  ports.reserve(root.value().at("ports").size());
  for (std::size_t index = 0U; index < root.value().at("ports").size(); ++index) {
    auto port = parse_port(root.value().at("ports")[index], "$.ports[" + std::to_string(index) + "]");
    if (!port.ok()) {
      return port.status();
    }
    ports.push_back(std::move(port).value());
  }

  auto contract = OperatorContract::create({.operator_id = std::move(operator_id).value(), .ports = std::move(ports)});
  if (!contract.ok()) {
    return contract.status();
  }
  return std::move(contract).value();
}

} // namespace ksj::recon::graph
