#include "kspacejet/platform/dynamic_library.hpp"
#include "kspacejet/platform/system.hpp"

#include <gtest/gtest.h>

namespace {

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

} // namespace
