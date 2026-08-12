#include "../eigen_test_adapter.hpp"
#include "kspacejet/base/types.hpp"
#include "kspacejet/array/array.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetArrayPooledEigen, VectorExposesEigenExpressionsWithoutCopies) {
  auto vector = ksj::array::make_pooled_vector<ksj::base::cf64>(3);
  as_eigen(vector).setConstant({1.0, 2.0});

  EXPECT_EQ((ksj::base::cf64{1.0, 2.0}), vector(0));
  EXPECT_EQ((ksj::base::cf64{1.0, 2.0}), vector(2));
  EXPECT_EQ((ksj::base::cf64{3.0, 6.0}), as_eigen(vector).sum());
}

TEST(KSpaceJetArrayPooledEigen, VectorViewReferencesUnderlyingVector) {
  auto vector = ksj::array::make_pooled_vector<int>(5);
  for (std::size_t index = 0; index < vector.size(); ++index) {
    vector(index) = static_cast<int>(index + 1U);
  }

  auto even_indices = vector.subview(ksj::array::slice(0U, vector.size(), 2U));
  even_indices(1) = 20;

  EXPECT_EQ(1, even_indices(0));
  EXPECT_EQ(20, vector(2));
  EXPECT_EQ(5, even_indices(2));

  auto middle = ksj::array::vector_view(vector).subview(ksj::array::slice(1U, 4U));
  ksj::array::reverse_in_place(middle);
  EXPECT_EQ(4, vector(1));
  EXPECT_EQ(20, vector(2));
  EXPECT_EQ(2, vector(3));

  ksj::array::rotate_left_in_place(middle, 1U);
  EXPECT_EQ(20, vector(1));
  EXPECT_EQ(2, vector(2));
  EXPECT_EQ(4, vector(3));

  EXPECT_TRUE(ksj::array::vector_view(vector).subview(ksj::array::slice(0U, 0U)).empty());
  EXPECT_THROW((void)ksj::array::vector_view(vector).subview(ksj::array::slice(4U, 6U)), std::out_of_range);

  const auto& const_vector = vector;
  const auto const_view = ksj::array::vector_view(const_vector);
  EXPECT_EQ(5U, const_view.size());
  EXPECT_EQ(2, const_view(2));
}

} // namespace
