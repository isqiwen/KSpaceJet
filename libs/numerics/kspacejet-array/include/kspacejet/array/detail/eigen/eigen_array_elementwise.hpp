#pragma once

#include "kspacejet/array/views.hpp"
#include "kspacejet/base/types.hpp"

namespace ksj::array::detail::eigen {

#define KSJ_ARRAY_EIGEN_ELEMENTWISE_DECLS(T)                                                                           \
  [[nodiscard]] bool add(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output);                      \
  [[nodiscard]] bool subtract(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output);                 \
  [[nodiscard]] bool multiply(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output);                 \
  [[nodiscard]] bool multiply_accumulate(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output);      \
  [[nodiscard]] bool divide(VectorView<const T> lhs, VectorView<const T> rhs, VectorView<T> output);                   \
  [[nodiscard]] bool add_scalar(VectorView<const T> input, T scalar, VectorView<T> output);                            \
  [[nodiscard]] bool subtract_scalar(VectorView<const T> input, T scalar, VectorView<T> output);                       \
  [[nodiscard]] bool scalar_subtract(VectorView<const T> input, T scalar, VectorView<T> output);                       \
  [[nodiscard]] bool scale(VectorView<const T> input, T scalar, VectorView<T> output);                                 \
  [[nodiscard]] bool divide_scalar(VectorView<const T> input, T scalar, VectorView<T> output);                         \
  [[nodiscard]] bool scalar_divide(VectorView<const T> input, T scalar, VectorView<T> output);                         \
  [[nodiscard]] bool negate(VectorView<const T> input, VectorView<T> output);                                          \
  [[nodiscard]] bool square(VectorView<const T> input, VectorView<T> output);                                          \
  [[nodiscard]] bool sqrt(VectorView<const T> input, VectorView<T> output);                                            \
  [[nodiscard]] bool exp(VectorView<const T> input, VectorView<T> output);                                             \
  [[nodiscard]] bool log(VectorView<const T> input, VectorView<T> output)

KSJ_ARRAY_EIGEN_ELEMENTWISE_DECLS(ksj::base::f32);
KSJ_ARRAY_EIGEN_ELEMENTWISE_DECLS(ksj::base::f64);
KSJ_ARRAY_EIGEN_ELEMENTWISE_DECLS(ksj::base::cf32);
KSJ_ARRAY_EIGEN_ELEMENTWISE_DECLS(ksj::base::cf64);

#undef KSJ_ARRAY_EIGEN_ELEMENTWISE_DECLS

[[nodiscard]] bool absolute(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool absolute(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool absolute(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool absolute(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool inverse(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool inverse(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);
[[nodiscard]] bool inverse_sqrt(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output);
[[nodiscard]] bool inverse_sqrt(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output);

[[nodiscard]] bool minimum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                           VectorView<ksj::base::f32> output);
[[nodiscard]] bool minimum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                           VectorView<ksj::base::f64> output);
[[nodiscard]] bool maximum(VectorView<const ksj::base::f32> lhs, VectorView<const ksj::base::f32> rhs,
                           VectorView<ksj::base::f32> output);
[[nodiscard]] bool maximum(VectorView<const ksj::base::f64> lhs, VectorView<const ksj::base::f64> rhs,
                           VectorView<ksj::base::f64> output);

[[nodiscard]] bool clamp(VectorView<const ksj::base::f32> input, ksj::base::f32 lower, ksj::base::f32 upper,
                         VectorView<ksj::base::f32> output);
[[nodiscard]] bool clamp(VectorView<const ksj::base::f64> input, ksj::base::f64 lower, ksj::base::f64 upper,
                         VectorView<ksj::base::f64> output);

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool add(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool subtract(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool multiply(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool multiply_accumulate(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool divide(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Scalar, typename OutputT>
[[nodiscard]] bool add_scalar(VectorView<InputT>, const Scalar&, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Scalar, typename OutputT>
[[nodiscard]] bool subtract_scalar(VectorView<InputT>, const Scalar&, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Scalar, typename OutputT>
[[nodiscard]] bool scalar_subtract(VectorView<InputT>, const Scalar&, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Scalar, typename OutputT>
[[nodiscard]] bool scale(VectorView<InputT>, const Scalar&, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Scalar, typename OutputT>
[[nodiscard]] bool divide_scalar(VectorView<InputT>, const Scalar&, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Scalar, typename OutputT>
[[nodiscard]] bool scalar_divide(VectorView<InputT>, const Scalar&, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool negate(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool absolute(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool square(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool inverse(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT>
[[nodiscard]] bool inverse_sqrt(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT> [[nodiscard]] bool sqrt(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT> [[nodiscard]] bool exp(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename OutputT> [[nodiscard]] bool log(VectorView<InputT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool minimum(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename LhsT, typename RhsT, typename OutputT>
[[nodiscard]] bool maximum(VectorView<LhsT>, VectorView<RhsT>, VectorView<OutputT>) noexcept {
  return false;
}

template <typename InputT, typename Lower, typename Upper, typename OutputT>
[[nodiscard]] bool clamp(VectorView<InputT>, const Lower&, const Upper&, VectorView<OutputT>) noexcept {
  return false;
}

} // namespace ksj::array::detail::eigen
