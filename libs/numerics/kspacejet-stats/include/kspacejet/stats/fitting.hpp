#pragma once

/// Parameter-estimation and regression helpers for numerical sample data.

#include "kspacejet/array/array.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_fitting.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace ksj::stats {

template <typename T> struct LinearFitResult {
  using value_type = T;

  T slope{};
  T intercept{};
};

template <typename T> class LinearFitAccumulator {
  static_assert(std::is_floating_point_v<T>, "LinearFitAccumulator<T> requires a floating-point scalar");

public:
  void add(const T x, const T y) noexcept {
    ++count_;
    sum_x_ += x;
    sum_y_ += y;
    sum_xx_ += x * x;
    sum_xy_ += x * y;
  }

  [[nodiscard]] std::size_t count() const noexcept { return count_; }

  [[nodiscard]] std::optional<LinearFitResult<T>> fit() const noexcept {
    if (count_ < 2U) {
      return std::nullopt;
    }

    const auto sample_count = static_cast<T>(count_);
    const auto denominator = sample_count * sum_xx_ - sum_x_ * sum_x_;
    const auto scale = std::max<T>(T{1}, std::max(std::abs(sample_count * sum_xx_), std::abs(sum_x_ * sum_x_)));
    if (std::abs(denominator) <= std::numeric_limits<T>::epsilon() * scale) {
      return std::nullopt;
    }

    LinearFitResult<T> output{};
    output.slope = (sample_count * sum_xy_ - sum_x_ * sum_y_) / denominator;
    output.intercept = (sum_y_ - output.slope * sum_x_) / sample_count;
    return output;
  }

private:
  std::size_t count_{0};
  T sum_x_{};
  T sum_y_{};
  T sum_xx_{};
  T sum_xy_{};
};

template <typename X, typename Y>
using linear_fit_value_t = std::common_type_t<ksj::array::real_scalar_t<std::remove_const_t<X>>,
                                              ksj::array::real_scalar_t<std::remove_const_t<Y>>, double>;

template <typename X, typename Y> using linear_fit_result_t = LinearFitResult<linear_fit_value_t<X, Y>>;

template <typename X, typename Y>
  requires(detail::eigen::supported_real_scalar_v<std::remove_const_t<X>> &&
           detail::eigen::supported_real_scalar_v<std::remove_const_t<Y>>)
[[nodiscard]] std::optional<linear_fit_result_t<X, Y>> linear_fit(ksj::array::VectorView<X> x,
                                                                  ksj::array::VectorView<Y> y) {
  if (x.size() != y.size()) {
    throw std::invalid_argument("linear_fit input dimension mismatch");
  }
  if (x.empty()) {
    throw std::invalid_argument("linear_fit input must not be empty");
  }

  const auto fit = detail::eigen::linear_fit(ksj::array::as_const_view(x), ksj::array::as_const_view(y));
  if (!fit.has_value()) {
    return std::nullopt;
  }
  using result_type = linear_fit_value_t<X, Y>;
  return LinearFitResult<result_type>{static_cast<result_type>(fit->slope), static_cast<result_type>(fit->intercept)};
}

template <typename X, typename Y>
  requires(detail::eigen::supported_real_scalar_v<X> && detail::eigen::supported_real_scalar_v<Y>)
[[nodiscard]] std::optional<linear_fit_result_t<X, Y>> linear_fit(const ksj::array::PooledVector<X>& x,
                                                                  const ksj::array::PooledVector<Y>& y) {
  return linear_fit(x.view(), y.view());
}

} // namespace ksj::stats
