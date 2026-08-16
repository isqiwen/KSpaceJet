// SPDX-License-Identifier: Apache-2.0

#include "support/kspace_frame.hpp"

#include "kspacejet/provider/detail/provider_support.hpp"

#include <limits>

namespace ksj::kspace_conditioning::support {
namespace {

using ::ksj::provider::detail::checked_multiply;

} // namespace

bool checked_add(const std::uint64_t lhs, const std::uint64_t rhs, std::uint64_t& output) noexcept {
  if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
    return false;
  }
  output = lhs + rhs;
  return true;
}

bool kspace_frame_byte_count(const std::uint32_t channels, const std::uint32_t rows, const std::uint32_t cols,
                             std::uint64_t& byte_count) noexcept {
  std::uint64_t element_count = 0U;
  return checked_multiply(static_cast<std::uint64_t>(channels), static_cast<std::uint64_t>(rows), element_count) &&
         checked_multiply(element_count, static_cast<std::uint64_t>(cols), element_count) &&
         checked_multiply(element_count, kComplexFloat32Bytes, byte_count) && byte_count <= kMaximumKspaceBytes;
}

bool noncartesian_kspace_frame_byte_count(const std::uint32_t channels, const std::uint32_t samples,
                                          std::uint64_t& byte_count) noexcept {
  std::uint64_t element_count = 0U;
  return checked_multiply(static_cast<std::uint64_t>(channels), static_cast<std::uint64_t>(samples), element_count) &&
         checked_multiply(element_count, kComplexFloat32Bytes, byte_count) && byte_count <= kMaximumKspaceBytes;
}

bool complex_matrix_byte_count(const std::uint32_t rows, const std::uint32_t cols, std::uint64_t& byte_count) noexcept {
  std::uint64_t element_count = 0U;
  return checked_multiply(static_cast<std::uint64_t>(rows), static_cast<std::uint64_t>(cols), element_count) &&
         checked_multiply(element_count, kComplexFloat32Bytes, byte_count) && byte_count <= kMaximumKspaceBytes;
}

bool calibration_identity_matches_dynamic(const ksj_input_item_view& dynamic_item,
                                          const ksj_input_item_view& calibration_item) noexcept {
  return (calibration_item.semantic_key_hash == 0U ||
          calibration_item.semantic_key_hash == dynamic_item.semantic_key_hash) &&
         (calibration_item.order_key == 0U || calibration_item.order_key == dynamic_item.order_key);
}

} // namespace ksj::kspace_conditioning::support
