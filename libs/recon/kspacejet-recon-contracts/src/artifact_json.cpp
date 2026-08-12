#include "kspacejet/recon/artifact_json.hpp"

#include "utf8.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ksj::recon {
namespace {

using Json = nlohmann::json;

struct JsonParseLimits {
  std::size_t max_document_bytes;
  std::size_t max_depth;
  std::size_t max_array_elements;
  std::size_t max_object_members;
  std::size_t max_string_bytes;
};

// Admission and terminal audit records are bounded control-plane artifacts.
// The limits are intentionally much tighter than the generic graph artifact
// boundary, while still allowing the schema's largest reason string.
inline constexpr JsonParseLimits kRecordJsonParseLimits{
  .max_document_bytes = 1U * 1024U * 1024U,
  .max_depth = 16U,
  .max_array_elements = 4'096U,
  .max_object_members = 64U,
  // The largest schema-bounded field is a 4,096-code-point admission reason.
  // A valid UTF-8 code point needs at most four bytes.
  .max_string_bytes = 4U * 4U * 1024U,
};

[[nodiscard]] Status validation_error(const std::string_view path, const std::string_view message) {
  return Status::ValidationError(std::string(path) + " " + std::string(message));
}

[[nodiscard]] Status parse_error(std::string message) {
  return Status::ParseError(std::move(message));
}

[[nodiscard]] bool is_allowed_member(const std::string_view member,
                                     const std::initializer_list<std::string_view> allowed) noexcept {
  for (const auto candidate : allowed) {
    if (member == candidate) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::string child_path(const std::string_view parent, const std::string_view child) {
  return std::string(parent) + "." + std::string(child);
}

[[nodiscard]] const Json& member(const Json& object, const std::string_view name) {
  return object.at(std::string(name));
}

[[nodiscard]] Status validate_object_members(const Json& value, const std::string_view path,
                                             const std::initializer_list<std::string_view> required,
                                             const std::initializer_list<std::string_view> allowed) {
  if (!value.is_object()) {
    return validation_error(path, "must be an object");
  }
  for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
    if (!is_allowed_member(iterator.key(), allowed)) {
      return validation_error(path, "contains unsupported member '" + iterator.key() + "'");
    }
  }
  for (const auto name : required) {
    if (!value.contains(std::string(name))) {
      return validation_error(path, "is missing required member '" + std::string(name) + "'");
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_schema_declaration(const Json& object, const std::string_view path) {
  if (!object.contains("$schema")) {
    return Status::Ok();
  }
  const auto& value = member(object, "$schema");
  if (!value.is_string() || value.get_ref<const std::string&>() != kJsonSchemaDraft202012) {
    return validation_error(child_path(path, "$schema"), "must equal the JSON Schema 2020-12 URI when present");
  }
  return Status::Ok();
}

[[nodiscard]] Result<std::string> require_string(const Json& object, const std::string_view name,
                                                 const std::string_view path) {
  const auto& value = member(object, name);
  if (!value.is_string()) {
    return validation_error(child_path(path, name), "must be a string");
  }
  return value.get<std::string>();
}

[[nodiscard]] Result<std::string> require_bounded_string(const Json& object, const std::string_view name,
                                                         const std::string_view path, const std::size_t minimum,
                                                         const std::size_t maximum) {
  auto value = require_string(object, name, path);
  if (!value.ok()) {
    return value.status();
  }
  auto code_point_count = detail::utf8_code_point_count(value.value(), child_path(path, name));
  if (!code_point_count.ok()) {
    return code_point_count.status();
  }
  if (code_point_count.value() < minimum || code_point_count.value() > maximum) {
    return validation_error(child_path(path, name), "has a Unicode length outside its v1 schema bounds");
  }
  return value;
}

[[nodiscard]] Result<std::optional<std::string>> optional_string(const Json& object, const std::string_view name,
                                                                 const std::string_view path) {
  if (!object.contains(std::string(name))) {
    return std::optional<std::string>{};
  }
  auto value = require_string(object, name, path);
  if (!value.ok()) {
    return value.status();
  }
  return std::optional<std::string>{std::move(value).value()};
}

[[nodiscard]] Result<Quantity> require_quantity(const Json& object, const std::string_view name,
                                                const std::string_view path) {
  const auto& value = member(object, name);
  Quantity result = 0U;
  if (value.is_number_unsigned()) {
    result = value.get<Quantity>();
  } else if (value.is_number_integer()) {
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) {
      return validation_error(child_path(path, name), "must be a non-negative integer");
    }
    result = static_cast<Quantity>(signed_value);
  } else {
    return validation_error(child_path(path, name), "must be a non-negative integer");
  }
  if (result > kMaxCanonicalJsonInteger) {
    return validation_error(child_path(path, name), "exceeds the exact v1 integer range");
  }
  return result;
}

[[nodiscard]] Result<std::optional<Quantity>> optional_quantity(const Json& object, const std::string_view name,
                                                                const std::string_view path) {
  if (!object.contains(std::string(name))) {
    return std::optional<Quantity>{};
  }
  auto value = require_quantity(object, name, path);
  if (!value.ok()) {
    return value.status();
  }
  return std::optional<Quantity>{value.value()};
}

// nlohmann::json's default DOM parser silently retains one duplicate object
// member.  This SAX adapter enforces the public record boundary before any
// value is materialized, so identity-bearing references cannot be ambiguous.
class BoundedJsonSax final : public nlohmann::json_sax<Json> {
public:
  explicit BoundedJsonSax(const JsonParseLimits& limits) : limits_(limits) {}

  [[nodiscard]] const Status& status() const noexcept { return status_; }
  [[nodiscard]] bool has_root() const noexcept { return root_.has_value(); }
  [[nodiscard]] Json take_root() { return std::move(root_).value(); }

  bool null() override { return append_value(Json(nullptr)); }
  bool boolean(const bool value) override { return append_value(Json(value)); }

  bool number_integer(const number_integer_t value) override {
    if (value < -static_cast<number_integer_t>(kMaxCanonicalJsonInteger) ||
        value > static_cast<number_integer_t>(kMaxCanonicalJsonInteger)) {
      return fail_validation("JSON integer exceeds the exact v1 range");
    }
    return append_value(Json(value));
  }

  bool number_unsigned(const number_unsigned_t value) override {
    if (value > static_cast<number_unsigned_t>(kMaxCanonicalJsonInteger)) {
      return fail_validation("JSON integer exceeds the exact v1 range");
    }
    return append_value(Json(value));
  }

  bool number_float(number_float_t /*value*/, const string_t& /*raw*/) override {
    return fail_validation("floating-point JSON values are not permitted in v1 records");
  }

  bool string(string_t& value) override {
    if (value.size() > limits_.max_string_bytes) {
      return fail_validation("JSON string exceeds the configured maximum length");
    }
    return append_value(Json(std::move(value)));
  }

  bool binary(binary_t& /*value*/) override {
    return fail_validation("binary JSON values are not permitted in textual v1 records");
  }

  bool start_object(const std::size_t elements) override {
    if (elements != static_cast<std::size_t>(-1) && elements > limits_.max_object_members) {
      return fail_validation("JSON object exceeds the configured member limit");
    }
    return start_container(Json::value_t::object);
  }

  bool key(string_t& value) override {
    if (frames_.empty() || !frames_.back().value.is_object()) {
      return fail_parse("JSON object key appeared outside an object");
    }
    if (value.size() > limits_.max_string_bytes) {
      return fail_validation("JSON object key exceeds the configured maximum length");
    }

    auto& frame = frames_.back();
    if (frame.pending_key.has_value()) {
      return fail_parse("JSON object key was not followed by a value");
    }
    if (frame.member_count == limits_.max_object_members) {
      return fail_validation("JSON object exceeds the configured member limit");
    }

    std::string key_value = std::move(value);
    if (!frame.keys.insert(key_value).second) {
      return fail_validation("JSON object contains a duplicate key: '" + key_value + "'");
    }
    ++frame.member_count;
    frame.pending_key = std::move(key_value);
    return true;
  }

  bool end_object() override { return finish_container(Json::value_t::object); }

  bool start_array(const std::size_t elements) override {
    if (elements != static_cast<std::size_t>(-1) && elements > limits_.max_array_elements) {
      return fail_validation("JSON array exceeds the configured element limit");
    }
    return start_container(Json::value_t::array);
  }

  bool end_array() override { return finish_container(Json::value_t::array); }

  bool parse_error(const std::size_t position, const std::string& /*last_token*/,
                   const nlohmann::detail::exception& exception) override {
    if (status_.ok()) {
      status_ = Status::ParseError("invalid JSON record at byte " + std::to_string(position) + ": " +
                                   std::string(exception.what()));
    }
    return false;
  }

private:
  struct Frame {
    Json value;
    std::unordered_set<std::string> keys;
    std::optional<std::string> pending_key;
    std::size_t member_count{0};
    std::size_t element_count{0};
  };

  bool fail_validation(std::string message) {
    if (status_.ok()) {
      status_ = Status::ValidationError(std::move(message));
    }
    return false;
  }

  bool fail_parse(std::string message) {
    if (status_.ok()) {
      status_ = Status::ParseError(std::move(message));
    }
    return false;
  }

  bool start_container(const Json::value_t kind) {
    if (frames_.size() >= limits_.max_depth) {
      return fail_validation("JSON nesting exceeds the configured maximum depth");
    }
    Frame frame;
    frame.value = Json(kind);
    frames_.push_back(std::move(frame));
    return true;
  }

  bool finish_container(const Json::value_t expected_kind) {
    if (frames_.empty() || frames_.back().value.type() != expected_kind) {
      return fail_parse("JSON container close does not match its opener");
    }
    if (frames_.back().pending_key.has_value()) {
      return fail_parse("JSON object ended before a member value");
    }

    Json value = std::move(frames_.back().value);
    frames_.pop_back();
    return append_value(std::move(value));
  }

  bool append_value(Json value) {
    if (frames_.empty()) {
      if (root_.has_value()) {
        return fail_parse("JSON document contains more than one root value");
      }
      root_ = std::move(value);
      return true;
    }

    auto& parent = frames_.back();
    if (parent.value.is_array()) {
      if (parent.element_count == limits_.max_array_elements) {
        return fail_validation("JSON array exceeds the configured element limit");
      }
      ++parent.element_count;
      parent.value.push_back(std::move(value));
      return true;
    }
    if (!parent.value.is_object() || !parent.pending_key.has_value()) {
      return fail_parse("JSON object value has no preceding key");
    }

    parent.value.emplace(std::move(parent.pending_key).value(), std::move(value));
    parent.pending_key.reset();
    return true;
  }

  const JsonParseLimits& limits_;
  std::vector<Frame> frames_;
  std::optional<Json> root_;
  Status status_;
};

[[nodiscard]] Result<Json> parse_bounded_json(const std::string_view document) {
  if (document.size() > kRecordJsonParseLimits.max_document_bytes) {
    return Status::ValidationError("JSON record exceeds the configured maximum byte size");
  }

  BoundedJsonSax sax(kRecordJsonParseLimits);
  try {
    const auto parsed =
      Json::sax_parse(document.begin(), document.end(), &sax, Json::input_format_t::json, true, false);
    if (!parsed || !sax.status().ok()) {
      return sax.status().ok() ? parse_error("invalid JSON record") : sax.status();
    }
  } catch (const Json::exception& exception) {
    return parse_error("invalid JSON record: " + std::string(exception.what()));
  }
  if (!sax.has_root()) {
    return parse_error("JSON record contains no root value");
  }
  return sax.take_root();
}

[[nodiscard]] Result<Json> canonicalize_value(const Json& value, const std::string_view path) {
  if (value.is_null() || value.is_boolean() || value.is_string()) {
    return value;
  }
  if (value.is_number_unsigned()) {
    const auto number = value.get<Quantity>();
    if (number > kMaxCanonicalJsonInteger) {
      return validation_error(path, "exceeds the exact v1 integer range");
    }
    return Json(number);
  }
  if (value.is_number_integer()) {
    const auto number = value.get<std::int64_t>();
    if (number < -static_cast<std::int64_t>(kMaxCanonicalJsonInteger) ||
        number > static_cast<std::int64_t>(kMaxCanonicalJsonInteger)) {
      return validation_error(path, "exceeds the exact v1 integer range");
    }
    return Json(number);
  }
  if (value.is_number_float()) {
    return validation_error(path, "is a floating-point JSON value; v1 records require exact values");
  }
  if (value.is_array()) {
    Json result = Json::array();
    for (std::size_t index = 0; index < value.size(); ++index) {
      auto element = canonicalize_value(value[index], std::string(path) + "[" + std::to_string(index) + "]");
      if (!element.ok()) {
        return element.status();
      }
      result.push_back(std::move(element).value());
    }
    return result;
  }
  if (value.is_object()) {
    Json result = Json::object();
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      auto nested = canonicalize_value(iterator.value(), std::string(path) + "." + iterator.key());
      if (!nested.ok()) {
        return nested.status();
      }
      result[iterator.key()] = std::move(nested).value();
    }
    return result;
  }
  return validation_error(path, "has an unsupported JSON value kind");
}

[[nodiscard]] Status validate_serialization_limits(const Json& value, const std::size_t container_depth,
                                                   const std::string_view path) {
  if (value.is_null() || value.is_boolean() || value.is_number()) {
    return Status::Ok();
  }
  if (value.is_string()) {
    if (value.get_ref<const std::string&>().size() > kRecordJsonParseLimits.max_string_bytes) {
      return validation_error(path, "exceeds the public JSON string byte limit");
    }
    return Status::Ok();
  }
  if (container_depth >= kRecordJsonParseLimits.max_depth) {
    return validation_error(path, "exceeds the public JSON nesting limit");
  }
  if (value.is_array()) {
    if (value.size() > kRecordJsonParseLimits.max_array_elements) {
      return validation_error(path, "exceeds the public JSON array element limit");
    }
    for (std::size_t index = 0U; index < value.size(); ++index) {
      const auto status = validate_serialization_limits(value[index], container_depth + 1U,
                                                        std::string(path) + "[" + std::to_string(index) + "]");
      if (!status.ok()) {
        return status;
      }
    }
    return Status::Ok();
  }
  if (value.is_object()) {
    if (value.size() > kRecordJsonParseLimits.max_object_members) {
      return validation_error(path, "exceeds the public JSON object member limit");
    }
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
      if (iterator.key().size() > kRecordJsonParseLimits.max_string_bytes) {
        return validation_error(path, "contains an object key above the public JSON byte limit");
      }
      const auto status =
        validate_serialization_limits(iterator.value(), container_depth + 1U, std::string(path) + "." + iterator.key());
      if (!status.ok()) {
        return status;
      }
    }
    return Status::Ok();
  }
  return validation_error(path, "has an unsupported JSON value kind");
}

[[nodiscard]] Result<std::string> serialize_canonical_json(const Json& value) {
  auto canonical = canonicalize_value(value, "$");
  if (!canonical.ok()) {
    return canonical.status();
  }
  const auto limit_status = validate_serialization_limits(canonical.value(), 0U, "$");
  if (!limit_status.ok()) {
    return limit_status;
  }
  try {
    auto document = std::move(canonical).value().dump(-1, ' ', false, Json::error_handler_t::strict);
    if (document.size() > kRecordJsonParseLimits.max_document_bytes) {
      return Status::ValidationError("canonical JSON record exceeds the public byte limit");
    }
    return document;
  } catch (const Json::exception& exception) {
    return Status::ValidationError("unable to serialize canonical JSON record: " + std::string(exception.what()));
  }
}

[[nodiscard]] Json resource_vector_json(const ResourceVector& resources) {
  Json devices = Json::array();
  for (const auto& device : resources.devices()) {
    devices.push_back({{"device_id", device.device_id()},
                       {"device_bytes", device.device_bytes()},
                       {"gpu_stream_slots", device.gpu_stream_slots()},
                       {"copy_engine_slots", device.copy_engine_slots()}});
  }
  return {
    {"host_normal_bytes", resources.host_normal_bytes()},
    {"host_pinned_bytes", resources.host_pinned_bytes()},
    {"host_hugepage_bytes", resources.host_hugepage_bytes()},
    {"shared_host_bytes", resources.shared_host_bytes()},
    {"spool_bytes", resources.spool_bytes()},
    {"transport_bytes", resources.transport_bytes()},
    {"descriptor_count", resources.descriptor_count()},
    {"async_token_count", resources.async_token_count()},
    {"cpu_leaf_permits", resources.cpu_leaf_permits()},
    {"backend_gang_permits", resources.backend_gang_permits()},
    {"provider_private_permits", resources.provider_private_permits()},
    {"io_slots", resources.io_slots()},
    {"devices", std::move(devices)},
  };
}

[[nodiscard]] ResourceVectorSpec resource_vector_spec(const ResourceVector& resources) {
  std::vector<DeviceResourceSlotSpec> devices;
  devices.reserve(resources.devices().size());
  for (const auto& device : resources.devices()) {
    devices.push_back({.device_id = device.device_id(),
                       .device_bytes = device.device_bytes(),
                       .gpu_stream_slots = device.gpu_stream_slots(),
                       .copy_engine_slots = device.copy_engine_slots()});
  }
  return {.host_normal_bytes = resources.host_normal_bytes(),
          .host_pinned_bytes = resources.host_pinned_bytes(),
          .host_hugepage_bytes = resources.host_hugepage_bytes(),
          .shared_host_bytes = resources.shared_host_bytes(),
          .spool_bytes = resources.spool_bytes(),
          .transport_bytes = resources.transport_bytes(),
          .descriptor_count = resources.descriptor_count(),
          .async_token_count = resources.async_token_count(),
          .cpu_leaf_permits = resources.cpu_leaf_permits(),
          .backend_gang_permits = resources.backend_gang_permits(),
          .provider_private_permits = resources.provider_private_permits(),
          .io_slots = resources.io_slots(),
          .devices = std::move(devices)};
}

[[nodiscard]] Result<ResourceVector> parse_resource_vector(const Json& value, const std::string_view path) {
  const auto object_status = validate_object_members(
    value, path,
    {"host_normal_bytes", "host_pinned_bytes", "host_hugepage_bytes", "shared_host_bytes", "spool_bytes",
     "transport_bytes", "descriptor_count", "async_token_count", "cpu_leaf_permits", "backend_gang_permits",
     "provider_private_permits", "io_slots", "devices"},
    {"host_normal_bytes", "host_pinned_bytes", "host_hugepage_bytes", "shared_host_bytes", "spool_bytes",
     "transport_bytes", "descriptor_count", "async_token_count", "cpu_leaf_permits", "backend_gang_permits",
     "provider_private_permits", "io_slots", "devices"});
  if (!object_status.ok()) {
    return object_status;
  }

  auto host_normal = require_quantity(value, "host_normal_bytes", path);
  auto host_pinned = require_quantity(value, "host_pinned_bytes", path);
  auto host_hugepage = require_quantity(value, "host_hugepage_bytes", path);
  auto shared_host = require_quantity(value, "shared_host_bytes", path);
  auto spool = require_quantity(value, "spool_bytes", path);
  auto transport = require_quantity(value, "transport_bytes", path);
  auto descriptors = require_quantity(value, "descriptor_count", path);
  auto asynchronous_tokens = require_quantity(value, "async_token_count", path);
  auto cpu = require_quantity(value, "cpu_leaf_permits", path);
  auto backend = require_quantity(value, "backend_gang_permits", path);
  auto provider = require_quantity(value, "provider_private_permits", path);
  auto io_slots = require_quantity(value, "io_slots", path);
  for (const auto* result : {&host_normal, &host_pinned, &host_hugepage, &shared_host, &spool, &transport, &descriptors,
                             &asynchronous_tokens, &cpu, &backend, &provider, &io_slots}) {
    if (!result->ok()) {
      return result->status();
    }
  }

  const auto& devices_value = member(value, "devices");
  if (!devices_value.is_array()) {
    return validation_error(child_path(path, "devices"), "must be an array");
  }
  std::vector<DeviceResourceSlotSpec> devices;
  devices.reserve(devices_value.size());
  for (std::size_t index = 0; index < devices_value.size(); ++index) {
    const auto element_path = child_path(path, "devices") + "[" + std::to_string(index) + "]";
    const auto device_status = validate_object_members(
      devices_value[index], element_path, {"device_id", "device_bytes", "gpu_stream_slots", "copy_engine_slots"},
      {"device_id", "device_bytes", "gpu_stream_slots", "copy_engine_slots"});
    if (!device_status.ok()) {
      return device_status;
    }
    auto device_id = require_bounded_string(devices_value[index], "device_id", element_path, 1U, 255U);
    auto device_bytes = require_quantity(devices_value[index], "device_bytes", element_path);
    auto streams = require_quantity(devices_value[index], "gpu_stream_slots", element_path);
    auto copy_engines = require_quantity(devices_value[index], "copy_engine_slots", element_path);
    if (!device_id.ok()) {
      return device_id.status();
    }
    if (!device_bytes.ok()) {
      return device_bytes.status();
    }
    if (!streams.ok()) {
      return streams.status();
    }
    if (!copy_engines.ok()) {
      return copy_engines.status();
    }
    devices.push_back({.device_id = std::move(device_id).value(),
                       .device_bytes = device_bytes.value(),
                       .gpu_stream_slots = streams.value(),
                       .copy_engine_slots = copy_engines.value()});
  }

  return ResourceVector::create({.host_normal_bytes = host_normal.value(),
                                 .host_pinned_bytes = host_pinned.value(),
                                 .host_hugepage_bytes = host_hugepage.value(),
                                 .shared_host_bytes = shared_host.value(),
                                 .spool_bytes = spool.value(),
                                 .transport_bytes = transport.value(),
                                 .descriptor_count = descriptors.value(),
                                 .async_token_count = asynchronous_tokens.value(),
                                 .cpu_leaf_permits = cpu.value(),
                                 .backend_gang_permits = backend.value(),
                                 .provider_private_permits = provider.value(),
                                 .io_slots = io_slots.value(),
                                 .devices = std::move(devices)},
                                path);
}

[[nodiscard]] Result<AdmissionOutcome> parse_admission_outcome(const std::string_view value,
                                                               const std::string_view path) {
  if (value == "admitted") {
    return AdmissionOutcome::admitted;
  }
  if (value == "rejected") {
    return AdmissionOutcome::rejected;
  }
  return validation_error(path, "must be 'admitted' or 'rejected'");
}

[[nodiscard]] std::string_view admission_outcome_to_string(const AdmissionOutcome value) noexcept {
  switch (value) {
    case AdmissionOutcome::admitted:
      return "admitted";
    case AdmissionOutcome::rejected:
      return "rejected";
  }
  return "invalid";
}

[[nodiscard]] Result<RunOutcome> parse_run_outcome(const std::string_view value, const std::string_view path) {
  if (value == "rejected") {
    return RunOutcome::rejected;
  }
  if (value == "cancelled_before_admission") {
    return RunOutcome::cancelled_before_admission;
  }
  if (value == "failed_pre_admission") {
    return RunOutcome::failed_pre_admission;
  }
  if (value == "cancelled") {
    return RunOutcome::cancelled;
  }
  if (value == "failed") {
    return RunOutcome::failed;
  }
  if (value == "completed") {
    return RunOutcome::completed;
  }
  return validation_error(path, "is not a v1 RunOutcome");
}

[[nodiscard]] Result<RecoveryClass> parse_recovery_class(const std::string_view value, const std::string_view path) {
  if (value == "fail_stop_no_resume") {
    return RecoveryClass::fail_stop_no_resume;
  }
  if (value == "source_replay_new_run") {
    return RecoveryClass::source_replay_new_run;
  }
  return validation_error(path, "is not a v1 RecoveryClass");
}

[[nodiscard]] Result<EgressVisibility> parse_egress_visibility(const std::string_view value,
                                                               const std::string_view path) {
  if (value == "none") {
    return EgressVisibility::none;
  }
  if (value == "partial") {
    return EgressVisibility::partial;
  }
  if (value == "flushed") {
    return EgressVisibility::flushed;
  }
  return validation_error(path, "is not a v1 EgressVisibility");
}

[[nodiscard]] Result<RunCauseKind> parse_run_cause_kind(const std::string_view value, const std::string_view path) {
  if (value == "cancellation") {
    return RunCauseKind::cancellation;
  }
  if (value == "rejection") {
    return RunCauseKind::rejection;
  }
  if (value == "failure") {
    return RunCauseKind::failure;
  }
  if (value == "invariant") {
    return RunCauseKind::invariant;
  }
  return validation_error(path, "is not a v1 RunCauseKind");
}

[[nodiscard]] Result<RunCauseSpec> parse_run_cause(const Json& value, const std::string_view path) {
  const auto object_status = validate_object_members(value, path, {"kind", "code"}, {"kind", "code"});
  if (!object_status.ok()) {
    return object_status;
  }
  auto kind_text = require_string(value, "kind", path);
  auto code = require_bounded_string(value, "code", path, 1U, kMaxRunCauseCodeLength);
  if (!kind_text.ok()) {
    return kind_text.status();
  }
  if (!code.ok()) {
    return code.status();
  }
  auto kind = parse_run_cause_kind(kind_text.value(), child_path(path, "kind"));
  if (!kind.ok()) {
    return kind.status();
  }
  return RunCauseSpec{.kind = kind.value(), .code = std::move(code).value()};
}

[[nodiscard]] Json run_cause_json(const RunCause& cause) {
  return {{"kind", to_string(cause.kind())}, {"code", cause.code()}};
}

} // namespace

Result<std::string> serialize_admission_record_canonical_json(const AdmissionRecord& record) {
  Json value{{"schema_version", kAdmissionRecordSchemaVersion},
             {"kind", "AdmissionRecord"},
             {"execution_plan_digest", record.execution_plan_digest().value()},
             {"verification_record_digest", record.verification_record_digest().value()},
             {"outcome", admission_outcome_to_string(record.outcome())},
             {"reservation", resource_vector_json(record.reservation())}};
  if (record.reason().has_value()) {
    value["reason"] = *record.reason();
  }
  return serialize_canonical_json(value);
}

Result<AdmissionRecord> parse_admission_record_json(const std::string_view document) {
  auto root = parse_bounded_json(document);
  if (!root.ok()) {
    return root.status();
  }
  const auto root_status = validate_object_members(
    root.value(), "$",
    {"schema_version", "kind", "execution_plan_digest", "verification_record_digest", "outcome", "reservation"},
    {"$schema", "schema_version", "kind", "execution_plan_digest", "verification_record_digest", "outcome",
     "reservation", "reason"});
  if (!root_status.ok()) {
    return root_status;
  }
  const auto schema_status = validate_schema_declaration(root.value(), "$");
  if (!schema_status.ok()) {
    return schema_status;
  }
  auto schema_version = require_string(root.value(), "schema_version", "$");
  auto kind = require_string(root.value(), "kind", "$");
  auto plan_digest = require_string(root.value(), "execution_plan_digest", "$");
  auto verification_digest = require_string(root.value(), "verification_record_digest", "$");
  auto outcome_text = require_string(root.value(), "outcome", "$");
  auto reason = optional_string(root.value(), "reason", "$");
  for (const auto* result : {&schema_version, &kind, &plan_digest, &verification_digest, &outcome_text}) {
    if (!result->ok()) {
      return result->status();
    }
  }
  if (!reason.ok()) {
    return reason.status();
  }
  if (schema_version.value() != kAdmissionRecordSchemaVersion) {
    return validation_error("$.schema_version", "must equal 'kspacejet.admission-record/v1'");
  }
  if (kind.value() != "AdmissionRecord") {
    return validation_error("$.kind", "must equal 'AdmissionRecord'");
  }
  auto outcome = parse_admission_outcome(outcome_text.value(), "$.outcome");
  if (!outcome.ok()) {
    return outcome.status();
  }
  auto reservation = parse_resource_vector(member(root.value(), "reservation"), "$.reservation");
  if (!reservation.ok()) {
    return reservation.status();
  }
  return AdmissionRecord::create({.schema_version = std::move(schema_version).value(),
                                  .execution_plan_digest = std::move(plan_digest).value(),
                                  .verification_record_digest = std::move(verification_digest).value(),
                                  .outcome = outcome.value(),
                                  .reservation = resource_vector_spec(reservation.value()),
                                  .reason = std::move(reason).value()});
}

Result<std::string> serialize_run_record_canonical_json(const RunRecord& record) {
  Json secondary_causes = Json::array();
  for (const auto& cause : record.secondary_causes()) {
    secondary_causes.push_back(run_cause_json(cause));
  }
  Json value{{"schema_version", kRunRecordSchemaVersion},
             {"kind", "RunRecord"},
             {"run_id", record.run_id()},
             {"execution_profile", to_string(record.execution_profile())},
             {"outcome", to_string(record.outcome())},
             {"recovery_class", to_string(record.recovery_class())},
             {"egress_visibility", to_string(record.egress_visibility())},
             {"secondary_causes", std::move(secondary_causes)}};
  if (record.execution_plan_digest().has_value()) {
    value["execution_plan_digest"] = record.execution_plan_digest()->value();
  }
  if (record.verification_record_digest().has_value()) {
    value["verification_record_digest"] = record.verification_record_digest()->value();
  }
  if (record.admission_record_digest().has_value()) {
    value["admission_record_digest"] = record.admission_record_digest()->value();
  }
  if (record.last_committed_ordinal().has_value()) {
    value["last_committed_ordinal"] = *record.last_committed_ordinal();
  }
  if (record.primary_cause().has_value()) {
    value["primary_cause"] = run_cause_json(*record.primary_cause());
  }
  if (record.replay_of_run_id().has_value()) {
    value["replay_of_run_id"] = *record.replay_of_run_id();
  }
  return serialize_canonical_json(value);
}

Result<RunRecord> parse_run_record_json(const std::string_view document) {
  auto root = parse_bounded_json(document);
  if (!root.ok()) {
    return root.status();
  }
  const auto root_status = validate_object_members(
    root.value(), "$",
    {"schema_version", "kind", "run_id", "execution_profile", "outcome", "recovery_class", "egress_visibility",
     "secondary_causes"},
    {"$schema", "schema_version", "kind", "run_id", "execution_profile", "execution_plan_digest",
     "verification_record_digest", "admission_record_digest", "outcome", "recovery_class", "egress_visibility",
     "last_committed_ordinal", "primary_cause", "secondary_causes", "replay_of_run_id"});
  if (!root_status.ok()) {
    return root_status;
  }
  const auto schema_status = validate_schema_declaration(root.value(), "$");
  if (!schema_status.ok()) {
    return schema_status;
  }

  auto schema_version = require_string(root.value(), "schema_version", "$");
  auto kind = require_string(root.value(), "kind", "$");
  auto run_id = require_bounded_string(root.value(), "run_id", "$", 1U, kMaxRunRecordIdentityLength);
  auto profile_text = require_string(root.value(), "execution_profile", "$");
  auto outcome_text = require_string(root.value(), "outcome", "$");
  auto recovery_text = require_string(root.value(), "recovery_class", "$");
  auto egress_text = require_string(root.value(), "egress_visibility", "$");
  auto plan_digest = optional_string(root.value(), "execution_plan_digest", "$");
  auto verification_digest = optional_string(root.value(), "verification_record_digest", "$");
  auto admission_digest = optional_string(root.value(), "admission_record_digest", "$");
  auto ordinal = optional_quantity(root.value(), "last_committed_ordinal", "$");
  auto replay_of_run_id = optional_string(root.value(), "replay_of_run_id", "$");
  for (const auto* result :
       {&schema_version, &kind, &run_id, &profile_text, &outcome_text, &recovery_text, &egress_text}) {
    if (!result->ok()) {
      return result->status();
    }
  }
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }
  if (!verification_digest.ok()) {
    return verification_digest.status();
  }
  if (!admission_digest.ok()) {
    return admission_digest.status();
  }
  if (!ordinal.ok()) {
    return ordinal.status();
  }
  if (!replay_of_run_id.ok()) {
    return replay_of_run_id.status();
  }
  if (schema_version.value() != kRunRecordSchemaVersion) {
    return validation_error("$.schema_version", "must equal 'kspacejet.run-record/v1'");
  }
  if (kind.value() != "RunRecord") {
    return validation_error("$.kind", "must equal 'RunRecord'");
  }
  auto profile = parse_execution_profile(profile_text.value());
  if (!profile.ok()) {
    return validation_error("$.execution_profile", "is not a v1 ExecutionProfile");
  }
  auto outcome = parse_run_outcome(outcome_text.value(), "$.outcome");
  auto recovery = parse_recovery_class(recovery_text.value(), "$.recovery_class");
  auto egress = parse_egress_visibility(egress_text.value(), "$.egress_visibility");
  if (!outcome.ok()) {
    return outcome.status();
  }
  if (!recovery.ok()) {
    return recovery.status();
  }
  if (!egress.ok()) {
    return egress.status();
  }

  const auto& secondary_json = member(root.value(), "secondary_causes");
  if (!secondary_json.is_array()) {
    return validation_error("$.secondary_causes", "must be an array");
  }
  if (secondary_json.size() > kMaxRunSecondaryCauses) {
    return validation_error("$.secondary_causes", "exceeds the v1 maximum cause count");
  }
  std::vector<RunCauseSpec> secondary_causes;
  secondary_causes.reserve(secondary_json.size());
  for (std::size_t index = 0; index < secondary_json.size(); ++index) {
    auto cause = parse_run_cause(secondary_json[index], "$.secondary_causes[" + std::to_string(index) + "]");
    if (!cause.ok()) {
      return cause.status();
    }
    secondary_causes.push_back(std::move(cause).value());
  }

  std::optional<RunCauseSpec> primary_cause;
  if (root.value().contains("primary_cause")) {
    auto parsed = parse_run_cause(member(root.value(), "primary_cause"), "$.primary_cause");
    if (!parsed.ok()) {
      return parsed.status();
    }
    primary_cause = std::move(parsed).value();
  }

  return RunRecord::create({.schema_version = std::move(schema_version).value(),
                            .run_id = std::move(run_id).value(),
                            .execution_profile = profile.value(),
                            .execution_plan_digest = std::move(plan_digest).value(),
                            .verification_record_digest = std::move(verification_digest).value(),
                            .admission_record_digest = std::move(admission_digest).value(),
                            .outcome = outcome.value(),
                            .recovery_class = recovery.value(),
                            .egress_visibility = egress.value(),
                            .last_committed_ordinal = std::move(ordinal).value(),
                            .primary_cause = std::move(primary_cause),
                            .secondary_causes = std::move(secondary_causes),
                            .replay_of_run_id = std::move(replay_of_run_id).value()});
}

} // namespace ksj::recon
