#pragma once

#include "kspacejet/base/types.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#ifndef KSJ_PERFORMANCE_LEVEL
#define KSJ_PERFORMANCE_LEVEL 0
#endif

namespace ksj::performance {

inline constexpr int kPerformanceLevel = KSJ_PERFORMANCE_LEVEL;
inline constexpr bool kCountersEnabled = kPerformanceLevel >= 1;
inline constexpr bool kTraceEnabled = kPerformanceLevel >= 2;

enum class MetricKind : std::uint8_t {
  counter,
  duration_ns,
  gauge,
};

struct SliceEventInfo {
  ksj::base::u32 frame_num = 0;
  ksj::base::u32 batch_num = 0;
  ksj::base::u32 volume_num = 0;
  ksj::base::u32 map_num = 0;
  ksj::base::u32 echo_num = 0;
  ksj::base::u32 slice_num = 0;
  ksj::base::u32 phase_num = 0;
  ksj::base::u32 coil_num = 0;
  ksj::base::u32 image_size_bytes = 0;
  ksj::base::u32 data_length = 0;
  ksj::base::u32 data_length2 = 0;
  ksj::base::u32 element_size = 0;
  ksj::base::u32 image_type = 0;
  bool is_last_slice = false;
};

struct ProcessArtifactOptions {
  std::filesystem::path artifact_dir;
  std::string role_name;
};

[[nodiscard]] bool enabled() noexcept;

void counter_add(std::string_view name, std::uint64_t value = 1) noexcept;
void gauge_set(std::string_view name, std::uint64_t value) noexcept;
void duration_add(std::string_view name, std::chrono::nanoseconds value) noexcept;

void trace_mark(std::string_view name) noexcept;
void trace_counter(std::string_view name, double value) noexcept;
void trace_span_begin(std::string_view name, std::string_view component = {}, std::string_view stage = {},
                      std::string_view item = {}, std::string_view span_kind = {}) noexcept;
void trace_span_end(std::string_view name, std::string_view component = {}, std::string_view stage = {},
                    std::string_view item = {}, std::string_view span_kind = {}) noexcept;

class ScopedDuration {
public:
  explicit ScopedDuration(std::string_view name) noexcept;
  ~ScopedDuration();

  ScopedDuration(const ScopedDuration&) = delete;
  ScopedDuration& operator=(const ScopedDuration&) = delete;

private:
  std::string name_;
  std::chrono::steady_clock::time_point started_{};
  bool armed_ = false;
};

class ScopedTrace {
public:
  explicit ScopedTrace(std::string_view name) noexcept;
  ScopedTrace(std::string_view name, std::string_view component, std::string_view stage, std::string_view item,
              std::string_view span_kind) noexcept;
  ~ScopedTrace();

  ScopedTrace(const ScopedTrace&) = delete;
  ScopedTrace& operator=(const ScopedTrace&) = delete;

private:
  std::string name_;
  std::string component_;
  std::string stage_;
  std::string item_;
  std::string span_kind_;
  bool armed_ = false;
};

void initialize_process_artifacts(ProcessArtifactOptions options = {});
void on_scan_start();
// Called once, immediately before dispatching the first acquired-data message for a scan.
void on_reconstruction_started();
void on_scan_uid_resolved(int scan_uid);
void on_provider_resolved(std::string_view provider_name, std::string_view provider_version);
void on_ismrmrd_dataset_resolved(std::string_view dataset_path);
void on_slice_sent(int scan_uid, const SliceEventInfo& slice_info, ksj::base::u32 transfer_index,
                   ksj::base::u32 expected_transfer_count, bool is_final_image);
void on_reconstruction_completed(std::string_view reason = {});
void on_reconstruction_failed(std::string_view reason);
void on_reconstruction_stopped(std::string_view reason);
void on_scan_end();
void flush_process_artifacts();

[[nodiscard]] std::filesystem::path artifact_dir();
[[nodiscard]] std::filesystem::path cpu_profiles_dir();
[[nodiscard]] std::filesystem::path pprof_reports_dir();

} // namespace ksj::performance

#define KSJ_PERF_CONCAT_IMPL_(lhs, rhs) lhs##rhs
#define KSJ_PERF_CONCAT_(lhs, rhs) KSJ_PERF_CONCAT_IMPL_(lhs, rhs)

#if KSJ_PERFORMANCE_LEVEL >= 1
#define KSJ_PERF_SCOPE(name) ::ksj::performance::ScopedDuration KSJ_PERF_CONCAT_(_ksj_perf_scope_, __LINE__)(name)
#define KSJ_PERF_COUNTER_ADD(name, value) ::ksj::performance::counter_add((name), static_cast<std::uint64_t>(value))
#define KSJ_PERF_GAUGE_SET(name, value) ::ksj::performance::gauge_set((name), static_cast<std::uint64_t>(value))
#else
#define KSJ_PERF_SCOPE(name)                                                                                           \
  do {                                                                                                                 \
  } while (false)
#define KSJ_PERF_COUNTER_ADD(name, value)                                                                              \
  do {                                                                                                                 \
    (void)sizeof(value);                                                                                               \
  } while (false)
#define KSJ_PERF_GAUGE_SET(name, value)                                                                                \
  do {                                                                                                                 \
    (void)sizeof(value);                                                                                               \
  } while (false)
#endif

#if KSJ_PERFORMANCE_LEVEL >= 2
#define KSJ_TRACE_MARK(name) ::ksj::performance::trace_mark(name)
#define KSJ_TRACE_COUNTER(name, value) ::ksj::performance::trace_counter((name), static_cast<double>(value))
#define KSJ_TRACE_SCOPE(name) ::ksj::performance::ScopedTrace KSJ_PERF_CONCAT_(_ksj_trace_scope_, __LINE__)(name)
#define KSJ_TRACE_SCOPE_EX(name, component, stage, item, span_kind)                                                    \
  ::ksj::performance::ScopedTrace KSJ_PERF_CONCAT_(_ksj_trace_scope_, __LINE__)((name), (component), (stage), (item),  \
                                                                                (span_kind))
#else
#define KSJ_TRACE_MARK(name)                                                                                           \
  do {                                                                                                                 \
  } while (false)
#define KSJ_TRACE_COUNTER(name, value)                                                                                 \
  do {                                                                                                                 \
    (void)sizeof(value);                                                                                               \
  } while (false)
#define KSJ_TRACE_SCOPE(name)                                                                                          \
  do {                                                                                                                 \
  } while (false)
#define KSJ_TRACE_SCOPE_EX(name, component, stage, item, span_kind)                                                    \
  do {                                                                                                                 \
  } while (false)
#endif
