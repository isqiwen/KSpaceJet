#include "kspacejet/config/site_config.hpp"
#include "kspacejet/config/key_value_config.hpp"
#include "kspacejet/config/runtime_config.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetConfigSiteConfig, UsesConfigFileName) {
  const std::filesystem::path primary{ksj::config::primary_site_config_text_path()};
  auto expected = std::filesystem::path("..") / ".." / "Site_config" / ksj::config::kSiteConfigFileName;
  expected.make_preferred();

  EXPECT_EQ(ksj::config::kSiteConfigFileName, primary.filename().string());
  EXPECT_EQ("Site_config", primary.parent_path().filename().string());
  EXPECT_EQ(expected, primary);
}

TEST(KSpaceJetConfigSiteConfig, BuildsRuntimeConfPath) {
  const auto path = ksj::config::primary_runtime_config_path("kspacejet_runtime");

  EXPECT_EQ("kspacejet_runtime.conf", std::filesystem::path(path).filename().string());
  EXPECT_EQ("config", std::filesystem::path(path).parent_path().filename().string());
  EXPECT_EQ("../config/kspacejet_runtime.conf", path);
}

TEST(KSpaceJetConfigKeyValueConfig, ParsesCommentsEmptyValuesAndQuotedValues) {
  const std::string text = R"(
# KSpaceJet runtime configuration in key=value form.
runtime_output_root_dir=
crash.enabled=true
logging.console.pattern="[%^%l%$] [%t] [%s:%#] %v"
provider.max_inflight_acquisitions=1.5
)";

  auto parsed = ksj::config::parse_key_value_config(text, "sample.conf");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto& config = parsed.value();

  EXPECT_TRUE(config.contains("runtime_output_root_dir"));
  EXPECT_EQ("", config.value_or("runtime_output_root_dir", "fallback"));
  EXPECT_EQ("[%^%l%$] [%t] [%s:%#] %v", config.value_or("logging.console.pattern", ""));

  auto enabled = config.bool_value("crash.enabled", false);
  ASSERT_TRUE(enabled.ok()) << enabled.status();
  EXPECT_TRUE(enabled.value());

  auto max_inflight = config.double_value("provider.max_inflight_acquisitions", 0.0);
  ASSERT_TRUE(max_inflight.ok()) << max_inflight.status();
  EXPECT_DOUBLE_EQ(1.5, max_inflight.value());
}

TEST(KSpaceJetConfigKeyValueConfig, RejectsMalformedLines) {
  auto parsed = ksj::config::parse_key_value_config("missing_separator\n", "bad.conf");
  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

TEST(KSpaceJetConfigRuntimeConfig, MapsRuntimeSchema) {
  const std::string text = R"(
runtime_output_root_dir=/tmp/KSpaceJet
provider_config_file=ksj_provider.conf
output.results_dir=results-output
output.log_dir=kspacejet-logs

crash.enabled=true
crash.capture_terminate=true
crash.use_altstack=true
crash.print_readable_stack=true
crash.launch_debugger_from_env=false
crash.debugger_env_var=USE_GDB_ON_FAULT
crash.max_frames=64

memory.pool.enabled=true
memory.pool.size_classes=64K,1M,2M,4M
memory.pool.size_class_block_counts=1024,64,32,16

debug.root_dir=debug-root
debug.report_dir=reports
debug.algorithm_dir=algorithms
debug.slice_dump_dir=
debug.matrix_dump_dir=

logging.flush_level=warn
logging.async=true
logging.queue_size=8192
logging.async_worker_count=1
logging.output_format=text
logging.console.enabled=true
logging.console.level=info
logging.console.console_color=false
logging.console.pattern="[%^%l%$] [%t] [%s:%#] %v"
logging.file.enabled=false
logging.file.level=debug
logging.file.retention_days=30
logging.file.pattern="[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v"
)";

  auto parsed = ksj::config::parse_runtime_config(text, "kspacejet_runtime.conf");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  const auto& config = parsed.value();

  EXPECT_EQ("/tmp/KSpaceJet", config.runtime_output_root_dir);
  EXPECT_EQ("ksj_provider.conf", config.provider_config_file);
  EXPECT_EQ("results-output", config.output.results_dir);
  EXPECT_EQ("kspacejet-logs", config.output.log_dir);
  EXPECT_FALSE(config.crash.launch_debugger_from_env);
  EXPECT_EQ(64U, config.crash.max_frames);
  EXPECT_TRUE(config.memory.pool.enabled);
  EXPECT_EQ((std::vector<std::size_t>{64U * 1024U, 1U * 1024U * 1024U, 2U * 1024U * 1024U, 4U * 1024U * 1024U}),
            config.memory.pool.size_classes);
  EXPECT_EQ((std::vector<std::size_t>{1024U, 64U, 32U, 16U}), config.memory.pool.size_class_block_counts);
  EXPECT_EQ("debug-root", config.debug.root_dir);
  EXPECT_EQ(8192U, config.logging.queue_size);
  EXPECT_EQ("debug", config.logging.file.level);
  EXPECT_EQ("[%^%l%$] [%t] [%s:%#] %v", config.logging.console.pattern);
}

TEST(KSpaceJetConfigRuntimeConfig, LoadsProviderConfigFile) {
  const auto temp_root = std::filesystem::temp_directory_path() / "ksj_runtime_config_provider_test";
  std::filesystem::create_directories(temp_root);
  const auto runtime_path = temp_root / "kspacejet_runtime.conf";
  const auto provider_path = temp_root / "ksj_provider.conf";

  {
    std::ofstream runtime_file(runtime_path);
    runtime_file << "provider_config_file=ksj_provider.conf\n";
    runtime_file << "memory.pool.size_classes=64K,1M\n";
    runtime_file << "memory.pool.size_class_block_counts=1024,64\n";
    runtime_file << "logging.console.level=info\n";
  }
  {
    std::ofstream provider_file(provider_path);
    provider_file << "logging.console.level=debug\n";
    provider_file << "debug.enabled=true\n";
  }

  auto loaded = ksj::config::load_runtime_config_file(runtime_path.string());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  EXPECT_EQ("ksj_provider.conf", loaded.value().provider_config_file);
  EXPECT_EQ("debug", loaded.value().logging.console.level);
  EXPECT_TRUE(loaded.value().debug.enabled);

  std::filesystem::remove_all(temp_root);
}

TEST(KSpaceJetConfigRuntimeConfig, RejectsInvalidUnsignedValue) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.size_classes=64K\n"
                                                  "memory.pool.size_class_block_counts=1024\n"
                                                  "logging.queue_size=-1\n",
                                                  "kspacejet_runtime.conf");

  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

TEST(KSpaceJetConfigRuntimeConfig, RejectsUnsortedMemoryPoolSizeClasses) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.size_classes=1M,64K\n", "kspacejet_runtime.conf");

  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

TEST(KSpaceJetConfigRuntimeConfig, AllowsMissingMemoryPoolConfigurationForNonMemoryPoolProcesses) {
  auto parsed = ksj::config::parse_runtime_config("runtime_output_root_dir=/tmp/KSpaceJet\n", "kspacejet_runtime.conf");

  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_TRUE(parsed.value().memory.pool.enabled);
  EXPECT_TRUE(parsed.value().memory.pool.size_classes.empty());
  EXPECT_TRUE(parsed.value().memory.pool.size_class_block_counts.empty());
}

TEST(KSpaceJetConfigRuntimeConfig, AllowsDisabledMemoryPoolWithoutSizeClassTable) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.enabled=false\n", "kspacejet_runtime.conf");

  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_FALSE(parsed.value().memory.pool.enabled);
  EXPECT_TRUE(parsed.value().memory.pool.size_classes.empty());
  EXPECT_TRUE(parsed.value().memory.pool.size_class_block_counts.empty());
}

TEST(KSpaceJetConfigRuntimeConfig, RejectsPartialMemoryPoolConfiguration) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.size_classes=64K,1M\n", "kspacejet_runtime.conf");

  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

TEST(KSpaceJetConfigRuntimeConfig, RejectsMemoryPoolBlockCountsWithoutSizeClasses) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.size_class_block_counts=1024,64\n", "kspacejet_runtime.conf");

  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

TEST(KSpaceJetConfigRuntimeConfig, RejectsMismatchedMemoryPoolBlockCounts) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.size_classes=64K,1M\n"
                                                  "memory.pool.size_class_block_counts=1024\n",
                                                  "kspacejet_runtime.conf");

  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

TEST(KSpaceJetConfigRuntimeConfig, RejectsZeroMemoryPoolBlockCount) {
  auto parsed = ksj::config::parse_runtime_config("memory.pool.size_class_block_counts=1024,0\n", "kspacejet_runtime.conf");

  ASSERT_FALSE(parsed.ok());
  EXPECT_EQ(ksj::base::StatusCode::parse_error, parsed.status().code());
}

} // namespace
