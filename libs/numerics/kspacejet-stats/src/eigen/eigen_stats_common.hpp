#pragma once

#include "kspacejet/stats/detail/eigen/eigen_stats_fitting.hpp"
#include "kspacejet/stats/moments.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"
#include "kspacejet/array/elementwise.hpp"
#include "kspacejet/array/reductions.hpp"

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>

namespace ksj::stats::detail::eigen {
namespace impl {
using ksj::array::detail::eigen_adapter::as_eigen;

template <typename T> [[nodiscard]] T sum_impl(ksj::array::VectorView<const T> input) {
  return ksj::array::sum(input);
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> sum_abs_impl(ksj::array::VectorView<const T> input) {
  return ksj::array::sum_abs(input);
}

template <typename T> [[nodiscard]] T kahan_sum_impl(ksj::array::VectorView<const T> input) {
  T sum{};
  T compensation{};
  for (std::size_t index = 0U; index < input.size(); ++index) {
    const T y = input(index) - compensation;
    const T t = sum + y;
    compensation = (t - sum) - y;
    sum = t;
  }
  return sum;
}

template <typename T> [[nodiscard]] T pair_sum_impl(ksj::array::VectorView<const T> input) {
  if (input.size() < 8096U) {
    return sum_impl(input);
  }

  const std::size_t half = input.size() / 2U;
  return pair_sum_impl(input.subview(ksj::array::slice(0U, half))) +
         pair_sum_impl(input.subview(ksj::array::slice(half, input.size())));
}

template <typename T> [[nodiscard]] std::size_t max_index_impl(ksj::array::VectorView<const T> input) {
  if (input.empty()) {
    throw std::invalid_argument("max_index input must not be empty");
  }

  Eigen::Index index = 0;
  if constexpr (ksj::array::is_complex_v<T>) {
    as_eigen(input).cwiseAbs().maxCoeff(&index);
  } else {
    as_eigen(input).maxCoeff(&index);
  }
  return static_cast<std::size_t>(index);
}

template <typename T> [[nodiscard]] std::size_t min_index_impl(ksj::array::VectorView<const T> input) {
  if (input.empty()) {
    throw std::invalid_argument("min_index input must not be empty");
  }

  Eigen::Index index = 0;
  if constexpr (ksj::array::is_complex_v<T>) {
    as_eigen(input).cwiseAbs().minCoeff(&index);
  } else {
    as_eigen(input).minCoeff(&index);
  }
  return static_cast<std::size_t>(index);
}

template <typename T> [[nodiscard]] T mean_impl(ksj::array::VectorView<const T> input) {
  if (input.empty()) {
    throw std::invalid_argument("mean input must not be empty");
  }
  return static_cast<T>(as_eigen(input).mean());
}

template <typename T>
[[nodiscard]] T variance_impl(ksj::array::VectorView<const T> input, const VarianceNormalization normalization) {
  if (input.empty()) {
    throw std::invalid_argument("variance input must not be empty");
  }
  if (normalization == VarianceNormalization::sample && input.size() < 2U) {
    throw std::invalid_argument("sample variance requires at least two values");
  }

  const auto values = as_eigen(input);
  const auto mean = static_cast<T>(values.mean());
  const auto total = (values.array() - mean).matrix().squaredNorm();
  const auto denominator = normalization == VarianceNormalization::sample ? input.size() - 1U : input.size();
  return static_cast<T>(total / static_cast<T>(denominator));
}

template <typename T>
[[nodiscard]] T covariance_impl(ksj::array::VectorView<const T> lhs, ksj::array::VectorView<const T> rhs,
                                const VarianceNormalization normalization) {
  if (lhs.size() != rhs.size()) {
    throw std::invalid_argument("covariance dimension mismatch");
  }
  if (lhs.empty()) {
    throw std::invalid_argument("covariance input must not be empty");
  }
  if (normalization == VarianceNormalization::sample && lhs.size() < 2U) {
    throw std::invalid_argument("sample covariance requires at least two values");
  }

  const auto lhs_values = as_eigen(lhs);
  const auto rhs_values = as_eigen(rhs);
  const auto lhs_mean = static_cast<T>(lhs_values.mean());
  const auto rhs_mean = static_cast<T>(rhs_values.mean());
  const auto total = ((lhs_values.array() - lhs_mean) * (rhs_values.array() - rhs_mean)).sum();
  const auto denominator = normalization == VarianceNormalization::sample ? lhs.size() - 1U : lhs.size();
  return static_cast<T>(total / static_cast<T>(denominator));
}

template <typename X, typename Y>
[[nodiscard]] std::optional<LinearFitParameters> linear_fit_impl(ksj::array::VectorView<const X> x,
                                                                 ksj::array::VectorView<const Y> y) {
  if (x.size() != y.size()) {
    throw std::invalid_argument("linear_fit input dimension mismatch");
  }
  if (x.empty()) {
    throw std::invalid_argument("linear_fit input must not be empty");
  }

  const auto x_values = as_eigen(x).template cast<double>().eval();
  const auto y_values = as_eigen(y).template cast<double>().eval();
  const auto sample_count = static_cast<double>(x.size());
  const auto sum_x = x_values.sum();
  const auto sum_y = y_values.sum();
  const auto sum_xx = x_values.squaredNorm();
  const auto sum_xy = x_values.dot(y_values);
  const auto denominator = sample_count * sum_xx - sum_x * sum_x;
  const auto scale = std::max<double>(1.0, std::max(std::abs(sample_count * sum_xx), std::abs(sum_x * sum_x)));
  if (std::abs(denominator) <= std::numeric_limits<double>::epsilon() * scale) {
    return std::nullopt;
  }

  LinearFitParameters output{};
  output.slope = (sample_count * sum_xy - sum_x * sum_y) / denominator;
  output.intercept = (sum_y - output.slope * sum_x) / sample_count;
  return output;
}

template <typename T>
void covariance_impl(ksj::array::MatrixView<const T> samples, ksj::array::MatrixView<T> output,
                     const VarianceNormalization normalization) {
  if (output.rows() != samples.cols() || output.cols() != samples.cols()) {
    throw std::invalid_argument("covariance output dimension mismatch");
  }
  if (samples.rows() == 0U || samples.cols() == 0U) {
    throw std::invalid_argument("covariance input must not be empty");
  }
  if (normalization == VarianceNormalization::sample && samples.rows() < 2U) {
    throw std::invalid_argument("sample covariance requires at least two samples");
  }

  using real_type = ksj::array::real_scalar_t<T>;
  const auto samples_values = as_eigen(samples);
  auto output_values = as_eigen(output);
  const auto means = samples_values.colwise().mean().eval();
  const auto centered = (samples_values.rowwise() - means).eval();
  const auto denominator = normalization == VarianceNormalization::sample ? samples.rows() - 1U : samples.rows();
  output_values = (centered.adjoint() * centered) / static_cast<real_type>(denominator);
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> sum_of_squares_impl(ksj::array::VectorView<const T> input) {
  return ksj::array::squared_norm(input);
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> root_sum_of_squares_impl(ksj::array::VectorView<const T> input) {
  return ksj::array::norm(input);
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> max_abs_impl(ksj::array::VectorView<const T> input) {
  using result_type = ksj::array::magnitude_result_t<T>;
  if (input.empty()) {
    return result_type{};
  }
  return static_cast<result_type>(as_eigen(input).cwiseAbs().maxCoeff());
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> l1_distance_impl(ksj::array::VectorView<const T> lhs,
                                                                 ksj::array::VectorView<const T> rhs) {
  return static_cast<ksj::array::magnitude_result_t<T>>((as_eigen(lhs) - as_eigen(rhs)).cwiseAbs().sum());
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> l2_distance_impl(ksj::array::VectorView<const T> lhs,
                                                                 ksj::array::VectorView<const T> rhs) {
  using std::sqrt;
  return static_cast<ksj::array::magnitude_result_t<T>>(sqrt(ksj::array::squared_distance(lhs, rhs)));
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> linf_distance_impl(ksj::array::VectorView<const T> lhs,
                                                                   ksj::array::VectorView<const T> rhs) {
  using result_type = ksj::array::magnitude_result_t<T>;
  if (lhs.empty()) {
    return result_type{};
  }
  return static_cast<result_type>((as_eigen(lhs) - as_eigen(rhs)).cwiseAbs().maxCoeff());
}

template <typename T> [[nodiscard]] ksj::array::magnitude_result_t<T> rmse_impl(ksj::array::VectorView<const T> diff) {
  if (diff.empty()) {
    throw std::invalid_argument("rmse input must not be empty");
  }

  return static_cast<ksj::array::magnitude_result_t<T>>(
    std::sqrt(ksj::array::squared_norm(diff) / static_cast<ksj::array::magnitude_result_t<T>>(diff.size())));
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> rmse_impl(ksj::array::VectorView<const T> data,
                                                          ksj::array::VectorView<const T> reference) {
  if (data.size() != reference.size()) {
    throw std::invalid_argument("rmse input dimension mismatch");
  }
  if (data.empty()) {
    throw std::invalid_argument("rmse input must not be empty");
  }

  return static_cast<ksj::array::magnitude_result_t<T>>(std::sqrt(
    ksj::array::squared_distance(data, reference) / static_cast<ksj::array::magnitude_result_t<T>>(data.size())));
}

template <typename T>
[[nodiscard]] bool equal_impl(ksj::array::VectorView<const T> data, ksj::array::VectorView<const T> reference,
                              const ksj::array::magnitude_result_t<T> precision) {
  if (data.size() != reference.size()) {
    return false;
  }
  if (data.empty()) {
    return true;
  }

  return ksj::array::all_close(data, reference, precision);
}

template <typename T>
[[nodiscard]] T otsu_threshold_impl(ksj::array::VectorView<const T> input, const std::size_t interval_count) {
  if (input.empty()) {
    throw std::invalid_argument("otsu_threshold input must not be empty");
  }
  if (interval_count == 0U) {
    throw std::invalid_argument("otsu_threshold interval count must not be zero");
  }

  const auto values = as_eigen(input);
  const T min_value = values.minCoeff();
  const T max_value = values.maxCoeff();
  if (min_value == max_value) {
    return min_value;
  }

  const auto bin_count = interval_count + 1U;
  auto histogram = ksj::array::make_pooled_vector<T>(bin_count);
  auto scores = ksj::array::make_pooled_vector<T>(bin_count);
  ksj::array::fill(histogram.view(), T{});
  ksj::array::fill(scores.view(), T{});

  const auto interval = (max_value - min_value) / static_cast<T>(interval_count);
  for (std::size_t index = 0U; index < input.size(); ++index) {
    auto bin = static_cast<std::size_t>(std::floor((input(index) - min_value) / interval));
    if (bin >= bin_count) {
      bin = bin_count - 1U;
    }
    histogram(bin) += T{1};
  }

  const auto sample_count = static_cast<T>(input.size());
  for (std::size_t bin = 0U; bin < bin_count; ++bin) {
    histogram(bin) /= sample_count;
  }

  for (std::size_t threshold_bin = 0U; threshold_bin < bin_count; ++threshold_bin) {
    T background_weight{};
    T background_mean{};
    for (std::size_t bin = 0U; bin <= threshold_bin; ++bin) {
      background_weight += histogram(bin);
      background_mean += static_cast<T>(bin) * histogram(bin);
    }

    const auto foreground_weight = T{1} - background_weight;
    if (background_weight == T{} || foreground_weight == T{}) {
      scores(threshold_bin) = T{};
      continue;
    }

    background_mean /= background_weight;
    T foreground_mean{};
    for (std::size_t bin = threshold_bin + 1U; bin < bin_count; ++bin) {
      foreground_mean += static_cast<T>(bin) * histogram(bin);
    }
    foreground_mean /= foreground_weight;

    const auto mean_delta = foreground_mean - background_mean;
    scores(threshold_bin) = background_weight * foreground_weight * mean_delta * mean_delta;
  }

  Eigen::Index best_bin = 0;
  as_eigen(scores.view()).maxCoeff(&best_bin);
  return min_value + (static_cast<T>(best_bin) + static_cast<T>(0.5)) * interval;
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> sum_magnitude(const ksj::array::MatrixView<const T> input) {
  return ksj::array::sum_abs(input);
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T>
centered_magnitude_average_impl(ksj::array::CubeView<const T> input, const std::size_t rows, const std::size_t cols) {
  if (input.empty()) {
    throw std::invalid_argument("centered_magnitude_average input must not be empty");
  }
  if (rows == 0U || cols == 0U || rows > input.dim0() || cols > input.dim1()) {
    throw std::invalid_argument("centered_magnitude_average window dimensions are invalid");
  }

  const std::size_t row_start = input.dim0() / 2U - rows / 2U;
  const std::size_t col_start = input.dim1() / 2U - cols / 2U;
  ksj::array::magnitude_result_t<T> total{};
  for (std::size_t slice = 0U; slice < input.dim2(); ++slice) {
    total += sum_magnitude(input.subview(ksj::array::slice(row_start, row_start + rows),
                                         ksj::array::slice(col_start, col_start + cols), slice));
  }
  return total / static_cast<ksj::array::magnitude_result_t<T>>(rows * cols * input.dim2());
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> squared_l2_norm_impl(ksj::array::CubeView<const T> input) {
  return ksj::array::squared_norm(input);
}

template <typename T>
[[nodiscard]] ksj::array::magnitude_result_t<T> squared_l2_distance_impl(ksj::array::CubeView<const T> lhs,
                                                                         ksj::array::CubeView<const T> rhs) {
  if (lhs.shape().extents != rhs.shape().extents) {
    throw std::invalid_argument("squared_l2_distance input dimension mismatch");
  }

  return ksj::array::squared_distance(lhs, rhs);
}

template <typename T>
void sum_of_squares_across_impl(ksj::array::CubeView<const T> input,
                                ksj::array::MatrixView<ksj::array::magnitude_result_t<T>> output,
                                const ksj::array::Dim dim) {
  if (dim != ksj::array::Dim::dim2) {
    throw std::invalid_argument("sum_of_squares_across currently supports Dim::dim2");
  }
  if (output.rows() != input.dim0() || output.cols() != input.dim1()) {
    throw std::invalid_argument("sum_of_squares_across output dimension mismatch");
  }

  ksj::array::squared_norm_across(input, output, dim);
}

template <typename T>
void root_sum_of_squares_across_impl(ksj::array::CubeView<const T> input,
                                     ksj::array::MatrixView<ksj::array::magnitude_result_t<T>> output,
                                     const ksj::array::Dim dim) {
  sum_of_squares_across_impl(input, output, dim);
  ksj::array::sqrt(output, output);
}

} // namespace impl
} // namespace ksj::stats::detail::eigen
