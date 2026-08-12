#pragma once

/// Scalar type traits for real/complex value categories and their corresponding result types.

#include <complex>
#include <type_traits>

namespace ksj::array {

template <typename T> struct is_complex : std::false_type {};

template <typename T> struct is_complex<std::complex<T>> : std::true_type {};

template <typename T> inline constexpr bool is_complex_v = is_complex<std::remove_cvref_t<T>>::value;

template <typename T> struct real_scalar {
  using type = T;
};

template <typename T> struct real_scalar<std::complex<T>> {
  using type = T;
};

template <typename T> using real_scalar_t = typename real_scalar<std::remove_cvref_t<T>>::type;

template <typename T>
using reduction_result_t =
  std::conditional_t<std::is_integral_v<std::remove_cvref_t<T>>, double, std::remove_cvref_t<T>>;

template <typename T>
using magnitude_result_t = std::conditional_t<std::is_integral_v<std::remove_cvref_t<T>>, double, real_scalar_t<T>>;

} // namespace ksj::array
