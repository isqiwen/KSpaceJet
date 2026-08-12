#pragma once

#include "kspacejet/array/views.hpp"
#include "kspacejet/base/types.hpp"

namespace ksj::array::detail::intel {

[[nodiscard]] bool sum(VectorView<const ksj::base::f32> input, ksj::base::f32& output);
[[nodiscard]] bool sum(VectorView<const ksj::base::f64> input, ksj::base::f64& output);
[[nodiscard]] bool min(VectorView<const ksj::base::f32> input, ksj::base::f32& output);
[[nodiscard]] bool min(VectorView<const ksj::base::f64> input, ksj::base::f64& output);
[[nodiscard]] bool max(VectorView<const ksj::base::f32> input, ksj::base::f32& output);
[[nodiscard]] bool max(VectorView<const ksj::base::f64> input, ksj::base::f64& output);

template <typename T, typename Output> [[nodiscard]] bool sum(VectorView<T>, Output&) noexcept {
  return false;
}

template <typename T, typename Output> [[nodiscard]] bool min(VectorView<T>, Output&) noexcept {
  return false;
}

template <typename T, typename Output> [[nodiscard]] bool max(VectorView<T>, Output&) noexcept {
  return false;
}

} // namespace ksj::array::detail::intel
