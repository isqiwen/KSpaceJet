#pragma once

#include "kspacejet/base/types.hpp"

#include "kspacejet/array/array.hpp"

#include <type_traits>

namespace ksj::optimization {

enum class LeastSquaresMethod;

} // namespace ksj::optimization

namespace ksj::optimization::detail::eigen {

template <typename T>
inline constexpr bool least_squares_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

template <typename T> inline constexpr bool unsupported_least_squares_scalar_v = false;

void least_squares(ksj::array::MatrixView<const float> matrix, ksj::array::VectorView<const float> rhs,
                   ksj::array::VectorView<float> output, LeastSquaresMethod method);
void least_squares(ksj::array::MatrixView<const double> matrix, ksj::array::VectorView<const double> rhs,
                   ksj::array::VectorView<double> output, LeastSquaresMethod method);
template <typename T>
void least_squares(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                   ksj::array::VectorView<T> output, const LeastSquaresMethod method) {
  if constexpr (least_squares_scalar_v<T>) {
    least_squares(matrix, rhs, output, method);
  } else {
    (void)matrix;
    (void)rhs;
    (void)output;
    (void)method;
    static_assert(unsupported_least_squares_scalar_v<T>,
                  "ksj::optimization Eigen backend does not support this scalar type");
  }
}

} // namespace ksj::optimization::detail::eigen
