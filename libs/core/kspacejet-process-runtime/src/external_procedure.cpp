#include "kspacejet/process_runtime/external_procedure.hpp"

#include "kspacejet/base/path.hpp"
#include "kspacejet/process_runtime/executable_layout.hpp"
#include "kspacejet/platform/process.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ksj::process_runtime {
namespace {

[[nodiscard]] std::string find_external_library_path(std::string_view file_name) {
#ifdef _WIN32
  const std::vector<std::filesystem::path> candidates = {
    executable_layout::executable_dir() / std::string(file_name),
    ksj::platform::current_working_directory() / std::string(file_name),
  };
#else
  const std::vector<std::filesystem::path> candidates = {
    executable_layout::runtime_layout_root() / "lib" / std::string(file_name),
    ksj::platform::current_working_directory() / std::string(file_name),
  };
#endif

  for (const std::filesystem::path& candidate : candidates) {
    if (ksj::base::path::exists(candidate)) {
      return ksj::base::path::normalize(candidate);
    }
  }
  return ksj::base::path::normalize(candidates.front());
}

} // namespace

bool ExternalProcedure::is_open() const noexcept {
  return library_.is_open();
}

ksj::base::Status ExternalProcedure::open(std::string_view library_name, ksj::platform::LoadMode mode) {
  close();

  if (library_name.empty()) {
    return ksj::base::Status::InvalidArgument("External procedure library name must not be empty");
  }

  library_name_ = std::string(library_name);
  const std::string library_file_name = ksj::platform::shared_library_file_name(library_name_);
  library_path_ = find_external_library_path(library_file_name);

  ksj::platform::DynamicLibrary library;
  const ksj::base::Status status = library.open(library_path_, mode);
  if (!status.ok()) {
    close();
    return status;
  }

  library_ = std::move(library);
  return ksj::base::Status::Ok();
}

ksj::base::Status ExternalProcedure::open(const ExternalProcedureDescriptor& descriptor, ksj::platform::LoadMode mode) {
  if (descriptor.symbol_name.empty()) {
    return ksj::base::Status::InvalidArgument("External procedure symbol name must not be empty");
  }
  return open(descriptor.library_name, mode);
}

void ExternalProcedure::close() noexcept {
  library_.close();
  library_name_.clear();
  library_path_.clear();
}

ksj::base::Result<void*> ExternalProcedure::symbol(std::string_view symbol_name) {
  if (symbol_name.empty()) {
    return ksj::base::Status::InvalidArgument("External procedure symbol name must not be empty");
  }
  return library_.symbol(symbol_name);
}

} // namespace ksj::process_runtime
