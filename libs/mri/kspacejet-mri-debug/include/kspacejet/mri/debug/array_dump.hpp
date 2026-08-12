#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/logging/logging.hpp"

#include <array>
#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ksj::mri::debug {

enum class ArrayMatFileVersion {
  mat5,
  mat73,
};

struct ArrayMatDumpOptions {
  std::filesystem::path directory{};
  bool force{false};
  bool append{false};
  bool compress{true};
  ArrayMatFileVersion file_version{ArrayMatFileVersion::mat73};
};

enum ArrayDumpResult : int {
  kArrayDumpOk = 0,
  kArrayDumpInvalidArgument = 1,
  kArrayDumpCreateFileFailure = 2,
  kArrayDumpCreateVariableFailure = 3,
  kArrayDumpWriteFailure = 4,
};

[[nodiscard]] bool array_dump_enabled(std::string_view name);

namespace detail {

template <typename T> struct is_std_complex : std::false_type {};

template <typename T> struct is_std_complex<std::complex<T>> : std::true_type {};

template <typename T> inline constexpr bool is_std_complex_v = is_std_complex<std::remove_cv_t<T>>::value;

template <typename T> inline constexpr bool always_false_v = false;

template <typename T> using mat_value_t = std::remove_cv_t<T>;

enum class MatScalarKind {
  f32,
  f64,
  i8,
  u8,
  i16,
  u16,
  i32,
  u32,
  i64,
  u64,
};

using MatScalarStorage =
  std::variant<std::vector<float>, std::vector<double>, std::vector<std::int8_t>, std::vector<std::uint8_t>,
               std::vector<std::int16_t>, std::vector<std::uint16_t>, std::vector<std::int32_t>,
               std::vector<std::uint32_t>, std::vector<std::int64_t>, std::vector<std::uint64_t>>;

struct ArrayMatPayload {
  std::string variable_name;
  std::vector<std::size_t> dimensions;
  MatScalarKind scalar_kind{MatScalarKind::f32};
  bool complex{false};
  MatScalarStorage real_values;
  MatScalarStorage imag_values;
};

template <typename T> [[nodiscard]] constexpr MatScalarKind mat_scalar_kind() {
  using value_type = mat_value_t<T>;
  if constexpr (std::same_as<value_type, float>) {
    return MatScalarKind::f32;
  } else if constexpr (std::same_as<value_type, double>) {
    return MatScalarKind::f64;
  } else if constexpr (std::same_as<value_type, std::int8_t>) {
    return MatScalarKind::i8;
  } else if constexpr (std::same_as<value_type, std::uint8_t>) {
    return MatScalarKind::u8;
  } else if constexpr (std::same_as<value_type, std::int16_t>) {
    return MatScalarKind::i16;
  } else if constexpr (std::same_as<value_type, std::uint16_t>) {
    return MatScalarKind::u16;
  } else if constexpr (std::same_as<value_type, std::int32_t>) {
    return MatScalarKind::i32;
  } else if constexpr (std::same_as<value_type, std::uint32_t>) {
    return MatScalarKind::u32;
  } else if constexpr (std::same_as<value_type, std::int64_t>) {
    return MatScalarKind::i64;
  } else if constexpr (std::same_as<value_type, std::uint64_t>) {
    return MatScalarKind::u64;
  } else {
    static_assert(always_false_v<value_type>, "unsupported array dump scalar type");
  }
}

template <typename T> struct mat_value_traits {
  using value_type = mat_value_t<T>;
  using scalar_type = value_type;
  static constexpr bool is_complex = false;
};

template <typename T> struct mat_value_traits<std::complex<T>> {
  using value_type = std::complex<T>;
  using scalar_type = mat_value_t<T>;
  static constexpr bool is_complex = true;
};

template <typename T> struct array_dump_traits;

template <typename T> struct array_dump_traits<ksj::array::VectorView<T>> {
  using value_type = typename ksj::array::VectorView<T>::value_type;
  static constexpr std::size_t rank = 1;

  [[nodiscard]] static std::array<std::size_t, rank> extents(ksj::array::VectorView<T> value) noexcept {
    return {value.size()};
  }

  [[nodiscard]] static std::size_t size(ksj::array::VectorView<T> value) noexcept { return value.size(); }

  [[nodiscard]] static decltype(auto) at(ksj::array::VectorView<T> value, const std::array<std::size_t, rank>& index) {
    return value(index[0]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::MatrixView<T>> {
  using value_type = typename ksj::array::MatrixView<T>::value_type;
  static constexpr std::size_t rank = 2;

  [[nodiscard]] static std::array<std::size_t, rank> extents(ksj::array::MatrixView<T> value) noexcept {
    return {value.rows(), value.cols()};
  }

  [[nodiscard]] static std::size_t size(ksj::array::MatrixView<T> value) noexcept { return value.size(); }

  [[nodiscard]] static decltype(auto) at(ksj::array::MatrixView<T> value, const std::array<std::size_t, rank>& index) {
    return value(index[0], index[1]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::ImageView<T>> {
  using value_type = typename ksj::array::ImageView<T>::value_type;
  static constexpr std::size_t rank = 2;

  [[nodiscard]] static std::array<std::size_t, rank> extents(ksj::array::ImageView<T> value) noexcept {
    return {value.rows(), value.cols()};
  }

  [[nodiscard]] static std::size_t size(ksj::array::ImageView<T> value) noexcept { return value.size(); }

  [[nodiscard]] static decltype(auto) at(ksj::array::ImageView<T> value, const std::array<std::size_t, rank>& index) {
    return value(index[0], index[1]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::CubeView<T>> {
  using value_type = typename ksj::array::CubeView<T>::value_type;
  static constexpr std::size_t rank = 3;

  [[nodiscard]] static std::array<std::size_t, rank> extents(ksj::array::CubeView<T> value) noexcept {
    return {value.dim0(), value.dim1(), value.dim2()};
  }

  [[nodiscard]] static std::size_t size(ksj::array::CubeView<T> value) noexcept { return value.size(); }

  [[nodiscard]] static decltype(auto) at(ksj::array::CubeView<T> value, const std::array<std::size_t, rank>& index) {
    return value(index[0], index[1], index[2]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::Array4DView<T>> {
  using value_type = typename ksj::array::Array4DView<T>::value_type;
  static constexpr std::size_t rank = 4;

  [[nodiscard]] static std::array<std::size_t, rank> extents(ksj::array::Array4DView<T> value) noexcept {
    return {value.dim0(), value.dim1(), value.dim2(), value.dim3()};
  }

  [[nodiscard]] static std::size_t size(ksj::array::Array4DView<T> value) noexcept { return value.size(); }

  [[nodiscard]] static decltype(auto) at(ksj::array::Array4DView<T> value, const std::array<std::size_t, rank>& index) {
    return value(index[0], index[1], index[2], index[3]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::PooledVector<T>> {
  using value_type = T;
  static constexpr std::size_t rank = 1;

  [[nodiscard]] static std::array<std::size_t, rank> extents(const ksj::array::PooledVector<T>& value) noexcept {
    return {value.size()};
  }

  [[nodiscard]] static std::size_t size(const ksj::array::PooledVector<T>& value) noexcept { return value.size(); }

  [[nodiscard]] static const T& at(const ksj::array::PooledVector<T>& value,
                                   const std::array<std::size_t, rank>& index) noexcept {
    return value(index[0]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::PooledMatrix<T>> {
  using value_type = T;
  static constexpr std::size_t rank = 2;

  [[nodiscard]] static std::array<std::size_t, rank> extents(const ksj::array::PooledMatrix<T>& value) noexcept {
    return {value.rows(), value.cols()};
  }

  [[nodiscard]] static std::size_t size(const ksj::array::PooledMatrix<T>& value) noexcept { return value.size(); }

  [[nodiscard]] static const T& at(const ksj::array::PooledMatrix<T>& value,
                                   const std::array<std::size_t, rank>& index) noexcept {
    return value(index[0], index[1]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::PooledImage<T>> {
  using value_type = T;
  static constexpr std::size_t rank = 2;

  [[nodiscard]] static std::array<std::size_t, rank> extents(const ksj::array::PooledImage<T>& value) noexcept {
    return {value.height(), value.width()};
  }

  [[nodiscard]] static std::size_t size(const ksj::array::PooledImage<T>& value) noexcept { return value.size(); }

  [[nodiscard]] static const T& at(const ksj::array::PooledImage<T>& value,
                                   const std::array<std::size_t, rank>& index) noexcept {
    return value(index[0], index[1]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::PooledCube<T>> {
  using value_type = T;
  static constexpr std::size_t rank = 3;

  [[nodiscard]] static std::array<std::size_t, rank> extents(const ksj::array::PooledCube<T>& value) noexcept {
    return {value.dim0(), value.dim1(), value.dim2()};
  }

  [[nodiscard]] static std::size_t size(const ksj::array::PooledCube<T>& value) noexcept { return value.size(); }

  [[nodiscard]] static const T& at(const ksj::array::PooledCube<T>& value,
                                   const std::array<std::size_t, rank>& index) noexcept {
    return value(index[0], index[1], index[2]);
  }
};

template <typename T> struct array_dump_traits<ksj::array::PooledArray4D<T>> {
  using value_type = T;
  static constexpr std::size_t rank = 4;

  [[nodiscard]] static std::array<std::size_t, rank> extents(const ksj::array::PooledArray4D<T>& value) noexcept {
    return {value.dim0(), value.dim1(), value.dim2(), value.dim3()};
  }

  [[nodiscard]] static std::size_t size(const ksj::array::PooledArray4D<T>& value) noexcept { return value.size(); }

  [[nodiscard]] static const T& at(const ksj::array::PooledArray4D<T>& value,
                                   const std::array<std::size_t, rank>& index) noexcept {
    return value(index[0], index[1], index[2], index[3]);
  }
};

template <typename T> using array_dump_traits_t = array_dump_traits<std::remove_cvref_t<T>>;

template <typename T>
concept DumpableArrayExpression = requires(const std::remove_cvref_t<T>& value) {
  typename array_dump_traits_t<T>::value_type;
  { array_dump_traits_t<T>::rank } -> std::convertible_to<std::size_t>;
  { array_dump_traits_t<T>::extents(value) };
  { array_dump_traits_t<T>::size(value) } -> std::convertible_to<std::size_t>;
};

template <DumpableArrayExpression Expression>
[[nodiscard]] std::array<std::size_t, array_dump_traits_t<Expression>::rank>
array_extents(const Expression& expression) {
  return array_dump_traits_t<Expression>::extents(expression);
}

template <std::size_t Rank>
[[nodiscard]] std::size_t array_element_count(const std::array<std::size_t, Rank>& extents) noexcept {
  std::size_t count = 1;
  for (const auto extent : extents) {
    count *= extent;
  }
  return count;
}

template <DumpableArrayExpression Expression>
void append_values_in_matlab_order(const Expression& expression,
                                   const std::array<std::size_t, array_dump_traits_t<Expression>::rank>& extents,
                                   std::array<std::size_t, array_dump_traits_t<Expression>::rank>& index,
                                   std::size_t remaining_rank,
                                   std::vector<typename array_dump_traits_t<Expression>::value_type>& values) {
  if (remaining_rank == 0) {
    values.push_back(static_cast<typename array_dump_traits_t<Expression>::value_type>(
      array_dump_traits_t<Expression>::at(expression, index)));
    return;
  }

  const auto dimension = remaining_rank - 1;
  for (std::size_t value = 0; value < extents[dimension]; ++value) {
    index[dimension] = value;
    append_values_in_matlab_order(expression, extents, index, dimension, values);
  }
  index[dimension] = 0;
}

template <DumpableArrayExpression Expression>
[[nodiscard]] std::vector<typename array_dump_traits_t<Expression>::value_type>
materialize_values_in_matlab_order(const Expression& expression) {
  std::vector<typename array_dump_traits_t<Expression>::value_type> values;
  const auto extents = array_extents(expression);
  const auto size = array_dump_traits_t<Expression>::size(expression);
  values.reserve(size);
  if (size == 0) {
    return values;
  }

  std::array<std::size_t, array_dump_traits_t<Expression>::rank> index{};
  append_values_in_matlab_order(expression, extents, index, array_dump_traits_t<Expression>::rank, values);
  return values;
}

template <std::size_t Rank>
[[nodiscard]] std::vector<std::size_t> mat_dimensions(const std::array<std::size_t, Rank>& extents) {
  static_assert(Rank > 0);

  if constexpr (Rank == 1) {
    return {extents[0], 1};
  } else {
    return {extents.begin(), extents.end()};
  }
}

template <DumpableArrayExpression Expression>
[[nodiscard]] ArrayMatPayload make_array_mat_payload(const Expression& expression, std::string_view variable_name) {
  using value_type = typename array_dump_traits_t<Expression>::value_type;
  using traits = mat_value_traits<value_type>;
  using scalar_type = typename traits::scalar_type;

  ArrayMatPayload payload;
  payload.variable_name = variable_name.empty() ? "array" : std::string{variable_name};
  payload.dimensions = mat_dimensions(array_extents(expression));
  payload.scalar_kind = mat_scalar_kind<scalar_type>();
  payload.complex = traits::is_complex;

  auto values = materialize_values_in_matlab_order(expression);
  if constexpr (traits::is_complex) {
    std::vector<scalar_type> real(values.size());
    std::vector<scalar_type> imag(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
      real[index] = values[index].real();
      imag[index] = values[index].imag();
    }
    payload.real_values = std::move(real);
    payload.imag_values = std::move(imag);
  } else {
    payload.real_values = std::move(values);
    payload.imag_values = std::vector<scalar_type>{};
  }
  return payload;
}

[[nodiscard]] int write_mat_array_payload(const ArrayMatPayload& payload, std::string_view file_prefix,
                                          const ArrayMatDumpOptions& options);

} // namespace detail

template <typename Expression>
  requires detail::DumpableArrayExpression<Expression>
[[nodiscard]] int dump_mat_array(const Expression& expression, std::string_view file_prefix,
                                 std::string_view variable_name, const ArrayMatDumpOptions& options = {}) {
  const auto effective_name = file_prefix.empty() ? variable_name : file_prefix;
  if (!options.force && !array_dump_enabled(effective_name)) {
    return kArrayDumpOk;
  }

  const auto extents = detail::array_extents(expression);
  if (detail::array_dump_traits_t<Expression>::size(expression) != detail::array_element_count(extents)) {
    KSJ_LOG_ERROR("dump_mat_array failed: expression size does not match extents.");
    return kArrayDumpInvalidArgument;
  }

  const auto payload = detail::make_array_mat_payload(expression, variable_name);
  return detail::write_mat_array_payload(payload, effective_name.empty() ? "array" : effective_name, options);
}

} // namespace ksj::mri::debug
