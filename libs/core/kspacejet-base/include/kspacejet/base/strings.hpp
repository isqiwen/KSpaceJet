#pragma once

#include <cctype>
#include <cstddef>
#include <string_view>

namespace ksj::base {

[[nodiscard]] inline bool ascii_iequals(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(left[index]);
    const auto rhs = static_cast<unsigned char>(right[index]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return false;
    }
  }

  return true;
}

} // namespace ksj::base
