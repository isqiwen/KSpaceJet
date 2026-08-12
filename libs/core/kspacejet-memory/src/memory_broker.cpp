#include "kspacejet/memory/memory_broker.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>

namespace ksj::memory {
namespace {

struct BrokerInstanceConfig {
  std::mutex mutex;
  MemoryPoolOptions options;
  bool configured{false};
  bool instance_created{false};
};

[[nodiscard]] BrokerInstanceConfig& broker_instance_config() {
  static BrokerInstanceConfig config;
  return config;
}

[[nodiscard]] MemoryPoolOptions consume_broker_instance_options() {
  auto& config = broker_instance_config();
  std::lock_guard lock(config.mutex);
  if (!config.configured) {
    throw std::invalid_argument("kspacejet-memory broker must be configured before first use");
  }
  config.instance_created = true;
  return config.options;
}

} // namespace

MemoryBroker::MemoryBroker(MemoryPoolOptions options) : MemoryBroker(TopologyDiscovery::discover(), options) {}

MemoryBroker::MemoryBroker(TopologySnapshot topology, MemoryPoolOptions options)
    : placement_(topology), pool_(MemoryPool::create(std::move(topology), options)) {}

bool MemoryBroker::configure_instance(MemoryPoolOptions options) {
  auto& config = broker_instance_config();
  std::lock_guard lock(config.mutex);
  if (config.instance_created) {
    return false;
  }
  config.options = std::move(options);
  config.configured = true;
  return true;
}

MemoryBroker& MemoryBroker::instance() {
  static MemoryBroker broker(consume_broker_instance_options());
  return broker;
}

MemoryLease MemoryBroker::acquire(const AllocationRequest& request) {
  const auto placement = placement_.decide(request.properties, request.worker_index, request.shard_key);
  return pool_->acquire(request, placement);
}

void MemoryBroker::trim() noexcept {
  pool_->trim_empty_slabs();
}

} // namespace ksj::memory
