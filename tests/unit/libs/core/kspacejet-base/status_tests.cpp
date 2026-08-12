#include "kspacejet/base/result.hpp"
#include "kspacejet/base/status.hpp"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

namespace {

TEST(KSpaceJetBaseStatus, DefaultStatusIsOk) {
  const ksj::base::Status status;

  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(static_cast<bool>(status));
  EXPECT_EQ(ksj::base::StatusCode::ok, status.code());
  EXPECT_TRUE(status.message().empty());
  EXPECT_EQ("ok", status.to_string());
}

TEST(KSpaceJetBaseStatus, CarriesStatusCodeAndMessage) {
  const auto status = ksj::base::Status::InvalidArgument("missing scan id");

  EXPECT_FALSE(status.ok());
  EXPECT_EQ(ksj::base::StatusCode::invalid_argument, status.code());
  EXPECT_EQ("missing scan id", status.message());
  EXPECT_EQ("invalid_argument: missing scan id", status.to_string());
}

TEST(KSpaceJetBaseStatus, ComparesCodeAndMessage) {
  EXPECT_EQ(ksj::base::Status::NotFound("series"), ksj::base::Status::NotFound("series"));
  EXPECT_NE(ksj::base::Status::NotFound("series"), ksj::base::Status::NotFound("image"));
}

TEST(KSpaceJetBaseResult, HoldsValueOrStatus) {
  ksj::base::Result<int> value{42};
  EXPECT_TRUE(value.ok());
  EXPECT_EQ(42, value.value());
  EXPECT_TRUE(value.status().ok());

  ksj::base::Result<int> missing{ksj::base::Status::NotFound("image")};
  EXPECT_FALSE(missing.ok());
  EXPECT_EQ(ksj::base::StatusCode::not_found, missing.status().code());
  EXPECT_THROW((void)missing.value(), std::logic_error);
}

} // namespace
