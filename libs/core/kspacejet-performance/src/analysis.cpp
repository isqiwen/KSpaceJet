#include "kspacejet/base/types.hpp"
#include "kspacejet/performance/analysis.hpp"
#include "lightweight_scan_timing.hpp"

#include "kspacejet/base/path.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/platform/process.hpp"
#include "kspacejet/process_runtime/state_paths.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#if KSJ_PERFORMANCE_LEVEL >= 2
#include <gperftools/profiler.h>
#endif

namespace ksj::performance {
namespace {

using SteadyClock = std::chrono::steady_clock;

struct ProcessSnapshot {
  std::string wall_time_utc;
  long long user_ms = 0;
  long long system_ms = 0;
  long long max_rss_kb = 0;
  long long current_rss_kb = 0;
};

struct ImageEvent {
  ksj::base::u32 image_index = 0;
  ksj::base::u32 transfer_index = 0;
  ksj::base::u32 expected_transfer_count = 0;
  std::string wall_time_utc;
  double since_scan_start_ms = -1.0;
  double since_recon_start_ms = -1.0;
  SliceEventInfo slice;
};

struct MetricAggregate {
  std::string name;
  MetricKind kind = MetricKind::counter;
  std::uint64_t updates = 0;
  std::uint64_t total = 0;
  std::uint64_t max = 0;
  std::uint64_t last = 0;
};

enum class TraceEventKind : std::uint8_t {
  mark,
  span_begin,
  span_end,
  counter,
};

struct TraceEvent {
  std::string wall_time_utc;
  double since_scan_start_ms = -1.0;
  double since_recon_start_ms = -1.0;
  TraceEventKind kind = TraceEventKind::mark;
  std::string name;
  double value = 0.0;
  std::string component;
  std::string stage;
  std::string item;
  std::string span_kind;
};

struct ScanRecord {
  int scan_uid = -1;
  std::string process_role;
  long long pid = -1;
  std::string status;
  std::string completion_reason;
  std::string provider_name;
  std::string provider_version;
  std::string ismrmrd_dataset_path;
  std::string cpu_profile_path;
  std::string cpu_profile_start_time_utc;
  std::string cpu_profile_stop_time_utc;
  std::string cpu_profile_stop_reason;
  std::string scan_start_time_utc;
  std::string recon_start_time_utc;
  std::string acquisition_end_time_utc;
  std::string first_final_image_time_utc;
  std::string last_final_image_time_utc;
  std::string recon_complete_time_utc;
  double acquisition_duration_ms = -1.0;
  double time_to_first_final_image_ms = -1.0;
  double time_to_last_final_image_ms = -1.0;
  double total_recon_ms = -1.0;
  double scan_to_recon_complete_ms = -1.0;
  ksj::base::u32 total_sent_image_count = 0;
  ksj::base::u32 final_image_count = 0;
  ksj::base::u32 expected_transfer_count = 0;
  ksj::base::u32 completed_scan_index = 0;
  ProcessSnapshot process_at_recon_start;
  ProcessSnapshot process_at_recon_complete;
  std::vector<ImageEvent> final_images;
  SteadyClock::time_point scan_start_steady{};
  SteadyClock::time_point recon_start_steady{};
  SteadyClock::time_point acquisition_end_steady{};
  SteadyClock::time_point recon_complete_steady{};
  bool has_scan_start = false;
  bool has_recon_start = false;
  bool has_acquisition_end = false;
  bool has_recon_complete = false;
  bool cpu_profiler_started = false;
  bool cpu_profiler_stopped = false;
  bool cpu_profiler_start_success = false;
};

struct Recorder {
  std::mutex mutex;
  bool initialized = false;
  std::filesystem::path artifact_dir;
  std::filesystem::path json_path;
  std::filesystem::path cpu_profiles_dir;
  std::filesystem::path pprof_reports_dir;
  std::string pending_ismrmrd_dataset_path;
  std::string role_name = "standalone";
  long long pid = 0;
  bool has_active_scan = false;
  ksj::base::u32 completed_scan_count = 0;
  bool profiler_running = false;
  bool exit_handler_registered = false;
  ScanRecord active_scan;
  std::vector<ScanRecord> completed_scans;
  std::vector<MetricAggregate> manual_metrics;
  std::vector<TraceEvent> trace_events;
};

[[nodiscard]] constexpr bool performance_enabled() noexcept {
  return true;
}

[[nodiscard]] constexpr bool cpu_profiler_enabled() noexcept {
  return KSJ_PERFORMANCE_LEVEL >= 2;
}

[[nodiscard]] Recorder& recorder() {
  static Recorder instance;
  return instance;
}

[[nodiscard]] long long current_process_id() noexcept {
  return static_cast<long long>(::getpid());
}

[[nodiscard]] std::tm utc_time_from(std::time_t raw_time) {
  std::tm time_info{};
  gmtime_r(&raw_time, &time_info);
  return time_info;
}

[[nodiscard]] std::string utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto raw_time = std::chrono::system_clock::to_time_t(now);
  const auto time_info = utc_time_from(raw_time);
  const auto milliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << time_info.tm_year + 1900 << '-' << std::setw(2) << time_info.tm_mon + 1
         << '-' << std::setw(2) << time_info.tm_mday << 'T' << std::setw(2) << time_info.tm_hour << ':' << std::setw(2)
         << time_info.tm_min << ':' << std::setw(2) << time_info.tm_sec << '.' << std::setw(3) << milliseconds << 'Z';
  return stream.str();
}

[[nodiscard]] std::string sanitize_path_component(std::string_view value) {
  std::string sanitized = ksj::base::path::sanitize_component(value);
  if (sanitized.empty()) {
    sanitized = "capture";
  }
  return sanitized;
}

[[nodiscard]] double duration_ms(SteadyClock::time_point start, SteadyClock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] long long read_current_rss_kb() {
  std::ifstream input("/proc/self/status");
  std::string key;
  while (input >> key) {
    if (key == "VmRSS:") {
      long long rss_kb = 0;
      input >> rss_kb;
      return rss_kb;
    }
    std::string rest_of_line;
    std::getline(input, rest_of_line);
  }
  return 0;
}

[[nodiscard]] ProcessSnapshot capture_process_snapshot() {
  ProcessSnapshot snapshot;
  snapshot.wall_time_utc = utc_timestamp();
  snapshot.current_rss_kb = read_current_rss_kb();
  rusage usage{};
  if (::getrusage(RUSAGE_SELF, &usage) == 0) {
    snapshot.user_ms =
      static_cast<long long>(usage.ru_utime.tv_sec) * 1000LL + static_cast<long long>(usage.ru_utime.tv_usec) / 1000LL;
    snapshot.system_ms =
      static_cast<long long>(usage.ru_stime.tv_sec) * 1000LL + static_cast<long long>(usage.ru_stime.tv_usec) / 1000LL;
    snapshot.max_rss_kb = static_cast<long long>(usage.ru_maxrss);
  }
  return snapshot;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
  std::ostringstream escaped;
  for (const char raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    switch (ch) {
      case '\"':
        escaped << "\\\"";
        break;
      case '\\':
        escaped << "\\\\";
        break;
      case '\b':
        escaped << "\\b";
        break;
      case '\f':
        escaped << "\\f";
        break;
      case '\n':
        escaped << "\\n";
        break;
      case '\r':
        escaped << "\\r";
        break;
      case '\t':
        escaped << "\\t";
        break;
      default:
        if (ch < 0x20) {
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec
                  << std::setfill(' ');
        } else {
          escaped << static_cast<char>(ch);
        }
        break;
    }
  }
  return escaped.str();
}

void indent(std::ostream& output, int spaces) {
  for (int index = 0; index < spaces; ++index) {
    output.put(' ');
  }
}

void quoted(std::ostream& output, std::string_view value) {
  output << '"' << json_escape(value) << '"';
}

void nullable_double(std::ostream& output, double value) {
  if (value < 0.0) {
    output << "null";
    return;
  }
  output << std::fixed << std::setprecision(3) << value;
}

[[nodiscard]] double throughput_per_second(ksj::base::u32 item_count, double duration) {
  if (item_count == 0 || duration <= 0.0) {
    return -1.0;
  }
  return static_cast<double>(item_count) * 1000.0 / duration;
}

[[nodiscard]] double average_final_image_interval_ms(const std::vector<ImageEvent>& final_images) {
  if (final_images.size() < 2) {
    return -1.0;
  }
  const double first = final_images.front().since_recon_start_ms;
  const double last = final_images.back().since_recon_start_ms;
  if (first < 0.0 || last < 0.0) {
    return -1.0;
  }
  return (last - first) / static_cast<double>(final_images.size() - 1);
}

[[nodiscard]] std::string_view metric_kind_name(MetricKind kind) noexcept {
  switch (kind) {
    case MetricKind::counter:
      return "counter";
    case MetricKind::duration_ns:
      return "duration_ns";
    case MetricKind::gauge:
      return "gauge";
  }
  return "unknown";
}

[[nodiscard]] std::string_view trace_event_kind_name(TraceEventKind kind) noexcept {
  switch (kind) {
    case TraceEventKind::mark:
      return "mark";
    case TraceEventKind::span_begin:
      return "span_begin";
    case TraceEventKind::span_end:
      return "span_end";
    case TraceEventKind::counter:
      return "counter";
  }
  return "unknown";
}

[[nodiscard]] MetricAggregate& metric_for_locked(std::vector<MetricAggregate>& metrics, std::string_view name,
                                                 MetricKind kind) {
  for (MetricAggregate& metric : metrics) {
    if (metric.name == name && metric.kind == kind) {
      return metric;
    }
  }
  MetricAggregate metric;
  metric.name = std::string(name);
  metric.kind = kind;
  metrics.push_back(std::move(metric));
  return metrics.back();
}

void record_metric_locked(Recorder& state, std::string_view name, MetricKind kind, std::uint64_t value) {
  if (name.empty()) {
    return;
  }
  MetricAggregate& metric = metric_for_locked(state.manual_metrics, name, kind);
  ++metric.updates;
  metric.last = value;
  if (kind == MetricKind::gauge) {
    metric.max = std::max(metric.max, value);
    return;
  }
  metric.total += value;
  metric.max = std::max(metric.max, value);
}

void append_trace_event_locked(Recorder& state, TraceEventKind kind, std::string_view name, double value = 0.0,
                               std::string_view component = {}, std::string_view stage = {}, std::string_view item = {},
                               std::string_view span_kind = {}) {
  if (name.empty()) {
    return;
  }

  TraceEvent event;
  event.wall_time_utc = utc_timestamp();
  event.kind = kind;
  event.name = std::string(name);
  event.value = value;
  event.component = std::string(component);
  event.stage = std::string(stage);
  event.item = std::string(item);
  event.span_kind = std::string(span_kind);

  const auto now = SteadyClock::now();
  if (state.has_active_scan && state.active_scan.has_scan_start) {
    event.since_scan_start_ms = duration_ms(state.active_scan.scan_start_steady, now);
  }
  if (state.has_active_scan && state.active_scan.has_recon_start) {
    event.since_recon_start_ms = duration_ms(state.active_scan.recon_start_steady, now);
  }

  state.trace_events.push_back(std::move(event));
}

[[nodiscard]] std::filesystem::path default_artifact_dir() {
  if (const char* performance_output_root = std::getenv("KSJ_PERFORMANCE_OUTPUT_ROOT");
      performance_output_root != nullptr && performance_output_root[0] != '\0') {
    return std::filesystem::path(performance_output_root);
  }
  if (const char* benchmark_output_root = std::getenv("KSJ_BENCHMARK_OUTPUT_ROOT");
      benchmark_output_root != nullptr && benchmark_output_root[0] != '\0') {
    return std::filesystem::path(benchmark_output_root) / "PerformanceAnalysis";
  }
  const std::string current_run_root = ksj::process_runtime::state_paths::current_run_output_root();
  if (!current_run_root.empty()) {
    return std::filesystem::path(current_run_root) / "PerformanceAnalysis";
  }
  return ksj::process_runtime::state_paths::resolve_relative_to_state_dir("performance_analysis");
}

void ensure_directory(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    KSJ_LOG_ERROR("[PERF] Failed to create directory [{}]: {}", path.string(), error.message());
  }
}

#if KSJ_PERFORMANCE_LEVEL >= 2
[[nodiscard]] std::filesystem::path build_cpu_profile_path(const Recorder& state, const ScanRecord& scan) {
  std::ostringstream file_name;
  file_name << "cpu_profile_" << sanitize_path_component(state.role_name) << '_' << state.pid << "_scan"
            << (state.completed_scan_count + 1);
  if (scan.scan_uid >= 0) {
    file_name << "_uid" << scan.scan_uid;
  }
  file_name << ".prof";
  return state.cpu_profiles_dir / file_name.str();
}
#endif

void stop_cpu_profiler_locked(Recorder& state, std::string_view reason) {
#if KSJ_PERFORMANCE_LEVEL >= 2
  if (!state.profiler_running) {
    return;
  }
  ProfilerStop();
  state.profiler_running = false;
  state.active_scan.cpu_profile_stop_time_utc = utc_timestamp();
  state.active_scan.cpu_profiler_stopped = true;
  state.active_scan.cpu_profile_stop_reason = std::string(reason);
#else
  (void)state;
  (void)reason;
#endif
}

void start_new_scan_locked(Recorder& state, SteadyClock::time_point now) {
  state.has_active_scan = true;
  state.active_scan = ScanRecord();
  state.active_scan.pid = state.pid;
  state.active_scan.process_role = state.role_name;
  state.active_scan.status = "in_progress";
  state.active_scan.scan_start_time_utc = utc_timestamp();
  state.active_scan.scan_start_steady = now;
  state.active_scan.has_scan_start = true;
  state.active_scan.ismrmrd_dataset_path = state.pending_ismrmrd_dataset_path;
  state.pending_ismrmrd_dataset_path.clear();
}

void ensure_active_scan_locked(Recorder& state, SteadyClock::time_point now) {
  if (!state.has_active_scan) {
    start_new_scan_locked(state, now);
  }
}

void ensure_recon_started_locked(Recorder& state, SteadyClock::time_point now) {
  ensure_active_scan_locked(state, now);
  if (state.active_scan.has_recon_start) {
    return;
  }

  state.active_scan.recon_start_time_utc = utc_timestamp();
  state.active_scan.recon_start_steady = now;
  state.active_scan.has_recon_start = true;
  state.active_scan.process_at_recon_start = capture_process_snapshot();

  if (!cpu_profiler_enabled()) {
    state.active_scan.cpu_profile_stop_reason = "cpu_profiler_requires_performance_level_2";
    return;
  }

#if KSJ_PERFORMANCE_LEVEL >= 2
  state.active_scan.cpu_profile_path = build_cpu_profile_path(state, state.active_scan).string();
  state.active_scan.cpu_profile_start_time_utc = utc_timestamp();
  state.active_scan.cpu_profiler_started = true;
  state.active_scan.cpu_profiler_start_success = ProfilerStart(state.active_scan.cpu_profile_path.c_str()) != 0;
  if (state.active_scan.cpu_profiler_start_success) {
    state.profiler_running = true;
  } else {
    state.active_scan.cpu_profile_stop_time_utc = utc_timestamp();
    state.active_scan.cpu_profile_stop_reason = "profiler_start_failed";
    state.active_scan.cpu_profiler_stopped = true;
    KSJ_LOG_ERROR("[PERF] Failed to start gperftools profiler [{}]", state.active_scan.cpu_profile_path);
  }
#endif
}

void finalize_locked(Recorder& state, std::string_view status, std::string_view reason) {
  if (!state.has_active_scan) {
    return;
  }

  if (!status.empty()) {
    state.active_scan.status = std::string(status);
  }
  if (!reason.empty()) {
    state.active_scan.completion_reason = std::string(reason);
  }
  stop_cpu_profiler_locked(state, status);
  if (state.active_scan.completed_scan_index == 0) {
    state.active_scan.completed_scan_index = ++state.completed_scan_count;
  }
  state.completed_scans.push_back(std::move(state.active_scan));
  state.active_scan = ScanRecord();
  state.has_active_scan = false;
}

void write_process_snapshot_json(std::ostream& output, const ProcessSnapshot& snapshot, int level) {
  indent(output, level);
  output << "{\n";
  indent(output, level + 2);
  output << "\"wall_time_utc\": ";
  quoted(output, snapshot.wall_time_utc);
  output << ",\n";
  indent(output, level + 2);
  output << "\"cpu_user_ms\": " << snapshot.user_ms << ",\n";
  indent(output, level + 2);
  output << "\"cpu_system_ms\": " << snapshot.system_ms << ",\n";
  indent(output, level + 2);
  output << "\"max_rss_kb\": " << snapshot.max_rss_kb << ",\n";
  indent(output, level + 2);
  output << "\"current_rss_kb\": " << snapshot.current_rss_kb << '\n';
  indent(output, level);
  output << "}";
}

void write_image_event_json(std::ostream& output, const ImageEvent& event, int level) {
  indent(output, level);
  output << "{\n";
  indent(output, level + 2);
  output << "\"image_index\": " << event.image_index << ",\n";
  indent(output, level + 2);
  output << "\"transfer_index\": " << event.transfer_index << ",\n";
  indent(output, level + 2);
  output << "\"expected_transfer_count\": " << event.expected_transfer_count << ",\n";
  indent(output, level + 2);
  output << "\"wall_time_utc\": ";
  quoted(output, event.wall_time_utc);
  output << ",\n";
  indent(output, level + 2);
  output << "\"since_scan_start_ms\": ";
  nullable_double(output, event.since_scan_start_ms);
  output << ",\n";
  indent(output, level + 2);
  output << "\"since_recon_start_ms\": ";
  nullable_double(output, event.since_recon_start_ms);
  output << ",\n";

  const auto write_field = [&](std::string_view name, ksj::base::u32 value) {
    indent(output, level + 2);
    quoted(output, name);
    output << ": " << value << ",\n";
  };
  write_field("frame_num", event.slice.frame_num);
  write_field("batch_num", event.slice.batch_num);
  write_field("volume_num", event.slice.volume_num);
  write_field("map_num", event.slice.map_num);
  write_field("echo_num", event.slice.echo_num);
  write_field("slice_num", event.slice.slice_num);
  write_field("phase_num", event.slice.phase_num);
  write_field("coil_num", event.slice.coil_num);
  write_field("image_size_bytes", event.slice.image_size_bytes);
  write_field("data_length", event.slice.data_length);
  write_field("data_length2", event.slice.data_length2);
  write_field("element_size", event.slice.element_size);
  write_field("image_type", event.slice.image_type);
  indent(output, level + 2);
  output << "\"is_last_slice\": " << (event.slice.is_last_slice ? "true" : "false") << '\n';
  indent(output, level);
  output << "}";
}

void write_metric_json(std::ostream& output, const MetricAggregate& metric, int level) {
  indent(output, level);
  output << "{\n";
  indent(output, level + 2);
  output << "\"name\": ";
  quoted(output, metric.name);
  output << ",\n";
  indent(output, level + 2);
  output << "\"kind\": ";
  quoted(output, metric_kind_name(metric.kind));
  output << ",\n";
  indent(output, level + 2);
  output << "\"updates\": " << metric.updates << ",\n";
  indent(output, level + 2);
  output << "\"total\": " << metric.total << ",\n";
  indent(output, level + 2);
  output << "\"max\": " << metric.max << ",\n";
  indent(output, level + 2);
  output << "\"last\": " << metric.last << '\n';
  indent(output, level);
  output << "}";
}

void write_trace_event_json(std::ostream& output, const TraceEvent& event, int level) {
  indent(output, level);
  output << "{\n";
  indent(output, level + 2);
  output << "\"wall_time_utc\": ";
  quoted(output, event.wall_time_utc);
  output << ",\n";
  indent(output, level + 2);
  output << "\"since_scan_start_ms\": ";
  nullable_double(output, event.since_scan_start_ms);
  output << ",\n";
  indent(output, level + 2);
  output << "\"since_recon_start_ms\": ";
  nullable_double(output, event.since_recon_start_ms);
  output << ",\n";
  indent(output, level + 2);
  output << "\"kind\": ";
  quoted(output, trace_event_kind_name(event.kind));
  output << ",\n";
  indent(output, level + 2);
  output << "\"name\": ";
  quoted(output, event.name);
  output << ",\n";
  indent(output, level + 2);
  output << "\"value\": " << std::fixed << std::setprecision(3) << event.value << ",\n";
  indent(output, level + 2);
  output << "\"component\": ";
  quoted(output, event.component);
  output << ",\n";
  indent(output, level + 2);
  output << "\"stage\": ";
  quoted(output, event.stage);
  output << ",\n";
  indent(output, level + 2);
  output << "\"item\": ";
  quoted(output, event.item);
  output << ",\n";
  indent(output, level + 2);
  output << "\"span_kind\": ";
  quoted(output, event.span_kind);
  output << '\n';
  indent(output, level);
  output << "}";
}

void write_scan_record_json(std::ostream& output, const ScanRecord& scan, int level) {
  indent(output, level);
  output << "{\n";
  const auto write_string = [&](std::string_view name, std::string_view value) {
    indent(output, level + 2);
    quoted(output, name);
    output << ": ";
    quoted(output, value);
    output << ",\n";
  };
  const auto write_u32 = [&](std::string_view name, ksj::base::u32 value) {
    indent(output, level + 2);
    quoted(output, name);
    output << ": " << value << ",\n";
  };
  const auto write_double = [&](std::string_view name, double value) {
    indent(output, level + 2);
    quoted(output, name);
    output << ": ";
    nullable_double(output, value);
    output << ",\n";
  };

  write_u32("completed_scan_index", scan.completed_scan_index);
  indent(output, level + 2);
  output << "\"scan_uid\": " << scan.scan_uid << ",\n";
  write_string("process_role", scan.process_role);
  indent(output, level + 2);
  output << "\"pid\": " << scan.pid << ",\n";
  write_string("status", scan.status);
  write_string("completion_reason", scan.completion_reason);
  write_string("provider_name", scan.provider_name);
  write_string("provider_version", scan.provider_version);
  write_string("ismrmrd_dataset_path", scan.ismrmrd_dataset_path);
  write_string("cpu_profile_path", scan.cpu_profile_path);
  write_string("cpu_profile_start_time_utc", scan.cpu_profile_start_time_utc);
  write_string("cpu_profile_stop_time_utc", scan.cpu_profile_stop_time_utc);
  write_string("cpu_profile_stop_reason", scan.cpu_profile_stop_reason);
  indent(output, level + 2);
  output << "\"cpu_profiler_started\": " << (scan.cpu_profiler_started ? "true" : "false") << ",\n";
  indent(output, level + 2);
  output << "\"cpu_profiler_stopped\": " << (scan.cpu_profiler_stopped ? "true" : "false") << ",\n";
  indent(output, level + 2);
  output << "\"cpu_profiler_start_success\": " << (scan.cpu_profiler_start_success ? "true" : "false") << ",\n";
  write_string("scan_start_time_utc", scan.scan_start_time_utc);
  write_string("recon_start_time_utc", scan.recon_start_time_utc);
  write_string("acquisition_end_time_utc", scan.acquisition_end_time_utc);
  write_string("first_final_image_time_utc", scan.first_final_image_time_utc);
  write_string("last_final_image_time_utc", scan.last_final_image_time_utc);
  write_string("recon_complete_time_utc", scan.recon_complete_time_utc);
  write_double("acquisition_duration_ms", scan.acquisition_duration_ms);
  write_double("time_to_first_final_image_ms", scan.time_to_first_final_image_ms);
  write_double("time_to_last_final_image_ms", scan.time_to_last_final_image_ms);
  write_double("average_final_image_interval_ms", average_final_image_interval_ms(scan.final_images));
  write_double("total_recon_ms", scan.total_recon_ms);
  write_double("scan_to_recon_complete_ms", scan.scan_to_recon_complete_ms);
  write_double("final_image_throughput_fps", throughput_per_second(scan.final_image_count, scan.total_recon_ms));
  write_double("total_output_throughput_ops", throughput_per_second(scan.total_sent_image_count, scan.total_recon_ms));
  write_u32("total_sent_image_count", scan.total_sent_image_count);
  write_u32("final_image_count", scan.final_image_count);
  write_u32("expected_transfer_count", scan.expected_transfer_count);
  indent(output, level + 2);
  output << "\"process_at_recon_start\": ";
  write_process_snapshot_json(output, scan.process_at_recon_start, level + 2);
  output << ",\n";
  indent(output, level + 2);
  output << "\"process_at_recon_complete\": ";
  write_process_snapshot_json(output, scan.process_at_recon_complete, level + 2);
  output << ",\n";
  indent(output, level + 2);
  output << "\"final_images\": [\n";
  for (std::size_t index = 0; index < scan.final_images.size(); ++index) {
    write_image_event_json(output, scan.final_images[index], level + 4);
    output << (index + 1 == scan.final_images.size() ? '\n' : ',');
  }
  indent(output, level + 2);
  output << "]\n";
  indent(output, level);
  output << "}";
}

void write_json_locked(const Recorder& state) {
  if (state.json_path.empty()) {
    return;
  }

  std::ofstream output(state.json_path, std::ios::out | std::ios::trunc);
  if (!output.is_open()) {
    KSJ_LOG_ERROR("[PERF] Failed to open JSON report [{}]", state.json_path.string());
    return;
  }

  output << "{\n";
  indent(output, 2);
  output << "\"generated_at_utc\": ";
  quoted(output, utc_timestamp());
  output << ",\n";
  indent(output, 2);
  output << "\"artifact_dir\": ";
  quoted(output, state.artifact_dir.string());
  output << ",\n";
  indent(output, 2);
  output << "\"json_path\": ";
  quoted(output, state.json_path.string());
  output << ",\n";
  indent(output, 2);
  output << "\"profiler_type\": ";
  quoted(output, cpu_profiler_enabled() ? "gperftools" : "disabled");
  output << ",\n";
  indent(output, 2);
  output << "\"cpu_profiles_dir\": ";
  quoted(output, state.cpu_profiles_dir.string());
  output << ",\n";
  indent(output, 2);
  output << "\"pprof_reports_dir\": ";
  quoted(output, state.pprof_reports_dir.string());
  output << ",\n";
  indent(output, 2);
  output << "\"process_role\": ";
  quoted(output, state.role_name);
  output << ",\n";
  indent(output, 2);
  output << "\"pid\": " << state.pid << ",\n";
  indent(output, 2);
  output << "\"completed_scan_count\": " << state.completed_scans.size() << ",\n";
  indent(output, 2);
  output << "\"active_scan_present\": " << (state.has_active_scan ? "true" : "false") << ",\n";
  indent(output, 2);
  output << "\"manual_metrics\": [\n";
  for (std::size_t index = 0; index < state.manual_metrics.size(); ++index) {
    write_metric_json(output, state.manual_metrics[index], 4);
    output << (index + 1 == state.manual_metrics.size() ? '\n' : ',');
  }
  indent(output, 2);
  output << "],\n";
  indent(output, 2);
  output << "\"trace_events\": [\n";
  for (std::size_t index = 0; index < state.trace_events.size(); ++index) {
    write_trace_event_json(output, state.trace_events[index], 4);
    output << (index + 1 == state.trace_events.size() ? '\n' : ',');
  }
  indent(output, 2);
  output << "],\n";
  indent(output, 2);
  output << "\"scans\": [\n";

  const std::size_t total_scans = state.completed_scans.size() + (state.has_active_scan ? 1U : 0U);
  std::size_t emitted = 0;
  for (const ScanRecord& scan : state.completed_scans) {
    write_scan_record_json(output, scan, 4);
    ++emitted;
    output << (emitted == total_scans ? '\n' : ',');
  }
  if (state.has_active_scan) {
    write_scan_record_json(output, state.active_scan, 4);
    output << '\n';
  }

  indent(output, 2);
  output << "]\n";
  output << "}\n";
}

void maybe_finalize_locked(Recorder& state) {
  if (state.has_active_scan && state.active_scan.has_acquisition_end && state.active_scan.has_recon_complete) {
    finalize_locked(state, "completed", "scan end and recon completion observed");
  }
}

void complete_active_reconstruction_locked(Recorder& state, std::string_view reason) {
  if (!state.has_active_scan) {
    return;
  }

  const std::string completion_reason =
    !reason.empty() ? std::string(reason)
                    : (state.active_scan.has_acquisition_end ? "reconstruction completed"
                                                             : "reconstruction completed before scan end");
  const std::string status = state.active_scan.final_image_count > 0 ? "completed" : "completed_without_final_images";
  finalize_locked(state, status, completion_reason);
}

void flush_at_process_exit() {
  flush_process_artifacts();
}

void ensure_initialized_locked(Recorder& state) {
  if (state.initialized) {
    return;
  }
  state.pid = current_process_id();
  state.artifact_dir = state.artifact_dir.empty() ? default_artifact_dir() : state.artifact_dir;
  if (cpu_profiler_enabled()) {
    state.cpu_profiles_dir = state.artifact_dir / "cpu_profiles";
    state.pprof_reports_dir = state.artifact_dir / "pprof_reports";
  } else {
    state.cpu_profiles_dir.clear();
    state.pprof_reports_dir.clear();
  }
  std::ostringstream json_name;
  json_name << "recon_perf_" << sanitize_path_component(state.role_name) << '_' << state.pid << ".json";
  state.json_path = state.artifact_dir / json_name.str();

  ensure_directory(state.artifact_dir);
  if (cpu_profiler_enabled()) {
    ensure_directory(state.cpu_profiles_dir);
    ensure_directory(state.pprof_reports_dir);
  }
  if (!state.exit_handler_registered) {
    std::atexit(flush_at_process_exit);
    state.exit_handler_registered = true;
  }
  state.initialized = true;
  if (cpu_profiler_enabled()) {
    KSJ_LOG_INFO("[PERF] JSON report=[{}], cpu_profiles_dir=[{}], pprof_reports_dir=[{}]", state.json_path.string(),
                 state.cpu_profiles_dir.string(), state.pprof_reports_dir.string());
  } else {
    KSJ_LOG_INFO("[PERF] JSON report=[{}], CPU profiler disabled at KSJ_PERFORMANCE_LEVEL={}", state.json_path.string(),
                 KSJ_PERFORMANCE_LEVEL);
  }
}

} // namespace

bool enabled() noexcept {
  return performance_enabled();
}

void counter_add(std::string_view name, std::uint64_t value) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    record_metric_locked(state, name, MetricKind::counter, value);
  } catch (...) {}
}

void gauge_set(std::string_view name, std::uint64_t value) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    record_metric_locked(state, name, MetricKind::gauge, value);
  } catch (...) {}
}

void duration_add(std::string_view name, std::chrono::nanoseconds value) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    record_metric_locked(state, name, MetricKind::duration_ns,
                         static_cast<std::uint64_t>(std::max<std::chrono::nanoseconds::rep>(value.count(), 0)));
  } catch (...) {}
}

void trace_mark(std::string_view name) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    append_trace_event_locked(state, TraceEventKind::mark, name);
  } catch (...) {}
}

void trace_counter(std::string_view name, double value) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    append_trace_event_locked(state, TraceEventKind::counter, name, value);
  } catch (...) {}
}

void trace_span_begin(std::string_view name, std::string_view component, std::string_view stage, std::string_view item,
                      std::string_view span_kind) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    append_trace_event_locked(state, TraceEventKind::span_begin, name, 0.0, component, stage, item, span_kind);
  } catch (...) {}
}

void trace_span_end(std::string_view name, std::string_view component, std::string_view stage, std::string_view item,
                    std::string_view span_kind) noexcept {
  if (!performance_enabled()) {
    return;
  }
  try {
    Recorder& state = recorder();
    std::lock_guard lock(state.mutex);
    ensure_initialized_locked(state);
    append_trace_event_locked(state, TraceEventKind::span_end, name, 0.0, component, stage, item, span_kind);
  } catch (...) {}
}

ScopedDuration::ScopedDuration(std::string_view name) noexcept
    : name_(name), started_(SteadyClock::now()), armed_(true) {}

ScopedDuration::~ScopedDuration() {
  if (!armed_) {
    return;
  }
  duration_add(name_, SteadyClock::now() - started_);
}

ScopedTrace::ScopedTrace(std::string_view name) noexcept : name_(name), armed_(true) {
  trace_span_begin(name_);
}

ScopedTrace::ScopedTrace(std::string_view name, std::string_view component, std::string_view stage,
                         std::string_view item, std::string_view span_kind) noexcept
    : name_(name), component_(component), stage_(stage), item_(item), span_kind_(span_kind), armed_(true) {
  trace_span_begin(name_, component_, stage_, item_, span_kind_);
}

ScopedTrace::~ScopedTrace() {
  if (!armed_) {
    return;
  }
  trace_span_end(name_, component_, stage_, item_, span_kind_);
}

void initialize_process_artifacts(ProcessArtifactOptions options) {
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  if (state.initialized) {
    return;
  }
  if (!options.role_name.empty()) {
    state.role_name = std::move(options.role_name);
  }
  if (!options.artifact_dir.empty()) {
    state.artifact_dir = std::move(options.artifact_dir);
  }
  ensure_initialized_locked(state);
  write_json_locked(state);
}

void on_scan_start() {
  const auto now = SteadyClock::now();
  detail::lightweight_scan_timing_on_scan_start(now);
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  if (state.has_active_scan) {
    finalize_locked(state,
                    state.active_scan.has_recon_complete ? "completed_without_scan_end" : "superseded_by_next_scan",
                    "new scan started before previous scan completed");
  }
  start_new_scan_locked(state, now);
  write_json_locked(state);
}

void on_reconstruction_started() {
  const auto now = SteadyClock::now();
  detail::lightweight_scan_timing_on_reconstruction_start(now);
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  ensure_recon_started_locked(state, now);
}

void on_scan_uid_resolved(int scan_uid) {
  detail::lightweight_scan_timing_on_scan_uid_resolved(scan_uid);
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  ensure_active_scan_locked(state, SteadyClock::now());
  state.active_scan.scan_uid = scan_uid;
  write_json_locked(state);
}

void on_provider_resolved(std::string_view provider_name, std::string_view provider_version) {
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  ensure_active_scan_locked(state, SteadyClock::now());
  state.active_scan.provider_name = std::string(provider_name);
  state.active_scan.provider_version = std::string(provider_version);
  write_json_locked(state);
}

void on_ismrmrd_dataset_resolved(std::string_view dataset_path) {
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  if (state.has_active_scan) {
    state.active_scan.ismrmrd_dataset_path = std::string(dataset_path);
  } else {
    state.pending_ismrmrd_dataset_path = std::string(dataset_path);
  }
  write_json_locked(state);
}

void on_slice_sent(int scan_uid, const SliceEventInfo& slice_info, ksj::base::u32 transfer_index,
                   ksj::base::u32 expected_transfer_count, bool is_final_image) {
  detail::lightweight_scan_timing_on_image_ready(scan_uid, slice_info, transfer_index, SteadyClock::now());
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  const auto now = SteadyClock::now();
  ensure_active_scan_locked(state, now);

  state.active_scan.scan_uid = scan_uid;
  ++state.active_scan.total_sent_image_count;
  state.active_scan.expected_transfer_count = expected_transfer_count;

  bool should_flush = slice_info.is_last_slice;
  if (is_final_image) {
    ImageEvent event;
    event.image_index = state.active_scan.final_image_count + 1;
    event.transfer_index = transfer_index;
    event.expected_transfer_count = expected_transfer_count;
    event.wall_time_utc = utc_timestamp();
    event.slice = slice_info;
    if (state.active_scan.has_scan_start) {
      event.since_scan_start_ms = duration_ms(state.active_scan.scan_start_steady, now);
    }
    if (state.active_scan.has_recon_start) {
      event.since_recon_start_ms = duration_ms(state.active_scan.recon_start_steady, now);
    }

    state.active_scan.final_images.push_back(event);
    ++state.active_scan.final_image_count;
    state.active_scan.last_final_image_time_utc = event.wall_time_utc;
    if (state.active_scan.first_final_image_time_utc.empty()) {
      state.active_scan.first_final_image_time_utc = event.wall_time_utc;
      state.active_scan.time_to_first_final_image_ms = event.since_recon_start_ms;
    }
    state.active_scan.time_to_last_final_image_ms = event.since_recon_start_ms;
    should_flush = true;
  }

  if (should_flush) {
    write_json_locked(state);
  }
}

void on_reconstruction_completed(std::string_view reason) {
  const auto now = SteadyClock::now();
  detail::lightweight_scan_timing_on_reconstruction_complete(-1, now);
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  if (!state.has_active_scan || state.active_scan.has_recon_complete) {
    return;
  }
  state.active_scan.recon_complete_time_utc = utc_timestamp();
  state.active_scan.recon_complete_steady = now;
  state.active_scan.has_recon_complete = true;
  state.active_scan.process_at_recon_complete = capture_process_snapshot();
  if (state.active_scan.has_recon_start) {
    state.active_scan.total_recon_ms =
      duration_ms(state.active_scan.recon_start_steady, state.active_scan.recon_complete_steady);
  }
  if (state.active_scan.has_scan_start) {
    state.active_scan.scan_to_recon_complete_ms =
      duration_ms(state.active_scan.scan_start_steady, state.active_scan.recon_complete_steady);
  }
  complete_active_reconstruction_locked(
    state, !reason.empty() ? reason
                           : (state.active_scan.final_image_count > 0
                                ? "last output slice observed"
                                : "last output slice observed but no final image matched the filter"));
  write_json_locked(state);
}

void on_reconstruction_failed(std::string_view reason) {
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  if (!state.has_active_scan) {
    return;
  }
  finalize_locked(state, "failed", reason.empty() ? "reconstruction failed" : reason);
  write_json_locked(state);
}

void on_reconstruction_stopped(std::string_view reason) {
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  if (!state.has_active_scan) {
    return;
  }
  finalize_locked(state, "stopped", reason.empty() ? "reconstruction stopped" : reason);
  write_json_locked(state);
}

void on_scan_end() {
  const auto now = SteadyClock::now();
  detail::lightweight_scan_timing_on_acquisition_end(now);
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  ensure_initialized_locked(state);
  if (!state.has_active_scan) {
    return;
  }

  state.active_scan.acquisition_end_time_utc = utc_timestamp();
  state.active_scan.acquisition_end_steady = now;
  state.active_scan.has_acquisition_end = true;
  if (state.active_scan.has_scan_start) {
    state.active_scan.acquisition_duration_ms =
      duration_ms(state.active_scan.scan_start_steady, state.active_scan.acquisition_end_steady);
  }

  if (!state.active_scan.has_recon_complete) {
    state.active_scan.status = "scan_ended_waiting_for_recon_completion";
    state.active_scan.completion_reason = "scan end received before final output slice";
    if (!state.active_scan.has_recon_start) {
      finalize_locked(state, "scan_end_without_processing", "scan end received before reconstruction started");
    }
  }
  maybe_finalize_locked(state);
  write_json_locked(state);
}

void flush_process_artifacts() {
  if (!performance_enabled()) {
    return;
  }
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  if (!state.initialized) {
    return;
  }
  if (state.has_active_scan) {
    finalize_locked(
      state, state.active_scan.has_recon_complete ? "process_exit_after_recon_complete" : "process_exit_during_scan",
      "process exited before scan completed");
  }
  write_json_locked(state);
}

std::filesystem::path artifact_dir() {
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  return state.artifact_dir;
}

std::filesystem::path cpu_profiles_dir() {
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  return state.cpu_profiles_dir;
}

std::filesystem::path pprof_reports_dir() {
  Recorder& state = recorder();
  std::lock_guard lock(state.mutex);
  return state.pprof_reports_dir;
}

} // namespace ksj::performance
