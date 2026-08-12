#pragma once

#include <cstdint>

namespace ksj::base::clock {

[[nodiscard]] std::uint64_t monotonic_milliseconds() noexcept;

} // namespace ksj::base::clock
