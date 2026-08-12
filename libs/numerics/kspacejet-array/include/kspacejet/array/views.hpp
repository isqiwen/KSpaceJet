#pragma once

/// Borrowed row-major vector, matrix, image, cube, and 4D views, including shape and stride-safe subviews.

#include "kspacejet/array/detail/storage_traits.hpp"
#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/slicing.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

template <typename T> class PooledMatrix;
template <typename T> class PooledVector;
template <typename T> class PooledCube;
template <typename T> class PooledImage;
template <typename T> class PooledArray4D;
template <typename T> class MatrixView;
template <typename T> class ImageView;
template <typename T> class CubeView;
template <typename T> class Array4DView;

template <typename T> class VectorView {
  template <typename> friend class VectorView;
  template <typename> friend class MatrixView;
  template <typename> friend class ImageView;
  template <typename> friend class CubeView;
  template <typename> friend class Array4DView;

  struct StridedConstructorTag {};

public:
  static_assert(detail::pooled_storage_scalar_v<std::remove_const_t<T>>,
                "VectorView<T> requires a trivially destructible scalar type");

  using value_type = std::remove_const_t<T>;
  using pointer = T*;
  using reference = T&;

  VectorView() = default;

  constexpr VectorView(pointer data, const std::size_t size) noexcept
      : VectorView(StridedConstructorTag{}, data, size, 1U) {}

  [[nodiscard]] VectorView<const value_type> as_const() const noexcept {
    return VectorView<const value_type>(typename VectorView<const value_type>::StridedConstructorTag{}, data_, size_,
                                        stride_);
  }

private:
  constexpr VectorView(StridedConstructorTag, pointer data, const std::size_t size, const std::size_t stride) noexcept
      : data_(data), size_(size), stride_(stride) {}

public:
  [[nodiscard]] pointer data() const noexcept { return data_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::size_t stride() const noexcept { return stride_; }
  [[nodiscard]] std::size_t extent() const noexcept { return size_; }
  [[nodiscard]] std::size_t extent(const Dim dim) const {
    detail::validate_supported_dim(dim, Dim::dim0, "vector view extent dim must be Dim::dim0");
    return size_;
  }
  [[nodiscard]] std::size_t stride(const Dim dim) const {
    detail::validate_supported_dim(dim, Dim::dim0, "vector view stride dim must be Dim::dim0");
    return stride_;
  }
  [[nodiscard]] Shape<1U> shape() const noexcept { return Shape<1U>(size_); }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size_ * sizeof(value_type); }
  [[nodiscard]] bool is_contiguous() const noexcept { return stride_ == 1U; }

  [[nodiscard]] reference operator[](const std::size_t index) const noexcept {
    if (is_contiguous()) {
      return data_[index];
    }
    return data_[index * stride_];
  }
  [[nodiscard]] reference operator()(const std::size_t index) const {
    if (index >= size_) {
      throw std::out_of_range("vector view index is outside the source view");
    }
    return (*this)[index];
  }
  [[nodiscard]] reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() const noexcept { return (*this)(size_ - 1U); }

  template <typename Elements>
  [[nodiscard]] decltype(auto) subview(Elements&& elements) const
    requires(detail::view_selector_v<Elements>)
  {
    if constexpr (detail::fixed_selector_v<Elements>) {
      return subview_impl(detail::normalize_index(std::forward<Elements>(elements), size_,
                                                  "vector view index is outside the source view"));
    } else {
      return subview_impl(detail::normalize_view_selector(std::forward<Elements>(elements), size_,
                                                          "vector view subview is outside the source view"));
    }
  }

private:
  [[nodiscard]] VectorView subview_impl(const detail::NormalizedSlice elements) const {
    if (elements.count == 0) {
      return VectorView(StridedConstructorTag{}, data_, 0U, stride_ * elements.step);
    }
    return VectorView(StridedConstructorTag{}, data_ + elements.start * stride_, elements.count,
                      stride_ * elements.step);
  }

  [[nodiscard]] reference subview_impl(const detail::NormalizedIndex element) const { return (*this)(element.value); }

  pointer data_{nullptr};
  std::size_t size_{0};
  std::size_t stride_{1};
};

template <typename T> class MatrixView {
  template <typename> friend class MatrixView;
  template <typename> friend class ImageView;
  template <typename> friend class CubeView;
  template <typename> friend class Array4DView;

  struct StridedConstructorTag {};

public:
  static_assert(detail::pooled_storage_scalar_v<std::remove_const_t<T>>,
                "MatrixView<T> requires a trivially destructible scalar type");

  using value_type = std::remove_const_t<T>;
  using pointer = T*;
  using reference = T&;
  using vector_view_type = VectorView<T>;

  MatrixView() = default;

  constexpr MatrixView(pointer data, const std::size_t rows, const std::size_t cols) noexcept
      : MatrixView(StridedConstructorTag{}, data, rows, cols, cols, 1U) {}

  [[nodiscard]] pointer data() const noexcept { return data_; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t row_stride() const noexcept { return row_stride_; }
  [[nodiscard]] std::size_t col_stride() const noexcept { return col_stride_; }
  [[nodiscard]] std::size_t row_stride_elements() const noexcept { return row_stride_; }
  [[nodiscard]] std::size_t col_stride_elements() const noexcept { return col_stride_; }
  [[nodiscard]] std::size_t row_stride_bytes() const noexcept { return row_stride_ * sizeof(value_type); }
  [[nodiscard]] std::size_t col_stride_bytes() const noexcept { return col_stride_ * sizeof(value_type); }
  [[nodiscard]] std::size_t size() const noexcept { return rows_ * cols_; }
  [[nodiscard]] bool empty() const noexcept { return rows_ == 0 || cols_ == 0; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size() * sizeof(value_type); }
  [[nodiscard]] bool is_contiguous() const noexcept { return row_stride_ == cols_ && col_stride_ == 1U; }
  [[nodiscard]] Shape<2U> shape() const noexcept { return Shape<2U>(rows_, cols_); }

  [[nodiscard]] std::size_t extent(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return rows_;
      case 1:
        return cols_;
      default:
        throw std::out_of_range("matrix view extent axis must be 0 or 1");
    }
  }

  [[nodiscard]] std::size_t extent(const Dim dim) const { return extent(dim_index(dim)); }

  [[nodiscard]] std::size_t stride(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return row_stride_;
      case 1:
        return col_stride_;
      default:
        throw std::out_of_range("matrix view stride axis must be 0 or 1");
    }
  }

  [[nodiscard]] std::size_t stride(const Dim dim) const { return stride(dim_index(dim)); }

  [[nodiscard]] reference operator[](const std::size_t index) const noexcept {
    if (is_contiguous()) {
      return data_[index];
    }
    return data_[(index / cols_) * row_stride_ + (index % cols_) * col_stride_];
  }
  [[nodiscard]] reference operator()(const std::size_t row, const std::size_t col) const {
    if (row >= rows_ || col >= cols_) {
      throw std::out_of_range("matrix view index is outside the source view");
    }
    return (*this)[row * cols_ + col];
  }
  [[nodiscard]] reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() const noexcept { return (*this)[size() - 1U]; }

  [[nodiscard]] vector_view_type row(const std::size_t row_index) const noexcept {
    return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_ + row_index * row_stride_, cols_,
                            col_stride_);
  }

  [[nodiscard]] vector_view_type col(const std::size_t col_index) const noexcept {
    return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_ + col_index * col_stride_, rows_,
                            row_stride_);
  }

  [[nodiscard]] MatrixView<const value_type> as_const() const noexcept {
    return MatrixView<const value_type>(typename MatrixView<const value_type>::StridedConstructorTag{}, data_, rows_,
                                        cols_, row_stride_, col_stride_);
  }
  [[nodiscard]] ImageView<T> as_image_view() const;

  template <typename Rows, typename Cols>
  [[nodiscard]] decltype(auto) subview(Rows&& rows, Cols&& cols) const
    requires(detail::view_selector_v<Rows> && detail::view_selector_v<Cols>)
  {
    constexpr bool fixed_row = detail::fixed_selector_v<Rows>;
    constexpr bool fixed_col = detail::fixed_selector_v<Cols>;

    if constexpr (fixed_row && fixed_col) {
      return subview_impl(
        detail::normalize_index(std::forward<Rows>(rows), rows_, "matrix view row index is outside the source view"),
        detail::normalize_index(std::forward<Cols>(cols), cols_,
                                "matrix view column index is outside the source view"));
    } else if constexpr (fixed_row) {
      return subview_impl(
        detail::normalize_index(std::forward<Rows>(rows), rows_, "matrix view row index is outside the source view"),
        detail::normalize_view_selector(std::forward<Cols>(cols), cols_,
                                        "matrix view column subview is outside the source view"));
    } else if constexpr (fixed_col) {
      return subview_impl(detail::normalize_view_selector(std::forward<Rows>(rows), rows_,
                                                          "matrix view row subview is outside the source view"),
                          detail::normalize_index(std::forward<Cols>(cols), cols_,
                                                  "matrix view column index is outside the source view"));
    } else {
      return subview_impl(detail::normalize_view_selector(std::forward<Rows>(rows), rows_,
                                                          "matrix view row subview is outside the source view"),
                          detail::normalize_view_selector(std::forward<Cols>(cols), cols_,
                                                          "matrix view column subview is outside the source view"));
    }
  }

private:
  constexpr MatrixView(StridedConstructorTag, pointer data, const std::size_t rows, const std::size_t cols,
                       const std::size_t row_stride, const std::size_t col_stride) noexcept
      : data_(data), rows_(rows), cols_(cols), row_stride_(row_stride), col_stride_(col_stride) {}

  [[nodiscard]] MatrixView subview_impl(const detail::NormalizedSlice rows, const detail::NormalizedSlice cols) const {
    if (rows.count == 0 || cols.count == 0) {
      return MatrixView(StridedConstructorTag{}, data_, 0U, 0U, row_stride_ * rows.step, col_stride_ * cols.step);
    }
    return MatrixView(StridedConstructorTag{}, data_ + rows.start * row_stride_ + cols.start * col_stride_, rows.count,
                      cols.count, row_stride_ * rows.step, col_stride_ * cols.step);
  }

  [[nodiscard]] vector_view_type subview_impl(const detail::NormalizedIndex row,
                                              const detail::NormalizedSlice cols) const {
    if (cols.count == 0) {
      return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_, 0U, col_stride_ * cols.step);
    }
    return vector_view_type(typename vector_view_type::StridedConstructorTag{},
                            data_ + row.value * row_stride_ + cols.start * col_stride_, cols.count,
                            col_stride_ * cols.step);
  }

  [[nodiscard]] vector_view_type subview_impl(const detail::NormalizedSlice rows,
                                              const detail::NormalizedIndex col) const {
    if (rows.count == 0) {
      return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_, 0U, row_stride_ * rows.step);
    }
    return vector_view_type(typename vector_view_type::StridedConstructorTag{},
                            data_ + rows.start * row_stride_ + col.value * col_stride_, rows.count,
                            row_stride_ * rows.step);
  }

  [[nodiscard]] reference subview_impl(const detail::NormalizedIndex row, const detail::NormalizedIndex col) const {
    return (*this)(row.value, col.value);
  }

  pointer data_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t row_stride_{0};
  std::size_t col_stride_{1};
};

template <typename T> class ImageView {
  template <typename> friend class ImageView;
  template <typename> friend class MatrixView;

  struct StridedConstructorTag {};

public:
  static_assert(detail::pooled_storage_scalar_v<std::remove_const_t<T>>,
                "ImageView<T> requires a trivially destructible scalar type");

  using value_type = std::remove_const_t<T>;
  using pointer = T*;
  using reference = T&;
  using vector_view_type = VectorView<T>;

  ImageView() = default;

  constexpr ImageView(pointer data, const std::size_t rows, const std::size_t cols) noexcept
      : ImageView(StridedConstructorTag{}, data, rows, cols, cols) {}

  [[nodiscard]] ImageView<const value_type> as_const() const noexcept {
    return ImageView<const value_type>(typename ImageView<const value_type>::StridedConstructorTag{}, data_, rows_,
                                       cols_, row_stride_);
  }

  [[nodiscard]] MatrixView<T> as_matrix_view() const noexcept {
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, rows_, cols_, row_stride_, 1U);
  }

private:
  constexpr ImageView(StridedConstructorTag, pointer data, const std::size_t rows, const std::size_t cols,
                      const std::size_t row_stride) noexcept
      : data_(data), rows_(rows), cols_(cols), row_stride_(row_stride) {}

public:
  [[nodiscard]] pointer data() const noexcept { return data_; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] std::size_t height() const noexcept { return rows_; }
  [[nodiscard]] std::size_t width() const noexcept { return cols_; }
  [[nodiscard]] std::size_t row_stride() const noexcept { return row_stride_; }
  [[nodiscard]] std::size_t row_stride_elements() const noexcept { return row_stride_; }
  [[nodiscard]] std::size_t row_stride_bytes() const noexcept { return row_stride_ * sizeof(value_type); }
  [[nodiscard]] std::size_t size() const noexcept { return rows_ * cols_; }
  [[nodiscard]] bool empty() const noexcept { return rows_ == 0 || cols_ == 0; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size() * sizeof(value_type); }
  [[nodiscard]] bool is_contiguous() const noexcept { return row_stride_ == cols_; }
  [[nodiscard]] Shape<2U> shape() const noexcept { return Shape<2U>(rows_, cols_); }

  [[nodiscard]] std::size_t extent(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return rows_;
      case 1:
        return cols_;
      default:
        throw std::out_of_range("image view extent axis must be 0 or 1");
    }
  }

  [[nodiscard]] std::size_t extent(const Dim dim) const { return extent(dim_index(dim)); }

  [[nodiscard]] std::size_t stride(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return row_stride_;
      case 1:
        return 1U;
      default:
        throw std::out_of_range("image view stride axis must be 0 or 1");
    }
  }

  [[nodiscard]] std::size_t stride(const Dim dim) const { return stride(dim_index(dim)); }

  [[nodiscard]] reference operator[](const std::size_t index) const noexcept {
    if (is_contiguous()) {
      return data_[index];
    }
    return data_[(index / cols_) * row_stride_ + index % cols_];
  }
  [[nodiscard]] reference operator()(const std::size_t row, const std::size_t col) const {
    if (row >= rows_ || col >= cols_) {
      throw std::out_of_range("image view index is outside the source view");
    }
    return (*this)[row * cols_ + col];
  }
  [[nodiscard]] reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() const noexcept { return (*this)[size() - 1U]; }

  [[nodiscard]] vector_view_type row(const std::size_t row_index) const noexcept {
    return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_ + row_index * row_stride_, cols_,
                            1U);
  }

  [[nodiscard]] vector_view_type col(const std::size_t col_index) const noexcept {
    return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_ + col_index, rows_, row_stride_);
  }

  template <typename Rows, typename Cols>
  [[nodiscard]] decltype(auto) subview(Rows&& rows, Cols&& cols) const
    requires(detail::view_selector_v<Rows> && detail::view_selector_v<Cols>)
  {
    constexpr bool fixed_row = detail::fixed_selector_v<Rows>;
    constexpr bool fixed_col = detail::fixed_selector_v<Cols>;

    if constexpr (fixed_row && fixed_col) {
      return subview_impl(
        detail::normalize_index(std::forward<Rows>(rows), rows_, "image view row index is outside the source view"),
        detail::normalize_index(std::forward<Cols>(cols), cols_, "image view column index is outside the source view"));
    } else if constexpr (fixed_row) {
      return subview_impl(
        detail::normalize_index(std::forward<Rows>(rows), rows_, "image view row index is outside the source view"),
        detail::normalize_view_selector(std::forward<Cols>(cols), cols_,
                                        "image view column subview is outside the source view"));
    } else if constexpr (fixed_col) {
      return subview_impl(
        detail::normalize_view_selector(std::forward<Rows>(rows), rows_,
                                        "image view row subview is outside the source view"),
        detail::normalize_index(std::forward<Cols>(cols), cols_, "image view column index is outside the source view"));
    } else {
      return subview_impl(detail::normalize_view_selector(std::forward<Rows>(rows), rows_,
                                                          "image view row subview is outside the source view"),
                          detail::normalize_view_selector(std::forward<Cols>(cols), cols_,
                                                          "image view column subview is outside the source view"));
    }
  }

private:
  [[nodiscard]] ImageView subview_impl(const detail::NormalizedSlice rows, const detail::NormalizedSlice cols) const {
    if (cols.step != 1U) {
      throw std::invalid_argument("image view cannot represent a strided column subview");
    }
    if (rows.count == 0 || cols.count == 0) {
      return ImageView(StridedConstructorTag{}, data_, 0U, 0U, row_stride_ * rows.step);
    }
    return ImageView(StridedConstructorTag{}, data_ + rows.start * row_stride_ + cols.start, rows.count, cols.count,
                     row_stride_ * rows.step);
  }

  [[nodiscard]] vector_view_type subview_impl(const detail::NormalizedIndex row,
                                              const detail::NormalizedSlice cols) const {
    if (cols.count == 0) {
      return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_, 0U, cols.step);
    }
    return vector_view_type(typename vector_view_type::StridedConstructorTag{},
                            data_ + row.value * row_stride_ + cols.start, cols.count, cols.step);
  }

  [[nodiscard]] vector_view_type subview_impl(const detail::NormalizedSlice rows,
                                              const detail::NormalizedIndex col) const {
    if (rows.count == 0) {
      return vector_view_type(typename vector_view_type::StridedConstructorTag{}, data_, 0U, row_stride_ * rows.step);
    }
    return vector_view_type(typename vector_view_type::StridedConstructorTag{},
                            data_ + rows.start * row_stride_ + col.value, rows.count, row_stride_ * rows.step);
  }

  [[nodiscard]] reference subview_impl(const detail::NormalizedIndex row, const detail::NormalizedIndex col) const {
    return (*this)(row.value, col.value);
  }

  pointer data_{nullptr};
  std::size_t rows_{0};
  std::size_t cols_{0};
  std::size_t row_stride_{0};
};

template <typename T> [[nodiscard]] ImageView<T> MatrixView<T>::as_image_view() const {
  if (col_stride_ != 1U) {
    throw std::invalid_argument("matrix view cannot be reinterpreted as image view unless column stride is 1");
  }
  return ImageView<T>(typename ImageView<T>::StridedConstructorTag{}, data_, rows_, cols_, row_stride_);
}

template <typename T> class CubeView {
  template <typename> friend class CubeView;
  template <typename> friend class Array4DView;

  struct StridedConstructorTag {};

public:
  static_assert(detail::pooled_storage_scalar_v<std::remove_const_t<T>>,
                "CubeView<T> requires a trivially destructible scalar type");

  using value_type = std::remove_const_t<T>;
  using pointer = T*;
  using reference = T&;
  using vector_view_type = VectorView<T>;
  using matrix_view_type = MatrixView<T>;

  CubeView() = default;

  constexpr CubeView(pointer data, const std::size_t dim0, const std::size_t dim1, const std::size_t dim2) noexcept
      : CubeView(StridedConstructorTag{}, data, dim0, dim1, dim2, dim1 * dim2, dim2, 1U) {}

  [[nodiscard]] pointer data() const noexcept { return data_; }
  [[nodiscard]] std::size_t dim0() const noexcept { return dim0_; }
  [[nodiscard]] std::size_t dim1() const noexcept { return dim1_; }
  [[nodiscard]] std::size_t dim2() const noexcept { return dim2_; }
  [[nodiscard]] std::size_t dim0_stride() const noexcept { return dim0_stride_; }
  [[nodiscard]] std::size_t dim1_stride() const noexcept { return dim1_stride_; }
  [[nodiscard]] std::size_t dim2_stride() const noexcept { return dim2_stride_; }
  [[nodiscard]] std::size_t size() const noexcept { return dim0_ * dim1_ * dim2_; }
  [[nodiscard]] bool empty() const noexcept { return dim0_ == 0 || dim1_ == 0 || dim2_ == 0; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size() * sizeof(value_type); }
  [[nodiscard]] bool is_contiguous() const noexcept {
    return dim0_stride_ == dim1_ * dim2_ && dim1_stride_ == dim2_ && dim2_stride_ == 1U;
  }
  [[nodiscard]] Shape<3U> shape() const noexcept { return Shape<3U>(dim0_, dim1_, dim2_); }

  [[nodiscard]] std::size_t extent(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return dim0_;
      case 1:
        return dim1_;
      case 2:
        return dim2_;
      default:
        throw std::out_of_range("cube view extent axis must be 0, 1, or 2");
    }
  }

  [[nodiscard]] std::size_t extent(const Dim dim) const { return extent(dim_index(dim)); }

  [[nodiscard]] std::size_t stride(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return dim0_stride_;
      case 1:
        return dim1_stride_;
      case 2:
        return dim2_stride_;
      default:
        throw std::out_of_range("cube view stride axis must be 0, 1, or 2");
    }
  }

  [[nodiscard]] std::size_t stride(const Dim dim) const { return stride(dim_index(dim)); }

  [[nodiscard]] reference operator[](const std::size_t index) const noexcept {
    if (is_contiguous()) {
      return data_[index];
    }
    const auto i2 = index % dim2_;
    const auto i0_i1 = index / dim2_;
    return data_[(i0_i1 / dim1_) * dim0_stride_ + (i0_i1 % dim1_) * dim1_stride_ + i2 * dim2_stride_];
  }
  [[nodiscard]] reference operator()(const std::size_t i0, const std::size_t i1, const std::size_t i2) const {
    if (i0 >= dim0_ || i1 >= dim1_ || i2 >= dim2_) {
      throw std::out_of_range("cube view index is outside the source view");
    }
    return (*this)[(i0 * dim1_ + i1) * dim2_ + i2];
  }
  [[nodiscard]] reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() const noexcept { return (*this)[size() - 1U]; }

  [[nodiscard]] CubeView<const value_type> as_const() const noexcept {
    return CubeView<const value_type>(typename CubeView<const value_type>::StridedConstructorTag{}, data_, dim0_, dim1_,
                                      dim2_, dim0_stride_, dim1_stride_, dim2_stride_);
  }

  template <typename Dim0, typename Dim1, typename Dim2>
  [[nodiscard]] decltype(auto) subview(Dim0&& dim0, Dim1&& dim1, Dim2&& dim2) const
    requires(detail::view_selector_v<Dim0> && detail::view_selector_v<Dim1> && detail::view_selector_v<Dim2>)
  {
    constexpr bool fixed0 = detail::fixed_selector_v<Dim0>;
    constexpr bool fixed1 = detail::fixed_selector_v<Dim1>;
    constexpr bool fixed2 = detail::fixed_selector_v<Dim2>;

    if constexpr (fixed0 && fixed1 && fixed2) {
      return subview_impl(
        detail::normalize_index(std::forward<Dim0>(dim0), dim0_, "cube view dim0 index is outside the source view"),
        detail::normalize_index(std::forward<Dim1>(dim1), dim1_, "cube view dim1 index is outside the source view"),
        detail::normalize_index(std::forward<Dim2>(dim2), dim2_, "cube view dim2 index is outside the source view"));
    } else if constexpr (fixed0 && fixed1) {
      return subview_impl(
        detail::normalize_index(std::forward<Dim0>(dim0), dim0_, "cube view dim0 index is outside the source view"),
        detail::normalize_index(std::forward<Dim1>(dim1), dim1_, "cube view dim1 index is outside the source view"),
        detail::normalize_view_selector(std::forward<Dim2>(dim2), dim2_,
                                        "cube view dim2 subview is outside the source view"));
    } else if constexpr (fixed0 && fixed2) {
      return subview_impl(
        detail::normalize_index(std::forward<Dim0>(dim0), dim0_, "cube view dim0 index is outside the source view"),
        detail::normalize_view_selector(std::forward<Dim1>(dim1), dim1_,
                                        "cube view dim1 subview is outside the source view"),
        detail::normalize_index(std::forward<Dim2>(dim2), dim2_, "cube view dim2 index is outside the source view"));
    } else if constexpr (fixed1 && fixed2) {
      return subview_impl(
        detail::normalize_view_selector(std::forward<Dim0>(dim0), dim0_,
                                        "cube view dim0 subview is outside the source view"),
        detail::normalize_index(std::forward<Dim1>(dim1), dim1_, "cube view dim1 index is outside the source view"),
        detail::normalize_index(std::forward<Dim2>(dim2), dim2_, "cube view dim2 index is outside the source view"));
    } else if constexpr (fixed0) {
      return subview_impl(
        detail::normalize_index(std::forward<Dim0>(dim0), dim0_, "cube view dim0 index is outside the source view"),
        detail::normalize_view_selector(std::forward<Dim1>(dim1), dim1_,
                                        "cube view dim1 subview is outside the source view"),
        detail::normalize_view_selector(std::forward<Dim2>(dim2), dim2_,
                                        "cube view dim2 subview is outside the source view"));
    } else if constexpr (fixed1) {
      return subview_impl(
        detail::normalize_view_selector(std::forward<Dim0>(dim0), dim0_,
                                        "cube view dim0 subview is outside the source view"),
        detail::normalize_index(std::forward<Dim1>(dim1), dim1_, "cube view dim1 index is outside the source view"),
        detail::normalize_view_selector(std::forward<Dim2>(dim2), dim2_,
                                        "cube view dim2 subview is outside the source view"));
    } else if constexpr (fixed2) {
      return subview_impl(
        detail::normalize_view_selector(std::forward<Dim0>(dim0), dim0_,
                                        "cube view dim0 subview is outside the source view"),
        detail::normalize_view_selector(std::forward<Dim1>(dim1), dim1_,
                                        "cube view dim1 subview is outside the source view"),
        detail::normalize_index(std::forward<Dim2>(dim2), dim2_, "cube view dim2 index is outside the source view"));
    } else {
      return subview_impl(detail::normalize_view_selector(std::forward<Dim0>(dim0), dim0_,
                                                          "cube view dim0 subview is outside the source view"),
                          detail::normalize_view_selector(std::forward<Dim1>(dim1), dim1_,
                                                          "cube view dim1 subview is outside the source view"),
                          detail::normalize_view_selector(std::forward<Dim2>(dim2), dim2_,
                                                          "cube view dim2 subview is outside the source view"));
    }
  }

private:
  constexpr CubeView(StridedConstructorTag, pointer data, const std::size_t dim0, const std::size_t dim1,
                     const std::size_t dim2, const std::size_t dim0_stride, const std::size_t dim1_stride,
                     const std::size_t dim2_stride) noexcept
      : data_(data), dim0_(dim0), dim1_(dim1), dim2_(dim2), dim0_stride_(dim0_stride), dim1_stride_(dim1_stride),
        dim2_stride_(dim2_stride) {}

  [[nodiscard]] CubeView subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedSlice dim1,
                                      const detail::NormalizedSlice dim2) const {
    if (dim0.count == 0 || dim1.count == 0 || dim2.count == 0) {
      return CubeView(StridedConstructorTag{}, data_, 0U, 0U, 0U, dim0_stride_ * dim0.step, dim1_stride_ * dim1.step,
                      dim2_stride_ * dim2.step);
    }
    return CubeView(StridedConstructorTag{},
                    data_ + dim0.start * dim0_stride_ + dim1.start * dim1_stride_ + dim2.start * dim2_stride_,
                    dim0.count, dim1.count, dim2.count, dim0_stride_ * dim0.step, dim1_stride_ * dim1.step,
                    dim2_stride_ * dim2.step);
  }

  [[nodiscard]] matrix_view_type subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedSlice dim1,
                                              const detail::NormalizedSlice dim2) const {
    if (dim1.count == 0 || dim2.count == 0) {
      return matrix_view_type(typename matrix_view_type::StridedConstructorTag{}, data_, 0U, 0U,
                              dim1_stride_ * dim1.step, dim2_stride_ * dim2.step);
    }
    return matrix_view_type(typename matrix_view_type::StridedConstructorTag{},
                            data_ + dim0.value * dim0_stride_ + dim1.start * dim1_stride_ + dim2.start * dim2_stride_,
                            dim1.count, dim2.count, dim1_stride_ * dim1.step, dim2_stride_ * dim2.step);
  }

  [[nodiscard]] matrix_view_type subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedIndex dim1,
                                              const detail::NormalizedSlice dim2) const {
    if (dim0.count == 0 || dim2.count == 0) {
      return matrix_view_type(typename matrix_view_type::StridedConstructorTag{}, data_, 0U, 0U,
                              dim0_stride_ * dim0.step, dim2_stride_ * dim2.step);
    }
    return matrix_view_type(typename matrix_view_type::StridedConstructorTag{},
                            data_ + dim0.start * dim0_stride_ + dim1.value * dim1_stride_ + dim2.start * dim2_stride_,
                            dim0.count, dim2.count, dim0_stride_ * dim0.step, dim2_stride_ * dim2.step);
  }

  [[nodiscard]] matrix_view_type subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedSlice dim1,
                                              const detail::NormalizedIndex dim2) const {
    if (dim0.count == 0 || dim1.count == 0) {
      return matrix_view_type(typename matrix_view_type::StridedConstructorTag{}, data_, 0U, 0U,
                              dim0_stride_ * dim0.step, dim1_stride_ * dim1.step);
    }
    return matrix_view_type(typename matrix_view_type::StridedConstructorTag{},
                            data_ + dim0.start * dim0_stride_ + dim1.start * dim1_stride_ + dim2.value * dim2_stride_,
                            dim0.count, dim1.count, dim0_stride_ * dim0.step, dim1_stride_ * dim1.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedSlice dim2) const {
    if (dim2.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim2_stride_ * dim2.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.value * dim1_stride_ + dim2.start * dim2_stride_,
                         dim2.count, dim2_stride_ * dim2.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedSlice dim1,
                                           const detail::NormalizedIndex dim2) const {
    if (dim1.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim1_stride_ * dim1.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.start * dim1_stride_ + dim2.value * dim2_stride_,
                         dim1.count, dim1_stride_ * dim1.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedIndex dim2) const {
    if (dim0.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim0_stride_ * dim0.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.start * dim0_stride_ + dim1.value * dim1_stride_ + dim2.value * dim2_stride_,
                         dim0.count, dim0_stride_ * dim0.step);
  }

  [[nodiscard]] reference subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedIndex dim1,
                                       const detail::NormalizedIndex dim2) const {
    return (*this)(dim0.value, dim1.value, dim2.value);
  }

  pointer data_{nullptr};
  std::size_t dim0_{0};
  std::size_t dim1_{0};
  std::size_t dim2_{0};
  std::size_t dim0_stride_{0};
  std::size_t dim1_stride_{0};
  std::size_t dim2_stride_{1};
};

template <typename T> class Array4DView {
  template <typename> friend class Array4DView;

  struct StridedConstructorTag {};

public:
  static_assert(detail::pooled_storage_scalar_v<std::remove_const_t<T>>,
                "Array4DView<T> requires a trivially destructible scalar type");

  using value_type = std::remove_const_t<T>;
  using pointer = T*;
  using reference = T&;

  Array4DView() = default;

  constexpr Array4DView(pointer data, const std::size_t dim0, const std::size_t dim1, const std::size_t dim2,
                        const std::size_t dim3) noexcept
      : Array4DView(StridedConstructorTag{}, data, dim0, dim1, dim2, dim3, dim1 * dim2 * dim3, dim2 * dim3, dim3, 1U) {}

  [[nodiscard]] pointer data() const noexcept { return data_; }
  [[nodiscard]] std::size_t dim0() const noexcept { return dim0_; }
  [[nodiscard]] std::size_t dim1() const noexcept { return dim1_; }
  [[nodiscard]] std::size_t dim2() const noexcept { return dim2_; }
  [[nodiscard]] std::size_t dim3() const noexcept { return dim3_; }
  [[nodiscard]] std::size_t dim0_stride() const noexcept { return dim0_stride_; }
  [[nodiscard]] std::size_t dim1_stride() const noexcept { return dim1_stride_; }
  [[nodiscard]] std::size_t dim2_stride() const noexcept { return dim2_stride_; }
  [[nodiscard]] std::size_t dim3_stride() const noexcept { return dim3_stride_; }
  [[nodiscard]] std::size_t size() const noexcept { return dim0_ * dim1_ * dim2_ * dim3_; }
  [[nodiscard]] bool empty() const noexcept { return dim0_ == 0 || dim1_ == 0 || dim2_ == 0 || dim3_ == 0; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size() * sizeof(value_type); }
  [[nodiscard]] bool is_contiguous() const noexcept {
    return dim0_stride_ == dim1_ * dim2_ * dim3_ && dim1_stride_ == dim2_ * dim3_ && dim2_stride_ == dim3_ &&
           dim3_stride_ == 1U;
  }
  [[nodiscard]] Shape<4U> shape() const noexcept { return Shape<4U>(dim0_, dim1_, dim2_, dim3_); }

  [[nodiscard]] std::size_t extent(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return dim0_;
      case 1:
        return dim1_;
      case 2:
        return dim2_;
      case 3:
        return dim3_;
      default:
        throw std::out_of_range("array4d view extent axis must be 0, 1, 2, or 3");
    }
  }

  [[nodiscard]] std::size_t extent(const Dim dim) const { return extent(dim_index(dim)); }

  [[nodiscard]] std::size_t stride(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return dim0_stride_;
      case 1:
        return dim1_stride_;
      case 2:
        return dim2_stride_;
      case 3:
        return dim3_stride_;
      default:
        throw std::out_of_range("array4d view stride axis must be 0, 1, 2, or 3");
    }
  }

  [[nodiscard]] std::size_t stride(const Dim dim) const { return stride(dim_index(dim)); }

  [[nodiscard]] reference operator[](const std::size_t index) const noexcept {
    if (is_contiguous()) {
      return data_[index];
    }
    const auto i3 = index % dim3_;
    const auto dim012 = index / dim3_;
    const auto i2 = dim012 % dim2_;
    const auto dim01 = dim012 / dim2_;
    const auto i1 = dim01 % dim1_;
    const auto i0 = dim01 / dim1_;
    return data_[i0 * dim0_stride_ + i1 * dim1_stride_ + i2 * dim2_stride_ + i3 * dim3_stride_];
  }
  [[nodiscard]] reference operator()(const std::size_t i0, const std::size_t i1, const std::size_t i2,
                                     const std::size_t i3) const {
    if (i0 >= dim0_ || i1 >= dim1_ || i2 >= dim2_ || i3 >= dim3_) {
      throw std::out_of_range("array4d view index is outside the source view");
    }
    return (*this)[((i0 * dim1_ + i1) * dim2_ + i2) * dim3_ + i3];
  }
  [[nodiscard]] reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() const noexcept { return (*this)[size() - 1U]; }

  [[nodiscard]] Array4DView<const value_type> as_const() const noexcept {
    return Array4DView<const value_type>(typename Array4DView<const value_type>::StridedConstructorTag{}, data_, dim0_,
                                         dim1_, dim2_, dim3_, dim0_stride_, dim1_stride_, dim2_stride_, dim3_stride_);
  }

  template <typename Dim0, typename Dim1, typename Dim2, typename Dim3>
  [[nodiscard]] decltype(auto) subview(Dim0&& dim0, Dim1&& dim1, Dim2&& dim2, Dim3&& dim3) const
    requires(detail::view_selector_v<Dim0> && detail::view_selector_v<Dim1> && detail::view_selector_v<Dim2> &&
             detail::view_selector_v<Dim3>)
  {
    return subview_impl(detail::normalize_subview_selector(std::forward<Dim0>(dim0), dim0_,
                                                           "array4d view dim0 subview is outside the source view",
                                                           "array4d view dim0 index is outside the source view"),
                        detail::normalize_subview_selector(std::forward<Dim1>(dim1), dim1_,
                                                           "array4d view dim1 subview is outside the source view",
                                                           "array4d view dim1 index is outside the source view"),
                        detail::normalize_subview_selector(std::forward<Dim2>(dim2), dim2_,
                                                           "array4d view dim2 subview is outside the source view",
                                                           "array4d view dim2 index is outside the source view"),
                        detail::normalize_subview_selector(std::forward<Dim3>(dim3), dim3_,
                                                           "array4d view dim3 subview is outside the source view",
                                                           "array4d view dim3 index is outside the source view"));
  }

private:
  constexpr Array4DView(StridedConstructorTag, pointer data, const std::size_t dim0, const std::size_t dim1,
                        const std::size_t dim2, const std::size_t dim3, const std::size_t dim0_stride,
                        const std::size_t dim1_stride, const std::size_t dim2_stride,
                        const std::size_t dim3_stride) noexcept
      : data_(data), dim0_(dim0), dim1_(dim1), dim2_(dim2), dim3_(dim3), dim0_stride_(dim0_stride),
        dim1_stride_(dim1_stride), dim2_stride_(dim2_stride), dim3_stride_(dim3_stride) {}

  [[nodiscard]] Array4DView subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedSlice dim1,
                                         const detail::NormalizedSlice dim2, const detail::NormalizedSlice dim3) const {
    if (dim0.count == 0 || dim1.count == 0 || dim2.count == 0 || dim3.count == 0) {
      return Array4DView(StridedConstructorTag{}, data_, 0U, 0U, 0U, 0U, dim0_stride_ * dim0.step,
                         dim1_stride_ * dim1.step, dim2_stride_ * dim2.step, dim3_stride_ * dim3.step);
    }
    return Array4DView(StridedConstructorTag{},
                       data_ + dim0.start * dim0_stride_ + dim1.start * dim1_stride_ + dim2.start * dim2_stride_ +
                         dim3.start * dim3_stride_,
                       dim0.count, dim1.count, dim2.count, dim3.count, dim0_stride_ * dim0.step,
                       dim1_stride_ * dim1.step, dim2_stride_ * dim2.step, dim3_stride_ * dim3.step);
  }

  [[nodiscard]] CubeView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedSlice dim1,
                                         const detail::NormalizedSlice dim2, const detail::NormalizedSlice dim3) const {
    if (dim1.count == 0 || dim2.count == 0 || dim3.count == 0) {
      return CubeView<T>(typename CubeView<T>::StridedConstructorTag{}, data_, 0U, 0U, 0U, dim1_stride_ * dim1.step,
                         dim2_stride_ * dim2.step, dim3_stride_ * dim3.step);
    }
    return CubeView<T>(typename CubeView<T>::StridedConstructorTag{},
                       data_ + dim0.value * dim0_stride_ + dim1.start * dim1_stride_ + dim2.start * dim2_stride_ +
                         dim3.start * dim3_stride_,
                       dim1.count, dim2.count, dim3.count, dim1_stride_ * dim1.step, dim2_stride_ * dim2.step,
                       dim3_stride_ * dim3.step);
  }

  [[nodiscard]] CubeView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedIndex dim1,
                                         const detail::NormalizedSlice dim2, const detail::NormalizedSlice dim3) const {
    if (dim0.count == 0 || dim2.count == 0 || dim3.count == 0) {
      return CubeView<T>(typename CubeView<T>::StridedConstructorTag{}, data_, 0U, 0U, 0U, dim0_stride_ * dim0.step,
                         dim2_stride_ * dim2.step, dim3_stride_ * dim3.step);
    }
    return CubeView<T>(typename CubeView<T>::StridedConstructorTag{},
                       data_ + dim0.start * dim0_stride_ + dim1.value * dim1_stride_ + dim2.start * dim2_stride_ +
                         dim3.start * dim3_stride_,
                       dim0.count, dim2.count, dim3.count, dim0_stride_ * dim0.step, dim2_stride_ * dim2.step,
                       dim3_stride_ * dim3.step);
  }

  [[nodiscard]] CubeView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedSlice dim1,
                                         const detail::NormalizedIndex dim2, const detail::NormalizedSlice dim3) const {
    if (dim0.count == 0 || dim1.count == 0 || dim3.count == 0) {
      return CubeView<T>(typename CubeView<T>::StridedConstructorTag{}, data_, 0U, 0U, 0U, dim0_stride_ * dim0.step,
                         dim1_stride_ * dim1.step, dim3_stride_ * dim3.step);
    }
    return CubeView<T>(typename CubeView<T>::StridedConstructorTag{},
                       data_ + dim0.start * dim0_stride_ + dim1.start * dim1_stride_ + dim2.value * dim2_stride_ +
                         dim3.start * dim3_stride_,
                       dim0.count, dim1.count, dim3.count, dim0_stride_ * dim0.step, dim1_stride_ * dim1.step,
                       dim3_stride_ * dim3.step);
  }

  [[nodiscard]] CubeView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedSlice dim1,
                                         const detail::NormalizedSlice dim2, const detail::NormalizedIndex dim3) const {
    if (dim0.count == 0 || dim1.count == 0 || dim2.count == 0) {
      return CubeView<T>(typename CubeView<T>::StridedConstructorTag{}, data_, 0U, 0U, 0U, dim0_stride_ * dim0.step,
                         dim1_stride_ * dim1.step, dim2_stride_ * dim2.step);
    }
    return CubeView<T>(typename CubeView<T>::StridedConstructorTag{},
                       data_ + dim0.start * dim0_stride_ + dim1.start * dim1_stride_ + dim2.start * dim2_stride_ +
                         dim3.value * dim3_stride_,
                       dim0.count, dim1.count, dim2.count, dim0_stride_ * dim0.step, dim1_stride_ * dim1.step,
                       dim2_stride_ * dim2.step);
  }

  [[nodiscard]] MatrixView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedSlice dim2,
                                           const detail::NormalizedSlice dim3) const {
    if (dim2.count == 0 || dim3.count == 0) {
      return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, 0U, 0U, dim2_stride_ * dim2.step,
                           dim3_stride_ * dim3.step);
    }
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.value * dim1_stride_ + dim2.start * dim2_stride_ +
                           dim3.start * dim3_stride_,
                         dim2.count, dim3.count, dim2_stride_ * dim2.step, dim3_stride_ * dim3.step);
  }

  [[nodiscard]] MatrixView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedSlice dim1,
                                           const detail::NormalizedIndex dim2,
                                           const detail::NormalizedSlice dim3) const {
    if (dim1.count == 0 || dim3.count == 0) {
      return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, 0U, 0U, dim1_stride_ * dim1.step,
                           dim3_stride_ * dim3.step);
    }
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.start * dim1_stride_ + dim2.value * dim2_stride_ +
                           dim3.start * dim3_stride_,
                         dim1.count, dim3.count, dim1_stride_ * dim1.step, dim3_stride_ * dim3.step);
  }

  [[nodiscard]] MatrixView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedSlice dim1,
                                           const detail::NormalizedSlice dim2,
                                           const detail::NormalizedIndex dim3) const {
    if (dim1.count == 0 || dim2.count == 0) {
      return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, 0U, 0U, dim1_stride_ * dim1.step,
                           dim2_stride_ * dim2.step);
    }
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.start * dim1_stride_ + dim2.start * dim2_stride_ +
                           dim3.value * dim3_stride_,
                         dim1.count, dim2.count, dim1_stride_ * dim1.step, dim2_stride_ * dim2.step);
  }

  [[nodiscard]] MatrixView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedIndex dim2,
                                           const detail::NormalizedSlice dim3) const {
    if (dim0.count == 0 || dim3.count == 0) {
      return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, 0U, 0U, dim0_stride_ * dim0.step,
                           dim3_stride_ * dim3.step);
    }
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{},
                         data_ + dim0.start * dim0_stride_ + dim1.value * dim1_stride_ + dim2.value * dim2_stride_ +
                           dim3.start * dim3_stride_,
                         dim0.count, dim3.count, dim0_stride_ * dim0.step, dim3_stride_ * dim3.step);
  }

  [[nodiscard]] MatrixView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedSlice dim2,
                                           const detail::NormalizedIndex dim3) const {
    if (dim0.count == 0 || dim2.count == 0) {
      return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, 0U, 0U, dim0_stride_ * dim0.step,
                           dim2_stride_ * dim2.step);
    }
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{},
                         data_ + dim0.start * dim0_stride_ + dim1.value * dim1_stride_ + dim2.start * dim2_stride_ +
                           dim3.value * dim3_stride_,
                         dim0.count, dim2.count, dim0_stride_ * dim0.step, dim2_stride_ * dim2.step);
  }

  [[nodiscard]] MatrixView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedSlice dim1,
                                           const detail::NormalizedIndex dim2,
                                           const detail::NormalizedIndex dim3) const {
    if (dim0.count == 0 || dim1.count == 0) {
      return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{}, data_, 0U, 0U, dim0_stride_ * dim0.step,
                           dim1_stride_ * dim1.step);
    }
    return MatrixView<T>(typename MatrixView<T>::StridedConstructorTag{},
                         data_ + dim0.start * dim0_stride_ + dim1.start * dim1_stride_ + dim2.value * dim2_stride_ +
                           dim3.value * dim3_stride_,
                         dim0.count, dim1.count, dim0_stride_ * dim0.step, dim1_stride_ * dim1.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedIndex dim2,
                                           const detail::NormalizedSlice dim3) const {
    if (dim3.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim3_stride_ * dim3.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.value * dim1_stride_ + dim2.value * dim2_stride_ +
                           dim3.start * dim3_stride_,
                         dim3.count, dim3_stride_ * dim3.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedSlice dim2,
                                           const detail::NormalizedIndex dim3) const {
    if (dim2.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim2_stride_ * dim2.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.value * dim1_stride_ + dim2.start * dim2_stride_ +
                           dim3.value * dim3_stride_,
                         dim2.count, dim2_stride_ * dim2.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedSlice dim1,
                                           const detail::NormalizedIndex dim2,
                                           const detail::NormalizedIndex dim3) const {
    if (dim1.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim1_stride_ * dim1.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.value * dim0_stride_ + dim1.start * dim1_stride_ + dim2.value * dim2_stride_ +
                           dim3.value * dim3_stride_,
                         dim1.count, dim1_stride_ * dim1.step);
  }

  [[nodiscard]] VectorView<T> subview_impl(const detail::NormalizedSlice dim0, const detail::NormalizedIndex dim1,
                                           const detail::NormalizedIndex dim2,
                                           const detail::NormalizedIndex dim3) const {
    if (dim0.count == 0) {
      return VectorView<T>(typename VectorView<T>::StridedConstructorTag{}, data_, 0U, dim0_stride_ * dim0.step);
    }
    return VectorView<T>(typename VectorView<T>::StridedConstructorTag{},
                         data_ + dim0.start * dim0_stride_ + dim1.value * dim1_stride_ + dim2.value * dim2_stride_ +
                           dim3.value * dim3_stride_,
                         dim0.count, dim0_stride_ * dim0.step);
  }

  [[nodiscard]] reference subview_impl(const detail::NormalizedIndex dim0, const detail::NormalizedIndex dim1,
                                       const detail::NormalizedIndex dim2, const detail::NormalizedIndex dim3) const {
    return (*this)(dim0.value, dim1.value, dim2.value, dim3.value);
  }

  pointer data_{nullptr};
  std::size_t dim0_{0};
  std::size_t dim1_{0};
  std::size_t dim2_{0};
  std::size_t dim3_{0};
  std::size_t dim0_stride_{0};
  std::size_t dim1_stride_{0};
  std::size_t dim2_stride_{0};
  std::size_t dim3_stride_{1};
};

template <typename T> [[nodiscard]] VectorView<T> vector_view(PooledVector<T>& vector) noexcept {
  return vector.view();
}

template <typename T> [[nodiscard]] VectorView<const T> vector_view(const PooledVector<T>& vector) noexcept {
  return vector.view();
}

template <typename T> [[nodiscard]] MatrixView<T> matrix_view(PooledMatrix<T>& matrix) noexcept {
  return matrix.view();
}

template <typename T> [[nodiscard]] MatrixView<const T> matrix_view(const PooledMatrix<T>& matrix) noexcept {
  return matrix.view();
}

template <typename T> [[nodiscard]] MatrixView<T> matrix_view(PooledImage<T>& image) noexcept {
  return image.view().as_matrix_view();
}

template <typename T> [[nodiscard]] MatrixView<const T> matrix_view(const PooledImage<T>& image) noexcept {
  return image.view().as_matrix_view();
}

template <typename T> [[nodiscard]] MatrixView<T> matrix_view(ImageView<T> image) noexcept {
  return image.as_matrix_view();
}

template <typename T> [[nodiscard]] ImageView<T> image_view(PooledImage<T>& image) noexcept {
  return image.view();
}

template <typename T> [[nodiscard]] ImageView<const T> image_view(const PooledImage<T>& image) noexcept {
  return image.view();
}

template <typename T> [[nodiscard]] ImageView<T> image_view(PooledMatrix<T>& matrix) noexcept {
  return ImageView<T>(matrix.data(), matrix.rows(), matrix.cols());
}

template <typename T> [[nodiscard]] ImageView<const T> image_view(const PooledMatrix<T>& matrix) noexcept {
  return ImageView<const T>(matrix.data(), matrix.rows(), matrix.cols());
}

template <typename T> [[nodiscard]] ImageView<T> image_view(MatrixView<T> matrix) {
  return matrix.as_image_view();
}

template <typename T> [[nodiscard]] CubeView<T> cube_view(PooledCube<T>& cube) noexcept {
  return cube.view();
}

template <typename T> [[nodiscard]] CubeView<const T> cube_view(const PooledCube<T>& cube) noexcept {
  return cube.view();
}

template <typename T>
[[nodiscard]] CubeView<T> cube_view(T* data, const std::size_t dim0, const std::size_t dim1,
                                    const std::size_t dim2) noexcept {
  return CubeView<T>(data, dim0, dim1, dim2);
}

template <typename T> [[nodiscard]] Array4DView<T> array4d_view(PooledArray4D<T>& array) noexcept {
  return array.view();
}

template <typename T> [[nodiscard]] Array4DView<const T> array4d_view(const PooledArray4D<T>& array) noexcept {
  return array.view();
}

template <typename T>
[[nodiscard]] Array4DView<T> array4d_view(T* data, const std::size_t dim0, const std::size_t dim1,
                                          const std::size_t dim2, const std::size_t dim3) noexcept {
  return Array4DView<T>(data, dim0, dim1, dim2, dim3);
}

template <typename T>
[[nodiscard]] VectorView<const std::remove_const_t<T>> as_const_view(VectorView<T> input) noexcept {
  return input.as_const();
}

template <typename T>
[[nodiscard]] MatrixView<const std::remove_const_t<T>> as_const_view(MatrixView<T> input) noexcept {
  return input.as_const();
}

template <typename T> [[nodiscard]] ImageView<const std::remove_const_t<T>> as_const_view(ImageView<T> input) noexcept {
  return input.as_const();
}

template <typename T> [[nodiscard]] CubeView<const std::remove_const_t<T>> as_const_view(CubeView<T> input) noexcept {
  return input.as_const();
}

template <typename T>
[[nodiscard]] Array4DView<const std::remove_const_t<T>> as_const_view(Array4DView<T> input) noexcept {
  return input.as_const();
}

} // namespace ksj::array
