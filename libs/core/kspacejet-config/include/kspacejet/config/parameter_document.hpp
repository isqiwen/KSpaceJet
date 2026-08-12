#pragma once

#include "kspacejet/base/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::config {

struct ParameterField {
  std::string key;
  std::string value;
};

class ParameterRecord {
public:
  [[nodiscard]] const std::vector<ParameterField>& fields() const noexcept;
  [[nodiscard]] bool empty() const noexcept;

  void add_field(std::string key, std::string value);
  bool set_field(std::string_view key, std::string value, std::size_t index = 0);

  [[nodiscard]] std::optional<std::string_view> field(std::string_view key, std::size_t index = 0) const;
  [[nodiscard]] std::optional<bool> bool_value(std::string_view key, std::size_t index = 0) const;
  [[nodiscard]] std::optional<int> int_value(std::string_view key, std::size_t index = 0) const;
  [[nodiscard]] std::optional<double> double_value(std::string_view key, std::size_t index = 0) const;

private:
  std::vector<ParameterField> fields_;
};

class ParameterDocument {
public:
  static ksj::base::Result<ParameterDocument> parse(std::string_view text, std::string_view source_name = {});
  static ksj::base::Result<ParameterDocument> read_file(const std::filesystem::path& path);

  [[nodiscard]] const std::vector<ParameterRecord>& records() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  void add_record(ParameterRecord record);

  [[nodiscard]] ParameterRecord* find(std::string_view field, std::string_view value) noexcept;
  [[nodiscard]] const ParameterRecord* find(std::string_view field, std::string_view value) const noexcept;
  [[nodiscard]] ParameterRecord* find_by_name(std::string_view name) noexcept;
  [[nodiscard]] const ParameterRecord* find_by_name(std::string_view name) const noexcept;

  [[nodiscard]] std::optional<std::string_view> string_value(std::string_view name, std::string_view field = "VALUE",
                                                             std::size_t index = 0) const;
  [[nodiscard]] std::optional<bool> bool_value(std::string_view name, std::string_view field = "VALUE",
                                               std::size_t index = 0) const;
  [[nodiscard]] std::optional<int> int_value(std::string_view name, std::string_view field = "VALUE",
                                             std::size_t index = 0) const;
  [[nodiscard]] std::optional<double> double_value(std::string_view name, std::string_view field = "VALUE",
                                                   std::size_t index = 0) const;

  bool set_value(std::string_view name, std::string value, std::string_view field = "VALUE");
  bool set_int(std::string_view name, int value, std::string_view field = "VALUE");
  bool set_double(std::string_view name, double value, std::string_view field = "VALUE");
  bool set_bool(std::string_view name, bool value, std::string_view field = "VALUE");

  [[nodiscard]] std::string encode() const;
  [[nodiscard]] ksj::base::Status write_file(const std::filesystem::path& path) const;

private:
  std::vector<ParameterRecord> records_;
};

} // namespace ksj::config
