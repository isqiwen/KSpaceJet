#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"

#include "kspacejet/recon/runtime/host_frame_assembler.hpp"
#include "kspacejet/recon/runtime/ismrmrd_image_artifact_sink.hpp"

#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/recon/artifact_digest.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <ismrmrd/dataset.h>
#include <ismrmrd/meta.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using ksj::base::Result;
using ksj::base::Status;
using ksj::recon::ArtifactDigest;
using ksj::recon::CalibrationArtifactBindingPlanSpec;
using ksj::recon::ExecutionPlan;
using ksj::recon::ExecutionPlanSpec;
using ksj::recon::ExecutionProfile;
using ksj::recon::OperatorPlanBindingSpec;
using ksj::recon::Quantity;
using ksj::recon::ResourceVectorCapacity;
using ksj::recon::ResourceVectorCapacitySpec;
using ksj::recon::ResourceVectorSpec;
using ksj::recon::SynchronousBufferPoolPlanSpec;
using ksj::recon::SynchronousDataEdgePlanSpec;
using ksj::recon::SynchronousDataEndpointKind;
using ksj::recon::SynchronousDynamicInputJoinPolicy;
using ksj::recon::SynchronousInputSourceKind;
using ksj::recon::SynchronousNodeInputBindingPlanSpec;
using ksj::recon::SynchronousNodeOutputBindingPlanSpec;
using ksj::recon::SynchronousNodePlanSpec;
using ksj::recon::SynchronousOutputDestinationKind;
using ksj::recon::TypeDescriptor;
using ksj::recon::TypeMemoryDomain;
using ksj::recon::VerificationRecord;
using ksj::recon::VerificationRecordSpec;
using ksj::recon::runtime::CalibrationArtifactStoreLifecycle;
using ksj::recon::runtime::CartesianFrameSlotConfig;
using ksj::recon::runtime::CartesianLineCoordinate;
using ksj::recon::runtime::CompletedFrameIngressBridge;
using ksj::recon::runtime::DataItemIdentity;
using ksj::recon::runtime::DuplicateAcquisitionPolicy;
using ksj::recon::runtime::FixedBufferEdgePollKind;
using ksj::recon::runtime::FrameSlotContext;
using ksj::recon::runtime::HostFrameAssembler;
using ksj::recon::runtime::HostFrameAssemblerConfig;
using ksj::recon::runtime::IncompleteFramePolicy;
using ksj::recon::runtime::ResourceVectorLedger;
using ksj::recon::runtime::SynchronousGraphBufferPoolStorage;
using ksj::recon::runtime::SynchronousGraphDataEdgeStorage;
using ksj::recon::runtime::SynchronousGraphExecutor;
using ksj::recon::runtime::SynchronousGraphExecutorLifecycle;
using ksj::recon::runtime::SynchronousGraphExecutorStorage;
using ksj::recon::runtime::SynchronousGraphNodeInvocation;
using ksj::recon::runtime::SynchronousProviderInvocation;

constexpr std::string_view kProviderId = "org.kspacejet.tests.synchronous-firing-lease";
constexpr std::string_view kProviderBundleDigest =
  "sha256:808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
constexpr std::string_view kOperatorId = "synchronous_firing_lease_test_operator";
constexpr std::string_view kPlanDigest = "sha256:101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f";
constexpr std::string_view kVerificationDigest =
  "sha256:303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f";
constexpr std::string_view kResolvedPipelineDigest =
  "sha256:505152535455565758595a5b5c5d5e5f606162636465666768696a6b6c6d6e6f";
constexpr std::string_view kScanFactsDigest = "sha256:707172737475767778797a7b7c7d7e7f808182838485868788898a8b8c8d8e8f";
constexpr std::string_view kEffectivePipelineBindingDigest =
  "sha256:808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f";
constexpr std::string_view kEnvelopeDigest = "sha256:909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf";
constexpr std::string_view kMachinePolicyDigest =
  "sha256:b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf";
constexpr Quantity kPayloadCapacityBytes = 64U;
constexpr Quantity kMetadataCapacityBytes = 0U;
constexpr Quantity kGraphHostCapacityBytes = 2U * 1024U * 1024U;
constexpr Quantity kGraphDescriptorCapacity = 4096U;
constexpr std::string_view kArtifactSinkSourceXml =
  "<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\"><experimentalConditions>"
  "<H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz>"
  "</experimentalConditions></ismrmrdHeader>";

[[nodiscard]] Result<ArtifactDigest> parse_digest(const std::string_view value, const std::string_view field) {
  return ArtifactDigest::parse(value, field);
}

[[nodiscard]] ksj_provider_abi_header header(const std::uint32_t size, const std::uint64_t capabilities = 0U) noexcept {
  return ksj_provider_abi_header_make(size, capabilities);
}

[[nodiscard]] ksj_utf8_view text(const std::string_view value) noexcept {
  ksj_utf8_view result{};
  result.abi = header(sizeof(result));
  result.data = value.data();
  result.size = value.size();
  return result;
}

[[nodiscard]] ksj_digest256 digest(const std::uint8_t seed) noexcept {
  ksj_digest256 result{};
  result.abi = header(sizeof(result));
  for (std::uint32_t index = 0U; index < KSJ_PROVIDER_DIGEST256_SIZE; ++index) {
    result.bytes[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

[[nodiscard]] ksj_error_view error_storage() noexcept {
  ksj_error_view result{};
  result.abi = header(sizeof(result));
  result.message.abi = header(sizeof(result.message));
  return result;
}

struct ProviderInstance final {
  ksj::provider::loader::ProviderModule module{};
  ksj::provider::loader::ProviderLease lease{};
  ksj_provider_operator* operator_handle{nullptr};
  ksj_execution_context* execution_context{nullptr};
  ksj_key_state* key_state{nullptr};

  ProviderInstance() = default;
  ProviderInstance(const ProviderInstance&) = delete;
  ProviderInstance& operator=(const ProviderInstance&) = delete;
  ProviderInstance(ProviderInstance&&) = delete;
  ProviderInstance& operator=(ProviderInstance&&) = delete;

  ~ProviderInstance() {
    if (!lease.valid() || lease.api() == nullptr)
      return;
    const auto* api = lease.api();
    if (key_state != nullptr) {
      api->key_state_reset(operator_handle, execution_context, key_state);
      key_state = nullptr;
    }
    if (execution_context != nullptr) {
      api->execution_context_destroy(operator_handle, execution_context);
      execution_context = nullptr;
    }
    if (operator_handle != nullptr) {
      api->operator_destroy(operator_handle);
      operator_handle = nullptr;
    }
  }

  [[nodiscard]] Result<SynchronousProviderInvocation> invocation(const std::string_view node_id,
                                                                 const std::string_view canonical_config) const {
    auto config_digest = ksj::recon::derive_canonical_config_digest(canonical_config, "test canonical config");
    if (!config_digest.ok())
      return config_digest.status();
    return SynchronousProviderInvocation{
      .provider = lease,
      .node_id = std::string(node_id),
      .operator_id = std::string(kOperatorId),
      .canonical_config_digest = std::move(config_digest).value(),
      .operator_handle = operator_handle,
      .execution_context = execution_context,
      .key_state = key_state,
    };
  }
};

[[nodiscard]] Status initialize_provider(ProviderInstance& instance, const std::string_view canonical_config) {
  auto module = ksj::provider::loader::ProviderModule::load(
    std::filesystem::path(KSJ_SYNCHRONOUS_FIRING_LEASE_TEST_PROVIDER_MODULE));
  if (!module.ok())
    return module.status();
  instance.module = std::move(module).value();
  instance.lease = instance.module.acquire();
  const auto* api = instance.lease.api();
  const auto* descriptor = instance.lease.descriptor();
  if (api == nullptr || descriptor == nullptr) {
    return Status::StateError("test Provider has no descriptor or ABI table");
  }
  if (std::find_if(descriptor->operators.begin(), descriptor->operators.end(), [](const auto& candidate) {
        return candidate.operator_id == kOperatorId;
      }) == descriptor->operators.end()) {
    return Status::NotFound("test Provider does not expose its regular synchronous operator");
  }

  ksj_operator_create_request create{};
  create.abi = header(sizeof(create));
  create.operator_id = text(kOperatorId);
  create.canonical_config.abi = header(sizeof(create.canonical_config));
  create.canonical_config.data = canonical_config.data();
  create.canonical_config.size = canonical_config.size();
  auto error = error_storage();
  if (api->operator_create(&create, &instance.operator_handle, &error) != KSJ_STATUS_OK ||
      instance.operator_handle == nullptr) {
    return Status::ValidationError("test Provider rejected operator creation");
  }

  ksj_execution_context_descriptor context{};
  context.abi = header(sizeof(context));
  context.execution_context_id = 1U;
  context.max_backend_concurrency = 1U;
  error = error_storage();
  if (api->execution_context_create(instance.operator_handle, &context, &instance.execution_context, &error) !=
        KSJ_STATUS_OK ||
      instance.execution_context == nullptr) {
    return Status::ValidationError("test Provider rejected execution-context creation");
  }

  ksj_key_state_descriptor key{};
  key.abi = header(sizeof(key));
  key.semantic_key.abi = header(sizeof(key.semantic_key));
  key.key_state_generation = 1U;
  error = error_storage();
  if (api->key_state_init(instance.operator_handle, instance.execution_context, &key, &instance.key_state, &error) !=
        KSJ_STATUS_OK ||
      instance.key_state == nullptr) {
    return Status::ValidationError("test Provider rejected key-state creation");
  }

  ksj_scan_start_descriptor start{};
  start.abi = header(sizeof(start));
  start.run_id = text("test-run");
  start.scan_id = text("test-scan");
  start.normalized_scan_facts_digest = digest(0x21U);
  start.execution_plan_digest = digest(0x41U);
  start.terminal_epoch = 7U;
  error = error_storage();
  if (api->operator_on_start(instance.operator_handle, instance.execution_context, instance.key_state, &start,
                             &error) != KSJ_STATUS_OK) {
    return Status::ValidationError("test Provider rejected scan start");
  }
  return Status::Ok();
}

[[nodiscard]] Result<SynchronousBufferPoolPlanSpec>
make_pool(const std::string_view id, const SynchronousDataEndpointKind owner_kind, const std::string_view owner_id,
          const std::string_view owner_port, const TypeDescriptor& type_descriptor) {
  auto metadata = ksj::recon::synchronous_buffer_pool_host_metadata_charged_bytes(1U, "test pool metadata");
  if (!metadata.ok())
    return metadata.status();
  auto physical = ksj::recon::synchronous_buffer_pool_physical_charge_bytes(
    1U, kPayloadCapacityBytes, kMetadataCapacityBytes, "test pool physical charge");
  if (!physical.ok())
    return physical.status();
  return SynchronousBufferPoolPlanSpec{
    .pool_id = std::string(id),
    .owner_kind = owner_kind,
    .owner_id = std::string(owner_id),
    .owner_port_name = std::string(owner_port),
    .type_descriptor = type_descriptor,
    .memory_domain = TypeMemoryDomain::host_normal,
    .slot_count = 1U,
    .payload_capacity_bytes = kPayloadCapacityBytes,
    .metadata_capacity_bytes = kMetadataCapacityBytes,
    .payload_alignment_bytes = type_descriptor.min_alignment_bytes(),
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = 1U,
    .physical_charge_bytes = physical.value(),
  };
}

[[nodiscard]] Result<SynchronousDataEdgePlanSpec> make_edge(
  const std::string_view id, const std::string_view source_pool_id, const SynchronousDataEndpointKind producer_kind,
  const std::string_view producer_id, const std::string_view producer_port, const Quantity producer_abi_port,
  const SynchronousDataEndpointKind consumer_kind, const std::string_view consumer_id,
  const std::string_view consumer_port, const Quantity consumer_abi_port, const TypeDescriptor& type_descriptor) {
  auto metadata = ksj::recon::synchronous_data_edge_host_metadata_charged_bytes(1U, "test edge metadata");
  if (!metadata.ok())
    return metadata.status();
  return SynchronousDataEdgePlanSpec{
    .edge_id = std::string(id),
    .source_pool_id = std::string(source_pool_id),
    .producer_kind = producer_kind,
    .producer_id = std::string(producer_id),
    .producer_port_name = std::string(producer_port),
    .producer_abi_port = producer_abi_port,
    .consumer_kind = consumer_kind,
    .consumer_id = std::string(consumer_id),
    .consumer_port_name = std::string(consumer_port),
    .consumer_abi_port = consumer_abi_port,
    .type_descriptor = type_descriptor,
    .max_items = 1U,
    .max_logical_bytes = kPayloadCapacityBytes + kMetadataCapacityBytes,
    .host_metadata_charged_bytes = metadata.value(),
    .descriptor_charged_count = 1U,
  };
}

[[nodiscard]] SynchronousNodeInputBindingPlanSpec data_input(const std::string_view port_name, const Quantity abi_port,
                                                             const std::string_view edge_id,
                                                             const TypeDescriptor& type_descriptor) {
  return {.port_name = std::string(port_name),
          .abi_port = abi_port,
          .source_kind = SynchronousInputSourceKind::data_edge,
          .source_id = std::string(edge_id),
          .type_descriptor = type_descriptor,
          .maximum_item_count = 1U};
}

[[nodiscard]] SynchronousNodeInputBindingPlanSpec calibration_input(const std::string_view port_name,
                                                                    const Quantity abi_port,
                                                                    const std::string_view binding_id,
                                                                    const TypeDescriptor& type_descriptor) {
  return {.port_name = std::string(port_name),
          .abi_port = abi_port,
          .source_kind = SynchronousInputSourceKind::calibration_artifact,
          .source_id = std::string(binding_id),
          .type_descriptor = type_descriptor,
          .maximum_item_count = 1U};
}

[[nodiscard]] SynchronousNodeOutputBindingPlanSpec data_output(const std::string_view port_name,
                                                               const Quantity abi_port, const std::string_view edge_id,
                                                               const std::string_view pool_id,
                                                               const TypeDescriptor& type_descriptor) {
  return {.port_name = std::string(port_name),
          .abi_port = abi_port,
          .destination_kind = SynchronousOutputDestinationKind::data_edge,
          .destination_id = std::string(edge_id),
          .pool_id = std::string(pool_id),
          .type_descriptor = type_descriptor,
          .maximum_item_count = 1U};
}

[[nodiscard]] SynchronousNodeOutputBindingPlanSpec
artifact_output(const std::string_view port_name, const Quantity abi_port, const std::string_view binding_id,
                const std::string_view pool_id, const TypeDescriptor& type_descriptor) {
  return {.port_name = std::string(port_name),
          .abi_port = abi_port,
          .destination_kind = SynchronousOutputDestinationKind::calibration_artifact,
          .destination_id = std::string(binding_id),
          .pool_id = std::string(pool_id),
          .type_descriptor = type_descriptor,
          .maximum_item_count = 1U};
}

[[nodiscard]] SynchronousNodePlanSpec
make_node(const std::string_view node_id, std::vector<SynchronousNodeInputBindingPlanSpec> inputs,
          std::vector<SynchronousNodeOutputBindingPlanSpec> outputs, const Quantity terminal_output_items = 0U,
          const Quantity terminal_output_bytes = 0U, const Quantity scratch_bytes = 0U) {
  const auto input_count = static_cast<Quantity>(inputs.size());
  const auto output_count = static_cast<Quantity>(outputs.size());
  return {
    .node_id = std::string(node_id),
    .provider_id = std::string(kProviderId),
    .provider_bundle_digest = std::string(kProviderBundleDigest),
    .operator_id = std::string(kOperatorId),
    .dynamic_input_join_policy = SynchronousDynamicInputJoinPolicy::exact_item_identity,
    .inputs = std::move(inputs),
    .outputs = std::move(outputs),
    .firing = {.maximum_input_batches = input_count,
               .maximum_input_items = input_count,
               .maximum_output_grants = output_count,
               .maximum_input_payload_bytes = input_count * kPayloadCapacityBytes,
               .maximum_scratch_bytes = scratch_bytes,
               .maximum_metadata_bytes = 64U,
               .staging_charged_bytes = 64U * 1024U,
               .staging_descriptor_count = 64U,
               .firing_reservation = {.cpu_leaf_permits = 1U}},
    .terminal = {.normal_max_output_items = terminal_output_items,
                 .normal_max_output_charged_bytes = terminal_output_bytes,
                 .normal_max_async_tokens = 0U,
                 .cancel_max_async_tokens = 0U},
  };
}

struct NodeConfiguration final {
  SynchronousNodePlanSpec plan;
  std::string canonical_config;
};

struct GraphArtifacts final {
  ExecutionPlan plan;
  VerificationRecord verification;
};

class GraphPlanBuilder final {
public:
  [[nodiscard]] Status add_pool(const std::string_view id, const SynchronousDataEndpointKind owner_kind,
                                const std::string_view owner_id, const std::string_view owner_port,
                                const TypeDescriptor& type_descriptor) {
    auto pool = make_pool(id, owner_kind, owner_id, owner_port, type_descriptor);
    if (!pool.ok())
      return pool.status();
    pools_.push_back(std::move(pool).value());
    return Status::Ok();
  }

  [[nodiscard]] Status add_edge(const std::string_view id, const std::string_view source_pool_id,
                                const SynchronousDataEndpointKind producer_kind, const std::string_view producer_id,
                                const std::string_view producer_port, const Quantity producer_abi_port,
                                const SynchronousDataEndpointKind consumer_kind, const std::string_view consumer_id,
                                const std::string_view consumer_port, const Quantity consumer_abi_port,
                                const TypeDescriptor& type_descriptor) {
    auto edge = make_edge(id, source_pool_id, producer_kind, producer_id, producer_port, producer_abi_port,
                          consumer_kind, consumer_id, consumer_port, consumer_abi_port, type_descriptor);
    if (!edge.ok())
      return edge.status();
    edges_.push_back(std::move(edge).value());
    return Status::Ok();
  }

  void add_artifact(const std::string_view binding_id, const std::string_view producer_node_id,
                    const std::string_view producer_port_name, const Quantity producer_abi_port,
                    const std::string_view producer_pool_id, const TypeDescriptor& type_descriptor) {
    artifacts_.push_back({.binding_id = std::string(binding_id),
                          .producer_node_id = std::string(producer_node_id),
                          .producer_port_name = std::string(producer_port_name),
                          .producer_abi_port = producer_abi_port,
                          .producer_pool_id = std::string(producer_pool_id),
                          .type_descriptor = type_descriptor,
                          .host_metadata_charged_bytes = 64U,
                          .descriptor_charged_count = 1U});
  }

  [[nodiscard]] Result<GraphArtifacts> build(const std::vector<NodeConfiguration>& nodes) const {
    auto plan_digest = parse_digest(kPlanDigest, "test plan digest");
    if (!plan_digest.ok())
      return plan_digest.status();
    ExecutionPlanSpec specification;
    specification.inputs = {
      .resolved_pipeline = std::string(kResolvedPipelineDigest),
      .scan_facts = std::string(kScanFactsDigest),
      .effective_pipeline_binding = std::string(kEffectivePipelineBindingDigest),
      .target_envelope = std::string(kEnvelopeDigest),
      .machine_policy = std::string(kMachinePolicyDigest),
    };
    specification.execution_profile = ExecutionProfile::bounded_reconstruction_graph;
    specification.synchronous_buffer_pool_plans = pools_;
    specification.synchronous_data_edge_plans = edges_;
    specification.calibration_artifact_binding_plans = artifacts_;
    specification.resource_vector = {.host_normal_bytes = kGraphHostCapacityBytes,
                                     .descriptor_count = kGraphDescriptorCapacity,
                                     .cpu_leaf_permits = 1U};
    specification.terminal_occurrences = std::max<Quantity>(1U, static_cast<Quantity>(nodes.size()));
    specification.proof_obligations = {"test.synchronous-graph"};
    for (const auto& node : nodes) {
      auto config_digest = ksj::recon::derive_canonical_config_digest(node.canonical_config, "test node config");
      if (!config_digest.ok())
        return config_digest.status();
      specification.operator_plan_bindings.push_back(
        {.node_id = node.plan.node_id, .canonical_config_digest = config_digest.value().value()});
      specification.synchronous_node_plans.push_back(node.plan);
    }
    auto plan = ExecutionPlan::create(std::move(plan_digest).value(), specification);
    if (!plan.ok())
      return plan.status();
    auto verification_digest = parse_digest(kVerificationDigest, "test verification digest");
    if (!verification_digest.ok())
      return verification_digest.status();
    auto verification = VerificationRecord::create(
      std::move(verification_digest).value(),
      VerificationRecordSpec{.execution_plan_digest = plan.value().digest().value(),
                             .execution_profile = plan.value().execution_profile(),
                             .verified_resource_vector = specification.resource_vector,
                             .verified_terminal_occurrences = specification.terminal_occurrences,
                             .verified_obligations = {"test.synchronous-graph"}});
    if (!verification.ok())
      return verification.status();
    auto plan_value = std::move(plan).value();
    auto verification_value = std::move(verification).value();
    return GraphArtifacts{std::move(plan_value), std::move(verification_value)};
  }

private:
  std::vector<SynchronousBufferPoolPlanSpec> pools_;
  std::vector<SynchronousDataEdgePlanSpec> edges_;
  std::vector<CalibrationArtifactBindingPlanSpec> artifacts_;
};

class AlignedBytes final {
public:
  AlignedBytes() = default;

  explicit AlignedBytes(const std::size_t bytes, const std::size_t requested_alignment = alignof(std::max_align_t))
      : bytes_(bytes), alignment_(std::max(requested_alignment, alignof(std::max_align_t))) {
    if (bytes_ != 0U) {
      data_ = static_cast<std::byte*>(::operator new(bytes_, std::align_val_t{alignment_}));
      std::memset(data_, 0, bytes_);
    }
  }

  ~AlignedBytes() {
    if (data_ != nullptr)
      ::operator delete(data_, std::align_val_t{alignment_});
  }

  AlignedBytes(const AlignedBytes&) = delete;
  AlignedBytes& operator=(const AlignedBytes&) = delete;

  AlignedBytes(AlignedBytes&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)), bytes_(std::exchange(other.bytes_, 0U)),
        alignment_(std::exchange(other.alignment_, alignof(std::max_align_t))) {}

  AlignedBytes& operator=(AlignedBytes&& other) noexcept {
    if (this != &other) {
      if (data_ != nullptr)
        ::operator delete(data_, std::align_val_t{alignment_});
      data_ = std::exchange(other.data_, nullptr);
      bytes_ = std::exchange(other.bytes_, 0U);
      alignment_ = std::exchange(other.alignment_, alignof(std::max_align_t));
    }
    return *this;
  }

  [[nodiscard]] ksj::base::ByteSpan view() noexcept { return {data_, bytes_}; }

private:
  std::byte* data_{nullptr};
  std::size_t bytes_{0U};
  std::size_t alignment_{alignof(std::max_align_t)};
};

struct PoolSlab final {
  std::string pool_id;
  AlignedBytes payload;
  AlignedBytes metadata;
  AlignedBytes control;
};

struct EdgeSlab final {
  std::string edge_id;
  AlignedBytes control;
};

struct NodeScratchSlab final {
  std::string node_id;
  AlignedBytes storage;
};

class GraphSlabs final {
public:
  [[nodiscard]] static Result<GraphSlabs> create(const ExecutionPlan& plan) {
    try {
      GraphSlabs result;
      result.pools_.reserve(plan.synchronous_buffer_pool_plans().size());
      result.edges_.reserve(plan.synchronous_data_edge_plans().size());
      result.node_scratch_.reserve(plan.synchronous_node_plans().size());
      for (const auto& pool : plan.synchronous_buffer_pool_plans()) {
        const auto control = ksj::recon::runtime::fixed_buffer_pool_required_control_storage_bytes(pool.slot_count());
        if (!control.ok())
          return control.status();
        const auto slots = static_cast<std::size_t>(pool.slot_count());
        const auto payload_capacity = static_cast<std::size_t>(pool.payload_capacity_bytes());
        const auto metadata_capacity = static_cast<std::size_t>(pool.metadata_capacity_bytes());
        if ((payload_capacity != 0U && slots > std::numeric_limits<std::size_t>::max() / payload_capacity) ||
            (metadata_capacity != 0U && slots > std::numeric_limits<std::size_t>::max() / metadata_capacity)) {
          return Status::ValidationError("test graph slab size exceeds host address space");
        }
        result.pools_.push_back(
          {.pool_id = pool.pool_id(),
           .payload = AlignedBytes{slots * payload_capacity, static_cast<std::size_t>(pool.payload_alignment_bytes())},
           .metadata = AlignedBytes{slots * metadata_capacity},
           .control = AlignedBytes{control.value()}});
      }
      for (const auto& edge : plan.synchronous_data_edge_plans()) {
        const auto control = ksj::recon::runtime::fixed_buffer_edge_required_control_storage_bytes(edge.max_items());
        if (!control.ok())
          return control.status();
        result.edges_.push_back(
          {.edge_id = edge.edge_id(),
           .control = AlignedBytes{control.value(), ksj::recon::runtime::fixed_buffer_edge_storage_alignment()}});
      }
      for (const auto& node : plan.synchronous_node_plans()) {
        if (node.firing().maximum_scratch_bytes() > std::numeric_limits<std::size_t>::max()) {
          return Status::ValidationError("test node scratch slab exceeds host address space");
        }
        result.node_scratch_.push_back(
          {.node_id = node.node_id(),
           .storage = AlignedBytes{static_cast<std::size_t>(node.firing().maximum_scratch_bytes()),
                                   ksj::recon::runtime::kSynchronousGraphScratchMinimumAlignment}});
      }
      return result;
    } catch (const std::bad_alloc&) {
      return Status::OutOfMemory("unable to allocate synchronous graph test slabs");
    }
  }

  [[nodiscard]] SynchronousGraphExecutorStorage storage() {
    SynchronousGraphExecutorStorage result;
    result.buffer_pools.reserve(pools_.size());
    result.data_edges.reserve(edges_.size());
    result.node_scratch.reserve(node_scratch_.size());
    for (auto& pool : pools_) {
      result.buffer_pools.push_back({.pool_id = pool.pool_id,
                                     .storage = {.payload = pool.payload.view(),
                                                 .metadata = pool.metadata.view(),
                                                 .control = pool.control.view()}});
    }
    for (auto& edge : edges_) {
      result.data_edges.push_back({.edge_id = edge.edge_id, .storage = {.control = edge.control.view()}});
    }
    for (auto& scratch : node_scratch_) {
      result.node_scratch.push_back({.node_id = scratch.node_id, .storage = scratch.storage.view()});
    }
    return result;
  }

private:
  std::vector<PoolSlab> pools_;
  std::vector<EdgeSlab> edges_;
  std::vector<NodeScratchSlab> node_scratch_;
};

[[nodiscard]] Result<std::shared_ptr<ResourceVectorLedger>> make_ledger() {
  auto capacity =
    ResourceVectorCapacity::create(ResourceVectorCapacitySpec{.domains = {.host_normal_bytes = kGraphHostCapacityBytes,
                                                                          .descriptor_count = kGraphDescriptorCapacity,
                                                                          .cpu_leaf_permits = 1U},
                                                              .host_total_cap_bytes = kGraphHostCapacityBytes});
  if (!capacity.ok())
    return capacity.status();
  return std::make_shared<ResourceVectorLedger>(std::move(capacity).value());
}

[[nodiscard]] Result<std::unique_ptr<SynchronousGraphExecutor>> make_executor(const GraphArtifacts& artifacts,
                                                                              GraphSlabs& slabs) {
  auto ledger = make_ledger();
  if (!ledger.ok())
    return ledger.status();
  return SynchronousGraphExecutor::create(artifacts.plan, artifacts.verification, slabs.storage(),
                                          std::move(ledger).value());
}

[[nodiscard]] Status publish_ingress(SynchronousGraphExecutor& executor, const std::string_view ingress_id,
                                     const DataItemIdentity identity, const std::byte fill = std::byte{0x11}) {
  auto output = executor.try_acquire_ingress(ingress_id);
  if (!output.ok())
    return output.status();
  auto payload = output.value().writable_payload();
  if (!payload.ok())
    return payload.status();
  if (payload.value().size() < 4U)
    return Status::InternalError("test ingress payload capacity is unexpectedly small");
  std::fill_n(payload.value().begin(), 4U, fill);
  return output.value().seal_and_commit(4U, {}, identity);
}

[[nodiscard]] Status publish_float32_image_ingress(SynchronousGraphExecutor& executor,
                                                   const std::string_view ingress_id, const DataItemIdentity identity,
                                                   const std::array<float, 4U>& pixels) {
  auto output = executor.try_acquire_ingress(ingress_id);
  if (!output.ok())
    return output.status();
  auto payload = output.value().writable_payload();
  if (!payload.ok())
    return payload.status();
  if (payload.value().size() < sizeof(pixels))
    return Status::InternalError("test ingress payload capacity is unexpectedly small for a float32 image");
  std::memcpy(payload.value().data(), pixels.data(), sizeof(pixels));
  return output.value().seal_and_commit(sizeof(pixels), {}, identity);
}

[[nodiscard]] std::filesystem::path artifact_sink_test_path(const std::string_view filename) {
  const auto directory = std::filesystem::temp_directory_path() / "ksj_ismrmrd_image_artifact_sink_tests";
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  const auto path = directory / filename;
  std::filesystem::remove(path, error);
  return path;
}

struct TemporaryArtifactSinkPath final {
  std::filesystem::path path;

  ~TemporaryArtifactSinkPath() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

[[nodiscard]] ksj::recon::runtime::IsmrmrdMagnitudeImageArtifactDescriptor artifact_sink_descriptor() {
  ksj::recon::runtime::IsmrmrdMagnitudeImageArtifactDescriptor descriptor;
  descriptor.source_xml = std::string(kArtifactSinkSourceXml);
  descriptor.source_acquisition.measurement_uid = 42U;
  descriptor.source_acquisition.acquisition_time_stamp = 123U;
  descriptor.source_acquisition.position = {1.0F, 2.0F, 3.0F};
  descriptor.source_acquisition.read_dir = {1.0F, 0.0F, 0.0F};
  descriptor.source_acquisition.phase_dir = {0.0F, 1.0F, 0.0F};
  descriptor.source_acquisition.slice_dir = {0.0F, 0.0F, 1.0F};
  descriptor.source_acquisition.patient_table_position = {4.0F, 5.0F, 6.0F};
  descriptor.field_of_view_mm = {.x = 200.0, .y = 200.0, .z = 5.0};
  descriptor.rows = 2U;
  descriptor.cols = 2U;
  descriptor.provenance_attributes = {{"KSpaceJet.Test", "synchronous-graph-terminal-sink"}};
  return descriptor;
}

[[nodiscard]] HostFrameAssemblerConfig host_frame_assembler_config() {
  return {
    .scan_instance_id = "synchronous-graph-executor-test-scan",
    .frame_slots =
      {{.slot_id = 1U,
        .dimensions =
          {.readout_samples = 2U, .phase_encode_1 = 2U, .phase_encode_2 = 1U, .channels = 1U, .bytes_per_sample = 2U},
        .completion = {.required_indices = {{.phase_encode_1 = 0U, .phase_encode_2 = 0U},
                                            {.phase_encode_1 = 1U, .phase_encode_2 = 0U}}},
        .resource_upper_bound = {.max_total_arrivals = 2U, .max_duplicate_arrivals = 0U, .max_payload_bytes = 4U},
        .duplicate_policy = DuplicateAcquisitionPolicy::reject,
        .incomplete_policy = IncompleteFramePolicy::fail}},
  };
}

[[nodiscard]] SynchronousGraphNodeInvocation invocation(SynchronousProviderInvocation provider_invocation) {
  return {.provider_invocation = std::move(provider_invocation),
          .resource_occurrence_id = 1U,
          .slot_generation = 1U,
          .terminal_epoch = 7U};
}

TEST(SynchronousGraphExecutor, ExactJoinTwoInputsProducesOneIdentityCohort) {
  auto kspace = ksj::recon::types::noncartesian_kspace_frame();
  auto trajectory = ksj::recon::types::trajectory_frame();
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(kspace.ok()) << kspace.status();
  ASSERT_TRUE(trajectory.ok()) << trajectory.status();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto kspace_type = std::move(kspace).value();
  const auto trajectory_type = std::move(trajectory).value();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.kspace", SynchronousDataEndpointKind::ingress, "kspace", "", kspace_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.trajectory", SynchronousDataEndpointKind::ingress, "trajectory", "", trajectory_type).ok());
  ASSERT_TRUE(builder.add_pool("pool.image", SynchronousDataEndpointKind::node, "noncart", "image", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.kspace", "pool.kspace", SynchronousDataEndpointKind::ingress, "kspace", "", 0U,
                          SynchronousDataEndpointKind::node, "noncart", "kspace", 0U, kspace_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.trajectory", "pool.trajectory", SynchronousDataEndpointKind::ingress, "trajectory", "",
                          0U, SynchronousDataEndpointKind::node, "noncart", "trajectory", 1U, trajectory_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.image", "pool.image", SynchronousDataEndpointKind::node, "noncart", "image", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("noncart",
                                      {data_input("kspace", 0U, "edge.kspace", kspace_type),
                                       data_input("trajectory", 1U, "edge.trajectory", trajectory_type)},
                                      {data_output("image", 0U, "edge.image", "pool.image", image_type)}),
                    .canonical_config = "{\"mode\":\"done-output\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();

  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());
  auto provider_invocation = provider.invocation("noncart", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  const DataItemIdentity identity{.semantic_key_hash = 31U, .order_key = 47U, .item_ordinal = 59U};
  ASSERT_TRUE(publish_ingress(*executor_value, "kspace", identity, std::byte{0x21}).ok());
  ASSERT_TRUE(publish_ingress(*executor_value, "trajectory", identity, std::byte{0x42}).ok());
  auto fired = executor_value->try_fire("noncart", invocation(std::move(provider_invocation).value()));
  ASSERT_TRUE(fired.ok()) << fired.status();
  EXPECT_EQ(ksj::recon::runtime::SynchronousFiringOutcome::done, fired.value().outcome);

  auto egress = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(egress.ok()) << egress.status();
  auto egress_value = std::move(egress).value();
  EXPECT_EQ(identity.semantic_key_hash, egress_value.item_identity().semantic_key_hash);
  EXPECT_EQ(identity.order_key, egress_value.item_identity().order_key);
  EXPECT_EQ(identity.item_ordinal, egress_value.item_identity().item_ordinal);
  auto payload = egress_value.payload();
  ASSERT_TRUE(payload.ok()) << payload.status();
  ASSERT_FALSE(payload.value().empty());
  EXPECT_EQ(std::byte{0x5A}, payload.value().front());
  EXPECT_TRUE(egress_value.acknowledge_consumed().ok());
}

TEST(SynchronousGraphExecutor, MissingSiblingLeavesClaimedHeadUnchangedForRetry) {
  auto kspace = ksj::recon::types::noncartesian_kspace_frame();
  auto trajectory = ksj::recon::types::trajectory_frame();
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(kspace.ok()) << kspace.status();
  ASSERT_TRUE(trajectory.ok()) << trajectory.status();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto kspace_type = std::move(kspace).value();
  const auto trajectory_type = std::move(trajectory).value();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.kspace", SynchronousDataEndpointKind::ingress, "kspace", "", kspace_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.trajectory", SynchronousDataEndpointKind::ingress, "trajectory", "", trajectory_type).ok());
  ASSERT_TRUE(builder.add_pool("pool.image", SynchronousDataEndpointKind::node, "noncart", "image", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.kspace", "pool.kspace", SynchronousDataEndpointKind::ingress, "kspace", "", 0U,
                          SynchronousDataEndpointKind::node, "noncart", "kspace", 0U, kspace_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.trajectory", "pool.trajectory", SynchronousDataEndpointKind::ingress, "trajectory", "",
                          0U, SynchronousDataEndpointKind::node, "noncart", "trajectory", 1U, trajectory_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.image", "pool.image", SynchronousDataEndpointKind::node, "noncart", "image", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("noncart",
                                      {data_input("kspace", 0U, "edge.kspace", kspace_type),
                                       data_input("trajectory", 1U, "edge.trajectory", trajectory_type)},
                                      {data_output("image", 0U, "edge.image", "pool.image", image_type)}),
                    .canonical_config = "{\"mode\":\"done-output\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());
  const DataItemIdentity identity{.semantic_key_hash = 61U, .order_key = 67U, .item_ordinal = 71U};
  ASSERT_TRUE(publish_ingress(*executor_value, "kspace", identity).ok());

  auto first_invocation = provider.invocation("noncart", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(first_invocation.ok()) << first_invocation.status();
  auto blocked = executor_value->try_fire("noncart", invocation(std::move(first_invocation).value()));
  ASSERT_FALSE(blocked.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, blocked.status().code());
  EXPECT_EQ(SynchronousGraphExecutorLifecycle::accepting, executor_value->snapshot().lifecycle);

  ASSERT_TRUE(publish_ingress(*executor_value, "trajectory", identity).ok());
  auto retry_invocation = provider.invocation("noncart", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(retry_invocation.ok()) << retry_invocation.status();
  auto retried = executor_value->try_fire("noncart", invocation(std::move(retry_invocation).value()));
  ASSERT_TRUE(retried.ok()) << retried.status();
  auto egress = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(egress.ok()) << egress.status();
  auto egress_value = std::move(egress).value();
  EXPECT_EQ(identity.item_ordinal, egress_value.item_identity().item_ordinal);
  EXPECT_TRUE(egress_value.acknowledge_consumed().ok());
}

TEST(SynchronousGraphExecutor, MismatchedDynamicInputIdentityFailsClosed) {
  auto kspace = ksj::recon::types::noncartesian_kspace_frame();
  auto trajectory = ksj::recon::types::trajectory_frame();
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(kspace.ok()) << kspace.status();
  ASSERT_TRUE(trajectory.ok()) << trajectory.status();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto kspace_type = std::move(kspace).value();
  const auto trajectory_type = std::move(trajectory).value();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.kspace", SynchronousDataEndpointKind::ingress, "kspace", "", kspace_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.trajectory", SynchronousDataEndpointKind::ingress, "trajectory", "", trajectory_type).ok());
  ASSERT_TRUE(builder.add_pool("pool.image", SynchronousDataEndpointKind::node, "noncart", "image", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.kspace", "pool.kspace", SynchronousDataEndpointKind::ingress, "kspace", "", 0U,
                          SynchronousDataEndpointKind::node, "noncart", "kspace", 0U, kspace_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.trajectory", "pool.trajectory", SynchronousDataEndpointKind::ingress, "trajectory", "",
                          0U, SynchronousDataEndpointKind::node, "noncart", "trajectory", 1U, trajectory_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.image", "pool.image", SynchronousDataEndpointKind::node, "noncart", "image", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("noncart",
                                      {data_input("kspace", 0U, "edge.kspace", kspace_type),
                                       data_input("trajectory", 1U, "edge.trajectory", trajectory_type)},
                                      {data_output("image", 0U, "edge.image", "pool.image", image_type)}),
                    .canonical_config = "{\"mode\":\"done-output\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());
  ASSERT_TRUE(
    publish_ingress(*executor_value, "kspace", {.semantic_key_hash = 201U, .order_key = 211U, .item_ordinal = 223U})
      .ok());
  ASSERT_TRUE(
    publish_ingress(*executor_value, "trajectory", {.semantic_key_hash = 201U, .order_key = 211U, .item_ordinal = 227U})
      .ok());
  auto provider_invocation = provider.invocation("noncart", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  auto fired = executor_value->try_fire("noncart", invocation(std::move(provider_invocation).value()));
  ASSERT_FALSE(fired.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, fired.status().code());
  EXPECT_EQ(SynchronousGraphExecutorLifecycle::failed, executor_value->snapshot().lifecycle);
  EXPECT_EQ(FixedBufferEdgePollKind::failed, executor_value->egress_poll_kind("images"));
}

TEST(SynchronousGraphExecutor, CompletedHostFrameBridgeCopiesIntoGenericIngressAndAcknowledgesSource) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.imaging", SynchronousDataEndpointKind::ingress, "imaging", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.image", SynchronousDataEndpointKind::node, "reconstruct", "image", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.imaging", "pool.imaging", SynchronousDataEndpointKind::ingress, "imaging", "", 0U,
                          SynchronousDataEndpointKind::node, "reconstruct", "image", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.image", "pool.image", SynchronousDataEndpointKind::node, "reconstruct", "image", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("reconstruct", {data_input("image", 0U, "edge.imaging", image_type)},
                                      {data_output("image", 0U, "edge.image", "pool.image", image_type)}),
                    .canonical_config = "{\"mode\":\"mirror-input\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();

  auto assembler = HostFrameAssembler::create(host_frame_assembler_config());
  ASSERT_TRUE(assembler.ok()) << assembler.status();
  auto assembler_value = std::move(assembler).value();
  auto bridge = CompletedFrameIngressBridge::create(*executor_value, "imaging", *assembler_value);
  ASSERT_TRUE(bridge.ok()) << bridge.status();
  auto bridge_value = std::move(bridge).value();

  FrameSlotContext context{};
  context.semantic_key.slice = 3U;
  context.order_key = 41U;
  context.placement_key = 43U;
  auto assembly = assembler_value->try_begin_frame(context);
  ASSERT_TRUE(assembly.ok()) << assembly.status();
  auto frame = std::move(assembly).value();
  const std::array<std::byte, 4U> ky0{std::byte{0x11}, std::byte{0x12}, std::byte{0x13}, std::byte{0x14}};
  const std::array<std::byte, 4U> ky1{std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0x24}};
  ASSERT_TRUE(frame.scatter({.phase_encode_1 = 0U, .phase_encode_2 = 0U}, ky0).ok());
  ASSERT_TRUE(frame.scatter({.phase_encode_1 = 1U, .phase_encode_2 = 0U}, ky1).ok());
  auto completed = frame.seal_complete();
  ASSERT_TRUE(completed.ok()) << completed.status();
  auto completed_value = std::move(completed).value();
  auto completed_context = completed_value.context();
  ASSERT_TRUE(completed_context.ok()) << completed_context.status();
  EXPECT_EQ(context.semantic_key.slice, completed_context.value().semantic_key.slice);
  EXPECT_EQ(context.order_key, completed_context.value().order_key);
  EXPECT_EQ(context.placement_key, completed_context.value().placement_key);

  const DataItemIdentity identity{.semantic_key_hash = 47U, .order_key = context.order_key, .item_ordinal = 53U};
  ASSERT_TRUE(bridge_value.publish(std::move(completed_value), identity).ok());
  const auto host_after_publish = assembler_value->snapshot();
  EXPECT_FALSE(host_after_publish.failed);
  EXPECT_EQ(1U, host_after_publish.free_slots);
  EXPECT_EQ(0U, host_after_publish.ready_slots);
  EXPECT_EQ(0U, host_after_publish.dispatched_slots);

  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"mirror-input\"}").ok());
  auto provider_invocation = provider.invocation("reconstruct", "{\"mode\":\"mirror-input\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("reconstruct", invocation(std::move(provider_invocation).value())).ok());
  auto egress = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(egress.ok()) << egress.status();
  auto egress_value = std::move(egress).value();
  EXPECT_EQ(identity.semantic_key_hash, egress_value.item_identity().semantic_key_hash);
  EXPECT_EQ(identity.order_key, egress_value.item_identity().order_key);
  EXPECT_EQ(identity.item_ordinal, egress_value.item_identity().item_ordinal);
  auto payload = egress_value.payload();
  ASSERT_TRUE(payload.ok()) << payload.status();
  ASSERT_EQ(8U, payload.value().size());
  EXPECT_TRUE(std::equal(payload.value().begin(), payload.value().begin() + 4U, ky0.begin()));
  EXPECT_TRUE(std::equal(payload.value().begin() + 4U, payload.value().end(), ky1.begin()));
  EXPECT_TRUE(egress_value.acknowledge_consumed().ok());
  ASSERT_TRUE(bridge_value.end_of_input().ok());
}

TEST(SynchronousGraphExecutor, OutputReservationPrecedesInputConsumption) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "scale", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "scale", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "scale", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("scale", {data_input("input", 0U, "edge.input", image_type)},
                                      {data_output("output", 0U, "edge.output", "pool.output", image_type)}),
                    .canonical_config = "{\"mode\":\"done-output\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output\"}").ok());

  const DataItemIdentity first_identity{.semantic_key_hash = 1U, .order_key = 2U, .item_ordinal = 3U};
  ASSERT_TRUE(publish_ingress(*executor_value, "input", first_identity).ok());
  auto first_invocation = provider.invocation("scale", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(first_invocation.ok()) << first_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("scale", invocation(std::move(first_invocation).value())).ok());
  auto held_egress = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(held_egress.ok()) << held_egress.status();
  auto held = std::move(held_egress).value();

  const DataItemIdentity second_identity{.semantic_key_hash = 4U, .order_key = 5U, .item_ordinal = 6U};
  ASSERT_TRUE(publish_ingress(*executor_value, "input", second_identity).ok());
  auto blocked_invocation = provider.invocation("scale", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(blocked_invocation.ok()) << blocked_invocation.status();
  auto blocked = executor_value->try_fire("scale", invocation(std::move(blocked_invocation).value()));
  ASSERT_FALSE(blocked.ok());
  EXPECT_EQ(ksj::base::StatusCode::unavailable, blocked.status().code());
  EXPECT_EQ(SynchronousGraphExecutorLifecycle::accepting, executor_value->snapshot().lifecycle);

  ASSERT_TRUE(held.acknowledge_consumed().ok());
  auto retry_invocation = provider.invocation("scale", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(retry_invocation.ok()) << retry_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("scale", invocation(std::move(retry_invocation).value())).ok());
  auto output = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(output.ok()) << output.status();
  auto output_value = std::move(output).value();
  EXPECT_EQ(second_identity.item_ordinal, output_value.item_identity().item_ordinal);
  EXPECT_TRUE(output_value.acknowledge_consumed().ok());
}

TEST(SynchronousGraphExecutor, OrdinaryArtifactPublishesAndConditionerReadsBeforeTerminal) {
  auto noise = ksj::recon::types::noise_calibration_frame();
  auto model = ksj::recon::types::noise_model();
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(noise.ok()) << noise.status();
  ASSERT_TRUE(model.ok()) << model.status();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto noise_type = std::move(noise).value();
  const auto model_type = std::move(model).value();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.noise", SynchronousDataEndpointKind::ingress, "noise", "", noise_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.model", SynchronousDataEndpointKind::node, "estimate", "noise-model", model_type).ok());
  ASSERT_TRUE(builder.add_pool("pool.image", SynchronousDataEndpointKind::ingress, "image", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.conditioned", SynchronousDataEndpointKind::node, "condition", "image", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.noise", "pool.noise", SynchronousDataEndpointKind::ingress, "noise", "", 0U,
                          SynchronousDataEndpointKind::node, "estimate", "noise", 0U, noise_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.image", "pool.image", SynchronousDataEndpointKind::ingress, "image", "", 0U,
                          SynchronousDataEndpointKind::node, "condition", "image", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.conditioned", "pool.conditioned", SynchronousDataEndpointKind::node, "condition",
                          "image", 0U, SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  builder.add_artifact("noise-model", "estimate", "noise-model", 0U, "pool.model", model_type);
  auto artifacts =
    builder.build({{.plan = make_node("estimate", {data_input("noise", 0U, "edge.noise", noise_type)},
                                      {artifact_output("noise-model", 0U, "noise-model", "pool.model", model_type)}),
                    .canonical_config = "{\"mode\":\"done-output\"}"},
                   {.plan = make_node("condition",
                                      {data_input("image", 0U, "edge.image", image_type),
                                       calibration_input("noise-model", 1U, "noise-model", model_type)},
                                      {data_output("image", 0U, "edge.conditioned", "pool.conditioned", image_type)}),
                    .canonical_config = "{\"mode\":\"done-output\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();

  ProviderInstance estimate_provider;
  ProviderInstance condition_provider;
  ASSERT_TRUE(initialize_provider(estimate_provider, "{\"mode\":\"done-output\"}").ok());
  ASSERT_TRUE(initialize_provider(condition_provider, "{\"mode\":\"done-output\"}").ok());
  const DataItemIdentity noise_identity{.semantic_key_hash = 101U, .order_key = 103U, .item_ordinal = 107U};
  ASSERT_TRUE(publish_ingress(*executor_value, "noise", noise_identity).ok());
  auto estimate_invocation = estimate_provider.invocation("estimate", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(estimate_invocation.ok()) << estimate_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("estimate", invocation(std::move(estimate_invocation).value())).ok());

  auto artifact = executor_value->try_acquire_calibration_artifact("noise-model");
  ASSERT_TRUE(artifact.ok()) << artifact.status();
  auto artifact_value = std::move(artifact).value();
  ASSERT_NE(nullptr, artifact_value.type_descriptor());
  EXPECT_TRUE(artifact_value.type_descriptor()->exactly_matches(model_type));
  auto artifact_payload = artifact_value.payload();
  ASSERT_TRUE(artifact_payload.ok()) << artifact_payload.status();
  ASSERT_FALSE(artifact_payload.value().empty());
  EXPECT_EQ(std::byte{0x5A}, artifact_payload.value().front());
  artifact_value.release();
  const auto artifact_snapshot = executor_value->snapshot();
  EXPECT_EQ(CalibrationArtifactStoreLifecycle::accepting, artifact_snapshot.calibration_artifact_lifecycle);
  EXPECT_EQ(1U, artifact_snapshot.published_calibration_artifacts);

  const DataItemIdentity image_identity{.semantic_key_hash = 109U, .order_key = 113U, .item_ordinal = 127U};
  ASSERT_TRUE(publish_ingress(*executor_value, "image", image_identity).ok());
  auto condition_invocation = condition_provider.invocation("condition", "{\"mode\":\"done-output\"}");
  ASSERT_TRUE(condition_invocation.ok()) << condition_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("condition", invocation(std::move(condition_invocation).value())).ok());
  auto output = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(output.ok()) << output.status();
  auto output_value = std::move(output).value();
  EXPECT_EQ(image_identity.item_ordinal, output_value.item_identity().item_ordinal);
  EXPECT_TRUE(output_value.acknowledge_consumed().ok());
}

TEST(SynchronousGraphExecutor, DoneWithRequiredOutputButZeroSealFailsClosed) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();
  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "operator", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "operator", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "operator", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("operator", {data_input("input", 0U, "edge.input", image_type)},
                                      {data_output("output", 0U, "edge.output", "pool.output", image_type)}),
                    .canonical_config = "{\"mode\":\"done-zero-output\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-zero-output\"}").ok());
  ASSERT_TRUE(
    publish_ingress(*executor_value, "input", {.semantic_key_hash = 7U, .order_key = 11U, .item_ordinal = 13U}).ok());
  auto provider_invocation = provider.invocation("operator", "{\"mode\":\"done-zero-output\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  auto fired = executor_value->try_fire("operator", invocation(std::move(provider_invocation).value()));
  ASSERT_FALSE(fired.ok());
  EXPECT_EQ(SynchronousGraphExecutorLifecycle::failed, executor_value->snapshot().lifecycle);
  EXPECT_EQ(FixedBufferEdgePollKind::failed, executor_value->egress_poll_kind("images"));
}

TEST(SynchronousGraphExecutor, OwnsExactPersistentNodeScratchAndSuppliesItToTheProvider) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "operator", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "operator", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "operator", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts = builder.build(
    {{.plan = make_node("operator", {data_input("input", 0U, "edge.input", image_type)},
                        {data_output("output", 0U, "edge.output", "pool.output", image_type)}, 0U, 0U, 16U),
      .canonical_config = "{\"mode\":\"uses-scratch\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();

  {
    auto missing = slabs_value.storage();
    missing.node_scratch.clear();
    auto ledger = make_ledger();
    ASSERT_TRUE(ledger.ok()) << ledger.status();
    auto rejected = SynchronousGraphExecutor::create(artifacts_value.plan, artifacts_value.verification,
                                                     std::move(missing), std::move(ledger).value());
    ASSERT_FALSE(rejected.ok());
    EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
  }
  {
    auto wrong_size = slabs_value.storage();
    auto& scratch = wrong_size.node_scratch.front().storage;
    scratch = {scratch.data(), 15U};
    auto ledger = make_ledger();
    ASSERT_TRUE(ledger.ok()) << ledger.status();
    auto rejected = SynchronousGraphExecutor::create(artifacts_value.plan, artifacts_value.verification,
                                                     std::move(wrong_size), std::move(ledger).value());
    ASSERT_FALSE(rejected.ok());
    EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
  }
  {
    AlignedBytes misaligned_backing{17U, ksj::recon::runtime::kSynchronousGraphScratchMinimumAlignment};
    auto misaligned = slabs_value.storage();
    const auto backing = misaligned_backing.view();
    misaligned.node_scratch.front().storage = {backing.data() + 1U, 16U};
    auto ledger = make_ledger();
    ASSERT_TRUE(ledger.ok()) << ledger.status();
    auto rejected = SynchronousGraphExecutor::create(artifacts_value.plan, artifacts_value.verification,
                                                     std::move(misaligned), std::move(ledger).value());
    ASSERT_FALSE(rejected.ok());
    EXPECT_EQ(ksj::base::StatusCode::validation_error, rejected.status().code());
  }

  auto storage = slabs_value.storage();
  ASSERT_EQ(1U, storage.node_scratch.size());
  const auto scratch_view = storage.node_scratch.front().storage;
  ASSERT_EQ(16U, scratch_view.size());
  auto ledger = make_ledger();
  ASSERT_TRUE(ledger.ok()) << ledger.status();
  auto ledger_value = std::move(ledger).value();
  auto executor = SynchronousGraphExecutor::create(artifacts_value.plan, artifacts_value.verification,
                                                   std::move(storage), ledger_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();
  const auto after_create = ledger_value->snapshot();
  EXPECT_GE(after_create.used.host_normal_bytes, 16U);
  EXPECT_TRUE(after_create.reserved.empty());

  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"uses-scratch\"}").ok());
  ASSERT_TRUE(
    publish_ingress(*executor_value, "input", {.semantic_key_hash = 29U, .order_key = 31U, .item_ordinal = 37U}).ok());
  auto provider_invocation = provider.invocation("operator", "{\"mode\":\"uses-scratch\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("operator", invocation(std::move(provider_invocation).value())).ok());
  EXPECT_EQ(std::byte{0xA5}, scratch_view.front());
  const auto after_fire = ledger_value->snapshot();
  EXPECT_EQ(after_create.used, after_fire.used);
  EXPECT_TRUE(after_fire.reserved.empty());

  auto output = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(output.ok()) << output.status();
  EXPECT_TRUE(output.value().acknowledge_consumed().ok());
}

TEST(SynchronousGraphExecutor, TerminalPropagatesEndOfInputAfterNodeDrains) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();
  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "operator", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "operator", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "operator", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("operator", {data_input("input", 0U, "edge.input", image_type)},
                                      {data_output("output", 0U, "edge.output", "pool.output", image_type)}),
                    .canonical_config = "{\"mode\":\"done-output-zero-terminal\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();
  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"done-output-zero-terminal\"}").ok());
  ASSERT_TRUE(
    publish_ingress(*executor_value, "input", {.semantic_key_hash = 17U, .order_key = 19U, .item_ordinal = 23U}).ok());
  auto ordinary_invocation = provider.invocation("operator", "{\"mode\":\"done-output-zero-terminal\"}");
  ASSERT_TRUE(ordinary_invocation.ok()) << ordinary_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("operator", invocation(std::move(ordinary_invocation).value())).ok());
  auto output = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(output.ok()) << output.status();
  auto output_value = std::move(output).value();
  ASSERT_TRUE(output_value.acknowledge_consumed().ok());
  ASSERT_TRUE(executor_value->end_ingress("input").ok());
  auto terminal_invocation = provider.invocation("operator", "{\"mode\":\"done-output-zero-terminal\"}");
  ASSERT_TRUE(terminal_invocation.ok()) << terminal_invocation.status();
  auto terminal = executor_value->try_finish_node("operator", invocation(std::move(terminal_invocation).value()));
  ASSERT_TRUE(terminal.ok()) << terminal.status();
  EXPECT_EQ(ksj::recon::runtime::SynchronousFiringOutcome::done, terminal.value().outcome);
  EXPECT_EQ(SynchronousGraphExecutorLifecycle::completed, executor_value->snapshot().lifecycle);
  EXPECT_EQ(FixedBufferEdgePollKind::completed, executor_value->egress_poll_kind("images"));
}

TEST(SynchronousGraphExecutor,
     IsmrmrdImageArtifactSinkAtomicallyReplacesExistingDestinationThenAcknowledgesTerminalEgress) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "operator", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "operator", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "operator", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("operator", {data_input("input", 0U, "edge.input", image_type)},
                                      {data_output("output", 0U, "edge.output", "pool.output", image_type)}),
                    .canonical_config = "{\"mode\":\"mirror-input\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();

  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"mirror-input\"}").ok());
  const std::array<float, 4U> pixels{1.25F, 2.5F, 3.75F, 5.0F};
  ASSERT_TRUE(publish_float32_image_ingress(*executor_value, "input",
                                            {.semantic_key_hash = 43U, .order_key = 47U, .item_ordinal = 53U}, pixels)
                .ok());
  auto provider_invocation = provider.invocation("operator", "{\"mode\":\"mirror-input\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("operator", invocation(std::move(provider_invocation).value())).ok());

  auto acquired = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(acquired.ok()) << acquired.status();
  auto egress = std::move(acquired).value();
  ASSERT_TRUE(egress.valid());

  constexpr std::string_view kPreviousArtifactContents = "previous ISMRMRD artifact must be replaced as one file";
  TemporaryArtifactSinkPath output{.path = artifact_sink_test_path("published_terminal_image.mrd")};
  {
    std::ofstream existing_output(output.path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(existing_output.is_open());
    existing_output.write(kPreviousArtifactContents.data(),
                          static_cast<std::streamsize>(kPreviousArtifactContents.size()));
    ASSERT_TRUE(existing_output.good());
  }
  ASSERT_TRUE(std::filesystem::exists(output.path));
  ASSERT_EQ(kPreviousArtifactContents.size(), std::filesystem::file_size(output.path));

  ksj::recon::runtime::IsmrmrdImageArtifactSink sink(output.path, artifact_sink_descriptor());
  ASSERT_TRUE(sink.commit(egress).ok());

  EXPECT_FALSE(egress.valid());
  EXPECT_EQ(FixedBufferEdgePollKind::empty, executor_value->egress_poll_kind("images"));
  EXPECT_TRUE(std::filesystem::exists(output.path));
  {
    std::ifstream published_bytes(output.path, std::ios::binary);
    ASSERT_TRUE(published_bytes.is_open());
    const std::string contents{std::istreambuf_iterator<char>{published_bytes}, std::istreambuf_iterator<char>{}};
    EXPECT_EQ(std::string::npos, contents.find(kPreviousArtifactContents));
  }

  const auto filename = output.path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", false);
  std::string source_xml;
  dataset.readHeader(source_xml);
  EXPECT_EQ(kArtifactSinkSourceXml, source_xml);
  ASSERT_EQ(1U, dataset.getNumberOfImages("image_0"));
  ISMRMRD::Image<float> artifact;
  dataset.readImage("image_0", 0U, artifact);
  const auto& header = artifact.getHead();
  EXPECT_EQ(ISMRMRD::ISMRMRD_FLOAT, artifact.getDataType());
  EXPECT_EQ(2U, header.matrix_size[0]);
  EXPECT_EQ(2U, header.matrix_size[1]);
  EXPECT_EQ(1U, header.matrix_size[2]);
  EXPECT_EQ(1U, header.channels);
  EXPECT_EQ(ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE, header.image_type);
  EXPECT_EQ(42U, header.measurement_uid);
  EXPECT_EQ(123U, header.acquisition_time_stamp);
  EXPECT_FLOAT_EQ(200.0F, header.field_of_view[0]);
  EXPECT_FLOAT_EQ(200.0F, header.field_of_view[1]);
  EXPECT_FLOAT_EQ(5.0F, header.field_of_view[2]);
  EXPECT_FLOAT_EQ(1.0F, header.position[0]);
  EXPECT_FLOAT_EQ(2.0F, header.position[1]);
  EXPECT_FLOAT_EQ(3.0F, header.position[2]);
  for (std::uint16_t row = 0U; row < 2U; ++row) {
    for (std::uint16_t column = 0U; column < 2U; ++column) {
      EXPECT_FLOAT_EQ(pixels[static_cast<std::size_t>(row) * 2U + column], artifact(column, row));
    }
  }
  std::string attributes;
  artifact.getAttributeString(attributes);
  ISMRMRD::MetaContainer metadata;
  ISMRMRD::deserialize(attributes.c_str(), metadata);
  EXPECT_STREQ("Image", metadata.as_str("DataRole"));
  EXPECT_STREQ("1", metadata.as_str("ImageNumber"));
  EXPECT_STREQ("synchronous-graph-terminal-sink", metadata.as_str("KSpaceJet.Test"));
}

TEST(SynchronousGraphExecutor, IsmrmrdImageArtifactSinkPublicationFailureLeavesExistingDestinationAndEgressUnchanged) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "operator", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "operator", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "operator", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("operator", {data_input("input", 0U, "edge.input", image_type)},
                                      {data_output("output", 0U, "edge.output", "pool.output", image_type)}),
                    .canonical_config = "{\"mode\":\"mirror-input\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();

  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"mirror-input\"}").ok());
  const std::array<float, 4U> pixels{1.0F, 2.0F, 3.0F, 4.0F};
  ASSERT_TRUE(publish_float32_image_ingress(*executor_value, "input",
                                            {.semantic_key_hash = 59U, .order_key = 61U, .item_ordinal = 67U}, pixels)
                .ok());
  auto provider_invocation = provider.invocation("operator", "{\"mode\":\"mirror-input\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("operator", invocation(std::move(provider_invocation).value())).ok());

  auto acquired = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(acquired.ok()) << acquired.status();
  auto egress = std::move(acquired).value();
  ASSERT_TRUE(egress.valid());

  TemporaryArtifactSinkPath failing_output{.path = artifact_sink_test_path("unpublished_terminal_image.mrd")};
  ASSERT_TRUE(std::filesystem::create_directory(failing_output.path));
  ksj::recon::runtime::IsmrmrdImageArtifactSink sink(failing_output.path, artifact_sink_descriptor());
  const auto committed = sink.commit(egress);

  EXPECT_FALSE(committed.ok());
  EXPECT_EQ(ksj::base::StatusCode::io_error, committed.code());
  EXPECT_NE(std::string::npos, committed.message().find("unable to publish ISMRMRD image artifact"));
  EXPECT_TRUE(egress.valid());
  EXPECT_TRUE(std::filesystem::is_directory(failing_output.path));
  const auto temporary_prefix = failing_output.path.filename().string() + ".tmp.";
  std::error_code iteration_error;
  for (const auto& entry : std::filesystem::directory_iterator(failing_output.path.parent_path(), iteration_error)) {
    EXPECT_FALSE(entry.path().filename().string().starts_with(temporary_prefix));
  }
  EXPECT_FALSE(iteration_error);
  ASSERT_TRUE(egress.acknowledge_consumed().ok());
  EXPECT_FALSE(egress.valid());
  EXPECT_EQ(FixedBufferEdgePollKind::empty, executor_value->egress_poll_kind("images"));
}

TEST(SynchronousGraphExecutor, IsmrmrdImageArtifactSinkRejectsNegativeMagnitudeWithoutAcknowledgingEgress) {
  auto image = ksj::recon::types::image_frame();
  ASSERT_TRUE(image.ok()) << image.status();
  const auto image_type = std::move(image).value();

  GraphPlanBuilder builder;
  ASSERT_TRUE(builder.add_pool("pool.input", SynchronousDataEndpointKind::ingress, "input", "", image_type).ok());
  ASSERT_TRUE(
    builder.add_pool("pool.output", SynchronousDataEndpointKind::node, "operator", "output", image_type).ok());
  ASSERT_TRUE(builder
                .add_edge("edge.input", "pool.input", SynchronousDataEndpointKind::ingress, "input", "", 0U,
                          SynchronousDataEndpointKind::node, "operator", "input", 0U, image_type)
                .ok());
  ASSERT_TRUE(builder
                .add_edge("edge.output", "pool.output", SynchronousDataEndpointKind::node, "operator", "output", 0U,
                          SynchronousDataEndpointKind::egress, "images", "", 0U, image_type)
                .ok());
  auto artifacts =
    builder.build({{.plan = make_node("operator", {data_input("input", 0U, "edge.input", image_type)},
                                      {data_output("output", 0U, "edge.output", "pool.output", image_type)}),
                    .canonical_config = "{\"mode\":\"mirror-input\"}"}});
  ASSERT_TRUE(artifacts.ok()) << artifacts.status();
  auto artifacts_value = std::move(artifacts).value();
  auto slabs = GraphSlabs::create(artifacts_value.plan);
  ASSERT_TRUE(slabs.ok()) << slabs.status();
  auto slabs_value = std::move(slabs).value();
  auto executor = make_executor(artifacts_value, slabs_value);
  ASSERT_TRUE(executor.ok()) << executor.status();
  auto executor_value = std::move(executor).value();

  ProviderInstance provider;
  ASSERT_TRUE(initialize_provider(provider, "{\"mode\":\"mirror-input\"}").ok());
  ASSERT_TRUE(publish_float32_image_ingress(*executor_value, "input",
                                            {.semantic_key_hash = 71U, .order_key = 73U, .item_ordinal = 79U},
                                            {-1.0F, 2.0F, 3.0F, 4.0F})
                .ok());
  auto provider_invocation = provider.invocation("operator", "{\"mode\":\"mirror-input\"}");
  ASSERT_TRUE(provider_invocation.ok()) << provider_invocation.status();
  ASSERT_TRUE(executor_value->try_fire("operator", invocation(std::move(provider_invocation).value())).ok());

  auto acquired = executor_value->try_acquire_egress("images");
  ASSERT_TRUE(acquired.ok()) << acquired.status();
  auto egress = std::move(acquired).value();
  TemporaryArtifactSinkPath output{.path = artifact_sink_test_path("negative_magnitude.mrd")};
  ksj::recon::runtime::IsmrmrdImageArtifactSink sink(output.path, artifact_sink_descriptor());
  const auto committed = sink.commit(egress);

  EXPECT_FALSE(committed.ok());
  EXPECT_EQ(ksj::base::StatusCode::validation_error, committed.code());
  EXPECT_TRUE(egress.valid());
  EXPECT_FALSE(std::filesystem::exists(output.path));
  ASSERT_TRUE(egress.acknowledge_consumed().ok());
}

} // namespace
