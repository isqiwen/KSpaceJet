#include "kspacejet/base/file.hpp"

#include "kspacejet/base/path.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <system_error>
#include <thread>
#include <utility>

namespace ksj::base::file {
namespace {

[[nodiscard]] std::string format_errno_error(const char* action, const std::filesystem::path& path) {
  std::string message{action};
  message += " [";
  message += path.string();
  message += "]";
  if (errno != 0) {
    message += ": ";
    message += std::strerror(errno);
  }
  return message;
}

[[nodiscard]] std::string format_filesystem_error(const char* action, const std::filesystem::path& path,
                                                  const std::error_code& error_code) {
  std::string message{action};
  message += " [";
  message += path.string();
  message += "]";
  if (error_code) {
    message += ": ";
    message += error_code.message();
  }
  return message;
}

[[nodiscard]] std::string format_filesystem_error(const char* action, const std::filesystem::path& source,
                                                  const std::filesystem::path& destination,
                                                  const std::error_code& error_code) {
  std::string message{action};
  message += " [";
  message += source.string();
  message += "] -> [";
  message += destination.string();
  message += "]";
  if (error_code) {
    message += ": ";
    message += error_code.message();
  }
  return message;
}

[[nodiscard]] bool prepare_parent_directory(const std::filesystem::path& path, std::string& error) {
  const std::filesystem::path parent = path.parent_path();
  if (parent.empty()) {
    return true;
  }
  if (ksj::base::path::ensure_directory_exists(parent)) {
    return true;
  }
  error = ksj::base::path::format_prepare_directory_error(parent.string(), "parent directory is not writable");
  return false;
}

[[nodiscard]] std::filesystem::path make_temp_path(const std::filesystem::path& final_path) {
  static std::atomic<std::uint64_t> counter{0};
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
  const auto sequence = counter.fetch_add(1U, std::memory_order_relaxed);

  std::filesystem::path temp_path = final_path;
  temp_path += ".tmp.";
  temp_path += std::to_string(timestamp);
  temp_path += ".";
  temp_path += std::to_string(thread_id);
  temp_path += ".";
  temp_path += std::to_string(sequence);
  return temp_path;
}

[[nodiscard]] bool checked_element_byte_count(const std::size_t element_size, const std::size_t element_count,
                                              std::size_t& byte_count, std::string& error) {
  byte_count = 0;
  if (element_count == 0U) {
    return true;
  }
  if (element_size == 0U) {
    error = "invalid binary element size";
    return false;
  }
  if (element_count > std::numeric_limits<std::size_t>::max() / element_size) {
    error = "binary byte count overflow";
    return false;
  }
  byte_count = element_size * element_count;
  return true;
}

[[nodiscard]] bool validate_binary_buffer(const void* data, const std::size_t element_size,
                                          const std::size_t element_count, std::string& error) {
  if (data == nullptr && element_count != 0U) {
    error = "invalid binary buffer";
    return false;
  }
  std::size_t byte_count = 0;
  return checked_element_byte_count(element_size, element_count, byte_count, error);
}

[[nodiscard]] bool checked_add_byte_count(const std::size_t value, std::size_t& total, std::string& error) {
  if (value > std::numeric_limits<std::size_t>::max() - total) {
    error = "binary byte count overflow";
    return false;
  }
  total += value;
  return true;
}

[[nodiscard]] bool validate_binary_chunks(std::span<const BinaryIoChunk> chunks, std::size_t& total_bytes,
                                          std::string& error) {
  total_bytes = 0;
  for (const BinaryIoChunk& chunk : chunks) {
    if (!validate_binary_buffer(chunk.data, chunk.element_size, chunk.element_count, error)) {
      return false;
    }

    std::size_t chunk_bytes = 0;
    if (!checked_element_byte_count(chunk.element_size, chunk.element_count, chunk_bytes, error)) {
      return false;
    }
    if (!checked_add_byte_count(chunk_bytes, total_bytes, error)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] BinaryIoResult write_binary_chunks_file(const std::filesystem::path& path,
                                                      std::span<const BinaryIoChunk> chunks) {
  BinaryIoResult result{
    .path = path.string(),
    .error = {},
  };

  if (result.path.empty()) {
    result.error = "empty binary file path";
    return result;
  }
  if (!validate_binary_chunks(chunks, result.requested_count, result.error)) {
    return result;
  }
  if (!prepare_parent_directory(path, result.error)) {
    return result;
  }

  errno = 0;
  FILE* file = std::fopen(result.path.c_str(), "wb");
  if (file == nullptr) {
    result.error = format_errno_error("failed to create binary file", path);
    return result;
  }

  result.opened = true;
  for (const BinaryIoChunk& chunk : chunks) {
    if (chunk.element_count == 0U) {
      continue;
    }

    const std::size_t written = std::fwrite(chunk.data, chunk.element_size, chunk.element_count, file);
    result.transferred_count += written * chunk.element_size;
    if (written != chunk.element_count) {
      result.error = "short binary chunk write";
      break;
    }
  }

  if (std::fclose(file) != 0 && result.error.empty()) {
    result.error = format_errno_error("failed to close binary file after write", path);
  }
  if (result.transferred_count != result.requested_count && result.error.empty()) {
    result.error = "short binary chunk write";
  }
  return result;
}

} // namespace

BinaryFile::~BinaryFile() {
  close_ignoring_errors();
}

BinaryFile::BinaryFile(BinaryFile&& other) noexcept : file_(other.file_), path_(std::move(other.path_)) {
  other.file_ = nullptr;
}

BinaryFile& BinaryFile::operator=(BinaryFile&& other) noexcept {
  if (this != &other) {
    close_ignoring_errors();
    file_ = other.file_;
    path_ = std::move(other.path_);
    other.file_ = nullptr;
  }
  return *this;
}

bool BinaryFile::open_read(const std::filesystem::path& path, std::string& error) {
  return open_impl(path, "rb", false, "open binary file for read", error);
}

bool BinaryFile::open_write(const std::filesystem::path& path, std::string& error) {
  return open_impl(path, "wb", true, "open binary file for write", error);
}

bool BinaryFile::open_append(const std::filesystem::path& path, std::string& error) {
  return open_impl(path, "ab", true, "open binary file for append", error);
}

bool BinaryFile::is_open() const noexcept {
  return file_ != nullptr;
}

bool BinaryFile::eof() const noexcept {
  return file_ != nullptr && std::feof(static_cast<FILE*>(file_)) != 0;
}

const std::filesystem::path& BinaryFile::path() const noexcept {
  return path_;
}

BinaryIoResult BinaryFile::write_all(const void* data, const std::size_t element_size,
                                     const std::size_t element_count) {
  BinaryIoResult result{
    .path = path_.string(),
    .requested_count = element_count,
    .opened = is_open(),
    .error = {},
  };

  if (!is_open()) {
    result.error = "binary file is not open";
    return result;
  }
  if (!validate_binary_buffer(data, element_size, element_count, result.error)) {
    return result;
  }
  if (element_count == 0U) {
    return result;
  }

  errno = 0;
  FILE* file = static_cast<FILE*>(file_);
  result.transferred_count = std::fwrite(data, element_size, element_count, file);
  if (result.transferred_count != result.requested_count) {
    result.error =
      std::ferror(file) != 0 ? format_errno_error("failed to write binary stream", path_) : "short binary stream write";
  }
  return result;
}

BinaryIoResult BinaryFile::read_some(void* data, const std::size_t element_size, const std::size_t element_count) {
  BinaryIoResult result{
    .path = path_.string(),
    .requested_count = element_count,
    .opened = is_open(),
    .error = {},
  };

  if (!is_open()) {
    result.error = "binary file is not open";
    return result;
  }
  if ((data == nullptr && element_count != 0U) || (element_size == 0U && element_count != 0U)) {
    result.error = "invalid binary read buffer or element size";
    return result;
  }
  if (element_count == 0U) {
    return result;
  }

  errno = 0;
  FILE* file = static_cast<FILE*>(file_);
  result.transferred_count = std::fread(data, element_size, element_count, file);
  if (result.transferred_count != result.requested_count && std::ferror(file) != 0) {
    result.error = format_errno_error("failed to read binary stream", path_);
  }
  return result;
}

BinaryIoResult BinaryFile::read_exact(void* data, const std::size_t element_size, const std::size_t element_count) {
  BinaryIoResult result = read_some(data, element_size, element_count);
  if (result.opened && result.transferred_count != result.requested_count && result.error.empty()) {
    result.error = eof() ? "unexpected end of binary file [" + path_.string() + "]" : "short binary stream read";
  }
  return result;
}

std::size_t BinaryFile::read_some_into(void* data, const std::size_t element_size, const std::size_t element_count,
                                       std::string& error) {
  error.clear();
  if (!is_open()) {
    error = "binary file is not open";
    return 0U;
  }
  if ((data == nullptr && element_count != 0U) || (element_size == 0U && element_count != 0U)) {
    error = "invalid binary read buffer or element size";
    return 0U;
  }
  if (element_count == 0U) {
    return 0U;
  }

  errno = 0;
  FILE* file = static_cast<FILE*>(file_);
  const auto transferred_count = std::fread(data, element_size, element_count, file);
  if (transferred_count != element_count && std::ferror(file) != 0) {
    error = format_errno_error("failed to read binary stream", path_);
  }
  return transferred_count;
}

bool BinaryFile::read_exact_into(void* data, const std::size_t element_size, const std::size_t element_count,
                                 std::string& error) {
  const auto transferred_count = read_some_into(data, element_size, element_count, error);
  if (transferred_count == element_count) {
    return true;
  }
  if (error.empty()) {
    error = eof() ? "unexpected end of binary file [" + path_.string() + "]" : "short binary stream read";
  }
  return false;
}

bool BinaryFile::flush(std::string& error) {
  if (!is_open()) {
    error.clear();
    return true;
  }

  errno = 0;
  if (std::fflush(static_cast<FILE*>(file_)) != 0) {
    error = format_errno_error("failed to flush binary file", path_);
    return false;
  }
  error.clear();
  return true;
}

bool BinaryFile::close(std::string& error) {
  if (!is_open()) {
    error.clear();
    return true;
  }

  errno = 0;
  FILE* file = static_cast<FILE*>(file_);
  file_ = nullptr;
  if (std::fclose(file) != 0) {
    error = format_errno_error("failed to close binary file", path_);
    return false;
  }
  error.clear();
  return true;
}

void BinaryFile::close_ignoring_errors() noexcept {
  if (is_open()) {
    FILE* file = static_cast<FILE*>(file_);
    file_ = nullptr;
    (void)std::fclose(file);
  }
}

bool BinaryFile::open_impl(const std::filesystem::path& path, const char* mode, const bool create_parent,
                           const char* action, std::string& error) {
  close_ignoring_errors();
  path_ = path;
  if (path_.empty()) {
    error = "empty binary file path";
    return false;
  }
  if (create_parent && !prepare_parent_directory(path_, error)) {
    return false;
  }

  errno = 0;
  file_ = std::fopen(path_.string().c_str(), mode);
  if (file_ == nullptr) {
    error = format_errno_error(action, path_);
    return false;
  }
  error.clear();
  return true;
}

bool copy_file(const std::filesystem::path& source, const std::filesystem::path& destination,
               std::string& error) noexcept {
  if (source.empty() || destination.empty()) {
    error = "empty source or destination path";
    return false;
  }
  if (!prepare_parent_directory(destination, error)) {
    return false;
  }

  std::error_code error_code;
  std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error_code);
  if (error_code) {
    error = format_filesystem_error("failed to copy file", source, destination, error_code);
    return false;
  }
  error.clear();
  return true;
}

bool remove_file(const std::filesystem::path& path, std::string& error) noexcept {
  if (path.empty()) {
    error = "empty file path";
    return false;
  }

  std::error_code error_code;
  if (!std::filesystem::exists(path, error_code)) {
    if (error_code) {
      error = format_filesystem_error("failed to inspect file before remove", path, error_code);
      return false;
    }
    error.clear();
    return true;
  }

  if (std::filesystem::is_directory(path, error_code)) {
    if (error_code) {
      error = format_filesystem_error("failed to inspect file before remove", path, error_code);
      return false;
    }
    error = "refusing to remove directory as file [" + path.string() + "]";
    return false;
  }

  if (!std::filesystem::remove(path, error_code)) {
    error = error_code ? format_filesystem_error("failed to remove file", path, error_code)
                       : "file was not removed [" + path.string() + "]";
    return false;
  }

  error.clear();
  return true;
}

bool rename_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                 std::string& error) noexcept {
  if (source.empty() || destination.empty()) {
    error = "empty source or destination path";
    return false;
  }
  if (!prepare_parent_directory(destination, error)) {
    return false;
  }

  std::error_code error_code;
  if (std::filesystem::exists(destination, error_code)) {
    if (error_code) {
      error = format_filesystem_error("failed to inspect destination before rename", destination, error_code);
      return false;
    }
    error = "destination file already exists [" + destination.string() + "]";
    return false;
  }

  std::filesystem::rename(source, destination, error_code);
  if (error_code) {
    error = format_filesystem_error("failed to rename file", source, destination, error_code);
    return false;
  }

  error.clear();
  return true;
}

bool replace_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                  std::string& error) noexcept {
  if (source.empty() || destination.empty()) {
    error = "empty source or destination path";
    return false;
  }
  if (!prepare_parent_directory(destination, error)) {
    return false;
  }

  std::error_code error_code;
  std::filesystem::rename(source, destination, error_code);
  if (error_code) {
    error = format_filesystem_error("failed to replace file", source, destination, error_code);
    return false;
  }

  error.clear();
  return true;
}

BinaryIoResult write_binary_file(const std::filesystem::path& path, const void* data, const std::size_t element_size,
                                 const std::size_t element_count) {
  BinaryIoResult result{
    .path = path.string(),
    .requested_count = element_count,
    .error = {},
  };

  if (result.path.empty()) {
    result.error = "empty binary file path";
    return result;
  }
  if ((data == nullptr && element_count != 0U) || (element_size == 0U && element_count != 0U)) {
    result.error = "invalid binary write buffer or element size";
    return result;
  }
  if (!prepare_parent_directory(path, result.error)) {
    return result;
  }

  errno = 0;
  FILE* file = std::fopen(result.path.c_str(), "wb");
  if (file == nullptr) {
    result.error = format_errno_error("failed to create binary file", path);
    return result;
  }

  result.opened = true;
  result.transferred_count = std::fwrite(data, element_size, element_count, file);
  if (std::fclose(file) != 0 && result.error.empty()) {
    result.error = format_errno_error("failed to close binary file after write", path);
  }
  if (result.transferred_count != result.requested_count && result.error.empty()) {
    result.error = "short binary file write";
  }
  return result;
}

BinaryIoResult write_binary_file_atomically(const std::filesystem::path& path, const void* data,
                                            const std::size_t element_size, const std::size_t element_count) {
  if (path.empty()) {
    return BinaryIoResult{
      .path = {},
      .requested_count = element_count,
      .error = "empty binary file path",
    };
  }

  const std::filesystem::path temp_path = make_temp_path(path);

  BinaryIoResult result = write_binary_file(temp_path, data, element_size, element_count);
  if (!result.complete()) {
    std::string cleanup_error;
    (void)remove_file(temp_path, cleanup_error);
    result.path = path.string();
    return result;
  }

  std::string replace_error;
  if (!replace_file(temp_path, path, replace_error)) {
    std::string cleanup_error;
    (void)remove_file(temp_path, cleanup_error);
    result.path = path.string();
    result.opened = false;
    result.error = replace_error;
    return result;
  }

  result.path = path.string();
  return result;
}

BinaryIoResult write_binary_chunks_atomically(const std::filesystem::path& path,
                                              std::span<const BinaryIoChunk> chunks) {
  if (path.empty()) {
    std::size_t requested_count = 0;
    std::string ignored_error;
    (void)validate_binary_chunks(chunks, requested_count, ignored_error);
    return BinaryIoResult{
      .path = {},
      .requested_count = requested_count,
      .error = "empty binary file path",
    };
  }

  const std::filesystem::path temp_path = make_temp_path(path);

  BinaryIoResult result = write_binary_chunks_file(temp_path, chunks);
  if (!result.complete()) {
    std::string cleanup_error;
    (void)remove_file(temp_path, cleanup_error);
    result.path = path.string();
    return result;
  }

  std::string replace_error;
  if (!replace_file(temp_path, path, replace_error)) {
    std::string cleanup_error;
    (void)remove_file(temp_path, cleanup_error);
    result.path = path.string();
    result.opened = false;
    result.error = replace_error;
    return result;
  }

  result.path = path.string();
  return result;
}

BinaryIoResult append_binary_file(const std::filesystem::path& path, const void* data, const std::size_t element_size,
                                  const std::size_t element_count) {
  BinaryIoResult result{
    .path = path.string(),
    .requested_count = element_count,
    .error = {},
  };

  if (result.path.empty()) {
    result.error = "empty binary file path";
    return result;
  }
  if (!validate_binary_buffer(data, element_size, element_count, result.error)) {
    return result;
  }
  if (!prepare_parent_directory(path, result.error)) {
    return result;
  }

  errno = 0;
  FILE* file = std::fopen(result.path.c_str(), "ab");
  if (file == nullptr) {
    result.error = format_errno_error("failed to open binary file for append", path);
    return result;
  }

  result.opened = true;
  result.transferred_count = std::fwrite(data, element_size, element_count, file);
  if (std::fclose(file) != 0 && result.error.empty()) {
    result.error = format_errno_error("failed to close binary file after append", path);
  }
  if (result.transferred_count != result.requested_count && result.error.empty()) {
    result.error = "short binary file append";
  }
  return result;
}

namespace {

TextIoResult write_text_file_impl(const std::filesystem::path& path, const std::string_view text, const char* mode,
                                  const char* open_error, const char* close_error, const char* short_error) {
  TextIoResult result{
    .path = path.string(),
    .requested_bytes = text.size(),
    .error = {},
  };

  if (result.path.empty()) {
    result.error = "empty text file path";
    return result;
  }
  if (!prepare_parent_directory(path, result.error)) {
    return result;
  }

  errno = 0;
  FILE* file = std::fopen(result.path.c_str(), mode);
  if (file == nullptr) {
    result.error = format_errno_error(open_error, path);
    return result;
  }

  result.opened = true;
  if (!text.empty()) {
    result.transferred_bytes = std::fwrite(text.data(), 1U, text.size(), file);
  }
  if (std::fclose(file) != 0 && result.error.empty()) {
    result.error = format_errno_error(close_error, path);
  }
  if (result.transferred_bytes != result.requested_bytes && result.error.empty()) {
    result.error = short_error;
  }
  return result;
}

} // namespace

TextIoResult append_text_file(const std::filesystem::path& path, const std::string_view text) {
  return write_text_file_impl(path, text, "ab", "failed to open text file for append",
                              "failed to close text file after append", "short text file append");
}

TextIoResult write_text_file_atomically(const std::filesystem::path& path, const std::string_view text) {
  if (path.empty()) {
    return TextIoResult{
      .path = {},
      .requested_bytes = text.size(),
      .error = "empty text file path",
    };
  }

  const std::filesystem::path temp_path = make_temp_path(path);

  TextIoResult result = write_text_file_impl(temp_path, text, "wb", "failed to create text file",
                                             "failed to close text file after write", "short text file write");
  if (!result.complete()) {
    std::string cleanup_error;
    (void)remove_file(temp_path, cleanup_error);
    result.path = path.string();
    return result;
  }

  std::string replace_error;
  if (!replace_file(temp_path, path, replace_error)) {
    std::string cleanup_error;
    (void)remove_file(temp_path, cleanup_error);
    result.path = path.string();
    result.opened = false;
    result.error = replace_error;
    return result;
  }

  result.path = path.string();
  return result;
}

BinaryIoResult read_binary_file(const std::filesystem::path& path, void* data, const std::size_t element_size,
                                const std::size_t element_count) {
  BinaryIoResult result{
    .path = path.string(),
    .requested_count = element_count,
    .error = {},
  };

  if (result.path.empty()) {
    result.error = "empty binary file path";
    return result;
  }
  if ((data == nullptr && element_count != 0U) || (element_size == 0U && element_count != 0U)) {
    result.error = "invalid binary read buffer or element size";
    return result;
  }

  errno = 0;
  FILE* file = std::fopen(result.path.c_str(), "rb");
  if (file == nullptr) {
    result.error = format_errno_error("failed to open binary file", path);
    return result;
  }

  result.opened = true;
  result.transferred_count = std::fread(data, element_size, element_count, file);
  if (std::fclose(file) != 0 && result.error.empty()) {
    result.error = format_errno_error("failed to close binary file after read", path);
  }
  if (result.transferred_count != result.requested_count && result.error.empty()) {
    result.error = "short binary file read";
  }
  return result;
}

BinaryBufferResult read_binary_file_to_vector(const std::filesystem::path& path) {
  BinaryBufferResult result;
  result.io.path = path.string();

  if (result.io.path.empty()) {
    result.io.error = "empty binary file path";
    return result;
  }

  std::error_code error_code;
  const auto file_size = std::filesystem::file_size(path, error_code);
  if (error_code) {
    result.io.error = format_filesystem_error("failed to inspect binary file size", path, error_code);
    return result;
  }
  if (file_size > std::numeric_limits<std::size_t>::max()) {
    result.io.error = "binary file is too large to read [" + path.string() + "]";
    return result;
  }

  result.bytes.resize(static_cast<std::size_t>(file_size));
  result.io = read_binary_file(path, result.bytes.data(), 1U, result.bytes.size());
  if (!result.io.complete() && result.io.transferred_count < result.bytes.size()) {
    result.bytes.resize(result.io.transferred_count);
  }
  return result;
}

BinaryIoResult read_binary_range(const std::filesystem::path& path, const std::uintmax_t offset, void* data,
                                 const std::size_t size) {
  BinaryIoResult result{
    .path = path.string(),
    .requested_count = size,
    .error = {},
  };

  if (result.path.empty()) {
    result.error = "empty binary file path";
    return result;
  }
  if (data == nullptr && size != 0U) {
    result.error = "invalid binary read buffer";
    return result;
  }

  std::error_code error_code;
  const auto file_size = std::filesystem::file_size(path, error_code);
  if (error_code) {
    result.error = format_filesystem_error("failed to inspect binary file size", path, error_code);
    return result;
  }
  if (offset > file_size || size > file_size - offset) {
    result.error = "binary read range is outside the file [" + path.string() + "]";
    return result;
  }
  if (offset > static_cast<std::uintmax_t>(std::numeric_limits<long>::max())) {
    result.error = "binary read range offset is too large [" + path.string() + "]";
    return result;
  }

  errno = 0;
  FILE* file = std::fopen(result.path.c_str(), "rb");
  if (file == nullptr) {
    result.error = format_errno_error("failed to open binary file", path);
    return result;
  }

  result.opened = true;
  if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    result.error = format_errno_error("failed to seek binary file", path);
  } else {
    result.transferred_count = std::fread(data, 1U, size, file);
  }
  if (std::fclose(file) != 0 && result.error.empty()) {
    result.error = format_errno_error("failed to close binary file after read", path);
  }
  if (result.transferred_count != result.requested_count && result.error.empty()) {
    result.error = "short binary range read";
  }
  return result;
}

} // namespace ksj::base::file
