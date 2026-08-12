#pragma once

#include <type_traits>

namespace ksj::signal::detail::intel {

template <typename T> inline constexpr bool ipp_real_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

} // namespace ksj::signal::detail::intel
