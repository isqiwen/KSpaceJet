#pragma once

#include "kspacejet/array/views.hpp"
#include "kspacejet/base/types.hpp"

namespace ksj::array::detail::intel::vml {

[[nodiscard]] bool add(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                       VectorView<ksj::base::f32> output);
[[nodiscard]] bool add(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                       VectorView<ksj::base::f64> output);

[[nodiscard]] bool subtract(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                            VectorView<ksj::base::f32> output);
[[nodiscard]] bool subtract(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                            VectorView<ksj::base::f64> output);

[[nodiscard]] bool multiply(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                            VectorView<ksj::base::f32> output);
[[nodiscard]] bool multiply(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                            VectorView<ksj::base::f64> output);

[[nodiscard]] bool divide(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                          VectorView<ksj::base::f32> output);
[[nodiscard]] bool divide(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                          VectorView<ksj::base::f64> output);

[[nodiscard]] bool absolute(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool absolute(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool sqrt(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool sqrt(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool inverse(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool inverse(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool inverse_sqrt(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool inverse_sqrt(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool exp(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool exp(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool log(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool log(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool minimum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                           VectorView<ksj::base::f32> output);
[[nodiscard]] bool minimum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                           VectorView<ksj::base::f64> output);

[[nodiscard]] bool maximum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                           VectorView<ksj::base::f32> output);
[[nodiscard]] bool maximum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                           VectorView<ksj::base::f64> output);

[[nodiscard]] bool hypot(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                         VectorView<ksj::base::f32> output);
[[nodiscard]] bool hypot(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                         VectorView<ksj::base::f64> output);

[[nodiscard]] bool absolute(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool absolute(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool complex_conjugate(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output);
[[nodiscard]] bool complex_conjugate(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output);

[[nodiscard]] bool multiply(VectorView<const ksj::base::cf32> lhs, VectorView<const ksj::base::cf32> rhs,
                            VectorView<ksj::base::cf32> output);
[[nodiscard]] bool multiply(VectorView<const ksj::base::cf64> lhs, VectorView<const ksj::base::cf64> rhs,
                            VectorView<ksj::base::cf64> output);

[[nodiscard]] bool divide(VectorView<const ksj::base::cf32> lhs, VectorView<const ksj::base::cf32> rhs,
                          VectorView<ksj::base::cf32> output);
[[nodiscard]] bool divide(VectorView<const ksj::base::cf64> lhs, VectorView<const ksj::base::cf64> rhs,
                          VectorView<ksj::base::cf64> output);

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool add(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool subtract(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool multiply(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool divide(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool absolute(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool sqrt(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool inverse(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool inverse_sqrt(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool exp(InputView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView> [[nodiscard]] bool log(InputView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool minimum(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool maximum(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename LhsView, typename RhsView, typename OutputView>
[[nodiscard]] bool hypot(LhsView, RhsView, OutputView) noexcept {
  return false;
}

template <typename InputView, typename OutputView>
[[nodiscard]] bool complex_conjugate(InputView, OutputView) noexcept {
  return false;
}

} // namespace ksj::array::detail::intel::vml
