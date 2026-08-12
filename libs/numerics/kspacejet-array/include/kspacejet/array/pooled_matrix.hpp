#pragma once

/// Owning two-dimensional row-major storage with matrix View, row, column, and subview accessors.

#include "kspacejet/array/detail/pooled_dense2d.hpp"
#include "kspacejet/array/copy.hpp"
#include "kspacejet/array/initialization.hpp"
#include "kspacejet/array/reductions.hpp"
#include "kspacejet/array/transforms.hpp"
#include "kspacejet/memory/allocation_properties.hpp"

#include <complex>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace ksj::array {

template <typename T> class PooledMatrix final : private detail::PooledDense2D<T> {
  using dense_base_type = detail::PooledDense2D<T>;

public:
  using value_type = typename dense_base_type::value_type;
  using view_type = MatrixView<T>;
  using const_view_type = MatrixView<const T>;
  using vector_view_type = typename view_type::vector_view_type;
  using const_vector_view_type = typename const_view_type::vector_view_type;

  PooledMatrix() = default;
  using dense_base_type::dense_base_type;
  using dense_base_type::shape;

  template <typename Value>
  [[nodiscard]] static PooledMatrix constant(std::size_t rows, std::size_t cols, const Value& value,
                                             ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledMatrix(rows, cols, std::move(properties));
    output.set_constant(value);
    return output;
  }

  [[nodiscard]] static PooledMatrix zeros(std::size_t rows, std::size_t cols,
                                          ksj::memory::AllocationProperties properties = {}) {
    return constant(rows, cols, T{}, std::move(properties));
  }

  [[nodiscard]] static PooledMatrix ones(std::size_t rows, std::size_t cols,
                                         ksj::memory::AllocationProperties properties = {}) {
    return constant(rows, cols, T{1}, std::move(properties));
  }

  [[nodiscard]] static PooledMatrix identity(std::size_t rows, std::size_t cols,
                                             ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledMatrix(rows, cols, std::move(properties));
    output.set_identity();
    return output;
  }

  [[nodiscard]] static PooledMatrix eye(std::size_t rows, std::size_t cols,
                                        ksj::memory::AllocationProperties properties = {}) {
    return identity(rows, cols, std::move(properties));
  }

  template <typename StartT, typename StopT>
  [[nodiscard]] static PooledMatrix linspace(std::size_t rows, std::size_t cols, const StartT& start, const StopT& stop,
                                             ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledMatrix(rows, cols, std::move(properties));
    output.set_linspace(start, stop);
    return output;
  }

  template <typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
  [[nodiscard]] static PooledMatrix uniform_random(std::size_t rows, std::size_t cols, const LowerT& lower,
                                                   const UpperT& upper, UniformRandomBitGenerator& generator,
                                                   ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledMatrix(rows, cols, std::move(properties));
    output.set_uniform_random(lower, upper, generator);
    return output;
  }

  template <typename LowerT, typename UpperT>
  [[nodiscard]] static PooledMatrix uniform_random(std::size_t rows, std::size_t cols, const LowerT& lower,
                                                   const UpperT& upper,
                                                   ksj::memory::AllocationProperties properties = {}) {
    auto output = PooledMatrix(rows, cols, std::move(properties));
    output.set_uniform_random(lower, upper);
    return output;
  }

  PooledMatrix(const PooledMatrix&) = delete;
  PooledMatrix& operator=(const PooledMatrix&) = delete;
  PooledMatrix(PooledMatrix&&) noexcept = default;
  PooledMatrix& operator=(PooledMatrix&&) noexcept = default;

  void swap(PooledMatrix& other) noexcept { dense_base_type::swap(other); }

  [[nodiscard]] view_type view() noexcept { return view_type(data(), rows(), cols()); }
  [[nodiscard]] const_view_type view() const noexcept { return const_view_type(data(), rows(), cols()); }

  [[nodiscard]] VectorView<T> reshape_view(std::size_t element_count) {
    detail::validate_reshape_count(size(), element_count, "PooledMatrix reshape_view cannot change the element count");
    return VectorView<T>(data(), element_count);
  }

  [[nodiscard]] VectorView<const T> reshape_view(std::size_t element_count) const {
    detail::validate_reshape_count(size(), element_count, "PooledMatrix reshape_view cannot change the element count");
    return VectorView<const T>(data(), element_count);
  }

  [[nodiscard]] view_type reshape_view(std::size_t new_rows, std::size_t new_cols) {
    const auto count = detail::checked_count(new_rows, new_cols);
    detail::validate_reshape_count(size(), count, "PooledMatrix reshape_view cannot change the element count");
    return view_type(data(), new_rows, new_cols);
  }

  [[nodiscard]] const_view_type reshape_view(std::size_t new_rows, std::size_t new_cols) const {
    const auto count = detail::checked_count(new_rows, new_cols);
    detail::validate_reshape_count(size(), count, "PooledMatrix reshape_view cannot change the element count");
    return const_view_type(data(), new_rows, new_cols);
  }

  [[nodiscard]] CubeView<T> reshape_view(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2) {
    const auto count = detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2);
    detail::validate_reshape_count(size(), count, "PooledMatrix reshape_view cannot change the element count");
    return CubeView<T>(data(), new_dim0, new_dim1, new_dim2);
  }

  [[nodiscard]] CubeView<const T> reshape_view(std::size_t new_dim0, std::size_t new_dim1, std::size_t new_dim2) const {
    const auto count = detail::checked_count(detail::checked_count(new_dim0, new_dim1), new_dim2);
    detail::validate_reshape_count(size(), count, "PooledMatrix reshape_view cannot change the element count");
    return CubeView<const T>(data(), new_dim0, new_dim1, new_dim2);
  }

  [[nodiscard]] vector_view_type row(std::size_t row_index) noexcept { return view().row(row_index); }
  [[nodiscard]] const_vector_view_type row(std::size_t row_index) const noexcept { return view().row(row_index); }

  [[nodiscard]] vector_view_type col(std::size_t col_index) noexcept { return view().col(col_index); }
  [[nodiscard]] const_vector_view_type col(std::size_t col_index) const noexcept { return view().col(col_index); }

  template <typename... Args> [[nodiscard]] decltype(auto) subview(Args&&... args) {
    return view().subview(std::forward<Args>(args)...);
  }

  template <typename... Args> [[nodiscard]] decltype(auto) subview(Args&&... args) const {
    return view().subview(std::forward<Args>(args)...);
  }

  template <typename Value> PooledMatrix& fill(const Value& value) {
    ksj::array::fill(view(), value);
    return *this;
  }

  template <typename Value> PooledMatrix& set_constant(const Value& value) { return fill(value); }

  PooledMatrix& set_zero() { return fill(T{}); }
  PooledMatrix& set_ones() { return fill(T{1}); }

  PooledMatrix& set_identity() {
    ksj::array::set_identity(view());
    return *this;
  }

  template <typename StartT, typename StopT> PooledMatrix& set_linspace(const StartT& start, const StopT& stop) {
    ksj::array::fill_linspace(view(), start, stop);
    return *this;
  }

  template <typename LowerT, typename UpperT, typename UniformRandomBitGenerator>
  PooledMatrix& set_uniform_random(const LowerT& lower, const UpperT& upper, UniformRandomBitGenerator& generator) {
    ksj::array::fill_uniform_random(view(), lower, upper, generator);
    return *this;
  }

  template <typename LowerT, typename UpperT>
  PooledMatrix& set_uniform_random(const LowerT& lower, const UpperT& upper) {
    ksj::array::fill_uniform_random(view(), lower, upper);
    return *this;
  }

  template <typename InputT> PooledMatrix& assign(MatrixView<InputT> input) {
    resize(input.rows(), input.cols());
    copy(input, view());
    return *this;
  }

  template <typename InputT> PooledMatrix& assign(const PooledMatrix<InputT>& input) { return assign(input.view()); }

  template <typename UnaryFunction> PooledMatrix& transform_in_place(UnaryFunction&& function) {
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
    auto output = PooledMatrix<output_type>(rows(), cols());
    transform(view(), output.view(), [](const T& value) -> output_type {
      return value.real();
    });
    return output;
  }

  [[nodiscard]] auto imag() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledMatrix<output_type>(rows(), cols());
    transform(view(), output.view(), [](const T& value) -> output_type {
      return value.imag();
    });
    return output;
  }

  [[nodiscard]] auto magnitude() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledMatrix<output_type>(rows(), cols());
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
    auto output = PooledMatrix<output_type>(rows(), cols());
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::norm(value);
    });
    return output;
  }

  [[nodiscard]] auto phase() const
    requires(is_complex_v<T>)
  {
    using output_type = real_scalar_t<T>;
    auto output = PooledMatrix<output_type>(rows(), cols());
    transform(view(), output.view(), [](const T& value) -> output_type {
      return std::arg(value);
    });
    return output;
  }

  [[nodiscard]] auto conjugate() const
    requires(is_complex_v<T>)
  {
    auto output = PooledMatrix<T>(rows(), cols());
    transform(view(), output.view(), [](const T& value) {
      return std::conj(value);
    });
    return output;
  }

  using dense_base_type::back;
  using dense_base_type::begin;
  using dense_base_type::buffer;
  using dense_base_type::capacity;
  using dense_base_type::capacity_bytes;
  using dense_base_type::cbegin;
  using dense_base_type::cend;
  using dense_base_type::clear;
  using dense_base_type::col_stride;
  using dense_base_type::cols;
  using dense_base_type::data;
  using dense_base_type::empty;
  using dense_base_type::end;
  using dense_base_type::extent;
  using dense_base_type::front;
  using dense_base_type::is_contiguous;
  using dense_base_type::operator();
  using dense_base_type::operator[];
  using dense_base_type::release;
  using dense_base_type::reserve;
  using dense_base_type::reserve_elements;
  using dense_base_type::reshape;
  using dense_base_type::resize;
  using dense_base_type::row_stride;
  using dense_base_type::row_stride_bytes;
  using dense_base_type::row_stride_elements;
  using dense_base_type::rows;
  using dense_base_type::size;
  using dense_base_type::size_bytes;
  using dense_base_type::stride;
};

template <typename T> void swap(PooledMatrix<T>& lhs, PooledMatrix<T>& rhs) noexcept {
  lhs.swap(rhs);
}

template <typename T>
[[nodiscard]] PooledMatrix<T> make_pooled_matrix(std::size_t rows, std::size_t cols,
                                                 ksj::memory::AllocationProperties properties = {}) {
  return PooledMatrix<T>(rows, cols, std::move(properties));
}

template <typename T>
[[nodiscard]] PooledMatrix<std::remove_const_t<T>>
make_pooled_matrix(MatrixView<T> input, ksj::memory::AllocationProperties properties = {}) {
  using value_type = std::remove_const_t<T>;
  auto output = make_pooled_matrix<value_type>(input.rows(), input.cols(), std::move(properties));
  copy(input, output.view());
  return output;
}

template <typename T>
[[nodiscard]] PooledMatrix<T> make_pooled_matrix(const PooledMatrix<T>& input,
                                                 ksj::memory::AllocationProperties properties = {}) {
  return make_pooled_matrix(input.view(), std::move(properties));
}

} // namespace ksj::array
