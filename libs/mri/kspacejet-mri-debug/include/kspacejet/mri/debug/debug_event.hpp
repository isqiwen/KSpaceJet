#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace ksj::mri::debug {

struct DebugEventOptions {
  std::string_view category{"debug_event"};
  std::string_view file_name{"events/debug_events.jsonl"};
};

[[nodiscard]] bool debug_event_enabled(std::string_view category = "debug_event");

[[nodiscard]] std::string json_escape(std::string_view value);

[[nodiscard]] bool append_debug_jsonl(std::string_view event_name, std::string_view payload_json = "{}",
                                      DebugEventOptions options = {});

[[nodiscard]] bool append_debug_timing_event(std::string_view name, double duration_ms,
                                             DebugEventOptions options = {
                                               .category = "debug_timer",
                                               .file_name = "events/debug_timing.jsonl",
                                             });

class ScopedDebugTimer {
public:
  explicit ScopedDebugTimer(std::string_view name, DebugEventOptions options =
                                                     {
                                                       .category = "debug_timer",
                                                       .file_name = "events/debug_timing.jsonl",
                                                     })
      : enabled_(debug_event_enabled(options.category)) {
    if (!enabled_) {
      return;
    }
    name_ = std::string(name);
    category_ = std::string(options.category);
    file_name_ = std::string(options.file_name);
    start_ = clock_type::now();
  }

  ScopedDebugTimer(const ScopedDebugTimer&) = delete;
  ScopedDebugTimer& operator=(const ScopedDebugTimer&) = delete;

  ScopedDebugTimer(ScopedDebugTimer&&) = delete;
  ScopedDebugTimer& operator=(ScopedDebugTimer&&) = delete;

  ~ScopedDebugTimer() {
    if (!enabled_) {
      return;
    }
    const auto end = clock_type::now();
    const auto duration = std::chrono::duration<double, std::milli>(end - start_).count();
    (void)append_debug_timing_event(
      name_, duration,
      DebugEventOptions{.category = std::string_view(category_), .file_name = std::string_view(file_name_)});
  }

  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

private:
  using clock_type = std::chrono::steady_clock;

  bool enabled_{false};
  std::string name_;
  std::string category_;
  std::string file_name_;
  clock_type::time_point start_{};
};

} // namespace ksj::mri::debug

#ifndef KSJ_DEBUG_SCOPE_TIMER
#define KSJ_DEBUG_SCOPE_TIMER_CONCAT_IMPL(lhs, rhs) lhs##rhs
#define KSJ_DEBUG_SCOPE_TIMER_CONCAT(lhs, rhs) KSJ_DEBUG_SCOPE_TIMER_CONCAT_IMPL(lhs, rhs)
#define KSJ_DEBUG_SCOPE_TIMER(name)                                                                                    \
  ::ksj::mri::debug::ScopedDebugTimer KSJ_DEBUG_SCOPE_TIMER_CONCAT(ksj_debug_scope_timer_, __LINE__) {                 \
    name                                                                                                               \
  }
#endif
