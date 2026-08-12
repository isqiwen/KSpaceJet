#include "kspacejet/process_runtime/executable_layout.hpp"

#include <filesystem>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetRuntimeExecutableLayout, ResolvesRuntimeLayoutRootFromExecutableDir) {
  const std::filesystem::path executable_dir = ksj::process_runtime::executable_layout::executable_dir();

  EXPECT_FALSE(executable_dir.empty());
  EXPECT_EQ(executable_dir / "..", ksj::process_runtime::executable_layout::runtime_layout_root());
}

} // namespace
