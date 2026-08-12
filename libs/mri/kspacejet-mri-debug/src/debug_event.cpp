#include "kspacejet/mri/debug/debug_event.hpp"

#include "kspacejet/base/file.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/process_runtime/debug_dump.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <thread>

namespace ksj::mri::debug {

namespace {

[[nodiscard]] std::uint64_t unix_time_ns() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

[[nodiscard]] std::size_t current_thread_hash() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

[[nodiscard]] std::string resolve_debug_event_path(std::string_view file_name) {
  const std::string file_name_string =
    file_name.empty() ? std::string("events/debug_events.jsonl") : std::string(file_name);
  return ksj::process_runtime::debug_dump::ResolveDebugFile(file_name_string.c_str());
}

[[nodiscard]] bool payload_looks_like_json_object(std::string_view payload) {
  while (!payload.empty() && std::isspace(static_cast<unsigned char>(payload.front())) != 0) {
    payload.remove_prefix(1U);
  }
  return !payload.empty() && payload.front() == '{';
}

} // namespace

bool debug_event_enabled(const std::string_view category) {
  return ksj::process_runtime::debug_dump::IsDebugDumpEnabledForCategory(category);
}

std::string json_escape(const std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
  for (const char ch : value) {
    switch (ch) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20U) {
          constexpr char hex[] = "0123456789abcdef";
          escaped += "\\u00";
          escaped.push_back(hex[(static_cast<unsigned char>(ch) >> 4U) & 0x0FU]);
          escaped.push_back(hex[static_cast<unsigned char>(ch) & 0x0FU]);
        } else {
          escaped.push_back(ch);
        }
        break;
    }
  }
  return escaped;
}

bool append_debug_jsonl(const std::string_view event_name, const std::string_view payload_json,
                        const DebugEventOptions options) {
  if (!debug_event_enabled(options.category)) {
    return false;
  }

  const auto path = resolve_debug_event_path(options.file_name);
  if (!ksj::process_runtime::debug_dump::PrepareDebugFilePath(path)) {
    KSJ_LOG_ERROR("Failed to prepare debug event path [{}].", path);
    return false;
  }

  std::ostringstream line;
  line << "{\"ts_ns\":" << unix_time_ns() << ",\"thread\":" << current_thread_hash() << ",\"category\":\""
       << json_escape(options.category) << "\",\"event\":\"" << json_escape(event_name) << "\"";
  if (!payload_json.empty() && payload_json != "{}") {
    if (payload_looks_like_json_object(payload_json)) {
      line << ",\"payload\":" << payload_json;
    } else {
      line << ",\"payload_text\":\"" << json_escape(payload_json) << "\"";
    }
  }
  line << "}\n";

  const auto result = ksj::base::file::append_text_file(path, line.str());
  if (!result.complete()) {
    KSJ_LOG_ERROR("Failed to append debug event [{}]: wrote [{}] of [{}] bytes. {}", result.path,
                  result.transferred_bytes, result.requested_bytes, result.error);
    return false;
  }
  return true;
}

bool append_debug_timing_event(const std::string_view name, const double duration_ms, const DebugEventOptions options) {
  std::ostringstream payload;
  payload << "{\"name\":\"" << json_escape(name) << "\",\"duration_ms\":" << duration_ms << "}";
  return append_debug_jsonl("scope_timing", payload.str(), options);
}

} // namespace ksj::mri::debug
