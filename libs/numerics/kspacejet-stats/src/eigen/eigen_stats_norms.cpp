#include "kspacejet/stats/detail/eigen/eigen_stats_norms.hpp"
#include "eigen_stats_common.hpp"

namespace ksj::stats::detail::eigen {

float sum_of_squares(const ksj::array::VectorView<const float> input) {
  return impl::sum_of_squares_impl(input);
}

double sum_of_squares(const ksj::array::VectorView<const double> input) {
  return impl::sum_of_squares_impl(input);
}

float sum_of_squares(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::sum_of_squares_impl(input);
}

double sum_of_squares(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::sum_of_squares_impl(input);
}

float root_sum_of_squares(const ksj::array::VectorView<const float> input) {
  return impl::root_sum_of_squares_impl(input);
}

double root_sum_of_squares(const ksj::array::VectorView<const double> input) {
  return impl::root_sum_of_squares_impl(input);
}

float root_sum_of_squares(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::root_sum_of_squares_impl(input);
}

double root_sum_of_squares(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::root_sum_of_squares_impl(input);
}

float max_abs(const ksj::array::VectorView<const float> input) {
  return impl::max_abs_impl(input);
}

double max_abs(const ksj::array::VectorView<const double> input) {
  return impl::max_abs_impl(input);
}

float max_abs(const ksj::array::VectorView<const ksj::base::cf32> input) {
  return impl::max_abs_impl(input);
}

double max_abs(const ksj::array::VectorView<const ksj::base::cf64> input) {
  return impl::max_abs_impl(input);
}

float l1_distance(const ksj::array::VectorView<const float> lhs, const ksj::array::VectorView<const float> rhs) {
  return impl::l1_distance_impl(lhs, rhs);
}

double l1_distance(const ksj::array::VectorView<const double> lhs, const ksj::array::VectorView<const double> rhs) {
  return impl::l1_distance_impl(lhs, rhs);
}

float l1_distance(const ksj::array::VectorView<const ksj::base::cf32> lhs,
                  const ksj::array::VectorView<const ksj::base::cf32> rhs) {
  return impl::l1_distance_impl(lhs, rhs);
}

double l1_distance(const ksj::array::VectorView<const ksj::base::cf64> lhs,
                   const ksj::array::VectorView<const ksj::base::cf64> rhs) {
  return impl::l1_distance_impl(lhs, rhs);
}

float l2_distance(const ksj::array::VectorView<const float> lhs, const ksj::array::VectorView<const float> rhs) {
  return impl::l2_distance_impl(lhs, rhs);
}

double l2_distance(const ksj::array::VectorView<const double> lhs, const ksj::array::VectorView<const double> rhs) {
  return impl::l2_distance_impl(lhs, rhs);
}

float l2_distance(const ksj::array::VectorView<const ksj::base::cf32> lhs,
                  const ksj::array::VectorView<const ksj::base::cf32> rhs) {
  return impl::l2_distance_impl(lhs, rhs);
}

double l2_distance(const ksj::array::VectorView<const ksj::base::cf64> lhs,
                   const ksj::array::VectorView<const ksj::base::cf64> rhs) {
  return impl::l2_distance_impl(lhs, rhs);
}

float linf_distance(const ksj::array::VectorView<const float> lhs, const ksj::array::VectorView<const float> rhs) {
  return impl::linf_distance_impl(lhs, rhs);
}

double linf_distance(const ksj::array::VectorView<const double> lhs, const ksj::array::VectorView<const double> rhs) {
  return impl::linf_distance_impl(lhs, rhs);
}

float linf_distance(const ksj::array::VectorView<const ksj::base::cf32> lhs,
                    const ksj::array::VectorView<const ksj::base::cf32> rhs) {
  return impl::linf_distance_impl(lhs, rhs);
}

double linf_distance(const ksj::array::VectorView<const ksj::base::cf64> lhs,
                     const ksj::array::VectorView<const ksj::base::cf64> rhs) {
  return impl::linf_distance_impl(lhs, rhs);
}

float centered_magnitude_average(const ksj::array::CubeView<const float> input, const std::size_t rows,
                                 const std::size_t cols) {
  return impl::centered_magnitude_average_impl(input, rows, cols);
}

double centered_magnitude_average(const ksj::array::CubeView<const double> input, const std::size_t rows,
                                  const std::size_t cols) {
  return impl::centered_magnitude_average_impl(input, rows, cols);
}

float centered_magnitude_average(const ksj::array::CubeView<const ksj::base::cf32> input, const std::size_t rows,
                                 const std::size_t cols) {
  return impl::centered_magnitude_average_impl(input, rows, cols);
}

double centered_magnitude_average(const ksj::array::CubeView<const ksj::base::cf64> input, const std::size_t rows,
                                  const std::size_t cols) {
  return impl::centered_magnitude_average_impl(input, rows, cols);
}

float squared_l2_norm(const ksj::array::CubeView<const float> input) {
  return impl::squared_l2_norm_impl(input);
}

double squared_l2_norm(const ksj::array::CubeView<const double> input) {
  return impl::squared_l2_norm_impl(input);
}

float squared_l2_norm(const ksj::array::CubeView<const ksj::base::cf32> input) {
  return impl::squared_l2_norm_impl(input);
}

double squared_l2_norm(const ksj::array::CubeView<const ksj::base::cf64> input) {
  return impl::squared_l2_norm_impl(input);
}

float squared_l2_distance(const ksj::array::CubeView<const float> lhs, const ksj::array::CubeView<const float> rhs) {
  return impl::squared_l2_distance_impl(lhs, rhs);
}

double squared_l2_distance(const ksj::array::CubeView<const double> lhs, const ksj::array::CubeView<const double> rhs) {
  return impl::squared_l2_distance_impl(lhs, rhs);
}

float squared_l2_distance(const ksj::array::CubeView<const ksj::base::cf32> lhs,
                          const ksj::array::CubeView<const ksj::base::cf32> rhs) {
  return impl::squared_l2_distance_impl(lhs, rhs);
}

double squared_l2_distance(const ksj::array::CubeView<const ksj::base::cf64> lhs,
                           const ksj::array::CubeView<const ksj::base::cf64> rhs) {
  return impl::squared_l2_distance_impl(lhs, rhs);
}

void sum_of_squares_across(const ksj::array::CubeView<const float> input, const ksj::array::MatrixView<float> output,
                           const ksj::array::Dim dim) {
  impl::sum_of_squares_across_impl(input, output, dim);
}

void sum_of_squares_across(const ksj::array::CubeView<const double> input, const ksj::array::MatrixView<double> output,
                           const ksj::array::Dim dim) {
  impl::sum_of_squares_across_impl(input, output, dim);
}

void sum_of_squares_across(const ksj::array::CubeView<const ksj::base::cf32> input,
                           const ksj::array::MatrixView<float> output, const ksj::array::Dim dim) {
  impl::sum_of_squares_across_impl(input, output, dim);
}

void sum_of_squares_across(const ksj::array::CubeView<const ksj::base::cf64> input,
                           const ksj::array::MatrixView<double> output, const ksj::array::Dim dim) {
  impl::sum_of_squares_across_impl(input, output, dim);
}

void root_sum_of_squares_across(const ksj::array::CubeView<const float> input,
                                const ksj::array::MatrixView<float> output, const ksj::array::Dim dim) {
  impl::root_sum_of_squares_across_impl(input, output, dim);
}

void root_sum_of_squares_across(const ksj::array::CubeView<const double> input,
                                const ksj::array::MatrixView<double> output, const ksj::array::Dim dim) {
  impl::root_sum_of_squares_across_impl(input, output, dim);
}

void root_sum_of_squares_across(const ksj::array::CubeView<const ksj::base::cf32> input,
                                const ksj::array::MatrixView<float> output, const ksj::array::Dim dim) {
  impl::root_sum_of_squares_across_impl(input, output, dim);
}

void root_sum_of_squares_across(const ksj::array::CubeView<const ksj::base::cf64> input,
                                const ksj::array::MatrixView<double> output, const ksj::array::Dim dim) {
  impl::root_sum_of_squares_across_impl(input, output, dim);
}

} // namespace ksj::stats::detail::eigen
