#pragma once

#include "kspacejet/array/views.hpp"
#include "kspacejet/base/types.hpp"

namespace ksj::array::detail::eigen {

[[nodiscard]] bool fill(VectorView<ksj::base::f32> output, ksj::base::f32 value);
[[nodiscard]] bool fill(VectorView<ksj::base::f64> output, ksj::base::f64 value);
[[nodiscard]] bool fill(VectorView<ksj::base::cf32> output, ksj::base::cf32 value);
[[nodiscard]] bool fill(VectorView<ksj::base::cf64> output, ksj::base::cf64 value);

[[nodiscard]] bool copy(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool copy(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool copy(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output);
[[nodiscard]] bool copy(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output);

template <typename T, typename Value> [[nodiscard]] bool fill(VectorView<T>, const Value&) noexcept {
  return false;
}

template <typename InputT, typename OutputT> [[nodiscard]] bool copy(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

} // namespace ksj::array::detail::eigen
