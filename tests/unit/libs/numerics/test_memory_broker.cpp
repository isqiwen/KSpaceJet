#include "kspacejet/memory/memory_broker.hpp"
#include "kspacejet/numerics/runtime.hpp"

#include <gtest/gtest.h>

namespace {

[[nodiscard]] ksj::memory::MemoryPoolOptions numerics_test_memory_pool_options() {
  ksj::memory::MemoryPoolOptions options;
  options.pooling_enabled = false;
  return options;
}

class NumericsMemoryBrokerEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    if (!ksj::memory::MemoryBroker::configure_instance(numerics_test_memory_pool_options())) {
      ADD_FAILURE() << "kspacejet-memory broker was initialized before numerics test memory configuration";
    }
    ksj::numerics::initialize_numerics_runtime();
  }
};

const auto* const kNumericsMemoryBrokerEnvironment =
  ::testing::AddGlobalTestEnvironment(new NumericsMemoryBrokerEnvironment);

} // namespace
