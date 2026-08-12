#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/base/span.hpp"

#include <cstdint>
#include <span>

namespace ksj::recon::runtime::detail {

// Exclusive ownership of caller-owned slab ranges within this
// ksj_recon_runtime image. This is runtime-internal plumbing for participating
// FixedBufferPool/FixedBufferEdge instances; it is not physical allocation
// admission or a replacement for a memory broker. Callers remain responsible
// for overlap with raw-slab primitives outside this facility. A successful
// claim owns every non-empty supplied range until this move-only token dies.
class SlabRangeClaim final {
public:
  SlabRangeClaim() = default;
  ~SlabRangeClaim();

  SlabRangeClaim(const SlabRangeClaim&) = delete;
  SlabRangeClaim& operator=(const SlabRangeClaim&) = delete;
  SlabRangeClaim(SlabRangeClaim&& other) noexcept;
  SlabRangeClaim& operator=(SlabRangeClaim&& other) noexcept;

  [[nodiscard]] bool valid() const noexcept;

private:
  friend ksj::base::Result<SlabRangeClaim> claim_exclusive_slab_ranges(std::span<const ksj::base::ByteSpan> slabs);

  explicit SlabRangeClaim(std::uint64_t claim_id) noexcept;
  void release_noexcept() noexcept;

  std::uint64_t claim_id_{0U};
};

// Validates that the supplied non-empty ranges do not overlap each other,
// then atomically claims them against every active participating Pool/Edge
// claim in this runtime image. A local overlap is invalid_argument; overlap
// with a live claim is unavailable. Callers must retain the returned token in
// shared state until every issued capability that could access the slabs has
// settled.
[[nodiscard]] ksj::base::Result<SlabRangeClaim> claim_exclusive_slab_ranges(std::span<const ksj::base::ByteSpan> slabs);

} // namespace ksj::recon::runtime::detail
