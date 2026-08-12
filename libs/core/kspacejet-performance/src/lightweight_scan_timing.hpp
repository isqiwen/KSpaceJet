#pragma once

#include <chrono>

#include "kspacejet/performance/analysis.hpp"

namespace ksj::performance::detail {

void lightweight_scan_timing_on_scan_start(std::chrono::steady_clock::time_point now) noexcept;
void lightweight_scan_timing_on_scan_uid_resolved(int scan_uid) noexcept;
void lightweight_scan_timing_on_reconstruction_start(std::chrono::steady_clock::time_point now) noexcept;
void lightweight_scan_timing_on_acquisition_end(std::chrono::steady_clock::time_point now) noexcept;
void lightweight_scan_timing_on_reconstruction_complete(int scan_uid,
                                                        std::chrono::steady_clock::time_point now) noexcept;
void lightweight_scan_timing_on_image_ready(int scan_uid, const SliceEventInfo& slice_info,
                                            ksj::base::u32 transfer_index,
                                            std::chrono::steady_clock::time_point now) noexcept;

} // namespace ksj::performance::detail
