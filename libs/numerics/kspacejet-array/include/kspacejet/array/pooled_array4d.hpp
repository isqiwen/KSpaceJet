#pragma once

/// Owning four-dimensional row-major storage with Array4D View and subview accessors.

#include "kspacejet/array/detail/storage_traits.hpp"
#include "kspacejet/array/dimensions.hpp"
#include "kspacejet/array/copy.hpp"
#include "kspacejet/array/initialization.hpp"
#include "kspacejet/array/reductions.hpp"
#include "kspacejet/array/transforms.hpp"
#include "kspacejet/memory/allocation_properties.hpp"
#include "kspacejet/memory/pooled_buffer.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

template <typename T> class PooledArray4D {
public:
  static_assert(detail::pooled_storage_scalar_v<T>,
                "PooledArray4D<T> requires a non-const trivially destructible scalar type");

  using value_type = T;
  using view_type = Array4DView<T>;
  using const_view_type = Array4DView<const T>;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;

  PooledArray4D() = default;

  PooledArray4D(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                ksj::memory::AllocationProperties properties = {})
      : buffer_(ksj::memory::allocate_array<T>(
          detail::checked_count(detail::checked_count(detail::checked_count(dim0, dim1), dim2), dim3),
          std::move(properties))),
        dim0_(dim0), dim1_(dim1), dim2_(dim2), dim3_(dim3) {}

  template <typename Value>
  [[nodiscard]] static PooledArray4D constant(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                              const Value& value, ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledArray4D(dim0, dim1, dim2, dim3, std::move(properties));
    output.set_constant(value);
    return output;
  }

  [[nodiscard]] static PooledArray4D zeros(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                           ksj::memory::AllocationProperties properties = {}) {
    return constant(dim0, dim1, dim2, dim3, T{}, std::move(properties));
  }

  [[nodiscard]] static PooledArray4D ones(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                          ksj::memory::AllocationProperties properties = {}) {
    return constant(dim0, dim1, dim2, dim3, T{1}, std::move(properties));
  }

  template <typename StartT, typename StopT>
  [[nodiscard]] static PooledArray4D linspace(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3,
                                              const StartT& start, const StopT& stop,
                                              ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledArray4D(dim0, dim1, dim2, dim3, std::move(properties));
    output.set_linspace(start, stop);
    return output;
  }

  template <typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
  [[nodiscard]] static PooledArray4D uniform_random(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                    std::size_t dim3, const LowerT& lower, const UpperT& upper,
                                                    UniformRandomBitGenerator& generator,
                                                    ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledArray4D(dim0, dim1, dim2, dim3, std::move(properties));
    output.set_uniform_random(lower, upper, generator);
    return output;
  }

  template <typename LowerT, typename UpperT>
  [[nodiscard]] static PooledArray4D uniform_random(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                    std::size_t dim3, const LowerT& lower, const UpperT& upper,
                                                    ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledArray4D(dim0, dim1, dim2, dim3, std::move(properties));
    output.set_uniform_random(lower, upper);
    return output;
  }

  PooledArray4D(const PooledArray4D&) = delete;
  PooledArray4D& operator=(const PooledArray4D&) = delete;
  PooledArray4D(PooledArray4D&&) noexcept = default;
  PooledArray4D& operator=(PooledArray4D&&) noexcept = default;

  void swap(PooledArray4D& other) noexcept {
    using std::swap;
    swap(buffer_, other.buffer_);
    swap(dim0_, other.dim0_);
    swap(dim1_, other.dim1_);
    swap(dim2_, other.dim2_);
    swap(dim3_, other.dim3_);
  }

  [[nodiscard]] T* data() noexcept { return buffer_.data(); }
  [[nodiscard]] const T* data() const noexcept { return buffer_.data(); }
  [[nodiscard]] std::size_t dim0() const noexcept { return dim0_; }
  [[nodiscard]] std::size_t dim1() const noexcept { return dim1_; }
  [[nodiscard]] std::size_t dim2() const noexcept { return dim2_; }
  [[nodiscard]] std::size_t dim3() const noexcept { return dim3_; }
  [[nodiscard]] std::size_t size() const noexcept { return dim0_ * dim1_ * dim2_ * dim3_; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size() * sizeof(T); }
  [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.capacity(); }
  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return buffer_.capacity_bytes(); }
  [[nodiscard]] bool empty() const noexcept { return dim0_ == 0 || dim1_ == 0 || dim2_ == 0 || dim3_ == 0; }
  [[nodiscard]] bool is_contiguous() const noexcept { return true; }
  [[nodiscard]] std::size_t dim0_stride() const noexcept { return dim1_ * dim2_ * dim3_; }
  [[nodiscard]] std::size_t dim1_stride() const noexcept { return dim2_ * dim3_; }
  [[nodiscard]] std::size_t dim2_stride() const noexcept { return dim3_; }
  [[nodiscard]] std::size_t dim3_stride() const noexcept { return 1U; }
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
        throw std::out_of_range("PooledArray4D extent axis must be 0, 1, 2, or 3");
    }
  }

  [[nodiscard]] std::size_t extent(const Dim dim) const { return extent(dim_index(dim)); }

  [[nodiscard]] std::size_t stride(const std::size_t axis) const {
    switch (axis) {
      case 0:
        return dim0_stride();
      case 1:
        return dim1_stride();
      case 2:
        return dim2_stride();
      case 3:
        return dim3_stride();
      default:
        throw std::out_of_range("PooledArray4D stride axis must be 0, 1, 2, or 3");
    }
  }

  [[nodiscard]] std::size_t stride(const Dim dim) const { return stride(dim_index(dim)); }

  [[nodiscard]] reference operator[](std::size_t index) noexcept { return data()[index]; }
  [[nodiscard]] const_reference operator[](std::size_t index) const noexcept { return data()[index]; }
  [[nodiscard]] reference operator()(std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3) {
    if (i0 >= dim0_ || i1 >= dim1_ || i2 >= dim2_ || i3 >= dim3_) {
      throw std::out_of_range("PooledArray4D index is outside the array");
    }
    return (*this)[((i0 * dim1_ + i1) * dim2_ + i2) * dim3_ + i3];
  }

  [[nodiscard]] const_reference operator()(std::size_t i0, std::size_t i1, std::size_t i2, std::size_t i3) const {
    if (i0 >= dim0_ || i1 >= dim1_ || i2 >= dim2_ || i3 >= dim3_) {
      throw std::out_of_range("PooledArray4D index is outside the array");
    }
    return (*this)[((i0 * dim1_ + i1) * dim2_ + i2) * dim3_ + i3];
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

  template <typename Value> PooledArray4D& fill(const Value& value) {
    ksj::array::fill(view(), value);
    return *this;
  }

  template <typename Value> PooledArray4D& set_constant(const Value& value) { return fill(value); }

  PooledArray4D& set_zero() { return fill(T{}); }
  PooledArray4D& set_ones() { return fill(T{1}); }

  template <typename StartT, typename StopT> PooledArray4D& set_linspace(const StartT& start, const StopT& stop) {
    ksj::array::fill_linspace(view(), start, stop);
    return *this;
  }

  template <typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
  PooledArray4D& set_uniform_random(const LowerT& lower, const UpperT& upper, UniformRandomBitGenerator& generator) {
    ksj::array::fill_uniform_random(view(), lower, upper, generator);
    return *this;
  }

  template <typename LowerT, typename UpperT>
  PooledArray4D& set_uniform_random(const LowerT& lower, const UpperT& upper) {
    ksj::array::fill_uniform_random(view(), lower, upper);
    return *this;
  }

  template <typename InputT> PooledArray4D& assign(Array4DView<InputT> input) {
    resize(input.dim0(), input.dim1(), input.dim2(), input.dim3());
    copy(input, view());
    return *this;
  }

  template <typename InputT> PooledArray4D& assign(const PooledArray4D<InputT>& input) { return assign(input.view()); }

  template <typename UnaryFunction> PooledArray4D& transform_in_place(UnaryFunction&& function) {
    transform(view(), view(), std::forward<UnaryFunction>(function));
    return *this;
  }

  template <typename Acc> [[nodiscard]] Acc sum(Acc init) const { return ksj::array::sum(view(), init); }
  [[nodiscard]] value_type sum() const { return ksj::array::sum(view()); }
  [[nodiscard]] auto mean() const { return ksj::array::mean(view()); }
  [[nodiscard]] value_type min() const { return ksj::array::min(view()); }
  [[nodiscard]] value_type max() const { return ksj::array::max(view()); }
  [[nodiscard]] auto minmax() const { return ksj::array::minmax(view()); }
  [[nodiscard]] auto squared_norm() const { return ksj::array::squared_norm(view()); }
  [[nodiscard]] auto norm() const { return ksj::array::norm(view()); }
  [[nodiscard]] std::size_t count_nonzero() const { return ksj::array::count_nonzero(view()); }

  [[nodiscard]] auto real() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledArray4D<output_type>(dim0_, dim1_, dim2_, dim3_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return value.real();
    });
    return output;
  }

  [[nodiscard]] auto imag() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledArray4D<output_type>(dim0_, dim1_, dim2_, dim3_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return value.imag();
    });
    return output;
  }

  [[nodiscard]] auto magnitude() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledArray4D<output_type>(dim0_, dim1_, dim2_, dim3_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::abs(value);
    });
    return output;
  }

  [[nodiscard]] auto abs() const
    requires(is_complex_v<T>)
  {
    return magnitude();
  }

  [[nodiscard]] auto modulus() const
    requires(is_complex_v<T>)
  {
    return magnitude();
  }

  [[nodiscard]] auto squared_magnitude() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledArray4D<output_type>(dim0_, dim1_, dim2_, dim3_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::norm(value);
    });
    return output;
  }

  [[nodiscard]] auto phase() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledArray4D<output_type>(dim0_, dim1_, dim2_, dim3_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::arg(value);
    });
    return output;
  }

  [[nodiscard]] auto conjugate() const
    requires(is_complex_v<T>)
  {
    auto output = PooledArray4D<T>(dim0_, dim1_, dim2_, dim3_);
    transform(view(), output.view(), [](const T& value) {
      return std::conj(value);
    });
    return output;
  }

  void clear() noexcept {
    buffer_.clear();
    dim0_ = 0U;
    dim1_ = 0U;
    dim2_ = 0U;
    dim3_ = 0U;
  }

  void release() noexcept {
    buffer_.release();
    dim0_ = 0U;
    dim1_ = 0U;
    dim2_ = 0U;
    dim3_ = 0U;
  }

  void resize(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2, std::size_t new_dim3,
              ksj::memory::AllocationProperties properties = {}) {
    const auto count =
      detail::checked_count(detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2), new_dim3);
    if (count <= capacity()) {
      buffer_.resize_count(count);
      dim0_ = new_dim0;
      dim1_ = new_dim1;
      dim2_ = new_dim2;
      dim3_ = new_dim3;
      return;
    }
    *this = PooledArray4D(new_dim0, new_dim1, new_dim2, new_dim3, std::move(properties));
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

  void reserve(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2, std::size_t new_dim3,
               ksj::memory::AllocationProperties properties = {}) {
    reserve_elements(
      detail::checked_count(detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2), new_dim3),
      std::move(properties));
  }

  void reshape(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2, std::size_t new_dim3) {
    const auto count =
      detail::checked_count(detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2), new_dim3);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape cannot change the element count");
    buffer_.resize_count(count);
    dim0_ = new_dim0;
    dim1_ = new_dim1;
    dim2_ = new_dim2;
    dim3_ = new_dim3;
  }

  [[nodiscard]] view_type view() noexcept { return view_type(data(), dim0_, dim1_, dim2_, dim3_); }

  [[nodiscard]] const_view_type view() const noexcept { return const_view_type(data(), dim0_, dim1_, dim2_, dim3_); }

  [[nodiscard]] VectorView<T> reshape_view(std::size_t element_count) {
    detail::validate_reshape_count(size(), element_count, "PooledArray4D reshape_view cannot change the element count");
    return VectorView<T>(data(), element_count);
  }

  [[nodiscard]] VectorView<const T> reshape_view(std::size_t element_count) const {
    detail::validate_reshape_count(size(), element_count, "PooledArray4D reshape_view cannot change the element count");
    return VectorView<const T>(data(), element_count);
  }

  [[nodiscard]] MatrixView<T> reshape_view(std::size_t new_rows, std::size_t new_cols) {
    const auto count = detail::checked_count(new_rows, new_cols);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape_view cannot change the element count");
    return MatrixView<T>(data(), new_rows, new_cols);
  }

  [[nodiscard]] MatrixView<const T> reshape_view(std::size_t new_rows, std::size_t new_cols) const {
    const auto count = detail::checked_count(new_rows, new_cols);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape_view cannot change the element count");
    return MatrixView<const T>(data(), new_rows, new_cols);
  }

  [[nodiscard]] CubeView<T> reshape_view(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2) {
    const auto count = detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape_view cannot change the element count");
    return CubeView<T>(data(), new_dim0, new_dim1, new_dim2);
  }

  [[nodiscard]] CubeView<const T> reshape_view(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2) const {
    const auto count = detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape_view cannot change the element count");
    return CubeView<const T>(data(), new_dim0, new_dim1, new_dim2);
  }

  [[nodiscard]] view_type reshape_view(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2,
                                       std::size_t new_dim3) {
    const auto count =
      detail::checked_count(detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2), new_dim3);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape_view cannot change the element count");
    return view_type(data(), new_dim0, new_dim1, new_dim2, new_dim3);
  }

  [[nodiscard]] const_view_type reshape_view(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2,
                                             std::size_t new_dim3) const {
    const auto count =
      detail::checked_count(detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2), new_dim3);
    detail::validate_reshape_count(size(), count, "PooledArray4D reshape_view cannot change the element count");
    return const_view_type(data(), new_dim0, new_dim1, new_dim2, new_dim3);
  }

  template <typename... Args> [[nodiscard]] decltype(auto) subview(Args&&... args) {
    return view().subview(std::forward<Args>(args)...);
  }

  template <typename... Args> [[nodiscard]] decltype(auto) subview(Args&&... args) const {
    return view().subview(std::forward<Args>(args)...);
  }

  [[nodiscard]] ksj::memory::PooledBuffer<T>& buffer() noexcept { return buffer_; }
  [[nodiscard]] const ksj::memory::PooledBuffer<T>& buffer() const noexcept { return buffer_; }

private:
  ksj::memory::PooledBuffer<T> buffer_{};
  std::size_t dim0_{0};
  std::size_t dim1_{0};
  std::size_t dim2_{0};
  std::size_t dim3_{0};
};

template <typename T> void swap(PooledArray4D<T>& lhs, PooledArray4D<T>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename T>
[[nodiscard]] PooledArray4D<T> make_pooled_array4d(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                   std::size_t dim3,
                                                   ksj::memory::AllocationProperties properties = {}) {
  return PooledArray4D<T>(dim0, dim1, dim2, dim3, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledArray4D<std::remove_const_t<T>>
make_pooled_array4d(Array4DView<T> input, ksj::memory::AllocationProperties properties = {}) {
  using value_type = std::remove_const_t<T>;
  auto output =
    make_pooled_array4d<value_type>(input.dim0(), input.dim1(), input.dim2(), input.dim3(), std::move(properties));
  copy(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledArray4D<T> make_pooled_array4d(const PooledArray4D<T>& input,
                                                   ksj::memory::AllocationProperties properties = {}) {
  return make_pooled_array4d(input.view(), std::move(properties));
}

} // namespace ksj::array
