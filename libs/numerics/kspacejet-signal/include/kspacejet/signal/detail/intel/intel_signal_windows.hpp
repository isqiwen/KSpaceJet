#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/signal/detail/intel/intel_signal_types.hpp"
#include "kspacejet/signal/types.hpp"

namespace ksj::signal::detail::intel {

[[nodiscard]] bool window(ksj::array::VectorView<float> output, WindowKind kind);
[[nodiscard]] bool window(ksj::array::VectorView<double> output, WindowKind kind);

template <typename T> [[nodiscard]] bool window(ksj::array::VectorView<T> output, const WindowKind kind) {
  if constexpr (!ipp_real_scalar_v<T>) {
    (void)output;
    (void)kind;
    return false;
  } else {
    return window(output, kind);
  }
}

} // namespace ksj::signal::detail::intel
