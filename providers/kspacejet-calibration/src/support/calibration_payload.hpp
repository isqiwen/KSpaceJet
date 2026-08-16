// SPDX-License-Identifier: Apache-2.0
//
// Shared calibration payload sizing and complex-value support.

#pragma once

#include "kspacejet/base/types.hpp"

#include <cstddef>
#include <cstdint>

namespace ksj::calibration::support {

using Complex = ksj::base::cf32;

inline constexpr std::uint32_t kInputPort = 0U;
inline constexpr std::uint32_t kOutputPort = 0U;
inline constexpr std::uint32_t kMaximumChannelCount = 64U;
inline constexpr std::uint32_t kMaximumReadoutSampleCount = 4096U;
inline constexpr std::uint64_t kComplexFloat32Bytes = 2U * sizeof(float);
inline constexpr std::uint64_t kMaximumInputBytes = 64U * 1024U * 1024U;
inline constexpr std::uint64_t kMaximumCalibrationOutputBytes =
  static_cast<std::uint64_t>(kMaximumChannelCount) * kMaximumReadoutSampleCount * kComplexFloat32Bytes;
inline constexpr std::uint32_t kRequiredAlignment = 64U;

[[nodiscard]] Complex read_complex_float32(const void* data, std::size_t element_index) noexcept;
void write_complex_float32(void* data, std::size_t element_index, Complex value) noexcept;

} // namespace ksj::calibration::support
