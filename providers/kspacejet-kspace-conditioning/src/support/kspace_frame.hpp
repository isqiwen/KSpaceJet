// SPDX-License-Identifier: Apache-2.0
//
// Shared k-space frame sizing and calibration identity support.

#pragma once

#include "kspacejet/base/types.hpp"
#include "kspacejet/provider/provider.h"

#include <cstdint>

namespace ksj::kspace_conditioning::support {

using Complex = ksj::base::cf32;

inline constexpr std::uint32_t kDynamicInputPort = 0U;
inline constexpr std::uint32_t kStaticInputPort = 1U;
inline constexpr std::uint32_t kOutputPort = 0U;
inline constexpr std::uint32_t kMaximumChannelCount = 64U;
inline constexpr std::uint32_t kMinimumNoncartesianSampleCount = 1U;
inline constexpr std::uint32_t kMaximumNoncartesianSampleCount = 65536U;
inline constexpr std::uint32_t kMinimumSpatialDimension = 2U;
inline constexpr std::uint32_t kMaximumSpatialDimension = 512U;
inline constexpr std::uint64_t kComplexFloat32Bytes = 2U * sizeof(float);
inline constexpr std::uint64_t kMaximumKspaceBytes = static_cast<std::uint64_t>(kMaximumChannelCount) *
                                                     kMaximumSpatialDimension * kMaximumSpatialDimension *
                                                     kComplexFloat32Bytes;
inline constexpr std::uint32_t kRequiredAlignment = 64U;

static_assert(sizeof(Complex) == kComplexFloat32Bytes,
              "The complex_float32 TypeRef requires two contiguous binary32 components.");

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& output) noexcept;
[[nodiscard]] bool kspace_frame_byte_count(std::uint32_t channels, std::uint32_t rows, std::uint32_t cols,
                                           std::uint64_t& byte_count) noexcept;
[[nodiscard]] bool noncartesian_kspace_frame_byte_count(std::uint32_t channels, std::uint32_t samples,
                                                        std::uint64_t& byte_count) noexcept;
[[nodiscard]] bool complex_matrix_byte_count(std::uint32_t rows, std::uint32_t cols,
                                             std::uint64_t& byte_count) noexcept;

// A calibration artifact may be unkeyed/static (both fields are zero). When
// it carries either inherited identity field, it must agree with the dynamic
// k-space item on that field before the Provider applies it.
[[nodiscard]] bool calibration_identity_matches_dynamic(const ksj_input_item_view& dynamic_item,
                                                        const ksj_input_item_view& calibration_item) noexcept;

} // namespace ksj::kspace_conditioning::support
