#pragma once

#ifndef KSJ_ARRAY_RUNTIME_CHECKS
#if defined(NDEBUG)
#define KSJ_ARRAY_RUNTIME_CHECKS 0
#else
#define KSJ_ARRAY_RUNTIME_CHECKS 1
#endif
#endif

namespace ksj::array::detail {

inline constexpr bool runtime_checks_enabled =
#if KSJ_ARRAY_RUNTIME_CHECKS
  true;
#else
  false;
#endif

} // namespace ksj::array::detail
