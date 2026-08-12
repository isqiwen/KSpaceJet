#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/status.hpp"

namespace ksj::platform {

enum class LoadMode : unsigned int {
  none = 0u,
  lazy = 1u << 0,
  now = 1u << 1,
  local = 1u << 2,
  global = 1u << 3,
  deep_bind = 1u << 4,
};

[[nodiscard]] constexpr LoadMode operator|(LoadMode lhs, LoadMode rhs) noexcept {
  return static_cast<LoadMode>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

[[nodiscard]] constexpr LoadMode operator&(LoadMode lhs, LoadMode rhs) noexcept {
  return static_cast<LoadMode>(static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs));
}

[[nodiscard]] constexpr bool has_flag(LoadMode mode, LoadMode flag) noexcept {
  return static_cast<unsigned int>(mode & flag) != 0u;
}

[[nodiscard]] std::string shared_library_file_name(std::string_view stem);

class DynamicLibrary {
public:
  DynamicLibrary() = default;
  ~DynamicLibrary();

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;

  DynamicLibrary(DynamicLibrary&& other) noexcept;
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] const std::filesystem::path& loaded_path() const noexcept { return loaded_path_; }
  [[nodiscard]] const std::string& loaded_name() const noexcept { return loaded_name_; }
  [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

  [[nodiscard]] ksj::base::Status open(const std::filesystem::path& path,
                                       LoadMode mode = LoadMode::lazy | LoadMode::local);
  [[nodiscard]] ksj::base::Status open_self(LoadMode mode = LoadMode::now | LoadMode::global);
  void close() noexcept;

  [[nodiscard]] ksj::base::Result<void*> symbol(std::string_view name);

  template <typename T> [[nodiscard]] ksj::base::Result<T> symbol_as(std::string_view name) {
    static_assert(std::is_pointer_v<T>, "DynamicLibrary::symbol_as requires a pointer type");

    auto result = symbol(name);
    if (!result.ok()) {
      return result.status();
    }
    return reinterpret_cast<T>(result.value());
  }

private:
  void move_from(DynamicLibrary&& other) noexcept;
  void reset_state() noexcept;
  [[nodiscard]] ksj::base::Status open_impl(const char* identifier, const std::filesystem::path& path, bool open_self,
                                            LoadMode mode);

  using NativeHandle = void*;

  NativeHandle handle_{nullptr};
  bool owns_handle_{false};
  std::filesystem::path loaded_path_{};
  std::string loaded_name_{};
  std::string last_error_{};
};

} // namespace ksj::platform
