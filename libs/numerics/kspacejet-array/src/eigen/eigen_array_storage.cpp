#include "kspacejet/array/detail/eigen/eigen_array_storage.hpp"

#include <Eigen/Core>

#include <complex>
#include <cstdint>
#include <limits>

namespace ksj::array::detail::eigen {
namespace {

template <typename T> using ArrayMap = Eigen::Map<Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

template <typename T> using ConstArrayMap = Eigen::Map<const Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

[[nodiscard]] bool fits_eigen_index(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max());
}

template <typename T> [[nodiscard]] bool valid_contiguous_view(VectorView<T> view) noexcept {
  return view.is_contiguous() && fits_eigen_index(view.size());
}

template <typename T> [[nodiscard]] auto map(VectorView<T> view) {
  return ArrayMap<T>(view.data(), static_cast<Eigen::Index>(view.size()));
}

template <typename T> [[nodiscard]] auto const_map(VectorView<const T> view) {
  return ConstArrayMap<T>(view.data(), static_cast<Eigen::Index>(view.size()));
}

template <typename T> [[nodiscard]] std::uintptr_t address(T* pointer) noexcept {
  return reinterpret_cast<std::uintptr_t>(pointer);
}

template <typename T> [[nodiscard]] bool contiguous_ranges_overlap(VectorView<const T> input, VectorView<T> output) {
  if (input.empty()) {
    return false;
  }
  const auto input_begin = address(input.data());
  const auto input_end = input_begin + input.size() * sizeof(T);
  const auto output_begin = address(output.data());
  const auto output_end = output_begin + output.size() * sizeof(T);
  return input_begin < output_end && output_begin < input_end;
}

template <typename T> [[nodiscard]] bool fill_impl(VectorView<T> output, const T value) {
  if (!valid_contiguous_view(output)) {
    return false;
  }
  map(output).setConstant(value);
  return true;
}

template <typename T> [[nodiscard]] bool copy_impl(VectorView<const T> input, VectorView<T> output) {
  if (input.size() != output.size()) {
    return false;
  }
  if (!valid_contiguous_view(input) || !valid_contiguous_view(output)) {
    return false;
  }
  if (input.data() == output.data()) {
    return true;
  }
  if (contiguous_ranges_overlap(input, output)) {
    return false;
  }
  map(output) = const_map(input);
  return true;
}

} // namespace

bool fill(VectorView<ksj::base::f32> output, const ksj::base::f32 value) {
  return fill_impl(output, value);
}

bool fill(VectorView<ksj::base::f64> output, const ksj::base::f64 value) {
  return fill_impl(output, value);
}

bool fill(VectorView<ksj::base::cf32> output, const ksj::base::cf32 value) {
  return fill_impl(output, value);
}

bool fill(VectorView<ksj::base::cf64> output, const ksj::base::cf64 value) {
  return fill_impl(output, value);
}

bool copy(VectorView<const ksj::base::f32> input, VectorView<ksj::base::f32> output) {
  return copy_impl(input, output);
}

bool copy(VectorView<const ksj::base::f64> input, VectorView<ksj::base::f64> output) {
  return copy_impl(input, output);
}

bool copy(VectorView<const ksj::base::cf32> input, VectorView<ksj::base::cf32> output) {
  return copy_impl(input, output);
}

bool copy(VectorView<const ksj::base::cf64> input, VectorView<ksj::base::cf64> output) {
  return copy_impl(input, output);
}

} // namespace ksj::array::detail::eigen
