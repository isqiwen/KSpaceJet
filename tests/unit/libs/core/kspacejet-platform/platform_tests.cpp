#include "kspacejet/base/status.hpp"
#include "kspacejet/platform/dynamic_library.hpp"
#include "kspacejet/platform/filesystem.hpp"
#include "kspacejet/platform/system.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>

namespace {

class ScopedTestDirectory {
public:
  explicit ScopedTestDirectory(std::filesystem::path path) : path_(std::move(path)) {}

  ScopedTestDirectory(const ScopedTestDirectory&) = delete;
  ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;

  ~ScopedTestDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

[[nodiscard]] ScopedTestDirectory make_test_directory() {
  static std::atomic_uint64_t sequence{0U};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path =
    std::filesystem::temp_directory_path() / ("ksj_platform_publish_" + std::to_string(stamp) + "_" +
                                              std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)));
  std::error_code error;
  EXPECT_TRUE(std::filesystem::create_directory(path, error)) << error.message();
  return ScopedTestDirectory(path);
}

TEST(KSpaceJetPlatformDynamicLibrary, CombinesLoadModeFlags) {
  const auto mode = ksj::platform::LoadMode::now | ksj::platform::LoadMode::global;

  EXPECT_TRUE(ksj::platform::has_flag(mode, ksj::platform::LoadMode::now));
  EXPECT_TRUE(ksj::platform::has_flag(mode, ksj::platform::LoadMode::global));
  EXPECT_FALSE(ksj::platform::has_flag(mode, ksj::platform::LoadMode::local));
}

TEST(KSpaceJetPlatformDynamicLibrary, ResolvesSharedLibraryFileNameForHost) {
#ifdef _WIN32
  EXPECT_EQ("recon.dll", ksj::platform::shared_library_file_name("recon"));
  EXPECT_EQ("recon.dll", ksj::platform::shared_library_file_name("recon.dll"));
#else
  EXPECT_EQ("librecon.so", ksj::platform::shared_library_file_name("recon"));
  EXPECT_EQ("librecon.so", ksj::platform::shared_library_file_name("librecon"));
  EXPECT_EQ("recon.so", ksj::platform::shared_library_file_name("recon.so"));
#endif
}

TEST(KSpaceJetPlatformSystem, ReportsAtLeastOneCpuCore) {
  EXPECT_GE(ksj::platform::available_cpu_cores(), 1U);
}

TEST(KSpaceJetPlatformFilesystem, PublishesDirectoryWithoutReplacingDestination) {
  const ScopedTestDirectory root = make_test_directory();
  const auto staging = root.path() / "staging";
  const auto destination = root.path() / "published";

  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directory(staging, error)) << error.message();

  const auto status = ksj::platform::publish_directory_no_replace(staging, destination);

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_FALSE(std::filesystem::exists(staging));
  EXPECT_TRUE(std::filesystem::is_directory(destination));
}

TEST(KSpaceJetPlatformFilesystem, RefusesToReplaceExistingDestination) {
  const ScopedTestDirectory root = make_test_directory();
  const auto staging = root.path() / "staging";
  const auto destination = root.path() / "published";

  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directory(staging, error)) << error.message();
  error.clear();
  ASSERT_TRUE(std::filesystem::create_directory(destination, error)) << error.message();

  const auto status = ksj::platform::publish_directory_no_replace(staging, destination);

  EXPECT_EQ(ksj::base::StatusCode::already_exists, status.code()) << status;
  EXPECT_TRUE(std::filesystem::is_directory(staging));
  EXPECT_TRUE(std::filesystem::is_directory(destination));
}

TEST(KSpaceJetPlatformFilesystem, RejectsNonDirectorySource) {
  const ScopedTestDirectory root = make_test_directory();
  const auto source_file = root.path() / "staging-file";
  const auto destination = root.path() / "published";

  std::ofstream output(source_file);
  ASSERT_TRUE(output.is_open());
  output << "not a directory";
  output.close();

  const auto status = ksj::platform::publish_directory_no_replace(source_file, destination);

  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, status.code()) << status;
  EXPECT_FALSE(std::filesystem::exists(destination));
}

TEST(KSpaceJetPlatformFilesystem, RequiresExistingDestinationParent) {
  const ScopedTestDirectory root = make_test_directory();
  const auto staging = root.path() / "staging";
  const auto destination = root.path() / "missing" / "published";

  std::error_code error;
  ASSERT_TRUE(std::filesystem::create_directory(staging, error)) << error.message();

  const auto status = ksj::platform::publish_directory_no_replace(staging, destination);

  EXPECT_EQ(ksj::base::StatusCode::not_found, status.code()) << status;
  EXPECT_TRUE(std::filesystem::is_directory(staging));
}

} // namespace
