#pragma once

#include "kspacejet/base/result.hpp"
#include "kspacejet/recon/execution_plan.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"
#include "kspacejet/recon/runtime/fixed_buffer_edge.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"

#include <cstddef>
#include <memory>

namespace ksj::recon::runtime {

// Every slab exposed by this owner is aligned to at least this boundary.  A
// pool whose TypeDescriptor requires a larger alignment receives that larger
// alignment while retaining this 64-byte guarantee.
inline constexpr std::size_t kSynchronousGraphPlanStorageAlignment = kSynchronousGraphScratchMinimumAlignment;

// Owning storage for every synchronous BufferPool, FixedBufferEdge, and
// Provider scratch slab declared by one frozen ExecutionPlan. The generated
// SynchronousGraphExecutorStorage only borrows these spans, so this object
// must outlive the executor and all of its leases.
class SynchronousGraphPlanStorage final {
public:
  [[nodiscard]] static ksj::base::Result<std::unique_ptr<SynchronousGraphPlanStorage>>
  create(const ExecutionPlan& execution_plan);

  SynchronousGraphPlanStorage(const SynchronousGraphPlanStorage&) = delete;
  SynchronousGraphPlanStorage& operator=(const SynchronousGraphPlanStorage&) = delete;
  SynchronousGraphPlanStorage(SynchronousGraphPlanStorage&&) = delete;
  SynchronousGraphPlanStorage& operator=(SynchronousGraphPlanStorage&&) = delete;
  ~SynchronousGraphPlanStorage();

  [[nodiscard]] const SynchronousGraphExecutorStorage& executor_storage() const noexcept;

private:
  struct Impl;

  explicit SynchronousGraphPlanStorage(std::unique_ptr<Impl> implementation) noexcept;

  std::unique_ptr<Impl> implementation_;
};

// Maps one completed host-frame context to the exact immutable graph identity
// used by ingress publication.  The semantic-key hash is a stable FNV-1a
// encoding of the seven FrameSemanticKey uint16 fields in field order; order
// and ordinal are deliberately not folded into that hash.
[[nodiscard]] DataItemIdentity make_data_item_identity(const FrameSlotContext& context,
                                                       std::uint64_t item_ordinal) noexcept;

} // namespace ksj::recon::runtime
