#include "kspacejet/platform/socket_subsystem.hpp"

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace ksj::platform {

SocketSubsystem::SocketSubsystem() noexcept {
#ifdef _WIN32
  WSADATA wsa_data{};
  WSASetLastError(0);
  error_code_ = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  initialized_ = error_code_ == 0;
#else
  initialized_ = true;
  error_code_ = 0;
#endif
}

SocketSubsystem::~SocketSubsystem() {
#ifdef _WIN32
  if (initialized_) {
    WSACleanup();
  }
#endif
}

bool SocketSubsystem::ok() const noexcept {
  return initialized_;
}

int SocketSubsystem::error_code() const noexcept {
  return error_code_;
}

} // namespace ksj::platform
