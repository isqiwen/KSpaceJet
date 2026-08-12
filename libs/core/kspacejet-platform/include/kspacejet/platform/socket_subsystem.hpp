#pragma once

namespace ksj::platform {

class SocketSubsystem {
public:
  SocketSubsystem() noexcept;
  ~SocketSubsystem();

  SocketSubsystem(const SocketSubsystem&) = delete;
  SocketSubsystem& operator=(const SocketSubsystem&) = delete;
  SocketSubsystem(SocketSubsystem&&) = delete;
  SocketSubsystem& operator=(SocketSubsystem&&) = delete;

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] int error_code() const noexcept;

private:
  bool initialized_ = true;
  int error_code_ = 0;
};

} // namespace ksj::platform
