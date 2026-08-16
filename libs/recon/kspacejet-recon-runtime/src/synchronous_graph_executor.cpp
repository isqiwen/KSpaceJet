#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"

#include "kspacejet/recon/runtime/host_frame_assembler.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {
namespace {

[[nodiscard]] ksj::base::Status fail_graph(const std::shared_ptr<detail::SynchronousGraphExecutorState>& state,
                                           ksj::base::Status cause) noexcept;

} // namespace

namespace detail {

struct FrozenAbiTypeDescriptor final {
  std::string type_ref;
  std::vector<std::string> dimensions;
  std::vector<ksj_utf8_view> dimension_views;
  ksj_type_descriptor_view view{};
};

struct SynchronousGraphInputRuntime final {
  const SynchronousNodeInputBindingPlan* binding{nullptr};
  FixedBufferEdge* data_edge{nullptr};
  std::shared_ptr<const FrozenAbiTypeDescriptor> abi_type;
  FixedBufferEdgeConsumerReservation claim{};
  FixedBufferEdgeConsumerLease data_lease{};
  CalibrationArtifactReadLease calibration_lease{};
};

struct SynchronousGraphOutputRuntime final {
  const SynchronousNodeOutputBindingPlan* binding{nullptr};
  FixedBufferPool* pool{nullptr};
  FixedBufferEdge* data_edge{nullptr};
  std::shared_ptr<const FrozenAbiTypeDescriptor> abi_type;
  MutableBufferLease mutable_buffer{};
  FixedBufferEdgeProducerReservation data_reservation{};
};

struct SynchronousGraphNodeRuntime final {
  const SynchronousNodePlan* plan{nullptr};
  std::unique_ptr<SynchronousFiringLeaseHost> host{};
  std::optional<ResourceVectorLedgerReservation> staging_reservation{};
  // This span is an exact frozen node scratch slab. Its long-lived ledger
  // credit below makes the physical allocation visible for the whole graph,
  // rather than treating a caller-owned persistent buffer as transient work.
  ksj::base::ByteSpan scratch{};
  std::optional<ResourceVectorLedgerReservation> scratch_reservation{};
  std::vector<SynchronousGraphInputRuntime> inputs;
  std::vector<SynchronousGraphOutputRuntime> outputs;
  // These vectors are sized once at create(). `try_fire` only overwrites the
  // fixed entries, so no graph-owned ABI request storage is allocated while a
  // Provider callback is live.
  std::vector<SynchronousInputItem> input_items;
  std::vector<SynchronousInputBatch> input_batches;
  std::vector<SynchronousOutputGrantSpec> output_grants;
  // ABI grant slots are compacted for a callback: ordinary zero-output
  // bindings and all calibration-artifact bindings during on_scan_end receive
  // no grant. This fixed mapping retains the exact plan binding behind each
  // callback-visible slot without allocating during a firing.
  std::vector<Quantity> active_output_binding_indices;
  Quantity active_output_binding_count{0U};
  Quantity completed_input_item_count{0U};
  bool terminal_attempted{false};
  bool terminal_completed{false};
};

struct SynchronousGraphExecutorState final {
  struct Ingress final {
    FixedBufferPool* pool{nullptr};
    FixedBufferEdge* edge{nullptr};
    const TypeDescriptor* type_descriptor{nullptr};
    Quantity max_logical_bytes{0U};
    bool closed{false};
  };

  struct Egress final {
    FixedBufferEdge* edge{nullptr};
  };

  const ExecutionPlan* execution_plan{nullptr};
  const VerificationRecord* verification_record{nullptr};
  std::shared_ptr<ResourceVectorLedger> resource_ledger{};
  mutable std::mutex mutex;
  SynchronousGraphExecutorLifecycle lifecycle{SynchronousGraphExecutorLifecycle::accepting};
  ksj::base::Status last_error{};
  std::map<std::string, std::unique_ptr<FixedBufferPool>> pools;
  std::map<std::string, std::unique_ptr<FixedBufferEdge>> edges;
  std::unique_ptr<CalibrationArtifactStore> calibration_store;
  std::map<std::string, Ingress> ingresses;
  std::map<std::string, Egress> egresses;
  std::map<std::string, SynchronousGraphNodeRuntime> nodes;
};

} // namespace detail

namespace {

[[nodiscard]] bool is_generic_plan(const ExecutionPlan& plan) noexcept {
  return !plan.synchronous_node_plans().empty() && !plan.synchronous_buffer_pool_plans().empty() &&
         !plan.synchronous_data_edge_plans().empty();
}

[[nodiscard]] std::uint8_t hex_value(const char value) noexcept {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<std::uint8_t>(10U + value - 'a');
  return 0xFFU;
}

[[nodiscard]] bool write_digest(const ArtifactDigest& source, ksj_digest256& destination) noexcept {
  constexpr std::size_t kPrefixBytes = 7U;
  constexpr std::size_t kHexBytes = KSJ_PROVIDER_DIGEST256_SIZE * 2U;
  const auto& encoded = source.value();
  if (encoded.size() != kPrefixBytes + kHexBytes || !std::string_view(encoded).starts_with("sha256:"))
    return false;
  destination = {};
  destination.abi = ksj_provider_abi_header_make(sizeof(destination), 0U);
  for (std::size_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    const auto high = hex_value(encoded[kPrefixBytes + index * 2U]);
    const auto low = hex_value(encoded[kPrefixBytes + index * 2U + 1U]);
    if (high == 0xFFU || low == 0xFFU)
      return false;
    destination.bytes[index] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  return true;
}

[[nodiscard]] bool digest_matches_loader_value(const ArtifactDigest& expected,
                                               const ksj::provider::loader::Digest256& actual) noexcept {
  ksj_digest256 encoded{};
  return write_digest(expected, encoded) && std::memcmp(encoded.bytes, actual.data(), KSJ_PROVIDER_DIGEST256_SIZE) == 0;
}

[[nodiscard]] ksj::base::Result<std::uint32_t> to_u32(const Quantity value, const char* const field_name) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    return ksj::base::Status::ValidationError(std::string("SynchronousGraphExecutor ") + field_name +
                                              " cannot be represented by the Provider ABI");
  }
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] ksj::base::Result<std::uint32_t> payload_kind_to_abi(const PayloadKind value) {
  switch (value) {
    case PayloadKind::buffer_handle:
      return KSJ_PAYLOAD_KIND_BUFFER_HANDLE;
    case PayloadKind::message_handle:
      return KSJ_PAYLOAD_KIND_MESSAGE_HANDLE;
    case PayloadKind::control_token:
      return KSJ_PAYLOAD_KIND_CONTROL_TOKEN;
    case PayloadKind::opaque_handle:
      return KSJ_PAYLOAD_KIND_OPAQUE_HANDLE;
  }
  return ksj::base::Status::ValidationError("SynchronousGraphExecutor encountered an unknown payload kind");
}

[[nodiscard]] ksj::base::Result<std::uint32_t> element_type_to_abi(const ElementType value) {
  switch (value) {
    case ElementType::none:
      return KSJ_ELEMENT_TYPE_NONE;
    case ElementType::uint8:
      return KSJ_ELEMENT_TYPE_UINT8;
    case ElementType::int16:
      return KSJ_ELEMENT_TYPE_INT16;
    case ElementType::uint16:
      return KSJ_ELEMENT_TYPE_UINT16;
    case ElementType::int32:
      return KSJ_ELEMENT_TYPE_INT32;
    case ElementType::uint32:
      return KSJ_ELEMENT_TYPE_UINT32;
    case ElementType::float32:
      return KSJ_ELEMENT_TYPE_FLOAT32;
    case ElementType::float64:
      return KSJ_ELEMENT_TYPE_FLOAT64;
    case ElementType::complex_int16:
      return KSJ_ELEMENT_TYPE_COMPLEX_INT16;
    case ElementType::complex_float32:
      return KSJ_ELEMENT_TYPE_COMPLEX_FLOAT32;
    case ElementType::complex_float64:
      return KSJ_ELEMENT_TYPE_COMPLEX_FLOAT64;
  }
  return ksj::base::Status::ValidationError("SynchronousGraphExecutor encountered an unknown element type");
}

[[nodiscard]] ksj::base::Result<std::uint64_t> layout_flags_to_abi(const LayoutKind layout, const StrideKind strides) {
  std::uint64_t flags = 0U;
  switch (layout) {
    case LayoutKind::canonical_contiguous:
      flags = KSJ_TYPE_LAYOUT_CANONICAL_CONTIGUOUS;
      break;
    case LayoutKind::channel_major_contiguous:
      flags = KSJ_TYPE_LAYOUT_CHANNEL_MAJOR_CONTIGUOUS;
      break;
    case LayoutKind::row_major_contiguous:
      flags = KSJ_TYPE_LAYOUT_ROW_MAJOR_CONTIGUOUS;
      break;
    case LayoutKind::column_major_contiguous:
      flags = KSJ_TYPE_LAYOUT_COLUMN_MAJOR_CONTIGUOUS;
      break;
    case LayoutKind::opaque:
      flags = KSJ_TYPE_LAYOUT_OPAQUE;
      break;
  }
  switch (strides) {
    case StrideKind::canonical:
      return flags | KSJ_TYPE_STRIDES_CANONICAL;
    case StrideKind::explicit_byte_strides:
      return flags | KSJ_TYPE_STRIDES_EXPLICIT_BYTE;
  }
  return ksj::base::Status::ValidationError("SynchronousGraphExecutor encountered an unknown stride encoding");
}

[[nodiscard]] ksj::base::Result<std::uint32_t>
memory_domains_to_abi(const std::vector<TypeMemoryDomain>& memory_domains) {
  std::uint32_t result = 0U;
  for (const auto domain : memory_domains) {
    switch (domain) {
      case TypeMemoryDomain::host_normal:
      case TypeMemoryDomain::host_hugepage:
        result |= KSJ_PROVIDER_MEMORY_HOST_PAGEABLE;
        break;
      case TypeMemoryDomain::host_pinned:
        result |= KSJ_PROVIDER_MEMORY_HOST_PINNED;
        break;
      case TypeMemoryDomain::shared_host:
        result |= KSJ_PROVIDER_MEMORY_SHARED;
        break;
      case TypeMemoryDomain::cuda_device:
        result |= KSJ_PROVIDER_MEMORY_DEVICE;
        break;
    }
  }
  if (result == 0U) {
    return ksj::base::Status::ValidationError("SynchronousGraphExecutor TypeDescriptor has no Provider ABI domain");
  }
  return result;
}

[[nodiscard]] ksj::base::Result<std::shared_ptr<const detail::FrozenAbiTypeDescriptor>>
make_frozen_abi_type_descriptor(const TypeDescriptor& source) {
  if (source.rank() > 8U || source.rank() != source.dimensions().size()) {
    return ksj::base::Status::ValidationError("SynchronousGraphExecutor TypeDescriptor rank is not ABI representable");
  }
  auto rank = to_u32(source.rank(), "type rank");
  auto alignment = to_u32(source.min_alignment_bytes(), "type alignment");
  auto payload_kind = payload_kind_to_abi(source.payload_kind());
  auto element_type = element_type_to_abi(source.element_type());
  auto layout_flags = layout_flags_to_abi(source.layout(), source.strides());
  auto memory_domains = memory_domains_to_abi(source.allowed_memory_domains());
  if (!rank.ok())
    return rank.status();
  if (!alignment.ok())
    return alignment.status();
  if (!payload_kind.ok())
    return payload_kind.status();
  if (!element_type.ok())
    return element_type.status();
  if (!layout_flags.ok())
    return layout_flags.status();
  if (!memory_domains.ok())
    return memory_domains.status();
  try {
    auto frozen = std::make_shared<detail::FrozenAbiTypeDescriptor>();
    frozen->type_ref = source.type_ref().value();
    frozen->dimensions = source.dimensions();
    frozen->dimension_views.reserve(frozen->dimensions.size());
    for (const auto& dimension : frozen->dimensions) {
      frozen->dimension_views.push_back({.abi = ksj_provider_abi_header_make(sizeof(ksj_utf8_view), 0U),
                                         .data = dimension.data(),
                                         .size = static_cast<std::uint64_t>(dimension.size())});
    }
    frozen->view.abi = ksj_provider_abi_header_make(sizeof(frozen->view), 0U);
    frozen->view.type_ref = {.abi = ksj_provider_abi_header_make(sizeof(ksj_utf8_view), 0U),
                             .data = frozen->type_ref.data(),
                             .size = static_cast<std::uint64_t>(frozen->type_ref.size())};
    frozen->view.payload_kind = payload_kind.value();
    if (!write_digest(source.type_identity_digest(), frozen->view.type_identity_digest)) {
      return ksj::base::Status::ValidationError(
        "SynchronousGraphExecutor TypeDescriptor digest is not ABI representable");
    }
    frozen->view.element_type = element_type.value();
    frozen->view.rank = rank.value();
    frozen->view.dimension_names = frozen->dimension_views.empty() ? nullptr : frozen->dimension_views.data();
    frozen->view.layout_flags = layout_flags.value();
    if (source.strides() == StrideKind::explicit_byte_strides) {
      if (source.explicit_byte_strides().size() != source.rank()) {
        return ksj::base::Status::ValidationError("SynchronousGraphExecutor explicit strides do not match rank");
      }
      for (std::size_t index = 0U; index < source.explicit_byte_strides().size(); ++index) {
        frozen->view.stride_bytes[index] = source.explicit_byte_strides()[index];
      }
    }
    frozen->view.allowed_memory_domains = memory_domains.value();
    frozen->view.minimum_alignment = alignment.value();
    frozen->view.mutability = source.mutability() == PayloadMutability::immutable_after_publish
                                ? KSJ_PAYLOAD_MUTABILITY_IMMUTABLE_AFTER_PUBLISH
                                : KSJ_PAYLOAD_MUTABILITY_MUTABLE_EXCLUSIVE;
    return std::shared_ptr<const detail::FrozenAbiTypeDescriptor>{std::move(frozen)};
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate frozen synchronous graph ABI TypeDescriptor");
  }
}

[[nodiscard]] ksj::base::Status fail_graph(const std::shared_ptr<detail::SynchronousGraphExecutorState>& state,
                                           ksj::base::Status cause) noexcept {
  if (state == nullptr) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor state is unavailable");
  }
  try {
    std::scoped_lock lock(state->mutex);
    if (state->lifecycle == SynchronousGraphExecutorLifecycle::failed) {
      return state->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor is failed")
                                    : state->last_error;
    }
    state->lifecycle = SynchronousGraphExecutorLifecycle::failed;
    state->last_error = std::move(cause);
    for (auto& [_, edge] : state->edges) {
      if (edge != nullptr) {
        static_cast<void>(edge->abort());
      }
    }
    if (state->calibration_store != nullptr) {
      static_cast<void>(state->calibration_store->abort());
    }
    return state->last_error;
  } catch (...) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor failed while closing the graph");
  }
}

// The executor holds this mutex across reserve → claim → Provider callback →
// commit, so readers cannot observe a half-published multi-output firing.
// Call only while state->mutex is held.
[[nodiscard]] ksj::base::Status fail_graph_locked(const std::shared_ptr<detail::SynchronousGraphExecutorState>& state,
                                                  ksj::base::Status cause) noexcept {
  if (state == nullptr) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor state is unavailable");
  }
  try {
    if (state->lifecycle == SynchronousGraphExecutorLifecycle::failed) {
      return state->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor is failed")
                                    : state->last_error;
    }
    state->lifecycle = SynchronousGraphExecutorLifecycle::failed;
    state->last_error = std::move(cause);
    for (auto& [_, edge] : state->edges) {
      if (edge != nullptr) {
        static_cast<void>(edge->abort());
      }
    }
    if (state->calibration_store != nullptr) {
      static_cast<void>(state->calibration_store->abort());
    }
    return state->last_error;
  } catch (...) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor failed while closing the graph");
  }
}

[[nodiscard]] ksj::base::Status validate_storage_ids(const std::vector<SynchronousGraphBufferPoolStorage>& supplied,
                                                     const std::vector<SynchronousBufferPoolPlan>& planned,
                                                     std::map<std::string, FixedBufferPoolStorage>& result) {
  if (supplied.size() != planned.size()) {
    return ksj::base::Status::ValidationError("SynchronousGraphExecutor requires exactly one caller slab set per pool");
  }
  for (const auto& item : supplied) {
    if (item.pool_id.empty() || !result.emplace(item.pool_id, item.storage).second) {
      return ksj::base::Status::ValidationError("SynchronousGraphExecutor buffer-pool storage ids must be unique");
    }
  }
  for (const auto& item : planned) {
    if (!result.contains(item.pool_id())) {
      return ksj::base::Status::ValidationError("SynchronousGraphExecutor is missing a frozen buffer-pool slab set");
    }
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status validate_storage_ids(const std::vector<SynchronousGraphDataEdgeStorage>& supplied,
                                                     const std::vector<SynchronousDataEdgePlan>& planned,
                                                     std::map<std::string, FixedBufferEdgeStorage>& result) {
  if (supplied.size() != planned.size()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor requires exactly one caller control slab per edge");
  }
  for (const auto& item : supplied) {
    if (item.edge_id.empty() || !result.emplace(item.edge_id, item.storage).second) {
      return ksj::base::Status::ValidationError("SynchronousGraphExecutor data-edge storage ids must be unique");
    }
  }
  for (const auto& item : planned) {
    if (!result.contains(item.edge_id())) {
      return ksj::base::Status::ValidationError("SynchronousGraphExecutor is missing a frozen data-edge control slab");
    }
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status validate_storage_ids(const std::vector<SynchronousGraphNodeScratchStorage>& supplied,
                                                     const std::vector<SynchronousNodePlan>& planned,
                                                     std::map<std::string, ksj::base::ByteSpan>& result) {
  if (supplied.size() != planned.size()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor requires exactly one caller scratch slab per frozen node");
  }
  for (const auto& item : supplied) {
    if (item.node_id.empty() || !result.emplace(item.node_id, item.storage).second) {
      return ksj::base::Status::ValidationError("SynchronousGraphExecutor node scratch storage ids must be unique");
    }
  }
  for (const auto& node : planned) {
    const auto found = result.find(node.node_id());
    if (found == result.end()) {
      return ksj::base::Status::ValidationError("SynchronousGraphExecutor is missing a frozen node scratch slab");
    }
    const auto bytes = node.firing().maximum_scratch_bytes();
    if (bytes > std::numeric_limits<std::size_t>::max() || found->second.size() != static_cast<std::size_t>(bytes) ||
        (bytes != 0U &&
         (found->second.data() == nullptr ||
          reinterpret_cast<std::uintptr_t>(found->second.data()) % kSynchronousGraphScratchMinimumAlignment != 0U))) {
      return ksj::base::Status::ValidationError(
        "SynchronousGraphExecutor node scratch slab must exactly match its frozen byte bound and minimum alignment");
    }
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] const OperatorPlanBinding* find_operator_plan_binding(const ExecutionPlan& plan,
                                                                    const std::string_view node_id) noexcept {
  const auto found = std::find_if(plan.operator_plan_bindings().begin(), plan.operator_plan_bindings().end(),
                                  [node_id](const OperatorPlanBinding& candidate) {
                                    return candidate.node_id() == node_id;
                                  });
  return found == plan.operator_plan_bindings().end() ? nullptr : &*found;
}

[[nodiscard]] ksj::base::Status validate_provider_invocation(const ExecutionPlan& plan, const SynchronousNodePlan& node,
                                                             const SynchronousProviderInvocation& invocation) {
  if (!invocation.provider.valid() || invocation.provider.api() == nullptr ||
      invocation.provider.descriptor() == nullptr || invocation.operator_handle == nullptr ||
      invocation.execution_context == nullptr || invocation.key_state == nullptr ||
      invocation.node_id != node.node_id() || invocation.operator_id != node.operator_id() ||
      !invocation.canonical_config_digest.has_value()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor requires a live Provider invocation bound to the frozen node identity");
  }
  const auto* binding = find_operator_plan_binding(plan, node.node_id());
  if (binding == nullptr || *invocation.canonical_config_digest != binding->canonical_config_digest()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor Provider invocation config digest does not match the frozen node binding");
  }
  const auto* descriptor = invocation.provider.descriptor();
  if (descriptor->provider_id != node.provider_id() ||
      !digest_matches_loader_value(node.provider_bundle_digest(), descriptor->bundle_digest)) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor Provider identity does not match the frozen node binding");
  }
  const auto found = std::find_if(descriptor->operators.begin(), descriptor->operators.end(),
                                  [&node](const ksj::provider::loader::OperatorDescriptor& candidate) {
                                    return candidate.operator_id == node.operator_id();
                                  });
  if (found == descriptor->operators.end()) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor Provider does not expose the frozen Operator identity");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] bool identities_match(const DataItemIdentity& left, const DataItemIdentity& right) noexcept {
  return left.semantic_key_hash == right.semantic_key_hash && left.order_key == right.order_key &&
         left.item_ordinal == right.item_ordinal;
}

[[nodiscard]] ksj::base::Result<ResourceVector> make_node_staging_reservation(const SynchronousNodePlan& node) {
  return ResourceVector::create({.host_normal_bytes = node.firing().staging_charged_bytes(),
                                 .descriptor_count = node.firing().staging_descriptor_count()},
                                "SynchronousGraphExecutor frozen node ABI staging");
}

[[nodiscard]] ksj::base::Result<ResourceVector> make_node_scratch_reservation(const SynchronousNodePlan& node) {
  return ResourceVector::create({.host_normal_bytes = node.firing().maximum_scratch_bytes()},
                                "SynchronousGraphExecutor frozen node scratch");
}

} // namespace

IngressOutputLease::IngressOutputLease(std::shared_ptr<detail::SynchronousGraphExecutorState> state,
                                       std::string ingress_id, const TypeDescriptor* type_descriptor,
                                       MutableBufferLease mutable_buffer,
                                       FixedBufferEdgeProducerReservation edge_reservation) noexcept
    : state_(std::move(state)), ingress_id_(std::move(ingress_id)), type_descriptor_(type_descriptor),
      mutable_buffer_(std::move(mutable_buffer)), edge_reservation_(std::move(edge_reservation)) {}

IngressOutputLease::~IngressOutputLease() {
  abort();
}

IngressOutputLease::IngressOutputLease(IngressOutputLease&& other) noexcept
    : state_(std::move(other.state_)), ingress_id_(std::move(other.ingress_id_)),
      type_descriptor_(std::exchange(other.type_descriptor_, nullptr)),
      mutable_buffer_(std::move(other.mutable_buffer_)), edge_reservation_(std::move(other.edge_reservation_)) {}

IngressOutputLease& IngressOutputLease::operator=(IngressOutputLease&& other) noexcept {
  if (this != &other) {
    abort();
    state_ = std::move(other.state_);
    ingress_id_ = std::move(other.ingress_id_);
    type_descriptor_ = std::exchange(other.type_descriptor_, nullptr);
    mutable_buffer_ = std::move(other.mutable_buffer_);
    edge_reservation_ = std::move(other.edge_reservation_);
  }
  return *this;
}

bool IngressOutputLease::valid() const noexcept {
  return state_ != nullptr && type_descriptor_ != nullptr && mutable_buffer_.valid() && edge_reservation_.valid();
}

ksj::base::Result<ksj::base::ByteSpan> IngressOutputLease::writable_payload() {
  if (!valid()) {
    return ksj::base::Status::StateError("IngressOutputLease is invalid or already settled");
  }
  return mutable_buffer_.writable_payload();
}

ksj::base::Result<ksj::base::ByteSpan> IngressOutputLease::writable_metadata() {
  if (!valid()) {
    return ksj::base::Status::StateError("IngressOutputLease is invalid or already settled");
  }
  return mutable_buffer_.writable_metadata();
}

ksj::base::Status IngressOutputLease::seal_and_commit(const Quantity payload_bytes,
                                                      const ksj::base::ConstByteSpan metadata,
                                                      const DataItemIdentity identity) {
  if (!valid()) {
    return ksj::base::Status::StateError("IngressOutputLease is invalid or already settled");
  }
  auto sealed = mutable_buffer_.seal(*type_descriptor_, payload_bytes, metadata);
  if (!sealed.ok()) {
    // MutableBufferLease::seal preserves the writable capability on failure.
    return sealed.status();
  }
  auto handle = std::move(sealed).value();
  const auto committed = edge_reservation_.commit_from_with_identity(handle, identity);
  if (!committed.ok()) {
    const auto failure = fail_graph(state_, committed);
    disarm();
    return failure;
  }
  disarm();
  return ksj::base::Status::Ok();
}

ksj::base::Status IngressOutputLease::seal_and_commit(const Quantity payload_bytes, const Quantity metadata_bytes,
                                                      const DataItemIdentity identity) {
  if (!valid()) {
    return ksj::base::Status::StateError("IngressOutputLease is invalid or already settled");
  }
  auto metadata = mutable_buffer_.writable_metadata();
  if (!metadata.ok()) {
    return metadata.status();
  }
  if (metadata_bytes > metadata.value().size()) {
    return ksj::base::Status::ValidationError("IngressOutputLease metadata bytes exceed its frozen pool capacity");
  }
  return seal_and_commit(payload_bytes, {metadata.value().data(), static_cast<std::size_t>(metadata_bytes)}, identity);
}

void IngressOutputLease::abort() noexcept {
  disarm();
}

void IngressOutputLease::disarm() noexcept {
  edge_reservation_ = FixedBufferEdgeProducerReservation{};
  mutable_buffer_ = MutableBufferLease{};
  type_descriptor_ = nullptr;
  ingress_id_.clear();
  state_.reset();
}

EgressInputLease::EgressInputLease(FixedBufferEdgeConsumerLease edge_lease) noexcept
    : edge_lease_(std::move(edge_lease)) {}

EgressInputLease::~EgressInputLease() = default;
EgressInputLease::EgressInputLease(EgressInputLease&& other) noexcept = default;
EgressInputLease& EgressInputLease::operator=(EgressInputLease&& other) noexcept = default;

bool EgressInputLease::valid() const noexcept {
  return edge_lease_.valid();
}

ksj::base::Result<ksj::base::ConstByteSpan> EgressInputLease::payload() const {
  if (!valid())
    return ksj::base::Status::StateError("EgressInputLease is invalid or already acknowledged");
  return edge_lease_.buffer().payload();
}

ksj::base::Result<ksj::base::ConstByteSpan> EgressInputLease::metadata() const {
  if (!valid())
    return ksj::base::Status::StateError("EgressInputLease is invalid or already acknowledged");
  return edge_lease_.buffer().metadata();
}

const TypeDescriptor* EgressInputLease::type_descriptor() const noexcept {
  return valid() ? edge_lease_.buffer().type_descriptor() : nullptr;
}

const DataItemIdentity& EgressInputLease::item_identity() const noexcept {
  return edge_lease_.item_identity();
}

ksj::base::Status EgressInputLease::acknowledge_consumed() {
  if (!valid())
    return ksj::base::Status::StateError("EgressInputLease is invalid or already acknowledged");
  return edge_lease_.acknowledge_consumed();
}

CompletedFrameIngressBridge::CompletedFrameIngressBridge(SynchronousGraphExecutor* executor, std::string ingress_id,
                                                         HostFrameAssembler* source_assembler) noexcept
    : executor_(executor), ingress_id_(std::move(ingress_id)), source_assembler_(source_assembler) {}

ksj::base::Result<CompletedFrameIngressBridge>
CompletedFrameIngressBridge::create(SynchronousGraphExecutor& executor, const std::string_view ingress_id,
                                    HostFrameAssembler& source_assembler) {
  if (!executor.has_ingress(ingress_id)) {
    return ksj::base::Status::ValidationError("CompletedFrameIngressBridge names no frozen graph ingress");
  }
  return CompletedFrameIngressBridge{&executor, std::string(ingress_id), &source_assembler};
}

CompletedFrameIngressBridge::CompletedFrameIngressBridge(CompletedFrameIngressBridge&& other) noexcept
    : executor_(std::exchange(other.executor_, nullptr)), ingress_id_(std::move(other.ingress_id_)),
      source_assembler_(std::exchange(other.source_assembler_, nullptr)) {}

CompletedFrameIngressBridge& CompletedFrameIngressBridge::operator=(CompletedFrameIngressBridge&& other) noexcept {
  if (this != &other) {
    executor_ = std::exchange(other.executor_, nullptr);
    ingress_id_ = std::move(other.ingress_id_);
    source_assembler_ = std::exchange(other.source_assembler_, nullptr);
  }
  return *this;
}

ksj::base::Status CompletedFrameIngressBridge::publish(CompletedFrameLease&& completed_frame,
                                                       const DataItemIdentity identity) {
  if (executor_ == nullptr || source_assembler_ == nullptr || !source_assembler_->owns(completed_frame)) {
    return ksj::base::Status::ValidationError("CompletedFrameIngressBridge received a foreign or invalid frame lease");
  }
  const auto dispatch = completed_frame.begin_dispatch();
  if (!dispatch.ok()) {
    return dispatch;
  }
  auto bytes = completed_frame.bytes();
  if (!bytes.ok()) {
    static_cast<void>(completed_frame.abandon());
    static_cast<void>(executor_->abort());
    return bytes.status();
  }
  auto output = executor_->try_acquire_ingress(ingress_id_);
  if (!output.ok()) {
    static_cast<void>(completed_frame.abandon());
    static_cast<void>(executor_->abort());
    return output.status();
  }
  auto payload = output.value().writable_payload();
  if (!payload.ok() || payload.value().size() < bytes.value().size()) {
    static_cast<void>(completed_frame.abandon());
    static_cast<void>(executor_->abort());
    return payload.ok() ? ksj::base::Status::ValidationError("completed frame exceeds frozen ingress payload capacity")
                        : payload.status();
  }
  if (!bytes.value().empty()) {
    std::memcpy(payload.value().data(), bytes.value().data(), bytes.value().size());
  }
  const auto committed = output.value().seal_and_commit(static_cast<Quantity>(bytes.value().size()), {}, identity);
  if (!committed.ok()) {
    static_cast<void>(completed_frame.abandon());
    static_cast<void>(executor_->abort());
    return committed;
  }
  const auto acknowledged = completed_frame.acknowledge_consumed();
  if (!acknowledged.ok()) {
    static_cast<void>(executor_->abort());
  }
  return acknowledged;
}

ksj::base::Status CompletedFrameIngressBridge::end_of_input() {
  if (executor_ == nullptr || source_assembler_ == nullptr) {
    return ksj::base::Status::StateError("CompletedFrameIngressBridge is moved from");
  }
  const auto source_end = source_assembler_->end_of_input();
  if (!source_end.ok())
    return source_end;
  return executor_->end_ingress(ingress_id_);
}

ksj::base::Status CompletedFrameIngressBridge::abort() {
  if (executor_ == nullptr || source_assembler_ == nullptr) {
    return ksj::base::Status::StateError("CompletedFrameIngressBridge is moved from");
  }
  static_cast<void>(source_assembler_->abort());
  return executor_->abort();
}

ksj::base::Result<std::unique_ptr<SynchronousGraphExecutor>>
SynchronousGraphExecutor::create(const ExecutionPlan& execution_plan, const VerificationRecord& verification_record,
                                 SynchronousGraphExecutorStorage storage,
                                 std::shared_ptr<ResourceVectorLedger> resource_ledger) {
  if (!is_generic_plan(execution_plan)) {
    return ksj::base::Status::ValidationError("SynchronousGraphExecutor requires a generic synchronous ExecutionPlan");
  }
  if (verification_record.execution_plan_digest() != execution_plan.digest() ||
      verification_record.execution_profile() != execution_plan.execution_profile()) {
    return ksj::base::Status::ValidationError("VerificationRecord does not exactly bind the supplied ExecutionPlan");
  }
  if (resource_ledger == nullptr || !resource_ledger->capacity().can_admit(execution_plan.resources())) {
    return ksj::base::Status::ValidationError(
      "ResourceVectorLedger cannot admit the frozen synchronous graph resources");
  }
  std::map<std::string, FixedBufferPoolStorage> pool_storage;
  std::map<std::string, FixedBufferEdgeStorage> edge_storage;
  std::map<std::string, ksj::base::ByteSpan> scratch_storage;
  auto storage_status =
    validate_storage_ids(storage.buffer_pools, execution_plan.synchronous_buffer_pool_plans(), pool_storage);
  if (!storage_status.ok())
    return storage_status;
  storage_status = validate_storage_ids(storage.data_edges, execution_plan.synchronous_data_edge_plans(), edge_storage);
  if (!storage_status.ok())
    return storage_status;
  storage_status = validate_storage_ids(storage.node_scratch, execution_plan.synchronous_node_plans(), scratch_storage);
  if (!storage_status.ok())
    return storage_status;

  try {
    auto state = std::make_shared<detail::SynchronousGraphExecutorState>();
    state->execution_plan = &execution_plan;
    state->verification_record = &verification_record;
    state->resource_ledger = std::move(resource_ledger);
    for (const auto& pool_plan : execution_plan.synchronous_buffer_pool_plans()) {
      auto pool = FixedBufferPool::create({.occupancy_ledger = state->resource_ledger,
                                           .type_descriptor = pool_plan.type_descriptor(),
                                           .slot_count = pool_plan.slot_count(),
                                           .payload_capacity_bytes = pool_plan.payload_capacity_bytes(),
                                           .metadata_capacity_bytes = pool_plan.metadata_capacity_bytes()},
                                          pool_storage.at(pool_plan.pool_id()));
      if (!pool.ok())
        return pool.status();
      state->pools.emplace(pool_plan.pool_id(), std::move(pool).value());
    }
    for (const auto& edge_plan : execution_plan.synchronous_data_edge_plans()) {
      const auto pool = state->pools.find(edge_plan.source_pool_id());
      if (pool == state->pools.end()) {
        return ksj::base::Status::ValidationError("Synchronous data edge names an unknown source pool");
      }
      auto edge = FixedBufferEdge::create({.occupancy_ledger = state->resource_ledger,
                                           .source_pool = pool->second.get(),
                                           .max_items = edge_plan.max_items(),
                                           .max_logical_bytes = edge_plan.max_logical_bytes(),
                                           .charged_control_storage_bytes = edge_plan.host_metadata_charged_bytes(),
                                           .charged_descriptor_count = edge_plan.descriptor_charged_count()},
                                          edge_storage.at(edge_plan.edge_id()));
      if (!edge.ok())
        return edge.status();
      state->edges.emplace(edge_plan.edge_id(), std::move(edge).value());
    }
    // A graph with no static calibration route owns no empty store. The store
    // intentionally rejects an empty configuration, while ordinary data-only
    // graphs remain valid and never need a calibration capability.
    if (!execution_plan.calibration_artifact_binding_plans().empty()) {
      CalibrationArtifactStoreConfig artifact_config;
      artifact_config.bindings.reserve(execution_plan.calibration_artifact_binding_plans().size());
      for (const auto& binding : execution_plan.calibration_artifact_binding_plans()) {
        const auto pool = state->pools.find(binding.producer_pool_id());
        if (pool == state->pools.end() || pool->second->pool_identity() == 0U) {
          return ksj::base::Status::ValidationError("Calibration artifact binding names an unknown producer pool");
        }
        artifact_config.bindings.push_back({.binding_id = binding.binding_id(),
                                            .source_pool_identity = pool->second->pool_identity(),
                                            .type_descriptor = binding.type_descriptor()});
      }
      auto artifact_store = CalibrationArtifactStore::create(std::move(artifact_config));
      if (!artifact_store.ok())
        return artifact_store.status();
      state->calibration_store = std::move(artifact_store).value();
    }
    for (const auto& edge_plan : execution_plan.synchronous_data_edge_plans()) {
      const auto edge = state->edges.find(edge_plan.edge_id());
      if (edge == state->edges.end())
        return ksj::base::Status::InternalError("synchronous edge creation was lost");
      if (edge_plan.producer_kind() == SynchronousDataEndpointKind::ingress) {
        const auto pool = state->pools.find(edge_plan.source_pool_id());
        if (pool == state->pools.end() || !state->ingresses
                                             .emplace(edge_plan.producer_id(),
                                                      detail::SynchronousGraphExecutorState::Ingress{
                                                        .pool = pool->second.get(),
                                                        .edge = edge->second.get(),
                                                        .type_descriptor = pool->second->type_descriptor(),
                                                        .max_logical_bytes = edge_plan.max_logical_bytes()})
                                             .second) {
          return ksj::base::Status::ValidationError("Synchronous ingress ids must have one frozen edge each");
        }
      }
      if (edge_plan.consumer_kind() == SynchronousDataEndpointKind::egress &&
          !state->egresses
             .emplace(edge_plan.consumer_id(), detail::SynchronousGraphExecutorState::Egress{edge->second.get()})
             .second) {
        return ksj::base::Status::ValidationError("Synchronous egress ids must have one frozen edge each");
      }
    }
    for (const auto& node_plan : execution_plan.synchronous_node_plans()) {
      detail::SynchronousGraphNodeRuntime node;
      node.plan = &node_plan;
      node.scratch = scratch_storage.at(node_plan.node_id());
      if (node_plan.firing().maximum_scratch_bytes() != 0U) {
        auto scratch_vector = make_node_scratch_reservation(node_plan);
        if (!scratch_vector.ok())
          return scratch_vector.status();
        auto scratch_reservation = state->resource_ledger->try_reserve(scratch_vector.value());
        if (!scratch_reservation.ok())
          return scratch_reservation.status();
        node.scratch_reservation.emplace(std::move(scratch_reservation).value());
        const auto scratch_committed = node.scratch_reservation->commit();
        if (!scratch_committed.ok())
          return scratch_committed;
      }
      auto staging_vector = make_node_staging_reservation(node_plan);
      if (!staging_vector.ok())
        return staging_vector.status();
      auto staging_reservation = state->resource_ledger->try_reserve(staging_vector.value());
      if (!staging_reservation.ok())
        return staging_reservation.status();
      node.staging_reservation.emplace(std::move(staging_reservation).value());
      const auto staging_committed = node.staging_reservation->commit();
      if (!staging_committed.ok())
        return staging_committed;

      auto maximum_input_batches = to_u32(node_plan.firing().maximum_input_batches(), "maximum_input_batches");
      auto maximum_input_items = to_u32(node_plan.firing().maximum_input_items(), "maximum_input_items");
      auto maximum_output_grants = to_u32(node_plan.firing().maximum_output_grants(), "maximum_output_grants");
      if (!maximum_input_batches.ok())
        return maximum_input_batches.status();
      if (!maximum_input_items.ok())
        return maximum_input_items.status();
      if (!maximum_output_grants.ok())
        return maximum_output_grants.status();
      SynchronousFiringLeaseConfig host_config{state->resource_ledger, node_plan.firing().firing_reservation()};
      host_config.maximum_input_batches = maximum_input_batches.value();
      host_config.maximum_input_items = maximum_input_items.value();
      host_config.maximum_output_grants = maximum_output_grants.value();
      host_config.maximum_input_payload_bytes = node_plan.firing().maximum_input_payload_bytes();
      host_config.maximum_scratch_bytes = node_plan.firing().maximum_scratch_bytes();
      host_config.maximum_metadata_bytes = node_plan.firing().maximum_metadata_bytes();
      host_config.frozen_staging_charged_bytes = node_plan.firing().staging_charged_bytes();
      host_config.frozen_staging_descriptor_count = node_plan.firing().staging_descriptor_count();
      auto host = SynchronousFiringLeaseHost::create_preaccounted_staging(std::move(host_config));
      if (!host.ok())
        return host.status();
      node.host = std::make_unique<SynchronousFiringLeaseHost>(std::move(host).value());

      node.inputs.reserve(node_plan.inputs().size());
      node.outputs.reserve(node_plan.outputs().size());
      node.input_items.resize(node_plan.inputs().size());
      node.input_batches.resize(node_plan.inputs().size());
      node.output_grants.resize(node_plan.outputs().size());
      node.active_output_binding_indices.resize(node_plan.outputs().size());
      for (const auto& input_binding : node_plan.inputs()) {
        auto abi_type = make_frozen_abi_type_descriptor(input_binding.type_descriptor());
        if (!abi_type.ok())
          return abi_type.status();
        detail::SynchronousGraphInputRuntime input;
        input.binding = &input_binding;
        input.abi_type = std::move(abi_type).value();
        if (input_binding.source_kind() == SynchronousInputSourceKind::data_edge) {
          const auto edge = state->edges.find(input_binding.source_id());
          if (edge == state->edges.end()) {
            return ksj::base::Status::ValidationError("Synchronous node input names an unknown data edge");
          }
          input.data_edge = edge->second.get();
        } else {
          const auto artifact = std::find_if(
            execution_plan.calibration_artifact_binding_plans().begin(),
            execution_plan.calibration_artifact_binding_plans().end(), [&input_binding](const auto& candidate) {
              return candidate.binding_id() == input_binding.source_id() &&
                     candidate.type_descriptor().exactly_matches(input_binding.type_descriptor());
            });
          if (artifact == execution_plan.calibration_artifact_binding_plans().end()) {
            return ksj::base::Status::ValidationError(
              "Synchronous node calibration input does not name an exact frozen artifact binding");
          }
        }
        node.inputs.push_back(std::move(input));
      }
      for (const auto& output_binding : node_plan.outputs()) {
        const auto pool = state->pools.find(output_binding.pool_id());
        if (pool == state->pools.end() || pool->second->type_descriptor() == nullptr ||
            !pool->second->type_descriptor()->exactly_matches(output_binding.type_descriptor())) {
          return ksj::base::Status::ValidationError(
            "Synchronous node output does not name an exact frozen source pool");
        }
        auto abi_type = make_frozen_abi_type_descriptor(output_binding.type_descriptor());
        if (!abi_type.ok())
          return abi_type.status();
        detail::SynchronousGraphOutputRuntime output;
        output.binding = &output_binding;
        output.pool = pool->second.get();
        output.abi_type = std::move(abi_type).value();
        if (output_binding.destination_kind() == SynchronousOutputDestinationKind::data_edge) {
          const auto edge = state->edges.find(output_binding.destination_id());
          if (edge == state->edges.end()) {
            return ksj::base::Status::ValidationError("Synchronous node output names an unknown data edge");
          }
          output.data_edge = edge->second.get();
        } else {
          const auto artifact =
            std::find_if(execution_plan.calibration_artifact_binding_plans().begin(),
                         execution_plan.calibration_artifact_binding_plans().end(),
                         [&output_binding, &node_plan](const auto& candidate) {
                           return candidate.binding_id() == output_binding.destination_id() &&
                                  candidate.producer_node_id() == node_plan.node_id() &&
                                  candidate.producer_port_name() == output_binding.port_name() &&
                                  candidate.producer_pool_id() == output_binding.pool_id() &&
                                  candidate.type_descriptor().exactly_matches(output_binding.type_descriptor());
                         });
          if (artifact == execution_plan.calibration_artifact_binding_plans().end()) {
            return ksj::base::Status::ValidationError(
              "Synchronous node artifact output does not name its exact frozen store binding");
          }
        }
        node.outputs.push_back(std::move(output));
      }
      if (!state->nodes.emplace(node_plan.node_id(), std::move(node)).second) {
        return ksj::base::Status::ValidationError("Synchronous graph node ids must be unique");
      }
    }
    return std::unique_ptr<SynchronousGraphExecutor>(new SynchronousGraphExecutor(std::move(state)));
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate SynchronousGraphExecutor state");
  }
}

SynchronousGraphExecutor::SynchronousGraphExecutor(
  std::shared_ptr<detail::SynchronousGraphExecutorState> state) noexcept
    : state_(std::move(state)) {}

SynchronousGraphExecutor::~SynchronousGraphExecutor() {
  static_cast<void>(abort());
}

bool SynchronousGraphExecutor::has_ingress(const std::string_view ingress_id) const noexcept {
  try {
    return state_ != nullptr && state_->ingresses.contains(std::string(ingress_id));
  } catch (...) {
    return false;
  }
}

ksj::base::Result<IngressOutputLease> SynchronousGraphExecutor::try_acquire_ingress(const std::string_view ingress_id) {
  if (state_ == nullptr)
    return ksj::base::Status::StateError("SynchronousGraphExecutor is invalid");
  std::scoped_lock lock(state_->mutex);
  if (state_->lifecycle != SynchronousGraphExecutorLifecycle::accepting) {
    return state_->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor no longer accepts ingress")
                                   : state_->last_error;
  }
  const auto ingress = state_->ingresses.find(std::string(ingress_id));
  if (ingress == state_->ingresses.end() || ingress->second.closed || ingress->second.pool == nullptr ||
      ingress->second.edge == nullptr || ingress->second.type_descriptor == nullptr) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor ingress is unknown or closed");
  }
  auto edge_reservation = ingress->second.edge->try_reserve(ingress->second.max_logical_bytes);
  if (!edge_reservation.ok())
    return edge_reservation.status();
  auto mutable_buffer = ingress->second.pool->try_acquire();
  if (!mutable_buffer.ok())
    return mutable_buffer.status();
  return IngressOutputLease{state_, std::string(ingress_id), ingress->second.type_descriptor,
                            std::move(mutable_buffer).value(), std::move(edge_reservation).value()};
}

ksj::base::Status SynchronousGraphExecutor::end_ingress(const std::string_view ingress_id) {
  if (state_ == nullptr)
    return ksj::base::Status::StateError("SynchronousGraphExecutor is invalid");
  std::scoped_lock lock(state_->mutex);
  if (state_->lifecycle != SynchronousGraphExecutorLifecycle::accepting) {
    return state_->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor is terminal")
                                   : state_->last_error;
  }
  const auto ingress = state_->ingresses.find(std::string(ingress_id));
  if (ingress == state_->ingresses.end() || ingress->second.closed || ingress->second.edge == nullptr) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor ingress is unknown or already closed");
  }
  const auto closed = ingress->second.edge->end_of_input();
  if (closed.ok())
    ingress->second.closed = true;
  return closed;
}

namespace {

using GraphState = detail::SynchronousGraphExecutorState;
using NodeRuntime = detail::SynchronousGraphNodeRuntime;

void clear_output_reservations(NodeRuntime& node) noexcept {
  for (auto& output : node.outputs) {
    output.data_reservation = FixedBufferEdgeProducerReservation{};
    output.mutable_buffer = MutableBufferLease{};
  }
  node.active_output_binding_count = 0U;
}

void rollback_input_claims(NodeRuntime& node) noexcept {
  for (auto& input : node.inputs) {
    if (input.claim.valid()) {
      static_cast<void>(input.claim.rollback());
    }
    input.claim = FixedBufferEdgeConsumerReservation{};
  }
}

void release_static_input_leases(NodeRuntime& node) noexcept {
  for (auto& input : node.inputs) {
    input.calibration_lease.release();
    input.calibration_lease = CalibrationArtifactReadLease{};
  }
}

void clear_all_node_transient(NodeRuntime& node) noexcept {
  rollback_input_claims(node);
  release_static_input_leases(node);
  for (auto& input : node.inputs) {
    input.data_lease = FixedBufferEdgeConsumerLease{};
  }
  clear_output_reservations(node);
}

[[nodiscard]] bool terminal_requires_output_grants(const NodeRuntime& node) noexcept {
  return node.plan != nullptr && node.plan->terminal().normal_max_output_items() != 0U;
}

[[nodiscard]] Quantity terminal_data_output_count(const NodeRuntime& node) noexcept {
  Quantity count{0U};
  for (const auto& output : node.outputs) {
    if (output.binding != nullptr &&
        output.binding->destination_kind() == SynchronousOutputDestinationKind::data_edge &&
        output.binding->maximum_item_count() != 0U) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] Quantity required_normal_output_count(const NodeRuntime& node) noexcept {
  Quantity count{0U};
  for (const auto& output : node.outputs) {
    if (output.binding != nullptr && output.binding->maximum_item_count() != 0U) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] ksj::base::Status reserve_all_outputs(GraphState& state, NodeRuntime& node, const bool terminal = false) {
  // A zero terminal-output bound is an explicit plan permission for
  // on_scan_end to have no grants. Do not reserve ordinary output slots and
  // accidentally require a Provider terminal-output capability in that case.
  if (terminal && !terminal_requires_output_grants(node)) {
    return ksj::base::Status::Ok();
  }
  if (terminal && node.plan != nullptr &&
      node.plan->terminal().normal_max_output_items() > terminal_data_output_count(node)) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor terminal output bound exceeds its data-edge output bindings");
  }
  for (auto& output : node.outputs) {
    if (output.binding == nullptr || output.pool == nullptr || !output.abi_type) {
      clear_output_reservations(node);
      return ksj::base::Status::StateError("SynchronousGraphExecutor node output runtime is incomplete");
    }
    if (output.binding->maximum_item_count() == 0U ||
        (terminal && output.binding->destination_kind() == SynchronousOutputDestinationKind::calibration_artifact)) {
      continue;
    }
    if (output.binding->destination_kind() == SynchronousOutputDestinationKind::calibration_artifact) {
      if (state.calibration_store == nullptr) {
        clear_output_reservations(node);
        return ksj::base::Status::StateError("SynchronousGraphExecutor calibration store is unavailable");
      }
      auto existing = state.calibration_store->try_acquire(output.binding->destination_id());
      if (existing.ok()) {
        existing.value().release();
        clear_output_reservations(node);
        return ksj::base::Status::AlreadyExists(
          "SynchronousGraphExecutor cannot overwrite an already-published calibration artifact");
      }
      if (existing.status().code() != ksj::base::StatusCode::unavailable) {
        clear_output_reservations(node);
        return existing.status();
      }
    } else {
      if (output.data_edge == nullptr) {
        clear_output_reservations(node);
        return ksj::base::Status::StateError("SynchronousGraphExecutor data output has no frozen edge");
      }
      const auto edge_snapshot = output.data_edge->snapshot();
      auto reservation = output.data_edge->try_reserve(edge_snapshot.max_logical_bytes);
      if (!reservation.ok()) {
        clear_output_reservations(node);
        return reservation.status();
      }
      output.data_reservation = std::move(reservation).value();
    }
    auto mutable_buffer = output.pool->try_acquire();
    if (!mutable_buffer.ok()) {
      clear_output_reservations(node);
      return mutable_buffer.status();
    }
    output.mutable_buffer = std::move(mutable_buffer).value();
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status claim_dynamic_inputs(NodeRuntime& node, DataItemIdentity& identity,
                                                     Quantity& dynamic_input_count) {
  bool have_identity = false;
  bool saw_completed = false;
  bool saw_item = false;
  dynamic_input_count = 0U;
  for (auto& input : node.inputs) {
    if (input.binding == nullptr) {
      rollback_input_claims(node);
      return ksj::base::Status::StateError("SynchronousGraphExecutor node input runtime is incomplete");
    }
    if (input.binding->source_kind() != SynchronousInputSourceKind::data_edge)
      continue;
    ++dynamic_input_count;
    if (input.data_edge == nullptr) {
      rollback_input_claims(node);
      return ksj::base::Status::StateError("SynchronousGraphExecutor dynamic input has no frozen edge");
    }
    auto polled = input.data_edge->try_reserve_consumer();
    if (polled.kind == FixedBufferEdgePollKind::empty) {
      rollback_input_claims(node);
      return ksj::base::Status::Unavailable("SynchronousGraphExecutor dynamic input is not ready");
    }
    if (polled.kind == FixedBufferEdgePollKind::failed ||
        !polled.reservation.has_value() && polled.kind != FixedBufferEdgePollKind::completed) {
      rollback_input_claims(node);
      return ksj::base::Status::StateError("SynchronousGraphExecutor dynamic input edge failed");
    }
    if (polled.kind == FixedBufferEdgePollKind::completed) {
      saw_completed = true;
      continue;
    }
    input.claim = std::move(*polled.reservation);
    saw_item = true;
    if (!input.claim.has_item_identity()) {
      rollback_input_claims(node);
      return ksj::base::Status::ValidationError(
        "SynchronousGraphExecutor requires an exact item identity on every dynamic input");
    }
    if (!have_identity) {
      identity = input.claim.item_identity();
      have_identity = true;
    } else if (!identities_match(identity, input.claim.item_identity())) {
      rollback_input_claims(node);
      return ksj::base::Status::ValidationError(
        "SynchronousGraphExecutor dynamic input heads do not form one exact identity cohort");
    }
  }
  if (dynamic_input_count == 0U) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor requires at least one dynamic input for a normal firing");
  }
  if (saw_completed) {
    rollback_input_claims(node);
    if (saw_item) {
      return ksj::base::Status::ValidationError(
        "SynchronousGraphExecutor cannot join a completed dynamic input with a queued sibling item");
    }
    return ksj::base::Status::Unavailable("SynchronousGraphExecutor node dynamic inputs reached EndOfInput");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status acquire_static_inputs(GraphState& state, NodeRuntime& node) {
  for (auto& input : node.inputs) {
    if (input.binding == nullptr || input.binding->source_kind() != SynchronousInputSourceKind::calibration_artifact) {
      continue;
    }
    if (state.calibration_store == nullptr) {
      release_static_input_leases(node);
      return ksj::base::Status::StateError("SynchronousGraphExecutor calibration store is unavailable");
    }
    auto calibration = state.calibration_store->try_acquire(input.binding->source_id());
    if (!calibration.ok()) {
      release_static_input_leases(node);
      return calibration.status();
    }
    input.calibration_lease = std::move(calibration).value();
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status materialize_and_build_inputs(NodeRuntime& node,
                                                             const DataItemIdentity& common_identity) {
  for (auto& input : node.inputs) {
    if (input.binding != nullptr && input.binding->source_kind() == SynchronousInputSourceKind::data_edge) {
      auto lease = input.claim.materialize();
      if (!lease.ok())
        return lease.status();
      input.data_lease = std::move(lease).value();
      input.claim = FixedBufferEdgeConsumerReservation{};
    }
  }
  if (node.input_items.size() != node.inputs.size() || node.input_batches.size() != node.inputs.size()) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor input ABI staging does not match frozen bindings");
  }
  for (std::size_t index = 0U; index < node.inputs.size(); ++index) {
    auto& input = node.inputs[index];
    if (input.binding == nullptr || !input.abi_type) {
      return ksj::base::Status::StateError("SynchronousGraphExecutor input ABI binding is incomplete");
    }
    ksj::base::Result<ksj::base::ConstByteSpan> payload =
      ksj::base::Status::StateError("SynchronousGraphExecutor input payload is not initialized");
    ksj::base::Result<ksj::base::ConstByteSpan> metadata =
      ksj::base::Status::StateError("SynchronousGraphExecutor input metadata is not initialized");
    const TypeDescriptor* actual_type = nullptr;
    if (input.binding->source_kind() == SynchronousInputSourceKind::data_edge) {
      if (!input.data_lease.valid()) {
        return ksj::base::Status::StateError("SynchronousGraphExecutor materialized an invalid dynamic input lease");
      }
      payload = input.data_lease.buffer().payload();
      metadata = input.data_lease.buffer().metadata();
      actual_type = input.data_lease.buffer().type_descriptor();
    } else {
      if (!input.calibration_lease.valid()) {
        return ksj::base::Status::StateError("SynchronousGraphExecutor acquired an invalid calibration read lease");
      }
      payload = input.calibration_lease.payload();
      metadata = input.calibration_lease.metadata();
      actual_type = input.calibration_lease.type_descriptor();
    }
    if (!payload.ok())
      return payload.status();
    if (!metadata.ok())
      return metadata.status();
    if (actual_type == nullptr || !actual_type->exactly_matches(input.binding->type_descriptor())) {
      return ksj::base::Status::ValidationError(
        "SynchronousGraphExecutor input payload TypeDescriptor does not match its frozen binding");
    }
    auto abi_port = to_u32(input.binding->abi_port(), "input ABI port");
    if (!abi_port.ok())
      return abi_port.status();
    node.input_items[index] = {.payload = payload.value(),
                               .metadata = metadata.value(),
                               .type = input.abi_type->view,
                               .semantic_key_hash = common_identity.semantic_key_hash,
                               .order_key = common_identity.order_key,
                               .item_ordinal = common_identity.item_ordinal};
    node.input_batches[index] = {.items = std::span<const SynchronousInputItem>{&node.input_items[index], 1U},
                                 .input_port = abi_port.value(),
                                 .batch_id = common_identity.item_ordinal,
                                 .order_domain = 0U};
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Result<Quantity> build_output_grants(NodeRuntime& node, const bool terminal = false) {
  if (node.output_grants.size() != node.outputs.size()) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor output ABI staging does not match frozen bindings");
  }
  if (terminal && !terminal_requires_output_grants(node)) {
    node.active_output_binding_count = 0U;
    return Quantity{0U};
  }
  Quantity active_count{0U};
  for (std::size_t index = 0U; index < node.outputs.size(); ++index) {
    auto& output = node.outputs[index];
    if (output.binding != nullptr &&
        (output.binding->maximum_item_count() == 0U ||
         (terminal && output.binding->destination_kind() == SynchronousOutputDestinationKind::calibration_artifact))) {
      continue;
    }
    if (output.binding == nullptr || !output.abi_type || !output.mutable_buffer.valid()) {
      return ksj::base::Status::StateError("SynchronousGraphExecutor output reservation is incomplete");
    }
    auto payload = output.mutable_buffer.writable_payload();
    auto metadata = output.mutable_buffer.writable_metadata();
    auto abi_port = to_u32(output.binding->abi_port(), "output ABI port");
    if (!payload.ok())
      return payload.status();
    if (!metadata.ok())
      return metadata.status();
    if (!abi_port.ok())
      return abi_port.status();
    auto maximum_item_count = to_u32(output.binding->maximum_item_count(), "output maximum_item_count");
    if (!maximum_item_count.ok())
      return maximum_item_count.status();
    if (active_count >= node.output_grants.size()) {
      return ksj::base::Status::InternalError("SynchronousGraphExecutor output grant staging overflowed");
    }
    node.output_grants[active_count] = {.storage = payload.value(),
                                        .metadata_storage = metadata.value(),
                                        .output_port = abi_port.value(),
                                        .maximum_item_count = maximum_item_count.value(),
                                        .required_type = output.abi_type->view};
    node.active_output_binding_indices[active_count] = static_cast<Quantity>(index);
    ++active_count;
  }
  node.active_output_binding_count = active_count;
  return active_count;
}

} // namespace

ksj::base::Status SynchronousGraphExecutor::commit_sealed_outputs_locked(
  const std::shared_ptr<detail::SynchronousGraphExecutorState>& state, detail::SynchronousGraphNodeRuntime& node,
  const std::span<const SynchronousSealedOutput> sealed_outputs,
  const std::optional<DataItemIdentity> expected_identity, const std::uint64_t terminal_epoch, const bool terminal) {
  // Ordinary graph firings have no optional output branch. A non-zero frozen
  // output binding must be sealed before any pool handle is published; this
  // preflight preserves fail-close atomicity instead of publishing a prefix
  // and discovering a silently omitted output afterward. A zero bound is the
  // sole explicit plan representation for an ordinary no-output binding.
  if (!terminal) {
    const auto required_count = required_normal_output_count(node);
    if (sealed_outputs.size() != required_count) {
      return fail_graph_locked(
        state, ksj::base::Status::ValidationError(
                 "SynchronousGraphExecutor ordinary firing did not seal every non-zero frozen output binding"));
    }
    for (Quantity slot = 0U; slot < node.active_output_binding_count; ++slot) {
      const auto sealed = std::find_if(sealed_outputs.begin(), sealed_outputs.end(), [slot](const auto& value) {
        return value.output_slot == slot;
      });
      if (sealed == sealed_outputs.end()) {
        return fail_graph_locked(
          state, ksj::base::Status::ValidationError(
                   "SynchronousGraphExecutor ordinary firing omitted a non-zero frozen output binding"));
      }
    }
  }
  Quantity terminal_items = 0U;
  Quantity terminal_bytes = 0U;
  for (std::size_t index = 0U; index < sealed_outputs.size(); ++index) {
    const auto& sealed = sealed_outputs[index];
    if (sealed.output_slot >= node.active_output_binding_count ||
        sealed.output_slot >= node.active_output_binding_indices.size()) {
      return fail_graph_locked(state,
                               ksj::base::Status::ValidationError(
                                 "SynchronousGraphExecutor Provider sealed an output slot outside the frozen plan"));
    }
    for (std::size_t earlier = 0U; earlier < index; ++earlier) {
      if (sealed_outputs[earlier].output_slot == sealed.output_slot) {
        return fail_graph_locked(state, ksj::base::Status::ValidationError(
                                          "SynchronousGraphExecutor Provider sealed one frozen output slot twice"));
      }
    }
    const auto binding_index = node.active_output_binding_indices[sealed.output_slot];
    if (binding_index >= node.outputs.size()) {
      return fail_graph_locked(state,
                               ksj::base::Status::InternalError(
                                 "SynchronousGraphExecutor output slot mapping is outside frozen node bindings"));
    }
    auto& output = node.outputs[binding_index];
    if (output.binding == nullptr || !output.mutable_buffer.valid() ||
        sealed.descriptor.output_port != output.binding->abi_port() || sealed.descriptor.produced_item_count != 1U) {
      return fail_graph_locked(state,
                               ksj::base::Status::ValidationError(
                                 "SynchronousGraphExecutor sealed output does not match its frozen one-item binding"));
    }
    if (expected_identity.has_value() && (sealed.descriptor.semantic_key_hash != expected_identity->semantic_key_hash ||
                                          sealed.descriptor.order_key != expected_identity->order_key)) {
      return fail_graph_locked(state, ksj::base::Status::ValidationError(
                                        "SynchronousGraphExecutor output identity does not match its input cohort"));
    }
    if (terminal) {
      if (output.binding->destination_kind() != SynchronousOutputDestinationKind::data_edge) {
        return fail_graph_locked(state,
                                 ksj::base::Status::ValidationError(
                                   "SynchronousGraphExecutor terminal callbacks cannot publish calibration artifacts"));
      }
      if (sealed.descriptor.produced_item_count > std::numeric_limits<Quantity>::max() - terminal_items ||
          sealed.descriptor.produced_byte_count > std::numeric_limits<Quantity>::max() - terminal_bytes) {
        return fail_graph_locked(
          state, ksj::base::Status::ValidationError("SynchronousGraphExecutor terminal output accounting overflowed"));
      }
      terminal_items += sealed.descriptor.produced_item_count;
      terminal_bytes += sealed.descriptor.produced_byte_count;
      if (terminal_items > node.plan->terminal().normal_max_output_items() ||
          terminal_bytes > node.plan->terminal().normal_max_output_charged_bytes()) {
        return fail_graph_locked(state,
                                 ksj::base::Status::ValidationError(
                                   "SynchronousGraphExecutor terminal output exceeds the frozen terminal bound"));
      }
    }
    const auto metadata_data = static_cast<const ksj::base::byte*>(sealed.descriptor.metadata.data);
    auto immutable =
      output.mutable_buffer.seal(output.binding->type_descriptor(), sealed.descriptor.produced_byte_count,
                                 {metadata_data, static_cast<std::size_t>(sealed.descriptor.metadata.size)});
    if (!immutable.ok())
      return fail_graph_locked(state, immutable.status());
    auto handle = std::move(immutable).value();
    const auto identity = expected_identity.has_value()
                            ? *expected_identity
                            : DataItemIdentity{.semantic_key_hash = sealed.descriptor.semantic_key_hash,
                                               .order_key = sealed.descriptor.order_key,
                                               .item_ordinal = terminal_epoch};
    ksj::base::Status committed;
    if (output.binding->destination_kind() == SynchronousOutputDestinationKind::data_edge) {
      if (!output.data_reservation.valid()) {
        return fail_graph_locked(state, ksj::base::Status::StateError(
                                          "SynchronousGraphExecutor data output lost its pre-reserved edge credit"));
      }
      committed = output.data_reservation.commit_from_with_identity(handle, identity);
    } else {
      if (state->calibration_store == nullptr) {
        return fail_graph_locked(
          state, ksj::base::Status::StateError("SynchronousGraphExecutor calibration store is unavailable"));
      }
      committed = state->calibration_store->publish(output.binding->destination_id(), handle);
    }
    if (!committed.ok())
      return fail_graph_locked(state, committed);
  }
  return ksj::base::Status::Ok();
}

namespace {

using GraphState = detail::SynchronousGraphExecutorState;
using NodeRuntime = detail::SynchronousGraphNodeRuntime;

[[nodiscard]] ksj::base::Status acknowledge_dynamic_inputs_locked(const std::shared_ptr<GraphState>& state,
                                                                  NodeRuntime& node,
                                                                  const Quantity dynamic_input_count) {
  for (auto& input : node.inputs) {
    if (input.binding != nullptr && input.binding->source_kind() == SynchronousInputSourceKind::data_edge) {
      const auto acknowledged = input.data_lease.acknowledge_consumed();
      if (!acknowledged.ok())
        return fail_graph_locked(state, acknowledged);
    }
  }
  if (dynamic_input_count > std::numeric_limits<Quantity>::max() - node.completed_input_item_count) {
    return fail_graph_locked(
      state, ksj::base::Status::ValidationError("SynchronousGraphExecutor input completion count overflowed"));
  }
  node.completed_input_item_count += dynamic_input_count;
  return ksj::base::Status::Ok();
}

[[nodiscard]] bool all_artifact_producers_completed(const GraphState& state) noexcept {
  if (state.execution_plan == nullptr)
    return false;
  for (const auto& artifact : state.execution_plan->calibration_artifact_binding_plans()) {
    const auto producer = state.nodes.find(artifact.producer_node_id());
    if (producer == state.nodes.end() || !producer->second.terminal_completed)
      return false;
  }
  return true;
}

[[nodiscard]] ksj::base::Status close_artifact_store_if_ready_locked(const std::shared_ptr<GraphState>& state) {
  if (state->calibration_store == nullptr || !all_artifact_producers_completed(*state)) {
    return ksj::base::Status::Ok();
  }
  const auto snapshot = state->calibration_store->snapshot();
  if (snapshot.lifecycle == CalibrationArtifactStoreLifecycle::accepting) {
    const auto ended = state->calibration_store->end_of_input();
    if (!ended.ok())
      return fail_graph_locked(state, ended);
  }
  const auto final_snapshot = state->calibration_store->snapshot();
  if (final_snapshot.missing_bindings != 0U) {
    return fail_graph_locked(state,
                             ksj::base::Status::StateError(
                               "SynchronousGraphExecutor reached calibration end_of_input with unpublished bindings"));
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] bool all_nodes_terminal(const GraphState& state) noexcept {
  return std::all_of(state.nodes.begin(), state.nodes.end(), [](const auto& item) {
    return item.second.terminal_completed;
  });
}

[[nodiscard]] ksj::base::Status dynamic_inputs_are_drained_and_completed(NodeRuntime& node) {
  bool saw_dynamic_input = false;
  for (auto& input : node.inputs) {
    if (input.binding == nullptr || input.binding->source_kind() != SynchronousInputSourceKind::data_edge) {
      continue;
    }
    saw_dynamic_input = true;
    if (input.claim.valid() || input.data_lease.valid() || input.data_edge == nullptr) {
      rollback_input_claims(node);
      return ksj::base::Status::StateError(
        "SynchronousGraphExecutor terminal admission found an unsettled dynamic input capability");
    }
    auto polled = input.data_edge->try_reserve_consumer();
    if (polled.kind == FixedBufferEdgePollKind::item && polled.reservation.has_value()) {
      input.claim = std::move(*polled.reservation);
      rollback_input_claims(node);
      return ksj::base::Status::Unavailable(
        "SynchronousGraphExecutor terminal admission is blocked by queued dynamic input");
    }
    if (polled.kind == FixedBufferEdgePollKind::empty) {
      rollback_input_claims(node);
      return ksj::base::Status::Unavailable(
        "SynchronousGraphExecutor terminal admission awaits dynamic input EndOfInput");
    }
    if (polled.kind != FixedBufferEdgePollKind::completed) {
      rollback_input_claims(node);
      return ksj::base::Status::StateError(
        "SynchronousGraphExecutor terminal admission observed a failed dynamic input edge");
    }
  }
  if (!saw_dynamic_input) {
    return ksj::base::Status::ValidationError(
      "SynchronousGraphExecutor terminal admission requires at least one dynamic data input");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status close_node_data_outputs_locked(const std::shared_ptr<GraphState>& state,
                                                               NodeRuntime& node) {
  for (std::size_t index = 0U; index < node.outputs.size(); ++index) {
    auto& output = node.outputs[index];
    if (output.binding == nullptr ||
        output.binding->destination_kind() != SynchronousOutputDestinationKind::data_edge) {
      continue;
    }
    if (output.data_edge == nullptr) {
      return fail_graph_locked(
        state, ksj::base::Status::StateError("SynchronousGraphExecutor terminal data output lost its edge"));
    }
    bool duplicate_edge = false;
    for (std::size_t earlier = 0U; earlier < index; ++earlier) {
      if (node.outputs[earlier].data_edge == output.data_edge) {
        duplicate_edge = true;
        break;
      }
    }
    if (duplicate_edge)
      continue;
    const auto closed = output.data_edge->end_of_input();
    if (!closed.ok())
      return fail_graph_locked(state, closed);
  }
  return ksj::base::Status::Ok();
}

} // namespace

ksj::base::Result<SynchronousFiringResult>
SynchronousGraphExecutor::try_fire(const std::string_view node_id, const SynchronousGraphNodeInvocation& invocation) {
  if (state_ == nullptr)
    return ksj::base::Status::StateError("SynchronousGraphExecutor is invalid");
  std::scoped_lock lock(state_->mutex);
  if (state_->lifecycle != SynchronousGraphExecutorLifecycle::accepting) {
    return state_->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor is terminal")
                                   : state_->last_error;
  }
  const auto found = state_->nodes.find(std::string(node_id));
  if (found == state_->nodes.end() || found->second.plan == nullptr || found->second.host == nullptr) {
    return ksj::base::Status::NotFound("SynchronousGraphExecutor node_id is not in the frozen graph");
  }
  auto& node = found->second;
  if (node.terminal_completed) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor node already reached normal terminal completion");
  }
  const auto fail_node = [this, &node](ksj::base::Status cause) -> ksj::base::Result<SynchronousFiringResult> {
    const auto failed = fail_graph_locked(state_, std::move(cause));
    clear_all_node_transient(node);
    return failed;
  };
  const auto invocation_status =
    validate_provider_invocation(*state_->execution_plan, *node.plan, invocation.provider_invocation);
  if (!invocation_status.ok())
    return invocation_status;
  const auto reserved = reserve_all_outputs(*state_, node);
  if (!reserved.ok()) {
    if (reserved.code() == ksj::base::StatusCode::unavailable)
      return reserved;
    return fail_graph_locked(state_, reserved);
  }
  DataItemIdentity common_identity{};
  Quantity dynamic_input_count{0U};
  const auto claimed = claim_dynamic_inputs(node, common_identity, dynamic_input_count);
  if (!claimed.ok()) {
    clear_output_reservations(node);
    if (claimed.code() == ksj::base::StatusCode::unavailable)
      return claimed;
    return fail_graph_locked(state_, claimed);
  }
  const auto static_inputs = acquire_static_inputs(*state_, node);
  if (!static_inputs.ok()) {
    rollback_input_claims(node);
    clear_output_reservations(node);
    if (static_inputs.code() == ksj::base::StatusCode::unavailable)
      return static_inputs;
    return fail_graph_locked(state_, static_inputs);
  }
  const auto inputs = materialize_and_build_inputs(node, common_identity);
  if (!inputs.ok()) {
    return fail_node(inputs);
  }
  const auto grants = build_output_grants(node);
  if (!grants.ok()) {
    return fail_node(grants.status());
  }
  bool outputs_committed = false;
  SynchronousFiringRequest request{
    .resource_occurrence_id = invocation.resource_occurrence_id,
    .slot_generation = invocation.slot_generation,
    .terminal_epoch = invocation.terminal_epoch,
    .input_batches = node.input_batches,
    .output_grants = std::span<const SynchronousOutputGrantSpec>{node.output_grants.data(), grants.value()},
    .scratch = node.scratch,
    .commit_outputs =
      [&state = state_, &node, common_identity,
       &outputs_committed](const std::span<const SynchronousSealedOutput> sealed_outputs) {
        const auto committed = SynchronousGraphExecutor::commit_sealed_outputs_locked(state, node, sealed_outputs,
                                                                                      common_identity, 0U, false);
        if (committed.ok())
          outputs_committed = true;
        return committed;
      },
  };
  auto result = node.host->process_preaccounted_output(invocation.provider_invocation, request);
  if (!result.ok()) {
    return fail_node(result.status());
  }
  if (result.value().outcome != SynchronousFiringOutcome::done ||
      (result.value().sealed_output_count != 0U && !outputs_committed) ||
      (required_normal_output_count(node) != 0U && result.value().sealed_output_count == 0U)) {
    const auto provider_error = result.value().provider_failure.message();
    return fail_node(ksj::base::Status::StateError(
      "SynchronousGraphExecutor Provider normal firing did not commit every required frozen output (outcome=" +
      std::string(to_string(result.value().outcome)) +
      ", sealed=" + std::to_string(result.value().sealed_output_count) +
      ", committed=" + std::to_string(result.value().committed_output_count) +
      ", required=" + std::to_string(required_normal_output_count(node)) +
      ", provider_status=" + std::to_string(static_cast<std::int32_t>(result.value().provider_status)) +
      ", provider_error_status=" + std::to_string(static_cast<std::int32_t>(result.value().provider_failure.status)) +
      (provider_error.empty() ? std::string{} : ", provider_error=\"" + std::string(provider_error) + "\"") +
      (result.value().provider_failure.message_truncated ? ", provider_error_truncated=true" : "") + ")"));
  }
  const auto acknowledged = acknowledge_dynamic_inputs_locked(state_, node, dynamic_input_count);
  if (!acknowledged.ok()) {
    clear_all_node_transient(node);
    return acknowledged;
  }
  release_static_input_leases(node);
  clear_output_reservations(node);
  return result;
}

ksj::base::Result<SynchronousFiringResult>
SynchronousGraphExecutor::try_finish_node(const std::string_view node_id,
                                          const SynchronousGraphNodeInvocation& invocation) {
  if (state_ == nullptr)
    return ksj::base::Status::StateError("SynchronousGraphExecutor is invalid");
  std::scoped_lock lock(state_->mutex);
  if (state_->lifecycle != SynchronousGraphExecutorLifecycle::accepting) {
    return state_->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor is terminal")
                                   : state_->last_error;
  }
  const auto found = state_->nodes.find(std::string(node_id));
  if (found == state_->nodes.end() || found->second.plan == nullptr || found->second.host == nullptr) {
    return ksj::base::Status::NotFound("SynchronousGraphExecutor node_id is not in the frozen graph");
  }
  auto& node = found->second;
  if (node.terminal_completed || node.terminal_attempted) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor node terminal callback was already admitted");
  }
  const auto fail_node = [this, &node](ksj::base::Status cause) -> ksj::base::Result<SynchronousFiringResult> {
    const auto failed = fail_graph_locked(state_, std::move(cause));
    clear_all_node_transient(node);
    return failed;
  };
  const auto invocation_status =
    validate_provider_invocation(*state_->execution_plan, *node.plan, invocation.provider_invocation);
  if (!invocation_status.ok())
    return invocation_status;
  const auto terminal_gate = dynamic_inputs_are_drained_and_completed(node);
  if (!terminal_gate.ok()) {
    if (terminal_gate.code() == ksj::base::StatusCode::unavailable)
      return terminal_gate;
    return fail_node(terminal_gate);
  }
  const auto reserved = reserve_all_outputs(*state_, node, true);
  if (!reserved.ok()) {
    if (reserved.code() == ksj::base::StatusCode::unavailable)
      return reserved;
    return fail_node(reserved);
  }
  const auto grants = build_output_grants(node, true);
  if (!grants.ok())
    return fail_node(grants.status());

  node.terminal_attempted = true;
  bool outputs_committed = false;
  SynchronousFiringRequest request{
    .resource_occurrence_id = invocation.resource_occurrence_id,
    .slot_generation = invocation.slot_generation,
    .terminal_epoch = invocation.terminal_epoch,
    .input_batches = {},
    .output_grants = std::span<const SynchronousOutputGrantSpec>{node.output_grants.data(), grants.value()},
    .scratch = node.scratch,
    .commit_outputs =
      [&state = state_, &node, terminal_epoch = invocation.terminal_epoch,
       &outputs_committed](const std::span<const SynchronousSealedOutput> sealed_outputs) {
        const auto committed = SynchronousGraphExecutor::commit_sealed_outputs_locked(
          state, node, sealed_outputs, std::nullopt, terminal_epoch, true);
        if (committed.ok())
          outputs_committed = true;
        return committed;
      },
  };
  auto result = node.host->on_scan_end_preaccounted_output(invocation.provider_invocation, request,
                                                           node.completed_input_item_count);
  if (!result.ok())
    return fail_node(result.status());
  if (result.value().outcome != SynchronousFiringOutcome::done ||
      (result.value().sealed_output_count != 0U && !outputs_committed)) {
    return fail_node(
      ksj::base::Status::StateError("SynchronousGraphExecutor Provider terminal firing did not commit successfully"));
  }
  clear_output_reservations(node);
  node.terminal_completed = true;
  const auto closed_outputs = close_node_data_outputs_locked(state_, node);
  if (!closed_outputs.ok()) {
    clear_all_node_transient(node);
    return closed_outputs;
  }
  const auto closed_artifacts = close_artifact_store_if_ready_locked(state_);
  if (!closed_artifacts.ok()) {
    clear_all_node_transient(node);
    return closed_artifacts;
  }
  if (all_nodes_terminal(*state_)) {
    state_->lifecycle = SynchronousGraphExecutorLifecycle::completed;
  }
  return result;
}

FixedBufferEdgePollKind SynchronousGraphExecutor::egress_poll_kind(const std::string_view egress_id) const {
  if (state_ == nullptr)
    return FixedBufferEdgePollKind::failed;
  std::scoped_lock lock(state_->mutex);
  const auto egress = state_->egresses.find(std::string(egress_id));
  if (egress == state_->egresses.end() || egress->second.edge == nullptr ||
      state_->lifecycle == SynchronousGraphExecutorLifecycle::failed) {
    return FixedBufferEdgePollKind::failed;
  }
  const auto snapshot = egress->second.edge->snapshot();
  if (snapshot.queued_items != 0U)
    return FixedBufferEdgePollKind::item;
  if (snapshot.lifecycle == FixedBufferEdgeLifecycle::completed)
    return FixedBufferEdgePollKind::completed;
  if (snapshot.lifecycle == FixedBufferEdgeLifecycle::failed ||
      snapshot.lifecycle == FixedBufferEdgeLifecycle::failed_draining) {
    return FixedBufferEdgePollKind::failed;
  }
  return FixedBufferEdgePollKind::empty;
}

ksj::base::Result<EgressInputLease> SynchronousGraphExecutor::try_acquire_egress(const std::string_view egress_id) {
  if (state_ == nullptr)
    return ksj::base::Status::StateError("SynchronousGraphExecutor is invalid");
  std::scoped_lock lock(state_->mutex);
  const auto egress = state_->egresses.find(std::string(egress_id));
  if (egress == state_->egresses.end() || egress->second.edge == nullptr ||
      state_->lifecycle == SynchronousGraphExecutorLifecycle::failed) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor egress is unknown or graph failed");
  }
  auto polled = egress->second.edge->try_acquire();
  if (polled.kind == FixedBufferEdgePollKind::item && polled.lease.has_value()) {
    return EgressInputLease{std::move(*polled.lease)};
  }
  if (polled.kind == FixedBufferEdgePollKind::empty) {
    return ksj::base::Status::Unavailable("SynchronousGraphExecutor egress has no available item");
  }
  return ksj::base::Status::StateError(polled.kind == FixedBufferEdgePollKind::completed
                                         ? "SynchronousGraphExecutor egress reached EndOfInput"
                                         : "SynchronousGraphExecutor egress failed");
}

ksj::base::Result<CalibrationArtifactReadLease>
SynchronousGraphExecutor::try_acquire_calibration_artifact(const std::string_view binding_id) {
  if (state_ == nullptr || state_->calibration_store == nullptr) {
    return ksj::base::Status::StateError("SynchronousGraphExecutor calibration store is unavailable");
  }
  // Publish/commit is graph-atomic under this mutex. Taking the same lock
  // here prevents an external diagnostic consumer from observing a prefix of
  // a multi-output firing before the executor has either committed all of it
  // or failed the graph closed.
  std::scoped_lock lock(state_->mutex);
  if (state_->lifecycle == SynchronousGraphExecutorLifecycle::failed) {
    return state_->last_error.ok() ? ksj::base::Status::StateError("SynchronousGraphExecutor is failed")
                                   : state_->last_error;
  }
  return state_->calibration_store->try_acquire(binding_id);
}

ksj::base::Status SynchronousGraphExecutor::abort() {
  if (state_ == nullptr)
    return ksj::base::Status::Ok();
  return fail_graph(state_, ksj::base::Status::StateError("SynchronousGraphExecutor was aborted"));
}

SynchronousGraphExecutorSnapshot SynchronousGraphExecutor::snapshot() const {
  SynchronousGraphExecutorSnapshot snapshot;
  if (state_ == nullptr) {
    snapshot.lifecycle = SynchronousGraphExecutorLifecycle::failed;
    snapshot.last_error = ksj::base::Status::StateError("SynchronousGraphExecutor is invalid");
    return snapshot;
  }
  std::scoped_lock lock(state_->mutex);
  snapshot.lifecycle = state_->lifecycle;
  snapshot.configured_nodes =
    state_->execution_plan == nullptr ? 0U : state_->execution_plan->synchronous_node_plans().size();
  for (const auto& [_, node] : state_->nodes) {
    if (node.terminal_completed)
      ++snapshot.completed_nodes;
  }
  snapshot.configured_ingresses = state_->ingresses.size();
  for (const auto& [_, ingress] : state_->ingresses) {
    if (ingress.closed)
      ++snapshot.closed_ingresses;
  }
  if (state_->calibration_store != nullptr) {
    const auto artifacts = state_->calibration_store->snapshot();
    snapshot.calibration_artifact_lifecycle = artifacts.lifecycle;
    snapshot.published_calibration_artifacts = artifacts.published_bindings;
    snapshot.missing_calibration_artifacts = artifacts.missing_bindings;
  }
  snapshot.last_error = state_->last_error;
  return snapshot;
}

} // namespace ksj::recon::runtime
