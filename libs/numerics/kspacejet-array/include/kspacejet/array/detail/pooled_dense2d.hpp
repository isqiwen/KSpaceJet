#pragma once

#include "kspacejet/array/detail/storage_traits.hpp"
#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/pooled_buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ksj::array::detail {

template <typename T> class PooledDense2D {
public:
  static_assert(pooled_storage_scalar_v<T>, "PooledDense2D<T> requires a non-const trivially destructible scalar type");

  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;

  PooledDense2D() = default;

  PooledDense2D(std::size_t rows, std::size_t cols, ksj::memory::AllocationProperties properties = {})
      : buffer_(ksj::memory::allocate_array<T>(checked_count(rows, cols), std::move(properties))), rows_(rows),
        cols_(cols) {}

  PooledDense2D(const PooledDense2D&) = delete;
  PooledDense2D& operator=(const PooledDense2D&) = delete;
  PooledDense2D(PooledDense2D&&) noexcept = default;
  PooledDense2D& operator=(PooledDense2D&&) noexcept = default;

  void swap(PooledDense2D& other) noexcept {
    using std::swap;
    swap(buffer_, other.buffer_);
    swap(rows_, other.rows_);
    swap(cols_, other.cols_);
  }

  [[nodiscard]] T* data() noexcept { return buffer_.data(); }
  [[nodiscard]] const T* data() const noexcept { return buffer_.data(); }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t size() const noexcept { return rows_ * cols_; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size() * sizeof(T); }
  [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.capacity(); }
  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return buffer_.capacity_bytes(); }
  [[nodiscard]] bool empty() const noexcept { return rows_ == 0 || cols_ == 0; }
  [[nodiscard]] bool is_contiguous() const noexcept { return true; }
  [[nodiscard]] std::size_t row_stride() const noexcept { return cols_; }
  [[nodiscard]] std::size_t col_stride() const noexcept { return 1U; }
  [[nodiscard]] Shape<2U> shape() const noexcept { return Shape<2U>(rows_, cols_); }
  [[nodiscard]] std::size_t extent(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return rows_;
      case 1:
        return cols_;
      default:
        throw std::out_of_range("PooledDense2D extent axis must be 0 or 1");
    }
  }
  [[nodiscard]] std::size_t extent(const Dim dim) const { return extent(dim_index(dim)); }
  [[nodiscard]] std::size_t stride(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return row_stride();
      case 1:
        return col_stride();
      default:
        throw std::out_of_range("PooledDense2D stride axis must be 0 or 1");
    }
  }
  [[nodiscard]] std::size_t stride(const Dim dim) const { return stride(dim_index(dim)); }

  [[nodiscard]] reference operator[](std::size_t index) noexcept { return data()[index]; }
  [[nodiscard]] const_reference operator[](std::size_t index) const noexcept { return data()[index]; }
  [[nodiscard]] reference operator()(std::size_t row, std::size_t col) {
    if (row >= rows_ || col >= cols_) {
      throw std::out_of_range("PooledDense2D index is outside the matrix");
    }
    return (*this)[row * cols_ + col];
  }

  [[nodiscard]] const_reference operator()(std::size_t row, std::size_t col) const {
    if (row >= rows_ || col >= cols_) {
      throw std::out_of_range("PooledDense2D index is outside the matrix");
    }
    return (*this)[row * cols_ + col];
  }
  [[nodiscard]] reference front() noexcept { return (*this)[0U]; }
  [[nodiscard]] const_reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() noexcept { return (*this)[size() - 1U]; }
  [[nodiscard]] const_reference back() const noexcept { return (*this)[size() - 1U]; }

  [[nodiscard]] pointer begin() noexcept { return data(); }
  [[nodiscard]] const_pointer begin() const noexcept { return data(); }
  [[nodiscard]] const_pointer cbegin() const noexcept { return data(); }
  [[nodiscard]] pointer end() noexcept { return empty() ? data() : data() + size(); }
  [[nodiscard]] const_pointer end() const noexcept { return empty() ? data() : data() + size(); }
  [[nodiscard]] const_pointer cend() const noexcept { return end(); }

  template <typename Value> PooledDense2D& fill(const Value& value) {
    for (auto& element : *this) {
      element = static_cast<T>(value);
    }
    return *this;
  }

  PooledDense2D& set_zero() { return fill(T{}); }

  template <typename UnaryFunction> PooledDense2D& transform_in_place(UnaryFunction&& function) {
    for (auto& element : *this) {
      element = function(element);
    }
    return *this;
  }

  void clear() noexcept {
    buffer_.clear();
    rows_ = 0U;
    cols_ = 0U;
  }

  void release() noexcept {
    buffer_.release();
    rows_ = 0U;
    cols_ = 0U;
  }

  void resize(std::size_t rows, std::size_t cols, ksj::memory::AllocationProperties properties = {}) {
    const auto count = checked_count(rows, cols);
    if (count <= capacity()) {
      buffer_.resize_count(count);
      rows_ = rows;
      cols_ = cols;
      return;
    }
    *this = PooledDense2D(rows, cols, std::move(properties));
  }

  void reserve_elements(std::size_t element_capacity, ksj::memory::AllocationProperties properties = {}) {
    if (element_capacity <= capacity()) {
      return;
    }

    auto new_buffer = ksj::memory::allocate_array<T>(element_capacity, std::move(properties));
    std::copy_n(data(), size(), new_buffer.data());
    new_buffer.resize_count(size());
    buffer_ = std::move(new_buffer);
  }

  void reserve(std::size_t rows, std::size_t cols, ksj::memory::AllocationProperties properties = {}) {
    reserve_elements(checked_count(rows, cols), std::move(properties));
  }

  void reshape(std::size_t rows, std::size_t cols) {
    const auto count = checked_count(rows, cols);
    validate_reshape_count(size(), count, "PooledDense2D reshape cannot change the element count");
    buffer_.resize_count(count);
    rows_ = rows;
    cols_ = cols;
  }

  [[nodiscard]] ksj::memory::PooledBuffer<T>& buffer() noexcept { return buffer_; }
  [[nodiscard]] const ksj::memory::PooledBuffer<T>& buffer() const noexcept { return buffer_; }

  [[nodiscard]] std::size_t height() const noexcept { return rows_; }

  [[nodiscard]] std::size_t width() const noexcept { return cols_; }

  [[nodiscard]] std::size_t row_stride_elements() const noexcept { return row_stride(); }

  [[nodiscard]] std::size_t row_stride_bytes() const noexcept { return row_stride() * sizeof(T); }

private:
  ksj::memory::PooledBuffer<T> buffer_{};
  std::size_t rows_{0};
  std::size_t cols_{0};
};

} // namespace ksj::array::detail
