#pragma once

/// Borrowed image-volume View helpers for algorithms operating on subvolumes without copies.

#include "kspacejet/array/slicing.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace ksj::image {

template <typename T> class VolumeView {
public:
  using value_type = T;

  constexpr VolumeView() noexcept = default;

  constexpr VolumeView(T* data, const std::size_t row_count, const std::size_t column_count,
                       const std::size_t slice_count, const std::size_t row_stride, const std::size_t column_stride,
                       const std::size_t slice_stride) noexcept
      : data_(data), rows_(row_count), columns_(column_count), slices_(slice_count), row_stride_(row_stride),
        column_stride_(column_stride), slice_stride_(slice_stride) {}

  template <typename U>
    requires(std::is_convertible_v<U*, T*>)
  constexpr VolumeView(const VolumeView<U>& other) noexcept
      : VolumeView(other.data(), other.rows(), other.columns(), other.slices(), other.row_stride(),
                   other.column_stride(), other.slice_stride()) {}

  [[nodiscard]] static constexpr VolumeView contiguous(T* data, const std::size_t row_count,
                                                       const std::size_t column_count,
                                                       const std::size_t slice_count) noexcept {
    return VolumeView(data, row_count, column_count, slice_count, column_count, 1U, row_count * column_count);
  }

  [[nodiscard]] constexpr T* data() const noexcept { return data_; }
  [[nodiscard]] constexpr std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] constexpr std::size_t columns() const noexcept { return columns_; }
  [[nodiscard]] constexpr std::size_t cols() const noexcept { return columns_; }
  [[nodiscard]] constexpr std::size_t slices() const noexcept { return slices_; }
  [[nodiscard]] constexpr std::size_t row_stride() const noexcept { return row_stride_; }
  [[nodiscard]] constexpr std::size_t column_stride() const noexcept { return column_stride_; }
  [[nodiscard]] constexpr std::size_t slice_stride() const noexcept { return slice_stride_; }
  [[nodiscard]] constexpr std::size_t slice_size() const noexcept { return rows_ * columns_; }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return slice_size() * slices_; }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return data_ == nullptr || rows_ == 0 || columns_ == 0 || slices_ == 0;
  }

  [[nodiscard]] constexpr T* slice_data(const std::size_t slice) const noexcept {
    return data_ + slice * slice_stride_;
  }

  [[nodiscard]] constexpr T& operator()(const std::size_t row, const std::size_t column,
                                        const std::size_t slice) const noexcept {
    return data_[(row * row_stride_) + (column * column_stride_) + (slice * slice_stride_)];
  }

  template <typename Rows, typename Columns, typename Slices>
  [[nodiscard]] VolumeView subview(Rows&& rows, Columns&& columns, Slices&& slices) const
    requires(ksj::array::detail::view_selector_v<Rows> && ksj::array::detail::view_selector_v<Columns> &&
             ksj::array::detail::view_selector_v<Slices> && !ksj::array::detail::fixed_selector_v<Rows> &&
             !ksj::array::detail::fixed_selector_v<Columns> && !ksj::array::detail::fixed_selector_v<Slices>)
  {
    const auto row_selection = ksj::array::detail::normalize_view_selector(
      std::forward<Rows>(rows), rows_, "volume view row subview is outside the source view");
    const auto column_selection = ksj::array::detail::normalize_view_selector(
      std::forward<Columns>(columns), columns_, "volume view column subview is outside the source view");
    const auto slice_selection = ksj::array::detail::normalize_view_selector(
      std::forward<Slices>(slices), slices_, "volume view slice subview is outside the source view");
    if (row_selection.count == 0U || column_selection.count == 0U || slice_selection.count == 0U) {
      return VolumeView(data_, 0U, 0U, 0U, row_stride_ * row_selection.step, column_stride_ * column_selection.step,
                        slice_stride_ * slice_selection.step);
    }
    return VolumeView(data_ + (row_selection.start * row_stride_) + (column_selection.start * column_stride_) +
                        (slice_selection.start * slice_stride_),
                      row_selection.count, column_selection.count, slice_selection.count,
                      row_stride_ * row_selection.step, column_stride_ * column_selection.step,
                      slice_stride_ * slice_selection.step);
  }

private:
  T* data_{};
  std::size_t rows_{};
  std::size_t columns_{};
  std::size_t slices_{};
  std::size_t row_stride_{};
  std::size_t column_stride_{};
  std::size_t slice_stride_{};
};

} // namespace ksj::image
