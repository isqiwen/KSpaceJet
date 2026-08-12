#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ksj::base::file {

struct BinaryIoChunk {
  const void* data = nullptr;
  std::size_t element_size = 1;
  std::size_t element_count = 0;
};

struct BinaryIoResult {
  std::string path;
  std::size_t requested_count = 0;
  std::size_t transferred_count = 0;
  bool opened = false;
  std::string error;

  [[nodiscard]] bool complete() const noexcept { return opened && transferred_count == requested_count; }
};

struct BinaryBufferResult {
  std::vector<std::byte> bytes;
  BinaryIoResult io;

  [[nodiscard]] bool complete() const noexcept { return io.complete(); }
};

struct TextIoResult {
  std::string path;
  std::size_t requested_bytes = 0;
  std::size_t transferred_bytes = 0;
  bool opened = false;
  std::string error;

  [[nodiscard]] bool complete() const noexcept { return opened && transferred_bytes == requested_bytes; }
};

class BinaryFile {
public:
  BinaryFile() = default;
  ~BinaryFile();

  BinaryFile(const BinaryFile&) = delete;
  BinaryFile& operator=(const BinaryFile&) = delete;

  BinaryFile(BinaryFile&& other) noexcept;
  BinaryFile& operator=(BinaryFile&& other) noexcept;

  [[nodiscard]] bool open_read(const std::filesystem::path& path, std::string& error);
  [[nodiscard]] bool open_write(const std::filesystem::path& path, std::string& error);
  [[nodiscard]] bool open_append(const std::filesystem::path& path, std::string& error);
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] bool eof() const noexcept;
  [[nodiscard]] const std::filesystem::path& path() const noexcept;

  [[nodiscard]] BinaryIoResult write_all(const void* data, std::size_t element_size, std::size_t element_count);
  [[nodiscard]] BinaryIoResult read_some(void* data, std::size_t element_size, std::size_t element_count);
  [[nodiscard]] BinaryIoResult read_exact(void* data, std::size_t element_size, std::size_t element_count);
  [[nodiscard]] std::size_t read_some_into(void* data, std::size_t element_size, std::size_t element_count,
                                           std::string& error);
  [[nodiscard]] bool read_exact_into(void* data, std::size_t element_size, std::size_t element_count,
                                     std::string& error);
  [[nodiscard]] bool flush(std::string& error);
  [[nodiscard]] bool close(std::string& error);
  void close_ignoring_errors() noexcept;

private:
  [[nodiscard]] bool open_impl(const std::filesystem::path& path, const char* mode, bool create_parent,
                               const char* action, std::string& error);

  void* file_ = nullptr;
  std::filesystem::path path_;
};

// Copy one regular file to another path. The source file is left unchanged and
// an existing destination file is overwritten.
[[nodiscard]] bool copy_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                             std::string& error) noexcept;

// Remove one regular file. A missing path is treated as success; directories are
// rejected so callers do not accidentally remove trees through this API.
[[nodiscard]] bool remove_file(const std::filesystem::path& path, std::string& error) noexcept;

// Move or rename one file. The source path is consumed and the operation fails
// if the destination already exists.
[[nodiscard]] bool rename_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                               std::string& error) noexcept;

// Atomically publish a completed temporary file when source and destination are
// on the same filesystem. The source path is consumed and the destination is
// replaced if it already exists.
[[nodiscard]] bool replace_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                                std::string& error) noexcept;

// Write element_count binary elements from data. The parent directory is created
// when needed, and BinaryIoResult reports short writes and open/close failures.
[[nodiscard]] BinaryIoResult write_binary_file(const std::filesystem::path& path, const void* data,
                                               std::size_t element_size, std::size_t element_count);

// Write to a unique temporary file in the destination directory, then publish it
// with replace_file(). This avoids leaving a partial destination file after a
// crash or interrupted write.
[[nodiscard]] BinaryIoResult write_binary_file_atomically(const std::filesystem::path& path, const void* data,
                                                          std::size_t element_size, std::size_t element_count);

// Write several binary chunks as one complete file. BinaryIoResult counts bytes
// for chunked writes because each chunk may use a different element size.
[[nodiscard]] BinaryIoResult write_binary_chunks_atomically(const std::filesystem::path& path,
                                                            std::span<const BinaryIoChunk> chunks);

// Append element_count binary elements to the end of a file. The parent
// directory is created when needed. Existing file contents are preserved.
[[nodiscard]] BinaryIoResult append_binary_file(const std::filesystem::path& path, const void* data,
                                                std::size_t element_size, std::size_t element_count);

// Append text bytes to the end of a file. The parent directory is created when
// needed. This is intended for reports and logs where existing contents must be
// preserved; use write_binary_file_atomically() for complete-file replacement.
[[nodiscard]] TextIoResult append_text_file(const std::filesystem::path& path, std::string_view text);

// Write text bytes as one complete file through a temporary file and atomic
// replace. Use this for generated text artifacts where a partial file would be
// misleading after an interrupted write.
[[nodiscard]] TextIoResult write_text_file_atomically(const std::filesystem::path& path, std::string_view text);

// Read element_count binary elements into data. BinaryIoResult reports whether
// the file opened successfully and whether the requested element count was read.
[[nodiscard]] BinaryIoResult read_binary_file(const std::filesystem::path& path, void* data, std::size_t element_size,
                                              std::size_t element_count);

// Read a whole binary file into memory. BinaryIoResult counts bytes for this
// API because the element size is fixed to one byte.
[[nodiscard]] BinaryBufferResult read_binary_file_to_vector(const std::filesystem::path& path);

// Read size bytes starting at offset into data.
[[nodiscard]] BinaryIoResult read_binary_range(const std::filesystem::path& path, std::uintmax_t offset, void* data,
                                               std::size_t size);

} // namespace ksj::base::file
