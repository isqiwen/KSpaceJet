#pragma once

#include "kspacejet/base/types.hpp"

#include <type_traits>

namespace ksj::linalg::detail {

template <typename T>
inline constexpr bool supported_linalg_scalar_v =
  std::is_same_v<std::remove_cv_t<T>, float> || std::is_same_v<std::remove_cv_t<T>, double> ||
  std::is_same_v<std::remove_cv_t<T>, ksj::base::cf32> || std::is_same_v<std::remove_cv_t<T>, ksj::base::cf64>;

template <typename T> constexpr void require_supported_linalg_scalar() {
  static_assert(supported_linalg_scalar_v<T>,
                "kspacejet-linalg backend operations support only float, double, ksj::base::cf32, and ksj::base::cf64");
}

} // namespace ksj::linalg::detail
