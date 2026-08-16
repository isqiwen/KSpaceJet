#include "kspacejet/logging/logging.hpp"

#include "kspacejet/base/path.hpp"
#include "kspacejet/config/runtime_config.hpp"

#include <spdlog/async_logger.h>
#include <spdlog/details/fmt_helper.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ksj::logging {
namespace {

namespace fs = std::filesystem;

struct LoggerState {
  std::mutex mutex;
  std::vector<spdlog::sink_ptr> sinks;
  std::shared_ptr<spdlog::details::thread_pool> thread_pool;
  std::shared_ptr<spdlog::logger> logger;
  ksj::config::LoggingConfig config;
  std::string logger_name;
  std::string base_dir;
  std::string default_logger_name;
  std::string default_file_path;
  fs::path resolved_file_path;
  fs::path active_file_path;
  std::atomic_size_t periodic_flush_ms{0};
  std::atomic<std::int64_t> next_periodic_flush_ns{0};
  std::chrono::steady_clock::time_point last_file_check{};
  std::atomic<Level> effective_level{Level::Off};
  bool configured = false;
  bool is_default_console_fallback = false;
};

enum class ConfigurationKind {
  explicit_configuration,
  default_console_fallback,
};

class UppercaseLevelFormatter final : public spdlog::custom_flag_formatter {
public:
  void format(const spdlog::details::log_msg& message, const std::tm&, spdlog::memory_buf_t& destination) override {
    std::string level_name;
    switch (message.level) {
      case spdlog::level::trace:
        level_name = "TRACE";
        break;
      case spdlog::level::debug:
        level_name = "DEBUG";
        break;
      case spdlog::level::info:
        level_name = "INFO";
        break;
      case spdlog::level::warn:
        level_name = "WARN";
        break;
      case spdlog::level::err:
        level_name = "ERROR";
        break;
      case spdlog::level::critical:
        level_name = "CRITICAL";
        break;
      case spdlog::level::off:
        level_name = "OFF";
        break;
      case spdlog::level::n_levels:
        level_name = "UNKNOWN";
        break;
    }
    spdlog::details::fmt_helper::append_string_view(spdlog::string_view_t(level_name.data(), level_name.size()),
                                                    destination);
  }

  [[nodiscard]] std::unique_ptr<spdlog::custom_flag_formatter> clone() const override {
    return std::make_unique<UppercaseLevelFormatter>();
  }
};

LoggerState& State() {
  static LoggerState state;
  return state;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string lower_copy(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return result;
}

[[nodiscard]] std::string choose_text(std::string_view configured, const char* fallback,
                                      std::string_view final_fallback) {
  if (!configured.empty()) {
    return std::string(configured);
  }
  if (fallback != nullptr && fallback[0] != '\0') {
    return std::string(fallback);
  }
  return std::string(final_fallback);
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

[[nodiscard]] bool try_parse_level(std::string_view text, Level* level) noexcept {
  if (level == nullptr) {
    return false;
  }

  text = trim(text);
  const std::string lower = lower_copy(text);
  if (lower == "trace") {
    *level = Level::Trace;
    return true;
  }
  if (lower == "debug") {
    *level = Level::Debug;
    return true;
  }
  if (lower == "info") {
    *level = Level::Info;
    return true;
  }
  if (lower == "warn" || lower == "warning") {
    *level = Level::Warn;
    return true;
  }
  if (lower == "error" || lower == "err") {
    *level = Level::Error;
    return true;
  }
  if (lower == "critical" || lower == "fatal") {
    *level = Level::Critical;
    return true;
  }
  if (lower == "off" || lower == "disabled" || lower == "none") {
    *level = Level::Off;
    return true;
  }
  return false;
}

[[nodiscard]] spdlog::level::level_enum to_spdlog_level(Level level) noexcept {
  switch (level) {
    case Level::Trace:
      return spdlog::level::trace;
    case Level::Debug:
      return spdlog::level::debug;
    case Level::Info:
      return spdlog::level::info;
    case Level::Warn:
      return spdlog::level::warn;
    case Level::Error:
      return spdlog::level::err;
    case Level::Critical:
      return spdlog::level::critical;
    case Level::Off:
      return spdlog::level::off;
  }
  return spdlog::level::off;
}

[[nodiscard]] bool is_level_enabled(Level requested_level, Level configured_level) noexcept {
  return configured_level != Level::Off && requested_level != Level::Off &&
         static_cast<int>(requested_level) >= static_cast<int>(configured_level);
}

[[nodiscard]] Level most_verbose(Level lhs, Level rhs) noexcept {
  return static_cast<int>(lhs) < static_cast<int>(rhs) ? lhs : rhs;
}

[[nodiscard]] bool parse_level_or_error(std::string_view name, std::string_view text, Level* level,
                                        std::string* error) {
  if (try_parse_level(text, level)) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string(name) + " must be trace, debug, info, warn, error, critical, or off.";
  }
  return false;
}

[[nodiscard]] fs::path resolve_path_from_base(const fs::path& path, const fs::path& base_dir) {
  if (path.empty() || ksj::base::path::is_absolute_path_like(path)) {
    return path;
  }
  return base_dir / path;
}

[[nodiscard]] std::unique_ptr<spdlog::formatter> make_formatter(const std::string& pattern) {
  auto formatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::local);
  formatter->add_flag<UppercaseLevelFormatter>('l');
  formatter->set_pattern(pattern);
  return formatter;
}

[[nodiscard]] fs::path daily_log_file_path(const fs::path& base_file_path) {
  const auto now = spdlog::log_clock::now();
  return fs::path(spdlog::sinks::daily_filename_calculator::calc_filename(
    base_file_path.string(), spdlog::details::os::localtime(spdlog::log_clock::to_time_t(now))));
}

[[nodiscard]] Level compute_effective_level(bool console_enabled, Level console_level, bool file_enabled,
                                            Level file_level) noexcept {
  bool has_enabled_sink = false;
  Level effective_level = Level::Off;

  const auto consider = [&](bool enabled, Level level) {
    if (!enabled) {
      return;
    }
    if (!has_enabled_sink) {
      effective_level = level;
      has_enabled_sink = true;
      return;
    }
    effective_level = most_verbose(effective_level, level);
  };

  consider(file_enabled, file_level);
  consider(console_enabled, console_level);
  return has_enabled_sink ? effective_level : Level::Off;
}

[[nodiscard]] bool validate_config(const ksj::config::LoggingConfig& config, const std::string& logger_name,
                                   const std::string& file_path, std::string* error_message) {
  if (logger_name.empty()) {
    if (error_message != nullptr) {
      *error_message = "logging.logger_name must not be empty after default resolution.";
    }
    return false;
  }
  if (!config.console.enabled && !config.file.enabled) {
    if (error_message != nullptr) {
      *error_message = "Logging configuration produced zero sinks.";
    }
    return false;
  }
  if (config.async && config.queue_size == 0) {
    if (error_message != nullptr) {
      *error_message = "logging.queue_size must be greater than zero when logging.async=true.";
    }
    return false;
  }
  if (config.async && config.async_worker_count == 0) {
    if (error_message != nullptr) {
      *error_message = "logging.async_worker_count must be greater than zero when logging.async=true.";
    }
    return false;
  }
  if (config.async && config.async_worker_count > 1000) {
    if (error_message != nullptr) {
      *error_message = "logging.async_worker_count must be in the range [1, 1000].";
    }
    return false;
  }
  if (config.file.enabled && file_path.empty()) {
    if (error_message != nullptr) {
      *error_message = "logging.file.path must not be empty when logging.file.enabled=true.";
    }
    return false;
  }
  if (config.file.retention_days > std::numeric_limits<std::uint16_t>::max()) {
    if (error_message != nullptr) {
      *error_message = "logging.file.retention_days must be in the range [0, 65535].";
    }
    return false;
  }
  if (!ascii_iequals(config.output_format, "text")) {
    if (error_message != nullptr) {
      *error_message = "logging.output_format currently supports only text.";
    }
    return false;
  }
  return true;
}

void reset_state_unlocked(LoggerState& state) {
  try {
    if (state.logger != nullptr) {
      state.logger->flush();
    }
    if (state.thread_pool != nullptr) {
      while (state.thread_pool->queue_size() != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  } catch (...) {}

  state.logger.reset();
  state.sinks.clear();
  state.thread_pool.reset();
  try {
    if (!state.logger_name.empty()) {
      spdlog::drop(state.logger_name);
    }
  } catch (...) {}
  state.config = ksj::config::LoggingConfig{};
  state.logger_name.clear();
  state.base_dir.clear();
  state.default_logger_name.clear();
  state.default_file_path.clear();
  state.resolved_file_path.clear();
  state.active_file_path.clear();
  state.periodic_flush_ms.store(0, std::memory_order_relaxed);
  state.next_periodic_flush_ns.store(0, std::memory_order_relaxed);
  state.last_file_check = std::chrono::steady_clock::time_point{};
  state.effective_level.store(Level::Off, std::memory_order_release);
  state.configured = false;
  state.is_default_console_fallback = false;
}

void close_logger_unlocked(LoggerState& state) noexcept {
  try {
    if (state.logger != nullptr) {
      state.logger->flush();
    }
    if (state.thread_pool != nullptr) {
      while (state.thread_pool->queue_size() != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  } catch (...) {}

  state.logger.reset();
  state.sinks.clear();
  state.thread_pool.reset();
  try {
    if (!state.logger_name.empty()) {
      spdlog::drop(state.logger_name);
    }
  } catch (...) {}
}

[[nodiscard]] bool create_logger_unlocked(LoggerState& state, const ksj::config::LoggingConfig& config,
                                          const std::string& logger_name, const fs::path& resolved_file_path,
                                          Level flush_level, Level console_level, Level file_level) {
  if (config.file.enabled) {
    if (resolved_file_path.has_parent_path()) {
      fs::create_directories(resolved_file_path.parent_path());
    }
    auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
      resolved_file_path.string(), 0, 0, false, static_cast<std::uint16_t>(config.file.retention_days));
    file_sink->set_level(to_spdlog_level(file_level));
    file_sink->set_formatter(make_formatter(config.file.pattern));
    state.active_file_path = daily_log_file_path(resolved_file_path);
    state.sinks.push_back(std::move(file_sink));
  }

  if (config.console.enabled) {
    // stdout is reserved for a process's data/protocol output, notably CLI JSON.
    // All diagnostic console records therefore use stderr.
    spdlog::sink_ptr console_sink;
    if (config.console.console_color) {
      console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    } else {
      console_sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
    }
    console_sink->set_level(to_spdlog_level(console_level));
    console_sink->set_formatter(make_formatter(config.console.pattern));
    state.sinks.push_back(std::move(console_sink));
  }

  if (config.async) {
    state.thread_pool = std::make_shared<spdlog::details::thread_pool>(config.queue_size, config.async_worker_count);
    state.logger = std::make_shared<spdlog::async_logger>(logger_name, state.sinks.begin(), state.sinks.end(),
                                                          state.thread_pool, spdlog::async_overflow_policy::block);
  } else {
    state.logger = std::make_shared<spdlog::logger>(logger_name, state.sinks.begin(), state.sinks.end());
  }

  const auto effective_level =
    compute_effective_level(config.console.enabled, console_level, config.file.enabled, file_level);
  state.logger->set_level(to_spdlog_level(effective_level));
  state.effective_level.store(effective_level, std::memory_order_release);
  state.logger->flush_on(to_spdlog_level(flush_level));
  spdlog::register_logger(state.logger);
  state.periodic_flush_ms.store(config.periodic_flush_ms, std::memory_order_relaxed);
  const auto next_flush =
    std::chrono::steady_clock::now().time_since_epoch() + std::chrono::milliseconds(config.periodic_flush_ms);
  state.next_periodic_flush_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(next_flush).count(),
                                     std::memory_order_relaxed);
  return true;
}

void flush_periodically_if_needed(const std::shared_ptr<spdlog::logger>& logger) {
  if (logger == nullptr) {
    return;
  }

  auto& state = State();
  const std::size_t interval_ms = state.periodic_flush_ms.load(std::memory_order_relaxed);
  if (interval_ms == 0) {
    return;
  }

  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  auto expected_next_ns = state.next_periodic_flush_ns.load(std::memory_order_relaxed);
  if (now_ns < expected_next_ns) {
    return;
  }

  const auto replacement_ns =
    now_ns + std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds(interval_ms)).count();
  if (state.next_periodic_flush_ns.compare_exchange_strong(expected_next_ns, replacement_ns, std::memory_order_relaxed,
                                                           std::memory_order_relaxed)) {
    logger->flush();
  }
}

void restore_file_sink_if_needed() {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  if (!state.configured || !state.config.file.enabled || state.resolved_file_path.empty()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (state.last_file_check.time_since_epoch().count() != 0 && now - state.last_file_check < std::chrono::seconds(1)) {
    return;
  }
  state.last_file_check = now;

  const fs::path expected_file_path = daily_log_file_path(state.resolved_file_path);
  if (fs::exists(expected_file_path)) {
    state.active_file_path = expected_file_path;
    return;
  }

  Level flush_level = Level::Warn;
  Level console_level = Level::Info;
  Level file_level = Level::Info;
  if (!try_parse_level(state.config.flush_level, &flush_level) ||
      !try_parse_level(state.config.console.level, &console_level) ||
      !try_parse_level(state.config.file.level, &file_level)) {
    return;
  }

  const auto config = state.config;
  const auto logger_name = state.logger_name;
  const auto resolved_file_path = state.resolved_file_path;
  close_logger_unlocked(state);
  try {
    (void)create_logger_unlocked(state, config, logger_name, resolved_file_path, flush_level, console_level,
                                 file_level);
    state.config = config;
    state.logger_name = logger_name;
    state.resolved_file_path = resolved_file_path;
    state.configured = true;
  } catch (...) {
    state.configured = false;
    state.effective_level.store(Level::Off, std::memory_order_release);
  }
}

[[nodiscard]] std::shared_ptr<spdlog::logger> current_logger() {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  return state.logger;
}

[[nodiscard]] bool configure_unlocked(LoggerState& state, const ksj::config::LoggingConfig& config,
                                      const char* base_dir, const char* default_logger_name,
                                      const char* default_file_path, ConfigurationKind configuration_kind,
                                      std::string* error_message) {
  if (state.configured) {
    if (configuration_kind == ConfigurationKind::default_console_fallback) {
      if (error_message != nullptr) {
        error_message->clear();
      }
      return true;
    }
    if (!state.is_default_console_fallback) {
      if (error_message != nullptr) {
        *error_message = "Logging is already configured. Call Shutdown() before reconfiguring.";
      }
      return false;
    }

    // A real process configuration supersedes the minimal process-entry
    // fallback. It is the only configuration transition allowed without an
    // explicit Shutdown(). Keep the fallback alive until the new settings
    // have passed validation below.
  }

  try {
    const fs::path resolved_base_dir(base_dir != nullptr && base_dir[0] != '\0' ? base_dir : ".");
    const std::string logger_name = choose_text(config.logger_name, default_logger_name, "KSpaceJet");
    const fs::path resolved_file_path = [&] {
      if (!config.file.path.empty()) {
        return resolve_path_from_base(fs::path(config.file.path), resolved_base_dir);
      }
      if (default_file_path != nullptr && default_file_path[0] != '\0') {
        return fs::path(default_file_path);
      }
      return resolved_base_dir / (logger_name + ".log");
    }();

    if (!validate_config(config, logger_name, resolved_file_path.string(), error_message)) {
      return false;
    }

    Level flush_level = Level::Warn;
    Level console_level = Level::Info;
    Level file_level = Level::Info;
    if (!parse_level_or_error("logging.flush_level", config.flush_level, &flush_level, error_message) ||
        !parse_level_or_error("logging.console.level", config.console.level, &console_level, error_message) ||
        !parse_level_or_error("logging.file.level", config.file.level, &file_level, error_message)) {
      return false;
    }

    reset_state_unlocked(state);
    (void)create_logger_unlocked(state, config, logger_name, resolved_file_path, flush_level, console_level,
                                 file_level);
    state.config = config;
    state.logger_name = logger_name;
    state.base_dir = base_dir != nullptr ? base_dir : "";
    state.default_logger_name = default_logger_name != nullptr ? default_logger_name : "";
    state.default_file_path = default_file_path != nullptr ? default_file_path : "";
    state.resolved_file_path = resolved_file_path;
    state.last_file_check = std::chrono::steady_clock::now();
    state.configured = true;
    state.is_default_console_fallback = configuration_kind == ConfigurationKind::default_console_fallback;
  } catch (const std::exception& error) {
    try {
      reset_state_unlocked(state);
    } catch (...) {}
    if (error_message != nullptr) {
      *error_message = std::string("Failed to configure KSpaceJet logging: ") + error.what();
    }
    return false;
  } catch (...) {
    try {
      reset_state_unlocked(state);
    } catch (...) {}
    if (error_message != nullptr) {
      *error_message = "Failed to configure KSpaceJet logging: unknown exception.";
    }
    return false;
  }

  if (error_message != nullptr) {
    error_message->clear();
  }
  return true;
}

} // namespace

bool Configure(const ksj::config::LoggingConfig& config, const char* base_dir, const char* default_logger_name,
               const char* default_file_path, std::string* error_message) {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  return configure_unlocked(state, config, base_dir, default_logger_name, default_file_path,
                            ConfigurationKind::explicit_configuration, error_message);
}

bool Configure(const char* config_path, const char* base_dir, std::string* error_message) {
  if (config_path == nullptr || config_path[0] == '\0') {
    if (error_message != nullptr) {
      *error_message = "Logging configuration path is empty.";
    }
    return false;
  }

  auto loaded = ksj::config::load_runtime_config_file(config_path);
  if (!loaded.ok()) {
    if (error_message != nullptr) {
      *error_message = loaded.status().to_string();
    }
    return false;
  }
  return Configure(loaded.value().logging, base_dir, nullptr, nullptr, error_message);
}

bool ConfigureDefaultConsole(std::string_view logger_name, std::string* error_message) {
  ksj::config::LoggingConfig config;
  config.logger_name = logger_name.empty() ? "KSpaceJet" : std::string(logger_name);
  config.async = false;
  config.console.enabled = true;
  config.console.console_color = false;
  config.file.enabled = false;

  auto& state = State();
  std::lock_guard lock(state.mutex);
  return configure_unlocked(state, config, ".", nullptr, nullptr, ConfigurationKind::default_console_fallback,
                            error_message);
}

bool IsConfigured() {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  return state.configured;
}

bool ShouldLog(Level level) {
  auto& state = State();
  return is_level_enabled(level, state.effective_level.load(std::memory_order_acquire));
}

void Log(Level level, const char* message, const char* file_name, int line, const char* function_name) noexcept {
  try {
    restore_file_sink_if_needed();
    const auto logger = current_logger();
    if (logger == nullptr) {
      return;
    }
    logger->log(
      spdlog::source_loc{file_name == nullptr ? "" : file_name, line, function_name == nullptr ? "" : function_name},
      to_spdlog_level(level), "{}", message == nullptr ? "" : message);
    flush_periodically_if_needed(logger);
  } catch (...) {}
}

void Log(Level level, const std::string& message, const char* file_name, int line, const char* function_name) noexcept {
  Log(level, message.c_str(), file_name, line, function_name);
}

void Flush() noexcept {
  try {
    const auto logger = current_logger();
    if (logger != nullptr) {
      logger->flush();
    }

    auto& state = State();
    std::shared_ptr<spdlog::details::thread_pool> thread_pool;
    {
      std::lock_guard lock(state.mutex);
      thread_pool = state.thread_pool;
    }
    if (thread_pool != nullptr) {
      while (thread_pool->queue_size() != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  } catch (...) {}
}

void Shutdown() noexcept {
  try {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    reset_state_unlocked(state);
  } catch (...) {}
}

} // namespace ksj::logging
