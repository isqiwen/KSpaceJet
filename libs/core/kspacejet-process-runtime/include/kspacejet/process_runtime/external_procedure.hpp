#pragma once

#include <string>
#include <string_view>
#include <type_traits>

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/status.hpp"
#include "kspacejet/platform/dynamic_library.hpp"

namespace ksj::process_runtime {

struct ExternalProcedureDescriptor {
  std::string_view library_name;
  std::string_view symbol_name;
};

class ExternalProcedure {
public:
  ExternalProcedure() = default;
  ~ExternalProcedure() = default;

  ExternalProcedure(const ExternalProcedure&) = delete;
  ExternalProcedure& operator=(const ExternalProcedure&) = delete;

  ExternalProcedure(ExternalProcedure&&) noexcept = default;
  ExternalProcedure& operator=(ExternalProcedure&&) noexcept = default;

  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] const std::string& library_name() const noexcept { return library_name_; }
  [[nodiscard]] const std::string& library_path() const noexcept { return library_path_; }

  [[nodiscard]] ksj::base::Status open(std::string_view library_name,
                                       ksj::platform::LoadMode mode = ksj::platform::LoadMode::lazy |
                                                                      ksj::platform::LoadMode::local |
                                                                      ksj::platform::LoadMode::deep_bind);

  [[nodiscard]] ksj::base::Status open(const ExternalProcedureDescriptor& descriptor,
                                       ksj::platform::LoadMode mode = ksj::platform::LoadMode::lazy |
                                                                      ksj::platform::LoadMode::local |
                                                                      ksj::platform::LoadMode::deep_bind);

  void close() noexcept;

  [[nodiscard]] ksj::base::Result<void*> symbol(std::string_view symbol_name);

  template <typename T> [[nodiscard]] ksj::base::Result<T> symbol_as(std::string_view symbol_name) {
    static_assert(std::is_pointer_v<T>, "ExternalProcedure::symbol_as requires a pointer type");

    auto result = symbol(symbol_name);
    if (!result.ok()) {
      return result.status();
    }
    return reinterpret_cast<T>(result.value());
  }

private:
  ksj::platform::DynamicLibrary library_;
  std::string library_name_;
  std::string library_path_;
};

} // namespace ksj::process_runtime
