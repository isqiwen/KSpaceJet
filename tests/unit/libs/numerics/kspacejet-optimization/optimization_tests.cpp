#include "../eigen_test_adapter.hpp"
#include "kspacejet/optimization/optimization.hpp"

#include <cstddef>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetOptimization, SolvesLeastSquaresWithQr) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  auto rhs = ksj::array::make_pooled_vector<double>(2);
  as_eigen(matrix).setIdentity();
  rhs(0) = 3.0;
  rhs(1) = 4.0;

  const auto solution = ksj::optimization::least_squares(matrix, rhs);

  ASSERT_EQ(2U, solution.size());
  EXPECT_NEAR(3.0, solution(0), 1e-12);
  EXPECT_NEAR(4.0, solution(1), 1e-12);
}

TEST(KSpaceJetOptimization, SolvesLeastSquaresIntoViewOutput) {
  auto matrix = ksj::array::make_pooled_matrix<double>(3, 2);
  auto rhs = ksj::array::make_pooled_vector<double>(3);
  matrix(0, 0) = 1.0;
  matrix(0, 1) = 0.0;
  matrix(1, 0) = 0.0;
  matrix(1, 1) = 1.0;
  matrix(2, 0) = 1.0;
  matrix(2, 1) = 1.0;
  rhs(0) = 2.0;
  rhs(1) = -1.0;
  rhs(2) = 1.0;

  auto solution = ksj::array::make_pooled_vector<double>(2);
  ksj::optimization::least_squares(matrix.view(), rhs.view(), solution.view());

  EXPECT_NEAR(2.0, solution(0), 1e-12);
  EXPECT_NEAR(-1.0, solution(1), 1e-12);
}

TEST(KSpaceJetOptimization, SolvesLeastSquaresWithSvdFromStridedConstMatrixView) {
  auto storage = ksj::array::make_pooled_matrix<double>(3, 3);
  storage(0, 0) = 1.0;
  storage(0, 1) = 0.0;
  storage(1, 0) = 0.0;
  storage(1, 1) = 1.0;
  storage(2, 0) = 1.0;
  storage(2, 1) = 1.0;

  auto rhs = ksj::array::make_pooled_vector<double>(3);
  rhs(0) = 2.0;
  rhs(1) = -1.0;
  rhs(2) = 1.0;
  auto solution = ksj::array::make_pooled_vector<double>(2);

  const auto matrix = ksj::array::as_const_view(storage.view().subview(ksj::array::_, ksj::array::slice(0U, 2U)));
  ksj::optimization::least_squares(matrix, ksj::array::as_const_view(rhs.view()), solution.view(),
                                   ksj::optimization::LeastSquaresMethod::svd);

  EXPECT_NEAR(2.0, solution(0), 1e-12);
  EXPECT_NEAR(-1.0, solution(1), 1e-12);
}

TEST(KSpaceJetOptimization, SolvesLeastSquaresIntoPooledOutput) {
  auto matrix = ksj::array::make_pooled_matrix<double>(2, 2);
  auto rhs = ksj::array::make_pooled_vector<double>(2);
  as_eigen(matrix).setIdentity();
  rhs(0) = 7.0;
  rhs(1) = 8.0;

  auto solution = ksj::array::make_pooled_vector<double>(2);
  ksj::optimization::least_squares(matrix, rhs, solution);

  EXPECT_NEAR(7.0, solution(0), 1e-12);
  EXPECT_NEAR(8.0, solution(1), 1e-12);
}

TEST(KSpaceJetOptimization, ReusesCallerOwnedWorkspaceForQrLeastSquares) {
  auto matrix = ksj::array::make_pooled_matrix<double>(130, 128);
  as_eigen(matrix).setZero();
  for (std::size_t index = 0U; index < 128U; ++index) {
    matrix(index, index) = 1.0;
  }

  auto expected = ksj::array::make_pooled_vector<double>(128);
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    expected(index) = static_cast<double>(index) * 0.5;
  }
  auto rhs = ksj::array::make_pooled_vector<double>(130);
  as_eigen(rhs) = as_eigen(matrix) * as_eigen(expected);

  auto solution = ksj::array::make_pooled_vector<double>(128);
  ksj::optimization::LeastSquaresWorkspace<double> workspace;
  ksj::optimization::least_squares(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                   solution.view(), workspace);
  EXPECT_NEAR(0.0, (as_eigen(solution) - as_eigen(expected)).norm(), 1.0e-12);

  const auto* matrix_work = workspace.qr.matrix_work.data();
  const auto* rhs_work = workspace.qr.rhs_vector_work.data();
  ASSERT_NE(nullptr, matrix_work);
  ASSERT_NE(nullptr, rhs_work);
  ksj::optimization::least_squares(ksj::array::as_const_view(matrix.view()), ksj::array::as_const_view(rhs.view()),
                                   solution.view(), workspace);
  EXPECT_EQ(matrix_work, workspace.qr.matrix_work.data());
  EXPECT_EQ(rhs_work, workspace.qr.rhs_vector_work.data());
}

TEST(KSpaceJetOptimization, MinimizesBoundedObjectiveWithDownhillSimplex) {
  auto objective = [](ksj::array::VectorView<const double> x) {
    const auto dx = x(0) - 2.0;
    const auto dy = x(1) + 3.0;
    return dx * dx + dy * dy;
  };

  const auto result =
    ksj::optimization::downhill_simplex(objective, {0.0, 0.0}, {-10.0, -10.0}, {10.0, 10.0}, 1.0e-10, {}, 200);

  ASSERT_EQ(2U, result.size());
  EXPECT_NEAR(2.0, result[0], 1.0e-3);
  EXPECT_NEAR(-3.0, result[1], 1.0e-3);
}

TEST(KSpaceJetOptimization, MinimizesBoundedObjectiveIntoViewOutput) {
  auto objective = [](ksj::array::VectorView<const double> x) {
    const auto dx = x(0) - 2.0;
    const auto dy = x(1) + 3.0;
    return dx * dx + dy * dy;
  };

  auto initial = ksj::array::make_pooled_vector<double>(2);
  auto lower_bounds = ksj::array::make_pooled_vector<double>(2);
  auto upper_bounds = ksj::array::make_pooled_vector<double>(2);
  auto output = ksj::array::make_pooled_vector<double>(2);
  initial(0) = 0.0;
  initial(1) = 0.0;
  lower_bounds(0) = -10.0;
  lower_bounds(1) = -10.0;
  upper_bounds(0) = 10.0;
  upper_bounds(1) = 10.0;

  ksj::optimization::downhill_simplex(objective, initial.view(), lower_bounds.view(), upper_bounds.view(),
                                      output.view(), 1.0e-10, {}, 200);

  EXPECT_NEAR(2.0, output(0), 1.0e-3);
  EXPECT_NEAR(-3.0, output(1), 1.0e-3);
}

TEST(KSpaceJetOptimization, MinimizesBoundedObjectiveWithPooledInput) {
  auto objective = [](ksj::array::VectorView<const double> x) {
    const auto dx = x(0) - 2.0;
    const auto dy = x(1) + 3.0;
    return dx * dx + dy * dy;
  };

  auto initial = ksj::array::make_pooled_vector<double>(2);
  auto lower_bounds = ksj::array::make_pooled_vector<double>(2);
  auto upper_bounds = ksj::array::make_pooled_vector<double>(2);
  initial(0) = 0.0;
  initial(1) = 0.0;
  lower_bounds(0) = -10.0;
  lower_bounds(1) = -10.0;
  upper_bounds(0) = 10.0;
  upper_bounds(1) = 10.0;

  const auto result = ksj::optimization::downhill_simplex(objective, initial, lower_bounds, upper_bounds, 1.0e-10, 200);

  EXPECT_NEAR(2.0, result(0), 1.0e-3);
  EXPECT_NEAR(-3.0, result(1), 1.0e-3);
}

} // namespace
