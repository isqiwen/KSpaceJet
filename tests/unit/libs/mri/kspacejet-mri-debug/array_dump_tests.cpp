#include "kspacejet/base/types.hpp"
#include "kspacejet/mri/debug/array_dump.hpp"

#include "kspacejet/array/array.hpp"

#include "matio.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

struct MatFileDeleter {
  void operator()(mat_t* file) const noexcept {
    if (file != nullptr) {
      Mat_Close(file);
    }
  }
};

struct MatVarDeleter {
  void operator()(matvar_t* variable) const noexcept {
    if (variable != nullptr) {
      Mat_VarFree(variable);
    }
  }
};

using MatFilePtr = std::unique_ptr<mat_t, MatFileDeleter>;
using MatVarPtr = std::unique_ptr<matvar_t, MatVarDeleter>;

[[nodiscard]] std::filesystem::path make_temp_dump_dir(const std::string& name) {
  auto path = std::filesystem::temp_directory_path() / "ksj_array_dump_tests" / name;
  std::error_code error;
  std::filesystem::remove_all(path, error);
  std::filesystem::create_directories(path, error);
  return path;
}

} // namespace

TEST(KSpaceJetMriDebugArrayDump, WritesRowMajorArrayAsMatlabOrderedMatFile) {
  const auto dump_dir = make_temp_dump_dir("row_major");

  auto matrix = ksj::array::make_pooled_image<float>(2, 3);
  for (std::size_t row = 0; row < matrix.height(); ++row) {
    for (std::size_t col = 0; col < matrix.width(); ++col) {
      matrix(row, col) = static_cast<float>(row * 10 + col);
    }
  }

  const ksj::mri::debug::ArrayMatDumpOptions options{
    .directory = dump_dir,
    .force = true,
    .append = false,
    .compress = false,
    .file_version = ksj::mri::debug::ArrayMatFileVersion::mat5,
  };
  ASSERT_EQ(ksj::mri::debug::kArrayDumpOk,
            ksj::mri::debug::dump_mat_array(matrix, "row_major_matrix", "rowMajorMatrix", options));

  const auto dump_path = dump_dir / "row_major_matrix.mat";
  ASSERT_TRUE(std::filesystem::exists(dump_path));

  MatFilePtr file{Mat_Open(dump_path.string().c_str(), MAT_ACC_RDONLY)};
  ASSERT_NE(nullptr, file);

  MatVarPtr variable{Mat_VarRead(file.get(), "rowMajorMatrix")};
  ASSERT_NE(nullptr, variable);
  ASSERT_EQ(2, variable->rank);
  ASSERT_EQ(2U, variable->dims[0]);
  ASSERT_EQ(3U, variable->dims[1]);
  ASSERT_EQ(MAT_C_SINGLE, variable->class_type);
  ASSERT_EQ(MAT_T_SINGLE, variable->data_type);
  ASSERT_EQ(0, variable->isComplex);

  const auto* data = static_cast<const float*>(variable->data);
  ASSERT_NE(nullptr, data);
  const std::vector<float> actual{data, data + 6};
  EXPECT_EQ((std::vector<float>{0.0F, 10.0F, 1.0F, 11.0F, 2.0F, 12.0F}), actual);
}

TEST(KSpaceJetMriDebugArrayDump, WritesComplexArraysWithSplitRealImaginaryStorage) {
  const auto dump_dir = make_temp_dump_dir("complex");

  auto vector = ksj::array::make_pooled_vector<ksj::base::cf64>(2);
  vector(0) = {1.0, 2.0};
  vector(1) = {3.0, 4.0};

  const ksj::mri::debug::ArrayMatDumpOptions options{
    .directory = dump_dir,
    .force = true,
    .append = false,
    .compress = false,
    .file_version = ksj::mri::debug::ArrayMatFileVersion::mat5,
  };
  ASSERT_EQ(ksj::mri::debug::kArrayDumpOk,
            ksj::mri::debug::dump_mat_array(vector, "complex_vector", "complexVector", options));

  const auto dump_path = dump_dir / "complex_vector.mat";
  MatFilePtr file{Mat_Open(dump_path.string().c_str(), MAT_ACC_RDONLY)};
  ASSERT_NE(nullptr, file);

  MatVarPtr variable{Mat_VarRead(file.get(), "complexVector")};
  ASSERT_NE(nullptr, variable);
  ASSERT_EQ(2, variable->rank);
  ASSERT_EQ(2U, variable->dims[0]);
  ASSERT_EQ(1U, variable->dims[1]);
  ASSERT_EQ(MAT_C_DOUBLE, variable->class_type);
  ASSERT_EQ(MAT_T_DOUBLE, variable->data_type);
  ASSERT_NE(0, variable->isComplex);

  const auto* complex_data = static_cast<const mat_complex_split_t*>(variable->data);
  ASSERT_NE(nullptr, complex_data);
  const auto* real = static_cast<const double*>(complex_data->Re);
  const auto* imag = static_cast<const double*>(complex_data->Im);
  ASSERT_NE(nullptr, real);
  ASSERT_NE(nullptr, imag);
  EXPECT_EQ((std::vector<double>{1.0, 3.0}), (std::vector<double>{real, real + 2}));
  EXPECT_EQ((std::vector<double>{2.0, 4.0}), (std::vector<double>{imag, imag + 2}));
}

TEST(KSpaceJetMriDebugArrayDump, RespectsDebugSwitchUnlessForced) {
  const auto dump_dir = make_temp_dump_dir("disabled");
  const auto vector = ksj::array::make_pooled_vector<float>(2);
  const ksj::mri::debug::ArrayMatDumpOptions options{
    .directory = dump_dir,
    .force = false,
  };

  EXPECT_EQ(ksj::mri::debug::kArrayDumpOk,
            ksj::mri::debug::dump_mat_array(vector, "disabled_vector", "disabledVector", options));
  EXPECT_FALSE(std::filesystem::exists(dump_dir / "disabled_vector.mat"));
}
