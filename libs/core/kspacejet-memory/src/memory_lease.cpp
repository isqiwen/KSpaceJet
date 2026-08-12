#include "kspacejet/memory/memory_lease.hpp"

#include <utility>

#include "kspacejet/memory/memory_pool.hpp"

namespace ksj::memory {

MemoryLease::MemoryLease(std::shared_ptr<MemoryPool> pool, AllocationRecord record) noexcept
    : pool_(std::move(pool)), record_(record) {}

MemoryLease::MemoryLease(MemoryLease&& other) noexcept : pool_(std::move(other.pool_)), record_(other.record_) {
  other.record_ = AllocationRecord{};
}

MemoryLease& MemoryLease::operator=(MemoryLease&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  release();
  pool_ = std::move(other.pool_);
  record_ = other.record_;
  other.record_ = AllocationRecord{};
  return *this;
}

MemoryLease::~MemoryLease() {
  release();
}

void MemoryLease::release() noexcept {
  if (pool_ == nullptr || record_.data == nullptr) {
    return;
  }
  pool_->release(record_);
  pool_.reset();
  record_ = AllocationRecord{};
}

} // namespace ksj::memory
