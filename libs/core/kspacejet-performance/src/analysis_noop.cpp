#include "kspacejet/base/types.hpp"
#include "kspacejet/performance/analysis.hpp"
#include "lightweight_scan_timing.hpp"

namespace ksj::performance {

bool enabled() noexcept {
  return false;
}

void counter_add(std::string_view, std::uint64_t) noexcept {}

void gauge_set(std::string_view, std::uint64_t) noexcept {}

void duration_add(std::string_view, std::chrono::nanoseconds) noexcept {}

void trace_mark(std::string_view) noexcept {}

void trace_counter(std::string_view, double) noexcept {}

void trace_span_begin(std::string_view, std::string_view, std::string_view, std::string_view,
                      std::string_view) noexcept {}

void trace_span_end(std::string_view, std::string_view, std::string_view, std::string_view, std::string_view) noexcept {
}

ScopedDuration::ScopedDuration(std::string_view) noexcept {}

ScopedDuration::~ScopedDuration() = default;

ScopedTrace::ScopedTrace(std::string_view) noexcept {}

ScopedTrace::ScopedTrace(std::string_view, std::string_view, std::string_view, std::string_view,
                         std::string_view) noexcept {}

ScopedTrace::~ScopedTrace() = default;

void initialize_process_artifacts(ProcessArtifactOptions) {}

void on_scan_start() {
  detail::lightweight_scan_timing_on_scan_start(std::chrono::steady_clock::now());
}

void on_reconstruction_started() {
  detail::lightweight_scan_timing_on_reconstruction_start(std::chrono::steady_clock::now());
}

void on_scan_uid_resolved(const int scan_uid) {
  detail::lightweight_scan_timing_on_scan_uid_resolved(scan_uid);
}

void on_provider_resolved(std::string_view, std::string_view) {}

void on_ismrmrd_dataset_resolved(std::string_view) {}

void on_slice_sent(const int scan_uid, const SliceEventInfo& slice_info, const ksj::base::u32 transfer_index,
                   ksj::base::u32, bool) {
  detail::lightweight_scan_timing_on_image_ready(scan_uid, slice_info, transfer_index,
                                                 std::chrono::steady_clock::now());
}

void on_reconstruction_completed(std::string_view) {
  detail::lightweight_scan_timing_on_reconstruction_complete(-1, std::chrono::steady_clock::now());
}

void on_reconstruction_failed(std::string_view) {}

void on_reconstruction_stopped(std::string_view) {}

void on_scan_end() {
  detail::lightweight_scan_timing_on_acquisition_end(std::chrono::steady_clock::now());
}

void flush_process_artifacts() {}

std::filesystem::path artifact_dir() {
  return {};
}

std::filesystem::path cpu_profiles_dir() {
  return {};
}

std::filesystem::path pprof_reports_dir() {
  return {};
}

} // namespace ksj::performance
