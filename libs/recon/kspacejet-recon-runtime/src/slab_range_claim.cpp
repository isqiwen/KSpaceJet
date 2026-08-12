#include "kspacejet/recon/runtime/detail/slab_range_claim.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon::runtime::detail {
namespace {

struct SlabRange {
  std::uintptr_t begin{0U};
  std::uintptr_t end{0U};
};

struct SlabRangeClaimRecord {
  std::uint64_t claim_id{0U};
  std::vector<SlabRange> ranges;
};

struct SlabRangeClaimRegistry {
  std::mutex mutex;
  std::vector<SlabRangeClaimRecord> claims;
  std::uint64_t next_claim_id{1U};
};

// A live Pool/Edge state can outlast ordinary runtime owner objects through a
// capability. Keep this runtime-image registry alive through process teardown
// so a claim destructor never observes static-destruction order.
[[nodiscard]] SlabRangeClaimRegistry& registry() {
  static auto* const value = new SlabRangeClaimRegistry();
  return *value;
}

[[nodiscard]] bool overlaps(const SlabRange lhs, const SlabRange rhs) noexcept {
  return lhs.begin < rhs.end && rhs.begin < lhs.end;
}

[[nodiscard]] ksj::base::Result<std::vector<SlabRange>>
validated_ranges(const std::span<const ksj::base::ByteSpan> slabs) {
  std::vector<SlabRange> ranges;
  ranges.reserve(slabs.size());
  for (const auto slab : slabs) {
    if (slab.empty()) {
      continue;
    }
    if (slab.data() == nullptr || slab.size() > std::numeric_limits<std::uintptr_t>::max()) {
      return ksj::base::Status::InvalidArgument("caller slab cannot be represented as an address range");
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(slab.data());
    const auto length = static_cast<std::uintptr_t>(slab.size());
    if (length > std::numeric_limits<std::uintptr_t>::max() - begin) {
      return ksj::base::Status::InvalidArgument("caller slab address range overflows");
    }
    const SlabRange candidate{.begin = begin, .end = begin + length};
    for (const auto existing : ranges) {
      if (overlaps(candidate, existing)) {
        return ksj::base::Status::InvalidArgument("caller payload, metadata, and control slabs must not overlap");
      }
    }
    ranges.push_back(candidate);
  }
  if (ranges.empty()) {
    return ksj::base::Status::InvalidArgument("at least one non-empty caller slab is required");
  }
  return ranges;
}

} // namespace

SlabRangeClaim::SlabRangeClaim(const std::uint64_t claim_id) noexcept : claim_id_(claim_id) {}

SlabRangeClaim::~SlabRangeClaim() {
  release_noexcept();
}

SlabRangeClaim::SlabRangeClaim(SlabRangeClaim&& other) noexcept : claim_id_(std::exchange(other.claim_id_, 0U)) {}

SlabRangeClaim& SlabRangeClaim::operator=(SlabRangeClaim&& other) noexcept {
  if (this != &other) {
    release_noexcept();
    claim_id_ = std::exchange(other.claim_id_, 0U);
  }
  return *this;
}

bool SlabRangeClaim::valid() const noexcept {
  return claim_id_ != 0U;
}

void SlabRangeClaim::release_noexcept() noexcept {
  const auto claim_id = std::exchange(claim_id_, 0U);
  if (claim_id == 0U) {
    return;
  }
  try {
    auto& global_registry = registry();
    std::lock_guard lock(global_registry.mutex);
    const auto found = std::find_if(global_registry.claims.begin(), global_registry.claims.end(),
                                    [claim_id](const SlabRangeClaimRecord& record) {
                                      return record.claim_id == claim_id;
                                    });
    if (found != global_registry.claims.end()) {
      global_registry.claims.erase(found);
    }
  } catch (...) {
    // A failed claim release is fail-closed: retaining the exclusive record is
    // safer than allowing reuse of a caller slab that might still be live.
  }
}

ksj::base::Result<SlabRangeClaim> claim_exclusive_slab_ranges(const std::span<const ksj::base::ByteSpan> slabs) {
  try {
    auto ranges = validated_ranges(slabs);
    if (!ranges.ok()) {
      return ranges.status();
    }

    auto& global_registry = registry();
    std::lock_guard lock(global_registry.mutex);
    for (const auto& active_claim : global_registry.claims) {
      for (const auto proposed : ranges.value()) {
        for (const auto active : active_claim.ranges) {
          if (overlaps(proposed, active)) {
            return ksj::base::Status::Unavailable("caller slab overlaps an active runtime-image slab claim");
          }
        }
      }
    }
    if (global_registry.next_claim_id == 0U ||
        global_registry.next_claim_id == std::numeric_limits<std::uint64_t>::max()) {
      return ksj::base::Status::Unavailable("runtime slab claim identity space is exhausted");
    }
    const auto claim_id = global_registry.next_claim_id;
    global_registry.claims.push_back({.claim_id = claim_id, .ranges = std::move(ranges).value()});
    ++global_registry.next_claim_id;
    return SlabRangeClaim{claim_id};
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to record runtime caller slab claim");
  }
}

} // namespace ksj::recon::runtime::detail
