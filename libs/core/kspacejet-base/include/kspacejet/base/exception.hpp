#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace ksj::base {

class Error : public std::runtime_error {
public:
  Error() : std::runtime_error("") {}

  explicit Error(const char* message) : std::runtime_error(message != nullptr ? message : "") {}

  explicit Error(std::string message) : std::runtime_error(std::move(message)) {}

  explicit Error(std::string_view message) : std::runtime_error(std::string(message)) {}
};

class ValidationError final : public Error {
public:
  using Error::Error;
};

class NotImplementedError final : public std::logic_error {
public:
  NotImplementedError() : std::logic_error("feature not implemented") {}

  explicit NotImplementedError(const char* feature)
      : std::logic_error(make_message(feature != nullptr ? std::string_view(feature) : std::string_view{})) {}

  explicit NotImplementedError(std::string feature) : std::logic_error(make_message(feature)) {}

  explicit NotImplementedError(std::string_view feature) : std::logic_error(make_message(feature)) {}

private:
  static std::string make_message(std::string_view feature) {
    if (feature.empty()) {
      return "feature not implemented";
    }
    return "feature " + std::string(feature) + " not implemented";
  }
};

} // namespace ksj::base
