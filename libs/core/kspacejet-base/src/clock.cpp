#include "kspacejet/base/clock.hpp"

#include <chrono>

namespace ksj::base::clock {

std::uint64_t monotonic_milliseconds() noexcept {
  const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

} // namespace ksj::base::clock
