#include "kspacejet/logging/logging.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path find_log_file(const fs::path& directory) {
  for (const auto& entry : fs::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".log") {
      return entry.path();
    }
  }
  return {};
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST(KSpaceJetLogging, RecreatesDailyLogFileAfterDeletion) {
  const fs::path log_dir = fs::temp_directory_path() / "ksj_logging_recreate_file_test";
  fs::remove_all(log_dir);
  fs::create_directories(log_dir);

  ksj::config::LoggingConfig config;
  config.logger_name = "ksj_logging_recreate_file_test";
  config.async = false;
  config.console.enabled = false;
  config.file.enabled = true;
  config.file.level = "info";
  config.file.path = (log_dir / "ksj_logging_recreate_file_test.log").string();
  config.file.pattern = "%v";

  std::string error_message;
  ASSERT_TRUE(ksj::logging::Configure(config, log_dir.c_str(), nullptr, nullptr, &error_message)) << error_message;

  KSJ_LOG_INFO("first log message");
  ksj::logging::Flush();

  const fs::path first_log_file = find_log_file(log_dir);
  ASSERT_FALSE(first_log_file.empty());
  EXPECT_NE(read_text_file(first_log_file).find("first log message"), std::string::npos);

  fs::remove(first_log_file);
  ASSERT_FALSE(fs::exists(first_log_file));

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  KSJ_LOG_INFO("second log message");
  ksj::logging::Flush();

  const fs::path recreated_log_file = find_log_file(log_dir);
  ASSERT_FALSE(recreated_log_file.empty());
  EXPECT_NE(read_text_file(recreated_log_file).find("second log message"), std::string::npos);

  ksj::logging::Shutdown();
  fs::remove_all(log_dir);
}
