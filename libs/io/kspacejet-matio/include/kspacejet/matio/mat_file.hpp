#pragma once

#include "kspacejet/array/array.hpp"
#include "kspacejet/sparse/sparse.hpp"

#include "matio.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ksj::matio {

enum class Compression {
  none,
  zlib,
};

namespace detail {

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
using MatSparseIndex = std::remove_pointer_t<decltype(std::declval<mat_sparse_t>().ir)>;
using MatSparseCount = std::remove_cv_t<decltype(std::declval<mat_sparse_t>().ndata)>;

template <typename T> struct MatScalarTraits;

template <> struct MatScalarTraits<float> {
  static constexpr matio_classes class_type = MAT_C_SINGLE;
  static constexpr matio_types data_type = MAT_T_SINGLE;
};

template <> struct MatScalarTraits<double> {
  static constexpr matio_classes class_type = MAT_C_DOUBLE;
  static constexpr matio_types data_type = MAT_T_DOUBLE;
};

template <> struct MatScalarTraits<std::int8_t> {
  static constexpr matio_classes class_type = MAT_C_INT8;
  static constexpr matio_types data_type = MAT_T_INT8;
};

template <> struct MatScalarTraits<std::uint8_t> {
  static constexpr matio_classes class_type = MAT_C_UINT8;
  static constexpr matio_types data_type = MAT_T_UINT8;
};

template <> struct MatScalarTraits<std::int16_t> {
  static constexpr matio_classes class_type = MAT_C_INT16;
  static constexpr matio_types data_type = MAT_T_INT16;
};

template <> struct MatScalarTraits<std::uint16_t> {
  static constexpr matio_classes class_type = MAT_C_UINT16;
  static constexpr matio_types data_type = MAT_T_UINT16;
};

template <> struct MatScalarTraits<std::int32_t> {
  static constexpr matio_classes class_type = MAT_C_INT32;
  static constexpr matio_types data_type = MAT_T_INT32;
};

template <> struct MatScalarTraits<std::uint32_t> {
  static constexpr matio_classes class_type = MAT_C_UINT32;
  static constexpr matio_types data_type = MAT_T_UINT32;
};

template <> struct MatScalarTraits<std::int64_t> {
  static constexpr matio_classes class_type = MAT_C_INT64;
  static constexpr matio_types data_type = MAT_T_INT64;
};

template <> struct MatScalarTraits<std::uint64_t> {
  static constexpr matio_classes class_type = MAT_C_UINT64;
  static constexpr matio_types data_type = MAT_T_UINT64;
};

template <typename T>
inline constexpr bool supported_dense_scalar_v =
  requires { MatScalarTraits<std::remove_cv_t<ksj::array::real_scalar_t<T>>>::class_type; };

template <typename T> using value_type_t = std::remove_cv_t<std::remove_reference_t<T>>;

[[nodiscard]] inline matio_compression to_matio_compression(const Compression compression) {
  return compression == Compression::none ? MAT_COMPRESSION_NONE : MAT_COMPRESSION_ZLIB;
}

[[nodiscard]] inline std::string to_string(const std::filesystem::path& path) {
  return path.string();
}

[[nodiscard]] inline MatFilePtr open_read_only(const std::filesystem::path& path) {
  const auto path_string = to_string(path);
  MatFilePtr file{Mat_Open(path_string.c_str(), MAT_ACC_RDONLY)};
  if (!file) {
    throw std::runtime_error("failed to open MAT file for reading: " + path_string);
  }
  return file;
}

[[nodiscard]] inline MatFilePtr open_or_create(const std::filesystem::path& path) {
  const auto path_string = to_string(path);
  MatFilePtr file{Mat_Open(path_string.c_str(), MAT_ACC_RDWR)};
  if (!file) {
    file.reset(Mat_CreateVer(path_string.c_str(), nullptr, MAT_FT_MAT5));
  }
  if (!file) {
    throw std::runtime_error("failed to open MAT file for writing: " + path_string);
  }
  return file;
}

[[nodiscard]] inline MatVarPtr read_variable(const std::filesystem::path& path, const std::string_view variable_name) {
  auto file = open_read_only(path);
  const std::string name{variable_name};
  MatVarPtr variable{Mat_VarRead(file.get(), name.c_str())};
  if (!variable) {
    throw std::runtime_error("failed to read MAT variable: " + name);
  }
  return variable;
}

inline void write_variable(const std::filesystem::path& path, matvar_t* variable, const Compression compression) {
  auto file = open_or_create(path);
  if (Mat_VarWrite(file.get(), variable, to_matio_compression(compression)) != 0) {
    throw std::runtime_error("failed to write MAT variable");
  }
}

template <typename T> [[nodiscard]] bool is_negative(const T value) noexcept {
  if constexpr (std::is_signed_v<T>) {
    return value < T{};
  } else {
    return false;
  }
}

template <std::size_t Rank> [[nodiscard]] std::size_t element_count(const std::array<std::size_t, Rank>& dims) {
  std::size_t count = 1U;
  for (const auto dim : dims) {
    if (dim != 0U && count > std::numeric_limits<std::size_t>::max() / dim) {
      throw std::length_error("MAT variable dimensions overflow size_t");
    }
    count *= dim;
  }
  return count;
}

template <typename T> void check_dense_type(const matvar_t& variable) {
  using value_type = value_type_t<T>;
  using real_type = ksj::array::real_scalar_t<value_type>;
  static_assert(supported_dense_scalar_v<value_type>, "unsupported MAT dense scalar type");

  if (variable.class_type != MatScalarTraits<real_type>::class_type ||
      variable.data_type != MatScalarTraits<real_type>::data_type) {
    throw std::invalid_argument("MAT variable scalar type does not match requested output type");
  }
  if constexpr (!ksj::array::is_complex_v<value_type>) {
    if (variable.isComplex != 0) {
      throw std::invalid_argument("cannot read a complex MAT variable into a real output view");
    }
  }
}

template <typename T> [[nodiscard]] const T* real_data(const matvar_t& variable) {
  if (variable.data == nullptr) {
    return nullptr;
  }
  return static_cast<const T*>(variable.data);
}

template <typename T> [[nodiscard]] const T* complex_real_data(const matvar_t& variable) {
  if (variable.data == nullptr) {
    return nullptr;
  }
  const auto* split = static_cast<const mat_complex_split_t*>(variable.data);
  return static_cast<const T*>(split->Re);
}

template <typename T> [[nodiscard]] const T* complex_imag_data(const matvar_t& variable) {
  if (variable.data == nullptr) {
    return nullptr;
  }
  const auto* split = static_cast<const mat_complex_split_t*>(variable.data);
  return static_cast<const T*>(split->Im);
}

template <typename T> [[nodiscard]] T read_dense_value(const matvar_t& variable, const std::size_t matlab_index) {
  using value_type = value_type_t<T>;
  using real_type = ksj::array::real_scalar_t<value_type>;
  if constexpr (ksj::array::is_complex_v<value_type>) {
    if (variable.isComplex != 0) {
      const auto* real = complex_real_data<real_type>(variable);
      const auto* imag = complex_imag_data<real_type>(variable);
      return value_type(real == nullptr ? real_type{} : real[matlab_index],
                        imag == nullptr ? real_type{} : imag[matlab_index]);
    }
    const auto* data = real_data<real_type>(variable);
    return value_type(data == nullptr ? real_type{} : data[matlab_index], real_type{});
  } else {
    const auto* data = real_data<value_type>(variable);
    return data == nullptr ? value_type{} : data[matlab_index];
  }
}

template <typename T, std::size_t Rank, typename Fill>
void write_dense(const std::filesystem::path& path, const std::string_view variable_name,
                 std::array<std::size_t, Rank> dims, Fill fill, const Compression compression) {
  using value_type = value_type_t<T>;
  using real_type = ksj::array::real_scalar_t<value_type>;
  static_assert(supported_dense_scalar_v<value_type>, "unsupported MAT dense scalar type");
  static_assert(!ksj::array::is_complex_v<value_type> || std::is_floating_point_v<real_type>,
                "complex MAT variables require float or double components");

  const auto count = element_count(dims);
  const std::string name{variable_name};
  MatVarPtr variable;
  if constexpr (ksj::array::is_complex_v<value_type>) {
    auto real = ksj::array::make_pooled_vector<real_type>(count);
    auto imag = ksj::array::make_pooled_vector<real_type>(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto value = fill(index);
      real(index) = static_cast<real_type>(value.real());
      imag(index) = static_cast<real_type>(value.imag());
    }
    mat_complex_split_t split{real.data(), imag.data()};
    variable.reset(Mat_VarCreate(name.c_str(), MatScalarTraits<real_type>::class_type,
                                 MatScalarTraits<real_type>::data_type, static_cast<int>(Rank), dims.data(), &split,
                                 MAT_F_COMPLEX));
    if (!variable) {
      throw std::runtime_error("failed to create complex MAT variable: " + name);
    }
    write_variable(path, variable.get(), compression);
  } else {
    auto data = ksj::array::make_pooled_vector<value_type>(count);
    for (std::size_t index = 0; index < count; ++index) {
      data(index) = fill(index);
    }
    variable.reset(Mat_VarCreate(name.c_str(), MatScalarTraits<value_type>::class_type,
                                 MatScalarTraits<value_type>::data_type, static_cast<int>(Rank), dims.data(),
                                 count == 0U ? nullptr : data.data(), 0));
    if (!variable) {
      throw std::runtime_error("failed to create MAT variable: " + name);
    }
    write_variable(path, variable.get(), compression);
  }
}

template <typename T> void validate_vector_variable(const matvar_t& variable) {
  check_dense_type<T>(variable);
  if (variable.rank != 2 || variable.dims == nullptr || (variable.dims[0] != 1U && variable.dims[1] != 1U)) {
    throw std::invalid_argument("MAT variable is not a vector");
  }
}

template <typename T>
void validate_matrix_variable(const matvar_t& variable, const std::size_t rows, const std::size_t cols) {
  check_dense_type<T>(variable);
  if (variable.rank != 2 || variable.dims == nullptr || variable.dims[0] != rows || variable.dims[1] != cols) {
    throw std::invalid_argument("MAT variable matrix dimensions do not match output view");
  }
}

template <typename T>
void validate_cube_variable(const matvar_t& variable, const std::size_t dim0, const std::size_t dim1,
                            const std::size_t dim2) {
  check_dense_type<T>(variable);
  if (variable.rank != 3 || variable.dims == nullptr || variable.dims[0] != dim0 || variable.dims[1] != dim1 ||
      variable.dims[2] != dim2) {
    throw std::invalid_argument("MAT variable cube dimensions do not match output view");
  }
}

template <typename T>
void validate_array4d_variable(const matvar_t& variable, const std::size_t dim0, const std::size_t dim1,
                               const std::size_t dim2, const std::size_t dim3) {
  check_dense_type<T>(variable);
  if (variable.rank != 4 || variable.dims == nullptr || variable.dims[0] != dim0 || variable.dims[1] != dim1 ||
      variable.dims[2] != dim2 || variable.dims[3] != dim3) {
    throw std::invalid_argument("MAT variable array4d dimensions do not match output view");
  }
}

template <typename T> void copy_variable_to_vector(const matvar_t& variable, ksj::array::VectorView<T> output) {
  validate_vector_variable<T>(variable);
  const auto length = variable.dims[0] * variable.dims[1];
  if (output.size() != length) {
    throw std::invalid_argument("MAT variable vector length does not match output view");
  }
  for (std::size_t index = 0; index < length; ++index) {
    output(index) = read_dense_value<T>(variable, index);
  }
}

template <typename T> void copy_variable_to_matrix(const matvar_t& variable, ksj::array::MatrixView<T> output) {
  validate_matrix_variable<T>(variable, output.rows(), output.cols());
  for (std::size_t col = 0; col < output.cols(); ++col) {
    for (std::size_t row = 0; row < output.rows(); ++row) {
      output(row, col) = read_dense_value<T>(variable, row + output.rows() * col);
    }
  }
}

template <typename T> void copy_variable_to_cube(const matvar_t& variable, ksj::array::CubeView<T> output) {
  validate_cube_variable<T>(variable, output.dim0(), output.dim1(), output.dim2());
  for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
    for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
      for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
        output(i0, i1, i2) = read_dense_value<T>(variable, i0 + output.dim0() * (i1 + output.dim1() * i2));
      }
    }
  }
}

template <typename T> void copy_variable_to_array4d(const matvar_t& variable, ksj::array::Array4DView<T> output) {
  validate_array4d_variable<T>(variable, output.dim0(), output.dim1(), output.dim2(), output.dim3());
  for (std::size_t i3 = 0; i3 < output.dim3(); ++i3) {
    for (std::size_t i2 = 0; i2 < output.dim2(); ++i2) {
      for (std::size_t i1 = 0; i1 < output.dim1(); ++i1) {
        for (std::size_t i0 = 0; i0 < output.dim0(); ++i0) {
          output(i0, i1, i2, i3) =
            read_dense_value<T>(variable, i0 + output.dim0() * (i1 + output.dim1() * (i2 + output.dim2() * i3)));
        }
      }
    }
  }
}

inline void check_int_extent(const std::size_t value, const char* message) {
  if (value > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::length_error(message);
  }
}

[[nodiscard]] inline MatSparseCount to_mat_sparse_count(const std::size_t value, const char* message) {
  if (value > static_cast<std::size_t>(std::numeric_limits<MatSparseCount>::max())) {
    throw std::length_error(message);
  }
  return static_cast<MatSparseCount>(value);
}

template <typename T> [[nodiscard]] T read_sparse_value(const matvar_t& variable, const std::size_t index) {
  using value_type = value_type_t<T>;
  using real_type = ksj::array::real_scalar_t<value_type>;
  static_assert(std::is_arithmetic_v<real_type>, "unsupported sparse value type");
  const auto* sparse = static_cast<const mat_sparse_t*>(variable.data);
  if constexpr (ksj::array::is_complex_v<value_type>) {
    if (variable.isComplex != 0) {
      const auto* split = static_cast<const mat_complex_split_t*>(sparse->data);
      const auto* real = static_cast<const double*>(split->Re);
      const auto* imag = static_cast<const double*>(split->Im);
      return value_type(static_cast<real_type>(real == nullptr ? 0.0 : real[index]),
                        static_cast<real_type>(imag == nullptr ? 0.0 : imag[index]));
    }
    const auto* values = static_cast<const double*>(sparse->data);
    return value_type(static_cast<real_type>(values == nullptr ? 0.0 : values[index]), real_type{});
  } else {
    if (variable.isComplex != 0) {
      throw std::invalid_argument("cannot read a complex sparse MAT variable into a real CSR matrix");
    }
    const auto* values = static_cast<const double*>(sparse->data);
    return static_cast<value_type>(values == nullptr ? 0.0 : values[index]);
  }
}

} // namespace detail

template <typename T>
void read_vector(const std::filesystem::path& path, const std::string_view variable_name,
                 ksj::array::VectorView<T> output) {
  auto variable = detail::read_variable(path, variable_name);
  detail::copy_variable_to_vector(*variable, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledVector<T> read_vector(const std::filesystem::path& path,
                                                      const std::string_view variable_name,
                                                      ksj::memory::AllocationProperties properties = {}) {
  auto variable = detail::read_variable(path, variable_name);
  detail::validate_vector_variable<T>(*variable);
  auto output = ksj::array::make_pooled_vector<T>(variable->dims[0] * variable->dims[1], std::move(properties));
  detail::copy_variable_to_vector(*variable, output.view());
  return output;
}

template <typename T>
void write_vector(const std::filesystem::path& path, const std::string_view variable_name,
                  ksj::array::VectorView<T> input, const Compression compression = Compression::zlib) {
  using value_type = detail::value_type_t<T>;
  detail::write_dense<value_type>(
    path, variable_name, std::array<std::size_t, 2>{input.size(), 1U},
    [&](const std::size_t index) -> value_type {
      return input(index);
    },
    compression);
}

template <typename T>
void write_vector(const std::filesystem::path& path, const std::string_view variable_name,
                  const ksj::array::PooledVector<T>& input, const Compression compression = Compression::zlib) {
  write_vector(path, variable_name, input.view(), compression);
}

template <typename T>
void read_matrix(const std::filesystem::path& path, const std::string_view variable_name,
                 ksj::array::MatrixView<T> output) {
  auto variable = detail::read_variable(path, variable_name);
  detail::copy_variable_to_matrix(*variable, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledMatrix<T> read_matrix(const std::filesystem::path& path,
                                                      const std::string_view variable_name,
                                                      ksj::memory::AllocationProperties properties = {}) {
  auto variable = detail::read_variable(path, variable_name);
  detail::check_dense_type<T>(*variable);
  if (variable->rank != 2 || variable->dims == nullptr) {
    throw std::invalid_argument("MAT variable is not a matrix");
  }
  auto output = ksj::array::make_pooled_matrix<T>(variable->dims[0], variable->dims[1], std::move(properties));
  detail::copy_variable_to_matrix(*variable, output.view());
  return output;
}

template <typename T>
void write_matrix(const std::filesystem::path& path, const std::string_view variable_name,
                  ksj::array::MatrixView<T> input, const Compression compression = Compression::zlib) {
  using value_type = detail::value_type_t<T>;
  const auto rows = input.rows();
  const auto cols = input.cols();
  detail::write_dense<value_type>(
    path, variable_name, std::array<std::size_t, 2>{rows, cols},
    [&](const std::size_t index) -> value_type {
      const auto row = index % rows;
      const auto col = index / rows;
      return input(row, col);
    },
    compression);
}

template <typename T>
void write_matrix(const std::filesystem::path& path, const std::string_view variable_name,
                  const ksj::array::PooledMatrix<T>& input, const Compression compression = Compression::zlib) {
  write_matrix(path, variable_name, input.view(), compression);
}

template <typename T>
void read_cube(const std::filesystem::path& path, const std::string_view variable_name,
               ksj::array::CubeView<T> output) {
  auto variable = detail::read_variable(path, variable_name);
  detail::copy_variable_to_cube(*variable, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledCube<T> read_cube(const std::filesystem::path& path,
                                                  const std::string_view variable_name,
                                                  ksj::memory::AllocationProperties properties = {}) {
  auto variable = detail::read_variable(path, variable_name);
  detail::check_dense_type<T>(*variable);
  if (variable->rank != 3 || variable->dims == nullptr) {
    throw std::invalid_argument("MAT variable is not a cube");
  }
  auto output =
    ksj::array::make_pooled_cube<T>(variable->dims[0], variable->dims[1], variable->dims[2], std::move(properties));
  detail::copy_variable_to_cube(*variable, output.view());
  return output;
}

template <typename T>
void write_cube(const std::filesystem::path& path, const std::string_view variable_name, ksj::array::CubeView<T> input,
                const Compression compression = Compression::zlib) {
  using value_type = detail::value_type_t<T>;
  const auto dim0 = input.dim0();
  const auto dim1 = input.dim1();
  const auto dim2 = input.dim2();
  detail::write_dense<value_type>(
    path, variable_name, std::array<std::size_t, 3>{dim0, dim1, dim2},
    [&](const std::size_t index) -> value_type {
      const auto i0 = index % dim0;
      const auto plane = index / dim0;
      const auto i1 = plane % dim1;
      const auto i2 = plane / dim1;
      return input(i0, i1, i2);
    },
    compression);
}

template <typename T>
void write_cube(const std::filesystem::path& path, const std::string_view variable_name,
                const ksj::array::PooledCube<T>& input, const Compression compression = Compression::zlib) {
  write_cube(path, variable_name, input.view(), compression);
}

template <typename T>
void read_array4d(const std::filesystem::path& path, const std::string_view variable_name,
                  ksj::array::Array4DView<T> output) {
  auto variable = detail::read_variable(path, variable_name);
  detail::copy_variable_to_array4d(*variable, output);
}

template <typename T>
[[nodiscard]] ksj::array::PooledArray4D<T> read_array4d(const std::filesystem::path& path,
                                                        const std::string_view variable_name,
                                                        ksj::memory::AllocationProperties properties = {}) {
  auto variable = detail::read_variable(path, variable_name);
  detail::check_dense_type<T>(*variable);
  if (variable->rank != 4 || variable->dims == nullptr) {
    throw std::invalid_argument("MAT variable is not a 4D array");
  }
  auto output = ksj::array::make_pooled_array4d<T>(variable->dims[0], variable->dims[1], variable->dims[2],
                                                   variable->dims[3], std::move(properties));
  detail::copy_variable_to_array4d(*variable, output.view());
  return output;
}

template <typename T>
void write_array4d(const std::filesystem::path& path, const std::string_view variable_name,
                   ksj::array::Array4DView<T> input, const Compression compression = Compression::zlib) {
  using value_type = detail::value_type_t<T>;
  const auto dim0 = input.dim0();
  const auto dim1 = input.dim1();
  const auto dim2 = input.dim2();
  const auto dim3 = input.dim3();
  detail::write_dense<value_type>(
    path, variable_name, std::array<std::size_t, 4>{dim0, dim1, dim2, dim3},
    [&](const std::size_t index) -> value_type {
      const auto i0 = index % dim0;
      const auto rem0 = index / dim0;
      const auto i1 = rem0 % dim1;
      const auto rem1 = rem0 / dim1;
      const auto i2 = rem1 % dim2;
      const auto i3 = rem1 / dim2;
      return input(i0, i1, i2, i3);
    },
    compression);
}

template <typename T>
void write_array4d(const std::filesystem::path& path, const std::string_view variable_name,
                   const ksj::array::PooledArray4D<T>& input, const Compression compression = Compression::zlib) {
  write_array4d(path, variable_name, input.view(), compression);
}

template <typename T>
[[nodiscard]] ksj::sparse::CsrMatrix<T> read_sparse(const std::filesystem::path& path,
                                                    const std::string_view variable_name) {
  auto variable = detail::read_variable(path, variable_name);
  if (variable->class_type != MAT_C_SPARSE || variable->data_type != MAT_T_DOUBLE || variable->rank != 2 ||
      variable->dims == nullptr || variable->data == nullptr) {
    throw std::invalid_argument("MAT variable is not a double sparse matrix");
  }

  const auto rows = variable->dims[0];
  const auto cols = variable->dims[1];
  const auto* sparse = static_cast<const mat_sparse_t*>(variable->data);
  if (sparse->jc == nullptr ||
      sparse->njc != detail::to_mat_sparse_count(cols + 1U, "MAT sparse column pointer count is too large") ||
      detail::is_negative(sparse->ndata) || sparse->nzmax < sparse->ndata) {
    throw std::invalid_argument("MAT sparse variable has invalid CSC metadata");
  }

  const auto nonzeros = static_cast<std::size_t>(sparse->ndata);
  auto row_counts = ksj::array::make_pooled_vector<int>(rows);
  for (std::size_t row = 0; row < rows; ++row) {
    row_counts(row) = 0;
  }
  for (std::size_t col = 0; col < cols; ++col) {
    if (sparse->jc[col] > sparse->jc[col + 1U] || detail::is_negative(sparse->jc[col]) ||
        static_cast<std::size_t>(sparse->jc[col + 1U]) > nonzeros) {
      throw std::invalid_argument("MAT sparse CSC column pointers are invalid");
    }
    for (auto offset = sparse->jc[col]; offset < sparse->jc[col + 1U]; ++offset) {
      const auto offset_index = static_cast<std::size_t>(offset);
      const auto row = static_cast<std::size_t>(sparse->ir[offset_index]);
      if (row >= rows) {
        throw std::invalid_argument("MAT sparse CSC row index is out of range");
      }
      ++row_counts(row);
    }
  }

  auto row_offsets = ksj::array::make_pooled_vector<int>(rows + 1U);
  row_offsets(0) = 0;
  for (std::size_t row = 0; row < rows; ++row) {
    row_offsets(row + 1U) = row_offsets(row) + row_counts(row);
  }

  auto next = ksj::array::make_pooled_vector<int>(rows);
  for (std::size_t row = 0; row < rows; ++row) {
    next(row) = row_offsets(row);
  }

  auto column_indices = ksj::array::make_pooled_vector<int>(nonzeros);
  auto values = ksj::array::make_pooled_vector<T>(nonzeros);
  for (std::size_t col = 0; col < cols; ++col) {
    for (auto offset = sparse->jc[col]; offset < sparse->jc[col + 1U]; ++offset) {
      const auto offset_index = static_cast<std::size_t>(offset);
      const auto row = static_cast<std::size_t>(sparse->ir[offset_index]);
      const auto destination = static_cast<std::size_t>(next(row)++);
      column_indices(destination) = static_cast<int>(col);
      values(destination) = detail::read_sparse_value<T>(*variable, offset_index);
    }
  }

  return ksj::sparse::CsrMatrix<T>(rows, cols, ksj::array::as_const_view(row_offsets.view()),
                                   ksj::array::as_const_view(column_indices.view()),
                                   ksj::array::as_const_view(values.view()));
}

template <typename T>
void write_sparse(const std::filesystem::path& path, const std::string_view variable_name,
                  const ksj::sparse::CsrMatrix<T>& input, const Compression compression = Compression::zlib) {
  using value_type = detail::value_type_t<T>;
  using real_type = ksj::array::real_scalar_t<value_type>;
  static_assert(std::is_arithmetic_v<real_type>, "unsupported sparse value type");
  detail::check_int_extent(input.rows(), "CSR row count exceeds MAT sparse int index range");
  detail::check_int_extent(input.cols(), "CSR column count exceeds MAT sparse int index range");
  detail::check_int_extent(input.nonzeros(), "CSR nonzero count exceeds MAT sparse int index range");

  const auto rows = input.rows();
  const auto cols = input.cols();
  const auto nonzeros = input.nonzeros();
  auto jc = ksj::array::make_pooled_vector<detail::MatSparseIndex>(cols + 1U);
  for (std::size_t col = 0; col <= cols; ++col) {
    jc(col) = detail::MatSparseIndex{};
  }
  for (std::size_t index = 0; index < nonzeros; ++index) {
    ++jc(static_cast<std::size_t>(input.column_indices()(index)) + 1U);
  }
  for (std::size_t col = 0; col < cols; ++col) {
    jc(col + 1U) += jc(col);
  }

  auto next = ksj::array::make_pooled_vector<detail::MatSparseIndex>(cols);
  for (std::size_t col = 0; col < cols; ++col) {
    next(col) = jc(col);
  }

  auto ir = ksj::array::make_pooled_vector<detail::MatSparseIndex>(nonzeros);
  auto real_values = ksj::array::make_pooled_vector<double>(nonzeros);
  auto imag_values = ksj::array::make_pooled_vector<double>(ksj::array::is_complex_v<value_type> ? nonzeros : 0U);
  for (std::size_t row = 0; row < rows; ++row) {
    for (int offset = input.row_starts()(row); offset < input.row_ends()(row); ++offset) {
      const auto source = static_cast<std::size_t>(offset);
      const auto col = static_cast<std::size_t>(input.column_indices()(source));
      const auto destination = static_cast<std::size_t>(next(col)++);
      ir(destination) = static_cast<detail::MatSparseIndex>(row);
      const auto value = input.values()(source);
      if constexpr (ksj::array::is_complex_v<value_type>) {
        real_values(destination) = static_cast<double>(value.real());
        imag_values(destination) = static_cast<double>(value.imag());
      } else {
        real_values(destination) = static_cast<double>(value);
      }
    }
  }

  mat_sparse_t sparse{};
  sparse.nzmax = detail::to_mat_sparse_count(nonzeros, "CSR nonzero count exceeds MAT sparse count range");
  sparse.ir = nonzeros == 0U ? nullptr : ir.data();
  sparse.nir = detail::to_mat_sparse_count(nonzeros, "CSR row index count exceeds MAT sparse count range");
  sparse.jc = jc.data();
  sparse.njc = detail::to_mat_sparse_count(cols + 1U, "CSR column pointer count exceeds MAT sparse count range");
  sparse.ndata = detail::to_mat_sparse_count(nonzeros, "CSR data count exceeds MAT sparse count range");
  sparse.data = nonzeros == 0U ? nullptr : real_values.data();

  mat_complex_split_t split{};
  int flags = 0;
  if constexpr (ksj::array::is_complex_v<value_type>) {
    split.Re = nonzeros == 0U ? nullptr : real_values.data();
    split.Im = nonzeros == 0U ? nullptr : imag_values.data();
    sparse.data = &split;
    flags = MAT_F_COMPLEX;
  }

  std::array<std::size_t, 2> dims{rows, cols};
  const std::string name{variable_name};
  detail::MatVarPtr variable{Mat_VarCreate(name.c_str(), MAT_C_SPARSE, MAT_T_DOUBLE, 2, dims.data(), &sparse, flags)};
  if (!variable) {
    throw std::runtime_error("failed to create sparse MAT variable: " + name);
  }
  detail::write_variable(path, variable.get(), compression);
}

} // namespace ksj::matio
