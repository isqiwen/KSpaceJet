#include "kspacejet/base/path.hpp"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetBasePath, HandlesTrailingSeparators) {
  const char separator = std::filesystem::path::preferred_separator;

  EXPECT_FALSE(ksj::base::path::has_trailing_separator(""));
  EXPECT_TRUE(ksj::base::path::has_trailing_separator("scan/"));
  EXPECT_TRUE(ksj::base::path::has_trailing_separator("scan\\"));
  EXPECT_EQ("scan", ksj::base::path::trim_trailing_separator("scan///"));
  EXPECT_EQ(std::string("scan") + separator, ksj::base::path::ensure_trailing_separator("scan"));
}

TEST(KSpaceJetBasePath, JoinsAndExtractsPathComponents) {
  EXPECT_EQ((std::filesystem::path("root") / "leaf").string(), ksj::base::path::join("root", "leaf"));
  EXPECT_EQ("leaf", ksj::base::path::basename("root/leaf/"));
  EXPECT_EQ("leaf", ksj::base::path::basename("leaf"));
}

TEST(KSpaceJetBasePath, NormalizesLexicalPaths) {
  const std::filesystem::path path = std::filesystem::path("root") / "scan" / ".." / "image";

  EXPECT_EQ((std::filesystem::path("root") / "image").string(), ksj::base::path::normalize(path));
}

TEST(KSpaceJetBasePath, DetectsAbsolutePathLikeInputs) {
  EXPECT_TRUE(ksj::base::path::is_absolute_path_like(std::filesystem::current_path()));
  EXPECT_TRUE(ksj::base::path::is_absolute_path_like("C:/KSpaceJet/config"));
  EXPECT_FALSE(ksj::base::path::is_absolute_path_like("relative/config"));
}

TEST(KSpaceJetBasePath, SanitizesFileNameComponents) {
  EXPECT_EQ("scan_1_ok.nii", ksj::base::path::sanitize_component("scan 1/ok.nii"));
  EXPECT_EQ("A-Z_09.", ksj::base::path::sanitize_component("A-Z_09."));
}

TEST(KSpaceJetBasePath, FormatsDirectoryPreparationErrors) {
  EXPECT_EQ("Failed to prepare KSpaceJet output folder [/tmp/KSpaceJet]: denied",
            ksj::base::path::format_prepare_directory_error("/tmp/KSpaceJet", "denied"));
  EXPECT_EQ("Failed to prepare KSpaceJet output folder", ksj::base::path::format_prepare_directory_error("", ""));
}

} // namespace
