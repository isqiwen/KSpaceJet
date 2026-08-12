#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace ksj::base::timestamp {

struct PathTimestampParts {
  std::string day;
  std::string path_timestamp;
};

[[nodiscard]] std::string compact_timestamp();
[[nodiscard]] std::string path_timestamp();
[[nodiscard]] std::string compact_day();
[[nodiscard]] PathTimestampParts path_timestamp_from(std::chrono::system_clock::time_point timestamp);
[[nodiscard]] std::optional<PathTimestampParts> parse_compact_datetime(std::string_view digits);

} // namespace ksj::base::timestamp
