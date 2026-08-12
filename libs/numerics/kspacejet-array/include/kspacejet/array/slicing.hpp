#pragma once

/// Slice descriptors and range validation for creating borrowed array subviews.

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

struct All {};

struct Slice {
  std::size_t start{};
  std::size_t stop{};
  std::size_t step{1U};
};

inline constexpr All _{};

[[nodiscard]] constexpr Slice slice(const std::size_t start, const std::size_t stop,
                                    const std::size_t step = 1U) noexcept {
  return {start, stop, step};
}

namespace detail {
template <typename T> using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

struct NormalizedSlice {
  std::size_t start{};
  std::size_t count{};
  std::size_t step{1U};
};

struct NormalizedIndex {
  std::size_t value{};
};

template <typename T>
inline constexpr bool integral_index_selector_v =
  std::is_integral_v<remove_cvref_t<T>> && !std::is_same_v<remove_cvref_t<T>, bool>;

template <typename T>
inline constexpr bool view_selector_v =
  std::is_same_v<remove_cvref_t<T>, All> || std::is_same_v<remove_cvref_t<T>, Slice> || integral_index_selector_v<T>;

template <typename T> inline constexpr bool fixed_selector_v = integral_index_selector_v<T>;

inline void validate_view_slice(const NormalizedSlice selection, const std::size_t extent, const char* message) {
  if (selection.step == 0U) {
    throw std::invalid_argument("view slice step must be positive");
  }
  if (selection.count == 0U) {
    if (selection.start > extent) {
      throw std::out_of_range(message);
    }
    return;
  }
  if (selection.start >= extent || selection.count - 1U > (extent - 1U - selection.start) / selection.step) {
    throw std::out_of_range(message);
  }
}

inline void validate_view_index(const NormalizedIndex index, const std::size_t extent, const char* message) {
  if (index.value >= extent) {
    throw std::out_of_range(message);
  }
}

inline NormalizedSlice full_slice(const std::size_t extent) noexcept {
  return {0U, extent, 1U};
}

inline NormalizedSlice normalize_slice(const Slice selection, const std::size_t extent, const char* message) {
  if (selection.step == 0U) {
    throw std::invalid_argument("view slice step must be positive");
  }
  if (selection.start > extent || selection.stop > extent) {
    throw std::out_of_range(message);
  }
  if (selection.start >= selection.stop) {
    return {selection.start, 0U, selection.step};
  }
  return {selection.start, (selection.stop - selection.start + selection.step - 1U) / selection.step, selection.step};
}

inline NormalizedSlice normalize_slice(const All, const std::size_t extent, const char*) noexcept {
  return full_slice(extent);
}

inline NormalizedSlice normalize_view_selector(const All selection, const std::size_t extent, const char* message) {
  return normalize_slice(selection, extent, message);
}

inline NormalizedSlice normalize_view_selector(const Slice selection, const std::size_t extent, const char* message) {
  auto result = normalize_slice(selection, extent, message);
  validate_view_slice(result, extent, message);
  return result;
}

template <typename T>
[[nodiscard]] NormalizedIndex normalize_index(const T value, const std::size_t extent, const char* message)
  requires(integral_index_selector_v<T>)
{
  if constexpr (std::is_signed_v<remove_cvref_t<T>>) {
    if (value < 0) {
      throw std::out_of_range(message);
    }
  }
  const auto result = NormalizedIndex{static_cast<std::size_t>(value)};
  validate_view_index(result, extent, message);
  return result;
}

template <typename T>
[[nodiscard]] decltype(auto) normalize_subview_selector(T&& selection, const std::size_t extent,
                                                        const char* slice_message, const char* index_message)
  requires(view_selector_v<T>)
{
  if constexpr (fixed_selector_v<T>) {
    return normalize_index(std::forward<T>(selection), extent, index_message);
  } else {
    return normalize_view_selector(std::forward<T>(selection), extent, slice_message);
  }
}
} // namespace detail

} // namespace ksj::array
