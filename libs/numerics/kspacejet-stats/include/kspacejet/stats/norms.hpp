#pragma once

/// Vector and matrix norm APIs, including normalization and distance-related operations.

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_norms.hpp"
#include "kspacejet/stats/detail/intel/intel_stats_norms.hpp"
#include "kspacejet/stats/detail/stats_policy.hpp"

#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace ksj::stats {

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> sum_of_squares(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_sum_of_squares<value_type>(input.size()) &&
        detail::intel::sum_of_squares(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::sum_of_squares(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>>
root_sum_of_squares(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_root_sum_of_squares<value_type>(input.size()) &&
        detail::intel::root_sum_of_squares(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::root_sum_of_squares(ksj::array::as_const_view(input));
}

/// Returns the largest element magnitude, or zero for an empty vector.
template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> max_abs(ksj::array::VectorView<T> input) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_max_abs<value_type>(input.size()) &&
        detail::intel::max_abs(ksj::array::as_const_view(input), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::max_abs(ksj::array::as_const_view(input));
}

/// Returns the sum of elementwise absolute differences.
template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> l1_distance(ksj::array::VectorView<T> lhs,
                                                                                 ksj::array::VectorView<T> rhs) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("l1_distance input dimension mismatch");
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_l1_distance<value_type>(lhs.size()) &&
        detail::intel::l1_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::l1_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

/// Returns the Euclidean distance between two vectors.
template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> l2_distance(ksj::array::VectorView<T> lhs,
                                                                                 ksj::array::VectorView<T> rhs) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("l2_distance input dimension mismatch");
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_l2_distance<value_type>(lhs.size()) &&
        detail::intel::l2_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::l2_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

/// Returns the largest elementwise absolute difference.
template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> linf_distance(ksj::array::VectorView<T> lhs,
                                                                                   ksj::array::VectorView<T> rhs) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("linf_distance input dimension mismatch");
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_linf_distance<value_type>(lhs.size()) &&
        detail::intel::linf_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs), intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::linf_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>>
centered_magnitude_average(ksj::array::CubeView<T> input, const std::size_t rows, const std::size_t cols) {
  if (input.empty()) {
    throw std::invalid_argument("centered_magnitude_average input must not be empty");
  }
  if (rows == 0U || cols == 0U || rows > input.dim0() || cols > input.dim1()) {
    throw std::invalid_argument("centered_magnitude_average window dimensions are invalid");
  }

  return detail::eigen::centered_magnitude_average(ksj::array::as_const_view(input), rows, cols);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> squared_l2_norm(ksj::array::CubeView<T> input) {
  return detail::eigen::squared_l2_norm(ksj::array::as_const_view(input));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::magnitude_result_t<std::remove_const_t<T>> squared_l2_distance(ksj::array::CubeView<T> lhs,
                                                                                         ksj::array::CubeView<T> rhs) {
  using value_type = std::remove_const_t<T>;
  using result_type = ksj::array::magnitude_result_t<value_type>;
  if (lhs.shape().extents != rhs.shape().extents) {
    throw std::invalid_argument("squared_l2_distance input dimension mismatch");
  }

  if constexpr (detail::intel::ipp_magnitude_scalar_v<value_type>) {
    result_type intel_output{};
    if (detail::prefer_intel_squared_l2_distance<value_type>(lhs.size()) &&
        detail::intel::squared_l2_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs),
                                           intel_output)) {
      return intel_output;
    }
  }

  return detail::eigen::squared_l2_distance(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
void sum_of_squares_across(ksj::array::CubeView<T> input,
                           ksj::array::MatrixView<ksj::array::magnitude_result_t<std::remove_const_t<T>>> output,
                           const ksj::array::Dim dim) {
  if (dim != ksj::array::Dim::dim2) {
    throw std::invalid_argument("sum_of_squares_across currently supports Dim::dim2");
  }
  if (output.rows() != input.dim0() || output.cols() != input.dim1()) {
    throw std::invalid_argument("sum_of_squares_across output dimension mismatch");
  }

  detail::eigen::sum_of_squares_across(ksj::array::as_const_view(input), output, dim);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::PooledMatrix<ksj::array::magnitude_result_t<std::remove_const_t<T>>>
sum_of_squares_across(ksj::array::CubeView<T> input, const ksj::array::Dim dim) {
  if (dim != ksj::array::Dim::dim2) {
    throw std::invalid_argument("sum_of_squares_across currently supports Dim::dim2");
  }
  using result_type = ksj::array::magnitude_result_t<std::remove_const_t<T>>;
  auto output = ksj::array::make_pooled_matrix<result_type>(input.dim0(), input.dim1());
  sum_of_squares_across(input, output.view(), dim);
  return output;
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
void root_sum_of_squares_across(ksj::array::CubeView<T> input,
                                ksj::array::MatrixView<ksj::array::magnitude_result_t<std::remove_const_t<T>>> output,
                                const ksj::array::Dim dim) {
  if (dim != ksj::array::Dim::dim2) {
    throw std::invalid_argument("root_sum_of_squares_across currently supports Dim::dim2");
  }
  if (output.rows() != input.dim0() || output.cols() != input.dim1()) {
    throw std::invalid_argument("root_sum_of_squares_across output dimension mismatch");
  }

  detail::eigen::root_sum_of_squares_across(ksj::array::as_const_view(input), output, dim);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<std::remove_const_t<T>>)
[[nodiscard]] ksj::array::PooledMatrix<ksj::array::magnitude_result_t<std::remove_const_t<T>>>
root_sum_of_squares_across(ksj::array::CubeView<T> input, const ksj::array::Dim dim) {
  if (dim != ksj::array::Dim::dim2) {
    throw std::invalid_argument("root_sum_of_squares_across currently supports Dim::dim2");
  }
  using result_type = ksj::array::magnitude_result_t<std::remove_const_t<T>>;
  auto output = ksj::array::make_pooled_matrix<result_type>(input.dim0(), input.dim1());
  root_sum_of_squares_across(input, output.view(), dim);
  return output;
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> sum_of_squares(const ksj::array::PooledVector<T>& input) {
  return sum_of_squares(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> root_sum_of_squares(const ksj::array::PooledVector<T>& input) {
  return root_sum_of_squares(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> max_abs(const ksj::array::PooledVector<T>& input) {
  return max_abs(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> l1_distance(const ksj::array::PooledVector<T>& lhs,
                                                            const ksj::array::PooledVector<T>& rhs) {
  return l1_distance(lhs.view(), rhs.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> l2_distance(const ksj::array::PooledVector<T>& lhs,
                                                            const ksj::array::PooledVector<T>& rhs) {
  return l2_distance(lhs.view(), rhs.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> linf_distance(const ksj::array::PooledVector<T>& lhs,
                                                              const ksj::array::PooledVector<T>& rhs) {
  return linf_distance(lhs.view(), rhs.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T>
centered_magnitude_average(const ksj::array::PooledCube<T>& input, const std::size_t rows, const std::size_t cols) {
  return centered_magnitude_average(input.view(), rows, cols);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> squared_l2_norm(const ksj::array::PooledCube<T>& input) {
  return squared_l2_norm(input.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::magnitude_result_t<T> squared_l2_distance(const ksj::array::PooledCube<T>& lhs,
                                                                    const ksj::array::PooledCube<T>& rhs) {
  return squared_l2_distance(lhs.view(), rhs.view());
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
void sum_of_squares_across(const ksj::array::PooledCube<T>& input,
                           ksj::array::PooledMatrix<ksj::array::magnitude_result_t<T>>& output,
                           const ksj::array::Dim dim) {
  sum_of_squares_across(input.view(), output.view(), dim);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::PooledMatrix<ksj::array::magnitude_result_t<T>>
sum_of_squares_across(const ksj::array::PooledCube<T>& input, const ksj::array::Dim dim) {
  return sum_of_squares_across(input.view(), dim);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
void root_sum_of_squares_across(const ksj::array::PooledCube<T>& input,
                                ksj::array::PooledMatrix<ksj::array::magnitude_result_t<T>>& output,
                                const ksj::array::Dim dim) {
  root_sum_of_squares_across(input.view(), output.view(), dim);
}

template <typename T>
  requires(detail::eigen::supported_scalar_v<T>)
[[nodiscard]] ksj::array::PooledMatrix<ksj::array::magnitude_result_t<T>>
root_sum_of_squares_across(const ksj::array::PooledCube<T>& input, const ksj::array::Dim dim) {
  return root_sum_of_squares_across(input.view(), dim);
}

} // namespace ksj::stats
