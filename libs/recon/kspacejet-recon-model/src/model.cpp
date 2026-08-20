#include "kspacejet/recon/model.hpp"

#include "kspacejet/recon/type_registry.hpp"

#include "utf8.hpp"

#include <ismrmrd/xml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace ksj::recon {
namespace {

[[nodiscard]] Status invalid(std::string message) {
  return Status::InvalidArgument(std::move(message));
}

[[nodiscard]] Status validation(std::string message) {
  return Status::ValidationError(std::move(message));
}

[[nodiscard]] Status parse_error(std::string message) {
  return Status::ParseError(std::move(message));
}

[[nodiscard]] std::string field(std::string_view prefix, std::string_view suffix) {
  std::string result(prefix);
  if (!result.empty() && !suffix.empty()) {
    result.push_back('.');
  }
  result.append(suffix);
  return result;
}

[[nodiscard]] bool has_nonempty_unique_strings(const std::vector<std::string>& values) {
  if (values.empty()) {
    return false;
  }
  std::vector<std::string_view> sorted;
  sorted.reserve(values.size());
  for (const auto& value : values) {
    if (value.empty()) {
      return false;
    }
    sorted.emplace_back(value);
  }
  std::sort(sorted.begin(), sorted.end());
  return std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end();
}

template <typename T> [[nodiscard]] bool has_unique_values(const std::vector<T>& values) {
  for (std::size_t left = 0; left < values.size(); ++left) {
    for (std::size_t right = left + 1; right < values.size(); ++right) {
      if (values[left] == values[right]) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] Result<CanonicalQuantity> required_quantity(const Quantity value, const std::string_view name) {
  if (value == 0) {
    return invalid(std::string(name) + " must be greater than zero.");
  }
  return CanonicalQuantity::create(value, name);
}

[[nodiscard]] Result<CanonicalQuantity> canonical_quantity(const Quantity value, const std::string_view name) {
  return CanonicalQuantity::create(value, name);
}

[[nodiscard]] Result<MatrixDimensions> to_matrix_dimensions(const ISMRMRD::MatrixSize& source,
                                                            const std::string_view name) {
  if (source.x == 0 || source.y == 0 || source.z == 0) {
    return parse_error(std::string(name) + " must have non-zero x, y, and z dimensions.");
  }
  return MatrixDimensions{source.x, source.y, source.z};
}

[[nodiscard]] Result<FieldOfViewMm> to_field_of_view(const ISMRMRD::FieldOfView_mm& source,
                                                     const std::string_view name) {
  const auto valid = [](const float value) {
    return std::isfinite(value) && value > 0.0F;
  };
  if (!valid(source.x) || !valid(source.y) || !valid(source.z)) {
    return parse_error(std::string(name) + " must have finite positive x, y, and z values.");
  }
  return FieldOfViewMm{source.x, source.y, source.z};
}

[[nodiscard]] Result<IndexLimit> to_index_limit(const ISMRMRD::Limit& source, const std::string_view name) {
  return IndexLimit::create(source.minimum, source.maximum, source.center, name);
}

[[nodiscard]] TrajectoryType to_trajectory_type(const ISMRMRD::TrajectoryType trajectory) noexcept {
  switch (trajectory) {
    case ISMRMRD::TrajectoryType::CARTESIAN:
      return TrajectoryType::cartesian;
    case ISMRMRD::TrajectoryType::EPI:
      return TrajectoryType::epi;
    case ISMRMRD::TrajectoryType::RADIAL:
      return TrajectoryType::radial;
    case ISMRMRD::TrajectoryType::GOLDENANGLE:
      return TrajectoryType::golden_angle;
    case ISMRMRD::TrajectoryType::SPIRAL:
      return TrajectoryType::spiral;
    case ISMRMRD::TrajectoryType::OTHER:
      return TrajectoryType::other;
  }
  return TrajectoryType::other;
}

template <typename OptionalLimit>
[[nodiscard]] Result<void*> copy_optional_limit(const OptionalLimit& source,
                                                std::array<std::optional<IndexLimit>, 18>& destination,
                                                const EncodingLimitDimension dimension, const std::string_view name) {
  if (!source) {
    return static_cast<void*>(nullptr);
  }
  auto limit = to_index_limit(*source, name);
  if (!limit.ok()) {
    return limit.status();
  }
  const auto index = static_cast<std::size_t>(dimension);
  destination[index] = std::move(limit).value();
  return static_cast<void*>(nullptr);
}

[[nodiscard]] Result<EncodingLimits> to_encoding_limits(const ISMRMRD::EncodingLimits& source,
                                                        const std::string_view name) {
  std::array<std::optional<IndexLimit>, 18> limits;

  struct LimitBinding {
    const ISMRMRD::Optional<ISMRMRD::Limit>* source;
    EncodingLimitDimension dimension;
    std::string_view name;
  };
  const std::array bindings{
    LimitBinding{&source.kspace_encoding_step_0, EncodingLimitDimension::kspace_encode_step_0, "kspace_encode_step_0"},
    LimitBinding{&source.kspace_encoding_step_1, EncodingLimitDimension::kspace_encode_step_1, "kspace_encode_step_1"},
    LimitBinding{&source.kspace_encoding_step_2, EncodingLimitDimension::kspace_encode_step_2, "kspace_encode_step_2"},
    LimitBinding{&source.average, EncodingLimitDimension::average, "average"},
    LimitBinding{&source.slice, EncodingLimitDimension::slice, "slice"},
    LimitBinding{&source.contrast, EncodingLimitDimension::contrast, "contrast"},
    LimitBinding{&source.phase, EncodingLimitDimension::phase, "phase"},
    LimitBinding{&source.repetition, EncodingLimitDimension::repetition, "repetition"},
    LimitBinding{&source.set, EncodingLimitDimension::set, "set"},
    LimitBinding{&source.segment, EncodingLimitDimension::segment, "segment"},
  };
  for (const auto& binding : bindings) {
    auto copied = copy_optional_limit(*binding.source, limits, binding.dimension, field(name, binding.name));
    if (!copied.ok()) {
      return copied.status();
    }
  }
  for (std::size_t index = 0; index < std::size(source.user); ++index) {
    const auto dimension =
      static_cast<EncodingLimitDimension>(static_cast<std::size_t>(EncodingLimitDimension::user_0) + index);
    auto copied =
      copy_optional_limit(source.user[index], limits, dimension, field(name, "user_" + std::to_string(index)));
    if (!copied.ok()) {
      return copied.status();
    }
  }
  return EncodingLimits::from_validated(std::move(limits));
}

[[nodiscard]] Result<EncodingDescriptor> to_encoding_descriptor(const ISMRMRD::Encoding& source,
                                                                const std::size_t index) {
  const std::string prefix = "encoding[" + std::to_string(index) + "]";

  auto encoded_matrix = to_matrix_dimensions(source.encodedSpace.matrixSize, field(prefix, "encoded_matrix"));
  if (!encoded_matrix.ok()) {
    return encoded_matrix.status();
  }
  auto encoded_fov = to_field_of_view(source.encodedSpace.fieldOfView_mm, field(prefix, "encoded_field_of_view_mm"));
  if (!encoded_fov.ok()) {
    return encoded_fov.status();
  }
  auto recon_matrix = to_matrix_dimensions(source.reconSpace.matrixSize, field(prefix, "recon_matrix"));
  if (!recon_matrix.ok()) {
    return recon_matrix.status();
  }
  auto recon_fov = to_field_of_view(source.reconSpace.fieldOfView_mm, field(prefix, "recon_field_of_view_mm"));
  if (!recon_fov.ok()) {
    return recon_fov.status();
  }
  auto limits = to_encoding_limits(source.encodingLimits, field(prefix, "limits"));
  if (!limits.ok()) {
    return limits.status();
  }

  return EncodingDescriptor::from_validated(std::move(encoded_matrix).value(), std::move(encoded_fov).value(),
                                            std::move(recon_matrix).value(), std::move(recon_fov).value(),
                                            to_trajectory_type(source.trajectory), std::move(limits).value());
}

[[nodiscard]] bool is_lower_hex_digest(std::string_view value) noexcept {
  constexpr std::string_view prefix = "sha256:";
  if (!value.starts_with(prefix) || value.size() != prefix.size() + 64U) {
    return false;
  }
  for (const char character : value.substr(prefix.size())) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Result<PlanInputDigests> to_plan_input_digests(const PlanInputDigestSpec& specification) {
  auto resolved_pipeline = ArtifactDigest::parse(specification.resolved_pipeline, "inputs.resolved_pipeline");
  if (!resolved_pipeline.ok()) {
    return resolved_pipeline.status();
  }
  auto scan_descriptor = ArtifactDigest::parse(specification.scan_descriptor, "inputs.scan_descriptor");
  if (!scan_descriptor.ok()) {
    return scan_descriptor.status();
  }
  auto target_envelope = ArtifactDigest::parse(specification.target_envelope, "inputs.target_envelope");
  if (!target_envelope.ok()) {
    return target_envelope.status();
  }
  auto machine_policy = ArtifactDigest::parse(specification.machine_policy, "inputs.machine_policy");
  if (!machine_policy.ok()) {
    return machine_policy.status();
  }
  return PlanInputDigests::from_validated(std::move(resolved_pipeline).value(), std::move(scan_descriptor).value(),
                                          std::move(target_envelope).value(), std::move(machine_policy).value());
}

[[nodiscard]] Result<std::vector<OperatorPlanBinding>>
to_operator_plan_bindings(const std::vector<OperatorPlanBindingSpec>& specifications) {
  if (specifications.empty()) {
    return validation("operator_plan_bindings must contain one binding for every planned node.");
  }

  std::vector<OperatorPlanBinding> bindings;
  bindings.reserve(specifications.size());
  std::vector<std::string_view> node_ids;
  node_ids.reserve(specifications.size());
  for (std::size_t index = 0U; index < specifications.size(); ++index) {
    const auto& specification = specifications[index];
    const auto prefix = "operator_plan_bindings[" + std::to_string(index) + "]";
    if (specification.node_id.empty()) {
      return validation(prefix + ".node_id must not be empty.");
    }
    auto digest = ArtifactDigest::parse(specification.canonical_config_digest, prefix + ".canonical_config_digest");
    if (!digest.ok()) {
      return digest.status();
    }
    node_ids.emplace_back(specification.node_id);
    bindings.push_back(OperatorPlanBinding::from_validated(specification.node_id, std::move(digest).value()));
  }
  std::sort(node_ids.begin(), node_ids.end());
  if (std::adjacent_find(node_ids.begin(), node_ids.end()) != node_ids.end()) {
    return validation("operator_plan_bindings must not contain duplicate node_id values.");
  }
  std::ranges::sort(bindings, {}, &OperatorPlanBinding::node_id);
  return bindings;
}

[[nodiscard]] bool type_allows_host_normal(const TypeDescriptor& type_descriptor) {
  return std::find(type_descriptor.allowed_memory_domains().begin(), type_descriptor.allowed_memory_domains().end(),
                   TypeMemoryDomain::host_normal) != type_descriptor.allowed_memory_domains().end();
}

[[nodiscard]] Status validate_synchronous_pool_type(const TypeDescriptor& type_descriptor,
                                                    const std::string_view prefix) {
  if (type_descriptor.payload_kind() != PayloadKind::buffer_handle ||
      type_descriptor.mutability() != PayloadMutability::immutable_after_publish ||
      !type_allows_host_normal(type_descriptor)) {
    return validation(std::string(prefix) +
                      ".type_descriptor must be an immutable_after_publish buffer_handle that permits host_normal.");
  }
  auto registered = types::resolve(type_descriptor.type_ref().value());
  if (!registered.ok()) {
    return validation(std::string(prefix) + ".type_descriptor.type_ref must resolve in the checked-in type registry.");
  }
  if (!type_descriptor.exactly_matches(registered.value())) {
    return validation(std::string(prefix) +
                      ".type_descriptor must exactly match its checked-in TypeRef registry entry.");
  }
  return Status::Ok();
}

[[nodiscard]] bool is_valid(const ExecutionProfile value) noexcept {
  switch (value) {
    case ExecutionProfile::offline_reference:
    case ExecutionProfile::bounded_reconstruction_graph:
    case ExecutionProfile::provider_development:
    case ExecutionProfile::embedded_incremental:
    case ExecutionProfile::isolated_provider_runtime:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const PortDirection value) noexcept {
  return value == PortDirection::input || value == PortDirection::output;
}

[[nodiscard]] bool is_valid(const InputGranularity value) noexcept {
  switch (value) {
    case InputGranularity::acquisition:
    case InputGranularity::microbatch:
    case InputGranularity::window:
    case InputGranularity::frame:
    case InputGranularity::volume:
    case InputGranularity::scan_finalizer:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const RateKind value) noexcept {
  return value == RateKind::sdf || value == RateKind::csdf || value == RateKind::keyed_dynamic;
}

[[nodiscard]] bool is_valid(const CalibrationRole value) noexcept {
  return value == CalibrationRole::none || value == CalibrationRole::producer || value == CalibrationRole::consumer;
}

[[nodiscard]] bool is_valid(const MemoryDomain value) noexcept {
  return value == MemoryDomain::host || value == MemoryDomain::pinned_host || value == MemoryDomain::device ||
         value == MemoryDomain::shared;
}

[[nodiscard]] bool is_valid(const SchedulerPolicy value) noexcept {
  return value == SchedulerPolicy::fifo || value == SchedulerPolicy::fair || value == SchedulerPolicy::deadline_aware;
}

[[nodiscard]] bool is_valid(const SlowSinkPolicy value) noexcept {
  return value == SlowSinkPolicy::fail || value == SlowSinkPolicy::spool || value == SlowSinkPolicy::externally_blocked;
}

[[nodiscard]] bool is_valid(const AdmissionOutcome value) noexcept {
  return value == AdmissionOutcome::admitted || value == AdmissionOutcome::rejected;
}

[[nodiscard]] bool is_valid(const JoinProgressProof value) noexcept {
  return value == JoinProgressProof::none || value == JoinProgressProof::verified_schedule_automaton ||
         value == JoinProgressProof::cohort_reservation;
}

[[nodiscard]] Status validate_quantity(const Quantity value, const std::string_view name) {
  auto quantity = CanonicalQuantity::create(value, name);
  if (!quantity.ok()) {
    return quantity.status();
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_unique_optional_strings(const std::vector<std::string>& values,
                                                      const std::string_view name) {
  if (values.empty()) {
    return Status::Ok();
  }
  if (!has_nonempty_unique_strings(values)) {
    return validation(std::string(name) + " must contain unique non-empty identifiers.");
  }
  return Status::Ok();
}

[[nodiscard]] const ResolvedPort* find_port(const std::vector<ResolvedPort>& ports,
                                            const std::string_view name) noexcept {
  const auto found = std::find_if(ports.begin(), ports.end(), [name](const ResolvedPort& port) {
    return port.name == name;
  });
  return found == ports.end() ? nullptr : &*found;
}

[[nodiscard]] Status validate_port_rate_entries(const std::vector<PortRateSpec>& entries,
                                                const std::vector<ResolvedPort>& ports, const PortDirection direction,
                                                const std::string_view name) {
  std::vector<std::string> names;
  names.reserve(entries.size());
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    const std::string entry_name = std::string(name) + "[" + std::to_string(index) + "]";
    const auto* port = find_port(ports, entry.port_name);
    if (port == nullptr || port->direction != direction) {
      return validation(entry_name + " must refer to a declared " +
                        std::string(direction == PortDirection::input ? "input" : "output") + " port.");
    }
    if (entry.items == 0) {
      return validation(entry_name + ".items must be greater than zero.");
    }
    auto items = validate_quantity(entry.items, entry_name + ".items");
    if (!items.ok()) {
      return items;
    }
    auto bytes = validate_quantity(entry.charged_bytes, entry_name + ".charged_bytes");
    if (!bytes.ok()) {
      return bytes;
    }
    names.push_back(entry.port_name);
  }
  if (!names.empty() && !has_nonempty_unique_strings(names)) {
    return validation(std::string(name) + " must not repeat a port name.");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_dynamic_phase(const DynamicPhaseBoundSpec& phase, const std::vector<ResolvedPort>& ports,
                                            const bool require_firing, const std::string_view name) {
  if (require_firing && phase.max_firings == 0) {
    return validation(std::string(name) + ".max_firings must be greater than zero.");
  }
  if (!phase.outputs.empty() && phase.max_firings == 0) {
    return validation(std::string(name) + ".max_firings must be greater than zero when outputs are declared.");
  }
  auto firings = validate_quantity(phase.max_firings, field(name, "max_firings"));
  if (!firings.ok()) {
    return firings;
  }
  return validate_port_rate_entries(phase.outputs, ports, PortDirection::output, field(name, "outputs"));
}

[[nodiscard]] Status validate_node_rates(const NodeRateSpec& rates, const std::vector<ResolvedPort>& ports) {
  if (!is_valid(rates.kind)) {
    return validation("rates.kind is invalid.");
  }

  if (rates.kind == RateKind::keyed_dynamic) {
    if (!rates.static_phases.empty()) {
      return validation("keyed_dynamic rates must not declare static_phases.");
    }
    auto ordinary =
      validate_port_rate_entries(rates.ordinary.outputs, ports, PortDirection::output, "rates.ordinary.outputs");
    if (!ordinary.ok()) {
      return ordinary;
    }
    auto flush = validate_dynamic_phase(rates.normal_flush, ports, false, "rates.normal_flush");
    if (!flush.ok()) {
      return flush;
    }
    return Status::Ok();
  }

  const std::size_t expected_phase_count = rates.kind == RateKind::sdf ? 1U : 2U;
  if (rates.static_phases.size() != expected_phase_count &&
      !(rates.kind == RateKind::csdf && rates.static_phases.size() > 1U)) {
    return validation(rates.kind == RateKind::sdf ? "SDF rates must declare exactly one static phase."
                                                  : "CSDF rates must declare at least two static phases.");
  }
  if (!rates.ordinary.outputs.empty() || rates.normal_flush.max_firings != 0 || !rates.normal_flush.outputs.empty()) {
    return validation("SDF/CSDF rates must not declare keyed_dynamic phase bounds.");
  }
  for (std::size_t index = 0; index < rates.static_phases.size(); ++index) {
    const auto& phase = rates.static_phases[index];
    if (phase.inputs.empty() && phase.outputs.empty()) {
      return validation("rates.static_phases[" + std::to_string(index) + "] must not be empty.");
    }
    auto inputs = validate_port_rate_entries(phase.inputs, ports, PortDirection::input,
                                             "rates.static_phases[" + std::to_string(index) + "].inputs");
    if (!inputs.ok()) {
      return inputs;
    }
    auto outputs = validate_port_rate_entries(phase.outputs, ports, PortDirection::output,
                                              "rates.static_phases[" + std::to_string(index) + "].outputs");
    if (!outputs.ok()) {
      return outputs;
    }
  }
  return Status::Ok();
}

struct OutputAggregate {
  Quantity items = 0;
  Quantity charged_bytes = 0;
};

struct InputAggregate {
  Quantity items = 0;
  Quantity charged_bytes = 0;
};

[[nodiscard]] Result<OutputAggregate> sum_output_bounds(const std::vector<PortRateSpec>& outputs,
                                                        const std::string_view expression_name) {
  OutputAggregate aggregate;
  for (const auto& output : outputs) {
    auto items = checked_add(aggregate.items, output.items, std::string(expression_name) + ".items");
    if (!items.ok()) {
      return items.status();
    }
    auto bytes =
      checked_add(aggregate.charged_bytes, output.charged_bytes, std::string(expression_name) + ".charged_bytes");
    if (!bytes.ok()) {
      return bytes.status();
    }
    aggregate.items = items.value();
    aggregate.charged_bytes = bytes.value();
  }
  return aggregate;
}

[[nodiscard]] Result<InputAggregate> sum_input_bounds(const std::vector<PortRateSpec>& inputs,
                                                      const std::string_view expression_name) {
  InputAggregate aggregate;
  for (const auto& input : inputs) {
    auto items = checked_add(aggregate.items, input.items, std::string(expression_name) + ".items");
    if (!items.ok()) {
      return items.status();
    }
    auto bytes =
      checked_add(aggregate.charged_bytes, input.charged_bytes, std::string(expression_name) + ".charged_bytes");
    if (!bytes.ok()) {
      return bytes.status();
    }
    aggregate.items = items.value();
    aggregate.charged_bytes = bytes.value();
  }
  return aggregate;
}

// Static SDF/CSDF phases explicitly describe all input consumption.  A
// single phase firing must therefore fit in both the batch envelope and the
// scheduler's activation item cap.  keyed_dynamic deliberately has no current
// input-rate model, so it is not inferred from its output-only phase bounds.
[[nodiscard]] Status validate_static_batch_activation_feasibility(const NodeRateSpec& rates, const NodeBatchSpec& batch,
                                                                  const NodeExecutionSpec& execution) {
  if (rates.kind == RateKind::keyed_dynamic) {
    return Status::Ok();
  }
  for (std::size_t index = 0; index < rates.static_phases.size(); ++index) {
    const auto& phase = rates.static_phases[index];
    const auto name = "rates.static_phases[" + std::to_string(index) + "].inputs";
    auto aggregate = sum_input_bounds(phase.inputs, name);
    if (!aggregate.ok()) {
      return aggregate.status();
    }
    if (aggregate.value().items > batch.max_items) {
      return validation(name + " aggregate items exceed batch.max_items.");
    }
    if (aggregate.value().charged_bytes > batch.max_charged_bytes) {
      return validation(name + " aggregate charged bytes exceed batch.max_charged_bytes.");
    }
    if (aggregate.value().items > execution.max_items_per_activation) {
      return validation(name + " aggregate items exceed execution.max_items_per_activation.");
    }
  }
  return Status::Ok();
}

// `resources.output_*` is the per-ordinary-firing reservation.  Terminal
// output is deliberately separate: a normal on_scan_end bundle is reserved
// from TerminalPlanningSpec, while cancellation owns no data-output grant.
[[nodiscard]] Result<OutputAggregate> ordinary_output_bound(const NodeRateSpec& rates) {
  if (rates.kind == RateKind::keyed_dynamic) {
    return sum_output_bounds(rates.ordinary.outputs, "rates.ordinary.outputs");
  }

  OutputAggregate maximum;
  for (std::size_t index = 0; index < rates.static_phases.size(); ++index) {
    auto phase = sum_output_bounds(rates.static_phases[index].outputs,
                                   "rates.static_phases[" + std::to_string(index) + "].outputs");
    if (!phase.ok()) {
      return phase.status();
    }
    maximum.items = std::max(maximum.items, phase.value().items);
    maximum.charged_bytes = std::max(maximum.charged_bytes, phase.value().charged_bytes);
  }
  return maximum;
}

[[nodiscard]] Result<OutputAggregate> normal_flush_output_bound(const NodeRateSpec& rates) {
  if (rates.kind != RateKind::keyed_dynamic) {
    return OutputAggregate{};
  }
  auto per_firing = sum_output_bounds(rates.normal_flush.outputs, "rates.normal_flush.outputs");
  if (!per_firing.ok()) {
    return per_firing.status();
  }
  auto items =
    checked_multiply(per_firing.value().items, rates.normal_flush.max_firings, "rates.normal_flush aggregate items");
  if (!items.ok()) {
    return items.status();
  }
  auto bytes = checked_multiply(per_firing.value().charged_bytes, rates.normal_flush.max_firings,
                                "rates.normal_flush aggregate charged bytes");
  if (!bytes.ok()) {
    return bytes.status();
  }
  return OutputAggregate{.items = items.value(), .charged_bytes = bytes.value()};
}

[[nodiscard]] Status validate_output_resource_coverage(const NodeResourceRequirements& resources,
                                                       const NodeRateSpec& rates,
                                                       const TerminalPlanningSpec& terminal) {
  auto ordinary = ordinary_output_bound(rates);
  if (!ordinary.ok()) {
    return ordinary.status();
  }
  if (resources.output_items < ordinary.value().items ||
      resources.output_charged_bytes < ordinary.value().charged_bytes) {
    return validation("resources.output_items and resources.output_charged_bytes must cover the maximum aggregate "
                      "ordinary output bound of one firing.");
  }

  auto flush = normal_flush_output_bound(rates);
  if (!flush.ok()) {
    return flush.status();
  }
  if (terminal.normal_max_output_items < flush.value().items ||
      terminal.normal_max_output_charged_bytes < flush.value().charged_bytes) {
    return validation("terminal.normal_max_output_items and terminal.normal_max_output_charged_bytes must cover "
                      "the aggregate keyed-dynamic normal_flush output bound.");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_node_execution(const NodeExecutionSpec& execution) {
  if (!is_valid(execution.input_granularity)) {
    return validation("execution contains an invalid enum value.");
  }
  auto partition = validate_unique_optional_strings(execution.partition_key, "execution.partition_key");
  if (!partition.ok()) {
    return partition;
  }
  const bool partitions_channel_groups = std::find(execution.partition_key.begin(), execution.partition_key.end(),
                                                   "channel_group") != execution.partition_key.end();
  if (!execution.channel_group.has_value()) {
    if (partitions_channel_groups) {
      return validation("execution.partition_key may use channel_group only with NodeChannelGroupSpec.");
    }
  } else {
    const auto& group = *execution.channel_group;
    if (!partitions_channel_groups) {
      return validation("NodeChannelGroupSpec requires channel_group in execution.partition_key.");
    }
    if (group.channels_per_group == 0 || group.max_active_channels == 0 || group.max_groups == 0) {
      return validation("NodeChannelGroupSpec requires finite non-zero group, channel, and group-count bounds.");
    }
    for (const auto& [value, name] : std::array{
           std::pair{group.channels_per_group, "execution.channel_group.channels_per_group"},
           std::pair{group.max_active_channels, "execution.channel_group.max_active_channels"},
           std::pair{group.max_groups, "execution.channel_group.max_groups"},
         }) {
      auto status = validate_quantity(value, name);
      if (!status.ok()) {
        return status;
      }
    }
    auto required_groups = checked_ceil_divide(group.max_active_channels, group.channels_per_group,
                                               "execution.channel_group.required_groups");
    if (!required_groups.ok()) {
      return required_groups.status();
    }
    if (group.max_groups < required_groups.value()) {
      return validation("execution.channel_group.max_groups cannot cover max_active_channels.");
    }
  }
  if (execution.max_active_keys == 0 || execution.max_in_flight == 0 || execution.max_items_per_activation == 0) {
    return validation(
      "execution max_active_keys, max_in_flight, and max_items_per_activation must be greater than zero.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{execution.max_active_keys, "execution.max_active_keys"},
         std::pair{execution.max_in_flight, "execution.max_in_flight"},
         std::pair{execution.max_items_per_activation, "execution.max_items_per_activation"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_node_batch(const NodeBatchSpec& batch) {
  if (batch.min_items == 0 || batch.preferred_items == 0 || batch.max_items == 0 || batch.max_charged_bytes == 0 ||
      batch.min_items > batch.preferred_items || batch.preferred_items > batch.max_items) {
    return validation("batch must satisfy 0 < min_items <= preferred_items <= max_items and max_charged_bytes > 0.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{batch.min_items, "batch.min_items"},
         std::pair{batch.preferred_items, "batch.preferred_items"},
         std::pair{batch.max_items, "batch.max_items"},
         std::pair{batch.max_charged_bytes, "batch.max_charged_bytes"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_node_resources(const NodeResourceRequirements& resources) {
  if (!is_valid(resources.memory_domain)) {
    return validation("resources.memory_domain is invalid.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{resources.scratch_charged_bytes_per_firing, "resources.scratch_charged_bytes_per_firing"},
         std::pair{resources.per_key_state_charged_bytes, "resources.per_key_state_charged_bytes"},
         std::pair{resources.per_scan_workspace_charged_bytes, "resources.per_scan_workspace_charged_bytes"},
         std::pair{resources.retention_charged_bytes, "resources.retention_charged_bytes"},
         std::pair{resources.output_items, "resources.output_items"},
         std::pair{resources.output_charged_bytes, "resources.output_charged_bytes"},
         std::pair{resources.cpu_permits, "resources.cpu_permits"},
         std::pair{resources.backend_gang_threads, "resources.backend_gang_threads"},
         std::pair{resources.provider_private_threads, "resources.provider_private_threads"},
         std::pair{resources.external_allocation_charged_bytes, "resources.external_allocation_charged_bytes"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_node_calibration(const NodeCalibrationRequirements& calibration) {
  if (!is_valid(calibration.role)) {
    return validation("calibration contains an invalid enum value.");
  }
  const bool has_bound = calibration.max_active_keys != 0 || calibration.precalibration_horizon_items != 0 ||
                         calibration.precalibration_horizon_charged_bytes != 0 ||
                         calibration.max_calibration_frame_charged_bytes != 0 ||
                         calibration.max_decoder_staging_bytes != 0;
  if (calibration.role == CalibrationRole::none) {
    if (!calibration.binding_id.empty() || has_bound) {
      return validation("calibration role none must not declare a binding or resource bound.");
    }
    return Status::Ok();
  }
  if (calibration.binding_id.empty() || calibration.max_active_keys == 0) {
    return validation("calibration producer/consumer must declare binding_id and max_active_keys.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{calibration.max_active_keys, "calibration.max_active_keys"},
         std::pair{calibration.precalibration_horizon_items, "calibration.precalibration_horizon_items"},
         std::pair{calibration.precalibration_horizon_charged_bytes,
                   "calibration.precalibration_horizon_charged_bytes"},
         std::pair{calibration.max_calibration_frame_charged_bytes, "calibration.max_calibration_frame_charged_bytes"},
         std::pair{calibration.max_decoder_staging_bytes, "calibration.max_decoder_staging_bytes"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_node_join(const std::optional<NodeJoinSpec>& join) {
  if (!join.has_value()) {
    return Status::Ok();
  }
  const auto& specification = *join;
  if (!is_valid(specification.progress_proof)) {
    return validation("join.progress_proof is invalid.");
  }
  if (specification.max_retained_charged_bytes_aggregate == 0U) {
    return validation("join.max_retained_charged_bytes_aggregate must be greater than zero for NodeJoinSpec.");
  }
  auto bytes =
    validate_quantity(specification.max_retained_charged_bytes_aggregate, "join.max_retained_charged_bytes_aggregate");
  if (!bytes.ok()) {
    return bytes;
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_terminal_planning(const TerminalPlanningSpec& terminal) {
  for (const auto& [value, name] : std::array{
         std::pair{terminal.normal_max_output_items, "terminal.normal_max_output_items"},
         std::pair{terminal.normal_max_output_charged_bytes, "terminal.normal_max_output_charged_bytes"},
         std::pair{terminal.normal_max_async_tokens, "terminal.normal_max_async_tokens"},
         std::pair{terminal.cancel_max_async_tokens, "terminal.cancel_max_async_tokens"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

} // namespace

Result<CanonicalQuantity> CanonicalQuantity::create(const Quantity value, const std::string_view field_name) {
  if (value > kMaxCanonicalJsonInteger) {
    return invalid(std::string(field_name) + " exceeds the RFC 8785 exact-integer limit.");
  }
  return CanonicalQuantity(value);
}

Result<Quantity> checked_add(const Quantity lhs, const Quantity rhs, const std::string_view expression_name) {
  if (lhs > kMaxCanonicalJsonInteger || rhs > kMaxCanonicalJsonInteger || lhs > kMaxCanonicalJsonInteger - rhs) {
    return validation(std::string(expression_name) + " overflows the RFC 8785 exact-integer limit.");
  }
  return lhs + rhs;
}

Result<Quantity> checked_multiply(const Quantity lhs, const Quantity rhs, const std::string_view expression_name) {
  if (lhs > kMaxCanonicalJsonInteger || rhs > kMaxCanonicalJsonInteger ||
      (lhs != 0 && rhs > kMaxCanonicalJsonInteger / lhs)) {
    return validation(std::string(expression_name) + " overflows the RFC 8785 exact-integer limit.");
  }
  return lhs * rhs;
}

Result<Quantity> checked_ceil_divide(const Quantity numerator, const Quantity denominator,
                                     const std::string_view expression_name) {
  if (denominator == 0) {
    return invalid(std::string(expression_name) + " has a zero divisor.");
  }
  if (numerator > kMaxCanonicalJsonInteger || denominator > kMaxCanonicalJsonInteger) {
    return validation(std::string(expression_name) + " exceeds the RFC 8785 exact-integer limit.");
  }
  return numerator / denominator + (numerator % denominator == 0 ? 0 : 1);
}

Result<Capacity> Capacity::create(const Quantity max_items, const Quantity max_charged_bytes,
                                  const std::string_view field_name) {
  auto items = required_quantity(max_items, field(field_name, "max_items"));
  if (!items.ok()) {
    return items.status();
  }
  auto bytes = required_quantity(max_charged_bytes, field(field_name, "max_charged_bytes"));
  if (!bytes.ok()) {
    return bytes.status();
  }
  return Capacity{std::move(items).value(), std::move(bytes).value()};
}

std::string_view to_string(const ExecutionProfile profile) noexcept {
  switch (profile) {
    case ExecutionProfile::offline_reference:
      return "offline-reference";
    case ExecutionProfile::bounded_reconstruction_graph:
      return "bounded-reconstruction-graph";
    case ExecutionProfile::provider_development:
      return "provider-development";
    case ExecutionProfile::embedded_incremental:
      return "embedded-incremental";
    case ExecutionProfile::isolated_provider_runtime:
      return "isolated-provider-runtime";
  }
  return "unknown";
}

Result<ExecutionProfile> parse_execution_profile(const std::string_view value) {
  if (value == "offline-reference") {
    return ExecutionProfile::offline_reference;
  }
  if (value == "bounded-reconstruction-graph") {
    return ExecutionProfile::bounded_reconstruction_graph;
  }
  if (value == "provider-development") {
    return ExecutionProfile::provider_development;
  }
  if (value == "embedded-incremental") {
    return ExecutionProfile::embedded_incremental;
  }
  if (value == "isolated-provider-runtime") {
    return ExecutionProfile::isolated_provider_runtime;
  }
  return invalid("Unknown execution profile '" + std::string(value) + "'.");
}

bool requires_provider_isolation(const ExecutionProfile profile) noexcept {
  return profile == ExecutionProfile::isolated_provider_runtime;
}

bool is_currently_supported_in_process(const ExecutionProfile profile) noexcept {
  return profile == ExecutionProfile::offline_reference || profile == ExecutionProfile::bounded_reconstruction_graph;
}

Result<IndexLimit> IndexLimit::create(const Quantity minimum, const Quantity maximum, const Quantity center,
                                      const std::string_view field_name) {
  auto checked_minimum = canonical_quantity(minimum, field(field_name, "minimum"));
  if (!checked_minimum.ok()) {
    return checked_minimum.status();
  }
  auto checked_maximum = canonical_quantity(maximum, field(field_name, "maximum"));
  if (!checked_maximum.ok()) {
    return checked_maximum.status();
  }
  auto checked_center = canonical_quantity(center, field(field_name, "center"));
  if (!checked_center.ok()) {
    return checked_center.status();
  }
  if (minimum > maximum || center < minimum || center > maximum) {
    return validation(std::string(field_name) + " must satisfy minimum <= center <= maximum.");
  }
  return IndexLimit{std::move(checked_minimum).value(), std::move(checked_maximum).value(),
                    std::move(checked_center).value()};
}

const std::optional<IndexLimit>& EncodingLimits::at(const EncodingLimitDimension dimension) const noexcept {
  return limits_[static_cast<std::size_t>(dimension)];
}

EncodingLimits EncodingLimits::from_validated(std::array<std::optional<IndexLimit>, 18> limits) noexcept {
  EncodingLimits result;
  result.limits_ = std::move(limits);
  return result;
}

EncodingDescriptor EncodingDescriptor::from_validated(MatrixDimensions encoded_matrix,
                                                      FieldOfViewMm encoded_field_of_view_mm,
                                                      MatrixDimensions recon_matrix,
                                                      FieldOfViewMm recon_field_of_view_mm,
                                                      const TrajectoryType trajectory, EncodingLimits limits) noexcept {
  EncodingDescriptor result;
  result.encoded_matrix_ = encoded_matrix;
  result.encoded_field_of_view_mm_ = encoded_field_of_view_mm;
  result.recon_matrix_ = recon_matrix;
  result.recon_field_of_view_mm_ = recon_field_of_view_mm;
  result.trajectory_ = trajectory;
  result.limits_ = std::move(limits);
  return result;
}

Result<ScanDescriptor> ScanDescriptor::parse_ismrmrd_xml(const std::string_view xml,
                                                         const ScanDescriptorParseOptions& options) {
  if (xml.empty()) {
    return parse_error("ISMRMRD XML header must not be empty.");
  }
  if (xml.find('\0') != std::string_view::npos) {
    return parse_error("ISMRMRD XML header must not contain NUL bytes.");
  }
  auto max_xml_bytes = required_quantity(options.max_xml_bytes, "parse_options.max_xml_bytes");
  if (!max_xml_bytes.ok()) {
    return max_xml_bytes.status();
  }
  if (xml.size() > kMaxCanonicalJsonInteger) {
    return validation("ISMRMRD XML header exceeds the RFC 8785 exact-integer limit.");
  }
  auto source_xml_bytes = CanonicalQuantity::create(static_cast<Quantity>(xml.size()), "ISMRMRD XML byte length");
  if (!source_xml_bytes.ok()) {
    return source_xml_bytes.status();
  }
  if (source_xml_bytes.value().value() > max_xml_bytes.value().value()) {
    return validation("ISMRMRD XML header exceeds parse_options.max_xml_bytes.");
  }

  try {
    ISMRMRD::IsmrmrdHeader header;
    const std::string nul_terminated_xml(xml);
    ISMRMRD::deserialize(nul_terminated_xml.c_str(), header);
    if (header.encoding.empty()) {
      return parse_error("ISMRMRD XML header must declare at least one encoding.");
    }

    std::vector<EncodingDescriptor> encodings;
    encodings.reserve(header.encoding.size());
    for (std::size_t index = 0; index < header.encoding.size(); ++index) {
      auto encoding = to_encoding_descriptor(header.encoding[index], index);
      if (!encoding.ok()) {
        return encoding.status();
      }
      encodings.push_back(std::move(encoding).value());
    }

    std::optional<Quantity> receiver_channels;
    if (header.acquisitionSystemInformation && header.acquisitionSystemInformation->receiverChannels) {
      const Quantity channels = *header.acquisitionSystemInformation->receiverChannels;
      if (channels == 0) {
        return parse_error("ISMRMRD receiverChannels must be greater than zero when declared.");
      }
      receiver_channels = channels;
    }
    return ScanDescriptor{std::move(encodings), receiver_channels, std::move(source_xml_bytes).value()};
  } catch (const std::exception& exception) {
    return parse_error("Unable to parse ISMRMRD XML header: " + std::string(exception.what()));
  } catch (...) {
    return parse_error("Unable to parse ISMRMRD XML header: unknown parser failure.");
  }
}

Result<ScanDescriptor> parse_ismrmrd_xml(const std::string_view xml, const ScanDescriptorParseOptions& options) {
  return ScanDescriptor::parse_ismrmrd_xml(xml, options);
}

Result<ArrivalEnvelope> ArrivalEnvelope::create(const ArrivalEnvelopeSpec& specification,
                                                const std::string_view field_name) {
  auto rate =
    required_quantity(specification.max_acquisitions_per_second, field(field_name, "max_acquisitions_per_second"));
  if (!rate.ok()) {
    return rate.status();
  }
  auto burst = required_quantity(specification.max_burst_acquisitions, field(field_name, "max_burst_acquisitions"));
  if (!burst.ok()) {
    return burst.status();
  }
  return ArrivalEnvelope{std::move(rate).value(), std::move(burst).value()};
}

Result<SinkServiceAssumption> SinkServiceAssumption::create(const SinkServiceAssumptionSpec& specification,
                                                            const std::string_view field_name) {
  if (!is_valid(specification.slow_sink_policy)) {
    return validation(std::string(field_name) + ".slow_sink_policy is invalid.");
  }
  auto rate = canonical_quantity(specification.minimum_drain_items_per_second,
                                 field(field_name, "minimum_drain_items_per_second"));
  if (!rate.ok()) {
    return rate.status();
  }
  auto pause = canonical_quantity(specification.max_pause_us, field(field_name, "max_pause_us"));
  if (!pause.ok()) {
    return pause.status();
  }
  auto staging =
    canonical_quantity(specification.transport_staging_bytes, field(field_name, "transport_staging_bytes"));
  if (!staging.ok()) {
    return staging.status();
  }
  if (specification.minimum_drain_items_per_second == 0 &&
      specification.slow_sink_policy != SlowSinkPolicy::externally_blocked) {
    return validation(std::string(field_name) +
                      " with zero minimum_drain_items_per_second must use externally_blocked policy.");
  }
  return SinkServiceAssumption{std::move(rate).value(), std::move(pause).value(), specification.slow_sink_policy,
                               std::move(staging).value()};
}

Result<TargetEnvelope> TargetEnvelope::create(const TargetEnvelopeSpec& specification) {
  auto max_xml = required_quantity(specification.max_xml_bytes, "target_envelope.max_xml_bytes");
  if (!max_xml.ok()) {
    return max_xml.status();
  }
  auto max_frame = required_quantity(specification.max_frame_charged_bytes, "target_envelope.max_frame_charged_bytes");
  if (!max_frame.ok()) {
    return max_frame.status();
  }
  auto max_image = required_quantity(specification.max_image_charged_bytes, "target_envelope.max_image_charged_bytes");
  if (!max_image.ok()) {
    return max_image.status();
  }
  auto max_decoder =
    canonical_quantity(specification.max_decoder_staging_bytes, "target_envelope.max_decoder_staging_bytes");
  if (!max_decoder.ok()) {
    return max_decoder.status();
  }
  auto max_samples =
    required_quantity(specification.max_samples_per_acquisition, "target_envelope.max_samples_per_acquisition");
  if (!max_samples.ok()) {
    return max_samples.status();
  }
  auto max_trajectory =
    canonical_quantity(specification.max_trajectory_dimensions, "target_envelope.max_trajectory_dimensions");
  if (!max_trajectory.ok()) {
    return max_trajectory.status();
  }
  auto max_channels = required_quantity(specification.max_active_channels, "target_envelope.max_active_channels");
  if (!max_channels.ok()) {
    return max_channels.status();
  }
  auto max_groups = required_quantity(specification.max_channel_groups, "target_envelope.max_channel_groups");
  if (!max_groups.ok()) {
    return max_groups.status();
  }
  auto max_keys =
    required_quantity(specification.max_dynamic_keys_per_scan, "target_envelope.max_dynamic_keys_per_scan");
  if (!max_keys.ok()) {
    return max_keys.status();
  }
  auto max_scans = required_quantity(specification.max_active_scans, "target_envelope.max_active_scans");
  if (!max_scans.ok()) {
    return max_scans.status();
  }
  auto horizon_items =
    canonical_quantity(specification.calibration_horizon_items, "target_envelope.calibration_horizon_items");
  if (!horizon_items.ok()) {
    return horizon_items.status();
  }
  auto horizon_bytes = canonical_quantity(specification.calibration_horizon_charged_bytes,
                                          "target_envelope.calibration_horizon_charged_bytes");
  if (!horizon_bytes.ok()) {
    return horizon_bytes.status();
  }
  auto arrival = ArrivalEnvelope::create(specification.arrival_envelope, "target_envelope.arrival_envelope");
  if (!arrival.ok()) {
    return arrival.status();
  }
  auto sink =
    SinkServiceAssumption::create(specification.sink_service_assumption, "target_envelope.sink_service_assumption");
  if (!sink.ok()) {
    return sink.status();
  }
  return TargetEnvelope{
    std::move(max_xml).value(),      std::move(max_frame).value(),     std::move(max_image).value(),
    std::move(max_decoder).value(),  std::move(max_samples).value(),   std::move(max_trajectory).value(),
    std::move(max_channels).value(), std::move(max_groups).value(),    std::move(max_keys).value(),
    std::move(max_scans).value(),    std::move(horizon_items).value(), std::move(horizon_bytes).value(),
    std::move(arrival).value(),      std::move(sink).value()};
}

Result<MachinePolicy> MachinePolicy::create(const MachinePolicySpec& specification) {
  auto capacity = ResourceVectorCapacity::create(specification.resource_capacity, "machine_policy.resource_capacity");
  if (!capacity.ok()) {
    return capacity.status();
  }
  auto numa_domains = required_quantity(specification.numa_domain_count, "machine_policy.numa_domain_count");
  if (!numa_domains.ok()) {
    return numa_domains.status();
  }
  if (specification.allowed_memory_domains.empty() || !has_unique_values(specification.allowed_memory_domains)) {
    return validation("machine_policy.allowed_memory_domains must contain unique values.");
  }
  for (const auto domain : specification.allowed_memory_domains) {
    if (!is_valid(domain)) {
      return validation("machine_policy.allowed_memory_domains contains an invalid memory domain.");
    }
  }
  if (specification.allowed_profiles.empty() || !has_unique_values(specification.allowed_profiles)) {
    return validation("machine_policy.allowed_profiles must contain unique values.");
  }
  for (const auto profile : specification.allowed_profiles) {
    if (!is_valid(profile)) {
      return validation("machine_policy.allowed_profiles contains an invalid execution profile.");
    }
  }
  if (!is_valid(specification.scheduler_policy)) {
    return validation("machine_policy.scheduler_policy is invalid.");
  }
  return MachinePolicy{std::move(capacity).value(), std::move(numa_domains).value(),
                       specification.allowed_memory_domains, specification.allowed_profiles,
                       specification.scheduler_policy};
}

bool MachinePolicy::allows(const ExecutionProfile profile) const noexcept {
  return std::find(allowed_profiles_.begin(), allowed_profiles_.end(), profile) != allowed_profiles_.end();
}

Result<ArtifactDigest> ArtifactDigest::parse(const std::string_view value, const std::string_view field_name) {
  if (!is_lower_hex_digest(value)) {
    return validation(std::string(field_name) + " must be a lower-case sha256: digest.");
  }
  return ArtifactDigest(std::string(value));
}

PlanInputDigests PlanInputDigests::from_validated(ArtifactDigest resolved_pipeline, ArtifactDigest scan_descriptor,
                                                  ArtifactDigest target_envelope,
                                                  ArtifactDigest machine_policy) noexcept {
  return PlanInputDigests{std::move(resolved_pipeline), std::move(scan_descriptor), std::move(target_envelope),
                          std::move(machine_policy)};
}

OperatorPlanBinding OperatorPlanBinding::from_validated(std::string node_id,
                                                        ArtifactDigest canonical_config_digest) noexcept {
  return OperatorPlanBinding{std::move(node_id), std::move(canonical_config_digest)};
}

Result<Quantity> synchronous_buffer_pool_host_metadata_charged_bytes(const Quantity slot_count,
                                                                     const std::string_view field_name) {
  return checked_multiply(slot_count, kSynchronousBufferPoolControlChargedBytesPerSlot,
                          std::string(field_name) + ".buffer_pool_control");
}

Result<Quantity> synchronous_buffer_pool_physical_charge_bytes(const Quantity slot_count,
                                                               const Quantity payload_capacity_bytes,
                                                               const Quantity metadata_capacity_bytes,
                                                               const std::string_view field_name) {
  auto payload_per_slot = checked_add(payload_capacity_bytes, metadata_capacity_bytes,
                                      std::string(field_name) + ".payload_and_metadata_per_slot");
  if (!payload_per_slot.ok()) {
    return payload_per_slot.status();
  }
  auto slab_per_slot = checked_add(payload_per_slot.value(), kSynchronousBufferPoolControlChargedBytesPerSlot,
                                   std::string(field_name) + ".with_control_per_slot");
  if (!slab_per_slot.ok()) {
    return slab_per_slot.status();
  }
  return checked_multiply(slot_count, slab_per_slot.value(), std::string(field_name) + ".all_pool_slots");
}

Result<Quantity> synchronous_data_edge_host_metadata_charged_bytes(const Quantity max_items,
                                                                   const std::string_view field_name) {
  return checked_multiply(max_items, kSynchronousDataEdgeControlChargedBytesPerItem,
                          std::string(field_name) + ".data_edge_control");
}

SynchronousBufferPoolPlan SynchronousBufferPoolPlan::from_validated(
  std::string pool_id, const SynchronousDataEndpointKind owner_kind, std::string owner_id, std::string owner_port_name,
  TypeDescriptor type_descriptor, const TypeMemoryDomain memory_domain, CanonicalQuantity slot_count,
  CanonicalQuantity payload_capacity_bytes, CanonicalQuantity metadata_capacity_bytes,
  CanonicalQuantity payload_alignment_bytes, std::string storage_accounting_id,
  CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
  CanonicalQuantity physical_charge_bytes) noexcept {
  return SynchronousBufferPoolPlan{std::move(pool_id),
                                   owner_kind,
                                   std::move(owner_id),
                                   std::move(owner_port_name),
                                   std::move(type_descriptor),
                                   memory_domain,
                                   slot_count,
                                   payload_capacity_bytes,
                                   metadata_capacity_bytes,
                                   payload_alignment_bytes,
                                   std::move(storage_accounting_id),
                                   host_metadata_charged_bytes,
                                   descriptor_charged_count,
                                   physical_charge_bytes};
}

SynchronousDataEdgePlan SynchronousDataEdgePlan::from_validated(
  std::string edge_id, std::string source_pool_id, const SynchronousDataEndpointKind producer_kind,
  std::string producer_id, std::string producer_port_name, CanonicalQuantity producer_abi_port,
  const SynchronousDataEndpointKind consumer_kind, std::string consumer_id, std::string consumer_port_name,
  CanonicalQuantity consumer_abi_port, TypeDescriptor type_descriptor, CanonicalQuantity max_items,
  CanonicalQuantity max_logical_bytes, std::string storage_accounting_id, CanonicalQuantity host_metadata_charged_bytes,
  CanonicalQuantity descriptor_charged_count, std::string terminal_policy) noexcept {
  return SynchronousDataEdgePlan{std::move(edge_id),
                                 std::move(source_pool_id),
                                 producer_kind,
                                 std::move(producer_id),
                                 std::move(producer_port_name),
                                 producer_abi_port,
                                 consumer_kind,
                                 std::move(consumer_id),
                                 std::move(consumer_port_name),
                                 consumer_abi_port,
                                 std::move(type_descriptor),
                                 max_items,
                                 max_logical_bytes,
                                 std::move(storage_accounting_id),
                                 host_metadata_charged_bytes,
                                 descriptor_charged_count,
                                 std::move(terminal_policy)};
}

SynchronousNodeInputBindingPlan SynchronousNodeInputBindingPlan::from_validated(
  std::string port_name, CanonicalQuantity abi_port, const SynchronousInputSourceKind source_kind,
  std::string source_id, TypeDescriptor type_descriptor, CanonicalQuantity maximum_item_count) noexcept {
  return SynchronousNodeInputBindingPlan{
    std::move(port_name), abi_port, source_kind, std::move(source_id), std::move(type_descriptor), maximum_item_count};
}

SynchronousNodeOutputBindingPlan SynchronousNodeOutputBindingPlan::from_validated(
  std::string port_name, CanonicalQuantity abi_port, const SynchronousOutputDestinationKind destination_kind,
  std::string destination_id, std::string pool_id, TypeDescriptor type_descriptor,
  CanonicalQuantity maximum_item_count) noexcept {
  return SynchronousNodeOutputBindingPlan{std::move(port_name),      abi_port,           destination_kind,
                                          std::move(destination_id), std::move(pool_id), std::move(type_descriptor),
                                          maximum_item_count};
}

SynchronousFiringPlan SynchronousFiringPlan::from_validated(
  CanonicalQuantity maximum_input_batches, CanonicalQuantity maximum_input_items,
  CanonicalQuantity maximum_output_grants, CanonicalQuantity maximum_input_payload_bytes,
  CanonicalQuantity maximum_scratch_bytes, CanonicalQuantity maximum_metadata_bytes,
  CanonicalQuantity staging_charged_bytes, CanonicalQuantity staging_descriptor_count,
  ResourceVector firing_reservation) noexcept {
  return SynchronousFiringPlan{maximum_input_batches,       maximum_input_items,      maximum_output_grants,
                               maximum_input_payload_bytes, maximum_scratch_bytes,    maximum_metadata_bytes,
                               staging_charged_bytes,       staging_descriptor_count, std::move(firing_reservation)};
}

SynchronousTerminalPlan SynchronousTerminalPlan::from_validated(CanonicalQuantity normal_max_output_items,
                                                                CanonicalQuantity normal_max_output_charged_bytes,
                                                                CanonicalQuantity normal_max_async_tokens,
                                                                CanonicalQuantity cancel_max_async_tokens) noexcept {
  return SynchronousTerminalPlan{normal_max_output_items, normal_max_output_charged_bytes, normal_max_async_tokens,
                                 cancel_max_async_tokens};
}

SynchronousNodePlan SynchronousNodePlan::from_validated(std::string node_id, std::string provider_id,
                                                        ArtifactDigest provider_bundle_digest, std::string operator_id,
                                                        SynchronousDynamicInputJoinPolicy dynamic_input_join_policy,
                                                        std::vector<SynchronousNodeInputBindingPlan> inputs,
                                                        std::vector<SynchronousNodeOutputBindingPlan> outputs,
                                                        SynchronousFiringPlan firing,
                                                        SynchronousTerminalPlan terminal) noexcept {
  return SynchronousNodePlan{std::move(node_id),     std::move(provider_id),    std::move(provider_bundle_digest),
                             std::move(operator_id), dynamic_input_join_policy, std::move(inputs),
                             std::move(outputs),     std::move(firing),         std::move(terminal)};
}

CalibrationArtifactBindingPlan CalibrationArtifactBindingPlan::from_validated(
  std::string binding_id, std::string producer_node_id, std::string producer_port_name,
  CanonicalQuantity producer_abi_port, std::string producer_pool_id, TypeDescriptor type_descriptor,
  CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count) noexcept {
  return CalibrationArtifactBindingPlan{
    std::move(binding_id),       std::move(producer_node_id), std::move(producer_port_name), producer_abi_port,
    std::move(producer_pool_id), std::move(type_descriptor),  host_metadata_charged_bytes,   descriptor_charged_count};
}

namespace {

[[nodiscard]] bool valid_synchronous_endpoint_kind(const SynchronousDataEndpointKind kind) noexcept {
  switch (kind) {
    case SynchronousDataEndpointKind::ingress:
    case SynchronousDataEndpointKind::node:
    case SynchronousDataEndpointKind::egress:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_synchronous_input_source_kind(const SynchronousInputSourceKind kind) noexcept {
  switch (kind) {
    case SynchronousInputSourceKind::data_edge:
    case SynchronousInputSourceKind::calibration_artifact:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_synchronous_output_destination_kind(const SynchronousOutputDestinationKind kind) noexcept {
  switch (kind) {
    case SynchronousOutputDestinationKind::data_edge:
    case SynchronousOutputDestinationKind::calibration_artifact:
      return true;
  }
  return false;
}

[[nodiscard]] bool
valid_synchronous_dynamic_input_join_policy(const SynchronousDynamicInputJoinPolicy policy) noexcept {
  switch (policy) {
    case SynchronousDynamicInputJoinPolicy::exact_item_identity:
      return true;
  }
  return false;
}

[[nodiscard]] Result<SynchronousBufferPoolPlan>
to_synchronous_buffer_pool_plan(const SynchronousBufferPoolPlanSpec& specification, const std::size_t index) {
  const std::string prefix = "synchronous_buffer_pool_plans[" + std::to_string(index) + "]";
  if (specification.pool_id.empty() || specification.owner_id.empty() ||
      !valid_synchronous_endpoint_kind(specification.owner_kind) ||
      specification.owner_kind == SynchronousDataEndpointKind::egress) {
    return validation(prefix + ".pool_id and owner_id must be non-empty, and the owner must be ingress or node.");
  }
  if (specification.owner_kind == SynchronousDataEndpointKind::node && specification.owner_port_name.empty()) {
    return validation(prefix + ".owner_port_name must be non-empty for a node-owned pool.");
  }
  if (specification.owner_kind == SynchronousDataEndpointKind::ingress && !specification.owner_port_name.empty()) {
    return validation(prefix + ".owner_port_name must be empty for an ingress-owned pool.");
  }
  const auto type_status = validate_synchronous_pool_type(specification.type_descriptor, prefix);
  if (!type_status.ok()) {
    return type_status;
  }
  if (specification.memory_domain != TypeMemoryDomain::host_normal ||
      specification.storage_accounting_id != "kspacejet.buffer-pool-storage/host-normal") {
    return validation(prefix + " requires host-normal immutable pool storage.");
  }
  auto slots = required_quantity(specification.slot_count, field(prefix, "slot_count"));
  auto payload = required_quantity(specification.payload_capacity_bytes, field(prefix, "payload_capacity_bytes"));
  auto metadata = canonical_quantity(specification.metadata_capacity_bytes, field(prefix, "metadata_capacity_bytes"));
  auto alignment = required_quantity(specification.payload_alignment_bytes, field(prefix, "payload_alignment_bytes"));
  auto host_metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  auto descriptors =
    required_quantity(specification.descriptor_charged_count, field(prefix, "descriptor_charged_count"));
  auto physical = required_quantity(specification.physical_charge_bytes, field(prefix, "physical_charge_bytes"));
  if (!slots.ok())
    return slots.status();
  if (!payload.ok())
    return payload.status();
  if (!metadata.ok())
    return metadata.status();
  if (!alignment.ok())
    return alignment.status();
  if (!host_metadata.ok())
    return host_metadata.status();
  if (!descriptors.ok())
    return descriptors.status();
  if (!physical.ok())
    return physical.status();
  if (alignment.value().value() != specification.type_descriptor.min_alignment_bytes() ||
      payload.value().value() % alignment.value().value() != 0U) {
    return validation(prefix + " payload alignment/capacity does not match the frozen TypeDescriptor.");
  }
  auto expected_metadata = synchronous_buffer_pool_host_metadata_charged_bytes(
    slots.value().value(), field(prefix, "host_metadata_charged_bytes"));
  auto expected_physical = synchronous_buffer_pool_physical_charge_bytes(
    slots.value().value(), payload.value().value(), metadata.value().value(), field(prefix, "physical_charge_bytes"));
  if (!expected_metadata.ok())
    return expected_metadata.status();
  if (!expected_physical.ok())
    return expected_physical.status();
  if (host_metadata.value().value() != expected_metadata.value() ||
      descriptors.value().value() != slots.value().value() || physical.value().value() != expected_physical.value()) {
    return validation(prefix + " fixed pool accounting does not match its capacity.");
  }
  return SynchronousBufferPoolPlan::from_validated(
    specification.pool_id, specification.owner_kind, specification.owner_id, specification.owner_port_name,
    specification.type_descriptor, specification.memory_domain, std::move(slots).value(), std::move(payload).value(),
    std::move(metadata).value(), std::move(alignment).value(), specification.storage_accounting_id,
    std::move(host_metadata).value(), std::move(descriptors).value(), std::move(physical).value());
}

[[nodiscard]] Result<SynchronousDataEdgePlan>
to_synchronous_data_edge_plan(const SynchronousDataEdgePlanSpec& specification, const std::size_t index) {
  const std::string prefix = "synchronous_data_edge_plans[" + std::to_string(index) + "]";
  if (specification.edge_id.empty() || specification.source_pool_id.empty() || specification.producer_id.empty() ||
      specification.consumer_id.empty() || !valid_synchronous_endpoint_kind(specification.producer_kind) ||
      !valid_synchronous_endpoint_kind(specification.consumer_kind) ||
      specification.producer_kind == SynchronousDataEndpointKind::egress ||
      specification.consumer_kind == SynchronousDataEndpointKind::ingress) {
    return validation(prefix + " has an invalid ingress/node/egress endpoint direction.");
  }
  if ((specification.producer_kind == SynchronousDataEndpointKind::node && specification.producer_port_name.empty()) ||
      (specification.producer_kind == SynchronousDataEndpointKind::ingress &&
       !specification.producer_port_name.empty()) ||
      (specification.consumer_kind == SynchronousDataEndpointKind::node && specification.consumer_port_name.empty()) ||
      (specification.consumer_kind == SynchronousDataEndpointKind::egress &&
       !specification.consumer_port_name.empty())) {
    return validation(prefix + " endpoint port names do not match their endpoint kinds.");
  }
  auto producer_abi = canonical_quantity(specification.producer_abi_port, field(prefix, "producer_abi_port"));
  auto consumer_abi = canonical_quantity(specification.consumer_abi_port, field(prefix, "consumer_abi_port"));
  if (!producer_abi.ok())
    return producer_abi.status();
  if (!consumer_abi.ok())
    return consumer_abi.status();
  if ((specification.producer_kind == SynchronousDataEndpointKind::ingress && producer_abi.value().value() != 0U) ||
      (specification.consumer_kind == SynchronousDataEndpointKind::egress && consumer_abi.value().value() != 0U) ||
      producer_abi.value().value() > std::numeric_limits<std::uint32_t>::max() ||
      consumer_abi.value().value() > std::numeric_limits<std::uint32_t>::max()) {
    return validation(prefix + " ABI port positions are invalid for their endpoint kinds.");
  }
  const auto type_status = validate_synchronous_pool_type(specification.type_descriptor, prefix);
  if (!type_status.ok())
    return type_status;
  if (specification.storage_accounting_id != "kspacejet.data-edge-storage/fixed-fifo" ||
      specification.terminal_policy != "normal-eoi-drain-cancellation-fail") {
    return validation(prefix + " does not use the current fixed FIFO terminal/storage policy.");
  }
  auto max_items = required_quantity(specification.max_items, field(prefix, "max_items"));
  auto max_logical = required_quantity(specification.max_logical_bytes, field(prefix, "max_logical_bytes"));
  auto host_metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  auto descriptors =
    required_quantity(specification.descriptor_charged_count, field(prefix, "descriptor_charged_count"));
  if (!max_items.ok())
    return max_items.status();
  if (!max_logical.ok())
    return max_logical.status();
  if (!host_metadata.ok())
    return host_metadata.status();
  if (!descriptors.ok())
    return descriptors.status();
  auto expected_metadata = synchronous_data_edge_host_metadata_charged_bytes(
    max_items.value().value(), field(prefix, "host_metadata_charged_bytes"));
  if (!expected_metadata.ok())
    return expected_metadata.status();
  if (host_metadata.value().value() != expected_metadata.value() ||
      descriptors.value().value() != max_items.value().value()) {
    return validation(prefix + " fixed FIFO accounting does not match max_items.");
  }
  return SynchronousDataEdgePlan::from_validated(
    specification.edge_id, specification.source_pool_id, specification.producer_kind, specification.producer_id,
    specification.producer_port_name, std::move(producer_abi).value(), specification.consumer_kind,
    specification.consumer_id, specification.consumer_port_name, std::move(consumer_abi).value(),
    specification.type_descriptor, std::move(max_items).value(), std::move(max_logical).value(),
    specification.storage_accounting_id, std::move(host_metadata).value(), std::move(descriptors).value(),
    specification.terminal_policy);
}

[[nodiscard]] Result<SynchronousNodePlan> to_synchronous_node_plan(const SynchronousNodePlanSpec& specification,
                                                                   const std::size_t index) {
  const std::string prefix = "synchronous_node_plans[" + std::to_string(index) + "]";
  if (specification.node_id.empty() || specification.provider_id.empty() || specification.operator_id.empty()) {
    return validation(prefix + ".node_id, provider_id, and operator_id must be non-empty.");
  }
  auto provider_bundle =
    ArtifactDigest::parse(specification.provider_bundle_digest, field(prefix, "provider_bundle_digest"));
  if (!provider_bundle.ok())
    return provider_bundle.status();
  if (!valid_synchronous_dynamic_input_join_policy(specification.dynamic_input_join_policy)) {
    return validation(prefix + ".dynamic_input_join_policy is invalid.");
  }
  std::vector<SynchronousNodeInputBindingPlan> inputs;
  inputs.reserve(specification.inputs.size());
  Quantity dynamic_input_count{0U};
  Quantity minimum_input_payload{0U};
  std::vector<std::string> input_ports;
  std::vector<Quantity> input_abi_ports;
  for (std::size_t input_index = 0U; input_index < specification.inputs.size(); ++input_index) {
    const auto& input = specification.inputs[input_index];
    const std::string input_prefix = prefix + ".inputs[" + std::to_string(input_index) + "]";
    if (input.port_name.empty() || input.source_id.empty() || !valid_synchronous_input_source_kind(input.source_kind)) {
      return validation(input_prefix + " must have a port, source id, and valid source kind.");
    }
    auto abi_port = canonical_quantity(input.abi_port, field(input_prefix, "abi_port"));
    auto maximum_item_count = required_quantity(input.maximum_item_count, field(input_prefix, "maximum_item_count"));
    if (!abi_port.ok())
      return abi_port.status();
    if (!maximum_item_count.ok())
      return maximum_item_count.status();
    if (abi_port.value().value() > std::numeric_limits<std::uint32_t>::max()) {
      return validation(input_prefix + ".abi_port cannot be represented by the Provider ABI.");
    }
    const auto type_status = validate_synchronous_pool_type(input.type_descriptor, input_prefix);
    if (!type_status.ok())
      return type_status;
    if (input.source_kind == SynchronousInputSourceKind::data_edge) {
      ++dynamic_input_count;
    }
    if (maximum_item_count.value().value() != 1U) {
      return validation(input_prefix + ".maximum_item_count must equal one for the transactional join executor.");
    }
    input_ports.push_back(input.port_name);
    input_abi_ports.push_back(abi_port.value().value());
    inputs.push_back(SynchronousNodeInputBindingPlan::from_validated(
      input.port_name, std::move(abi_port).value(), input.source_kind, input.source_id, input.type_descriptor,
      std::move(maximum_item_count).value()));
  }
  if (dynamic_input_count == 0U || dynamic_input_count > kSynchronousMaximumDynamicInputEdgesPerNode) {
    return validation(prefix + " must have between one and " +
                      std::to_string(kSynchronousMaximumDynamicInputEdgesPerNode) +
                      " dynamic data inputs for the bounded synchronous executor.");
  }
  std::sort(input_ports.begin(), input_ports.end());
  std::sort(input_abi_ports.begin(), input_abi_ports.end());
  if (std::adjacent_find(input_ports.begin(), input_ports.end()) != input_ports.end() ||
      std::adjacent_find(input_abi_ports.begin(), input_abi_ports.end()) != input_abi_ports.end()) {
    return validation(prefix + " input port names and ABI positions must each be unique.");
  }

  std::vector<SynchronousNodeOutputBindingPlan> outputs;
  outputs.reserve(specification.outputs.size());
  std::vector<std::string> output_ports;
  std::vector<Quantity> output_abi_ports;
  for (std::size_t output_index = 0U; output_index < specification.outputs.size(); ++output_index) {
    const auto& output = specification.outputs[output_index];
    const std::string output_prefix = prefix + ".outputs[" + std::to_string(output_index) + "]";
    if (output.port_name.empty() || output.destination_id.empty() || output.pool_id.empty() ||
        !valid_synchronous_output_destination_kind(output.destination_kind)) {
      return validation(output_prefix + " must have a port, pool, destination id, and valid destination kind.");
    }
    auto abi_port = canonical_quantity(output.abi_port, field(output_prefix, "abi_port"));
    auto maximum_item_count = required_quantity(output.maximum_item_count, field(output_prefix, "maximum_item_count"));
    if (!abi_port.ok())
      return abi_port.status();
    if (!maximum_item_count.ok())
      return maximum_item_count.status();
    if (abi_port.value().value() > std::numeric_limits<std::uint32_t>::max()) {
      return validation(output_prefix + ".abi_port cannot be represented by the Provider ABI.");
    }
    const auto type_status = validate_synchronous_pool_type(output.type_descriptor, output_prefix);
    if (!type_status.ok())
      return type_status;
    if (output.destination_kind == SynchronousOutputDestinationKind::calibration_artifact &&
        maximum_item_count.value().value() != 1U) {
      return validation(output_prefix + " calibration artifact output must publish exactly one immutable artifact.");
    }
    output_ports.push_back(output.port_name);
    output_abi_ports.push_back(abi_port.value().value());
    outputs.push_back(SynchronousNodeOutputBindingPlan::from_validated(
      output.port_name, std::move(abi_port).value(), output.destination_kind, output.destination_id, output.pool_id,
      output.type_descriptor, std::move(maximum_item_count).value()));
  }
  std::sort(output_ports.begin(), output_ports.end());
  std::sort(output_abi_ports.begin(), output_abi_ports.end());
  if (std::adjacent_find(output_ports.begin(), output_ports.end()) != output_ports.end() ||
      std::adjacent_find(output_abi_ports.begin(), output_abi_ports.end()) != output_abi_ports.end()) {
    return validation(prefix + " output port names and ABI positions must each be unique.");
  }

  const auto& firing_spec = specification.firing;
  auto input_batches =
    canonical_quantity(firing_spec.maximum_input_batches, field(prefix, "firing.maximum_input_batches"));
  auto input_items = canonical_quantity(firing_spec.maximum_input_items, field(prefix, "firing.maximum_input_items"));
  auto output_grants =
    canonical_quantity(firing_spec.maximum_output_grants, field(prefix, "firing.maximum_output_grants"));
  auto input_payload =
    canonical_quantity(firing_spec.maximum_input_payload_bytes, field(prefix, "firing.maximum_input_payload_bytes"));
  auto scratch = canonical_quantity(firing_spec.maximum_scratch_bytes, field(prefix, "firing.maximum_scratch_bytes"));
  auto metadata = required_quantity(firing_spec.maximum_metadata_bytes, field(prefix, "firing.maximum_metadata_bytes"));
  auto staging_bytes =
    required_quantity(firing_spec.staging_charged_bytes, field(prefix, "firing.staging_charged_bytes"));
  auto staging_descriptors =
    required_quantity(firing_spec.staging_descriptor_count, field(prefix, "firing.staging_descriptor_count"));
  auto firing_reservation =
    ResourceVector::create(firing_spec.firing_reservation, field(prefix, "firing.firing_reservation"));
  if (!input_batches.ok())
    return input_batches.status();
  if (!input_items.ok())
    return input_items.status();
  if (!output_grants.ok())
    return output_grants.status();
  if (!input_payload.ok())
    return input_payload.status();
  if (!scratch.ok())
    return scratch.status();
  if (!metadata.ok())
    return metadata.status();
  if (!staging_bytes.ok())
    return staging_bytes.status();
  if (!staging_descriptors.ok())
    return staging_descriptors.status();
  if (!firing_reservation.ok())
    return firing_reservation.status();
  if (input_batches.value().value() != inputs.size() || input_items.value().value() != inputs.size() ||
      output_grants.value().value() != outputs.size()) {
    return validation(prefix + " firing input/output ABI capacities must exactly cover the frozen bindings.");
  }
  if (firing_reservation.value().cpu_leaf_permits() == 0U) {
    return validation(prefix + " firing_reservation must include at least one CPU permit.");
  }
  auto firing = SynchronousFiringPlan::from_validated(
    std::move(input_batches).value(), std::move(input_items).value(), std::move(output_grants).value(),
    std::move(input_payload).value(), std::move(scratch).value(), std::move(metadata).value(),
    std::move(staging_bytes).value(), std::move(staging_descriptors).value(), std::move(firing_reservation).value());
  const auto& terminal_spec = specification.terminal;
  auto normal_items =
    canonical_quantity(terminal_spec.normal_max_output_items, field(prefix, "terminal.normal_max_output_items"));
  auto normal_bytes = canonical_quantity(terminal_spec.normal_max_output_charged_bytes,
                                         field(prefix, "terminal.normal_max_output_charged_bytes"));
  auto normal_async =
    canonical_quantity(terminal_spec.normal_max_async_tokens, field(prefix, "terminal.normal_max_async_tokens"));
  auto cancel_async =
    canonical_quantity(terminal_spec.cancel_max_async_tokens, field(prefix, "terminal.cancel_max_async_tokens"));
  if (!normal_items.ok())
    return normal_items.status();
  if (!normal_bytes.ok())
    return normal_bytes.status();
  if (!normal_async.ok())
    return normal_async.status();
  if (!cancel_async.ok())
    return cancel_async.status();
  return SynchronousNodePlan::from_validated(
    specification.node_id, specification.provider_id, std::move(provider_bundle).value(), specification.operator_id,
    specification.dynamic_input_join_policy, std::move(inputs), std::move(outputs), std::move(firing),
    SynchronousTerminalPlan::from_validated(std::move(normal_items).value(), std::move(normal_bytes).value(),
                                            std::move(normal_async).value(), std::move(cancel_async).value()));
}

[[nodiscard]] Result<CalibrationArtifactBindingPlan>
to_calibration_artifact_binding_plan(const CalibrationArtifactBindingPlanSpec& specification, const std::size_t index) {
  const std::string prefix = "calibration_artifact_binding_plans[" + std::to_string(index) + "]";
  if (specification.binding_id.empty() || specification.producer_node_id.empty() ||
      specification.producer_port_name.empty() || specification.producer_pool_id.empty()) {
    return validation(prefix + " binding, producer endpoint, and producer pool must be non-empty.");
  }
  auto abi_port = canonical_quantity(specification.producer_abi_port, field(prefix, "producer_abi_port"));
  auto host_metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  auto descriptors =
    required_quantity(specification.descriptor_charged_count, field(prefix, "descriptor_charged_count"));
  if (!abi_port.ok())
    return abi_port.status();
  if (!host_metadata.ok())
    return host_metadata.status();
  if (!descriptors.ok())
    return descriptors.status();
  if (abi_port.value().value() > std::numeric_limits<std::uint32_t>::max() || descriptors.value().value() != 1U) {
    return validation(prefix + " has invalid ABI or calibration-store descriptor accounting.");
  }
  const auto type_status = validate_synchronous_pool_type(specification.type_descriptor, prefix);
  if (!type_status.ok())
    return type_status;
  return CalibrationArtifactBindingPlan::from_validated(
    specification.binding_id, specification.producer_node_id, specification.producer_port_name,
    std::move(abi_port).value(), specification.producer_pool_id, specification.type_descriptor,
    std::move(host_metadata).value(), std::move(descriptors).value());
}

} // namespace

Result<ExecutionPlan> ExecutionPlan::create_synchronous(ArtifactDigest digest, const ExecutionPlanSpec& specification,
                                                        PlanInputDigests inputs,
                                                        std::vector<OperatorPlanBinding> operator_plan_bindings) {
  if (specification.synchronous_node_plans.empty() || specification.synchronous_buffer_pool_plans.empty() ||
      specification.synchronous_data_edge_plans.empty()) {
    return validation("A synchronous graph ExecutionPlan requires nodes, pools, and data edges.");
  }
  std::vector<SynchronousNodePlan> nodes;
  std::vector<SynchronousBufferPoolPlan> pools;
  std::vector<SynchronousDataEdgePlan> edges;
  std::vector<CalibrationArtifactBindingPlan> artifacts;
  nodes.reserve(specification.synchronous_node_plans.size());
  pools.reserve(specification.synchronous_buffer_pool_plans.size());
  edges.reserve(specification.synchronous_data_edge_plans.size());
  artifacts.reserve(specification.calibration_artifact_binding_plans.size());
  std::vector<std::string> node_ids;
  std::vector<std::string> pool_ids;
  std::vector<std::string> edge_ids;
  std::vector<std::string> artifact_ids;
  Quantity pool_bytes{0U};
  Quantity pool_descriptors{0U};
  Quantity edge_bytes{0U};
  Quantity edge_descriptors{0U};
  Quantity node_staging_bytes{0U};
  Quantity node_staging_descriptors{0U};
  Quantity artifact_bytes{0U};
  Quantity artifact_descriptors{0U};
  for (std::size_t index = 0U; index < specification.synchronous_node_plans.size(); ++index) {
    auto node = to_synchronous_node_plan(specification.synchronous_node_plans[index], index);
    if (!node.ok())
      return node.status();
    node_ids.push_back(node.value().node_id());
    auto next_bytes =
      checked_add(node_staging_bytes, node.value().firing().staging_charged_bytes(), "node firing staging");
    auto next_descriptors = checked_add(node_staging_descriptors, node.value().firing().staging_descriptor_count(),
                                        "node firing staging descriptors");
    if (!next_bytes.ok())
      return next_bytes.status();
    if (!next_descriptors.ok())
      return next_descriptors.status();
    node_staging_bytes = next_bytes.value();
    node_staging_descriptors = next_descriptors.value();
    nodes.push_back(std::move(node).value());
  }
  for (std::size_t index = 0U; index < specification.synchronous_buffer_pool_plans.size(); ++index) {
    auto pool = to_synchronous_buffer_pool_plan(specification.synchronous_buffer_pool_plans[index], index);
    if (!pool.ok())
      return pool.status();
    pool_ids.push_back(pool.value().pool_id());
    auto next_bytes = checked_add(pool_bytes, pool.value().physical_charge_bytes(), "synchronous pool physical charge");
    auto next_descriptors =
      checked_add(pool_descriptors, pool.value().descriptor_charged_count(), "synchronous pool descriptors");
    if (!next_bytes.ok())
      return next_bytes.status();
    if (!next_descriptors.ok())
      return next_descriptors.status();
    pool_bytes = next_bytes.value();
    pool_descriptors = next_descriptors.value();
    pools.push_back(std::move(pool).value());
  }
  for (std::size_t index = 0U; index < specification.synchronous_data_edge_plans.size(); ++index) {
    auto edge = to_synchronous_data_edge_plan(specification.synchronous_data_edge_plans[index], index);
    if (!edge.ok())
      return edge.status();
    edge_ids.push_back(edge.value().edge_id());
    auto next_bytes = checked_add(edge_bytes, edge.value().host_metadata_charged_bytes(), "synchronous edge metadata");
    auto next_descriptors =
      checked_add(edge_descriptors, edge.value().descriptor_charged_count(), "synchronous edge descriptors");
    if (!next_bytes.ok())
      return next_bytes.status();
    if (!next_descriptors.ok())
      return next_descriptors.status();
    edge_bytes = next_bytes.value();
    edge_descriptors = next_descriptors.value();
    edges.push_back(std::move(edge).value());
  }
  for (std::size_t index = 0U; index < specification.calibration_artifact_binding_plans.size(); ++index) {
    auto artifact =
      to_calibration_artifact_binding_plan(specification.calibration_artifact_binding_plans[index], index);
    if (!artifact.ok())
      return artifact.status();
    artifact_ids.push_back(artifact.value().binding_id());
    auto next_bytes = checked_add(artifact_bytes, artifact.value().host_metadata_charged_bytes(),
                                  "calibration artifact store metadata");
    auto next_descriptors = checked_add(artifact_descriptors, artifact.value().descriptor_charged_count(),
                                        "calibration artifact store descriptors");
    if (!next_bytes.ok())
      return next_bytes.status();
    if (!next_descriptors.ok())
      return next_descriptors.status();
    artifact_bytes = next_bytes.value();
    artifact_descriptors = next_descriptors.value();
    artifacts.push_back(std::move(artifact).value());
  }
  const auto unique = [](std::vector<std::string>& values) {
    std::sort(values.begin(), values.end());
    return std::adjacent_find(values.begin(), values.end()) == values.end();
  };
  if (!unique(node_ids) || !unique(pool_ids) || !unique(edge_ids) || !unique(artifact_ids)) {
    return validation("Synchronous graph node, pool, edge, and calibration binding ids must each be unique.");
  }
  const auto find_node = [&nodes](const std::string_view id) -> const SynchronousNodePlan* {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const SynchronousNodePlan& node) {
      return node.node_id() == id;
    });
    return found == nodes.end() ? nullptr : &*found;
  };
  const auto find_pool = [&pools](const std::string_view id) -> const SynchronousBufferPoolPlan* {
    const auto found = std::find_if(pools.begin(), pools.end(), [&](const SynchronousBufferPoolPlan& pool) {
      return pool.pool_id() == id;
    });
    return found == pools.end() ? nullptr : &*found;
  };
  const auto find_edge = [&edges](const std::string_view id) -> const SynchronousDataEdgePlan* {
    const auto found = std::find_if(edges.begin(), edges.end(), [&](const SynchronousDataEdgePlan& edge) {
      return edge.edge_id() == id;
    });
    return found == edges.end() ? nullptr : &*found;
  };
  const auto find_artifact = [&artifacts](const std::string_view id) -> const CalibrationArtifactBindingPlan* {
    const auto found =
      std::find_if(artifacts.begin(), artifacts.end(), [&](const CalibrationArtifactBindingPlan& artifact) {
        return artifact.binding_id() == id;
      });
    return found == artifacts.end() ? nullptr : &*found;
  };
  for (const auto& edge : edges) {
    const auto* pool = find_pool(edge.source_pool_id());
    if (pool == nullptr || !pool->type_descriptor().exactly_matches(edge.type_descriptor()) ||
        pool->owner_kind() != edge.producer_kind() || pool->owner_id() != edge.producer_id() ||
        pool->owner_port_name() != edge.producer_port_name()) {
      return validation("Synchronous data edge must exactly bind its owning source pool and TypeDescriptor.");
    }
    auto logical_per_item = checked_add(pool->payload_capacity_bytes(), pool->metadata_capacity_bytes(),
                                        "synchronous edge logical bytes per item");
    if (!logical_per_item.ok())
      return logical_per_item.status();
    auto expected_logical =
      checked_multiply(edge.max_items(), logical_per_item.value(), "synchronous edge logical byte capacity");
    if (!expected_logical.ok())
      return expected_logical.status();
    if (edge.max_logical_bytes() != expected_logical.value()) {
      return validation("Synchronous data edge logical bytes must exactly cover full source-pool slots.");
    }
  }
  for (std::size_t left = 0U; left < edges.size(); ++left) {
    for (std::size_t right = left + 1U; right < edges.size(); ++right) {
      if (edges[left].source_pool_id() == edges[right].source_pool_id()) {
        return validation("Synchronous graph fan-out is not implemented: one source pool may feed only one data edge.");
      }
    }
  }
  for (const auto& node : nodes) {
    Quantity dynamic_inputs{0U};
    for (const auto& input : node.inputs()) {
      if (input.source_kind() == SynchronousInputSourceKind::data_edge) {
        ++dynamic_inputs;
        const auto* edge = find_edge(input.source_id());
        if (edge == nullptr || edge->consumer_kind() != SynchronousDataEndpointKind::node ||
            edge->consumer_id() != node.node_id() || edge->consumer_port_name() != input.port_name() ||
            edge->consumer_abi_port() != input.abi_port() ||
            !edge->type_descriptor().exactly_matches(input.type_descriptor())) {
          return validation("Synchronous node data input must exactly bind one incoming data edge.");
        }
      } else {
        const auto* artifact = find_artifact(input.source_id());
        if (artifact == nullptr || !artifact->type_descriptor().exactly_matches(input.type_descriptor())) {
          return validation("Synchronous node calibration input must exactly bind one calibration artifact.");
        }
      }
    }
    if (dynamic_inputs == 0U || dynamic_inputs > kSynchronousMaximumDynamicInputEdgesPerNode) {
      return validation("Synchronous node dynamic input count exceeds the frozen bounded join protocol.");
    }
    for (const auto& output : node.outputs()) {
      const auto* pool = find_pool(output.pool_id());
      if (pool == nullptr || pool->owner_kind() != SynchronousDataEndpointKind::node ||
          pool->owner_id() != node.node_id() || pool->owner_port_name() != output.port_name() ||
          !pool->type_descriptor().exactly_matches(output.type_descriptor())) {
        return validation("Synchronous node output must exactly bind one node-owned buffer pool.");
      }
      if (output.destination_kind() == SynchronousOutputDestinationKind::data_edge) {
        const auto* edge = find_edge(output.destination_id());
        if (edge == nullptr || edge->producer_kind() != SynchronousDataEndpointKind::node ||
            edge->producer_id() != node.node_id() || edge->producer_port_name() != output.port_name() ||
            edge->producer_abi_port() != output.abi_port() || edge->source_pool_id() != output.pool_id() ||
            !edge->type_descriptor().exactly_matches(output.type_descriptor())) {
          return validation("Synchronous node output must exactly bind one outgoing data edge.");
        }
      } else {
        const auto* artifact = find_artifact(output.destination_id());
        if (artifact == nullptr || artifact->producer_node_id() != node.node_id() ||
            artifact->producer_port_name() != output.port_name() ||
            artifact->producer_abi_port() != output.abi_port() || artifact->producer_pool_id() != output.pool_id() ||
            !artifact->type_descriptor().exactly_matches(output.type_descriptor())) {
          return validation("Synchronous node output must exactly bind one calibration artifact publication.");
        }
      }
    }
  }
  for (const auto& pool : pools) {
    const auto edge = std::find_if(edges.begin(), edges.end(), [&](const SynchronousDataEdgePlan& candidate) {
      return candidate.source_pool_id() == pool.pool_id();
    });
    if (pool.owner_kind() == SynchronousDataEndpointKind::ingress && edge == edges.end()) {
      return validation("Every synchronous ingress pool must feed exactly one data edge.");
    }
    if (pool.owner_kind() != SynchronousDataEndpointKind::node) {
      continue;
    }
    const auto* owner = find_node(pool.owner_id());
    if (owner == nullptr) {
      return validation("Synchronous buffer pool names an unknown node owner.");
    }
    const auto output = std::find_if(owner->outputs().begin(), owner->outputs().end(), [&](const auto& candidate) {
      return candidate.pool_id() == pool.pool_id();
    });
    if (output == owner->outputs().end()) {
      return validation("A node-owned synchronous buffer pool must be bound by exactly one node output.");
    }
    if ((output->destination_kind() == SynchronousOutputDestinationKind::data_edge) != (edge != edges.end())) {
      return validation("Synchronous output destination and source-pool data edge disagree.");
    }
  }
  for (const auto& artifact : artifacts) {
    const auto* node = find_node(artifact.producer_node_id());
    if (node == nullptr)
      return validation("Calibration artifact producer node does not exist.");
    const auto output = std::find_if(node->outputs().begin(), node->outputs().end(), [&](const auto& candidate) {
      return candidate.destination_kind() == SynchronousOutputDestinationKind::calibration_artifact &&
             candidate.destination_id() == artifact.binding_id();
    });
    if (output == node->outputs().end()) {
      return validation("Calibration artifact must have exactly one matching producer output binding.");
    }
  }
  std::vector<std::string> bound_nodes;
  bound_nodes.reserve(operator_plan_bindings.size());
  for (const auto& binding : operator_plan_bindings)
    bound_nodes.push_back(binding.node_id());
  if (!unique(bound_nodes) || bound_nodes != node_ids) {
    return validation("operator_plan_bindings must exactly cover the synchronous node set.");
  }
  auto resources = ResourceVector::create(specification.resource_vector, "resource_vector");
  if (!resources.ok())
    return resources.status();
  auto required_host = checked_add(pool_bytes, edge_bytes, "synchronous pools plus edges");
  if (!required_host.ok())
    return required_host.status();
  required_host = checked_add(required_host.value(), node_staging_bytes, "synchronous firing staging");
  if (!required_host.ok())
    return required_host.status();
  required_host = checked_add(required_host.value(), artifact_bytes, "calibration artifact store");
  if (!required_host.ok())
    return required_host.status();
  auto required_descriptors = checked_add(pool_descriptors, edge_descriptors, "synchronous pool plus edge descriptors");
  if (!required_descriptors.ok())
    return required_descriptors.status();
  required_descriptors =
    checked_add(required_descriptors.value(), node_staging_descriptors, "synchronous firing staging descriptors");
  if (!required_descriptors.ok())
    return required_descriptors.status();
  required_descriptors =
    checked_add(required_descriptors.value(), artifact_descriptors, "calibration artifact descriptors");
  if (!required_descriptors.ok())
    return required_descriptors.status();
  if (resources.value().host_normal_bytes() < required_host.value() ||
      resources.value().descriptor_count() < required_descriptors.value()) {
    return validation("resource_vector does not cover frozen synchronous pool, edge, firing, and calibration storage.");
  }
  auto terminal_occurrences = required_quantity(specification.terminal_occurrences, "terminal_occurrences");
  if (!terminal_occurrences.ok())
    return terminal_occurrences.status();
  if (!has_nonempty_unique_strings(specification.proof_obligations)) {
    return validation("proof_obligations must contain unique non-empty identifiers.");
  }
  return ExecutionPlan{std::move(digest),
                       std::move(inputs),
                       std::move(operator_plan_bindings),
                       specification.execution_profile,
                       std::move(nodes),
                       std::move(pools),
                       std::move(edges),
                       std::move(artifacts),
                       std::move(resources).value(),
                       std::move(terminal_occurrences).value(),
                       specification.proof_obligations};
}

Result<ExecutionPlan> ExecutionPlan::create(ArtifactDigest digest, const ExecutionPlanSpec& specification) {
  if (!is_valid(specification.execution_profile)) {
    return validation("ExecutionPlan execution_profile is invalid.");
  }
  auto inputs = to_plan_input_digests(specification.inputs);
  if (!inputs.ok()) {
    return inputs.status();
  }
  auto operator_plan_bindings = to_operator_plan_bindings(specification.operator_plan_bindings);
  if (!operator_plan_bindings.ok()) {
    return operator_plan_bindings.status();
  }
  return create_synchronous(std::move(digest), specification, std::move(inputs).value(),
                            std::move(operator_plan_bindings).value());
}

Result<VerificationRecord> VerificationRecord::create(ArtifactDigest digest,
                                                      const VerificationRecordSpec& specification) {
  if (!is_valid(specification.execution_profile)) {
    return validation("VerificationRecord execution_profile is invalid.");
  }
  auto plan_digest = ArtifactDigest::parse(specification.execution_plan_digest, "execution_plan_digest");
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }
  auto resources = ResourceVector::create(specification.verified_resource_vector, "verified_resource_vector");
  if (!resources.ok()) {
    return resources.status();
  }
  auto terminal = required_quantity(specification.verified_terminal_occurrences, "verified_terminal_occurrences");
  if (!terminal.ok()) {
    return terminal.status();
  }
  if (!has_nonempty_unique_strings(specification.verified_obligations)) {
    return validation("verified_obligations must contain unique non-empty identifiers.");
  }
  return VerificationRecord{std::move(digest),
                            std::move(plan_digest).value(),
                            specification.execution_profile,
                            std::move(resources).value(),
                            std::move(terminal).value(),
                            specification.verified_obligations};
}

Result<AdmissionRecord> AdmissionRecord::create(const AdmissionRecordSpec& specification) {
  if (!is_valid(specification.outcome)) {
    return validation("AdmissionRecord outcome is invalid.");
  }
  auto plan_digest = ArtifactDigest::parse(specification.execution_plan_digest, "execution_plan_digest");
  if (!plan_digest.ok()) {
    return plan_digest.status();
  }
  auto verification_record_digest =
    ArtifactDigest::parse(specification.verification_record_digest, "verification_record_digest");
  if (!verification_record_digest.ok()) {
    return verification_record_digest.status();
  }
  auto reservation = ResourceVector::create(specification.reservation, "reservation");
  if (!reservation.ok()) {
    return reservation.status();
  }
  if (specification.reason.has_value()) {
    auto reason_length = detail::utf8_code_point_count(*specification.reason, "AdmissionRecord reason");
    if (!reason_length.ok()) {
      return reason_length.status();
    }
    if (reason_length.value() == 0U || reason_length.value() > 4096U) {
      return validation("AdmissionRecord reason, when present, must contain 1 to 4096 Unicode code points.");
    }
  }
  if (specification.outcome == AdmissionOutcome::rejected) {
    if (!reservation.value().empty()) {
      return validation("Rejected AdmissionRecord must not carry a process resource reservation.");
    }
    if (!specification.reason.has_value() || specification.reason->empty()) {
      return validation("Rejected AdmissionRecord must include a non-empty reason.");
    }
  }
  return AdmissionRecord{std::move(plan_digest).value(), std::move(verification_record_digest).value(),
                         specification.outcome, std::move(reservation).value(), specification.reason};
}

Result<TypeDescriptor> completed_frame_slot_context_type() {
  return types::kspace_frame();
}

Result<OperatorContract> OperatorContract::create(const OperatorContractSpec& specification) {
  if (specification.operator_id.empty()) {
    return validation("OperatorContract operator_id must not be empty.");
  }
  if (specification.ports.empty()) {
    return validation("ports must contain at least one declared port.");
  }
  std::vector<std::string> port_names;
  port_names.reserve(specification.ports.size());
  std::vector<ResolvedPort> resolved_ports;
  resolved_ports.reserve(specification.ports.size());
  for (std::size_t index = 0; index < specification.ports.size(); ++index) {
    const auto& port = specification.ports[index];
    const std::string prefix = "ports[" + std::to_string(index) + "]";
    if (port.name.empty() || !is_valid(port.direction)) {
      return validation(prefix + " must declare a non-empty name and valid direction.");
    }
    auto type_ref = TypeRef::parse(port.type_ref, prefix + ".type_ref");
    if (!type_ref.ok()) {
      return type_ref.status();
    }
    auto descriptor = types::resolve(type_ref.value().value());
    if (!descriptor.ok()) {
      return Status::ValidationError(
        prefix + ".type_ref does not resolve in the checked-in type registry: " + descriptor.status().message());
    }
    if (descriptor.value().type_ref() != type_ref.value()) {
      return validation(prefix + ".type_ref resolved a descriptor with a different canonical TypeRef.");
    }
    port_names.push_back(port.name);
    resolved_ports.push_back(
      {.name = port.name, .type_descriptor = std::move(descriptor).value(), .direction = port.direction});
  }
  if (!has_nonempty_unique_strings(port_names)) {
    return validation("ports must not contain duplicate port names.");
  }

  return OperatorContract{specification.operator_id, std::move(resolved_ports)};
}

Result<NodePlanningRequirements> NodePlanningRequirements::create(const NodePlanningRequirementsSpec& specification,
                                                                  const OperatorContract& contract) {
  const auto& ports = contract.ports();

  auto execution = validate_node_execution(specification.execution);
  if (!execution.ok()) {
    return execution;
  }
  auto batch = validate_node_batch(specification.batch);
  if (!batch.ok()) {
    return batch;
  }
  if (specification.batch.max_items > specification.execution.max_items_per_activation) {
    return validation("batch.max_items must not exceed execution.max_items_per_activation.");
  }
  auto rates = validate_node_rates(specification.rates, ports);
  if (!rates.ok()) {
    return rates;
  }
  auto static_batch_feasibility =
    validate_static_batch_activation_feasibility(specification.rates, specification.batch, specification.execution);
  if (!static_batch_feasibility.ok()) {
    return static_batch_feasibility;
  }
  auto resources = validate_node_resources(specification.resources);
  if (!resources.ok()) {
    return resources;
  }
  auto calibration = validate_node_calibration(specification.calibration);
  if (!calibration.ok()) {
    return calibration;
  }
  auto join = validate_node_join(specification.join);
  if (!join.ok()) {
    return join;
  }
  auto terminal = validate_terminal_planning(specification.terminal);
  if (!terminal.ok()) {
    return terminal;
  }
  auto output_coverage =
    validate_output_resource_coverage(specification.resources, specification.rates, specification.terminal);
  if (!output_coverage.ok()) {
    return output_coverage;
  }
  return NodePlanningRequirements{specification.execution, specification.batch,       specification.rates,
                                  specification.resources, specification.calibration, specification.join,
                                  specification.terminal};
}

Status NodePlanningRequirements::validate_against(const OperatorContract& contract) const {
  const NodePlanningRequirementsSpec specification{
    .execution = execution_,
    .batch = batch_,
    .rates = rates_,
    .resources = resources_,
    .calibration = calibration_,
    .join = join_,
    .terminal = terminal_,
  };
  return NodePlanningRequirements::create(specification, contract).status();
}

} // namespace ksj::recon
