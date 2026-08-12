#include "kspacejet/array/detail/eigen/eigen_array_reductions.hpp"

#include <Eigen/Core>

#include <limits>

namespace ksj::array::detail::eigen {
namespace {

template <typename T> using ConstArrayMap = Eigen::Map<const Eigen::Array<T, Eigen::Dynamic, 1>, Eigen::Unaligned>;

[[nodiscard]] bool fits_eigen_index(const std::size_t size) noexcept {
  return size <= static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max());
}

template <typename T> [[nodiscard]] bool valid_contiguous_view(VectorView<T> view) noexcept {
  return view.is_contiguous() && fits_eigen_index(view.size());
}

template <typename T> [[nodiscard]] auto const_map(VectorView<const T> view) {
  return ConstArrayMap<T>(view.data(), static_cast<Eigen::Index>(view.size()));
}

template <typename T> [[nodiscard]] bool sum_impl(VectorView<const T> input, T& output) {
  if (!valid_contiguous_view(input)) {
    return false;
  }
  output = const_map(input).sum();
  return true;
}

template <typename T> [[nodiscard]] bool min_impl(VectorView<const T> input, T& output) {
  if (input.empty() || !valid_contiguous_view(input)) {
    return false;
  }
  output = const_map(input).minCoeff();
  return true;
}

template <typename T> [[nodiscard]] bool max_impl(VectorView<const T> input, T& output) {
  if (input.empty() || !valid_contiguous_view(input)) {
    return false;
  }
  output = const_map(input).maxCoeff();
  return true;
}

} // namespace

bool sum(VectorView<const ksj::base::f32> input, ksj::base::f32& output) {
  return sum_impl(input, output);
}

bool sum(VectorView<const ksj::base::f64> input, ksj::base::f64& output) {
  return sum_impl(input, output);
}

bool min(VectorView<const ksj::base::f32> input, ksj::base::f32& output) {
  return min_impl(input, output);
}

bool min(VectorView<const ksj::base::f64> input, ksj::base::f64& output) {
  return min_impl(input, output);
}

bool max(VectorView<const ksj::base::f32> input, ksj::base::f32& output) {
  return max_impl(input, output);
}

bool max(VectorView<const ksj::base::f64> input, ksj::base::f64& output) {
  return max_impl(input, output);
}

} // namespace ksj::array::detail::eigen
