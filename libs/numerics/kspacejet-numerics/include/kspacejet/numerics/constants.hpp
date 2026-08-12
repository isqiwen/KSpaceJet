#pragma once

/// Mathematical constants and numerical limits exposed independently of any backend.

#include "kspacejet/base/types.hpp"

#include <limits>
#include <numbers>

namespace ksj::numerics {

template <typename T> inline constexpr T pi_v = std::numbers::pi_v<T>;

template <typename T> inline constexpr T epsilon_v = std::numeric_limits<T>::epsilon();

inline constexpr double pi = pi_v<double>;
inline constexpr double epsilon = epsilon_v<double>;
inline constexpr ksj::base::cf32 imaginary_unit_f{0.0f, 1.0f};
inline constexpr ksj::base::cf64 imaginary_unit_d{0.0, 1.0};

} // namespace ksj::numerics
