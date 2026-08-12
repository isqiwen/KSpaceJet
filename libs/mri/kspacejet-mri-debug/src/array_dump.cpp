#include "kspacejet/mri/debug/array_dump.hpp"

#include "kspacejet/base/path.hpp"
#include "kspacejet/process_runtime/debug_dump.hpp"
#include "kspacejet/process_runtime/state_paths.hpp"

#include "matio.h"

#include <cctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>

namespace {

using ksj::mri::debug::ArrayMatDumpOptions;
using ksj::mri::debug::ArrayMatFileVersion;
using ksj::mri::debug::detail::ArrayMatPayload;
using ksj::mri::debug::detail::MatScalarKind;

struct MatVarDeleter {
  void operator()(matvar_t* variable) const noexcept {
    if (variable != nullptr) {
      Mat_VarFree(variable);
    }
  }
};

struct MatFileDeleter {
  void operator()(mat_t* file) const noexcept {
    if (file != nullptr) {
      Mat_Close(file);
    }
  }
};

using MatVariablePtr = std::unique_ptr<matvar_t, MatVarDeleter>;
using MatFilePtr = std::unique_ptr<mat_t, MatFileDeleter>;

[[nodiscard]] bool debug_dump_category_enabled(std::string_view category, std::string_view detail) {
  return ksj::process_runtime::debug_dump::IsDebugDumpEnabledForCategory(category) ||
         (!detail.empty() && ksj::process_runtime::debug_dump::IsDebugDumpEnabledForCategory(detail));
}

[[nodiscard]] std::string sanitize_path_component(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char ch : value) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) != 0 || ch == '_' || ch == '-' || ch == '.') {
      result.push_back(ch);
    } else {
      result.push_back('_');
    }
  }
  return result.empty() ? std::string{"array"} : result;
}

[[nodiscard]] std::string sanitize_mat_variable_name(const std::string_view variable_name) {
  std::string result;
  result.reserve(variable_name.size() + 4);

  for (const char ch : variable_name) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) != 0 || ch == '_') {
      result.push_back(ch);
    } else {
      result.push_back('_');
    }
  }

  if (result.empty()) {
    result = "array";
  }
  if (std::isalpha(static_cast<unsigned char>(result.front())) == 0) {
    result.insert(0, "ksj_");
  }
  return result;
}

[[nodiscard]] std::filesystem::path resolve_dump_directory(const std::filesystem::path& configured_directory) {
  if (configured_directory.empty()) {
    return ksj::process_runtime::state_paths::debug_matrix_dump_dir_path();
  }

  if (ksj::base::path::is_absolute_path_like(configured_directory)) {
    return configured_directory;
  }
  return ksj::process_runtime::state_paths::debug_subdir_path(configured_directory.string());
}

[[nodiscard]] std::filesystem::path resolve_array_mat_dump_path(const std::string_view file_prefix,
                                                                const ArrayMatDumpOptions& options) {
  auto filename = sanitize_path_component(file_prefix);
  if (std::filesystem::path(filename).extension() != ".mat") {
    filename += ".mat";
  }
  return resolve_dump_directory(options.directory) / filename;
}

[[nodiscard]] mat_ft mat_file_type(const ArrayMatFileVersion version) noexcept {
  switch (version) {
    case ArrayMatFileVersion::mat5:
      return MAT_FT_MAT5;
    case ArrayMatFileVersion::mat73:
      return MAT_FT_MAT73;
  }
  return MAT_FT_MAT73;
}

[[nodiscard]] matio_compression mat_compression(const bool enabled) noexcept {
  return enabled ? MAT_COMPRESSION_ZLIB : MAT_COMPRESSION_NONE;
}

[[nodiscard]] matio_classes mat_class(const MatScalarKind kind) noexcept {
  switch (kind) {
    case MatScalarKind::f32:
      return MAT_C_SINGLE;
    case MatScalarKind::f64:
      return MAT_C_DOUBLE;
    case MatScalarKind::i8:
      return MAT_C_INT8;
    case MatScalarKind::u8:
      return MAT_C_UINT8;
    case MatScalarKind::i16:
      return MAT_C_INT16;
    case MatScalarKind::u16:
      return MAT_C_UINT16;
    case MatScalarKind::i32:
      return MAT_C_INT32;
    case MatScalarKind::u32:
      return MAT_C_UINT32;
    case MatScalarKind::i64:
      return MAT_C_INT64;
    case MatScalarKind::u64:
      return MAT_C_UINT64;
  }
  return MAT_C_DOUBLE;
}

[[nodiscard]] matio_types mat_data_type(const MatScalarKind kind) noexcept {
  switch (kind) {
    case MatScalarKind::f32:
      return MAT_T_SINGLE;
    case MatScalarKind::f64:
      return MAT_T_DOUBLE;
    case MatScalarKind::i8:
      return MAT_T_INT8;
    case MatScalarKind::u8:
      return MAT_T_UINT8;
    case MatScalarKind::i16:
      return MAT_T_INT16;
    case MatScalarKind::u16:
      return MAT_T_UINT16;
    case MatScalarKind::i32:
      return MAT_T_INT32;
    case MatScalarKind::u32:
      return MAT_T_UINT32;
    case MatScalarKind::i64:
      return MAT_T_INT64;
    case MatScalarKind::u64:
      return MAT_T_UINT64;
  }
  return MAT_T_DOUBLE;
}

[[nodiscard]] MatFilePtr open_mat_dump_file(const std::filesystem::path& path, const ArrayMatDumpOptions& options) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return nullptr;
  }

  mat_t* file = nullptr;
  if (options.append) {
    file = Mat_Open(path.string().c_str(), MAT_ACC_RDWR);
  }
  if (file == nullptr) {
    file = Mat_CreateVer(path.string().c_str(), "created by KSpaceJet array dump", mat_file_type(options.file_version));
  }
  return MatFilePtr{file};
}

[[nodiscard]] MatVariablePtr create_mat_variable(const ArrayMatPayload& payload) {
  if (payload.dimensions.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }

  const auto sanitized_variable_name = sanitize_mat_variable_name(payload.variable_name);
  return std::visit(
    [&](const auto& real_values) -> MatVariablePtr {
      using real_storage_type = std::remove_cvref_t<decltype(real_values)>;
      using scalar_type = typename real_storage_type::value_type;

      void* data = real_values.empty() ? nullptr : const_cast<void*>(static_cast<const void*>(real_values.data()));
      int flags = 0;
      mat_complex_split_t complex_data{};

      if (payload.complex) {
        const auto* imag_values = std::get_if<std::vector<scalar_type>>(&payload.imag_values);
        if (imag_values == nullptr || imag_values->size() != real_values.size()) {
          return nullptr;
        }
        complex_data.Re =
          real_values.empty() ? nullptr : const_cast<void*>(static_cast<const void*>(real_values.data()));
        complex_data.Im =
          imag_values->empty() ? nullptr : const_cast<void*>(static_cast<const void*>(imag_values->data()));
        data = &complex_data;
        flags = MAT_F_COMPLEX;
      }

      return MatVariablePtr{Mat_VarCreate(
        sanitized_variable_name.c_str(), mat_class(payload.scalar_kind), mat_data_type(payload.scalar_kind),
        static_cast<int>(payload.dimensions.size()), const_cast<std::size_t*>(payload.dimensions.data()), data, flags)};
    },
    payload.real_values);
}

} // namespace

namespace ksj::mri::debug {

bool array_dump_enabled(const std::string_view name) {
  return debug_dump_category_enabled("array_dump", name) || debug_dump_category_enabled("matrix_dump", name);
}

namespace detail {

int write_mat_array_payload(const ArrayMatPayload& payload, const std::string_view file_prefix,
                            const ArrayMatDumpOptions& options) {
  const auto dump_path = resolve_array_mat_dump_path(file_prefix.empty() ? "array" : file_prefix, options);
  auto file = open_mat_dump_file(dump_path, options);
  if (file == nullptr) {
    KSJ_LOG_ERROR("dump_mat_array failed to create MAT file [{}].", dump_path.string());
    return kArrayDumpCreateFileFailure;
  }

  auto variable = create_mat_variable(payload);
  if (variable == nullptr) {
    KSJ_LOG_ERROR("dump_mat_array failed to create MAT variable for [{}].", dump_path.string());
    return kArrayDumpCreateVariableFailure;
  }

  if (Mat_VarWrite(file.get(), variable.get(), mat_compression(options.compress)) != 0) {
    KSJ_LOG_ERROR("dump_mat_array failed to write MAT variable to [{}].", dump_path.string());
    return kArrayDumpWriteFailure;
  }

  if (ksj::logging::ShouldLog(ksj::logging::Level::Debug)) {
    KSJ_LOG_DEBUG("dump_mat_array wrote [{}].", dump_path.string());
  }
  return kArrayDumpOk;
}

} // namespace detail

} // namespace ksj::mri::debug
