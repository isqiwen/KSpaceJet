#pragma once

#include "kspacejet/array/array.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ksj::mri::debug {

struct ViewLayout {
  std::string kind;
  std::size_t rank{0};
  std::vector<std::size_t> extents;
  std::vector<std::size_t> strides;
  std::size_t element_size{0};
  std::size_t element_count{0};
  std::size_t logical_size_bytes{0};
  std::uintptr_t data_address{0};
  bool contiguous{false};
  bool empty{true};
  bool aligned_16{false};
  bool aligned_32{false};
  bool aligned_64{false};
  std::string contiguity_note;
};

struct ArraySummary {
  ViewLayout layout;
  bool complex_values{false};
  std::uint64_t fingerprint{0};

  std::size_t finite_count{0};
  std::size_t nan_count{0};
  std::size_t inf_count{0};
  std::size_t zero_count{0};
  std::size_t nonzero_count{0};

  double min_real{std::numeric_limits<double>::quiet_NaN()};
  double max_real{std::numeric_limits<double>::quiet_NaN()};
  double mean_real{std::numeric_limits<double>::quiet_NaN()};
  double stddev_real{std::numeric_limits<double>::quiet_NaN()};

  double min_imag{std::numeric_limits<double>::quiet_NaN()};
  double max_imag{std::numeric_limits<double>::quiet_NaN()};
  double mean_imag{std::numeric_limits<double>::quiet_NaN()};
  double stddev_imag{std::numeric_limits<double>::quiet_NaN()};

  double min_abs{std::numeric_limits<double>::quiet_NaN()};
  double max_abs{std::numeric_limits<double>::quiet_NaN()};
  double mean_abs{std::numeric_limits<double>::quiet_NaN()};
  double stddev_abs{std::numeric_limits<double>::quiet_NaN()};

  double min_phase{std::numeric_limits<double>::quiet_NaN()};
  double max_phase{std::numeric_limits<double>::quiet_NaN()};
  double mean_phase{std::numeric_limits<double>::quiet_NaN()};
  double stddev_phase{std::numeric_limits<double>::quiet_NaN()};
};

struct ArrayCompareOptions {
  double abs_tolerance{0.0};
  double rel_tolerance{0.0};
  std::size_t max_recorded_differences{16};
  std::size_t max_integer_delta_bins{32};
};

struct ArrayDifferenceSample {
  std::size_t linear_index{0};
  std::vector<std::size_t> coordinate;
  double lhs_real{0.0};
  double lhs_imag{0.0};
  double rhs_real{0.0};
  double rhs_imag{0.0};
  double abs_diff{0.0};
  double rel_diff{0.0};
};

struct IntegerDeltaBin {
  long long delta{0};
  std::size_t count{0};
};

struct ArrayComparison {
  ViewLayout lhs_layout;
  ViewLayout rhs_layout;
  bool same_shape{false};
  bool same_element_count{false};
  std::size_t compared_count{0};

  std::size_t exact_mismatch_count{0};
  std::size_t tolerance_mismatch_count{0};
  double max_abs_diff{0.0};
  double mean_abs_diff{0.0};
  double rmse{0.0};
  double max_rel_diff{0.0};
  std::size_t max_abs_diff_linear_index{0};
  std::vector<std::size_t> max_abs_diff_coordinate;

  std::vector<ArrayDifferenceSample> samples;
  std::vector<IntegerDeltaBin> integer_delta_histogram;
  std::size_t integer_delta_overflow_count{0};
};

namespace detail {

template <typename T> struct is_std_complex : std::false_type {};

template <typename T> struct is_std_complex<std::complex<T>> : std::true_type {};

template <typename T> inline constexpr bool is_std_complex_v = is_std_complex<std::remove_cv_t<T>>::value;

template <typename T> struct view_rank;

template <typename T> struct view_rank<ksj::array::VectorView<T>> : std::integral_constant<std::size_t, 1> {};
template <typename T> struct view_rank<ksj::array::MatrixView<T>> : std::integral_constant<std::size_t, 2> {};
template <typename T> struct view_rank<ksj::array::ImageView<T>> : std::integral_constant<std::size_t, 2> {};
template <typename T> struct view_rank<ksj::array::CubeView<T>> : std::integral_constant<std::size_t, 3> {};
template <typename T> struct view_rank<ksj::array::Array4DView<T>> : std::integral_constant<std::size_t, 4> {};

template <typename T> [[nodiscard]] ksj::array::VectorView<T> debug_view(ksj::array::VectorView<T> value) noexcept {
  return value;
}

template <typename T> [[nodiscard]] ksj::array::MatrixView<T> debug_view(ksj::array::MatrixView<T> value) noexcept {
  return value;
}

template <typename T> [[nodiscard]] ksj::array::ImageView<T> debug_view(ksj::array::ImageView<T> value) noexcept {
  return value;
}

template <typename T> [[nodiscard]] ksj::array::CubeView<T> debug_view(ksj::array::CubeView<T> value) noexcept {
  return value;
}

template <typename T> [[nodiscard]] ksj::array::Array4DView<T> debug_view(ksj::array::Array4DView<T> value) noexcept {
  return value;
}

template <typename T> [[nodiscard]] ksj::array::VectorView<T> debug_view(ksj::array::PooledVector<T>& value) noexcept {
  return value.view();
}

template <typename T>
[[nodiscard]] ksj::array::VectorView<const T> debug_view(const ksj::array::PooledVector<T>& value) noexcept {
  return value.view();
}

template <typename T> [[nodiscard]] ksj::array::MatrixView<T> debug_view(ksj::array::PooledMatrix<T>& value) noexcept {
  return value.view();
}

template <typename T>
[[nodiscard]] ksj::array::MatrixView<const T> debug_view(const ksj::array::PooledMatrix<T>& value) noexcept {
  return value.view();
}

template <typename T> [[nodiscard]] ksj::array::ImageView<T> debug_view(ksj::array::PooledImage<T>& value) noexcept {
  return value.view();
}

template <typename T>
[[nodiscard]] ksj::array::ImageView<const T> debug_view(const ksj::array::PooledImage<T>& value) noexcept {
  return value.view();
}

template <typename T> [[nodiscard]] ksj::array::CubeView<T> debug_view(ksj::array::PooledCube<T>& value) noexcept {
  return value.view();
}

template <typename T>
[[nodiscard]] ksj::array::CubeView<const T> debug_view(const ksj::array::PooledCube<T>& value) noexcept {
  return value.view();
}

template <typename T>
[[nodiscard]] ksj::array::Array4DView<T> debug_view(ksj::array::PooledArray4D<T>& value) noexcept {
  return value.view();
}

template <typename T>
[[nodiscard]] ksj::array::Array4DView<const T> debug_view(const ksj::array::PooledArray4D<T>& value) noexcept {
  return value.view();
}

template <typename T> [[nodiscard]] std::string view_kind(ksj::array::VectorView<T>) {
  return "vector";
}
template <typename T> [[nodiscard]] std::string view_kind(ksj::array::MatrixView<T>) {
  return "matrix";
}
template <typename T> [[nodiscard]] std::string view_kind(ksj::array::ImageView<T>) {
  return "image";
}
template <typename T> [[nodiscard]] std::string view_kind(ksj::array::CubeView<T>) {
  return "cube";
}
template <typename T> [[nodiscard]] std::string view_kind(ksj::array::Array4DView<T>) {
  return "array4d";
}

template <typename T> [[nodiscard]] std::size_t view_extent(ksj::array::VectorView<T> view, std::size_t) noexcept {
  return view.extent();
}

template <typename T> [[nodiscard]] std::size_t view_stride(ksj::array::VectorView<T> view, std::size_t) noexcept {
  return view.stride();
}

template <typename View> [[nodiscard]] std::size_t view_extent(View view, const std::size_t axis) {
  return view.extent(axis);
}

template <typename View> [[nodiscard]] std::size_t view_stride(View view, const std::size_t axis) {
  return view.stride(axis);
}

[[nodiscard]] inline std::vector<std::size_t> expected_row_major_strides(const std::vector<std::size_t>& extents) {
  std::vector<std::size_t> expected(extents.size(), 1U);
  if (extents.empty()) {
    return expected;
  }
  std::size_t stride = 1U;
  for (std::size_t reverse_index = extents.size(); reverse_index > 0U; --reverse_index) {
    const auto axis = reverse_index - 1U;
    expected[axis] = stride;
    stride *= extents[axis];
  }
  return expected;
}

[[nodiscard]] inline std::vector<std::size_t> coordinate_from_linear_index(std::size_t linear_index,
                                                                           const std::vector<std::size_t>& extents) {
  std::vector<std::size_t> coordinate(extents.size(), 0U);
  for (std::size_t reverse_index = extents.size(); reverse_index > 0U; --reverse_index) {
    const auto axis = reverse_index - 1U;
    const auto extent = extents[axis];
    if (extent == 0U) {
      coordinate[axis] = 0U;
      continue;
    }
    coordinate[axis] = linear_index % extent;
    linear_index /= extent;
  }
  return coordinate;
}

struct RunningStats {
  std::size_t count{0};
  long double min{std::numeric_limits<long double>::infinity()};
  long double max{-std::numeric_limits<long double>::infinity()};
  long double mean{0.0L};
  long double m2{0.0L};

  void add(const long double value) noexcept {
    ++count;
    min = std::min(min, value);
    max = std::max(max, value);
    const auto delta = value - mean;
    mean += delta / static_cast<long double>(count);
    const auto delta2 = value - mean;
    m2 += delta * delta2;
  }

  [[nodiscard]] double minimum() const noexcept {
    return count == 0U ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(min);
  }

  [[nodiscard]] double maximum() const noexcept {
    return count == 0U ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(max);
  }

  [[nodiscard]] double average() const noexcept {
    return count == 0U ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(mean);
  }

  [[nodiscard]] double stddev() const noexcept {
    return count == 0U ? std::numeric_limits<double>::quiet_NaN()
                       : static_cast<double>(std::sqrt(m2 / static_cast<long double>(count)));
  }
};

template <typename T> [[nodiscard]] constexpr bool value_is_complex() noexcept {
  return is_std_complex_v<std::remove_cv_t<T>>;
}

template <typename T> [[nodiscard]] long double value_real(const T& value) noexcept {
  if constexpr (value_is_complex<T>()) {
    return static_cast<long double>(value.real());
  } else {
    return static_cast<long double>(value);
  }
}

template <typename T> [[nodiscard]] long double value_imag(const T& value) noexcept {
  if constexpr (value_is_complex<T>()) {
    return static_cast<long double>(value.imag());
  } else {
    return 0.0L;
  }
}

template <typename T> [[nodiscard]] long double value_abs(const T& value) noexcept {
  if constexpr (value_is_complex<T>()) {
    return std::abs(std::complex<long double>{value_real(value), value_imag(value)});
  } else if constexpr (std::is_signed_v<std::remove_cv_t<T>>) {
    return std::abs(static_cast<long double>(value));
  } else {
    return static_cast<long double>(value);
  }
}

template <typename T> [[nodiscard]] long double value_phase(const T& value) noexcept {
  return std::atan2(value_imag(value), value_real(value));
}

template <typename T> [[nodiscard]] bool component_is_nan(const T value) noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return std::isnan(value);
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] bool component_is_inf(const T value) noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return std::isinf(value);
  } else {
    return false;
  }
}

template <typename T> [[nodiscard]] bool component_is_finite(const T value) noexcept {
  if constexpr (std::is_floating_point_v<T>) {
    return std::isfinite(value);
  } else {
    return true;
  }
}

template <typename T> [[nodiscard]] bool value_has_nan(const T& value) noexcept {
  if constexpr (value_is_complex<T>()) {
    return component_is_nan(value.real()) || component_is_nan(value.imag());
  } else {
    return component_is_nan(value);
  }
}

template <typename T> [[nodiscard]] bool value_has_inf(const T& value) noexcept {
  if constexpr (value_is_complex<T>()) {
    return component_is_inf(value.real()) || component_is_inf(value.imag());
  } else {
    return component_is_inf(value);
  }
}

template <typename T> [[nodiscard]] bool value_is_finite(const T& value) noexcept {
  if constexpr (value_is_complex<T>()) {
    return component_is_finite(value.real()) && component_is_finite(value.imag());
  } else {
    return component_is_finite(value);
  }
}

inline void fnv1a_append_bytes(std::uint64_t& hash, const void* data, const std::size_t size) noexcept {
  constexpr std::uint64_t prime = 1099511628211ULL;
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= static_cast<std::uint64_t>(bytes[index]);
    hash *= prime;
  }
}

template <typename T> void fnv1a_append_value(std::uint64_t& hash, const T& value) noexcept {
  const auto stored_value = std::remove_cv_t<T>(value);
  fnv1a_append_bytes(hash, &stored_value, sizeof(stored_value));
}

template <typename LhsT, typename RhsT> [[nodiscard]] bool values_exact_equal(const LhsT& lhs, const RhsT& rhs) {
  if constexpr (value_is_complex<LhsT>() || value_is_complex<RhsT>()) {
    return value_real(lhs) == value_real(rhs) && value_imag(lhs) == value_imag(rhs);
  } else {
    return lhs == rhs;
  }
}

template <typename LhsT, typename RhsT> [[nodiscard]] long double difference_abs(const LhsT& lhs, const RhsT& rhs) {
  if constexpr (value_is_complex<LhsT>() || value_is_complex<RhsT>()) {
    const auto real_diff = value_real(rhs) - value_real(lhs);
    const auto imag_diff = value_imag(rhs) - value_imag(lhs);
    return std::abs(std::complex<long double>{real_diff, imag_diff});
  } else {
    return std::abs(static_cast<long double>(rhs) - static_cast<long double>(lhs));
  }
}

template <typename LhsT, typename RhsT>
[[nodiscard]] long double relative_diff(const LhsT& lhs, const RhsT& rhs, const long double abs_diff) {
  const auto scale = std::max({value_abs(lhs), value_abs(rhs), 1.0e-300L});
  return abs_diff / scale;
}

template <typename LhsT, typename RhsT> [[nodiscard]] constexpr bool supports_integer_delta_histogram() noexcept {
  return !value_is_complex<LhsT>() && !value_is_complex<RhsT>() && std::is_integral_v<std::remove_cv_t<LhsT>> &&
         std::is_integral_v<std::remove_cv_t<RhsT>>;
}

template <typename T> [[nodiscard]] long long signed_integer_value(const T& value) noexcept {
  using value_type = std::remove_cv_t<T>;
  if constexpr (std::is_signed_v<value_type>) {
    return static_cast<long long>(value);
  } else {
    constexpr auto max_signed = static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    const auto unsigned_value = static_cast<unsigned long long>(value);
    return unsigned_value > max_signed ? std::numeric_limits<long long>::max() : static_cast<long long>(unsigned_value);
  }
}

template <typename View, typename Function> void for_each_logical(View view, Function&& function) {
  for (std::size_t index = 0; index < view.size(); ++index) {
    function(index, view[index]);
  }
}

} // namespace detail

template <typename Expression> [[nodiscard]] auto debug_view(Expression&& expression) noexcept {
  return detail::debug_view(std::forward<Expression>(expression));
}

template <typename Expression> [[nodiscard]] ViewLayout describe_layout(Expression&& expression) {
  const auto view = debug_view(std::forward<Expression>(expression));
  using view_type = std::remove_cvref_t<decltype(view)>;
  using value_type = typename view_type::value_type;
  constexpr auto rank = detail::view_rank<view_type>::value;

  ViewLayout layout;
  layout.kind = detail::view_kind(view);
  layout.rank = rank;
  layout.element_size = sizeof(value_type);
  layout.element_count = view.size();
  layout.logical_size_bytes = view.size_bytes();
  layout.data_address = reinterpret_cast<std::uintptr_t>(view.data());
  layout.contiguous = view.is_contiguous();
  layout.empty = view.empty();
  layout.aligned_16 = layout.data_address != 0U && layout.data_address % 16U == 0U;
  layout.aligned_32 = layout.data_address != 0U && layout.data_address % 32U == 0U;
  layout.aligned_64 = layout.data_address != 0U && layout.data_address % 64U == 0U;

  layout.extents.reserve(rank);
  layout.strides.reserve(rank);
  for (std::size_t axis = 0; axis < rank; ++axis) {
    layout.extents.push_back(detail::view_extent(view, axis));
    layout.strides.push_back(detail::view_stride(view, axis));
  }

  const auto expected = detail::expected_row_major_strides(layout.extents);
  if (layout.empty) {
    layout.contiguity_note = "empty";
  } else if (layout.contiguous) {
    layout.contiguity_note = "contiguous row-major logical order";
  } else {
    std::ostringstream note;
    note << "non-contiguous";
    for (std::size_t axis = 0; axis < std::min(layout.strides.size(), expected.size()); ++axis) {
      if (layout.strides[axis] != expected[axis]) {
        note << "; axis " << axis << " stride=" << layout.strides[axis] << " expected=" << expected[axis];
        break;
      }
    }
    layout.contiguity_note = note.str();
  }

  return layout;
}

template <typename Expression> [[nodiscard]] ArraySummary summarize_array(Expression&& expression) {
  const auto view = debug_view(std::forward<Expression>(expression));
  using view_type = std::remove_cvref_t<decltype(view)>;
  using value_type = typename view_type::value_type;

  ArraySummary summary;
  summary.layout = describe_layout(view);
  summary.complex_values = detail::value_is_complex<value_type>();

  constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ULL;
  summary.fingerprint = fnv_offset_basis;
  detail::fnv1a_append_value(summary.fingerprint, summary.layout.rank);
  for (const auto extent : summary.layout.extents) {
    detail::fnv1a_append_value(summary.fingerprint, extent);
  }

  detail::RunningStats real_stats;
  detail::RunningStats imag_stats;
  detail::RunningStats abs_stats;
  detail::RunningStats phase_stats;

  detail::for_each_logical(view, [&](const std::size_t, const auto& value) {
    detail::fnv1a_append_value(summary.fingerprint, value);

    if (detail::value_has_nan(value)) {
      ++summary.nan_count;
    }
    if (detail::value_has_inf(value)) {
      ++summary.inf_count;
    }
    if (detail::value_is_finite(value)) {
      ++summary.finite_count;
    }

    const auto abs_value = detail::value_abs(value);
    if (abs_value == 0.0L) {
      ++summary.zero_count;
    } else {
      ++summary.nonzero_count;
    }

    const auto real_value = detail::value_real(value);
    if (std::isfinite(real_value)) {
      real_stats.add(real_value);
    }

    if constexpr (detail::value_is_complex<value_type>()) {
      const auto imag_value = detail::value_imag(value);
      if (std::isfinite(imag_value)) {
        imag_stats.add(imag_value);
      }
      if (std::isfinite(abs_value)) {
        abs_stats.add(abs_value);
      }
      if (std::isfinite(abs_value) && abs_value != 0.0L) {
        phase_stats.add(detail::value_phase(value));
      }
    } else if (std::isfinite(abs_value)) {
      abs_stats.add(abs_value);
    }
  });

  summary.min_real = real_stats.minimum();
  summary.max_real = real_stats.maximum();
  summary.mean_real = real_stats.average();
  summary.stddev_real = real_stats.stddev();

  summary.min_imag = imag_stats.minimum();
  summary.max_imag = imag_stats.maximum();
  summary.mean_imag = imag_stats.average();
  summary.stddev_imag = imag_stats.stddev();

  summary.min_abs = abs_stats.minimum();
  summary.max_abs = abs_stats.maximum();
  summary.mean_abs = abs_stats.average();
  summary.stddev_abs = abs_stats.stddev();

  summary.min_phase = phase_stats.minimum();
  summary.max_phase = phase_stats.maximum();
  summary.mean_phase = phase_stats.average();
  summary.stddev_phase = phase_stats.stddev();
  return summary;
}

template <typename LhsExpression, typename RhsExpression>
[[nodiscard]] ArrayComparison compare_arrays(LhsExpression&& lhs_expression, RhsExpression&& rhs_expression,
                                             const ArrayCompareOptions& options = {}) {
  const auto lhs = debug_view(std::forward<LhsExpression>(lhs_expression));
  const auto rhs = debug_view(std::forward<RhsExpression>(rhs_expression));
  using lhs_value_type = typename std::remove_cvref_t<decltype(lhs)>::value_type;
  using rhs_value_type = typename std::remove_cvref_t<decltype(rhs)>::value_type;

  ArrayComparison comparison;
  comparison.lhs_layout = describe_layout(lhs);
  comparison.rhs_layout = describe_layout(rhs);
  comparison.same_shape = comparison.lhs_layout.extents == comparison.rhs_layout.extents;
  comparison.same_element_count = lhs.size() == rhs.size();
  comparison.compared_count = std::min(lhs.size(), rhs.size());

  long double abs_sum = 0.0L;
  long double abs_square_sum = 0.0L;
  std::map<long long, std::size_t> integer_delta_counts;

  for (std::size_t index = 0; index < comparison.compared_count; ++index) {
    const auto& lhs_value = lhs[index];
    const auto& rhs_value = rhs[index];
    const auto abs_diff = detail::difference_abs(lhs_value, rhs_value);
    const auto rel_diff = detail::relative_diff(lhs_value, rhs_value, abs_diff);

    abs_sum += abs_diff;
    abs_square_sum += abs_diff * abs_diff;
    if (abs_diff > comparison.max_abs_diff) {
      comparison.max_abs_diff = static_cast<double>(abs_diff);
      comparison.max_abs_diff_linear_index = index;
      comparison.max_abs_diff_coordinate = detail::coordinate_from_linear_index(index, comparison.lhs_layout.extents);
    }
    comparison.max_rel_diff = std::max(comparison.max_rel_diff, static_cast<double>(rel_diff));

    const bool exact_equal = detail::values_exact_equal(lhs_value, rhs_value);
    if (!exact_equal) {
      ++comparison.exact_mismatch_count;
      if (comparison.samples.size() < options.max_recorded_differences) {
        comparison.samples.push_back(ArrayDifferenceSample{
          .linear_index = index,
          .coordinate = detail::coordinate_from_linear_index(index, comparison.lhs_layout.extents),
          .lhs_real = static_cast<double>(detail::value_real(lhs_value)),
          .lhs_imag = static_cast<double>(detail::value_imag(lhs_value)),
          .rhs_real = static_cast<double>(detail::value_real(rhs_value)),
          .rhs_imag = static_cast<double>(detail::value_imag(rhs_value)),
          .abs_diff = static_cast<double>(abs_diff),
          .rel_diff = static_cast<double>(rel_diff),
        });
      }

      if constexpr (detail::supports_integer_delta_histogram<lhs_value_type, rhs_value_type>()) {
        const auto delta = detail::signed_integer_value(rhs_value) - detail::signed_integer_value(lhs_value);
        if (integer_delta_counts.contains(delta) || integer_delta_counts.size() < options.max_integer_delta_bins) {
          ++integer_delta_counts[delta];
        } else {
          ++comparison.integer_delta_overflow_count;
        }
      }
    }

    const auto tolerance_scale = std::max({detail::value_abs(lhs_value), detail::value_abs(rhs_value), 1.0L});
    const bool within_abs_tolerance = abs_diff <= static_cast<long double>(options.abs_tolerance);
    const bool within_rel_tolerance = abs_diff <= static_cast<long double>(options.rel_tolerance) * tolerance_scale;
    if (!within_abs_tolerance && !within_rel_tolerance) {
      ++comparison.tolerance_mismatch_count;
    }
  }

  if (comparison.compared_count > 0U) {
    const auto count = static_cast<long double>(comparison.compared_count);
    comparison.mean_abs_diff = static_cast<double>(abs_sum / count);
    comparison.rmse = static_cast<double>(std::sqrt(abs_square_sum / count));
  }

  comparison.integer_delta_histogram.reserve(integer_delta_counts.size());
  for (const auto& [delta, count] : integer_delta_counts) {
    comparison.integer_delta_histogram.push_back(IntegerDeltaBin{.delta = delta, .count = count});
  }
  return comparison;
}

[[nodiscard]] inline std::string format_shape(const std::vector<std::size_t>& values) {
  std::ostringstream out;
  out << "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      out << ", ";
    }
    out << values[index];
  }
  out << "]";
  return out.str();
}

[[nodiscard]] inline std::string format_layout(const ViewLayout& layout) {
  std::ostringstream out;
  out << layout.kind << " rank=" << layout.rank << " shape=" << format_shape(layout.extents)
      << " strides=" << format_shape(layout.strides) << " elements=" << layout.element_count
      << " bytes=" << layout.logical_size_bytes << " contiguous=" << (layout.contiguous ? "true" : "false")
      << " aligned16=" << (layout.aligned_16 ? "true" : "false")
      << " aligned32=" << (layout.aligned_32 ? "true" : "false")
      << " aligned64=" << (layout.aligned_64 ? "true" : "false") << " note=\"" << layout.contiguity_note << "\"";
  return out.str();
}

[[nodiscard]] inline std::string format_summary(const ArraySummary& summary) {
  std::ostringstream out;
  out << format_layout(summary.layout) << " fingerprint=0x" << std::hex << summary.fingerprint << std::dec
      << " finite=" << summary.finite_count << " nan=" << summary.nan_count << " inf=" << summary.inf_count
      << " zero=" << summary.zero_count << " nonzero=" << summary.nonzero_count << " real[min,max,mean,std]=["
      << summary.min_real << ", " << summary.max_real << ", " << summary.mean_real << ", " << summary.stddev_real
      << "] abs[min,max,mean,std]=[" << summary.min_abs << ", " << summary.max_abs << ", " << summary.mean_abs << ", "
      << summary.stddev_abs << "]";
  if (summary.complex_values) {
    out << " imag[min,max,mean,std]=[" << summary.min_imag << ", " << summary.max_imag << ", " << summary.mean_imag
        << ", " << summary.stddev_imag << "] phase[min,max,mean,std]=[" << summary.min_phase << ", "
        << summary.max_phase << ", " << summary.mean_phase << ", " << summary.stddev_phase << "]";
  }
  return out.str();
}

[[nodiscard]] inline std::string format_comparison(const ArrayComparison& comparison) {
  std::ostringstream out;
  out << "same_shape=" << (comparison.same_shape ? "true" : "false") << " compared=" << comparison.compared_count
      << " exact_mismatch=" << comparison.exact_mismatch_count
      << " tolerance_mismatch=" << comparison.tolerance_mismatch_count << " max_abs_diff=" << comparison.max_abs_diff
      << " mean_abs_diff=" << comparison.mean_abs_diff << " rmse=" << comparison.rmse
      << " max_rel_diff=" << comparison.max_rel_diff << " max_abs_diff_index=" << comparison.max_abs_diff_linear_index
      << " max_abs_diff_coord=" << format_shape(comparison.max_abs_diff_coordinate);
  if (!comparison.integer_delta_histogram.empty()) {
    out << " integer_delta_histogram={";
    for (std::size_t index = 0; index < comparison.integer_delta_histogram.size(); ++index) {
      if (index != 0U) {
        out << ", ";
      }
      out << comparison.integer_delta_histogram[index].delta << ":" << comparison.integer_delta_histogram[index].count;
    }
    out << "}";
    if (comparison.integer_delta_overflow_count != 0U) {
      out << " overflow=" << comparison.integer_delta_overflow_count;
    }
  }
  return out.str();
}

} // namespace ksj::mri::debug
