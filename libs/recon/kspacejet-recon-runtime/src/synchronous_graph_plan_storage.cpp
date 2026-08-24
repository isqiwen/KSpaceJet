#include "kspacejet/recon/runtime/synchronous_graph_plan_storage.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {
namespace {

class AlignedSlab final {
public:
  AlignedSlab() = default;

  AlignedSlab(const AlignedSlab&) = delete;
  AlignedSlab& operator=(const AlignedSlab&) = delete;

  AlignedSlab(AlignedSlab&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)), bytes_(std::exchange(other.bytes_, 0U)),
        alignment_(std::exchange(other.alignment_, kSynchronousGraphPlanStorageAlignment)) {}

  AlignedSlab& operator=(AlignedSlab&& other) noexcept {
    if (this != &other) {
      release();
      data_ = std::exchange(other.data_, nullptr);
      bytes_ = std::exchange(other.bytes_, 0U);
      alignment_ = std::exchange(other.alignment_, kSynchronousGraphPlanStorageAlignment);
    }
    return *this;
  }

  ~AlignedSlab() { release(); }

  [[nodiscard]] static ksj::base::Result<AlignedSlab> create(const std::size_t bytes, const std::size_t alignment) {
    if (alignment < kSynchronousGraphPlanStorageAlignment || (alignment & (alignment - 1U)) != 0U) {
      return ksj::base::Status::ValidationError(
        "SynchronousGraphPlanStorage requires a power-of-two alignment of at least 64 bytes");
    }
    try {
      AlignedSlab result;
      result.bytes_ = bytes;
      result.alignment_ = alignment;
      if (bytes != 0U) {
        result.data_ = static_cast<ksj::base::byte*>(::operator new(bytes, std::align_val_t{alignment}));
        std::memset(result.data_, 0, bytes);
      }
      return result;
    } catch (const std::bad_alloc&) {
      return ksj::base::Status::OutOfMemory("unable to allocate a synchronous graph slab");
    }
  }

  [[nodiscard]] ksj::base::ByteSpan view() noexcept { return {data_, bytes_}; }

private:
  void release() noexcept {
    if (data_ != nullptr) {
      ::operator delete(data_, std::align_val_t{alignment_});
      data_ = nullptr;
    }
    bytes_ = 0U;
  }

  ksj::base::byte* data_{nullptr};
  std::size_t bytes_{0U};
  std::size_t alignment_{kSynchronousGraphPlanStorageAlignment};
};

struct PoolSlabs final {
  std::string pool_id;
  AlignedSlab payload;
  AlignedSlab metadata;
  AlignedSlab control;
};

struct EdgeSlabs final {
  std::string edge_id;
  AlignedSlab control;
};

struct NodeScratchSlab final {
  std::string node_id;
  AlignedSlab storage;
};

[[nodiscard]] ksj::base::Result<std::size_t> slot_storage_bytes(const Quantity slots, const Quantity capacity,
                                                                const std::string_view field_name) {
  if (slots > std::numeric_limits<std::size_t>::max() || capacity > std::numeric_limits<std::size_t>::max() ||
      (capacity != 0U && slots > std::numeric_limits<std::size_t>::max() / capacity)) {
    return ksj::base::Status::ValidationError("SynchronousGraphPlanStorage " + std::string(field_name) +
                                              " exceeds this host's addressable ByteSpan size");
  }
  return static_cast<std::size_t>(slots * capacity);
}

[[nodiscard]] ksj::base::Result<std::size_t> pool_alignment(const SynchronousBufferPoolPlan& pool) {
  if (pool.payload_alignment_bytes() > std::numeric_limits<std::size_t>::max()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphPlanStorage pool payload alignment exceeds this host's size_t range");
  }
  const auto alignment =
    std::max(kSynchronousGraphPlanStorageAlignment, static_cast<std::size_t>(pool.payload_alignment_bytes()));
  if ((alignment & (alignment - 1U)) != 0U) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphPlanStorage pool payload alignment is not a power of two");
  }
  return alignment;
}

} // namespace

struct SynchronousGraphPlanStorage::Impl final {
  std::vector<PoolSlabs> pools;
  std::vector<EdgeSlabs> edges;
  std::vector<NodeScratchSlab> node_scratch;
  SynchronousGraphExecutorStorage executor_storage;
};

SynchronousGraphPlanStorage::SynchronousGraphPlanStorage(std::unique_ptr<Impl> implementation) noexcept
    : implementation_(std::move(implementation)) {}

SynchronousGraphPlanStorage::~SynchronousGraphPlanStorage() = default;

ksj::base::Result<std::unique_ptr<SynchronousGraphPlanStorage>>
SynchronousGraphPlanStorage::create(const ExecutionPlan& execution_plan) {
  if (execution_plan.synchronous_buffer_pool_plans().empty() || execution_plan.synchronous_data_edge_plans().empty()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphPlanStorage requires a synchronous ExecutionPlan with buffer pools and data edges");
  }
  try {
    auto implementation = std::make_unique<Impl>();
    implementation->pools.reserve(execution_plan.synchronous_buffer_pool_plans().size());
    implementation->edges.reserve(execution_plan.synchronous_data_edge_plans().size());
    implementation->node_scratch.reserve(execution_plan.synchronous_node_plans().size());
    implementation->executor_storage.buffer_pools.reserve(execution_plan.synchronous_buffer_pool_plans().size());
    implementation->executor_storage.data_edges.reserve(execution_plan.synchronous_data_edge_plans().size());
    implementation->executor_storage.node_scratch.reserve(execution_plan.synchronous_node_plans().size());

    for (const auto& pool : execution_plan.synchronous_buffer_pool_plans()) {
      auto payload_bytes = slot_storage_bytes(pool.slot_count(), pool.payload_capacity_bytes(), "pool payload slab");
      if (!payload_bytes.ok())
        return payload_bytes.status();
      auto metadata_bytes = slot_storage_bytes(pool.slot_count(), pool.metadata_capacity_bytes(), "pool metadata slab");
      if (!metadata_bytes.ok())
        return metadata_bytes.status();
      auto control_bytes = fixed_buffer_pool_required_control_storage_bytes(pool.slot_count());
      if (!control_bytes.ok())
        return control_bytes.status();
      auto payload_alignment = pool_alignment(pool);
      if (!payload_alignment.ok())
        return payload_alignment.status();
      auto payload = AlignedSlab::create(payload_bytes.value(), payload_alignment.value());
      if (!payload.ok())
        return payload.status();
      auto metadata = AlignedSlab::create(metadata_bytes.value(), kSynchronousGraphPlanStorageAlignment);
      if (!metadata.ok())
        return metadata.status();
      auto control = AlignedSlab::create(control_bytes.value(), kSynchronousGraphPlanStorageAlignment);
      if (!control.ok())
        return control.status();
      implementation->pools.push_back({.pool_id = pool.pool_id(),
                                       .payload = std::move(payload).value(),
                                       .metadata = std::move(metadata).value(),
                                       .control = std::move(control).value()});
    }
    for (const auto& edge : execution_plan.synchronous_data_edge_plans()) {
      auto control_bytes = fixed_buffer_edge_required_control_storage_bytes(edge.max_items());
      if (!control_bytes.ok())
        return control_bytes.status();
      auto control = AlignedSlab::create(control_bytes.value(), kSynchronousGraphPlanStorageAlignment);
      if (!control.ok())
        return control.status();
      implementation->edges.push_back({.edge_id = edge.edge_id(), .control = std::move(control).value()});
    }
    for (const auto& node : execution_plan.synchronous_node_plans()) {
      auto scratch_bytes = slot_storage_bytes(1U, node.firing().maximum_scratch_bytes(), "node scratch slab");
      if (!scratch_bytes.ok())
        return scratch_bytes.status();
      auto scratch = AlignedSlab::create(scratch_bytes.value(), kSynchronousGraphScratchMinimumAlignment);
      if (!scratch.ok())
        return scratch.status();
      implementation->node_scratch.push_back({.node_id = node.node_id(), .storage = std::move(scratch).value()});
    }
    for (auto& pool : implementation->pools) {
      implementation->executor_storage.buffer_pools.push_back({.pool_id = pool.pool_id,
                                                               .storage = {.payload = pool.payload.view(),
                                                                           .metadata = pool.metadata.view(),
                                                                           .control = pool.control.view()}});
    }
    for (auto& edge : implementation->edges) {
      implementation->executor_storage.data_edges.push_back(
        {.edge_id = edge.edge_id, .storage = {.control = edge.control.view()}});
    }
    for (auto& scratch : implementation->node_scratch) {
      implementation->executor_storage.node_scratch.push_back(
        {.node_id = scratch.node_id, .storage = scratch.storage.view()});
    }
    return std::unique_ptr<SynchronousGraphPlanStorage>(new SynchronousGraphPlanStorage(std::move(implementation)));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate synchronous graph plan storage");
  }
}

const SynchronousGraphExecutorStorage& SynchronousGraphPlanStorage::executor_storage() const noexcept {
  static const SynchronousGraphExecutorStorage kEmptyStorage{};
  return implementation_ == nullptr ? kEmptyStorage : implementation_->executor_storage;
}

DataItemIdentity make_data_item_identity(const FrameSlotContext& context, const std::uint64_t item_ordinal) noexcept {
  constexpr std::uint64_t kOffset = 1469598103934665603ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash = kOffset;
  const auto mix_u16 = [&hash](const std::uint16_t value) noexcept {
    hash ^= static_cast<std::uint8_t>(value & 0xFFU);
    hash *= kPrime;
    hash ^= static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    hash *= kPrime;
  };
  mix_u16(context.semantic_key.encoding_space);
  mix_u16(context.semantic_key.slice);
  mix_u16(context.semantic_key.contrast);
  mix_u16(context.semantic_key.repetition);
  mix_u16(context.semantic_key.set);
  mix_u16(context.semantic_key.phase);
  mix_u16(context.semantic_key.average);
  mix_u16(context.semantic_key.segment);
  return {.semantic_key_hash = hash, .order_key = context.order_key, .item_ordinal = item_ordinal};
}

} // namespace ksj::recon::runtime
