#pragma once

#include "kspacejet/base/types.hpp"
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace ksj::base {

enum class StatusCode : ksj::base::u32 {
  ok = 0,
  invalid_argument,
  not_found,
  already_exists,
  out_of_memory,
  timeout,
  unavailable,
  internal_error,
  unimplemented,
  parse_error,
  validation_error,
  state_error,
  io_error,
};

[[nodiscard]] std::string_view to_string(StatusCode code) noexcept;

template <typename T> class Result;

class Status {
public:
  Status() = default;
  Status(StatusCode code, std::string message = {});

  [[nodiscard]] bool ok() const noexcept { return code_ == StatusCode::ok; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] StatusCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] std::string to_string() const;

  static Status Ok();
  static Status InvalidArgument(std::string message);
  static Status NotFound(std::string message);
  static Status AlreadyExists(std::string message);
  static Status OutOfMemory(std::string message);
  static Status Timeout(std::string message);
  static Status Unavailable(std::string message);
  static Status InternalError(std::string message);
  static Status Unimplemented(std::string message);
  static Status ParseError(std::string message);
  static Status ValidationError(std::string message);
  static Status StateError(std::string message);
  static Status IoError(std::string message);

private:
  StatusCode code_{StatusCode::ok};
  std::string message_;
};

[[nodiscard]] bool operator==(const Status& lhs, const Status& rhs) noexcept;
std::ostream& operator<<(std::ostream& os, const Status& status);

} // namespace ksj::base
