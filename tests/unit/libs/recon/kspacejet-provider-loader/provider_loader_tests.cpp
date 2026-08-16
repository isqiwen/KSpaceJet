#include "kspacejet/provider/loader/provider_loader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace {

using ksj::provider::loader::ProviderModule;

[[nodiscard]] ksj::provider::loader::Digest256 digest(const std::uint8_t seed) {
  auto result = ksj::provider::loader::Digest256{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    result[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

[[nodiscard]] std::filesystem::path valid_provider_path() {
  return std::filesystem::path(KSJ_PROVIDER_LOADER_VALID_MODULE);
}

TEST(ProviderLoader, LoadsValidatedProviderAndLeasePinsModule) {
  auto loaded = ProviderModule::load(valid_provider_path());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  ProviderModule module = std::move(loaded).value();

  ASSERT_TRUE(module.loaded());
  ASSERT_NE(module.loaded_path(), nullptr);
  ASSERT_NE(module.descriptor(), nullptr);
  EXPECT_EQ(module.descriptor()->provider_id, "org.kspacejet.tests.provider-loader");
  ASSERT_EQ(module.descriptor()->operators.size(), 1U);
  EXPECT_EQ(module.descriptor()->operators.front().operator_id, "test_operator");

  auto lease = module.acquire();
  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.api(), nullptr);
  ASSERT_NE(lease.api()->operator_destroy, nullptr);
  module = ProviderModule{};

  ASSERT_TRUE(lease.valid());
  ASSERT_NE(lease.descriptor(), nullptr);
  EXPECT_EQ(lease.descriptor()->provider_id, "org.kspacejet.tests.provider-loader");
  lease.api()->operator_destroy(nullptr);
}

TEST(ProviderLoader, RejectsProviderWithReservedHeaderFields) {
  auto loaded = ProviderModule::load(std::filesystem::path(KSJ_PROVIDER_LOADER_BAD_ABI_MODULE));
  ASSERT_FALSE(loaded.ok());
  EXPECT_NE(loaded.status().message().find("reserved ABI header fields"), std::string::npos) << loaded.status();
}

TEST(ProviderLoader, RejectsModuleWithoutQuerySymbol) {
  auto loaded = ProviderModule::load(std::filesystem::path(KSJ_PROVIDER_LOADER_MISSING_QUERY_MODULE));
  ASSERT_FALSE(loaded.ok());
  EXPECT_NE(loaded.status().message().find("ksj_provider_query"), std::string::npos) << loaded.status();
}

TEST(ProviderLoader, RequiresAbsoluteTrustedFilePath) {
  auto loaded = ProviderModule::load(valid_provider_path().filename());
  ASSERT_FALSE(loaded.ok());
  EXPECT_EQ(loaded.status().code(), ksj::base::StatusCode::invalid_argument);
}

TEST(ProviderLoader, EnforcesTrustedRootWhenConfigured) {
  ksj::provider::loader::ProviderLoadOptions options;
  options.trusted_root = valid_provider_path().parent_path();
  auto loaded = ProviderModule::load(valid_provider_path(), options);
  ASSERT_TRUE(loaded.ok()) << loaded.status();

  options.trusted_root = valid_provider_path().parent_path() / "not-the-provider-directory";
  auto rejected = ProviderModule::load(valid_provider_path(), options);
  ASSERT_FALSE(rejected.ok());
  EXPECT_NE(rejected.status().message().find("trusted_root"), std::string::npos) << rejected.status();
}

TEST(ProviderLoader, AttestsRequiredBundleDigest) {
  ksj::provider::loader::ProviderLoadOptions options;
  options.required_bundle_digest = digest(0x80U);
  auto loaded = ProviderModule::load(valid_provider_path(), options);
  ASSERT_TRUE(loaded.ok()) << loaded.status();

  options.required_bundle_digest = digest(0x81U);
  auto mismatch = ProviderModule::load(valid_provider_path(), options);
  ASSERT_FALSE(mismatch.ok());
  EXPECT_NE(mismatch.status().message().find("bundle_digest"), std::string::npos) << mismatch.status();
}

} // namespace
