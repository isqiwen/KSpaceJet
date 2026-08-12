#pragma once

#include "kspacejet/base/status.hpp"
#include "kspacejet/config/runtime_config.hpp"

#include <atomic>
#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <spdlog/fmt/fmt.h>

template <> struct fmt::formatter<ksj::base::Status> : fmt::formatter<std::string_view> {
  template <typename FormatContext> auto format(const ksj::base::Status& status, FormatContext& ctx) const {
    const auto text = status.to_string();
    return fmt::formatter<std::string_view>::format(std::string_view{text.data(), text.size()}, ctx);
  }
};

namespace ksj::logging {

enum class Level {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
  Critical,
  Off,
};

[[nodiscard]] bool Configure(const ksj::config::LoggingConfig& config, const char* base_dir,
                             const char* default_logger_name, const char* default_file_path,
                             std::string* error_message = nullptr);
[[nodiscard]] bool Configure(const char* config_path, const char* base_dir, std::string* error_message = nullptr);
[[nodiscard]] bool EnsureConfigured();
[[nodiscard]] bool IsConfigured();
[[nodiscard]] bool ShouldLog(Level level);
void Log(Level level, const char* message, const char* file_name = nullptr, int line = 0,
         const char* function_name = nullptr);
void Log(Level level, const std::string& message, const char* file_name = nullptr, int line = 0,
         const char* function_name = nullptr);
void Flush();
void Shutdown();

namespace detail {

inline std::string ToMessageString(const char* value) {
  return value == nullptr ? std::string() : std::string(value);
}

inline std::string ToMessageString(char* value) {
  return ToMessageString(static_cast<const char*>(value));
}

inline std::string ToMessageString(const std::string& value) {
  return value;
}

inline std::string ToMessageString(std::string_view value) {
  return std::string(value);
}

template <std::size_t N> inline std::string ToMessageString(const char (&value)[N]) {
  return std::string(value);
}

template <typename T, typename = void> struct IsOstreamWritable : std::false_type {};

template <typename T>
struct IsOstreamWritable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T>
inline std::enable_if_t<IsOstreamWritable<std::decay_t<T>>::value, std::string> ToMessageString(T&& value) {
  std::ostringstream stream;
  stream << std::forward<T>(value);
  return stream.str();
}

template <typename T>
inline std::enable_if_t<!IsOstreamWritable<std::decay_t<T>>::value, std::string> ToMessageString(T&&) {
  return "<unformattable>";
}

template <typename Format, typename... Args> inline std::string FormatMessage(Format&& format, Args&&... args) {
  std::string format_text = ToMessageString(std::forward<Format>(format));
  try {
    return fmt::vformat(fmt::string_view(format_text.data(), format_text.size()), fmt::make_format_args(args...));
  } catch (const fmt::format_error& error) {
    format_text.append(" [log format error: ");
    format_text.append(error.what());
    format_text.push_back(']');
    return format_text;
  }
}

} // namespace detail

template <typename Format, typename... Args>
inline void LogFormatted(Level level, const char* file_name, int line, const char* function_name, Format&& format,
                         Args&&... args) {
  if (!ShouldLog(level)) {
    return;
  }

  if constexpr (sizeof...(Args) == 0) {
    Log(level, detail::ToMessageString(std::forward<Format>(format)), file_name, line, function_name);
  } else {
    Log(level, detail::FormatMessage(std::forward<Format>(format), std::forward<Args>(args)...), file_name, line,
        function_name);
  }
}

[[nodiscard]] inline bool ShouldLogOccurrence(std::size_t occurrence, std::size_t interval) noexcept {
  return interval <= 1 || occurrence == 1 || (occurrence % interval) == 0;
}

} // namespace ksj::logging

#define KSJ_LOG_CONFIGURE(config_path, base_dir, error_message)                                                        \
  ::ksj::logging::Configure((config_path), (base_dir), (error_message))

#define KSJ_LOG(level, ...)                                                                                            \
  do {                                                                                                                 \
    ::ksj::logging::LogFormatted((level), __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__);                              \
  } while (false)

#define KSJ_LOG_TRACE(...) KSJ_LOG(::ksj::logging::Level::Trace, __VA_ARGS__)
#define KSJ_LOG_DEBUG(...) KSJ_LOG(::ksj::logging::Level::Debug, __VA_ARGS__)
#define KSJ_LOG_INFO(...) KSJ_LOG(::ksj::logging::Level::Info, __VA_ARGS__)
#define KSJ_LOG_WARN(...) KSJ_LOG(::ksj::logging::Level::Warn, __VA_ARGS__)
#define KSJ_LOG_ERROR(...) KSJ_LOG(::ksj::logging::Level::Error, __VA_ARGS__)
#define KSJ_LOG_CRITICAL(...) KSJ_LOG(::ksj::logging::Level::Critical, __VA_ARGS__)

#define KSJ_LOG_EVERY_N(level, interval, ...)                                                                          \
  do {                                                                                                                 \
    static ::std::atomic_size_t ksj_log_occurrence_counter{0};                                                         \
    const ::std::size_t ksj_log_occurrence = ksj_log_occurrence_counter.fetch_add(1, ::std::memory_order_relaxed) + 1; \
    if (::ksj::logging::ShouldLogOccurrence(ksj_log_occurrence, static_cast<::std::size_t>(interval))) {               \
      KSJ_LOG((level), __VA_ARGS__);                                                                                   \
    }                                                                                                                  \
  } while (false)

#define KSJ_LOG_TRACE_EVERY_N(interval, ...) KSJ_LOG_EVERY_N(::ksj::logging::Level::Trace, (interval), __VA_ARGS__)
#define KSJ_LOG_DEBUG_EVERY_N(interval, ...) KSJ_LOG_EVERY_N(::ksj::logging::Level::Debug, (interval), __VA_ARGS__)
#define KSJ_LOG_INFO_EVERY_N(interval, ...) KSJ_LOG_EVERY_N(::ksj::logging::Level::Info, (interval), __VA_ARGS__)
#define KSJ_LOG_WARN_EVERY_N(interval, ...) KSJ_LOG_EVERY_N(::ksj::logging::Level::Warn, (interval), __VA_ARGS__)
#define KSJ_LOG_ERROR_EVERY_N(interval, ...) KSJ_LOG_EVERY_N(::ksj::logging::Level::Error, (interval), __VA_ARGS__)
#define KSJ_LOG_CRITICAL_EVERY_N(interval, ...)                                                                        \
  KSJ_LOG_EVERY_N(::ksj::logging::Level::Critical, (interval), __VA_ARGS__)

#define KSJ_LOG_RETURN_IF(level, condition, ret, ...)                                                                  \
  do {                                                                                                                 \
    if (condition) {                                                                                                   \
      KSJ_LOG((level), __VA_ARGS__);                                                                                   \
      return ret;                                                                                                      \
    }                                                                                                                  \
  } while (false)

#define KSJ_LOG_ERROR_RETURN_IF(condition, ret, ...)                                                                   \
  KSJ_LOG_RETURN_IF(::ksj::logging::Level::Error, (condition), ret, __VA_ARGS__)

#define KSJ_LOG_FLUSH() ::ksj::logging::Flush()
#define KSJ_LOG_SHUTDOWN() ::ksj::logging::Shutdown()
