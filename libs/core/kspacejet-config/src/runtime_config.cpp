#include "kspacejet/config/runtime_config.hpp"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ksj::config {
namespace {

template <typename T>
[[nodiscard]] bool assign_or_status(ksj::base::Result<T>&& result, T* out, ksj::base::Status* status) {
  if (!result.ok()) {
    *status = result.status();
    return false;
  }
  *out = std::move(result).value();
  return true;
}

[[nodiscard]] std::string string_value(const KeyValueConfig& config, std::string_view key,
                                       std::string_view default_value) {
  return config.value_or(key, default_value);
}

[[nodiscard]] std::string field_error(std::string_view key, std::string_view message) {
  std::string error("Invalid value for key '");
  error.append(key);
  error.append("': ");
  error.append(message);
  return error;
}

[[nodiscard]] bool is_space(unsigned char ch) noexcept {
  return std::isspace(ch) != 0;
}

[[nodiscard]] std::string_view trim_view(std::string_view text) {
  while (!text.empty() && is_space(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
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

[[nodiscard]] ksj::base::Result<int> int_value(const KeyValueConfig& config, std::string_view key, int default_value) {
  const auto value = config.find(key);
  if (!value.has_value()) {
    return default_value;
  }

  int parsed = 0;
  const char* begin = value->data();
  const char* end = value->data() + value->size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return ksj::base::Status::ParseError(field_error(key, "expected an integer"));
  }
  return parsed;
}

[[nodiscard]] ksj::base::Result<std::size_t> size_with_unit_value(std::string_view key, std::string_view value) {
  value = trim_view(value);
  if (value.empty() || value.front() == '-') {
    return ksj::base::Status::ParseError(field_error(key, "expected a non-negative size"));
  }

  std::size_t digit_count = 0;
  while (digit_count < value.size() && std::isdigit(static_cast<unsigned char>(value[digit_count])) != 0) {
    ++digit_count;
  }
  if (digit_count == 0) {
    return ksj::base::Status::ParseError(field_error(key, "expected a non-negative size"));
  }

  std::size_t parsed = 0;
  const char* begin = value.data();
  const char* end = value.data() + digit_count;
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return ksj::base::Status::ParseError(field_error(key, "size is out of range"));
  }

  std::string_view suffix = trim_view(value.substr(digit_count));
  std::size_t multiplier = 1;
  if (suffix.empty()) {
    multiplier = 1;
  } else if (ascii_iequals(suffix, "k") || ascii_iequals(suffix, "kb") || ascii_iequals(suffix, "kib")) {
    multiplier = 1024ULL;
  } else if (ascii_iequals(suffix, "m") || ascii_iequals(suffix, "mb") || ascii_iequals(suffix, "mib")) {
    multiplier = 1024ULL * 1024ULL;
  } else if (ascii_iequals(suffix, "g") || ascii_iequals(suffix, "gb") || ascii_iequals(suffix, "gib")) {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
  } else {
    return ksj::base::Status::ParseError(field_error(key, "expected a byte size or K/M/G suffix"));
  }

  if (parsed > std::numeric_limits<std::size_t>::max() / multiplier) {
    return ksj::base::Status::ParseError(field_error(key, "size is out of range"));
  }
  return parsed * multiplier;
}

[[nodiscard]] ksj::base::Result<std::vector<std::size_t>> size_list_value(const KeyValueConfig& config,
                                                                          std::string_view key) {
  const auto value = config.find(key);
  if (!value.has_value()) {
    return std::vector<std::size_t>{};
  }

  std::string_view remaining = trim_view(*value);
  if (remaining.empty()) {
    return std::vector<std::size_t>{};
  }

  std::vector<std::size_t> sizes;
  while (true) {
    const std::size_t separator = remaining.find(',');
    const std::string_view token = separator == std::string_view::npos ? remaining : remaining.substr(0, separator);
    auto parsed = size_with_unit_value(key, token);
    if (!parsed.ok()) {
      return parsed.status();
    }
    const std::size_t bytes = parsed.value();
    if (bytes == 0) {
      return ksj::base::Status::ParseError(field_error(key, "size classes must be greater than zero"));
    }
    if (!sizes.empty() && bytes <= sizes.back()) {
      return ksj::base::Status::ParseError(field_error(key, "size classes must be strictly increasing"));
    }
    sizes.push_back(bytes);
    if (separator == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(separator + 1);
    if (trim_view(remaining).empty()) {
      return ksj::base::Status::ParseError(field_error(key, "empty size class entry"));
    }
  }
  return sizes;
}

[[nodiscard]] ksj::base::Result<std::size_t> positive_size_value(std::string_view key, std::string_view value) {
  value = trim_view(value);
  if (value.empty() || value.front() == '-') {
    return ksj::base::Status::ParseError(field_error(key, "expected a positive integer"));
  }

  std::size_t parsed = 0;
  const char* begin = value.data();
  const char* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return ksj::base::Status::ParseError(field_error(key, "expected a positive integer"));
  }
  if (parsed == 0) {
    return ksj::base::Status::ParseError(field_error(key, "entries must be greater than zero"));
  }
  return parsed;
}

[[nodiscard]] ksj::base::Result<std::vector<std::size_t>> positive_size_list_value(const KeyValueConfig& config,
                                                                                   std::string_view key) {
  const auto value = config.find(key);
  if (!value.has_value()) {
    return std::vector<std::size_t>{};
  }

  std::string_view remaining = trim_view(*value);
  if (remaining.empty()) {
    return std::vector<std::size_t>{};
  }

  std::vector<std::size_t> values;
  while (true) {
    const std::size_t separator = remaining.find(',');
    const std::string_view token = separator == std::string_view::npos ? remaining : remaining.substr(0, separator);
    auto parsed = positive_size_value(key, token);
    if (!parsed.ok()) {
      return parsed.status();
    }
    values.push_back(parsed.value());
    if (separator == std::string_view::npos) {
      break;
    }
    remaining = remaining.substr(separator + 1);
    if (trim_view(remaining).empty()) {
      return ksj::base::Status::ParseError(field_error(key, "empty entry"));
    }
  }
  return values;
}

[[nodiscard]] ksj::base::Status validate_memory_pool_config(const RuntimeConfig& runtime_config) {
  const auto& pool = runtime_config.memory.pool;
  if (!pool.enabled) {
    return {};
  }

  const bool has_size_classes = !pool.size_classes.empty();
  const bool has_block_counts = !pool.size_class_block_counts.empty();
  if (!has_size_classes && !has_block_counts) {
    return {};
  }

  if (!has_size_classes) {
    return ksj::base::Status::ParseError(
      field_error("memory.pool.size_classes", "required when memory.pool.size_class_block_counts is configured"));
  }
  if (!has_block_counts) {
    return ksj::base::Status::ParseError(
      field_error("memory.pool.size_class_block_counts", "required when memory.pool.size_classes is configured"));
  }
  if (pool.size_class_block_counts.size() != pool.size_classes.size()) {
    return ksj::base::Status::ParseError(
      field_error("memory.pool.size_class_block_counts", "entry count must match memory.pool.size_classes"));
  }
  return {};
}

} // namespace

ksj::base::Result<RuntimeConfig> runtime_config_from_key_value_config(const KeyValueConfig& config) {
  RuntimeConfig runtime_config;
  ksj::base::Status status;

  runtime_config.runtime_output_root_dir = string_value(config, "runtime_output_root_dir", "");
  runtime_config.provider_config_file = string_value(config, "provider_config_file", "");
  runtime_config.output.results_dir = string_value(config, "output.results_dir", runtime_config.output.results_dir);
  runtime_config.output.log_dir = string_value(config, "output.log_dir", runtime_config.output.log_dir);

  if (!assign_or_status(config.bool_value("crash.enabled", runtime_config.crash.enabled), &runtime_config.crash.enabled,
                        &status) ||
      !assign_or_status(config.bool_value("crash.capture_terminate", runtime_config.crash.capture_terminate),
                        &runtime_config.crash.capture_terminate, &status) ||
      !assign_or_status(config.bool_value("crash.use_altstack", runtime_config.crash.use_altstack),
                        &runtime_config.crash.use_altstack, &status) ||
      !assign_or_status(config.bool_value("crash.print_readable_stack", runtime_config.crash.print_readable_stack),
                        &runtime_config.crash.print_readable_stack, &status) ||
      !assign_or_status(
        config.bool_value("crash.launch_debugger_from_env", runtime_config.crash.launch_debugger_from_env),
        &runtime_config.crash.launch_debugger_from_env, &status) ||
      !assign_or_status(config.size_value("crash.max_frames", runtime_config.crash.max_frames),
                        &runtime_config.crash.max_frames, &status)) {
    return status;
  }
  runtime_config.crash.debugger_env_var =
    string_value(config, "crash.debugger_env_var", runtime_config.crash.debugger_env_var);

  if (!assign_or_status(config.bool_value("memory.pool.enabled", runtime_config.memory.pool.enabled),
                        &runtime_config.memory.pool.enabled, &status) ||
      !assign_or_status(size_list_value(config, "memory.pool.size_classes"), &runtime_config.memory.pool.size_classes,
                        &status) ||
      !assign_or_status(positive_size_list_value(config, "memory.pool.size_class_block_counts"),
                        &runtime_config.memory.pool.size_class_block_counts, &status)) {
    return status;
  }
  status = validate_memory_pool_config(runtime_config);
  if (!status.ok()) {
    return status;
  }

  runtime_config.debug.root_dir = string_value(config, "debug.root_dir", "");
  runtime_config.debug.report_dir = string_value(config, "debug.report_dir", "");
  runtime_config.debug.algorithm_dir = string_value(config, "debug.algorithm_dir", "");
  runtime_config.debug.slice_dump_dir = string_value(config, "debug.slice_dump_dir", "");
  runtime_config.debug.matrix_dump_dir = string_value(config, "debug.matrix_dump_dir", "");
  runtime_config.debug.categories = string_value(config, "debug.categories", "");
  if (!assign_or_status(config.bool_value("debug.enabled", runtime_config.debug.enabled), &runtime_config.debug.enabled,
                        &status) ||
      !assign_or_status(int_value(config, "debug.dump_slice_index", runtime_config.debug.dump_slice_index),
                        &runtime_config.debug.dump_slice_index, &status)) {
    return status;
  }

  runtime_config.logging.logger_name = string_value(config, "logging.logger_name", "");
  runtime_config.logging.flush_level = string_value(config, "logging.flush_level", runtime_config.logging.flush_level);
  runtime_config.logging.output_format =
    string_value(config, "logging.output_format", runtime_config.logging.output_format);
  if (!assign_or_status(config.bool_value("logging.async", runtime_config.logging.async), &runtime_config.logging.async,
                        &status) ||
      !assign_or_status(config.size_value("logging.periodic_flush_ms", runtime_config.logging.periodic_flush_ms),
                        &runtime_config.logging.periodic_flush_ms, &status) ||
      !assign_or_status(config.size_value("logging.queue_size", runtime_config.logging.queue_size),
                        &runtime_config.logging.queue_size, &status) ||
      !assign_or_status(config.size_value("logging.async_worker_count", runtime_config.logging.async_worker_count),
                        &runtime_config.logging.async_worker_count, &status) ||
      !assign_or_status(config.bool_value("logging.console.enabled", runtime_config.logging.console.enabled),
                        &runtime_config.logging.console.enabled, &status) ||
      !assign_or_status(
        config.bool_value("logging.console.console_color", runtime_config.logging.console.console_color),
        &runtime_config.logging.console.console_color, &status) ||
      !assign_or_status(config.bool_value("logging.file.enabled", runtime_config.logging.file.enabled),
                        &runtime_config.logging.file.enabled, &status) ||
      !assign_or_status(config.uint32_value("logging.file.retention_days", runtime_config.logging.file.retention_days),
                        &runtime_config.logging.file.retention_days, &status)) {
    return status;
  }
  runtime_config.logging.console.level =
    string_value(config, "logging.console.level", runtime_config.logging.console.level);
  runtime_config.logging.console.pattern =
    string_value(config, "logging.console.pattern", runtime_config.logging.console.pattern);
  runtime_config.logging.file.level = string_value(config, "logging.file.level", runtime_config.logging.file.level);
  runtime_config.logging.file.path = string_value(config, "logging.file.path", runtime_config.logging.file.path);
  runtime_config.logging.file.pattern =
    string_value(config, "logging.file.pattern", runtime_config.logging.file.pattern);

  return runtime_config;
}

ksj::base::Result<RuntimeConfig> parse_runtime_config(std::string_view text, std::string_view source_name) {
  auto parsed = parse_key_value_config(text, source_name);
  if (!parsed.ok()) {
    return parsed.status();
  }
  return runtime_config_from_key_value_config(parsed.value());
}

ksj::base::Result<RuntimeConfig> load_runtime_config_file(const std::string& path) {
  auto loaded = load_key_value_config_file(path);
  if (!loaded.ok()) {
    return loaded.status();
  }

  KeyValueConfig merged_config = std::move(loaded).value();
  const std::string provider_config_file = merged_config.value_or("provider_config_file", "");
  if (!provider_config_file.empty()) {
    const std::filesystem::path runtime_config_path(path);
    const std::filesystem::path provider_config_path = std::filesystem::path(provider_config_file).is_absolute()
                                                         ? std::filesystem::path(provider_config_file)
                                                         : runtime_config_path.parent_path() / provider_config_file;
    auto provider_config = load_key_value_config_file(provider_config_path.string());
    if (!provider_config.ok()) {
      return provider_config.status();
    }
    merged_config.merge_from(provider_config.value());
    merged_config.set("provider_config_file", provider_config_file);
  }

  return runtime_config_from_key_value_config(merged_config);
}

} // namespace ksj::config
