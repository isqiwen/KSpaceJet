#include "kspacejet/base/checksum/crc32c.hpp"

#include <array>
#include <cstdint>

namespace {
constexpr ksj::base::u32 kPolynomial = 0x82F63B78u;

constexpr ksj::base::u32 crc32c_table_entry(const ksj::base::u32 index) {
  ksj::base::u32 crc = index;
  for (int bit = 0; bit < 8; ++bit) {
    if ((crc & 1u) != 0u) {
      crc = (crc >> 1u) ^ kPolynomial;
    } else {
      crc >>= 1u;
    }
  }
  return crc;
}

constexpr std::array<ksj::base::u32, 256> make_crc32c_table() {
  std::array<ksj::base::u32, 256> table{};
  for (ksj::base::u32 i = 0; i < table.size(); ++i) {
    table[i] = crc32c_table_entry(i);
  }
  return table;
}

constexpr auto kTable = make_crc32c_table();
} // namespace

namespace ksj::base {

u32 compute_crc32c(const std::span<const std::byte> bytes) {
  u32 crc = 0xFFFF'FFFFu;
  for (const std::byte value : bytes) {
    const auto index = static_cast<std::uint8_t>(crc ^ static_cast<std::uint8_t>(value));
    crc = (crc >> 8u) ^ kTable[index];
  }
  return ~crc;
}

} // namespace ksj::base
