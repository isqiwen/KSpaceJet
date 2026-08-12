#pragma once

/// Geometric and intensity measurements computed from images, masks, and labeled regions.

#include "kspacejet/array/array.hpp"
#include "kspacejet/image/detail/common.hpp"
#include "kspacejet/stats/stats.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ksj::image {

struct MeanStdDev {
  double mean{};
  double stddev{};
};

namespace detail {

template <typename T> void validate_non_empty_image(ksj::array::ImageView<T> input, const char* message) {
  if (input.empty()) {
    throw std::invalid_argument(message);
  }
}

template <typename T>
[[nodiscard]] ksj::array::VectorView<const T> as_contiguous_vector(ksj::array::ImageView<const T> input) {
  return ksj::array::VectorView<const T>(input.data(), input.size());
}

template <typename T>
inline constexpr bool stats_measurement_scalar_v = std::is_same_v<T, float> || std::is_same_v<T, double>;

template <typename T> [[nodiscard]] MeanStdDev mean_stddev_fallback(ksj::array::ImageView<const T> input) {
  const auto mean_value = static_cast<double>(ksj::array::mean(input));
  double squared_sum = 0.0;
  ksj::array::for_each(input, [&](const auto& value) {
    const auto delta = static_cast<double>(value) - mean_value;
    squared_sum += delta * delta;
  });
  return MeanStdDev{mean_value, std::sqrt(squared_sum / static_cast<double>(input.size()))};
}

template <typename T> [[nodiscard]] double norm_inf_fallback(ksj::array::ImageView<const T> input) {
  double output = 0.0;
  ksj::array::for_each(input, [&](const auto& value) {
    using std::abs;
    output = std::max(output, static_cast<double>(abs(value)));
  });
  return output;
}

template <typename LhsT, typename RhsT>
[[nodiscard]] double norm_diff_l1_fallback(ksj::array::ImageView<const LhsT> lhs,
                                           ksj::array::ImageView<const RhsT> rhs) {
  double output = 0.0;
  ksj::array::for_each_zip(lhs, rhs, [&](const auto& lhs_value, const auto& rhs_value) {
    using std::abs;
    output += static_cast<double>(abs(lhs_value - rhs_value));
  });
  return output;
}

template <typename LhsT, typename RhsT>
[[nodiscard]] double norm_diff_l2_fallback(ksj::array::ImageView<const LhsT> lhs,
                                           ksj::array::ImageView<const RhsT> rhs) {
  double output = 0.0;
  ksj::array::for_each_zip(lhs, rhs, [&](const auto& lhs_value, const auto& rhs_value) {
    const auto diff = static_cast<double>(lhs_value - rhs_value);
    output += diff * diff;
  });
  return std::sqrt(output);
}

template <typename LhsT, typename RhsT>
[[nodiscard]] double norm_diff_inf_fallback(ksj::array::ImageView<const LhsT> lhs,
                                            ksj::array::ImageView<const RhsT> rhs) {
  double output = 0.0;
  ksj::array::for_each_zip(lhs, rhs, [&](const auto& lhs_value, const auto& rhs_value) {
    using std::abs;
    output = std::max(output, static_cast<double>(abs(lhs_value - rhs_value)));
  });
  return output;
}

} // namespace detail

template <typename T> [[nodiscard]] double mean(ksj::array::ImageView<const T> input) {
  detail::validate_non_empty_image(input, "image mean input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (input.is_contiguous()) {
      return static_cast<double>(ksj::stats::mean(detail::as_contiguous_vector(input)));
    }
  }
  return static_cast<double>(ksj::array::mean(input));
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double mean(ksj::array::ImageView<T> input) {
  return mean(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] double mean(const ksj::array::PooledImage<T>& input) {
  return mean(ksj::array::as_const_view(input.view()));
}

template <typename T> [[nodiscard]] MeanStdDev mean_stddev(ksj::array::ImageView<const T> input) {
  detail::validate_non_empty_image(input, "image mean_stddev input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (input.is_contiguous()) {
      const auto values = detail::as_contiguous_vector(input);
      const auto mean_value = ksj::stats::mean(values);
      const auto variance = ksj::stats::variance(values, ksj::stats::VarianceNormalization::population);
      return MeanStdDev{static_cast<double>(mean_value), std::sqrt(static_cast<double>(variance))};
    }
  }
  return detail::mean_stddev_fallback(input);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] MeanStdDev mean_stddev(ksj::array::ImageView<T> input) {
  return mean_stddev(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] MeanStdDev mean_stddev(const ksj::array::PooledImage<T>& input) {
  return mean_stddev(ksj::array::as_const_view(input.view()));
}

template <typename T>
[[nodiscard]] std::pair<std::remove_const_t<T>, std::remove_const_t<T>> minmax(ksj::array::ImageView<const T> input) {
  detail::validate_non_empty_image(input, "image minmax input must be non-empty");
  return ksj::array::minmax(input);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] std::pair<std::remove_const_t<T>, std::remove_const_t<T>> minmax(ksj::array::ImageView<T> input) {
  return minmax(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] std::pair<T, T> minmax(const ksj::array::PooledImage<T>& input) {
  return minmax(ksj::array::as_const_view(input.view()));
}

template <typename T> [[nodiscard]] double norm_l1(ksj::array::ImageView<const T> input) {
  detail::validate_non_empty_image(input, "image norm_l1 input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (input.is_contiguous()) {
      return static_cast<double>(ksj::stats::sum_abs(detail::as_contiguous_vector(input)));
    }
  }
  return static_cast<double>(ksj::array::sum_abs(input));
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double norm_l1(ksj::array::ImageView<T> input) {
  return norm_l1(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] double norm_l1(const ksj::array::PooledImage<T>& input) {
  return norm_l1(ksj::array::as_const_view(input.view()));
}

template <typename T> [[nodiscard]] double norm_l2(ksj::array::ImageView<const T> input) {
  detail::validate_non_empty_image(input, "image norm_l2 input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (input.is_contiguous()) {
      return static_cast<double>(ksj::stats::root_sum_of_squares(detail::as_contiguous_vector(input)));
    }
  }
  return static_cast<double>(ksj::array::norm(input));
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double norm_l2(ksj::array::ImageView<T> input) {
  return norm_l2(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] double norm_l2(const ksj::array::PooledImage<T>& input) {
  return norm_l2(ksj::array::as_const_view(input.view()));
}

template <typename T> [[nodiscard]] double norm_inf(ksj::array::ImageView<const T> input) {
  detail::validate_non_empty_image(input, "image norm_inf input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (input.is_contiguous()) {
      return static_cast<double>(ksj::stats::max_abs(detail::as_contiguous_vector(input)));
    }
  }
  return detail::norm_inf_fallback(input);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double norm_inf(ksj::array::ImageView<T> input) {
  return norm_inf(ksj::array::as_const_view(input));
}

template <typename T> [[nodiscard]] double norm_inf(const ksj::array::PooledImage<T>& input) {
  return norm_inf(ksj::array::as_const_view(input.view()));
}

template <typename T>
[[nodiscard]] double norm_diff_l1(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs) {
  detail::validate_image_shape(lhs, rhs, "image norm_diff_l1 input dimension mismatch");
  detail::validate_non_empty_image(lhs, "image norm_diff_l1 input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (lhs.is_contiguous() && rhs.is_contiguous()) {
      return static_cast<double>(
        ksj::stats::l1_distance(detail::as_contiguous_vector(lhs), detail::as_contiguous_vector(rhs)));
    }
  }
  return detail::norm_diff_l1_fallback(lhs, rhs);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double norm_diff_l1(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return norm_diff_l1(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] double norm_diff_l1(const ksj::array::PooledImage<T>& lhs, const ksj::array::PooledImage<T>& rhs) {
  return norm_diff_l1(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T>
[[nodiscard]] double norm_diff_l2(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs) {
  detail::validate_image_shape(lhs, rhs, "image norm_diff_l2 input dimension mismatch");
  detail::validate_non_empty_image(lhs, "image norm_diff_l2 input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (lhs.is_contiguous() && rhs.is_contiguous()) {
      return static_cast<double>(
        ksj::stats::l2_distance(detail::as_contiguous_vector(lhs), detail::as_contiguous_vector(rhs)));
    }
  }
  return detail::norm_diff_l2_fallback(lhs, rhs);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double norm_diff_l2(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return norm_diff_l2(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] double norm_diff_l2(const ksj::array::PooledImage<T>& lhs, const ksj::array::PooledImage<T>& rhs) {
  return norm_diff_l2(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

template <typename T>
[[nodiscard]] double norm_diff_inf(ksj::array::ImageView<const T> lhs, ksj::array::ImageView<const T> rhs) {
  detail::validate_image_shape(lhs, rhs, "image norm_diff_inf input dimension mismatch");
  detail::validate_non_empty_image(lhs, "image norm_diff_inf input must be non-empty");
  if constexpr (detail::stats_measurement_scalar_v<T>) {
    if (lhs.is_contiguous() && rhs.is_contiguous()) {
      return static_cast<double>(
        ksj::stats::linf_distance(detail::as_contiguous_vector(lhs), detail::as_contiguous_vector(rhs)));
    }
  }
  return detail::norm_diff_inf_fallback(lhs, rhs);
}

template <typename T>
  requires(!std::is_const_v<T>)
[[nodiscard]] double norm_diff_inf(ksj::array::ImageView<T> lhs, ksj::array::ImageView<T> rhs) {
  return norm_diff_inf(ksj::array::as_const_view(lhs), ksj::array::as_const_view(rhs));
}

template <typename T>
[[nodiscard]] double norm_diff_inf(const ksj::array::PooledImage<T>& lhs, const ksj::array::PooledImage<T>& rhs) {
  return norm_diff_inf(ksj::array::as_const_view(lhs.view()), ksj::array::as_const_view(rhs.view()));
}

} // namespace ksj::image
