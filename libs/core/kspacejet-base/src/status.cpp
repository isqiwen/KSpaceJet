#include "kspacejet/base/status.hpp"

#include <sstream>

namespace ksj::base {

std::string_view to_string(StatusCode code) noexcept {
  switch (code) {
    case StatusCode::ok:
      return "ok";
    case StatusCode::invalid_argument:
      return "invalid_argument";
    case StatusCode::not_found:
      return "not_found";
    case StatusCode::already_exists:
      return "already_exists";
    case StatusCode::out_of_memory:
      return "out_of_memory";
    case StatusCode::timeout:
      return "timeout";
    case StatusCode::unavailable:
      return "unavailable";
    case StatusCode::internal_error:
      return "internal_error";
    case StatusCode::unimplemented:
      return "unimplemented";
    case StatusCode::parse_error:
      return "parse_error";
    case StatusCode::validation_error:
      return "validation_error";
    case StatusCode::state_error:
      return "state_error";
    case StatusCode::io_error:
      return "io_error";
  }
  return "unknown";
}

Status::Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

Status Status::Ok() {
  return {};
}
Status Status::InvalidArgument(std::string message) {
  return {StatusCode::invalid_argument, std::move(message)};
}
Status Status::NotFound(std::string message) {
  return {StatusCode::not_found, std::move(message)};
}
Status Status::AlreadyExists(std::string message) {
  return {StatusCode::already_exists, std::move(message)};
}
Status Status::OutOfMemory(std::string message) {
  return {StatusCode::out_of_memory, std::move(message)};
}
Status Status::Timeout(std::string message) {
  return {StatusCode::timeout, std::move(message)};
}
Status Status::Unavailable(std::string message) {
  return {StatusCode::unavailable, std::move(message)};
}
Status Status::InternalError(std::string message) {
  return {StatusCode::internal_error, std::move(message)};
}
Status Status::Unimplemented(std::string message) {
  return {StatusCode::unimplemented, std::move(message)};
}
Status Status::ParseError(std::string message) {
  return {StatusCode::parse_error, std::move(message)};
}
Status Status::ValidationError(std::string message) {
  return {StatusCode::validation_error, std::move(message)};
}
Status Status::StateError(std::string message) {
  return {StatusCode::state_error, std::move(message)};
}
Status Status::IoError(std::string message) {
  return {StatusCode::io_error, std::move(message)};
}

std::string Status::to_string() const {
  if (message_.empty()) {
    return std::string(ksj::base::to_string(code_));
  }
  std::ostringstream os;
  os << ksj::base::to_string(code_) << ": " << message_;
  return os.str();
}

bool operator==(const Status& lhs, const Status& rhs) noexcept {
  return lhs.code() == rhs.code() && lhs.message() == rhs.message();
}

std::ostream& operator<<(std::ostream& os, const Status& status) {
  os << status.to_string();
  return os;
}

} // namespace ksj::base
