#pragma once

#include "kspacejet/base/types.hpp"

#include <type_traits>

namespace ksj::stats::detail::eigen {

template <typename T>
inline constexpr bool supported_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double> ||
                                           std::is_same_v<T, ksj::base::cf32> || std::is_same_v<T, ksj::base::cf64>;

template <typename T>
inline constexpr bool supported_real_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

} // namespace ksj::stats::detail::eigen
