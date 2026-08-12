#pragma once

/// Dimension identifiers and shape helpers used by rank-aware array algorithms.

#include <array>
#include <cstddef>
#include <initializer_list>
#include <numeric>
#include <stdexcept>
#include <type_traits>

namespace ksj::array {

enum class Dim : std::size_t {
  dim0 = 0U,
  dim1 = 1U,
  dim2 = 2U,
  dim3 = 3U,
};

[[nodiscard]] constexpr std::size_t dim_index(const Dim dim) noexcept {
  return static_cast<std::size_t>(dim);
}

template <std::size_t Rank> struct Shape {
  static_assert(Rank > 0U, "Shape<Rank> requires Rank > 0");

  std::array<std::size_t, Rank> extents{};

  Shape() = default;

  template <typename... Extents>
    requires(sizeof...(Extents) == Rank && (std::is_convertible_v<Extents, std::size_t> && ...))
  constexpr explicit Shape(Extents... values) noexcept : extents{static_cast<std::size_t>(values)...} {}

  [[nodiscard]] static constexpr std::size_t rank() noexcept { return Rank; }

  [[nodiscard]] constexpr std::size_t operator[](const std::size_t dim) const { return extents.at(dim); }
  [[nodiscard]] constexpr std::size_t& operator[](const std::size_t dim) { return extents.at(dim); }

  [[nodiscard]] constexpr std::size_t extent(const Dim dim) const { return extents.at(dim_index(dim)); }

  [[nodiscard]] constexpr std::size_t element_count() const noexcept {
    std::size_t count = 1U;
    for (const auto extent_value : extents) {
      count *= extent_value;
    }
    return count;
  }
};

template <std::size_t Rank> struct Index {
  static_assert(Rank > 0U, "Index<Rank> requires Rank > 0");

  std::array<std::size_t, Rank> values{};

  Index() = default;

  template <typename... Values>
    requires(sizeof...(Values) == Rank && (std::is_convertible_v<Values, std::size_t> && ...))
  constexpr explicit Index(Values... indices) noexcept : values{static_cast<std::size_t>(indices)...} {}

  [[nodiscard]] static constexpr std::size_t rank() noexcept { return Rank; }

  [[nodiscard]] constexpr std::size_t operator[](const std::size_t dim) const { return values.at(dim); }
  [[nodiscard]] constexpr std::size_t& operator[](const std::size_t dim) { return values.at(dim); }

  [[nodiscard]] constexpr std::size_t value(const Dim dim) const { return values.at(dim_index(dim)); }
};

namespace detail {

inline void validate_supported_dim(const Dim dim, const Dim supported, const char* message) {
  if (dim != supported) {
    throw std::invalid_argument(message);
  }
}

inline void validate_rank_dim(const Dim dim, const std::size_t rank, const char* message) {
  if (dim_index(dim) >= rank) {
    throw std::out_of_range(message);
  }
}

} // namespace detail

} // namespace ksj::array
