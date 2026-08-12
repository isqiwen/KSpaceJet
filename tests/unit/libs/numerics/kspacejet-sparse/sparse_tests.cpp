#include "kspacejet/base/types.hpp"
#include "kspacejet/sparse/sparse.hpp"

#include <gtest/gtest.h>

#include <array>
namespace {

TEST(KSpaceJetSparse, MultipliesCsrMatrixByPooledVector) {
  constexpr std::array<std::size_t, 3> row_offsets{0, 2, 3};
  constexpr std::array<std::size_t, 3> column_indices{0, 1, 1};
  constexpr std::array<double, 3> values{1.0, 2.0, 3.0};
  const ksj::sparse::CsrMatrix<double> matrix(2, 2, row_offsets, column_indices, values);
  auto vector = ksj::array::make_pooled_vector<double>(2);
  vector(0) = 4.0;
  vector(1) = 5.0;

  const auto output = ksj::sparse::spmv(matrix, vector);
  auto output_storage = ksj::array::make_pooled_vector<double>(2);
  ksj::sparse::spmv(matrix, vector, output_storage);

  ASSERT_EQ(2U, output.size());
  EXPECT_DOUBLE_EQ(14.0, output(0));
  EXPECT_DOUBLE_EQ(15.0, output(1));
  EXPECT_DOUBLE_EQ(14.0, output_storage(0));
  EXPECT_DOUBLE_EQ(15.0, output_storage(1));
}

TEST(KSpaceJetSparse, ReusesCsrPlanForVectorAndMatrixProducts) {
  constexpr std::array<std::size_t, 3> row_offsets{0, 2, 3};
  constexpr std::array<std::size_t, 3> column_indices{0, 1, 1};
  constexpr std::array<double, 3> values{1.0, 2.0, 3.0};
  const ksj::sparse::CsrMatrix<double> matrix(2, 2, row_offsets, column_indices, values);
  auto plan = ksj::sparse::make_csr_plan(matrix);

  auto vector = ksj::array::make_pooled_vector<double>(2);
  auto vector_output = ksj::array::make_pooled_vector<double>(2);
  vector(0) = 4.0;
  vector(1) = 5.0;
  ksj::sparse::spmv(plan, vector, vector_output);
  EXPECT_DOUBLE_EQ(14.0, vector_output(0));
  EXPECT_DOUBLE_EQ(15.0, vector_output(1));

  vector(0) = 1.0;
  vector(1) = 2.0;
  ksj::sparse::spmv(plan, vector, vector_output);
  EXPECT_DOUBLE_EQ(5.0, vector_output(0));
  EXPECT_DOUBLE_EQ(6.0, vector_output(1));

  auto dense = ksj::array::make_pooled_matrix<double>(2, 2);
  auto dense_output = ksj::array::make_pooled_matrix<double>(2, 2);
  dense(0, 0) = 4.0;
  dense(0, 1) = 6.0;
  dense(1, 0) = 5.0;
  dense(1, 1) = 7.0;
  ksj::sparse::spmm(plan, dense, dense_output);
  EXPECT_DOUBLE_EQ(14.0, dense_output(0, 0));
  EXPECT_DOUBLE_EQ(20.0, dense_output(0, 1));
  EXPECT_DOUBLE_EQ(15.0, dense_output(1, 0));
  EXPECT_DOUBLE_EQ(21.0, dense_output(1, 1));
}

TEST(KSpaceJetSparse, MultipliesCsrMatrixByDenseMatrix) {
  constexpr std::array<std::size_t, 3> row_offsets{0, 2, 3};
  constexpr std::array<std::size_t, 3> column_indices{0, 1, 1};
  constexpr std::array<double, 3> values{1.0, 2.0, 3.0};
  const ksj::sparse::CsrMatrix<double> matrix(2, 2, row_offsets, column_indices, values);
  auto dense = ksj::array::make_pooled_matrix<double>(2, 2);
  dense(0, 0) = 4.0;
  dense(0, 1) = 6.0;
  dense(1, 0) = 5.0;
  dense(1, 1) = 7.0;

  const auto output = ksj::sparse::spmm(matrix, dense);
  auto output_storage = ksj::array::make_pooled_matrix<double>(2, 2);
  ksj::sparse::spmm(matrix, dense, output_storage);

  ASSERT_EQ(2U, output.rows());
  ASSERT_EQ(2U, output.cols());
  EXPECT_DOUBLE_EQ(14.0, output(0, 0));
  EXPECT_DOUBLE_EQ(20.0, output(0, 1));
  EXPECT_DOUBLE_EQ(15.0, output(1, 0));
  EXPECT_DOUBLE_EQ(21.0, output(1, 1));
  EXPECT_DOUBLE_EQ(14.0, output_storage(0, 0));
  EXPECT_DOUBLE_EQ(21.0, output_storage(1, 1));
}

TEST(KSpaceJetSparse, HandlesAliasedVectorAndMatrixOutputs) {
  constexpr std::array<int, 3> row_offsets{0, 2, 4};
  constexpr std::array<int, 4> column_indices{0, 1, 0, 1};
  constexpr std::array<double, 4> values{1.0, 2.0, 3.0, 4.0};
  const ksj::sparse::CsrMatrix<double> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const double>(values.data(), values.size()));

  auto vector = ksj::array::make_pooled_vector<double>(2);
  vector(0) = 5.0;
  vector(1) = 7.0;
  ksj::sparse::spmv(matrix, vector.view(), vector.view());
  EXPECT_DOUBLE_EQ(19.0, vector(0));
  EXPECT_DOUBLE_EQ(43.0, vector(1));

  auto dense = ksj::array::make_pooled_matrix<double>(2, 2);
  dense(0, 0) = 1.0;
  dense(0, 1) = 2.0;
  dense(1, 0) = 3.0;
  dense(1, 1) = 4.0;
  ksj::sparse::spmm(matrix, dense.view(), dense.view());
  EXPECT_DOUBLE_EQ(7.0, dense(0, 0));
  EXPECT_DOUBLE_EQ(10.0, dense(0, 1));
  EXPECT_DOUBLE_EQ(15.0, dense(1, 0));
  EXPECT_DOUBLE_EQ(22.0, dense(1, 1));
}

TEST(KSpaceJetSparse, MultipliesCsrMatrixByRowMajorDenseView) {
  constexpr std::array<int, 3> row_offsets{0, 2, 3};
  constexpr std::array<int, 3> column_indices{0, 1, 1};
  constexpr std::array<double, 3> values{1.0, 2.0, 3.0};
  const ksj::sparse::CsrMatrix<double> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const double>(values.data(), values.size()));
  std::array<double, 4> dense_storage{4.0, 5.0, 6.0, 7.0};
  std::array<double, 4> output_storage{};

  ksj::sparse::spmm(matrix, ksj::array::MatrixView<const double>(dense_storage.data(), 2U, 2U),
                    ksj::array::MatrixView<double>(output_storage.data(), 2U, 2U));

  EXPECT_DOUBLE_EQ(16.0, output_storage[0]);
  EXPECT_DOUBLE_EQ(19.0, output_storage[1]);
  EXPECT_DOUBLE_EQ(18.0, output_storage[2]);
  EXPECT_DOUBLE_EQ(21.0, output_storage[3]);
}

TEST(KSpaceJetSparse, MultipliesConjugateTransposeByDenseMatrix) {
  constexpr std::array<int, 3> row_offsets{0, 1, 2};
  constexpr std::array<int, 2> column_indices{0, 1};
  const std::array<ksj::base::cf32, 2> values{ksj::base::cf32{1.0F, 2.0F}, ksj::base::cf32{3.0F, -4.0F}};
  const ksj::sparse::CsrMatrix<ksj::base::cf32> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const ksj::base::cf32>(values.data(), values.size()));
  auto dense = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 1);
  dense(0, 0) = {2.0F, 0.0F};
  dense(1, 0) = {0.0F, 1.0F};
  auto output = ksj::array::make_pooled_matrix<ksj::base::cf32>(2, 1);

  ksj::sparse::spmm(matrix, dense.view(), output.view(), ksj::sparse::SparseOperation::conjugate_transpose);

  EXPECT_FLOAT_EQ(2.0F, output(0, 0).real());
  EXPECT_FLOAT_EQ(-4.0F, output(0, 0).imag());
  EXPECT_FLOAT_EQ(-4.0F, output(1, 0).real());
  EXPECT_FLOAT_EQ(3.0F, output(1, 0).imag());
}

TEST(KSpaceJetSparse, ConvertsCsrOperationToCsr) {
  constexpr std::array<int, 3> row_offsets{0, 2, 3};
  constexpr std::array<int, 3> column_indices{0, 1, 1};
  constexpr std::array<double, 3> values{1.0, 2.0, 3.0};
  const ksj::sparse::CsrMatrix<double> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const double>(values.data(), values.size()));

  const auto transposed = ksj::sparse::convert_csr(matrix, ksj::sparse::SparseOperation::transpose);
  auto vector = ksj::array::make_pooled_vector<double>(2);
  vector(0) = 1.0;
  vector(1) = 1.0;
  const auto output = ksj::sparse::spmv(transposed, vector);

  EXPECT_DOUBLE_EQ(1.0, output(0));
  EXPECT_DOUBLE_EQ(5.0, output(1));
}

TEST(KSpaceJetSparse, AddsCsrMatrices) {
  constexpr std::array<int, 3> a_row_offsets{0, 2, 3};
  constexpr std::array<int, 3> a_column_indices{0, 1, 1};
  constexpr std::array<double, 3> a_values{1.0, 2.0, 3.0};
  constexpr std::array<int, 3> b_row_offsets{0, 1, 3};
  constexpr std::array<int, 3> b_column_indices{0, 0, 1};
  constexpr std::array<double, 3> b_values{4.0, 5.0, 6.0};
  const ksj::sparse::CsrMatrix<double> lhs(
    2, 2, ksj::array::VectorView<const int>(a_row_offsets.data(), a_row_offsets.size()),
    ksj::array::VectorView<const int>(a_column_indices.data(), a_column_indices.size()),
    ksj::array::VectorView<const double>(a_values.data(), a_values.size()));
  const ksj::sparse::CsrMatrix<double> rhs(
    2, 2, ksj::array::VectorView<const int>(b_row_offsets.data(), b_row_offsets.size()),
    ksj::array::VectorView<const int>(b_column_indices.data(), b_column_indices.size()),
    ksj::array::VectorView<const double>(b_values.data(), b_values.size()));

  const auto sum = ksj::sparse::add(lhs, 2.0, rhs);
  auto vector = ksj::array::make_pooled_vector<double>(2);
  vector(0) = 1.0;
  vector(1) = 1.0;
  const auto output = ksj::sparse::spmv(sum, vector);

  EXPECT_DOUBLE_EQ(10.0, output(0));
  EXPECT_DOUBLE_EQ(17.0, output(1));
}

TEST(KSpaceJetSparse, SolvesSparseTriangularVectorAndMatrixSystems) {
  constexpr std::array<int, 3> row_offsets{0, 1, 3};
  constexpr std::array<int, 3> column_indices{0, 0, 1};
  constexpr std::array<double, 3> values{2.0, 3.0, 1.0};
  const ksj::sparse::CsrMatrix<double> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const double>(values.data(), values.size()));

  auto rhs = ksj::array::make_pooled_vector<double>(2);
  rhs(0) = 4.0;
  rhs(1) = 5.0;
  const auto solution =
    ksj::sparse::spsv(matrix, rhs, ksj::sparse::SparseTriangle::lower, ksj::sparse::SparseDiagonal::non_unit);
  EXPECT_DOUBLE_EQ(2.0, solution(0));
  EXPECT_DOUBLE_EQ(-1.0, solution(1));

  auto dense_rhs = ksj::array::make_pooled_matrix<double>(2, 2);
  dense_rhs(0, 0) = 4.0;
  dense_rhs(0, 1) = 8.0;
  dense_rhs(1, 0) = 5.0;
  dense_rhs(1, 1) = 13.0;
  const auto dense_solution =
    ksj::sparse::spsm(matrix, dense_rhs, ksj::sparse::SparseTriangle::lower, ksj::sparse::SparseDiagonal::non_unit);
  EXPECT_DOUBLE_EQ(2.0, dense_solution(0, 0));
  EXPECT_DOUBLE_EQ(-1.0, dense_solution(1, 0));
  EXPECT_DOUBLE_EQ(4.0, dense_solution(0, 1));
  EXPECT_DOUBLE_EQ(1.0, dense_solution(1, 1));
}

TEST(KSpaceJetSparse, ReusesCsrPlanForTriangularSolves) {
  constexpr std::array<int, 3> row_offsets{0, 1, 3};
  constexpr std::array<int, 3> column_indices{0, 0, 1};
  constexpr std::array<double, 3> values{2.0, 3.0, 1.0};
  const ksj::sparse::CsrMatrix<double> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const double>(values.data(), values.size()));
  auto plan = ksj::sparse::make_csr_plan(matrix);

  auto rhs = ksj::array::make_pooled_vector<double>(2);
  auto solution = ksj::array::make_pooled_vector<double>(2);
  rhs(0) = 4.0;
  rhs(1) = 5.0;
  ksj::sparse::spsv(plan, rhs, solution, ksj::sparse::SparseTriangle::lower, ksj::sparse::SparseDiagonal::non_unit);
  EXPECT_DOUBLE_EQ(2.0, solution(0));
  EXPECT_DOUBLE_EQ(-1.0, solution(1));

  auto dense_rhs = ksj::array::make_pooled_matrix<double>(2, 2);
  auto dense_solution = ksj::array::make_pooled_matrix<double>(2, 2);
  dense_rhs(0, 0) = 4.0;
  dense_rhs(0, 1) = 8.0;
  dense_rhs(1, 0) = 5.0;
  dense_rhs(1, 1) = 13.0;
  ksj::sparse::spsm(plan, dense_rhs, dense_solution, ksj::sparse::SparseTriangle::lower,
                    ksj::sparse::SparseDiagonal::non_unit);
  EXPECT_DOUBLE_EQ(2.0, dense_solution(0, 0));
  EXPECT_DOUBLE_EQ(-1.0, dense_solution(1, 0));
  EXPECT_DOUBLE_EQ(4.0, dense_solution(0, 1));
  EXPECT_DOUBLE_EQ(1.0, dense_solution(1, 1));
}

TEST(KSpaceJetSparse, HandlesAliasedSparseTriangularSolveOutput) {
  constexpr std::array<int, 3> row_offsets{0, 1, 3};
  constexpr std::array<int, 3> column_indices{0, 0, 1};
  constexpr std::array<double, 3> values{2.0, 3.0, 1.0};
  const ksj::sparse::CsrMatrix<double> matrix(
    2, 2, ksj::array::VectorView<const int>(row_offsets.data(), row_offsets.size()),
    ksj::array::VectorView<const int>(column_indices.data(), column_indices.size()),
    ksj::array::VectorView<const double>(values.data(), values.size()));

  auto rhs = ksj::array::make_pooled_vector<double>(2);
  rhs(0) = 4.0;
  rhs(1) = 5.0;
  ksj::sparse::spsv(matrix, rhs.view(), rhs.view(), ksj::sparse::SparseTriangle::lower,
                    ksj::sparse::SparseDiagonal::non_unit);
  EXPECT_DOUBLE_EQ(2.0, rhs(0));
  EXPECT_DOUBLE_EQ(-1.0, rhs(1));
}

} // namespace
