#include "kspacejet/recon/runtime/provider_node_instance.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace ksj::recon::runtime {
namespace {

[[nodiscard]] ksj_provider_abi_header abi_header(const std::uint32_t size) noexcept {
  return ksj_provider_abi_header_make(size, 0U);
}

[[nodiscard]] ksj_utf8_view utf8_view(const std::string_view value) noexcept {
  ksj_utf8_view result{};
  result.abi = abi_header(sizeof(result));
  result.data = value.data();
  result.size = static_cast<std::uint64_t>(value.size());
  return result;
}

[[nodiscard]] ksj_byte_view byte_view(const ksj::base::ConstByteSpan value) noexcept {
  ksj_byte_view result{};
  result.abi = abi_header(sizeof(result));
  result.data = value.data();
  result.size = static_cast<std::uint64_t>(value.size());
  return result;
}

[[nodiscard]] ksj_error_view error_storage() noexcept {
  ksj_error_view result{};
  result.abi = abi_header(sizeof(result));
  result.message.abi = abi_header(sizeof(result.message));
  return result;
}

[[nodiscard]] std::uint8_t hex_value(const char value) noexcept {
  if (value >= '0' && value <= '9')
    return static_cast<std::uint8_t>(value - '0');
  if (value >= 'a' && value <= 'f')
    return static_cast<std::uint8_t>(10U + value - 'a');
  return 0xFFU;
}

[[nodiscard]] ksj::base::Result<ksj::provider::loader::Digest256> loader_digest(const ArtifactDigest& digest,
                                                                                const std::string_view field_name) {
  constexpr std::size_t kPrefixSize = 7U;
  constexpr std::size_t kHexSize = KSJ_PROVIDER_DIGEST256_SIZE * 2U;
  const auto& encoded = digest.value();
  if (encoded.size() != kPrefixSize + kHexSize || !std::string_view(encoded).starts_with("sha256:")) {
    return ksj::base::Status::ValidationError(std::string(field_name) + " is not a Provider ABI digest");
  }
  ksj::provider::loader::Digest256 result{};
  for (std::size_t index = 0U; index < result.size(); ++index) {
    const auto high = hex_value(encoded[kPrefixSize + index * 2U]);
    const auto low = hex_value(encoded[kPrefixSize + index * 2U + 1U]);
    if (high == 0xFFU || low == 0xFFU) {
      return ksj::base::Status::ValidationError(std::string(field_name) + " has invalid hexadecimal bytes");
    }
    result[index] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  return result;
}

[[nodiscard]] ksj::base::Result<ksj_digest256> abi_digest(const ArtifactDigest& digest,
                                                          const std::string_view field_name) {
  auto bytes = loader_digest(digest, field_name);
  if (!bytes.ok())
    return bytes.status();
  ksj_digest256 result{};
  result.abi = abi_header(sizeof(result));
  std::copy(bytes.value().begin(), bytes.value().end(), result.bytes);
  return result;
}

[[nodiscard]] bool contains_nul(const std::string_view value) noexcept {
  return value.find('\0') != std::string_view::npos;
}

[[nodiscard]] const SynchronousNodePlan* find_node(const ExecutionPlan& execution_plan,
                                                   const std::string_view node_id) noexcept {
  const auto found = std::find_if(execution_plan.synchronous_node_plans().begin(),
                                  execution_plan.synchronous_node_plans().end(), [node_id](const auto& node) {
                                    return node.node_id() == node_id;
                                  });
  return found == execution_plan.synchronous_node_plans().end() ? nullptr : &*found;
}

[[nodiscard]] const OperatorPlanBinding* find_config_binding(const ExecutionPlan& execution_plan,
                                                             const std::string_view node_id) noexcept {
  const auto found = std::find_if(execution_plan.operator_plan_bindings().begin(),
                                  execution_plan.operator_plan_bindings().end(), [node_id](const auto& binding) {
                                    return binding.node_id() == node_id;
                                  });
  return found == execution_plan.operator_plan_bindings().end() ? nullptr : &*found;
}

[[nodiscard]] const ksj::provider::loader::OperatorDescriptor*
find_operator(const ksj::provider::loader::ProviderDescriptor& descriptor,
              const std::string_view operator_id) noexcept {
  const auto found =
    std::find_if(descriptor.operators.begin(), descriptor.operators.end(), [operator_id](const auto& candidate) {
      return candidate.operator_id == operator_id;
    });
  return found == descriptor.operators.end() ? nullptr : &*found;
}

[[nodiscard]] ksj::base::Result<std::uint64_t> planned_output_capacity_bytes(const ExecutionPlan& execution_plan,
                                                                             const SynchronousNodePlan& node_plan) {
  std::uint64_t total{0U};
  for (const auto& output : node_plan.outputs()) {
    const auto pool =
      std::find_if(execution_plan.synchronous_buffer_pool_plans().begin(),
                   execution_plan.synchronous_buffer_pool_plans().end(), [&output](const auto& candidate) {
                     return candidate.pool_id() == output.pool_id();
                   });
    if (pool == execution_plan.synchronous_buffer_pool_plans().end()) {
      return ksj::base::Status::ValidationError(
        "ProviderNodeInstance node output does not name a frozen synchronous buffer pool");
    }
    if (pool->payload_capacity_bytes() > std::numeric_limits<std::uint64_t>::max() - total) {
      return ksj::base::Status::ValidationError("ProviderNodeInstance output payload capacity overflows");
    }
    total += pool->payload_capacity_bytes();
    if (pool->metadata_capacity_bytes() > std::numeric_limits<std::uint64_t>::max() - total) {
      return ksj::base::Status::ValidationError("ProviderNodeInstance output metadata capacity overflows");
    }
    total += pool->metadata_capacity_bytes();
  }
  return total;
}

[[nodiscard]] ksj::base::Status validate_config(const ExecutionPlan& execution_plan,
                                                const ProviderNodeInstanceConfig& config,
                                                const SynchronousNodePlan*& node_plan,
                                                ArtifactDigest& canonical_config_digest) {
  if (config.module_path.empty() || config.node_id.empty() || config.canonical_config.empty()) {
    return ksj::base::Status::InvalidArgument(
      "ProviderNodeInstance requires a module path, node id, and canonical configuration");
  }
  if (contains_nul(config.node_id) || contains_nul(config.canonical_config) || config.start_facts.run_id.empty() ||
      config.start_facts.scan_instance_id.empty() || contains_nul(config.start_facts.run_id) ||
      contains_nul(config.start_facts.scan_instance_id)) {
    return ksj::base::Status::InvalidArgument(
      "ProviderNodeInstance node/config/run/scan identifiers must be non-empty and contain no NUL bytes");
  }
  if (config.execution_context_id == 0U || config.max_backend_concurrency == 0U || config.key_state.generation == 0U ||
      config.start_facts.terminal_epoch == 0U) {
    return ksj::base::Status::InvalidArgument(
      "ProviderNodeInstance execution context, key-state generation, and terminal epoch must be non-zero");
  }
  if (config.start_facts.execution_plan_digest != execution_plan.digest()) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance start execution-plan digest does not match the supplied ExecutionPlan");
  }
  if (config.start_facts.normalized_scan_facts_digest != execution_plan.inputs().scan_facts()) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance normalized scan-facts digest does not match the ExecutionPlan scan facts");
  }
  node_plan = find_node(execution_plan, config.node_id);
  if (node_plan == nullptr) {
    return ksj::base::Status::NotFound("ProviderNodeInstance node_id is absent from the synchronous ExecutionPlan");
  }
  auto digest =
    derive_canonical_config_digest(config.canonical_config, "ProviderNodeInstance canonical configuration digest");
  if (!digest.ok())
    return digest.status();
  canonical_config_digest = std::move(digest).value();
  const auto* const binding = find_config_binding(execution_plan, config.node_id);
  if (binding == nullptr || binding->canonical_config_digest() != canonical_config_digest) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance canonical configuration does not match the frozen node binding");
  }
  return ksj::base::Status::Ok();
}

[[nodiscard]] ksj::base::Status provider_status_failure(const std::string_view operation,
                                                        const ksj_status provider_status) {
  return ksj::base::Status::ValidationError("ProviderNodeInstance Provider " + std::string(operation) +
                                            " returned status " + std::to_string(provider_status));
}

} // namespace

ProviderNodeInstance::ProviderNodeInstance(ProviderNodeInstanceConfig config, ArtifactDigest canonical_config_digest,
                                           std::string operator_id) noexcept
    : config_(std::move(config)), canonical_config_digest_(std::move(canonical_config_digest)),
      operator_id_(std::move(operator_id)) {}

ksj::base::Result<std::unique_ptr<ProviderNodeInstance>>
ProviderNodeInstance::create(const ExecutionPlan& execution_plan, ProviderNodeInstanceConfig config) {
  const SynchronousNodePlan* node_plan = nullptr;
  ArtifactDigest canonical_config_digest = config.start_facts.execution_plan_digest;
  const auto config_status = validate_config(execution_plan, config, node_plan, canonical_config_digest);
  if (!config_status.ok())
    return config_status;
  try {
    auto result = std::unique_ptr<ProviderNodeInstance>(
      new ProviderNodeInstance(std::move(config), std::move(canonical_config_digest), node_plan->operator_id()));
    const auto initialized = result->initialize(execution_plan, *node_plan);
    if (!initialized.ok())
      return initialized;
    return result;
  } catch (const std::bad_alloc&) {
    return ksj::base::Status::OutOfMemory("unable to allocate ProviderNodeInstance");
  }
}

ProviderNodeInstance::~ProviderNodeInstance() {
  destroy_noexcept();
}

ksj::base::Status ProviderNodeInstance::initialize(const ExecutionPlan& execution_plan,
                                                   const SynchronousNodePlan& node_plan) {
  auto required_bundle =
    loader_digest(node_plan.provider_bundle_digest(), "ProviderNodeInstance provider bundle digest");
  if (!required_bundle.ok())
    return required_bundle.status();
  ksj::provider::loader::ProviderLoadOptions options;
  options.required_bundle_digest.emplace(std::move(required_bundle).value());
  auto loaded = ksj::provider::loader::ProviderModule::load(config_.module_path, std::move(options));
  if (!loaded.ok())
    return loaded.status();
  module_ = std::move(loaded).value();
  lease_ = module_.acquire();
  const auto* const descriptor = lease_.descriptor();
  const auto* const api = lease_.api();
  if (descriptor == nullptr || api == nullptr || descriptor->provider_id != node_plan.provider_id()) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance loaded module does not expose the frozen Provider identity");
  }
  const auto* const operator_descriptor = find_operator(*descriptor, node_plan.operator_id());
  if (operator_descriptor == nullptr) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance loaded Provider does not expose the frozen Operator identity");
  }
  auto output_capacity = planned_output_capacity_bytes(execution_plan, node_plan);
  if (!output_capacity.ok())
    return output_capacity.status();
  if (operator_descriptor->max_input_items_per_firing < node_plan.firing().maximum_input_items() ||
      operator_descriptor->max_output_items_per_firing < node_plan.firing().maximum_output_grants() ||
      operator_descriptor->max_output_bytes_per_firing < output_capacity.value() ||
      operator_descriptor->max_scratch_bytes_per_firing < node_plan.firing().maximum_scratch_bytes()) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance loaded Operator descriptor cannot cover the frozen synchronous node bounds");
  }

  ksj_operator_create_request create{};
  create.abi = abi_header(sizeof(create));
  create.operator_id = utf8_view(operator_id_);
  create.canonical_config = byte_view(ksj::base::ConstByteSpan{
    reinterpret_cast<const ksj::base::byte*>(config_.canonical_config.data()), config_.canonical_config.size()});
  auto error = error_storage();
  ksj_status provider_status = KSJ_STATUS_INTERNAL_ERROR;
  try {
    provider_status = api->operator_create(&create, &operator_handle_, &error);
  } catch (...) {
    return ksj::base::Status::InternalError("ProviderNodeInstance Provider threw across operator_create");
  }
  if (provider_status != KSJ_STATUS_OK || operator_handle_ == nullptr) {
    return provider_status_failure("operator_create", provider_status);
  }

  ksj_execution_context_descriptor context{};
  context.abi = abi_header(sizeof(context));
  context.numa_node = config_.numa_node;
  context.device_ordinal = config_.device_ordinal;
  context.execution_context_id = config_.execution_context_id;
  context.resource_domain_id = config_.resource_domain_id;
  context.max_backend_concurrency = config_.max_backend_concurrency;
  error = error_storage();
  try {
    provider_status = api->execution_context_create(operator_handle_, &context, &execution_context_, &error);
  } catch (...) {
    return ksj::base::Status::InternalError("ProviderNodeInstance Provider threw across execution_context_create");
  }
  if (provider_status != KSJ_STATUS_OK || execution_context_ == nullptr) {
    return provider_status_failure("execution_context_create", provider_status);
  }

  ksj_key_state_descriptor key_state{};
  key_state.abi = abi_header(sizeof(key_state));
  key_state.semantic_key = byte_view(ksj::base::ConstByteSpan{config_.key_state.semantic_key});
  key_state.placement_key = config_.key_state.placement_key;
  key_state.key_state_generation = config_.key_state.generation;
  key_state.home_shard = config_.key_state.home_shard;
  error = error_storage();
  try {
    provider_status = api->key_state_init(operator_handle_, execution_context_, &key_state, &key_state_, &error);
  } catch (...) {
    return ksj::base::Status::InternalError("ProviderNodeInstance Provider threw across key_state_init");
  }
  if (provider_status != KSJ_STATUS_OK || key_state_ == nullptr) {
    return provider_status_failure("key_state_init", provider_status);
  }

  auto normalized_scan_digest =
    abi_digest(config_.start_facts.normalized_scan_facts_digest, "ProviderNodeInstance normalized scan-facts digest");
  if (!normalized_scan_digest.ok())
    return normalized_scan_digest.status();
  auto execution_plan_digest =
    abi_digest(config_.start_facts.execution_plan_digest, "ProviderNodeInstance execution-plan digest");
  if (!execution_plan_digest.ok())
    return execution_plan_digest.status();
  ksj_scan_start_descriptor start{};
  start.abi = abi_header(sizeof(start));
  start.run_id = utf8_view(config_.start_facts.run_id);
  start.scan_id = utf8_view(config_.start_facts.scan_instance_id);
  start.normalized_scan_facts_digest = std::move(normalized_scan_digest).value();
  start.execution_plan_digest = std::move(execution_plan_digest).value();
  start.terminal_epoch = config_.start_facts.terminal_epoch;
  error = error_storage();
  try {
    provider_status = api->operator_on_start(operator_handle_, execution_context_, key_state_, &start, &error);
  } catch (...) {
    return ksj::base::Status::InternalError("ProviderNodeInstance Provider threw across operator_on_start");
  }
  if (provider_status != KSJ_STATUS_OK)
    return provider_status_failure("operator_on_start", provider_status);
  started_ = true;
  return ksj::base::Status::Ok();
}

ksj::base::Result<SynchronousProviderInvocation> ProviderNodeInstance::invocation() const {
  if (!started_)
    return ksj::base::Status::StateError("ProviderNodeInstance is not started");
  if (normal_terminal_completed_ || cancellation_invoked_) {
    return ksj::base::Status::StateError("ProviderNodeInstance has already reached a terminal lifecycle state");
  }
  if (!lease_.valid() || operator_handle_ == nullptr || execution_context_ == nullptr || key_state_ == nullptr) {
    return ksj::base::Status::StateError("ProviderNodeInstance Provider lifecycle handles are unavailable");
  }
  return SynchronousProviderInvocation{.provider = lease_,
                                       .node_id = config_.node_id,
                                       .operator_id = operator_id_,
                                       .canonical_config_digest = canonical_config_digest_,
                                       .operator_handle = operator_handle_,
                                       .execution_context = execution_context_,
                                       .key_state = key_state_};
}

ksj::base::Status ProviderNodeInstance::complete_normal_terminal(const SynchronousFiringResult& terminal_result) {
  if (!started_ || cancellation_invoked_ || normal_terminal_completed_) {
    return ksj::base::Status::StateError(
      "ProviderNodeInstance normal terminal completion is invalid in its current lifecycle state");
  }
  if (terminal_result.outcome != SynchronousFiringOutcome::done || terminal_result.provider_status != KSJ_STATUS_OK ||
      terminal_result.terminal_epoch != config_.start_facts.terminal_epoch) {
    return ksj::base::Status::ValidationError(
      "ProviderNodeInstance normal terminal result does not attest this node's successful terminal epoch");
  }
  normal_terminal_completed_ = true;
  return ksj::base::Status::Ok();
}

ksj::base::Status ProviderNodeInstance::cancel(const std::string_view reason,
                                               const std::uint64_t cancellation_generation) {
  if (!started_ || normal_terminal_completed_ || cancellation_invoked_) {
    return ksj::base::Status::StateError("ProviderNodeInstance cancellation is invalid in its current lifecycle state");
  }
  if (reason.empty() || contains_nul(reason) || cancellation_generation == 0U) {
    return ksj::base::Status::InvalidArgument(
      "ProviderNodeInstance cancellation requires a non-empty reason and non-zero generation");
  }
  return cancel_impl(reason, cancellation_generation);
}

ksj::base::Status ProviderNodeInstance::cancel_impl(const std::string_view reason,
                                                    const std::uint64_t cancellation_generation) noexcept {
  try {
    if (cancellation_invoked_) {
      return ksj::base::Status::StateError("ProviderNodeInstance cancellation was already invoked");
    }
    cancellation_invoked_ = true;
    const auto* const api = lease_.api();
    if (!started_ || api == nullptr || operator_handle_ == nullptr || execution_context_ == nullptr ||
        key_state_ == nullptr) {
      return ksj::base::Status::StateError("ProviderNodeInstance cannot cancel unavailable Provider lifecycle handles");
    }
    ksj_cancel_context cancellation{};
    cancellation.abi = abi_header(sizeof(cancellation));
    cancellation.kind = KSJ_PROVIDER_SCAN_END_FAILED;
    cancellation.terminal_epoch = config_.start_facts.terminal_epoch;
    cancellation.cancellation_generation = cancellation_generation;
    cancellation.reason = utf8_view(reason);
    auto error = error_storage();
    ksj_status provider_status = KSJ_STATUS_INTERNAL_ERROR;
    try {
      provider_status =
        api->operator_on_cancel(operator_handle_, execution_context_, key_state_, &cancellation, &error);
    } catch (...) {
      return ksj::base::Status::InternalError("ProviderNodeInstance Provider threw across operator_on_cancel");
    }
    return provider_status == KSJ_STATUS_OK ? ksj::base::Status::Ok()
                                            : provider_status_failure("operator_on_cancel", provider_status);
  } catch (...) {
    return ksj::base::Status::InternalError("ProviderNodeInstance cancellation bookkeeping failed");
  }
}

ProviderNodeInstanceSnapshot ProviderNodeInstance::snapshot() const noexcept {
  const auto lifecycle = normal_terminal_completed_ ? ProviderNodeLifecycle::normal_terminal_completed
                                                    : (cancellation_invoked_ ? ProviderNodeLifecycle::cancelled
                                                                             : ProviderNodeLifecycle::started);
  return {.lifecycle = lifecycle,
          .started = started_,
          .normal_terminal_completed = normal_terminal_completed_,
          .cancellation_invoked = cancellation_invoked_};
}

void ProviderNodeInstance::destroy_noexcept() noexcept {
  try {
    if (started_ && !normal_terminal_completed_ && !cancellation_invoked_) {
      static constexpr std::string_view kDestructorCancellationReason =
        "ProviderNodeInstance destroyed before normal terminal completion";
      static_cast<void>(cancel_impl(kDestructorCancellationReason, 1U));
    }
    const auto* const api = lease_.api();
    if (api == nullptr)
      return;
    if (key_state_ != nullptr) {
      try {
        api->key_state_reset(operator_handle_, execution_context_, key_state_);
      } catch (...) {}
      key_state_ = nullptr;
    }
    if (execution_context_ != nullptr) {
      try {
        api->execution_context_destroy(operator_handle_, execution_context_);
      } catch (...) {}
      execution_context_ = nullptr;
    }
    if (operator_handle_ != nullptr) {
      try {
        api->operator_destroy(operator_handle_);
      } catch (...) {}
      operator_handle_ = nullptr;
    }
  } catch (...) {
    // Destruction cannot safely retry Provider lifecycle cleanup after an
    // unexpected host-side failure.  Never let it escape a destructor.
  }
}

} // namespace ksj::recon::runtime
