#include "kspacejet/base/file.hpp"

#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

[[nodiscard]] std::filesystem::path make_test_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path dir = std::filesystem::temp_directory_path() / ("ksj_base_file_tests_" + std::to_string(stamp));
  std::filesystem::create_directories(dir);
  return dir;
}

void append_bytes(std::vector<std::byte>& destination, const void* data, const std::size_t size) {
  const auto* bytes = static_cast<const std::byte*>(data);
  destination.insert(destination.end(), bytes, bytes + size);
}

TEST(KSpaceJetBaseFile, WritesBinaryChunksAtomically) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "chunks.bin";

  const int header[] = {7, 11};
  const std::string payload = "payload";
  const ksj::base::file::BinaryIoChunk chunks[] = {
    {
      .data = header,
      .element_size = sizeof(header[0]),
      .element_count = 2,
    },
    {
      .data = payload.data(),
      .element_size = 1,
      .element_count = payload.size(),
    },
  };

  const auto write_result = ksj::base::file::write_binary_chunks_atomically(path, chunks);

  EXPECT_TRUE(write_result.complete()) << write_result.error;
  EXPECT_EQ(sizeof(header) + payload.size(), write_result.requested_count);
  EXPECT_EQ(write_result.requested_count, write_result.transferred_count);

  const auto read_result = ksj::base::file::read_binary_file_to_vector(path);
  ASSERT_TRUE(read_result.complete()) << read_result.io.error;

  std::vector<std::byte> expected;
  append_bytes(expected, header, sizeof(header));
  append_bytes(expected, payload.data(), payload.size());
  EXPECT_EQ(expected, read_result.bytes);

  std::error_code error;
  std::error_code cleanup_error;
  std::filesystem::remove_all(dir, cleanup_error);
}

TEST(KSpaceJetBaseFile, AppendsBinaryFile) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "append.bin";

  const std::string first = "first";
  const std::string second = "_second";
  ASSERT_TRUE(ksj::base::file::write_binary_file_atomically(path, first.data(), 1U, first.size()).complete());

  const auto append_result = ksj::base::file::append_binary_file(path, second.data(), 1U, second.size());

  EXPECT_TRUE(append_result.complete()) << append_result.error;

  const auto read_result = ksj::base::file::read_binary_file_to_vector(path);
  ASSERT_TRUE(read_result.complete()) << read_result.io.error;
  const std::string actual(reinterpret_cast<const char*>(read_result.bytes.data()), read_result.bytes.size());
  EXPECT_EQ(first + second, actual);

  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

TEST(KSpaceJetBaseFile, BinaryFileStreamsReadWriteAndDetectEof) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "nested" / "stream.bin";

  ksj::base::file::BinaryFile writer;
  std::string error;
  ASSERT_TRUE(writer.open_write(path, error)) << error;

  const int header[] = {1, 2, 3};
  const std::string payload = "payload";
  EXPECT_TRUE(writer.write_all(header, sizeof(header[0]), 3U).complete());
  EXPECT_TRUE(writer.write_all(payload.data(), 1U, payload.size()).complete());
  EXPECT_TRUE(writer.flush(error)) << error;
  EXPECT_TRUE(writer.close(error)) << error;

  ksj::base::file::BinaryFile reader;
  ASSERT_TRUE(reader.open_read(path, error)) << error;

  int actual_header[3]{};
  auto header_read = reader.read_exact(actual_header, sizeof(actual_header[0]), 3U);
  EXPECT_TRUE(header_read.complete()) << header_read.error;
  EXPECT_EQ(0, std::memcmp(header, actual_header, sizeof(header)));

  std::vector<char> actual_payload(payload.size());
  auto payload_read = reader.read_exact(actual_payload.data(), 1U, actual_payload.size());
  EXPECT_TRUE(payload_read.complete()) << payload_read.error;
  EXPECT_EQ(payload, std::string(actual_payload.data(), actual_payload.size()));

  EXPECT_TRUE(reader.close(error)) << error;
  ASSERT_TRUE(reader.open_read(path, error)) << error;
  int streamed_header[3]{};
  EXPECT_TRUE(reader.read_exact_into(streamed_header, sizeof(streamed_header[0]), 3U, error)) << error;
  EXPECT_EQ(0, std::memcmp(header, streamed_header, sizeof(header)));
  std::vector<char> streamed_payload(payload.size());
  EXPECT_TRUE(reader.read_exact_into(streamed_payload.data(), 1U, streamed_payload.size(), error)) << error;
  EXPECT_EQ(payload, std::string(streamed_payload.data(), streamed_payload.size()));

  char extra = '\0';
  auto eof_read = reader.read_some(&extra, 1U, 1U);
  EXPECT_FALSE(eof_read.complete());
  EXPECT_EQ(0U, eof_read.transferred_count);
  EXPECT_TRUE(reader.eof());
  EXPECT_TRUE(reader.close(error)) << error;

  std::error_code cleanup_error;
  std::filesystem::remove_all(dir, cleanup_error);
}

TEST(KSpaceJetBaseFile, AppendsTextFile) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "reports" / "append.txt";

  const auto first_result = ksj::base::file::append_text_file(path, "first\n");
  const auto second_result = ksj::base::file::append_text_file(path, "second\n");

  EXPECT_TRUE(first_result.complete()) << first_result.error;
  EXPECT_TRUE(second_result.complete()) << second_result.error;

  const auto read_result = ksj::base::file::read_binary_file_to_vector(path);
  ASSERT_TRUE(read_result.complete()) << read_result.io.error;
  const std::string actual(reinterpret_cast<const char*>(read_result.bytes.data()), read_result.bytes.size());
  EXPECT_EQ("first\nsecond\n", actual);

  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

TEST(KSpaceJetBaseFile, WritesTextFileAtomically) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "reports" / "text.txt";

  const auto first_result = ksj::base::file::write_text_file_atomically(path, "old text");
  const auto second_result = ksj::base::file::write_text_file_atomically(path, "new text\n");

  EXPECT_TRUE(first_result.complete()) << first_result.error;
  EXPECT_TRUE(second_result.complete()) << second_result.error;

  const auto read_result = ksj::base::file::read_binary_file_to_vector(path);
  ASSERT_TRUE(read_result.complete()) << read_result.io.error;
  const std::string actual(reinterpret_cast<const char*>(read_result.bytes.data()), read_result.bytes.size());
  EXPECT_EQ("new text\n", actual);

  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

TEST(KSpaceJetBaseFile, ReadsBinaryRange) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "range.bin";
  const std::string contents = "0123456789";
  ASSERT_TRUE(ksj::base::file::write_binary_file_atomically(path, contents.data(), 1U, contents.size()).complete());

  char buffer[4] = {};
  const auto read_result = ksj::base::file::read_binary_range(path, 3U, buffer, sizeof(buffer));

  EXPECT_TRUE(read_result.complete()) << read_result.error;
  EXPECT_EQ(std::string("3456"), std::string(buffer, sizeof(buffer)));

  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

TEST(KSpaceJetBaseFile, ReportsOutOfRangeBinaryRead) {
  const std::filesystem::path dir = make_test_dir();
  const std::filesystem::path path = dir / "range_error.bin";
  const std::string contents = "abc";
  ASSERT_TRUE(ksj::base::file::write_binary_file_atomically(path, contents.data(), 1U, contents.size()).complete());

  char buffer[4] = {};
  const auto read_result = ksj::base::file::read_binary_range(path, 2U, buffer, sizeof(buffer));

  EXPECT_FALSE(read_result.complete());
  EXPECT_FALSE(read_result.error.empty());

  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

} // namespace
