#include "kspacejet/logging/logging.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <stdexcept>
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

struct ThrowingLogValue {};

std::ostream& operator<<(std::ostream&, const ThrowingLogValue&) {
  throw std::runtime_error("deliberate formatting failure");
}

} // namespace

TEST(KSpaceJetLogging, ConfiguresDefaultConsoleIdempotently) {
  ksj::logging::Shutdown();

  std::string error_message{"stale error"};
  ASSERT_TRUE(ksj::logging::ConfigureDefaultConsole("ksj_logging_default_console_test", &error_message))
    << error_message;
  EXPECT_TRUE(error_message.empty());
  EXPECT_TRUE(ksj::logging::IsConfigured());

  error_message = "stale error";
  EXPECT_TRUE(ksj::logging::ConfigureDefaultConsole("another_logger_name", &error_message)) << error_message;
  EXPECT_TRUE(error_message.empty());

  ksj::logging::Shutdown();
}

TEST(KSpaceJetLogging, ExplicitConfigurationReplacesDefaultConsoleFallback) {
  ksj::logging::Shutdown();

  std::string error_message;
  ASSERT_TRUE(ksj::logging::ConfigureDefaultConsole("ksj_logging_fallback_test", &error_message)) << error_message;

  ksj::config::LoggingConfig config;
  config.logger_name = "ksj_logging_explicit_test";
  config.async = false;
  ASSERT_TRUE(ksj::logging::Configure(config, ".", nullptr, nullptr, &error_message)) << error_message;

  EXPECT_FALSE(ksj::logging::Configure(config, ".", nullptr, nullptr, &error_message));
  EXPECT_FALSE(error_message.empty());

  ksj::logging::Shutdown();
}

TEST(KSpaceJetLogging, RejectsStructuredDiagnosticOutput) {
  ksj::logging::Shutdown();

  ksj::config::LoggingConfig config;
  config.logger_name = "ksj_logging_text_only_test";
  config.async = false;
  config.output_format = "json";

  std::string error_message;
  EXPECT_FALSE(ksj::logging::Configure(config, ".", nullptr, nullptr, &error_message));
  EXPECT_EQ(error_message, "logging.output_format currently supports only text.");
  EXPECT_FALSE(ksj::logging::IsConfigured());
}

TEST(KSpaceJetLogging, FormattedLoggingNeverThrows) {
  ksj::logging::Shutdown();

  std::string error_message;
  ASSERT_TRUE(ksj::logging::ConfigureDefaultConsole("ksj_logging_noexcept_test", &error_message)) << error_message;

  EXPECT_NO_THROW(KSJ_LOG_INFO(ThrowingLogValue{}));

  ksj::logging::Shutdown();
}

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
