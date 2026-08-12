#include "kspacejet/base/types.hpp"
#include "kspacejet/config/key_value_config.hpp"

#include <charconv>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace ksj::config {
namespace {

[[nodiscard]] bool is_space(unsigned char ch) noexcept {
  return std::isspace(ch) != 0;
}

[[nodiscard]] std::string trim(std::string_view text) {
  while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

[[nodiscard]] std::string field_error(std::string_view key, std::string_view message) {
  std::string error("Invalid value for key '");
  error.append(key);
  error.append("': ");
  error.append(message);
  return error;
}

[[nodiscard]] std::string parse_location(std::string_view source_name, std::size_t line_number) {
  std::ostringstream os;
  if (!source_name.empty()) {
    os << source_name << ':';
  }
  os << line_number;
  return os.str();
}

[[nodiscard]] std::string strip_inline_comment(std::string_view line) {
  char quote = '\0';
  bool escaped = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char ch = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (quote != '\0') {
      if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        quote = '\0';
      }
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quote = ch;
      continue;
    }
    if (ch == '#') {
      return std::string(line.substr(0, index));
    }
  }
  return std::string(line);
}

[[nodiscard]] bool is_valid_key(std::string_view key) {
  if (key.empty()) {
    return false;
  }
  for (const unsigned char ch : key) {
    if (std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.') {
      continue;
    }
    return false;
  }
  return true;
}

[[nodiscard]] ksj::base::Result<std::string> unquote_value(std::string_view raw_value, std::string_view source_name,
                                                           std::size_t line_number) {
  const std::string value = trim(raw_value);
  if (value.empty()) {
    return std::string();
  }

  const char quote = value.front();
  if (quote != '"' && quote != '\'') {
    return value;
  }
  if (value.size() < 2 || value.back() != quote) {
    return ksj::base::Status::ParseError(parse_location(source_name, line_number) +
                                         ": quoted value is missing its closing quote");
  }

  std::string unquoted;
  unquoted.reserve(value.size() - 2);
  bool escaped = false;
  for (std::size_t index = 1; index + 1 < value.size(); ++index) {
    const char ch = value[index];
    if (!escaped) {
      if (ch == '\\') {
        escaped = true;
      } else {
        unquoted.push_back(ch);
      }
      continue;
    }

    switch (ch) {
      case 'n':
        unquoted.push_back('\n');
        break;
      case 'r':
        unquoted.push_back('\r');
        break;
      case 't':
        unquoted.push_back('\t');
        break;
      case '\\':
      case '\'':
      case '"':
        unquoted.push_back(ch);
        break;
      default:
        unquoted.push_back(ch);
        break;
    }
    escaped = false;
  }
  if (escaped) {
    return ksj::base::Status::ParseError(parse_location(source_name, line_number) +
                                         ": quoted value ends with an incomplete escape");
  }
  return unquoted;
}

[[nodiscard]] bool ascii_iequals(std::string_view lhs, std::string_view rhs) {
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

[[nodiscard]] ksj::base::Result<bool> parse_bool_value(std::string_view key, std::string_view value) {
  if (ascii_iequals(value, "true") || ascii_iequals(value, "yes") || ascii_iequals(value, "on") || value == "1") {
    return true;
  }
  if (ascii_iequals(value, "false") || ascii_iequals(value, "no") || ascii_iequals(value, "off") || value == "0") {
    return false;
  }
  return ksj::base::Status::ParseError(field_error(key, "expected true/false, yes/no, on/off, or 1/0"));
}

template <typename T>
[[nodiscard]] ksj::base::Result<T> parse_unsigned_value(std::string_view key, std::string_view value, T max_value) {
  if (value.empty() || value.front() == '-') {
    return ksj::base::Status::ParseError(field_error(key, "expected a non-negative integer"));
  }

  unsigned long long parsed = 0;
  const char* begin = value.data();
  const char* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return ksj::base::Status::ParseError(field_error(key, "expected a non-negative integer"));
  }
  if (parsed > static_cast<unsigned long long>(max_value)) {
    return ksj::base::Status::ParseError(field_error(key, "integer is out of range"));
  }
  return static_cast<T>(parsed);
}

[[nodiscard]] ksj::base::Result<double> parse_double_value(std::string_view key, std::string_view value) {
  if (value.empty()) {
    return ksj::base::Status::ParseError(field_error(key, "expected a number"));
  }

  std::string owned_value(value);
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(owned_value.c_str(), &end);
  if (errno != 0 || end == owned_value.c_str() || *end != '\0') {
    return ksj::base::Status::ParseError(field_error(key, "expected a number"));
  }
  return parsed;
}

} // namespace

void KeyValueConfig::set(std::string key, std::string value) {
  values_[std::move(key)] = std::move(value);
}

void KeyValueConfig::merge_from(const KeyValueConfig& other) {
  for (const auto& [key, value] : other.values()) {
    set(key, value);
  }
}

const KeyValueConfig::Map& KeyValueConfig::values() const noexcept {
  return values_;
}

bool KeyValueConfig::empty() const noexcept {
  return values_.empty();
}

bool KeyValueConfig::contains(std::string_view key) const {
  return values_.find(key) != values_.end();
}

std::optional<std::string_view> KeyValueConfig::find(std::string_view key) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return std::nullopt;
  }
  return std::string_view(it->second);
}

std::string KeyValueConfig::value_or(std::string_view key, std::string_view default_value) const {
  const auto value = find(key);
  return value.has_value() ? std::string(*value) : std::string(default_value);
}

ksj::base::Result<bool> KeyValueConfig::bool_value(std::string_view key, bool default_value) const {
  const auto value = find(key);
  if (!value.has_value()) {
    return default_value;
  }
  return parse_bool_value(key, *value);
}

ksj::base::Result<ksj::base::u32> KeyValueConfig::uint32_value(std::string_view key,
                                                               ksj::base::u32 default_value) const {
  const auto value = find(key);
  if (!value.has_value()) {
    return default_value;
  }
  return parse_unsigned_value<ksj::base::u32>(key, *value, std::numeric_limits<ksj::base::u32>::max());
}

ksj::base::Result<std::size_t> KeyValueConfig::size_value(std::string_view key, std::size_t default_value) const {
  const auto value = find(key);
  if (!value.has_value()) {
    return default_value;
  }
  return parse_unsigned_value<std::size_t>(key, *value, std::numeric_limits<std::size_t>::max());
}

ksj::base::Result<double> KeyValueConfig::double_value(std::string_view key, double default_value) const {
  const auto value = find(key);
  if (!value.has_value()) {
    return default_value;
  }
  return parse_double_value(key, *value);
}

ksj::base::Result<KeyValueConfig> parse_key_value_config(std::string_view text, std::string_view source_name) {
  KeyValueConfig config;
  std::istringstream input{std::string(text)};
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    const std::string without_comment = strip_inline_comment(line);
    const std::string trimmed_line = trim(without_comment);
    if (trimmed_line.empty()) {
      continue;
    }

    const std::size_t separator = trimmed_line.find('=');
    if (separator == std::string::npos) {
      return ksj::base::Status::ParseError(parse_location(source_name, line_number) + ": expected key=value");
    }

    const std::string key = trim(std::string_view(trimmed_line).substr(0, separator));
    if (!is_valid_key(key)) {
      return ksj::base::Status::ParseError(parse_location(source_name, line_number) + ": invalid key '" + key + "'");
    }

    auto parsed_value = unquote_value(std::string_view(trimmed_line).substr(separator + 1), source_name, line_number);
    if (!parsed_value.ok()) {
      return parsed_value.status();
    }
    config.set(key, std::move(parsed_value).value());
  }
  return config;
}

ksj::base::Result<KeyValueConfig> load_key_value_config_file(const std::string& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return ksj::base::Status::NotFound("failed to open config file '" + path + "'");
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return ksj::base::Status::IoError("failed to read config file '" + path + "'");
  }

  return parse_key_value_config(buffer.str(), path);
}

} // namespace ksj::config
