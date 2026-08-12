#include "kspacejet/config/parameter_document.hpp"

#include "kspacejet/base/status.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace ksj::config {
namespace {

[[nodiscard]] bool ascii_iequals(std::string_view lhs, std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_space(const char ch) noexcept {
  return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

[[nodiscard]] std::string trim(std::string_view text) {
  while (!text.empty() && is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(text.back())) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

void skip_spaces(std::string_view text, std::size_t& index) noexcept {
  while (index < text.size() && is_space(text[index])) {
    ++index;
  }
}

[[nodiscard]] std::string parse_location(std::string_view source_name, const std::size_t offset) {
  std::string location;
  if (!source_name.empty()) {
    location.append(source_name);
    location.push_back(':');
  }
  location.append("offset ");
  location.append(std::to_string(offset));
  return location;
}

[[nodiscard]] ksj::base::Status parse_error(std::string_view source_name, const std::size_t offset,
                                            std::string_view message) {
  std::string text = parse_location(source_name, offset);
  text.append(": ");
  text.append(message);
  return ksj::base::Status::ParseError(std::move(text));
}

[[nodiscard]] ksj::base::Result<std::string> parse_key(std::string_view text, std::size_t& index,
                                                       std::string_view source_name) {
  skip_spaces(text, index);
  const std::size_t begin = index;
  while (index < text.size()) {
    const char ch = text[index];
    if (ch == '=' || ch == ',' || ch == ')' || is_space(ch)) {
      break;
    }
    ++index;
  }
  std::string key = trim(text.substr(begin, index - begin));
  if (key.empty()) {
    return parse_error(source_name, begin, "expected field key");
  }
  return key;
}

[[nodiscard]] ksj::base::Result<std::string> parse_quoted_value(std::string_view text, std::size_t& index,
                                                                std::string_view source_name) {
  const char quote = text[index++];
  std::string value;
  while (index < text.size()) {
    const char ch = text[index++];
    if (ch == quote) {
      return value;
    }
    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }
    if (index >= text.size()) {
      return parse_error(source_name, index, "unterminated escape in quoted value");
    }
    const char escaped = text[index++];
    switch (escaped) {
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        value.push_back(escaped);
        break;
    }
  }
  return parse_error(source_name, index, "unterminated quoted value");
}

[[nodiscard]] ksj::base::Result<std::string> parse_value(std::string_view text, std::size_t& index,
                                                         std::string_view source_name) {
  skip_spaces(text, index);
  if (index >= text.size()) {
    return parse_error(source_name, index, "expected field value");
  }
  if (text[index] == '"' || text[index] == '\'') {
    return parse_quoted_value(text, index, source_name);
  }

  const std::size_t begin = index;
  while (index < text.size() && text[index] != ',' && text[index] != ')') {
    ++index;
  }
  return trim(text.substr(begin, index - begin));
}

[[nodiscard]] ksj::base::Result<ParameterRecord> parse_record(std::string_view text, std::size_t& index,
                                                              std::string_view source_name) {
  ParameterRecord record;
  ++index;

  while (index < text.size()) {
    skip_spaces(text, index);
    if (index < text.size() && text[index] == ')') {
      ++index;
      return record;
    }

    auto key_result = parse_key(text, index, source_name);
    if (!key_result.ok()) {
      return key_result.status();
    }
    skip_spaces(text, index);
    if (index >= text.size() || text[index] != '=') {
      return parse_error(source_name, index, "expected '=' after field key");
    }
    ++index;

    auto value_result = parse_value(text, index, source_name);
    if (!value_result.ok()) {
      return value_result.status();
    }
    record.add_field(std::move(key_result).value(), std::move(value_result).value());

    skip_spaces(text, index);
    if (index < text.size() && text[index] == ',') {
      ++index;
      continue;
    }
    if (index < text.size() && text[index] == ')') {
      ++index;
      return record;
    }
    if (index >= text.size()) {
      break;
    }
    return parse_error(source_name, index, "expected ',' or ')' after field value");
  }

  return parse_error(source_name, index, "unterminated parameter record");
}

[[nodiscard]] std::string quote(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 2);
  out.push_back('"');
  for (const char ch : value) {
    switch (ch) {
      case '\\':
      case '"':
        out.push_back('\\');
        out.push_back(ch);
        break;
      case '\n':
        out.append("\\n");
        break;
      case '\r':
        out.append("\\r");
        break;
      case '\t':
        out.append("\\t");
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  out.push_back('"');
  return out;
}

template <typename T> [[nodiscard]] std::optional<T> parse_number(std::string_view text) {
  T value{};
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

} // namespace

const std::vector<ParameterField>& ParameterRecord::fields() const noexcept {
  return fields_;
}

bool ParameterRecord::empty() const noexcept {
  return fields_.empty();
}

void ParameterRecord::add_field(std::string key, std::string value) {
  fields_.push_back({std::move(key), std::move(value)});
}

bool ParameterRecord::set_field(std::string_view key, std::string value, const std::size_t index) {
  std::size_t match_index = 0;
  for (auto& field : fields_) {
    if (field.key != key) {
      continue;
    }
    if (match_index++ == index) {
      field.value = std::move(value);
      return true;
    }
  }
  if (index == 0) {
    add_field(std::string(key), std::move(value));
    return true;
  }
  return false;
}

std::optional<std::string_view> ParameterRecord::field(std::string_view key, const std::size_t index) const {
  std::size_t match_index = 0;
  for (const auto& field : fields_) {
    if (field.key != key) {
      continue;
    }
    if (match_index++ == index) {
      return std::string_view(field.value);
    }
  }
  return std::nullopt;
}

std::optional<bool> ParameterRecord::bool_value(std::string_view key, const std::size_t index) const {
  const auto value = field(key, index);
  if (!value.has_value()) {
    return std::nullopt;
  }
  if (ascii_iequals(*value, "T") || ascii_iequals(*value, "TRUE") || ascii_iequals(*value, "YES") || *value == "1") {
    return true;
  }
  if (ascii_iequals(*value, "F") || ascii_iequals(*value, "FALSE") || ascii_iequals(*value, "NO") || *value == "0") {
    return false;
  }
  return std::nullopt;
}

std::optional<int> ParameterRecord::int_value(std::string_view key, const std::size_t index) const {
  const auto value = field(key, index);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_number<int>(*value);
}

std::optional<double> ParameterRecord::double_value(std::string_view key, const std::size_t index) const {
  const auto value = field(key, index);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_number<double>(*value);
}

ksj::base::Result<ParameterDocument> ParameterDocument::parse(std::string_view text, std::string_view source_name) {
  ParameterDocument document;
  std::size_t index = 0;
  while (index < text.size()) {
    skip_spaces(text, index);
    if (index >= text.size()) {
      break;
    }
    if (text[index] != '(') {
      ++index;
      continue;
    }
    auto record_result = parse_record(text, index, source_name);
    if (!record_result.ok()) {
      return record_result.status();
    }
    document.add_record(std::move(record_result).value());
  }
  return document;
}

ksj::base::Result<ParameterDocument> ParameterDocument::read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return ksj::base::Status::IoError("failed to open parameter file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return parse(buffer.str(), path.string());
}

const std::vector<ParameterRecord>& ParameterDocument::records() const noexcept {
  return records_;
}

bool ParameterDocument::empty() const noexcept {
  return records_.empty();
}

void ParameterDocument::add_record(ParameterRecord record) {
  records_.push_back(std::move(record));
}

ParameterRecord* ParameterDocument::find(std::string_view field, std::string_view value) noexcept {
  for (auto& record : records_) {
    const auto candidate = record.field(field);
    if (candidate.has_value() && *candidate == value) {
      return &record;
    }
  }
  return nullptr;
}

const ParameterRecord* ParameterDocument::find(std::string_view field, std::string_view value) const noexcept {
  for (const auto& record : records_) {
    const auto candidate = record.field(field);
    if (candidate.has_value() && *candidate == value) {
      return &record;
    }
  }
  return nullptr;
}

ParameterRecord* ParameterDocument::find_by_name(std::string_view name) noexcept {
  return find("NAME", name);
}

const ParameterRecord* ParameterDocument::find_by_name(std::string_view name) const noexcept {
  return find("NAME", name);
}

std::optional<std::string_view> ParameterDocument::string_value(std::string_view name, std::string_view field,
                                                                const std::size_t index) const {
  const auto* record = find_by_name(name);
  return record == nullptr ? std::nullopt : record->field(field, index);
}

std::optional<bool> ParameterDocument::bool_value(std::string_view name, std::string_view field,
                                                  const std::size_t index) const {
  const auto* record = find_by_name(name);
  return record == nullptr ? std::nullopt : record->bool_value(field, index);
}

std::optional<int> ParameterDocument::int_value(std::string_view name, std::string_view field,
                                                const std::size_t index) const {
  const auto* record = find_by_name(name);
  return record == nullptr ? std::nullopt : record->int_value(field, index);
}

std::optional<double> ParameterDocument::double_value(std::string_view name, std::string_view field,
                                                      const std::size_t index) const {
  const auto* record = find_by_name(name);
  return record == nullptr ? std::nullopt : record->double_value(field, index);
}

bool ParameterDocument::set_value(std::string_view name, std::string value, std::string_view field) {
  auto* record = find_by_name(name);
  if (record == nullptr) {
    ParameterRecord new_record;
    new_record.add_field("NAME", std::string(name));
    new_record.add_field(std::string(field), std::move(value));
    add_record(std::move(new_record));
    return true;
  }
  return record->set_field(field, std::move(value));
}

bool ParameterDocument::set_int(std::string_view name, const int value, std::string_view field) {
  return set_value(name, std::to_string(value), field);
}

bool ParameterDocument::set_double(std::string_view name, const double value, std::string_view field) {
  return set_value(name, std::to_string(value), field);
}

bool ParameterDocument::set_bool(std::string_view name, const bool value, std::string_view field) {
  return set_value(name, value ? "T" : "F", field);
}

std::string ParameterDocument::encode() const {
  std::string out;
  for (const auto& record : records_) {
    out.append("( ");
    for (std::size_t index = 0; index < record.fields().size(); ++index) {
      const auto& field = record.fields()[index];
      if (index != 0) {
        out.append(", ");
      }
      out.append(field.key);
      out.append(" = ");
      out.append(quote(field.value));
    }
    out.append(" )\n");
  }
  return out;
}

ksj::base::Status ParameterDocument::write_file(const std::filesystem::path& path) const {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return ksj::base::Status::IoError("failed to open parameter file for write: " + path.string());
  }
  file << encode();
  if (!file) {
    return ksj::base::Status::IoError("failed to write parameter file: " + path.string());
  }
  return ksj::base::Status::Ok();
}

} // namespace ksj::config
