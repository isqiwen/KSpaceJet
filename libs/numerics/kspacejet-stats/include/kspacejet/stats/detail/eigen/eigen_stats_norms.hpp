#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/stats/detail/eigen/eigen_stats_types.hpp"

#include <cstddef>

namespace ksj::stats::detail::eigen {

[[nodiscard]] float sum_of_squares(ksj::array::VectorView<const float> input);
[[nodiscard]] double sum_of_squares(ksj::array::VectorView<const double> input);
[[nodiscard]] float sum_of_squares(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] double sum_of_squares(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] float root_sum_of_squares(ksj::array::VectorView<const float> input);
[[nodiscard]] double root_sum_of_squares(ksj::array::VectorView<const double> input);
[[nodiscard]] float root_sum_of_squares(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] double root_sum_of_squares(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] float max_abs(ksj::array::VectorView<const float> input);
[[nodiscard]] double max_abs(ksj::array::VectorView<const double> input);
[[nodiscard]] float max_abs(ksj::array::VectorView<const ksj::base::cf32> input);
[[nodiscard]] double max_abs(ksj::array::VectorView<const ksj::base::cf64> input);

[[nodiscard]] float l1_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs);
[[nodiscard]] double l1_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs);
[[nodiscard]] float l1_distance(ksj::array::VectorView<const ksj::base::cf32> lhs,
                                ksj::array::VectorView<const ksj::base::cf32> rhs);
[[nodiscard]] double l1_distance(ksj::array::VectorView<const ksj::base::cf64> lhs,
                                 ksj::array::VectorView<const ksj::base::cf64> rhs);

[[nodiscard]] float l2_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs);
[[nodiscard]] double l2_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs);
[[nodiscard]] float l2_distance(ksj::array::VectorView<const ksj::base::cf32> lhs,
                                ksj::array::VectorView<const ksj::base::cf32> rhs);
[[nodiscard]] double l2_distance(ksj::array::VectorView<const ksj::base::cf64> lhs,
                                 ksj::array::VectorView<const ksj::base::cf64> rhs);

[[nodiscard]] float linf_distance(ksj::array::VectorView<const float> lhs, ksj::array::VectorView<const float> rhs);
[[nodiscard]] double linf_distance(ksj::array::VectorView<const double> lhs, ksj::array::VectorView<const double> rhs);
[[nodiscard]] float linf_distance(ksj::array::VectorView<const ksj::base::cf32> lhs,
                                  ksj::array::VectorView<const ksj::base::cf32> rhs);
[[nodiscard]] double linf_distance(ksj::array::VectorView<const ksj::base::cf64> lhs,
                                   ksj::array::VectorView<const ksj::base::cf64> rhs);

[[nodiscard]] float centered_magnitude_average(ksj::array::CubeView<const float> input, std::size_t rows,
                                               std::size_t cols);
[[nodiscard]] double centered_magnitude_average(ksj::array::CubeView<const double> input, std::size_t rows,
                                                std::size_t cols);
[[nodiscard]] float centered_magnitude_average(ksj::array::CubeView<const ksj::base::cf32> input, std::size_t rows,
                                               std::size_t cols);
[[nodiscard]] double centered_magnitude_average(ksj::array::CubeView<const ksj::base::cf64> input, std::size_t rows,
                                                std::size_t cols);

[[nodiscard]] float squared_l2_norm(ksj::array::CubeView<const float> input);
[[nodiscard]] double squared_l2_norm(ksj::array::CubeView<const double> input);
[[nodiscard]] float squared_l2_norm(ksj::array::CubeView<const ksj::base::cf32> input);
[[nodiscard]] double squared_l2_norm(ksj::array::CubeView<const ksj::base::cf64> input);

[[nodiscard]] float squared_l2_distance(ksj::array::CubeView<const float> lhs, ksj::array::CubeView<const float> rhs);
[[nodiscard]] double squared_l2_distance(ksj::array::CubeView<const double> lhs,
                                         ksj::array::CubeView<const double> rhs);
[[nodiscard]] float squared_l2_distance(ksj::array::CubeView<const ksj::base::cf32> lhs,
                                        ksj::array::CubeView<const ksj::base::cf32> rhs);
[[nodiscard]] double squared_l2_distance(ksj::array::CubeView<const ksj::base::cf64> lhs,
                                         ksj::array::CubeView<const ksj::base::cf64> rhs);

void sum_of_squares_across(ksj::array::CubeView<const float> input, ksj::array::MatrixView<float> output,
                           ksj::array::Dim dim);
void sum_of_squares_across(ksj::array::CubeView<const double> input, ksj::array::MatrixView<double> output,
                           ksj::array::Dim dim);
void sum_of_squares_across(ksj::array::CubeView<const ksj::base::cf32> input, ksj::array::MatrixView<float> output,
                           ksj::array::Dim dim);
void sum_of_squares_across(ksj::array::CubeView<const ksj::base::cf64> input, ksj::array::MatrixView<double> output,
                           ksj::array::Dim dim);

void root_sum_of_squares_across(ksj::array::CubeView<const float> input, ksj::array::MatrixView<float> output,
                                ksj::array::Dim dim);
void root_sum_of_squares_across(ksj::array::CubeView<const double> input, ksj::array::MatrixView<double> output,
                                ksj::array::Dim dim);
void root_sum_of_squares_across(ksj::array::CubeView<const ksj::base::cf32> input, ksj::array::MatrixView<float> output,
                                ksj::array::Dim dim);
void root_sum_of_squares_across(ksj::array::CubeView<const ksj::base::cf64> input,
                                ksj::array::MatrixView<double> output, ksj::array::Dim dim);

} // namespace ksj::stats::detail::eigen
