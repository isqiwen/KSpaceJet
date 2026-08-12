#pragma once

#include <string_view>

namespace ksj::crash {

struct Breadcrumb {
  std::string_view category{};
  std::string_view message{};
};

void RecordBreadcrumb(const Breadcrumb& breadcrumb) noexcept;
void RecordBreadcrumb(std::string_view category, std::string_view message) noexcept;

} // namespace ksj::crash
