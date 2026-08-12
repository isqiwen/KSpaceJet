#pragma once

#include "kspacejet/base/types.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "kspacejet/base/result.hpp"

namespace ksj::config {

class KeyValueConfig {
public:
  using Map = std::map<std::string, std::string, std::less<>>;

  void set(std::string key, std::string value);
  void merge_from(const KeyValueConfig& other);

  [[nodiscard]] const Map& values() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] bool contains(std::string_view key) const;
  [[nodiscard]] std::optional<std::string_view> find(std::string_view key) const;
  [[nodiscard]] std::string value_or(std::string_view key, std::string_view default_value) const;

  [[nodiscard]] ksj::base::Result<bool> bool_value(std::string_view key, bool default_value) const;
  [[nodiscard]] ksj::base::Result<ksj::base::u32> uint32_value(std::string_view key,
                                                               ksj::base::u32 default_value) const;
  [[nodiscard]] ksj::base::Result<std::size_t> size_value(std::string_view key, std::size_t default_value) const;
  [[nodiscard]] ksj::base::Result<double> double_value(std::string_view key, double default_value) const;

private:
  Map values_;
};

[[nodiscard]] ksj::base::Result<KeyValueConfig> parse_key_value_config(std::string_view text,
                                                                       std::string_view source_name = {});

[[nodiscard]] ksj::base::Result<KeyValueConfig> load_key_value_config_file(const std::string& path);

} // namespace ksj::config
