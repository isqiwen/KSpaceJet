#include "kspacejet/platform/dynamic_library.hpp"

#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ksj::platform {
namespace {

[[nodiscard]] bool string_ends_with(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] bool string_starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] std::string read_platform_error() {
#ifdef _WIN32
  const DWORD error_code = ::GetLastError();
  if (error_code == 0) {
    return {};
  }

  LPSTR buffer = nullptr;
  const DWORD size = ::FormatMessageA(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error_code,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
  if (size == 0 || buffer == nullptr) {
    return "unknown platform error";
  }

  std::string message(buffer, size);
  ::LocalFree(buffer);
  while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
    message.pop_back();
  }
  return message;
#else
  const char* error = ::dlerror();
  return error == nullptr ? std::string() : std::string(error);
#endif
}

[[nodiscard]] int to_native_flags(LoadMode mode) {
#ifdef _WIN32
  (void)mode;
  return 0;
#else
  int flags = 0;

  if (has_flag(mode, LoadMode::now)) {
    flags |= RTLD_NOW;
  } else {
    flags |= RTLD_LAZY;
  }
  if (has_flag(mode, LoadMode::global)) {
    flags |= RTLD_GLOBAL;
  } else {
    flags |= RTLD_LOCAL;
  }
#ifdef RTLD_DEEPBIND
  if (has_flag(mode, LoadMode::deep_bind)) {
    flags |= RTLD_DEEPBIND;
  }
#endif
  return flags;
#endif
}

} // namespace

std::string shared_library_file_name(std::string_view stem) {
#ifdef _WIN32
  if (string_ends_with(stem, ".dll")) {
    return std::string(stem);
  }
  return std::string(stem) + ".dll";
#else
  if (string_ends_with(stem, ".so")) {
    return std::string(stem);
  }
  if (string_starts_with(stem, "lib")) {
    return std::string(stem) + ".so";
  }
  return "lib" + std::string(stem) + ".so";
#endif
}

DynamicLibrary::~DynamicLibrary() {
  close();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept {
  move_from(std::move(other));
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
  if (this != &other) {
    close();
    move_from(std::move(other));
  }
  return *this;
}

bool DynamicLibrary::is_open() const noexcept {
  return handle_ != nullptr;
}

ksj::base::Status DynamicLibrary::open(const std::filesystem::path& path, LoadMode mode) {
  if (path.empty()) {
    return ksj::base::Status::InvalidArgument("Dynamic library path must not be empty");
  }
  return open_impl(path.string().c_str(), path, false, mode);
}

ksj::base::Status DynamicLibrary::open_self(LoadMode mode) {
  return open_impl("<self>", {}, true, mode);
}

void DynamicLibrary::close() noexcept {
  if (handle_ != nullptr && owns_handle_) {
#ifdef _WIN32
    ::FreeLibrary(static_cast<HMODULE>(handle_));
#else
    ::dlclose(handle_);
#endif
  }
  reset_state();
}

ksj::base::Result<void*> DynamicLibrary::symbol(std::string_view name) {
  if (!is_open()) {
    return ksj::base::Status::StateError("Dynamic library is not open");
  }
  if (name.empty()) {
    return ksj::base::Status::InvalidArgument("Dynamic library symbol name must not be empty");
  }

#ifndef _WIN32
  (void)::dlerror();
#endif

#ifdef _WIN32
  FARPROC address = ::GetProcAddress(static_cast<HMODULE>(handle_), std::string(name).c_str());
  if (address == nullptr) {
    last_error_ = read_platform_error();
    return ksj::base::Status::NotFound("Failed to resolve symbol [" + std::string(name) + "] from [" + loaded_name_ +
                                       "]: " + (last_error_.empty() ? std::string("unknown error") : last_error_));
  }
  return reinterpret_cast<void*>(address);
#else
  void* address = ::dlsym(handle_, std::string(name).c_str());
  last_error_ = read_platform_error();
  if (!last_error_.empty()) {
    return ksj::base::Status::NotFound("Failed to resolve symbol [" + std::string(name) + "] from [" + loaded_name_ +
                                       "]: " + last_error_);
  }
  return address;
#endif
}

void DynamicLibrary::move_from(DynamicLibrary&& other) noexcept {
  handle_ = other.handle_;
  owns_handle_ = other.owns_handle_;
  loaded_path_ = std::move(other.loaded_path_);
  loaded_name_ = std::move(other.loaded_name_);
  last_error_ = std::move(other.last_error_);
  other.reset_state();
}

void DynamicLibrary::reset_state() noexcept {
  handle_ = nullptr;
  owns_handle_ = false;
  loaded_path_.clear();
  loaded_name_.clear();
  last_error_.clear();
}

ksj::base::Status DynamicLibrary::open_impl(const char* identifier, const std::filesystem::path& path,
                                            bool open_self_handle, LoadMode mode) {
  close();

  const std::filesystem::path requested_path = path;
  const std::string requested_name = identifier == nullptr ? std::string() : std::string(identifier);
  loaded_path_ = requested_path;
  loaded_name_ = requested_name;

#ifdef _WIN32
  HMODULE library_handle = nullptr;
  if (open_self_handle) {
    library_handle = ::GetModuleHandleA(nullptr);
    owns_handle_ = false;
  } else {
    library_handle = ::LoadLibraryA(path.string().c_str());
    owns_handle_ = true;
  }
  if (library_handle == nullptr) {
    last_error_ = read_platform_error();
    const std::string error_text = last_error_;
    reset_state();
    return ksj::base::Status::Unavailable("Failed to load dynamic library [" + requested_name +
                                          "]: " + (error_text.empty() ? std::string("unknown error") : error_text));
  }
  handle_ = static_cast<void*>(library_handle);
#else
  void* library_handle = nullptr;
  const int flags = to_native_flags(mode);
  if (open_self_handle) {
    library_handle = ::dlopen(nullptr, flags);
  } else {
    library_handle = ::dlopen(path.string().c_str(), flags);
  }
  if (library_handle == nullptr) {
    last_error_ = read_platform_error();
    const std::string error_text = last_error_;
    reset_state();
    return ksj::base::Status::Unavailable("Failed to load dynamic library [" + requested_name +
                                          "]: " + (error_text.empty() ? std::string("unknown error") : error_text));
  }
  handle_ = library_handle;
  owns_handle_ = true;
#endif

  last_error_.clear();
  return ksj::base::Status::Ok();
}

} // namespace ksj::platform
