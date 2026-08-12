#include "kspacejet/optimization/detail/eigen/eigen_optimization_least_squares.hpp"
#include "kspacejet/array/detail/eigen/eigen_array_adapter.hpp"

#include "kspacejet/optimization/least_squares.hpp"

#include <Eigen/Cholesky>
#include <Eigen/QR>
#include <Eigen/SVD>

namespace ksj::optimization::detail::eigen {
namespace {
using ksj::array::detail::eigen_adapter::as_eigen;

template <typename T>
void least_squares_impl(ksj::array::MatrixView<const T> matrix, ksj::array::VectorView<const T> rhs,
                        ksj::array::VectorView<T> output, const LeastSquaresMethod method) {
  switch (method) {
    case LeastSquaresMethod::qr:
      as_eigen(output) = as_eigen(matrix).colPivHouseholderQr().solve(as_eigen(rhs));
      break;
    case LeastSquaresMethod::svd:
      as_eigen(output) = as_eigen(matrix).bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(as_eigen(rhs));
      break;
    case LeastSquaresMethod::normal_equations:
      as_eigen(output) =
        (as_eigen(matrix).transpose() * as_eigen(matrix)).ldlt().solve(as_eigen(matrix).transpose() * as_eigen(rhs));
      break;
  }
}

} // namespace

void least_squares(ksj::array::MatrixView<const float> matrix, ksj::array::VectorView<const float> rhs,
                   ksj::array::VectorView<float> output, const LeastSquaresMethod method) {
  least_squares_impl(matrix, rhs, output, method);
}

void least_squares(ksj::array::MatrixView<const double> matrix, ksj::array::VectorView<const double> rhs,
                   ksj::array::VectorView<double> output, const LeastSquaresMethod method) {
  least_squares_impl(matrix, rhs, output, method);
}

} // namespace ksj::optimization::detail::eigen
