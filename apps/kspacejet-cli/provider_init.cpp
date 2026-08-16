#include "provider_init.hpp"

#include "kspacejet/base/status.hpp"
#include "kspacejet/platform/filesystem.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifndef KSJ_PROVIDER_TEMPLATE_INSTALL_DIR
#define KSJ_PROVIDER_TEMPLATE_INSTALL_DIR ""
#endif

#ifndef KSJ_PROVIDER_TEMPLATE_SOURCE_DIR
#define KSJ_PROVIDER_TEMPLATE_SOURCE_DIR ""
#endif

namespace ksj::cli {
namespace {

constexpr std::size_t kMaximumProviderSlugLength = 63U;
constexpr std::size_t kMaximumOperatorIdLength = 96U;
constexpr unsigned int kMaximumStagingDirectoryAttempts = 64U;

[[nodiscard]] bool is_ascii_lowercase_letter(const char character) noexcept {
  return character >= 'a' && character <= 'z';
}

[[nodiscard]] bool is_ascii_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

[[nodiscard]] bool is_valid_provider_slug(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumProviderSlugLength || !is_ascii_lowercase_letter(value.front()) ||
      !(is_ascii_lowercase_letter(value.back()) || is_ascii_digit(value.back()))) {
    return false;
  }

  bool previous_was_hyphen = false;
  for (const char character : value) {
    const bool valid = is_ascii_lowercase_letter(character) || is_ascii_digit(character) || character == '-';
    if (!valid || (character == '-' && previous_was_hyphen)) {
      return false;
    }
    previous_was_hyphen = character == '-';
  }
  return true;
}

[[nodiscard]] bool is_valid_operator_id(const std::string_view value) noexcept {
  if (value.empty() || value.size() > kMaximumOperatorIdLength || !is_ascii_lowercase_letter(value.front()) ||
      !(is_ascii_lowercase_letter(value.back()) || is_ascii_digit(value.back()))) {
    return false;
  }

  bool previous_was_underscore = false;
  for (const char character : value) {
    const bool valid = is_ascii_lowercase_letter(character) || is_ascii_digit(character) || character == '_';
    if (!valid || (character == '_' && previous_was_underscore)) {
      return false;
    }
    previous_was_underscore = character == '_';
  }
  return true;
}

[[nodiscard]] std::string provider_target_name(const std::string_view provider_slug) {
  std::string target{provider_slug};
  for (char& character : target) {
    if (character == '-') {
      character = '_';
    }
  }
  return target;
}

[[nodiscard]] ProviderInitResult invalid_request(const std::string_view message) {
  return ProviderInitResult{
    .outcome = ProviderInitOutcome::invalid_request,
    .message = std::string(message),
  };
}

[[nodiscard]] ProviderInitResult unavailable(const std::string_view message) {
  return ProviderInitResult{
    .outcome = ProviderInitOutcome::unavailable,
    .message = std::string(message),
  };
}

[[nodiscard]] ProviderInitResult io_error(const std::string_view message) {
  return ProviderInitResult{
    .outcome = ProviderInitOutcome::io_error,
    .message = std::string(message),
  };
}

[[nodiscard]] std::string quoted_path(const std::filesystem::path& path) {
  return "'" + path.string() + "'";
}

[[nodiscard]] std::optional<std::filesystem::path> provider_template_directory(std::string* const error_message) {
  const std::array<std::pair<std::string_view, std::string_view>, 2U> candidates{{
    {"installed", KSJ_PROVIDER_TEMPLATE_INSTALL_DIR},
    {"development", KSJ_PROVIDER_TEMPLATE_SOURCE_DIR},
  }};

  std::string attempted_paths;
  for (const auto& [source, path_text] : candidates) {
    if (path_text.empty()) {
      continue;
    }

    const std::filesystem::path candidate{std::string(path_text)};
    std::error_code error;
    if (std::filesystem::is_directory(candidate, error)) {
      return candidate;
    }

    if (!attempted_paths.empty()) {
      attempted_paths += ", ";
    }
    attempted_paths += std::string(source) + " path " + quoted_path(candidate);
    if (error) {
      attempted_paths += " (" + error.message() + ")";
    }
  }

  *error_message = "Provider starter template is unavailable";
  if (!attempted_paths.empty()) {
    *error_message += "; checked " + attempted_paths;
  }
  *error_message += ". Reinstall ksj with its SDK templates.";
  return std::nullopt;
}

[[nodiscard]] bool is_not_found(const std::error_code& error) noexcept {
  return error == std::errc::no_such_file_or_directory;
}

[[nodiscard]] std::optional<std::filesystem::path> normalized_output_parent(const std::filesystem::path& output_parent,
                                                                            std::string* const error_message) {
  if (output_parent.empty()) {
    *error_message = "--output must name an existing directory";
    return std::nullopt;
  }

  std::error_code error;
  const auto absolute = std::filesystem::absolute(output_parent, error);
  if (error) {
    *error_message = "cannot resolve output directory " + quoted_path(output_parent) + ": " + error.message();
    return std::nullopt;
  }
  if (!std::filesystem::is_directory(absolute, error)) {
    if (error) {
      *error_message = "cannot inspect output directory " + quoted_path(absolute) + ": " + error.message();
    } else {
      *error_message = "--output must name an existing directory: " + quoted_path(absolute);
    }
    return std::nullopt;
  }

  const auto normalized = std::filesystem::canonical(absolute, error);
  if (error) {
    *error_message = "cannot canonicalize output directory " + quoted_path(absolute) + ": " + error.message();
    return std::nullopt;
  }
  return normalized;
}

[[nodiscard]] bool path_exists_or_is_link(const std::filesystem::path& path, std::string* const error_message) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error) {
    if (is_not_found(error)) {
      return false;
    }
    *error_message = "cannot inspect destination " + quoted_path(path) + ": " + error.message();
    return true;
  }
  return status.type() != std::filesystem::file_type::not_found;
}

class StagingDirectory {
public:
  explicit StagingDirectory(std::filesystem::path path) : path_(std::move(path)) {}

  StagingDirectory(const StagingDirectory&) = delete;
  StagingDirectory& operator=(const StagingDirectory&) = delete;

  StagingDirectory(StagingDirectory&& other) noexcept
      : path_(std::move(other.path_)), committed_(std::exchange(other.committed_, true)) {}
  StagingDirectory& operator=(StagingDirectory&& other) noexcept {
    if (this != &other) {
      if (!committed_) {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
      }
      path_ = std::move(other.path_);
      committed_ = std::exchange(other.committed_, true);
    }
    return *this;
  }

  ~StagingDirectory() {
    if (!committed_) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
  void commit() noexcept { committed_ = true; }

private:
  std::filesystem::path path_;
  bool committed_{false};
};

[[nodiscard]] std::optional<StagingDirectory> create_staging_directory(const std::filesystem::path& parent,
                                                                       const std::string_view target_name,
                                                                       std::string* const error_message) {
  static std::atomic_uint64_t sequence{0U};
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

  for (unsigned int attempt = 0U; attempt < kMaximumStagingDirectoryAttempts; ++attempt) {
    const auto ordinal = sequence.fetch_add(1U, std::memory_order_relaxed);
    const auto candidate = parent / ("." + std::string(target_name) + ".staging-" + std::to_string(timestamp) + "-" +
                                     std::to_string(ordinal));
    std::error_code error;
    if (std::filesystem::create_directory(candidate, error)) {
      return StagingDirectory(candidate);
    }
    if (error && error != std::errc::file_exists) {
      *error_message = "cannot create staging directory " + quoted_path(candidate) + ": " + error.message();
      return std::nullopt;
    }
  }

  *error_message = "cannot allocate a unique staging directory below " + quoted_path(parent);
  return std::nullopt;
}

void replace_all(std::string* const text, const std::string_view needle, const std::string_view replacement) {
  std::size_t position = 0U;
  while ((position = text->find(needle, position)) != std::string::npos) {
    text->replace(position, needle.size(), replacement);
    position += replacement.size();
  }
}

[[nodiscard]] std::string materialize_template_text(std::string text, const std::string_view provider_slug,
                                                    const std::string_view provider_target,
                                                    const std::string_view operator_id) {
  replace_all(&text, "@PROVIDER_SLUG@", provider_slug);
  replace_all(&text, "@PROVIDER_TARGET@", provider_target);
  replace_all(&text, "@OPERATOR_SLUG@", operator_id);
  replace_all(&text, "@OPERATOR_ID@", operator_id);
  return text;
}

[[nodiscard]] std::optional<std::filesystem::path> output_relative_path(const std::filesystem::path& input_relative,
                                                                        const std::string_view operator_id,
                                                                        std::string* const error_message) {
  if (input_relative.empty() || input_relative.is_absolute()) {
    *error_message = "template contains an invalid relative path " + quoted_path(input_relative);
    return std::nullopt;
  }

  for (const auto& component : input_relative) {
    if (component == "." || component == "..") {
      *error_message = "template contains a traversal path " + quoted_path(input_relative);
      return std::nullopt;
    }
  }

  auto output_relative = input_relative;
  const auto filename = output_relative.filename().string();
  constexpr std::string_view kTemplateSuffix{".in"};
  if (filename.size() > kTemplateSuffix.size() && filename.ends_with(kTemplateSuffix)) {
    output_relative.replace_filename(filename.substr(0U, filename.size() - kTemplateSuffix.size()));
  }
  if (output_relative.filename() == "operator.hpp") {
    output_relative.replace_filename(std::string(operator_id) + ".hpp");
  } else if (output_relative.filename() == "operator.cpp") {
    output_relative.replace_filename(std::string(operator_id) + ".cpp");
  } else if (output_relative.filename() == "operator.json") {
    output_relative.replace_filename(std::string(operator_id) + ".json");
  }
  return output_relative;
}

[[nodiscard]] bool write_materialized_file(const std::filesystem::path& input_path,
                                           const std::filesystem::path& output_path,
                                           const std::string_view provider_slug, const std::string_view provider_target,
                                           const std::string_view operator_id, std::string* const error_message) {
  std::ifstream input(input_path, std::ios::binary);
  if (!input.is_open()) {
    *error_message = "cannot read template file " + quoted_path(input_path);
    return false;
  }
  std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (!input.good() && !input.eof()) {
    *error_message = "cannot read complete template file " + quoted_path(input_path);
    return false;
  }

  content = materialize_template_text(std::move(content), provider_slug, provider_target, operator_id);
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    *error_message = "cannot create generated file " + quoted_path(output_path);
    return false;
  }
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  if (!output) {
    *error_message = "cannot write generated file " + quoted_path(output_path);
    return false;
  }
  return true;
}

[[nodiscard]] bool materialize_template(const std::filesystem::path& template_directory,
                                        const std::filesystem::path& staging_directory,
                                        const std::string_view provider_slug, const std::string_view provider_target,
                                        const std::string_view operator_id, std::string* const error_message) {
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator{template_directory, std::filesystem::directory_options::none,
                                                         error};
  if (error) {
    *error_message = "cannot enumerate Provider template " + quoted_path(template_directory) + ": " + error.message();
    return false;
  }

  std::unordered_set<std::string> generated_relative_paths;
  const std::filesystem::recursive_directory_iterator end;
  for (; iterator != end; iterator.increment(error)) {
    if (error) {
      *error_message = "cannot enumerate Provider template " + quoted_path(template_directory) + ": " + error.message();
      return false;
    }

    const auto entry_status = iterator->symlink_status(error);
    if (error) {
      *error_message = "cannot inspect template entry " + quoted_path(iterator->path()) + ": " + error.message();
      return false;
    }
    if (std::filesystem::is_symlink(entry_status)) {
      *error_message = "Provider template must not contain symbolic links: " + quoted_path(iterator->path());
      return false;
    }

    const auto relative = std::filesystem::relative(iterator->path(), template_directory, error);
    if (error) {
      *error_message =
        "cannot derive template-relative path for " + quoted_path(iterator->path()) + ": " + error.message();
      return false;
    }
    const auto output_relative = output_relative_path(relative, operator_id, error_message);
    if (!output_relative.has_value()) {
      return false;
    }
    const auto output_path = staging_directory / *output_relative;

    if (std::filesystem::is_directory(entry_status)) {
      std::filesystem::create_directories(output_path, error);
      if (error) {
        *error_message = "cannot create generated directory " + quoted_path(output_path) + ": " + error.message();
        return false;
      }
      continue;
    }
    if (!std::filesystem::is_regular_file(entry_status)) {
      *error_message = "Provider template contains an unsupported entry: " + quoted_path(iterator->path());
      return false;
    }

    const auto generated_key = output_relative->generic_string();
    if (!generated_relative_paths.insert(generated_key).second) {
      *error_message = "Provider template generates the same path more than once: " + quoted_path(*output_relative);
      return false;
    }

    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
      *error_message =
        "cannot create generated directory " + quoted_path(output_path.parent_path()) + ": " + error.message();
      return false;
    }
    if (!write_materialized_file(iterator->path(), output_path, provider_slug, provider_target, operator_id,
                                 error_message)) {
      return false;
    }
  }
  return true;
}

} // namespace

ProviderInitResult initialize_provider(const ProviderInitRequest& request) {
  if (!is_valid_provider_slug(request.provider_slug)) {
    return invalid_request(
      "provider slug must use lowercase letters, digits, and single hyphens; it must start with a letter and end "
      "with a letter or digit");
  }
  if (request.provider_slug.starts_with("kspacejet-")) {
    return invalid_request("provider slug must omit the 'kspacejet-' directory prefix");
  }
  if (!is_valid_operator_id(request.operator_id)) {
    return invalid_request(
      "operator id must use lowercase letters, digits, and single underscores; it must start with a letter and end "
      "with a letter or digit");
  }

  std::string message;
  const auto output_parent = normalized_output_parent(request.output_parent, &message);
  if (!output_parent.has_value()) {
    return invalid_request(message);
  }

  const auto provider_directory = *output_parent / ("kspacejet-" + request.provider_slug);
  if (path_exists_or_is_link(provider_directory, &message)) {
    if (message.empty()) {
      message = "Provider destination already exists: " + quoted_path(provider_directory);
    }
    return invalid_request(message);
  }

  const auto template_directory = provider_template_directory(&message);
  if (!template_directory.has_value()) {
    return unavailable(message);
  }

  auto staging = create_staging_directory(*output_parent, provider_directory.filename().string(), &message);
  if (!staging.has_value()) {
    return io_error(message);
  }

  const auto target_name = provider_target_name(request.provider_slug);
  if (!materialize_template(*template_directory, staging->path(), request.provider_slug, target_name,
                            request.operator_id, &message)) {
    return io_error(message);
  }

  const auto publish_status = ksj::platform::publish_directory_no_replace(staging->path(), provider_directory);
  if (!publish_status.ok()) {
    if (publish_status.code() == ksj::base::StatusCode::already_exists) {
      return invalid_request(publish_status.message());
    }
    return io_error(publish_status.message());
  }
  staging->commit();

  return ProviderInitResult{
    .outcome = ProviderInitOutcome::success,
    .provider_directory = provider_directory,
  };
}

} // namespace ksj::cli
