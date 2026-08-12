#include "kspacejet/base/timestamp.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>

namespace ksj::base::timestamp {
namespace {

[[nodiscard]] std::tm local_time_from(std::time_t raw_time) {
  std::tm time_info{};
#ifndef WIN32
  localtime_r(&raw_time, &time_info);
#else
  localtime_s(&time_info, &raw_time);
#endif
  return time_info;
}

[[nodiscard]] std::string format_compact_timestamp(const std::tm& time_info, const long milliseconds) {
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << (1900 + time_info.tm_year) << std::setw(2) << (1 + time_info.tm_mon)
         << std::setw(2) << time_info.tm_mday << std::setw(2) << time_info.tm_hour << std::setw(2) << time_info.tm_min
         << std::setw(2) << time_info.tm_sec << std::setw(3) << milliseconds;
  return stream.str();
}

[[nodiscard]] std::string format_path_timestamp(const std::tm& time_info, const long milliseconds) {
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << (1900 + time_info.tm_year) << '-' << std::setw(2)
         << (1 + time_info.tm_mon) << '-' << std::setw(2) << time_info.tm_mday << '_' << std::setw(2)
         << time_info.tm_hour << '-' << std::setw(2) << time_info.tm_min << '-' << std::setw(2) << time_info.tm_sec
         << '_' << std::setw(3) << milliseconds;
  return stream.str();
}

[[nodiscard]] std::string format_compact_day(const std::tm& time_info) {
  std::ostringstream stream;
  stream << std::setfill('0') << std::setw(4) << (1900 + time_info.tm_year) << std::setw(2) << (1 + time_info.tm_mon)
         << std::setw(2) << time_info.tm_mday;
  return stream.str();
}

[[nodiscard]] bool is_leap_year(const int year) noexcept {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

[[nodiscard]] bool is_valid_datetime(const int year, const int month, const int day, const int hour, const int minute,
                                     const int second, const int millisecond) noexcept {
  if (year < 1900 || year > 2100 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59 || millisecond < 0 || millisecond > 999) {
    return false;
  }

  constexpr std::array<int, 12> days_in_month = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  int max_day = days_in_month[static_cast<std::size_t>(month - 1)];
  if (month == 2 && is_leap_year(year)) {
    max_day = 29;
  }
  return day >= 1 && day <= max_day;
}

[[nodiscard]] std::optional<int> parse_fixed_int(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  int value = 0;
  for (const char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return std::nullopt;
    }
    value = value * 10 + (ch - '0');
  }
  return value;
}

} // namespace

namespace {

struct CurrentTime {
  std::tm local_time{};
  long milliseconds = 0;
};

[[nodiscard]] CurrentTime current_time() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t raw_time = std::chrono::system_clock::to_time_t(now);
  return {
    .local_time = local_time_from(raw_time),
    .milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000,
  };
}

} // namespace

std::string compact_timestamp() {
  const CurrentTime time = current_time();
  return format_compact_timestamp(time.local_time, time.milliseconds);
}

std::string path_timestamp() {
  return path_timestamp_from(std::chrono::system_clock::now()).path_timestamp;
}

std::string compact_day() {
  return path_timestamp_from(std::chrono::system_clock::now()).day;
}

PathTimestampParts path_timestamp_from(const std::chrono::system_clock::time_point timestamp) {
  const std::time_t raw_time = std::chrono::system_clock::to_time_t(timestamp);
  const std::tm time_info = local_time_from(raw_time);
  const long milliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() % 1000;

  return {
    .day = format_compact_day(time_info),
    .path_timestamp = format_path_timestamp(time_info, milliseconds),
  };
}

std::optional<PathTimestampParts> parse_compact_datetime(std::string_view digits) {
  if (digits.size() != 14 && digits.size() != 17) {
    return std::nullopt;
  }

  const auto year = parse_fixed_int(digits.substr(0, 4));
  const auto month = parse_fixed_int(digits.substr(4, 2));
  const auto day = parse_fixed_int(digits.substr(6, 2));
  const auto hour = parse_fixed_int(digits.substr(8, 2));
  const auto minute = parse_fixed_int(digits.substr(10, 2));
  const auto second = parse_fixed_int(digits.substr(12, 2));
  const int millisecond = digits.size() == 17 ? parse_fixed_int(digits.substr(14, 3)).value_or(-1) : 0;
  if (!year.has_value() || !month.has_value() || !day.has_value() || !hour.has_value() || !minute.has_value() ||
      !second.has_value() || !is_valid_datetime(*year, *month, *day, *hour, *minute, *second, millisecond)) {
    return std::nullopt;
  }

  std::tm time_info{};
  time_info.tm_year = *year - 1900;
  time_info.tm_mon = *month - 1;
  time_info.tm_mday = *day;
  time_info.tm_hour = *hour;
  time_info.tm_min = *minute;
  time_info.tm_sec = *second;

  return PathTimestampParts{
    .day = format_compact_day(time_info),
    .path_timestamp = format_path_timestamp(time_info, millisecond),
  };
}

} // namespace ksj::base::timestamp
