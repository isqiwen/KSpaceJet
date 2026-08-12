#pragma once

#include "kspacejet/base/types.hpp"

#include <cstddef>
#include <span>

namespace ksj::base {

// CRC-32C (Castagnoli) is an integrity checksum, not a general-purpose hash.
u32 compute_crc32c(std::span<const std::byte> bytes);

inline u32 compute_crc32c(const char* data, const std::size_t size) {
  return compute_crc32c(std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size));
}

} // namespace ksj::base
