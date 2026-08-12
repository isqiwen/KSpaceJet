#include "kspacejet/base/types.hpp"
#include "kspacejet/matio/mat_file.hpp"

#include "kspacejet/memory/memory_broker.hpp"

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

[[nodiscard]] ksj::memory::MemoryPoolOptions matio_test_memory_pool_options() {
  ksj::memory::MemoryPoolOptions options;
  options.pooling_enabled = false;
  return options;
}

class MatioMemoryBrokerEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    if (!ksj::memory::MemoryBroker::configure_instance(matio_test_memory_pool_options())) {
      ADD_FAILURE() << "kspacejet-memory broker was initialized before kspacejet-matio test memory configuration";
    }
  }
};

const auto* const kMatioMemoryBrokerEnvironment = ::testing::AddGlobalTestEnvironment(new MatioMemoryBrokerEnvironment);

[[nodiscard]] std::filesystem::path make_temp_mat_path(const std::string& name) {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_matio_tests";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  auto path = directory / (name + ".mat");
  std::filesystem::remove(path, error);
  return path;
}

template <typename T> void expect_near_complex(const std::complex<T>& actual, const std::complex<T>& expected) {
  EXPECT_NEAR(expected.real(), actual.real(), static_cast<T>(1.0E-5));
  EXPECT_NEAR(expected.imag(), actual.imag(), static_cast<T>(1.0E-5));
}

} // namespace

TEST(KSpaceJetMatio, RoundTripsPooledMatrix) {
  const auto path = make_temp_mat_path("matrix");
  auto matrix = ksj::array::make_pooled_matrix<float>(2, 3);
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      matrix(row, col) = static_cast<float>(row * 10U + col);
    }
  }

  ksj::matio::write_matrix(path, "m", matrix, ksj::matio::Compression::none);
  const auto roundtrip = ksj::matio::read_matrix<float>(path, "m");

  ASSERT_EQ(2U, roundtrip.rows());
  ASSERT_EQ(3U, roundtrip.cols());
  for (std::size_t row = 0; row < matrix.rows(); ++row) {
    for (std::size_t col = 0; col < matrix.cols(); ++col) {
      EXPECT_FLOAT_EQ(matrix(row, col), roundtrip(row, col));
    }
  }
}

TEST(KSpaceJetMatio, RoundTripsComplexCube) {
  const auto path = make_temp_mat_path("complex_cube");
  auto cube = ksj::array::make_pooled_cube<ksj::base::cf32>(2, 2, 2);
  for (std::size_t i0 = 0; i0 < cube.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < cube.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < cube.dim2(); ++i2) {
        cube(i0, i1, i2) = ksj::base::cf32{static_cast<float>(i0 * 10U + i1 * 2U + i2),
                                           static_cast<float>(100U + i0 * 10U + i1 * 2U + i2)};
      }
    }
  }

  ksj::matio::write_cube(path, "cube", cube.view(), ksj::matio::Compression::none);
  const auto roundtrip = ksj::matio::read_cube<ksj::base::cf32>(path, "cube");

  ASSERT_EQ(2U, roundtrip.dim0());
  ASSERT_EQ(2U, roundtrip.dim1());
  ASSERT_EQ(2U, roundtrip.dim2());
  for (std::size_t i0 = 0; i0 < cube.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < cube.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < cube.dim2(); ++i2) {
        expect_near_complex(roundtrip(i0, i1, i2), cube(i0, i1, i2));
      }
    }
  }
}

TEST(KSpaceJetMatio, RoundTripsArray4D) {
  const auto path = make_temp_mat_path("array4d");
  auto array = ksj::array::make_pooled_array4d<std::int32_t>(2, 2, 2, 2);
  for (std::size_t i0 = 0; i0 < array.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < array.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < array.dim2(); ++i2) {
        for (std::size_t i3 = 0; i3 < array.dim3(); ++i3) {
          array(i0, i1, i2, i3) = static_cast<std::int32_t>(i0 * 100U + i1 * 10U + i2 * 2U + i3);
        }
      }
    }
  }

  ksj::matio::write_array4d(path, "array4d", array, ksj::matio::Compression::none);
  const auto roundtrip = ksj::matio::read_array4d<std::int32_t>(path, "array4d");

  ASSERT_EQ(2U, roundtrip.dim0());
  ASSERT_EQ(2U, roundtrip.dim1());
  ASSERT_EQ(2U, roundtrip.dim2());
  ASSERT_EQ(2U, roundtrip.dim3());
  for (std::size_t i0 = 0; i0 < array.dim0(); ++i0) {
    for (std::size_t i1 = 0; i1 < array.dim1(); ++i1) {
      for (std::size_t i2 = 0; i2 < array.dim2(); ++i2) {
        for (std::size_t i3 = 0; i3 < array.dim3(); ++i3) {
          EXPECT_EQ(array(i0, i1, i2, i3), roundtrip(i0, i1, i2, i3));
        }
      }
    }
  }
}

TEST(KSpaceJetMatio, RoundTripsSparseCsrMatrix) {
  const auto path = make_temp_mat_path("sparse");
  constexpr std::array<std::size_t, 3> row_offsets{0U, 2U, 3U};
  constexpr std::array<std::size_t, 3> column_indices{0U, 2U, 1U};
  constexpr std::array<double, 3> values{1.5, 2.5, 3.5};
  const ksj::sparse::CsrMatrix<double> matrix(2, 3, row_offsets, column_indices, values);

  ksj::matio::write_sparse(path, "s", matrix, ksj::matio::Compression::none);
  const auto roundtrip = ksj::matio::read_sparse<double>(path, "s");

  ASSERT_EQ(2U, roundtrip.rows());
  ASSERT_EQ(3U, roundtrip.cols());
  ASSERT_EQ(3U, roundtrip.nonzeros());
  EXPECT_EQ(0, roundtrip.row_offsets()(0));
  EXPECT_EQ(2, roundtrip.row_offsets()(1));
  EXPECT_EQ(3, roundtrip.row_offsets()(2));
  EXPECT_EQ(0, roundtrip.column_indices()(0));
  EXPECT_EQ(2, roundtrip.column_indices()(1));
  EXPECT_EQ(1, roundtrip.column_indices()(2));
  EXPECT_DOUBLE_EQ(1.5, roundtrip.values()(0));
  EXPECT_DOUBLE_EQ(2.5, roundtrip.values()(1));
  EXPECT_DOUBLE_EQ(3.5, roundtrip.values()(2));
}
