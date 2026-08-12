#pragma once

#include "kspacejet/base/types.hpp"

#include <type_traits>

namespace ksj::stats::detail::intel {

template <typename T> inline constexpr bool ipp_real_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

template <typename T>
inline constexpr bool ipp_magnitude_scalar_v =
  ipp_real_scalar_v<T> || std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;

} // namespace ksj::stats::detail::intel
