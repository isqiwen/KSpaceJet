#pragma once

/// Owning contiguous one-dimensional KSpaceJet row-major storage with non-owning View accessors.

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
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::array {

template <typename T> class PooledVector {
public:
  static_assert(detail::pooled_storage_scalar_v<T>,
                "PooledVector<T> requires a non-const trivially destructible scalar type");

  using value_type = T;
  using view_type = VectorView<T>;
  using const_view_type = VectorView<const T>;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reference = T&;
  using const_reference = const T&;

  PooledVector() = default;

  explicit PooledVector(std::size_t size, ksj::memory::AllocationProperties properties = {})
      : buffer_(ksj::memory::allocate_array<T>(size, std::move(properties))), size_(size) {}

  template <typename Value>
  [[nodiscard]] static PooledVector constant(std::size_t size, const Value& value,
                                             ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledVector(size, std::move(properties));
    output.set_constant(value);
    return output;
  }

  [[nodiscard]] static PooledVector zeros(std::size_t size, ksj::memory::AllocationProperties properties = {}) {
    return constant(size, T{}, std::move(properties));
  }

  [[nodiscard]] static PooledVector ones(std::size_t size, ksj::memory::AllocationProperties properties = {}) {
    return constant(size, T{1}, std::move(properties));
  }

  template <typename StartT, typename StopT>
  [[nodiscard]] static PooledVector linspace(std::size_t size, const StartT& start, const StopT& stop,
                                             ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledVector(size, std::move(properties));
    output.set_linspace(start, stop);
    return output;
  }

  template <typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
  [[nodiscard]] static PooledVector uniform_random(std::size_t size, const LowerT& lower, const UpperT& upper,
                                                   UniformRandomBitGenerator& generator,
                                                   ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledVector(size, std::move(properties));
    output.set_uniform_random(lower, upper, generator);
    return output;
  }

  template <typename LowerT, typename UpperT>
  [[nodiscard]] static PooledVector uniform_random(std::size_t size, const LowerT& lower, const UpperT& upper,
                                                   ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledVector(size, std::move(properties));
    output.set_uniform_random(lower, upper);
    return output;
  }

  PooledVector(const PooledVector&) = delete;
  PooledVector& operator=(const PooledVector&) = delete;
  PooledVector(PooledVector&&) noexcept = default;
  PooledVector& operator=(PooledVector&&) noexcept = default;

  void swap(PooledVector& other) noexcept {
    using std::swap;
    swap(buffer_, other.buffer_);
    swap(size_, other.size_);
  }

  [[nodiscard]] T* data() noexcept { return buffer_.data(); }
  [[nodiscard]] const T* data() const noexcept { return buffer_.data(); }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::size_t size_bytes() const noexcept { return size_ * sizeof(T); }
  [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.capacity(); }
  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return buffer_.capacity_bytes(); }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] bool is_contiguous() const noexcept { return true; }
  [[nodiscard]] std::size_t extent() const noexcept { return size_; }
  [[nodiscard]] std::size_t stride() const noexcept { return 1U; }
  [[nodiscard]] std::size_t extent(const Dim dim) const {
    detail::validate_supported_dim(dim, Dim::dim0, "PooledVector extent dim must be Dim::dim0");
    return size_;
  }
  [[nodiscard]] std::size_t stride(const Dim dim) const {
    detail::validate_supported_dim(dim, Dim::dim0, "PooledVector stride dim must be Dim::dim0");
    return 1U;
  }
  [[nodiscard]] Shape<1U> shape() const noexcept { return Shape<1U>(size_); }

  [[nodiscard]] reference operator[](std::size_t index) noexcept { return data()[index]; }
  [[nodiscard]] const_reference operator[](std::size_t index) const noexcept { return data()[index]; }
  [[nodiscard]] reference operator()(std::size_t index) {
    if (index >= size_) {
      throw std::out_of_range("PooledVector index is outside the vector");
    }
    return (*this)[index];
  }
  [[nodiscard]] const_reference operator()(std::size_t index) const {
    if (index >= size_) {
      throw std::out_of_range("PooledVector index is outside the vector");
    }
    return (*this)[index];
  }
  [[nodiscard]] reference at(std::size_t index) { return (*this)(index); }
  [[nodiscard]] const_reference at(std::size_t index) const { return (*this)(index); }
  [[nodiscard]] reference front() noexcept { return (*this)[0U]; }
  [[nodiscard]] const_reference front() const noexcept { return (*this)[0U]; }
  [[nodiscard]] reference back() noexcept { return (*this)[size_ - 1U]; }
  [[nodiscard]] const_reference back() const noexcept { return (*this)[size_ - 1U]; }

  [[nodiscard]] pointer begin() noexcept { return data(); }
  [[nodiscard]] const_pointer begin() const noexcept { return data(); }
  [[nodiscard]] const_pointer cbegin() const noexcept { return data(); }
  [[nodiscard]] pointer end() noexcept { return empty() ? data() : data() + size_; }
  [[nodiscard]] const_pointer end() const noexcept { return empty() ? data() : data() + size_; }
  [[nodiscard]] const_pointer cend() const noexcept { return end(); }
  [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
  [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

  template <typename Value> PooledVector& fill(const Value& value) {
    ksj::array::fill(view(), value);
    return *this;
  }

  template <typename Value> PooledVector& set_constant(const Value& value) { return fill(value); }

  PooledVector& set_zero() { return fill(T{}); }
  PooledVector& set_ones() { return fill(T{1}); }

  template <typename StartT, typename StopT> PooledVector& set_linspace(const StartT& start, const StopT& stop) {
    ksj::array::fill_linspace(view(), start, stop);
    return *this;
  }

  template <typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
  PooledVector& set_uniform_random(const LowerT& lower, const UpperT& upper, UniformRandomBitGenerator& generator) {
    ksj::array::fill_uniform_random(view(), lower, upper, generator);
    return *this;
  }

  template <typename LowerT, typename UpperT>
  PooledVector& set_uniform_random(const LowerT& lower, const UpperT& upper) {
    ksj::array::fill_uniform_random(view(), lower, upper);
    return *this;
  }

  template <typename InputT> PooledVector& assign(VectorView<InputT> input) {
    resize(input.size());
    copy(input, view());
    return *this;
  }

  template <typename InputT> PooledVector& assign(const PooledVector<InputT>& input) { return assign(input.view()); }

  template <typename Value> PooledVector& assign(std::size_t size, const Value& value) {
    resize(size);
    fill(value);
    return *this;
  }

  template <typename UnaryFunction> PooledVector& transform_in_place(UnaryFunction&& function) {
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
    auto output = PooledVector<output_type>(size_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return value.real();
    });
    return output;
  }

  [[nodiscard]] auto imag() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledVector<output_type>(size_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return value.imag();
    });
    return output;
  }

  [[nodiscard]] auto magnitude() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledVector<output_type>(size_);
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
    auto output = PooledVector<output_type>(size_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::norm(value);
    });
    return output;
  }

  [[nodiscard]] auto phase() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledVector<output_type>(size_);
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::arg(value);
    });
    return output;
  }

  [[nodiscard]] auto conjugate() const
    requires(is_complex_v<T>)
  {
    auto output = PooledVector<T>(size_);
    transform(view(), output.view(), [](const T& value) {
      return std::conj(value);
    });
    return output;
  }

  void clear() noexcept {
    buffer_.clear();
    size_ = 0U;
  }

  void release() noexcept {
    buffer_.release();
    size_ = 0U;
  }

  void resize(std::size_t size, ksj::memory::AllocationProperties properties = {}) {
    if (size <= capacity()) {
      buffer_.resize_count(size);
      size_ = size;
      return;
    }
    *this = PooledVector(size, std::move(properties));
  }

  template <typename Value>
  void resize(std::size_t size, const Value& value, ksj::memory::AllocationProperties properties = {}) {
    const auto old_size = size_;
    if (size > capacity()) {
      reserve(size, std::move(properties));
    }
    buffer_.resize_count(size);
    size_ = size;
    if (size > old_size) {
      std::fill(data() + old_size, data() + size, static_cast<T>(value));
    }
  }

  void reserve(std::size_t element_capacity, ksj::memory::AllocationProperties properties = {}) {
    if (element_capacity <= capacity()) {
      return;
    }

    auto new_buffer = ksj::memory::allocate_array<T>(element_capacity, std::move(properties));
    std::copy_n(data(), size_, new_buffer.data());
    new_buffer.resize_count(size_);
    buffer_ = std::move(new_buffer);
  }

  [[nodiscard]] view_type view() noexcept { return view_type(data(), size_); }
  [[nodiscard]] const_view_type view() const noexcept { return const_view_type(data(), size_); }

  [[nodiscard]] MatrixView<T> reshape_view(std::size_t rows, std::size_t cols) {
    const auto count = detail::checked_count(rows, cols);
    detail::validate_reshape_count(size_, count, "PooledVector reshape_view cannot change the element count");
    return MatrixView<T>(data(), rows, cols);
  }

  [[nodiscard]] MatrixView<const T> reshape_view(std::size_t rows, std::size_t cols) const {
    const auto count = detail::checked_count(rows, cols);
    detail::validate_reshape_count(size_, count, "PooledVector reshape_view cannot change the element count");
    return MatrixView<const T>(data(), rows, cols);
  }

  [[nodiscard]] CubeView<T> reshape_view(std::size_t dim0, std::size_t dim1, std::size_t dim2) {
    const auto count = detail::checked_count(detail::checked_count(dim0, dim1), dim2);
    detail::validate_reshape_count(size_, count, "PooledVector reshape_view cannot change the element count");
    return CubeView<T>(data(), dim0, dim1, dim2);
  }

  [[nodiscard]] CubeView<const T> reshape_view(std::size_t dim0, std::size_t dim1, std::size_t dim2) const {
    const auto count = detail::checked_count(detail::checked_count(dim0, dim1), dim2);
    detail::validate_reshape_count(size_, count, "PooledVector reshape_view cannot change the element count");
    return CubeView<const T>(data(), dim0, dim1, dim2);
  }

  [[nodiscard]] Array4DView<T> reshape_view(std::size_t dim0, std::size_t dim1, std::size_t dim2, std::size_t dim3) {
    const auto count = detail::checked_count(detail::checked_count(detail::checked_count(dim0, dim1), dim2), dim3);
    detail::validate_reshape_count(size_, count, "PooledVector reshape_view cannot change the element count");
    return Array4DView<T>(data(), dim0, dim1, dim2, dim3);
  }

  [[nodiscard]] Array4DView<const T> reshape_view(std::size_t dim0, std::size_t dim1, std::size_t dim2,
                                                  std::size_t dim3) const {
    const auto count = detail::checked_count(detail::checked_count(detail::checked_count(dim0, dim1), dim2), dim3);
    detail::validate_reshape_count(size_, count, "PooledVector reshape_view cannot change the element count");
    return Array4DView<const T>(data(), dim0, dim1, dim2, dim3);
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
  std::size_t size_{0};
};

template <typename T> void swap(PooledVector<T>& lhs, PooledVector<T>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename T>
[[nodiscard]] PooledVector<T> make_pooled_vector(std::size_t size, ksj::memory::AllocationProperties properties = {}) {
  return PooledVector<T>(size, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledVector<std::remove_const_t<T>>
make_pooled_vector(VectorView<T> input, ksj::memory::AllocationProperties properties = {}) {
  using value_type = std::remove_const_t<T>;
  auto output = make_pooled_vector<value_type>(input.size(), std::move(properties));
  copy(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledVector<T> make_pooled_vector(const PooledVector<T>& input,
                                                 ksj::memory::AllocationProperties properties = {}) {
  return make_pooled_vector(input.view(), std::move(properties));
}

} // namespace ksj::array
