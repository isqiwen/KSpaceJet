#include "kspacejet/base/checksum/crc32c.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetBaseCrc32c, ComputesKnownCastagnoliVector) {
  constexpr std::string_view payload = "123456789";

  EXPECT_EQ(0xE3069283u, ksj::base::compute_crc32c(payload.data(), payload.size()));
}

TEST(KSpaceJetBaseCrc32c, EmptyPayloadIsZero) {
  constexpr std::array<std::byte, 0> payload{};

  EXPECT_EQ(0u, ksj::base::compute_crc32c(payload));
}

} // namespace
