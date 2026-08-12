#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "kspacejet/base/status.hpp"

namespace ksj::base {

template <typename T> class Result {
public:
  Result(const T& value) : data_(value) {}
  Result(T&& value) : data_(std::move(value)) {}
  Result(const Status& status) : data_(status) {}
  Result(Status&& status) : data_(std::move(status)) {}

  [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
  [[nodiscard]] const Status& status() const {
    if (ok()) {
      return ok_status();
    }
    return std::get<Status>(data_);
  }

  [[nodiscard]] const T& value() const& {
    if (!ok()) {
      throw std::logic_error("Result does not contain a value");
    }
    return std::get<T>(data_);
  }

  [[nodiscard]] T& value() & {
    if (!ok()) {
      throw std::logic_error("Result does not contain a value");
    }
    return std::get<T>(data_);
  }

  [[nodiscard]] T&& value() && {
    if (!ok()) {
      throw std::logic_error("Result does not contain a value");
    }
    return std::get<T>(std::move(data_));
  }

private:
  [[nodiscard]] static const Status& ok_status();

  std::variant<T, Status> data_;
};

} // namespace ksj::base

namespace ksj::base {

template <typename T> const Status& Result<T>::ok_status() {
  static const Status ok = Status::Ok();
  return ok;
}

} // namespace ksj::base
