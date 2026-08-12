#pragma once

#include <type_traits>

namespace ksj::fft::detail::intel {

template <typename T> inline constexpr bool dfti_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

} // namespace ksj::fft::detail::intel
