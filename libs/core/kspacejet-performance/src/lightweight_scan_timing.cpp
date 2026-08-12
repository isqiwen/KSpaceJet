#include "lightweight_scan_timing.hpp"

#include "kspacejet/logging/logging.hpp"

#include <algorithm>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

namespace ksj::performance::detail {
namespace {

using SteadyClock = std::chrono::steady_clock;

struct ScanTimingState {
  struct ImageReadyEvent {
    ksj::base::u32 transfer_index{};
    ksj::base::u32 frame_num{};
    ksj::base::u32 batch_num{};
    ksj::base::u32 volume_num{};
    ksj::base::u32 map_num{};
    ksj::base::u32 echo_num{};
    ksj::base::u32 slice_num{};
    ksj::base::u32 phase_num{};
    ksj::base::u32 coil_num{};
    SteadyClock::time_point ready_time{};
    double scan_start_to_image_ready_ms{};
  };

  int scan_uid = -1;
  std::optional<SteadyClock::time_point> scan_start;
  std::optional<SteadyClock::time_point> reconstruction_start;
  std::optional<SteadyClock::time_point> acquisition_end;
  std::optional<SteadyClock::time_point> reconstruction_complete;
  std::vector<ImageReadyEvent> image_ready_events;
  bool summary_logged = false;
};

struct ScanTimingRecorder {
  std::mutex mutex;
  ScanTimingState state;
};

[[nodiscard]] ScanTimingRecorder& recorder() noexcept {
  static ScanTimingRecorder instance;
  return instance;
}

[[nodiscard]] double duration_ms(const SteadyClock::time_point start, const SteadyClock::time_point end) noexcept {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] double duration_seconds(const SteadyClock::time_point start, const SteadyClock::time_point end) noexcept {
  return std::chrono::duration<double>(end - start).count();
}

void log_summary_if_complete(ScanTimingState& state) noexcept {
  if (state.summary_logged || !state.scan_start.has_value() || !state.reconstruction_start.has_value() ||
      !state.acquisition_end.has_value() || !state.reconstruction_complete.has_value()) {
    return;
  }

  const ScanTimingState::ImageReadyEvent* final_image = nullptr;
  if (!state.image_ready_events.empty()) {
    final_image =
      &*std::max_element(state.image_ready_events.cbegin(), state.image_ready_events.cend(),
                         [](const ScanTimingState::ImageReadyEvent& lhs, const ScanTimingState::ImageReadyEvent& rhs) {
                           return lhs.ready_time < rhs.ready_time;
                         });
  }

  bool show_frame = false;
  bool show_batch = false;
  bool show_volume = false;
  bool show_map = false;
  bool show_echo = false;
  bool show_slice = false;
  bool show_phase = false;
  bool show_coil = false;
  if (!state.image_ready_events.empty()) {
    const auto coordinate_varies = [&]<typename Projection>(const Projection projection) {
      const auto first_value = projection(state.image_ready_events.front());
      return std::any_of(std::next(state.image_ready_events.cbegin()), state.image_ready_events.cend(),
                         [first_value, projection](const ScanTimingState::ImageReadyEvent& image) {
                           return projection(image) != first_value;
                         });
    };
    show_frame = coordinate_varies([](const auto& image) {
      return image.frame_num;
    });
    show_batch = coordinate_varies([](const auto& image) {
      return image.batch_num;
    });
    show_volume = coordinate_varies([](const auto& image) {
      return image.volume_num;
    });
    show_map = coordinate_varies([](const auto& image) {
      return image.map_num;
    });
    show_echo = coordinate_varies([](const auto& image) {
      return image.echo_num;
    });
    show_slice = coordinate_varies([](const auto& image) {
      return image.slice_num;
    });
    show_phase = coordinate_varies([](const auto& image) {
      return image.phase_num;
    });
    show_coil = coordinate_varies([](const auto& image) {
      return image.coil_num;
    });
  }

  enum class TimelineEventKind : std::uint8_t {
    acquisition_start,
    acquisition_end,
    image_ready,
    final_image_ready,
  };
  struct TimelineEvent {
    SteadyClock::time_point time;
    TimelineEventKind kind;
    const ScanTimingState::ImageReadyEvent* image = nullptr;
  };

  std::vector<TimelineEvent> events;
  events.reserve(state.image_ready_events.size() + 3U);
  events.push_back({.time = *state.reconstruction_start, .kind = TimelineEventKind::acquisition_start});
  events.push_back({.time = *state.acquisition_end, .kind = TimelineEventKind::acquisition_end});
  for (const ScanTimingState::ImageReadyEvent& image : state.image_ready_events) {
    events.push_back({.time = image.ready_time, .kind = TimelineEventKind::image_ready, .image = &image});
  }
  if (final_image == nullptr) {
    events.push_back({.time = *state.reconstruction_complete, .kind = TimelineEventKind::final_image_ready});
  }
  std::sort(events.begin(), events.end(), [](const TimelineEvent& lhs, const TimelineEvent& rhs) {
    if (lhs.time != rhs.time) {
      return lhs.time < rhs.time;
    }
    return lhs.kind < rhs.kind;
  });

  std::ostringstream summary;
  summary << "[PERF][SCAN-TIMING] ScanUID=" << state.scan_uid << '\n'
          << std::fixed << std::setprecision(3) << "  +" << std::setw(10) << 0.0 << " s  Scan start";
  for (const TimelineEvent& event : events) {
    summary << "\n  +" << std::setw(10) << duration_seconds(*state.scan_start, event.time) << " s  ";
    if (event.kind == TimelineEventKind::acquisition_start) {
      summary << "Acquisition start";
      continue;
    }
    if (event.kind == TimelineEventKind::acquisition_end) {
      summary << "Acquisition end";
      continue;
    }
    if (event.kind == TimelineEventKind::final_image_ready) {
      summary << "Final image ready";
      continue;
    }

    const ScanTimingState::ImageReadyEvent& image = *event.image;
    summary << "Image ready #" << image.transfer_index;
    {
      bool has_coordinates = false;
      const auto append_coordinate = [&](const char* const label, const ksj::base::u32 value) {
        summary << (has_coordinates ? " " : " [") << label << "=" << value;
        has_coordinates = true;
      };
      if (show_frame) {
        append_coordinate("f", image.frame_num);
      }
      if (show_batch) {
        append_coordinate("b", image.batch_num);
      }
      if (show_volume) {
        append_coordinate("v", image.volume_num);
      }
      if (show_map) {
        append_coordinate("m", image.map_num);
      }
      if (show_echo) {
        append_coordinate("e", image.echo_num);
      }
      if (show_slice) {
        append_coordinate("s", image.slice_num);
      }
      if (show_phase) {
        append_coordinate("p", image.phase_num);
      }
      if (show_coil) {
        append_coordinate("c", image.coil_num);
      }
      if (has_coordinates) {
        summary << "]";
      }
    }
  }

  KSJ_LOG_INFO("{}", summary.str());
  state.summary_logged = true;
}

} // namespace

void lightweight_scan_timing_on_scan_start(const SteadyClock::time_point now) noexcept {
  ScanTimingRecorder& timing_recorder = recorder();
  std::lock_guard lock(timing_recorder.mutex);
  timing_recorder.state = {};
  timing_recorder.state.scan_start = now;
}

void lightweight_scan_timing_on_scan_uid_resolved(const int scan_uid) noexcept {
  ScanTimingRecorder& timing_recorder = recorder();
  std::lock_guard lock(timing_recorder.mutex);
  if (!timing_recorder.state.scan_start.has_value()) {
    return;
  }
  timing_recorder.state.scan_uid = scan_uid;
}

void lightweight_scan_timing_on_reconstruction_start(const SteadyClock::time_point now) noexcept {
  ScanTimingRecorder& timing_recorder = recorder();
  std::lock_guard lock(timing_recorder.mutex);
  if (!timing_recorder.state.scan_start.has_value() || timing_recorder.state.reconstruction_start.has_value()) {
    return;
  }
  timing_recorder.state.reconstruction_start = now;
  log_summary_if_complete(timing_recorder.state);
}

void lightweight_scan_timing_on_acquisition_end(const SteadyClock::time_point now) noexcept {
  ScanTimingRecorder& timing_recorder = recorder();
  std::lock_guard lock(timing_recorder.mutex);
  if (!timing_recorder.state.scan_start.has_value() || timing_recorder.state.acquisition_end.has_value()) {
    return;
  }
  timing_recorder.state.acquisition_end = now;
  log_summary_if_complete(timing_recorder.state);
}

void lightweight_scan_timing_on_reconstruction_complete(const int scan_uid,
                                                        const SteadyClock::time_point now) noexcept {
  ScanTimingRecorder& timing_recorder = recorder();
  std::lock_guard lock(timing_recorder.mutex);
  if (!timing_recorder.state.scan_start.has_value() || timing_recorder.state.reconstruction_complete.has_value() ||
      (timing_recorder.state.scan_uid >= 0 && scan_uid >= 0 && timing_recorder.state.scan_uid != scan_uid)) {
    return;
  }
  if (timing_recorder.state.scan_uid < 0 && scan_uid >= 0) {
    timing_recorder.state.scan_uid = scan_uid;
  }
  timing_recorder.state.reconstruction_complete = now;
  log_summary_if_complete(timing_recorder.state);
}

void lightweight_scan_timing_on_image_ready(const int scan_uid, const SliceEventInfo& slice_info,
                                            const ksj::base::u32 transfer_index,
                                            const SteadyClock::time_point now) noexcept {
  ScanTimingRecorder& timing_recorder = recorder();
  std::lock_guard lock(timing_recorder.mutex);
  ScanTimingState& state = timing_recorder.state;
  if (!state.scan_start.has_value() || (scan_uid >= 0 && state.scan_uid >= 0 && scan_uid != state.scan_uid)) {
    return;
  }
  if (state.scan_uid < 0 && scan_uid >= 0) {
    state.scan_uid = scan_uid;
  }
  state.image_ready_events.push_back({
    .transfer_index = transfer_index,
    .frame_num = slice_info.frame_num,
    .batch_num = slice_info.batch_num,
    .volume_num = slice_info.volume_num,
    .map_num = slice_info.map_num,
    .echo_num = slice_info.echo_num,
    .slice_num = slice_info.slice_num,
    .phase_num = slice_info.phase_num,
    .coil_num = slice_info.coil_num,
    .ready_time = now,
    .scan_start_to_image_ready_ms = duration_ms(*state.scan_start, now),
  });
}

} // namespace ksj::performance::detail
