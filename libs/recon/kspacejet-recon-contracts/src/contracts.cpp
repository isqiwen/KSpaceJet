#include "kspacejet/recon/contracts.hpp"

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
  if (specification.provider_contracts.empty()) {
    return validation("inputs.provider_contracts must contain at least one resolved Provider contract digest.");
  }

  std::vector<ArtifactDigest> provider_contracts;
  provider_contracts.reserve(specification.provider_contracts.size());
  std::vector<std::string_view> seen;
  seen.reserve(specification.provider_contracts.size());
  for (std::size_t index = 0; index < specification.provider_contracts.size(); ++index) {
    auto digest = ArtifactDigest::parse(specification.provider_contracts[index],
                                        "inputs.provider_contracts[" + std::to_string(index) + "]");
    if (!digest.ok()) {
      return digest.status();
    }
    seen.emplace_back(digest.value().value());
    provider_contracts.push_back(std::move(digest).value());
  }
  std::sort(seen.begin(), seen.end());
  if (std::adjacent_find(seen.begin(), seen.end()) != seen.end()) {
    return validation("inputs.provider_contracts must not contain duplicate digests.");
  }

  return PlanInputDigests::from_validated(std::move(resolved_pipeline).value(), std::move(scan_descriptor).value(),
                                          std::move(target_envelope).value(), std::move(machine_policy).value(),
                                          std::move(provider_contracts));
}

[[nodiscard]] Result<KeySlotTablePlan> to_key_slot_table_plan(const KeySlotTablePlanSpec& specification,
                                                              const std::size_t index) {
  const std::string prefix = "key_slot_tables[" + std::to_string(index) + "]";
  if (specification.node_id.empty()) {
    return validation(prefix + ".node_id must not be empty.");
  }

  Quantity dense_product = 1U;
  std::vector<DenseKeySlotDimension> dimensions;
  dimensions.reserve(specification.dense_dimensions.size());
  std::vector<std::string_view> fields;
  fields.reserve(specification.dense_dimensions.size());
  for (std::size_t dimension_index = 0; dimension_index < specification.dense_dimensions.size(); ++dimension_index) {
    const auto& dimension = specification.dense_dimensions[dimension_index];
    const auto dimension_prefix = prefix + ".dense_dimensions[" + std::to_string(dimension_index) + "]";
    if (dimension.field.empty()) {
      return validation(dimension_prefix + ".field must not be empty.");
    }
    auto minimum = canonical_quantity(dimension.minimum, field(dimension_prefix, "minimum"));
    if (!minimum.ok()) {
      return minimum.status();
    }
    auto cardinality = required_quantity(dimension.cardinality, field(dimension_prefix, "cardinality"));
    if (!cardinality.ok()) {
      return cardinality.status();
    }
    auto next_product =
      checked_multiply(dense_product, cardinality.value().value(), prefix + ".dense_dimensions mixed-radix product");
    if (!next_product.ok()) {
      return next_product.status();
    }
    dense_product = next_product.value();
    fields.emplace_back(dimension.field);
    dimensions.push_back(DenseKeySlotDimension::from_validated(dimension.field, std::move(minimum).value(),
                                                               std::move(cardinality).value()));
  }
  std::sort(fields.begin(), fields.end());
  if (std::adjacent_find(fields.begin(), fields.end()) != fields.end()) {
    return validation(prefix + ".dense_dimensions must contain unique field values.");
  }

  if (specification.mapping_algorithm_id != kDenseMixedRadixKeySlotMappingAlgorithmId) {
    return validation(prefix + ".mapping_algorithm_id must be '" +
                      std::string(kDenseMixedRadixKeySlotMappingAlgorithmId) + "'.");
  }
  if (specification.storage_accounting_id != kDenseKeySlotStorageAccountingId) {
    return validation(prefix + ".storage_accounting_id must be '" + std::string(kDenseKeySlotStorageAccountingId) +
                      "'.");
  }
  if (specification.generation_policy != kMonotonicU64KeySlotGenerationPolicy) {
    return validation(prefix + ".generation_policy must be '" + std::string(kMonotonicU64KeySlotGenerationPolicy) +
                      "'.");
  }
  if (specification.eviction_policy != kCompletedOnlyKeySlotEvictionPolicy) {
    return validation(prefix + ".eviction_policy must be '" + std::string(kCompletedOnlyKeySlotEvictionPolicy) + "'.");
  }
  if (specification.late_event_policy != kFailKeySlotLateEventPolicy) {
    return validation(prefix + ".late_event_policy must be '" + std::string(kFailKeySlotLateEventPolicy) + "'.");
  }

  auto key_domain_bound = required_quantity(specification.key_domain_bound, field(prefix, "key_domain_bound"));
  if (!key_domain_bound.ok()) {
    return key_domain_bound.status();
  }
  if (key_domain_bound.value().value() != dense_product) {
    return validation(prefix + ".key_domain_bound must equal the checked dense_dimensions mixed-radix product.");
  }
  auto max_distinct_keys = required_quantity(specification.max_distinct_keys, field(prefix, "max_distinct_keys"));
  if (!max_distinct_keys.ok()) {
    return max_distinct_keys.status();
  }
  if (max_distinct_keys.value().value() != dense_product) {
    return validation(prefix + ".max_distinct_keys must equal the checked dense_dimensions mixed-radix product.");
  }
  auto max_live_keys = required_quantity(specification.max_live_keys, field(prefix, "max_live_keys"));
  if (!max_live_keys.ok()) {
    return max_live_keys.status();
  }
  if (max_live_keys.value().value() > dense_product) {
    return validation(prefix + ".max_live_keys must not exceed key_domain_bound.");
  }
  auto slot_count = required_quantity(specification.slot_count, field(prefix, "slot_count"));
  if (!slot_count.ok()) {
    return slot_count.status();
  }
  if (slot_count.value().value() != max_live_keys.value().value()) {
    return validation(prefix + ".slot_count must equal max_live_keys for the fixed physical slot pool.");
  }
  auto initial_generation = required_quantity(specification.initial_generation, field(prefix, "initial_generation"));
  if (!initial_generation.ok()) {
    return initial_generation.status();
  }
  if (initial_generation.value().value() != kInitialKeySlotGeneration) {
    return validation(prefix + ".initial_generation must equal " + std::to_string(kInitialKeySlotGeneration) + ".");
  }
  if (!specification.seal_on_completion) {
    return validation(prefix + ".seal_on_completion must be true for completed-only KeySlot eviction.");
  }
  auto host_metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  if (!host_metadata.ok()) {
    return host_metadata.status();
  }
  auto expected_host_metadata = dense_key_slot_host_metadata_charged_bytes(
    key_domain_bound.value().value(), slot_count.value().value(), field(prefix, "host_metadata_charged_bytes"));
  if (!expected_host_metadata.ok()) {
    return expected_host_metadata.status();
  }
  if (host_metadata.value().value() != expected_host_metadata.value()) {
    return validation(prefix + ".host_metadata_charged_bytes does not match '" +
                      std::string(kDenseKeySlotStorageAccountingId) + "'.");
  }
  auto max_items = required_quantity(specification.max_items_per_activation, field(prefix, "max_items_per_activation"));
  if (!max_items.ok()) {
    return max_items.status();
  }
  auto max_bytes = required_quantity(specification.max_charged_bytes_per_activation,
                                     field(prefix, "max_charged_bytes_per_activation"));
  if (!max_bytes.ok()) {
    return max_bytes.status();
  }
  return KeySlotTablePlan::from_validated(
    specification.node_id, std::move(dimensions), specification.mapping_algorithm_id,
    specification.storage_accounting_id, std::move(key_domain_bound).value(), std::move(max_distinct_keys).value(),
    std::move(max_live_keys).value(), std::move(slot_count).value(), specification.generation_policy,
    std::move(initial_generation).value(), specification.seal_on_completion, specification.eviction_policy,
    specification.late_event_policy, std::move(host_metadata).value(), std::move(max_items).value(),
    std::move(max_bytes).value());
}

[[nodiscard]] Result<ReorderPlan> to_reorder_plan(const ReorderPlanSpec& specification, const std::size_t index) {
  const std::string prefix = "reorder_plans[" + std::to_string(index) + "]";
  if (specification.node_id.empty()) {
    return validation(prefix + ".node_id must not be empty.");
  }
  if (specification.order_domain_id.empty()) {
    return validation(prefix + ".order_domain_id must not be empty.");
  }
  if (specification.ordinal_binding_id != kCompletedFrameSlotContextSemanticKeyOrdinalBindingId) {
    return validation(prefix + ".ordinal_binding_id must be '" +
                      std::string(kCompletedFrameSlotContextSemanticKeyOrdinalBindingId) + "'.");
  }
  if (specification.completed_frame_input_port.empty()) {
    return validation(prefix + ".completed_frame_input_port must not be empty.");
  }
  if (specification.ordered_output_port.empty()) {
    return validation(prefix + ".ordered_output_port must not be empty.");
  }
  auto outputs_per_ordinal = required_quantity(specification.outputs_per_ordinal, field(prefix, "outputs_per_ordinal"));
  if (!outputs_per_ordinal.ok()) {
    return outputs_per_ordinal.status();
  }
  if (outputs_per_ordinal.value().value() != 1U) {
    return validation(prefix + ".outputs_per_ordinal must equal 1 in M3.");
  }
  auto charged_bytes_per_ordinal =
    required_quantity(specification.charged_bytes_per_ordinal, field(prefix, "charged_bytes_per_ordinal"));
  if (!charged_bytes_per_ordinal.ok()) {
    return charged_bytes_per_ordinal.status();
  }
  if (specification.ordinal_dimensions.empty()) {
    return validation(prefix + ".ordinal_dimensions must contain at least one XML-derived Cartesian axis.");
  }

  Quantity dense_product = 1U;
  std::vector<DenseCartesianOrdinalDimension> dimensions;
  dimensions.reserve(specification.ordinal_dimensions.size());
  std::vector<std::string_view> fields;
  fields.reserve(specification.ordinal_dimensions.size());
  for (std::size_t dimension_index = 0; dimension_index < specification.ordinal_dimensions.size(); ++dimension_index) {
    const auto& dimension = specification.ordinal_dimensions[dimension_index];
    const auto dimension_prefix = prefix + ".ordinal_dimensions[" + std::to_string(dimension_index) + "]";
    if (dimension.field.empty()) {
      return validation(dimension_prefix + ".field must not be empty.");
    }
    auto minimum = canonical_quantity(dimension.minimum, field(dimension_prefix, "minimum"));
    if (!minimum.ok()) {
      return minimum.status();
    }
    auto cardinality = required_quantity(dimension.cardinality, field(dimension_prefix, "cardinality"));
    if (!cardinality.ok()) {
      return cardinality.status();
    }
    auto next_product =
      checked_multiply(dense_product, cardinality.value().value(), prefix + ".ordinal_dimensions mixed-radix product");
    if (!next_product.ok()) {
      return next_product.status();
    }
    dense_product = next_product.value();
    fields.emplace_back(dimension.field);
    dimensions.push_back(DenseCartesianOrdinalDimension::from_validated(dimension.field, std::move(minimum).value(),
                                                                        std::move(cardinality).value()));
  }
  std::sort(fields.begin(), fields.end());
  if (std::adjacent_find(fields.begin(), fields.end()) != fields.end()) {
    return validation(prefix + ".ordinal_dimensions must contain unique field values.");
  }

  if (specification.mapping_algorithm_id != kDenseCartesianReorderMappingAlgorithmId) {
    return validation(prefix + ".mapping_algorithm_id must be '" +
                      std::string(kDenseCartesianReorderMappingAlgorithmId) + "'.");
  }
  if (specification.storage_accounting_id != kDenseCartesianReorderStorageAccountingId) {
    return validation(prefix + ".storage_accounting_id must be '" +
                      std::string(kDenseCartesianReorderStorageAccountingId) + "'.");
  }
  if (specification.publish_policy != kNextExpectedOnlyReorderPublishPolicy) {
    return validation(prefix + ".publish_policy must be '" + std::string(kNextExpectedOnlyReorderPublishPolicy) + "'.");
  }
  if (!specification.certified_skipped_ordinals.empty()) {
    return validation(prefix + ".certified_skipped_ordinals must be empty in M3.");
  }
  if (specification.end_of_input_policy != kFailReorderEndOfInputPolicy) {
    return validation(prefix + ".end_of_input_policy must be '" + std::string(kFailReorderEndOfInputPolicy) +
                      "' in M3.");
  }
  if (specification.occurrence_policy != kStrictDenseAllTuplesReorderOccurrencePolicy) {
    return validation(prefix + ".occurrence_policy must be '" +
                      std::string(kStrictDenseAllTuplesReorderOccurrencePolicy) +
                      "'; XML limits define the expected tuple domain and runtime must fail at EndOfInput for any "
                      "missing completed FrameSlotContext.");
  }

  auto ordinal_domain = required_quantity(specification.ordinal_domain_bound, field(prefix, "ordinal_domain_bound"));
  if (!ordinal_domain.ok()) {
    return ordinal_domain.status();
  }
  if (ordinal_domain.value().value() != dense_product) {
    return validation(prefix + ".ordinal_domain_bound must equal the checked ordinal_dimensions mixed-radix product.");
  }
  auto first_expected =
    canonical_quantity(specification.first_expected_ordinal, field(prefix, "first_expected_ordinal"));
  if (!first_expected.ok()) {
    return first_expected.status();
  }
  if (first_expected.value().value() != kFirstExpectedReorderOrdinal) {
    return validation(prefix + ".first_expected_ordinal must equal " + std::to_string(kFirstExpectedReorderOrdinal) +
                      ".");
  }
  auto last_expected = canonical_quantity(specification.last_expected_ordinal, field(prefix, "last_expected_ordinal"));
  if (!last_expected.ok()) {
    return last_expected.status();
  }
  const auto expected_last = ordinal_domain.value().value() - 1U;
  if (last_expected.value().value() != expected_last) {
    return validation(prefix + ".last_expected_ordinal must equal ordinal_domain_bound - 1.");
  }
  auto max_ahead_items = required_quantity(specification.max_ahead_items, field(prefix, "max_ahead_items"));
  if (!max_ahead_items.ok()) {
    return max_ahead_items.status();
  }
  if (max_ahead_items.value().value() > ordinal_domain.value().value()) {
    return validation(prefix + ".max_ahead_items must not exceed ordinal_domain_bound.");
  }
  auto max_ahead_bytes =
    required_quantity(specification.max_ahead_charged_bytes, field(prefix, "max_ahead_charged_bytes"));
  if (!max_ahead_bytes.ok()) {
    return max_ahead_bytes.status();
  }
  auto required_ahead_bytes =
    checked_multiply(max_ahead_items.value().value(), charged_bytes_per_ordinal.value().value(),
                     prefix + ".max_ahead_items * charged_bytes_per_ordinal");
  if (!required_ahead_bytes.ok()) {
    return required_ahead_bytes.status();
  }
  if (max_ahead_bytes.value().value() < required_ahead_bytes.value()) {
    return validation(prefix + ".max_ahead_charged_bytes must cover max_ahead_items full OutputEnvelope "
                               "reservations.");
  }
  auto handle_storage =
    required_quantity(specification.handle_storage_charged_bytes, field(prefix, "handle_storage_charged_bytes"));
  if (!handle_storage.ok()) {
    return handle_storage.status();
  }
  auto expected_handle_storage =
    checked_multiply(max_ahead_items.value().value(), kDenseCartesianReorderHandleSidecarChargedBytes,
                     prefix + ".max_ahead_items * immutable handle sidecar bytes");
  if (!expected_handle_storage.ok()) {
    return expected_handle_storage.status();
  }
  if (handle_storage.value().value() != expected_handle_storage.value()) {
    return validation(prefix + ".handle_storage_charged_bytes must equal max_ahead_items * " +
                      std::to_string(kDenseCartesianReorderHandleSidecarChargedBytes) + ".");
  }
  auto max_gap = canonical_quantity(specification.max_gap_ordinals, field(prefix, "max_gap_ordinals"));
  if (!max_gap.ok()) {
    return max_gap.status();
  }
  const auto expected_max_gap = expected_last - kFirstExpectedReorderOrdinal;
  if (max_gap.value().value() != expected_max_gap) {
    return validation(prefix + ".max_gap_ordinals is a closed-domain arithmetic upper bound and must equal "
                               "ordinal_domain_bound - 1; it is not a scheduling or skip policy.");
  }
  auto host_metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  if (!host_metadata.ok()) {
    return host_metadata.status();
  }
  auto expected_host_metadata = dense_cartesian_reorder_host_metadata_charged_bytes(
    ordinal_domain.value().value(), max_ahead_items.value().value(), field(prefix, "host_metadata_charged_bytes"));
  if (!expected_host_metadata.ok()) {
    return expected_host_metadata.status();
  }
  if (host_metadata.value().value() != expected_host_metadata.value()) {
    return validation(prefix + ".host_metadata_charged_bytes does not match '" +
                      std::string(kDenseCartesianReorderStorageAccountingId) + "'.");
  }
  auto descriptor_charge =
    required_quantity(specification.descriptor_charged_count, field(prefix, "descriptor_charged_count"));
  if (!descriptor_charge.ok()) {
    return descriptor_charge.status();
  }
  if (descriptor_charge.value().value() != max_ahead_items.value().value()) {
    return validation(prefix + ".descriptor_charged_count must equal max_ahead_items.");
  }

  return ReorderPlan::from_validated(
    specification.node_id, std::move(dimensions), specification.order_domain_id, specification.ordinal_binding_id,
    specification.completed_frame_input_port, specification.ordered_output_port, std::move(outputs_per_ordinal).value(),
    std::move(charged_bytes_per_ordinal).value(), specification.mapping_algorithm_id,
    specification.storage_accounting_id, std::move(ordinal_domain).value(), std::move(first_expected).value(),
    std::move(last_expected).value(), std::move(max_ahead_items).value(), std::move(max_ahead_bytes).value(),
    std::move(max_gap).value(), specification.occurrence_policy, specification.publish_policy,
    specification.certified_skipped_ordinals, specification.end_of_input_policy, std::move(handle_storage).value(),
    std::move(host_metadata).value(), std::move(descriptor_charge).value());
}

[[nodiscard]] bool type_allows_host_normal(const TypeDescriptor& type_descriptor) {
  return std::find(type_descriptor.allowed_memory_domains().begin(), type_descriptor.allowed_memory_domains().end(),
                   TypeMemoryDomain::host_normal) != type_descriptor.allowed_memory_domains().end();
}

[[nodiscard]] Status validate_m37_pool_type(const TypeDescriptor& type_descriptor, const std::string_view prefix) {
  if (type_descriptor.payload_kind() != PayloadKind::buffer_handle ||
      type_descriptor.mutability() != PayloadMutability::immutable_after_publish ||
      !type_allows_host_normal(type_descriptor)) {
    return validation(std::string(prefix) +
                      ".type_descriptor must be an immutable_after_publish buffer_handle that permits host_normal.");
  }
  return Status::Ok();
}

[[nodiscard]] Result<BufferPoolPlan> to_buffer_pool_plan(const BufferPoolPlanSpec& specification,
                                                         const std::size_t index) {
  const std::string prefix = "buffer_pool_plans[" + std::to_string(index) + "]";
  if (specification.pool_id.empty() || specification.producer_node_id.empty() ||
      specification.producer_port_name.empty() || specification.producer_provider_id.empty() ||
      specification.producer_operator_id.empty()) {
    return validation(prefix + ".pool_id, producer node/port, producer_provider_id, and producer_operator_id must not "
                               "be empty.");
  }
  auto producer_bundle_digest =
    ArtifactDigest::parse(specification.producer_bundle_digest, field(prefix, "producer_bundle_digest"));
  if (!producer_bundle_digest.ok()) {
    return producer_bundle_digest.status();
  }
  auto producer_contract_digest =
    ArtifactDigest::parse(specification.producer_contract_digest, field(prefix, "producer_contract_digest"));
  if (!producer_contract_digest.ok()) {
    return producer_contract_digest.status();
  }
  const auto type_status = validate_m37_pool_type(specification.type_descriptor, prefix);
  if (!type_status.ok()) {
    return type_status;
  }
  if (specification.memory_domain != TypeMemoryDomain::host_normal) {
    return validation(prefix + ".memory_domain must be host_normal in M3.7.");
  }
  if (specification.storage_accounting_id != kM37BufferPoolStorageAccountingId) {
    return validation(prefix + ".storage_accounting_id must be '" + std::string(kM37BufferPoolStorageAccountingId) +
                      "'.");
  }
  auto slot_count = required_quantity(specification.slot_count, field(prefix, "slot_count"));
  if (!slot_count.ok()) {
    return slot_count.status();
  }
  auto payload_capacity =
    required_quantity(specification.payload_capacity_bytes, field(prefix, "payload_capacity_bytes"));
  if (!payload_capacity.ok()) {
    return payload_capacity.status();
  }
  auto metadata_capacity =
    canonical_quantity(specification.metadata_capacity_bytes, field(prefix, "metadata_capacity_bytes"));
  if (!metadata_capacity.ok()) {
    return metadata_capacity.status();
  }
  auto payload_alignment =
    required_quantity(specification.payload_alignment_bytes, field(prefix, "payload_alignment_bytes"));
  if (!payload_alignment.ok()) {
    return payload_alignment.status();
  }
  if (payload_alignment.value().value() != specification.type_descriptor.min_alignment_bytes()) {
    return validation(prefix + ".payload_alignment_bytes must exactly match type_descriptor.min_alignment_bytes.");
  }
  if (payload_capacity.value().value() % payload_alignment.value().value() != 0U) {
    return validation(prefix + ".payload_capacity_bytes must be an integral multiple of payload_alignment_bytes.");
  }
  auto metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  if (!metadata.ok()) {
    return metadata.status();
  }
  auto expected_metadata = m37_buffer_pool_host_metadata_charged_bytes(slot_count.value().value(),
                                                                       field(prefix, "host_metadata_charged_bytes"));
  if (!expected_metadata.ok()) {
    return expected_metadata.status();
  }
  if (metadata.value().value() != expected_metadata.value()) {
    return validation(prefix + ".host_metadata_charged_bytes does not match '" +
                      std::string(kM37BufferPoolStorageAccountingId) + "'.");
  }
  auto descriptors =
    required_quantity(specification.descriptor_charged_count, field(prefix, "descriptor_charged_count"));
  if (!descriptors.ok()) {
    return descriptors.status();
  }
  if (descriptors.value().value() != slot_count.value().value()) {
    return validation(prefix + ".descriptor_charged_count must equal slot_count.");
  }
  auto physical_charge = required_quantity(specification.physical_charge_bytes, field(prefix, "physical_charge_bytes"));
  if (!physical_charge.ok()) {
    return physical_charge.status();
  }
  auto expected_physical_charge =
    m37_buffer_pool_physical_charge_bytes(slot_count.value().value(), payload_capacity.value().value(),
                                          metadata_capacity.value().value(), field(prefix, "physical_charge_bytes"));
  if (!expected_physical_charge.ok()) {
    return expected_physical_charge.status();
  }
  if (physical_charge.value().value() != expected_physical_charge.value()) {
    return validation(prefix + ".physical_charge_bytes must exactly equal caller-slab payload + metadata + pool "
                               "control charge.");
  }
  return BufferPoolPlan::from_validated(
    specification.pool_id, specification.producer_node_id, specification.producer_port_name,
    specification.producer_provider_id, std::move(producer_bundle_digest).value(), specification.producer_operator_id,
    std::move(producer_contract_digest).value(), specification.type_descriptor, specification.memory_domain,
    std::move(slot_count).value(), std::move(payload_capacity).value(), std::move(metadata_capacity).value(),
    std::move(payload_alignment).value(), specification.storage_accounting_id, std::move(metadata).value(),
    std::move(descriptors).value(), std::move(physical_charge).value());
}

[[nodiscard]] Result<DataEdgePlan> to_data_edge_plan(const DataEdgePlanSpec& specification, const std::size_t index) {
  const std::string prefix = "data_edge_plans[" + std::to_string(index) + "]";
  if (specification.edge_id.empty() || specification.source_pool_id.empty() || specification.producer_node_id.empty() ||
      specification.producer_port_name.empty() || specification.consumer_node_id.empty() ||
      specification.consumer_port_name.empty()) {
    return validation(prefix + ".edge_id, source_pool_id, producer endpoint, and consumer endpoint must not be empty.");
  }
  const auto type_status = validate_m37_pool_type(specification.type_descriptor, prefix);
  if (!type_status.ok()) {
    return type_status;
  }
  auto producer_abi_port = canonical_quantity(specification.producer_abi_port, field(prefix, "producer_abi_port"));
  if (!producer_abi_port.ok()) {
    return producer_abi_port.status();
  }
  if (producer_abi_port.value().value() > kM37MaximumProducerAbiPort) {
    return validation(prefix + ".producer_abi_port exceeds the uint32 Provider ABI range.");
  }
  if (specification.storage_accounting_id != kM37DataEdgeStorageAccountingId) {
    return validation(prefix + ".storage_accounting_id must be '" + std::string(kM37DataEdgeStorageAccountingId) +
                      "'.");
  }
  if (specification.terminal_policy != kM37NormalEoiDrainCancellationFailTerminalPolicy) {
    return validation(prefix + ".terminal_policy must be '" +
                      std::string(kM37NormalEoiDrainCancellationFailTerminalPolicy) + "'.");
  }
  auto max_items = required_quantity(specification.max_items, field(prefix, "max_items"));
  if (!max_items.ok()) {
    return max_items.status();
  }
  auto max_logical_bytes = required_quantity(specification.max_logical_bytes, field(prefix, "max_logical_bytes"));
  if (!max_logical_bytes.ok()) {
    return max_logical_bytes.status();
  }
  auto metadata =
    required_quantity(specification.host_metadata_charged_bytes, field(prefix, "host_metadata_charged_bytes"));
  if (!metadata.ok()) {
    return metadata.status();
  }
  auto expected_metadata =
    m37_data_edge_host_metadata_charged_bytes(max_items.value().value(), field(prefix, "host_metadata_charged_bytes"));
  if (!expected_metadata.ok()) {
    return expected_metadata.status();
  }
  if (metadata.value().value() != expected_metadata.value()) {
    return validation(prefix + ".host_metadata_charged_bytes does not match '" +
                      std::string(kM37DataEdgeStorageAccountingId) + "'.");
  }
  auto descriptors =
    required_quantity(specification.descriptor_charged_count, field(prefix, "descriptor_charged_count"));
  if (!descriptors.ok()) {
    return descriptors.status();
  }
  if (descriptors.value().value() != max_items.value().value()) {
    return validation(prefix + ".descriptor_charged_count must equal max_items.");
  }
  auto firing_lease_staging_bytes = required_quantity(specification.firing_lease_staging_charged_bytes,
                                                      field(prefix, "firing_lease_staging_charged_bytes"));
  if (!firing_lease_staging_bytes.ok()) {
    return firing_lease_staging_bytes.status();
  }
  if (firing_lease_staging_bytes.value().value() != kM37FiringLeaseHostStagingChargedBytes) {
    return validation(prefix + ".firing_lease_staging_charged_bytes must equal the frozen M3.7 synchronous "
                               "Provider ABI staging charge.");
  }
  auto firing_lease_staging_descriptors = required_quantity(specification.firing_lease_staging_descriptor_count,
                                                            field(prefix, "firing_lease_staging_descriptor_count"));
  if (!firing_lease_staging_descriptors.ok()) {
    return firing_lease_staging_descriptors.status();
  }
  if (firing_lease_staging_descriptors.value().value() != kM37FiringLeaseHostStagingDescriptorCount) {
    return validation(prefix + ".firing_lease_staging_descriptor_count must equal the frozen M3.7 synchronous "
                               "Provider ABI staging descriptor charge.");
  }
  return DataEdgePlan::from_validated(
    specification.edge_id, specification.source_pool_id, specification.producer_node_id,
    specification.producer_port_name, std::move(producer_abi_port).value(), specification.consumer_node_id,
    specification.consumer_port_name, specification.type_descriptor, std::move(max_items).value(),
    std::move(max_logical_bytes).value(), specification.storage_accounting_id, std::move(metadata).value(),
    std::move(descriptors).value(), std::move(firing_lease_staging_bytes).value(),
    std::move(firing_lease_staging_descriptors).value(), specification.terminal_policy);
}

[[nodiscard]] Result<EdgeCapacity> to_edge_capacity(const EdgeCapacitySpec& specification, const std::size_t index) {
  const std::string prefix = "edge_capacities[" + std::to_string(index) + "]";
  if (specification.edge_id.empty()) {
    return validation(prefix + ".edge_id must not be empty.");
  }
  auto capacity = Capacity::create(specification.max_items, specification.max_charged_bytes, prefix);
  if (!capacity.ok()) {
    return capacity.status();
  }
  return EdgeCapacity::from_validated(specification.edge_id, std::move(capacity).value());
}

[[nodiscard]] bool is_valid(const ExecutionProfile value) noexcept {
  switch (value) {
    case ExecutionProfile::offline:
    case ExecutionProfile::bounded_online:
    case ExecutionProfile::isolated_strict_online:
    case ExecutionProfile::deadline_qualified_online:
    case ExecutionProfile::research_unbounded:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const PortDirection value) noexcept {
  return value == PortDirection::input || value == PortDirection::output;
}

[[nodiscard]] bool is_valid(const PortCardinality value) noexcept {
  return value == PortCardinality::single || value == PortCardinality::many;
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

[[nodiscard]] bool is_valid(const OrderDomain value) noexcept {
  return value == OrderDomain::strict_global || value == OrderDomain::per_key || value == OrderDomain::unordered;
}

[[nodiscard]] bool is_valid(const CallModel value) noexcept {
  return value == CallModel::serial || value == CallModel::keyed_parallel;
}

[[nodiscard]] bool is_valid(const RateKind value) noexcept {
  return value == RateKind::sdf || value == RateKind::csdf || value == RateKind::keyed_dynamic;
}

[[nodiscard]] bool is_valid(const CompletionKind value) noexcept {
  switch (value) {
    case CompletionKind::expected_count:
    case CompletionKind::header_predicate:
    case CompletionKind::watermark:
    case CompletionKind::end_of_key:
    case CompletionKind::end_of_input:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_valid(const EndOfInputPolicy value) noexcept {
  return value == EndOfInputPolicy::fail || value == EndOfInputPolicy::partial_output ||
         value == EndOfInputPolicy::skip;
}

[[nodiscard]] bool is_valid(const CalibrationRole value) noexcept {
  return value == CalibrationRole::none || value == CalibrationRole::producer || value == CalibrationRole::consumer;
}

[[nodiscard]] bool is_valid(const TerminalBehavior value) noexcept {
  return value == TerminalBehavior::none || value == TerminalBehavior::flush_declared ||
         value == TerminalBehavior::cleanup_declared;
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

[[nodiscard]] bool is_valid(const ChannelGroupSource value) noexcept {
  return value == ChannelGroupSource::acquisition_active_channel_range;
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

[[nodiscard]] const PortSpec* find_port(const std::vector<PortSpec>& ports, const std::string_view name) noexcept {
  const auto found = std::find_if(ports.begin(), ports.end(), [name](const PortSpec& port) {
    return port.name == name;
  });
  return found == ports.end() ? nullptr : &*found;
}

[[nodiscard]] Status validate_port_rate_entries(const std::vector<PortRateSpec>& entries,
                                                const std::vector<PortSpec>& ports, const PortDirection direction,
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

[[nodiscard]] Status validate_dynamic_phase(const DynamicPhaseBoundSpec& phase, const std::vector<PortSpec>& ports,
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

[[nodiscard]] Status validate_operator_rates(const RateSpec& rates, const std::vector<PortSpec>& ports) {
  if (!is_valid(rates.kind)) {
    return validation("rates.kind is invalid.");
  }
  if (!is_valid(rates.completion.kind) || !is_valid(rates.completion.on_end_of_input)) {
    return validation("rates.completion contains an invalid enum value.");
  }
  if (rates.completion.kind == CompletionKind::expected_count) {
    if (rates.completion.expected_count == 0) {
      return validation("rates.completion.expected_count must be greater than zero for expected_count.");
    }
  } else if (rates.completion.expected_count != 0) {
    return validation("rates.completion.expected_count is valid only for expected_count completion.");
  }
  auto completion_count = validate_quantity(rates.completion.expected_count, "rates.completion.expected_count");
  if (!completion_count.ok()) {
    return completion_count;
  }

  if (rates.kind == RateKind::keyed_dynamic) {
    if (!rates.static_phases.empty()) {
      return validation("keyed_dynamic rates must not declare static_phases.");
    }
    auto ordinary = validate_dynamic_phase(rates.ordinary, ports, true, "rates.ordinary");
    if (!ordinary.ok()) {
      return ordinary;
    }
    auto flush = validate_dynamic_phase(rates.normal_flush, ports, false, "rates.normal_flush");
    if (!flush.ok()) {
      return flush;
    }
    auto cleanup = validate_dynamic_phase(rates.cancel_cleanup, ports, false, "rates.cancel_cleanup");
    if (!cleanup.ok()) {
      return cleanup;
    }
    if (!rates.cancel_cleanup.outputs.empty()) {
      return validation("rates.cancel_cleanup.outputs must be empty: v1 cancellation cleanup cannot publish ordinary "
                        "MRI data.");
    }
    return Status::Ok();
  }

  const std::size_t expected_phase_count = rates.kind == RateKind::sdf ? 1U : 2U;
  if (rates.static_phases.size() != expected_phase_count &&
      !(rates.kind == RateKind::csdf && rates.static_phases.size() > 1U)) {
    return validation(rates.kind == RateKind::sdf ? "SDF rates must declare exactly one static phase."
                                                  : "CSDF rates must declare at least two static phases.");
  }
  if (rates.ordinary.max_firings != 0 || rates.normal_flush.max_firings != 0 || rates.cancel_cleanup.max_firings != 0 ||
      !rates.ordinary.outputs.empty() || !rates.normal_flush.outputs.empty() || !rates.cancel_cleanup.outputs.empty()) {
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
// scheduler's activation item cap.  keyed_dynamic deliberately has no v1
// input-rate model, so it is not inferred from its output-only phase bounds.
[[nodiscard]] Status validate_static_batch_activation_feasibility(const RateSpec& rates, const OperatorBatchSpec& batch,
                                                                  const OperatorExecutionShapeSpec& execution) {
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
// from TerminalContractSpec, while cancellation owns no data-output grant.
[[nodiscard]] Result<OutputAggregate> ordinary_output_bound(const RateSpec& rates) {
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

[[nodiscard]] Result<OutputAggregate> normal_flush_output_bound(const RateSpec& rates) {
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

[[nodiscard]] Status validate_output_resource_coverage(const OperatorResourceSpec& resources, const RateSpec& rates,
                                                       const TerminalContractSpec& terminal) {
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

[[nodiscard]] Status validate_operator_execution(const OperatorExecutionShapeSpec& execution) {
  if (!is_valid(execution.input_granularity) || !is_valid(execution.order_domain) || !is_valid(execution.call_model)) {
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
      return validation("execution.partition_key may use channel_group only with ChannelGroupSpec.");
    }
  } else {
    const auto& group = *execution.channel_group;
    if (!partitions_channel_groups) {
      return validation("ChannelGroupSpec requires channel_group in execution.partition_key.");
    }
    if (!is_valid(group.source)) {
      return validation("execution.channel_group.source is invalid.");
    }
    if (group.channels_per_group == 0 || group.max_active_channels == 0 || group.max_groups == 0 ||
        group.max_charged_bytes_per_group == 0) {
      return validation("ChannelGroupSpec requires finite non-zero group, channel, group-count, and byte bounds.");
    }
    for (const auto& [value, name] : std::array{
           std::pair{group.channels_per_group, "execution.channel_group.channels_per_group"},
           std::pair{group.max_active_channels, "execution.channel_group.max_active_channels"},
           std::pair{group.max_groups, "execution.channel_group.max_groups"},
           std::pair{group.max_charged_bytes_per_group, "execution.channel_group.max_charged_bytes_per_group"},
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
  if (execution.max_active_keys == 0 || execution.max_in_flight == 0 || execution.max_items_per_activation == 0 ||
      execution.cooperative_quantum_us == 0) {
    return validation("execution max_active_keys, max_in_flight, max_items_per_activation, and cooperative_quantum_us "
                      "must be greater than zero.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{execution.max_active_keys, "execution.max_active_keys"},
         std::pair{execution.max_in_flight, "execution.max_in_flight"},
         std::pair{execution.max_items_per_activation, "execution.max_items_per_activation"},
         std::pair{execution.cooperative_quantum_us, "execution.cooperative_quantum_us"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_operator_batch(const OperatorBatchSpec& batch) {
  if (batch.min_items == 0 || batch.preferred_items == 0 || batch.max_items == 0 || batch.max_charged_bytes == 0 ||
      batch.min_items > batch.preferred_items || batch.preferred_items > batch.max_items) {
    return validation("batch must satisfy 0 < min_items <= preferred_items <= max_items and max_charged_bytes > 0.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{batch.min_items, "batch.min_items"},
         std::pair{batch.preferred_items, "batch.preferred_items"},
         std::pair{batch.max_items, "batch.max_items"},
         std::pair{batch.max_charged_bytes, "batch.max_charged_bytes"},
         std::pair{batch.max_wait_us, "batch.max_wait_us"},
       }) {
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_operator_resources(const OperatorResourceSpec& resources) {
  if (!is_valid(resources.memory_domain)) {
    return validation("resources.memory_domain is invalid.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{resources.scratch_charged_bytes_per_firing, "resources.scratch_charged_bytes_per_firing"},
         std::pair{resources.per_key_state_charged_bytes, "resources.per_key_state_charged_bytes"},
         std::pair{resources.per_scan_workspace_charged_bytes, "resources.per_scan_workspace_charged_bytes"},
         std::pair{resources.retention_items, "resources.retention_items"},
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

[[nodiscard]] Status validate_calibration(const CalibrationSpec& calibration) {
  if (!is_valid(calibration.role) || !is_valid(calibration.on_end_of_input)) {
    return validation("calibration contains an invalid enum value.");
  }
  if (!calibration.single_epoch_v1) {
    return validation("calibration.single_epoch_v1 must be true in OperatorContract v1.");
  }
  auto projection = validate_unique_optional_strings(calibration.key_projection, "calibration.key_projection");
  if (!projection.ok()) {
    return projection;
  }
  const bool has_bound = calibration.max_active_keys != 0 || calibration.precalibration_horizon_items != 0 ||
                         calibration.precalibration_horizon_charged_bytes != 0 ||
                         calibration.max_calibration_frame_charged_bytes != 0 ||
                         calibration.max_decoder_staging_bytes != 0;
  if (calibration.role == CalibrationRole::none) {
    if (!calibration.binding_id.empty() || !calibration.key_projection.empty() || has_bound) {
      return validation("calibration role none must not declare a binding, key projection, or resource bound.");
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

[[nodiscard]] Status validate_join(const std::optional<JoinSpec>& join, const std::vector<PortSpec>& ports,
                                   const std::vector<ExecutionProfile>& supported_profiles) {
  if (!join.has_value()) {
    return Status::Ok();
  }
  const auto& specification = *join;
  if (!is_valid(specification.on_end_of_input) || !is_valid(specification.progress_proof)) {
    return validation("join contains an invalid EndOfInputPolicy or JoinProgressProof.");
  }
  if (!specification.exactly_once) {
    return validation("JoinSpec.exactly_once must be true in OperatorContract v1.");
  }
  if (specification.inputs.size() < 2U) {
    return validation("JoinSpec requires at least two declared input ports.");
  }
  std::vector<std::string> input_names;
  input_names.reserve(specification.inputs.size());
  for (std::size_t index = 0; index < specification.inputs.size(); ++index) {
    const auto& input = specification.inputs[index];
    const std::string prefix = "join.inputs[" + std::to_string(index) + "]";
    const auto* port = find_port(ports, input.port_name);
    if (port == nullptr || port->direction != PortDirection::input) {
      return validation(prefix + " must refer to a declared input port.");
    }
    auto projection = validate_unique_optional_strings(input.key_projection, prefix + ".key_projection");
    if (!projection.ok()) {
      return projection;
    }
    if (input.required_items_per_key == 0) {
      return validation(prefix + ".required_items_per_key must be greater than zero.");
    }
    auto items = validate_quantity(input.required_items_per_key, prefix + ".required_items_per_key");
    if (!items.ok()) {
      return items;
    }
    input_names.push_back(input.port_name);
  }
  if (!has_nonempty_unique_strings(input_names)) {
    return validation("JoinSpec.inputs must not repeat a port name.");
  }
  for (const auto& [value, name] : std::array{
         std::pair{specification.max_retained_items_per_key, "join.max_retained_items_per_key"},
         std::pair{specification.max_retained_charged_bytes_per_key, "join.max_retained_charged_bytes_per_key"},
         std::pair{specification.max_retained_items_aggregate, "join.max_retained_items_aggregate"},
         std::pair{specification.max_retained_charged_bytes_aggregate, "join.max_retained_charged_bytes_aggregate"},
       }) {
    if (value == 0) {
      return validation(std::string(name) + " must be greater than zero for JoinSpec.");
    }
    auto status = validate_quantity(value, name);
    if (!status.ok()) {
      return status;
    }
  }
  if (specification.max_retained_items_aggregate < specification.max_retained_items_per_key ||
      specification.max_retained_charged_bytes_aggregate < specification.max_retained_charged_bytes_per_key) {
    return validation("JoinSpec aggregate retention must cover one per-key retention bound.");
  }
  const bool supports_online = std::find(supported_profiles.begin(), supported_profiles.end(),
                                         ExecutionProfile::bounded_online) != supported_profiles.end() ||
                               std::find(supported_profiles.begin(), supported_profiles.end(),
                                         ExecutionProfile::isolated_strict_online) != supported_profiles.end() ||
                               std::find(supported_profiles.begin(), supported_profiles.end(),
                                         ExecutionProfile::deadline_qualified_online) != supported_profiles.end();
  if (supports_online && specification.progress_proof == JoinProgressProof::none) {
    return validation("online JoinSpec requires verified_schedule_automaton or cohort_reservation proof.");
  }
  return Status::Ok();
}

[[nodiscard]] Result<Quantity> derive_m3_ordered_output_charged_bytes(const ReorderSpec& specification,
                                                                      const std::vector<PortSpec>& ports,
                                                                      const RateSpec& rates) {
  const auto* port = find_port(ports, specification.ordered_output_port);
  if (port == nullptr || port->direction != PortDirection::output) {
    return validation("M3 ReorderSpec.ordered_output_port must name a declared output port.");
  }

  std::optional<Quantity> charged_bytes;
  const auto require_one_output_envelope = [&](const std::vector<PortRateSpec>& outputs,
                                               const std::string_view rate_name) -> Status {
    const auto found = std::find_if(outputs.begin(), outputs.end(), [&](const PortRateSpec& output) {
      return output.port_name == specification.ordered_output_port;
    });
    if (found == outputs.end()) {
      return validation("M3 ReorderSpec.ordered_output_port must have an ordinary rate in " + std::string(rate_name) +
                        ".");
    }
    if (found->items != specification.outputs_per_ordinal) {
      return validation("M3 ReorderSpec.outputs_per_ordinal must exactly match " + std::string(rate_name) +
                        " for the selected output port.");
    }
    if (found->charged_bytes == 0U) {
      return validation("M3 ReorderSpec selected output port must declare a positive charged-byte bound in " +
                        std::string(rate_name) + ".");
    }
    if (charged_bytes.has_value() && *charged_bytes != found->charged_bytes) {
      return validation("M3 ReorderSpec selected output port must have one fixed charged-byte bound across every "
                        "ordinary rate phase.");
    }
    charged_bytes = found->charged_bytes;
    return Status::Ok();
  };

  if (rates.kind == RateKind::keyed_dynamic) {
    auto ordinary = require_one_output_envelope(rates.ordinary.outputs, "rates.ordinary.outputs");
    if (!ordinary.ok()) {
      return ordinary;
    }
    const auto terminal = std::find_if(rates.normal_flush.outputs.begin(), rates.normal_flush.outputs.end(),
                                       [&](const PortRateSpec& output) {
                                         return output.port_name == specification.ordered_output_port;
                                       });
    if (terminal != rates.normal_flush.outputs.end()) {
      return validation("M3 ReorderSpec selected output port must not be emitted by normal_flush; its finite "
                        "ordinal domain covers only ordinary OutputEnvelopes.");
    }
  } else {
    for (std::size_t index = 0U; index < rates.static_phases.size(); ++index) {
      auto phase = require_one_output_envelope(rates.static_phases[index].outputs,
                                               "rates.static_phases[" + std::to_string(index) + "].outputs");
      if (!phase.ok()) {
        return phase;
      }
    }
  }
  if (!charged_bytes.has_value()) {
    return validation("M3 ReorderSpec has no selected ordinary output rate.");
  }
  return *charged_bytes;
}

// M3's ordinal binding is deliberately narrower than a generic operator
// firing model.  A plan may claim one ordinal only for one completed host
// FrameSlotContext entering a named ksj.kspace-frame input port and producing
// exactly one selected OutputEnvelope in the same SDF firing.  This prevents
// an acquisition callback, a multi-frame batch, or Provider-local firing
// order from being silently reinterpreted as a dense ordinal source.
[[nodiscard]] Status validate_m3_completed_frame_slot_binding(const ReorderSpec& specification,
                                                              const OperatorExecutionShapeSpec& execution,
                                                              const OperatorBatchSpec& batch,
                                                              const std::vector<PortSpec>& ports,
                                                              const RateSpec& rates) {
  if (execution.input_granularity != InputGranularity::frame) {
    return validation("M3 ReorderSpec requires execution.input_granularity=frame for the completed "
                      "FrameSlotContext ordinal binding.");
  }
  if (execution.max_items_per_activation != 1U) {
    return validation("M3 ReorderSpec requires execution.max_items_per_activation=1 for one completed FrameSlot "
                      "per activation.");
  }
  if (batch.min_items != 1U || batch.preferred_items != 1U || batch.max_items != 1U) {
    return validation("M3 ReorderSpec requires batch min_items, preferred_items, and max_items to all equal 1 "
                      "for one completed FrameSlot per activation.");
  }
  if (specification.completed_frame_input_port.empty()) {
    return validation("M3 ReorderSpec requires one completed_frame_input_port.");
  }
  const auto* frame_port = find_port(ports, specification.completed_frame_input_port);
  if (frame_port == nullptr || frame_port->direction != PortDirection::input) {
    return validation("M3 ReorderSpec.completed_frame_input_port must name a declared input port.");
  }
  auto completed_frame_type = completed_frame_slot_context_type();
  if (!completed_frame_type.ok()) {
    return completed_frame_type.status();
  }
  if (!frame_port->type_descriptor.exactly_matches(completed_frame_type.value())) {
    return validation("M3 ReorderSpec.completed_frame_input_port must exactly match the frozen immutable "
                      "ksj.kspace-frame FrameSlotContext TypeDescriptor ABI.");
  }
  if (rates.kind != RateKind::sdf || rates.static_phases.size() != 1U) {
    return validation("M3 ReorderSpec requires one SDF phase so the selected output has an exact one-per-completed-"
                      "FrameSlot rate; CSDF and keyed_dynamic are unsupported.");
  }
  const auto& phase = rates.static_phases.front();
  const auto frame_input = std::find_if(phase.inputs.begin(), phase.inputs.end(), [&](const PortRateSpec& input) {
    return input.port_name == specification.completed_frame_input_port;
  });
  if (phase.inputs.size() != 1U || frame_input == phase.inputs.end() || frame_input->items != 1U) {
    return validation("M3 ReorderSpec completed_frame_input_port must consume exactly one completed FrameSlot in "
                      "the SDF phase.");
  }
  auto aggregate_inputs = sum_input_bounds(phase.inputs, "M3 ReorderSpec SDF frame input rate");
  if (!aggregate_inputs.ok()) {
    return aggregate_inputs.status();
  }
  if (aggregate_inputs.value().items != 1U) {
    return validation("M3 ReorderSpec SDF phase must consume exactly one total input item; multi-input frame "
                      "aggregation is unsupported.");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_reorder(const std::optional<ReorderSpec>& reorder,
                                      const OperatorExecutionShapeSpec& execution, const OperatorBatchSpec& batch,
                                      const std::vector<PortSpec>& ports, const RateSpec& rates) {
  if (!reorder.has_value()) {
    return Status::Ok();
  }
  const auto& specification = *reorder;
  if (execution.order_domain == OrderDomain::unordered) {
    return validation("ReorderSpec is incompatible with unordered execution.order_domain.");
  }
  const auto frame_binding = validate_m3_completed_frame_slot_binding(specification, execution, batch, ports, rates);
  if (!frame_binding.ok()) {
    return frame_binding;
  }
  if (!is_valid(specification.missing_at_end_of_input)) {
    return validation("reorder.missing_at_end_of_input is invalid.");
  }
  if (specification.missing_at_end_of_input != EndOfInputPolicy::fail) {
    return validation("M3 ReorderSpec requires fail for every missing ordinal at EndOfInput; skipped and partial "
                      "ordinals are unsupported.");
  }
  if (specification.ordered_output_port.empty()) {
    return validation("M3 ReorderSpec requires one ordered_output_port.");
  }
  if (specification.outputs_per_ordinal != 1U) {
    return validation("M3 ReorderSpec.outputs_per_ordinal must equal 1.");
  }
  auto outputs_per_ordinal = validate_quantity(specification.outputs_per_ordinal, "reorder.outputs_per_ordinal");
  if (!outputs_per_ordinal.ok()) {
    return outputs_per_ordinal;
  }
  auto projection = validate_unique_optional_strings(specification.order_projection, "reorder.order_projection");
  if (!projection.ok()) {
    return projection;
  }
  if (specification.order_projection.empty()) {
    return validation("M3 ReorderSpec requires a non-empty XML-derived Cartesian order_projection.");
  }
  if (execution.channel_group.has_value() || std::find(execution.partition_key.begin(), execution.partition_key.end(),
                                                       "channel_group") != execution.partition_key.end()) {
    return validation("M3 ReorderSpec forbids channel_group; a completed FrameSlotContext semantic key has no "
                      "channel-group ordinal axis.");
  }
  if (execution.partition_key != specification.order_projection) {
    return validation("M3 ReorderSpec requires execution.partition_key to exactly equal order_projection in the "
                      "same order, so the node KeySlotTable and FrameSlotContext ordinal have one identity.");
  }
  if (specification.max_ahead_items == 0 || specification.max_ahead_charged_bytes == 0) {
    return validation("ReorderSpec requires finite non-zero ahead item and byte bounds.");
  }
  auto items = validate_quantity(specification.max_ahead_items, "reorder.max_ahead_items");
  if (!items.ok()) {
    return items;
  }
  auto bytes = validate_quantity(specification.max_ahead_charged_bytes, "reorder.max_ahead_charged_bytes");
  if (!bytes.ok()) {
    return bytes;
  }
  auto output_bytes = derive_m3_ordered_output_charged_bytes(specification, ports, rates);
  if (!output_bytes.ok()) {
    return output_bytes.status();
  }
  auto required_ahead_bytes = checked_multiply(specification.max_ahead_items, output_bytes.value(),
                                               "reorder.max_ahead_items * selected output charged bytes");
  if (!required_ahead_bytes.ok()) {
    return required_ahead_bytes.status();
  }
  if (specification.max_ahead_charged_bytes < required_ahead_bytes.value()) {
    return validation("M3 ReorderSpec.max_ahead_charged_bytes must cover max_ahead_items full selected-output "
                      "reservations.");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_terminal_contract(const TerminalContractSpec& terminal) {
  if (!is_valid(terminal.normal) || !is_valid(terminal.cancel)) {
    return validation("terminal contains an invalid enum value.");
  }
  if (terminal.normal == TerminalBehavior::cleanup_declared || terminal.cancel == TerminalBehavior::flush_declared) {
    return validation(
      "normal terminal behavior may only be none or flush_declared; cancel behavior may only be none or "
      "cleanup_declared.");
  }
  const bool normal_bounds = terminal.normal_max_output_items != 0 || terminal.normal_max_output_charged_bytes != 0 ||
                             terminal.normal_max_async_tokens != 0;
  const bool cancel_bounds = terminal.cancel_max_async_tokens != 0;
  if ((terminal.normal == TerminalBehavior::none && normal_bounds) ||
      (terminal.cancel == TerminalBehavior::none && cancel_bounds)) {
    return validation("terminal bounds require their corresponding declared terminal behavior.");
  }
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
    case ExecutionProfile::offline:
      return "offline";
    case ExecutionProfile::bounded_online:
      return "bounded-online";
    case ExecutionProfile::isolated_strict_online:
      return "isolated-strict-online";
    case ExecutionProfile::deadline_qualified_online:
      return "deadline-qualified-online";
    case ExecutionProfile::research_unbounded:
      return "research-unbounded";
  }
  return "unknown";
}

Result<ExecutionProfile> parse_execution_profile(const std::string_view value) {
  if (value == "offline") {
    return ExecutionProfile::offline;
  }
  if (value == "bounded-online") {
    return ExecutionProfile::bounded_online;
  }
  if (value == "isolated-strict-online") {
    return ExecutionProfile::isolated_strict_online;
  }
  if (value == "deadline-qualified-online") {
    return ExecutionProfile::deadline_qualified_online;
  }
  if (value == "research-unbounded") {
    return ExecutionProfile::research_unbounded;
  }
  return invalid("Unknown execution profile '" + std::string(value) + "'.");
}

bool requires_provider_isolation(const ExecutionProfile profile) noexcept {
  return profile == ExecutionProfile::isolated_strict_online || profile == ExecutionProfile::deadline_qualified_online;
}

bool is_currently_supported_in_process(const ExecutionProfile profile) noexcept {
  return profile == ExecutionProfile::offline || profile == ExecutionProfile::bounded_online;
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
                                                  ArtifactDigest target_envelope, ArtifactDigest machine_policy,
                                                  std::vector<ArtifactDigest> provider_contracts) noexcept {
  return PlanInputDigests{std::move(resolved_pipeline), std::move(scan_descriptor), std::move(target_envelope),
                          std::move(machine_policy), std::move(provider_contracts)};
}

Result<Quantity> dense_key_slot_host_metadata_charged_bytes(const Quantity key_domain_bound, const Quantity slot_count,
                                                            const std::string_view field_name) {
  auto semantic_records = checked_multiply(key_domain_bound, kDenseKeySlotSemanticRecordChargedBytes,
                                           std::string(field_name) + ".semantic_records");
  if (!semantic_records.ok()) {
    return semantic_records.status();
  }
  auto physical_slots =
    checked_multiply(slot_count, kDenseKeySlotPhysicalSlotChargedBytes, std::string(field_name) + ".physical_slots");
  if (!physical_slots.ok()) {
    return physical_slots.status();
  }
  return checked_add(semantic_records.value(), physical_slots.value(),
                     std::string(field_name) + ".total_dense_key_slot_metadata");
}

Result<Quantity> dense_cartesian_reorder_host_metadata_charged_bytes(const Quantity ordinal_domain_bound,
                                                                     const Quantity max_ahead_items,
                                                                     const std::string_view field_name) {
  auto ordinal_records = checked_multiply(ordinal_domain_bound, kDenseCartesianReorderOrdinalRecordChargedBytes,
                                          std::string(field_name) + ".ordinal_records");
  if (!ordinal_records.ok()) {
    return ordinal_records.status();
  }
  auto buffered_slots = checked_multiply(max_ahead_items, kDenseCartesianReorderBufferedSlotChargedBytes,
                                         std::string(field_name) + ".buffered_slots");
  if (!buffered_slots.ok()) {
    return buffered_slots.status();
  }
  auto handle_sidecars = checked_multiply(max_ahead_items, kDenseCartesianReorderHandleSidecarChargedBytes,
                                          std::string(field_name) + ".immutable_handle_sidecars");
  if (!handle_sidecars.ok()) {
    return handle_sidecars.status();
  }
  auto records_and_slots = checked_add(ordinal_records.value(), buffered_slots.value(),
                                       std::string(field_name) + ".ordinal_records_and_buffered_slots");
  if (!records_and_slots.ok()) {
    return records_and_slots.status();
  }
  return checked_add(records_and_slots.value(), handle_sidecars.value(),
                     std::string(field_name) + ".total_dense_cartesian_reorder_metadata");
}

Result<Quantity> m37_buffer_pool_host_metadata_charged_bytes(const Quantity slot_count,
                                                             const std::string_view field_name) {
  return checked_multiply(slot_count, kM37BufferPoolControlChargedBytesPerSlot,
                          std::string(field_name) + ".buffer_pool_control");
}

Result<Quantity> m37_buffer_pool_physical_charge_bytes(const Quantity slot_count, const Quantity payload_capacity_bytes,
                                                       const Quantity metadata_capacity_bytes,
                                                       const std::string_view field_name) {
  auto payload_per_slot = checked_add(payload_capacity_bytes, metadata_capacity_bytes,
                                      std::string(field_name) + ".payload_and_metadata_per_slot");
  if (!payload_per_slot.ok()) {
    return payload_per_slot.status();
  }
  auto slab_per_slot = checked_add(payload_per_slot.value(), kM37BufferPoolControlChargedBytesPerSlot,
                                   std::string(field_name) + ".with_control_per_slot");
  if (!slab_per_slot.ok()) {
    return slab_per_slot.status();
  }
  return checked_multiply(slot_count, slab_per_slot.value(), std::string(field_name) + ".all_pool_slots");
}

Result<Quantity> m37_data_edge_host_metadata_charged_bytes(const Quantity max_items,
                                                           const std::string_view field_name) {
  return checked_multiply(max_items, kM37DataEdgeControlChargedBytesPerItem,
                          std::string(field_name) + ".data_edge_control");
}

DenseKeySlotDimension DenseKeySlotDimension::from_validated(std::string field, CanonicalQuantity minimum,
                                                            CanonicalQuantity cardinality) noexcept {
  return DenseKeySlotDimension{std::move(field), minimum, cardinality};
}

KeySlotTablePlan KeySlotTablePlan::from_validated(
  std::string node_id, std::vector<DenseKeySlotDimension> dense_dimensions, std::string mapping_algorithm_id,
  std::string storage_accounting_id, CanonicalQuantity key_domain_bound, CanonicalQuantity max_distinct_keys,
  CanonicalQuantity max_live_keys, CanonicalQuantity slot_count, std::string generation_policy,
  CanonicalQuantity initial_generation, const bool seal_on_completion, std::string eviction_policy,
  std::string late_event_policy, CanonicalQuantity host_metadata_charged_bytes,
  CanonicalQuantity max_items_per_activation, CanonicalQuantity max_charged_bytes_per_activation) noexcept {
  return KeySlotTablePlan{std::move(node_id),
                          std::move(dense_dimensions),
                          std::move(mapping_algorithm_id),
                          std::move(storage_accounting_id),
                          key_domain_bound,
                          max_distinct_keys,
                          max_live_keys,
                          slot_count,
                          std::move(generation_policy),
                          initial_generation,
                          seal_on_completion,
                          std::move(eviction_policy),
                          std::move(late_event_policy),
                          host_metadata_charged_bytes,
                          max_items_per_activation,
                          max_charged_bytes_per_activation};
}

DenseCartesianOrdinalDimension DenseCartesianOrdinalDimension::from_validated(std::string field,
                                                                              CanonicalQuantity minimum,
                                                                              CanonicalQuantity cardinality) noexcept {
  return DenseCartesianOrdinalDimension{std::move(field), minimum, cardinality};
}

ReorderPlan ReorderPlan::from_validated(
  std::string node_id, std::vector<DenseCartesianOrdinalDimension> ordinal_dimensions, std::string order_domain_id,
  std::string ordinal_binding_id, std::string completed_frame_input_port, std::string ordered_output_port,
  CanonicalQuantity outputs_per_ordinal, CanonicalQuantity charged_bytes_per_ordinal, std::string mapping_algorithm_id,
  std::string storage_accounting_id, CanonicalQuantity ordinal_domain_bound, CanonicalQuantity first_expected_ordinal,
  CanonicalQuantity last_expected_ordinal, CanonicalQuantity max_ahead_items, CanonicalQuantity max_ahead_charged_bytes,
  CanonicalQuantity max_gap_ordinals, std::string occurrence_policy, std::string publish_policy,
  std::vector<Quantity> certified_skipped_ordinals, std::string end_of_input_policy,
  CanonicalQuantity handle_storage_charged_bytes, CanonicalQuantity host_metadata_charged_bytes,
  CanonicalQuantity descriptor_charged_count) noexcept {
  return ReorderPlan{std::move(node_id),
                     std::move(ordinal_dimensions),
                     std::move(order_domain_id),
                     std::move(ordinal_binding_id),
                     std::move(completed_frame_input_port),
                     std::move(ordered_output_port),
                     outputs_per_ordinal,
                     charged_bytes_per_ordinal,
                     std::move(mapping_algorithm_id),
                     std::move(storage_accounting_id),
                     ordinal_domain_bound,
                     first_expected_ordinal,
                     last_expected_ordinal,
                     max_ahead_items,
                     max_ahead_charged_bytes,
                     max_gap_ordinals,
                     std::move(occurrence_policy),
                     std::move(publish_policy),
                     std::move(certified_skipped_ordinals),
                     std::move(end_of_input_policy),
                     handle_storage_charged_bytes,
                     host_metadata_charged_bytes,
                     descriptor_charged_count};
}

BufferPoolPlan BufferPoolPlan::from_validated(
  std::string pool_id, std::string producer_node_id, std::string producer_port_name, std::string producer_provider_id,
  ArtifactDigest producer_bundle_digest, std::string producer_operator_id, ArtifactDigest producer_contract_digest,
  TypeDescriptor type_descriptor, const TypeMemoryDomain memory_domain, CanonicalQuantity slot_count,
  CanonicalQuantity payload_capacity_bytes, CanonicalQuantity metadata_capacity_bytes,
  CanonicalQuantity payload_alignment_bytes, std::string storage_accounting_id,
  CanonicalQuantity host_metadata_charged_bytes, CanonicalQuantity descriptor_charged_count,
  CanonicalQuantity physical_charge_bytes) noexcept {
  return BufferPoolPlan{std::move(pool_id),
                        std::move(producer_node_id),
                        std::move(producer_port_name),
                        std::move(producer_provider_id),
                        std::move(producer_bundle_digest),
                        std::move(producer_operator_id),
                        std::move(producer_contract_digest),
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

DataEdgePlan DataEdgePlan::from_validated(
  std::string edge_id, std::string source_pool_id, std::string producer_node_id, std::string producer_port_name,
  CanonicalQuantity producer_abi_port, std::string consumer_node_id, std::string consumer_port_name,
  TypeDescriptor type_descriptor, CanonicalQuantity max_items, CanonicalQuantity max_logical_bytes,
  std::string storage_accounting_id, CanonicalQuantity host_metadata_charged_bytes,
  CanonicalQuantity descriptor_charged_count, CanonicalQuantity firing_lease_staging_charged_bytes,
  CanonicalQuantity firing_lease_staging_descriptor_count, std::string terminal_policy) noexcept {
  return DataEdgePlan{std::move(edge_id),
                      std::move(source_pool_id),
                      std::move(producer_node_id),
                      std::move(producer_port_name),
                      producer_abi_port,
                      std::move(consumer_node_id),
                      std::move(consumer_port_name),
                      std::move(type_descriptor),
                      max_items,
                      max_logical_bytes,
                      std::move(storage_accounting_id),
                      host_metadata_charged_bytes,
                      descriptor_charged_count,
                      firing_lease_staging_charged_bytes,
                      firing_lease_staging_descriptor_count,
                      std::move(terminal_policy)};
}

EdgeCapacity EdgeCapacity::from_validated(std::string edge_id, Capacity capacity) noexcept {
  return EdgeCapacity{std::move(edge_id), std::move(capacity)};
}

Result<ExecutionPlan> ExecutionPlan::create(ArtifactDigest digest, const ExecutionPlanSpec& specification) {
  if (specification.schema_version != kExecutionPlanSchemaVersion) {
    return validation("ExecutionPlan schema_version must be '" + std::string(kExecutionPlanSchemaVersion) + "'.");
  }
  if (!is_valid(specification.execution_profile)) {
    return validation("ExecutionPlan execution_profile is invalid.");
  }
  auto inputs = to_plan_input_digests(specification.inputs);
  if (!inputs.ok()) {
    return inputs.status();
  }
  if (specification.key_slot_tables.empty()) {
    return validation("key_slot_tables must contain at least one KeySlotTable plan.");
  }
  std::vector<KeySlotTablePlan> key_slot_tables;
  key_slot_tables.reserve(specification.key_slot_tables.size());
  std::vector<std::string> key_slot_node_ids;
  key_slot_node_ids.reserve(specification.key_slot_tables.size());
  Quantity total_key_slot_host_metadata = 0U;
  for (std::size_t index = 0; index < specification.key_slot_tables.size(); ++index) {
    auto table = to_key_slot_table_plan(specification.key_slot_tables[index], index);
    if (!table.ok()) {
      return table.status();
    }
    auto next_metadata = checked_add(total_key_slot_host_metadata, table.value().host_metadata_charged_bytes(),
                                     "key_slot_tables host metadata total");
    if (!next_metadata.ok()) {
      return next_metadata.status();
    }
    total_key_slot_host_metadata = next_metadata.value();
    key_slot_node_ids.emplace_back(table.value().node_id());
    key_slot_tables.push_back(std::move(table).value());
  }
  std::sort(key_slot_node_ids.begin(), key_slot_node_ids.end());
  if (std::adjacent_find(key_slot_node_ids.begin(), key_slot_node_ids.end()) != key_slot_node_ids.end()) {
    return validation("key_slot_tables must not contain duplicate node_id values.");
  }

  std::vector<ReorderPlan> reorder_plans;
  reorder_plans.reserve(specification.reorder_plans.size());
  std::vector<std::string> reorder_node_ids;
  reorder_node_ids.reserve(specification.reorder_plans.size());
  std::vector<std::string> reorder_order_domain_ids;
  reorder_order_domain_ids.reserve(specification.reorder_plans.size());
  Quantity total_reorder_host_metadata = 0U;
  Quantity total_reorder_descriptors = 0U;
  for (std::size_t index = 0; index < specification.reorder_plans.size(); ++index) {
    auto reorder = to_reorder_plan(specification.reorder_plans[index], index);
    if (!reorder.ok()) {
      return reorder.status();
    }
    if (reorder.value().order_domain_id() != reorder.value().node_id()) {
      return validation("M3 ReorderPlan order_domain_id must equal its owning node_id; cross-node order domains are "
                        "unsupported.");
    }
    if (!std::binary_search(key_slot_node_ids.begin(), key_slot_node_ids.end(), reorder.value().node_id())) {
      return validation("M3 ReorderPlan node_id must own a corresponding KeySlotTable plan.");
    }
    const auto table = std::find_if(key_slot_tables.begin(), key_slot_tables.end(), [&](const KeySlotTablePlan& value) {
      return value.node_id() == reorder.value().node_id();
    });
    if (table == key_slot_tables.end()) {
      return validation("internal error: M3 ReorderPlan KeySlotTable ownership lookup failed.");
    }
    if (table->key_domain_bound() != reorder.value().ordinal_domain_bound() ||
        table->dense_dimensions().size() != reorder.value().ordinal_dimensions().size()) {
      return validation("M3 ReorderPlan must have the same dense domain and dimensions as its owning KeySlotTable.");
    }
    for (std::size_t dimension = 0U; dimension < table->dense_dimensions().size(); ++dimension) {
      const auto& key_dimension = table->dense_dimensions()[dimension];
      const auto& ordinal_dimension = reorder.value().ordinal_dimensions()[dimension];
      if (key_dimension.field() != ordinal_dimension.field() ||
          key_dimension.minimum() != ordinal_dimension.minimum() ||
          key_dimension.cardinality() != ordinal_dimension.cardinality()) {
        return validation(
          "M3 ReorderPlan ordinal_dimensions must exactly equal its owning KeySlotTable dense_dimensions.");
      }
    }
    auto next_host_charge = checked_add(total_reorder_host_metadata, reorder.value().host_metadata_charged_bytes(),
                                        "reorder_plans aggregate host metadata charge");
    if (!next_host_charge.ok()) {
      return next_host_charge.status();
    }
    total_reorder_host_metadata = next_host_charge.value();
    auto next_descriptors = checked_add(total_reorder_descriptors, reorder.value().descriptor_charged_count(),
                                        "reorder_plans aggregate descriptor charge");
    if (!next_descriptors.ok()) {
      return next_descriptors.status();
    }
    total_reorder_descriptors = next_descriptors.value();
    reorder_node_ids.emplace_back(reorder.value().node_id());
    reorder_order_domain_ids.emplace_back(reorder.value().order_domain_id());
    reorder_plans.push_back(std::move(reorder).value());
  }
  std::sort(reorder_node_ids.begin(), reorder_node_ids.end());
  if (std::adjacent_find(reorder_node_ids.begin(), reorder_node_ids.end()) != reorder_node_ids.end()) {
    return validation("reorder_plans must not contain duplicate node_id values.");
  }
  std::sort(reorder_order_domain_ids.begin(), reorder_order_domain_ids.end());
  if (std::adjacent_find(reorder_order_domain_ids.begin(), reorder_order_domain_ids.end()) !=
      reorder_order_domain_ids.end()) {
    return validation("reorder_plans must not contain duplicate order_domain_id values.");
  }

  if (specification.buffer_pool_plans.empty() != specification.data_edge_plans.empty()) {
    return validation("M3.7 data-plane plans require both buffer_pool_plans and data_edge_plans; no legacy "
                      "fallback exists for a buffer_handle topology.");
  }
  std::vector<BufferPoolPlan> buffer_pool_plans;
  buffer_pool_plans.reserve(specification.buffer_pool_plans.size());
  std::vector<std::string> pool_ids;
  pool_ids.reserve(specification.buffer_pool_plans.size());
  std::vector<std::string> pool_producer_endpoints;
  pool_producer_endpoints.reserve(specification.buffer_pool_plans.size());
  Quantity total_pool_physical_charge = 0U;
  Quantity total_pool_descriptors = 0U;
  for (std::size_t index = 0U; index < specification.buffer_pool_plans.size(); ++index) {
    auto pool = to_buffer_pool_plan(specification.buffer_pool_plans[index], index);
    if (!pool.ok()) {
      return pool.status();
    }
    if (std::find(inputs.value().provider_contracts().begin(), inputs.value().provider_contracts().end(),
                  pool.value().producer_contract_digest()) == inputs.value().provider_contracts().end()) {
      return validation("BufferPoolPlan producer_contract_digest must occur in inputs.provider_contracts.");
    }
    auto next_physical = checked_add(total_pool_physical_charge, pool.value().physical_charge_bytes(),
                                     "buffer_pool_plans aggregate physical charge");
    if (!next_physical.ok()) {
      return next_physical.status();
    }
    total_pool_physical_charge = next_physical.value();
    auto next_descriptors = checked_add(total_pool_descriptors, pool.value().descriptor_charged_count(),
                                        "buffer_pool_plans aggregate descriptor charge");
    if (!next_descriptors.ok()) {
      return next_descriptors.status();
    }
    total_pool_descriptors = next_descriptors.value();
    pool_ids.emplace_back(pool.value().pool_id());
    pool_producer_endpoints.push_back(pool.value().producer_node_id() + "." + pool.value().producer_port_name());
    buffer_pool_plans.push_back(std::move(pool).value());
  }
  std::sort(pool_ids.begin(), pool_ids.end());
  if (std::adjacent_find(pool_ids.begin(), pool_ids.end()) != pool_ids.end()) {
    return validation("buffer_pool_plans must not contain duplicate pool_id values.");
  }
  std::sort(pool_producer_endpoints.begin(), pool_producer_endpoints.end());
  if (std::adjacent_find(pool_producer_endpoints.begin(), pool_producer_endpoints.end()) !=
      pool_producer_endpoints.end()) {
    return validation("buffer_pool_plans must not duplicate a producer node/port endpoint.");
  }

  std::vector<DataEdgePlan> data_edge_plans;
  data_edge_plans.reserve(specification.data_edge_plans.size());
  std::vector<std::string> data_edge_ids;
  data_edge_ids.reserve(specification.data_edge_plans.size());
  std::vector<std::string> data_edge_source_pools;
  data_edge_source_pools.reserve(specification.data_edge_plans.size());
  Quantity total_data_edge_host_metadata = 0U;
  Quantity total_data_edge_descriptors = 0U;
  for (std::size_t index = 0U; index < specification.data_edge_plans.size(); ++index) {
    auto edge = to_data_edge_plan(specification.data_edge_plans[index], index);
    if (!edge.ok()) {
      return edge.status();
    }
    const auto pool =
      std::find_if(buffer_pool_plans.begin(), buffer_pool_plans.end(), [&](const BufferPoolPlan& candidate) {
        return candidate.pool_id() == edge.value().source_pool_id();
      });
    if (pool == buffer_pool_plans.end()) {
      return validation("DataEdgePlan source_pool_id must name one BufferPoolPlan.");
    }
    if (pool->producer_node_id() != edge.value().producer_node_id() ||
        pool->producer_port_name() != edge.value().producer_port_name() ||
        !pool->type_descriptor().exactly_matches(edge.value().type_descriptor())) {
      return validation("DataEdgePlan source pool must exactly bind the same producer endpoint and TypeDescriptor.");
    }
    auto item_logical_bytes = checked_add(pool->payload_capacity_bytes(), pool->metadata_capacity_bytes(),
                                          "DataEdgePlan source pool logical bytes per item");
    if (!item_logical_bytes.ok()) {
      return item_logical_bytes.status();
    }
    auto expected_logical_bytes =
      checked_multiply(edge.value().max_items(), item_logical_bytes.value(), "DataEdgePlan full logical edge credit");
    if (!expected_logical_bytes.ok()) {
      return expected_logical_bytes.status();
    }
    if (edge.value().max_logical_bytes() != expected_logical_bytes.value()) {
      return validation("DataEdgePlan max_logical_bytes must exactly cover max_items full source-pool slots.");
    }
    const auto matching_reorder =
      std::find_if(reorder_plans.begin(), reorder_plans.end(), [&](const ReorderPlan& reorder) {
        return reorder.node_id() == edge.value().producer_node_id() &&
               reorder.ordered_output_port() == edge.value().producer_port_name();
      });
    if (matching_reorder == reorder_plans.end()) {
      return validation("M3.7 DataEdgePlan producer endpoint must be the selected ordered output of one "
                        "ReorderPlan; legacy buffer_handle topology is unsupported.");
    }
    // M3.7 reserves a DataEdge credit before obtaining a MutableBufferLease.
    // The compiler includes both downstream FIFO occupancy and reorder-held
    // handles in DataEdgePlan.max_items, then transfers that one credit at
    // ordered publish.  Hence every live payload needs exactly one pool slot,
    // not a second reorder-ahead term.
    if (pool->slot_count() != edge.value().max_items()) {
      return validation("BufferPoolPlan slot_count must equal the full DataEdgePlan max_items credit, including "
                        "reorder-held handles; no additional physical slot term is permitted.");
    }
    auto edge_host_charge =
      checked_add(edge.value().host_metadata_charged_bytes(), edge.value().firing_lease_staging_charged_bytes(),
                  "DataEdgePlan control plus firing-lease staging host charge");
    if (!edge_host_charge.ok()) {
      return edge_host_charge.status();
    }
    auto next_metadata = checked_add(total_data_edge_host_metadata, edge_host_charge.value(),
                                     "data_edge_plans aggregate host metadata/staging charge");
    if (!next_metadata.ok()) {
      return next_metadata.status();
    }
    total_data_edge_host_metadata = next_metadata.value();
    auto edge_descriptor_charge =
      checked_add(edge.value().descriptor_charged_count(), edge.value().firing_lease_staging_descriptor_count(),
                  "DataEdgePlan control plus firing-lease staging descriptor charge");
    if (!edge_descriptor_charge.ok()) {
      return edge_descriptor_charge.status();
    }
    auto next_descriptors = checked_add(total_data_edge_descriptors, edge_descriptor_charge.value(),
                                        "data_edge_plans aggregate descriptor/staging charge");
    if (!next_descriptors.ok()) {
      return next_descriptors.status();
    }
    total_data_edge_descriptors = next_descriptors.value();
    data_edge_ids.emplace_back(edge.value().edge_id());
    data_edge_source_pools.emplace_back(edge.value().source_pool_id());
    data_edge_plans.push_back(std::move(edge).value());
  }
  std::sort(data_edge_ids.begin(), data_edge_ids.end());
  if (std::adjacent_find(data_edge_ids.begin(), data_edge_ids.end()) != data_edge_ids.end()) {
    return validation("data_edge_plans must not contain duplicate edge_id values.");
  }
  std::sort(data_edge_source_pools.begin(), data_edge_source_pools.end());
  if (std::adjacent_find(data_edge_source_pools.begin(), data_edge_source_pools.end()) !=
      data_edge_source_pools.end()) {
    return validation(
      "M3.7 permits exactly one DataEdgePlan consumer for each BufferPoolPlan; fan-out is unsupported.");
  }
  if (data_edge_source_pools.size() != buffer_pool_plans.size()) {
    return validation("every M3.7 BufferPoolPlan must have exactly one DataEdgePlan consumer.");
  }

  Quantity legacy_reorder_payload_charge = 0U;
  if (data_edge_plans.empty()) {
    // Compatibility is intentionally all-or-nothing: an opaque legacy M3
    // plan keeps its historical physical ahead payload only when the artifact
    // contains no M3.7 pool/data-edge ownership model at all.
    for (const auto& reorder : reorder_plans) {
      auto next = checked_add(legacy_reorder_payload_charge, reorder.max_ahead_charged_bytes(),
                              "legacy ReorderPlan ahead payload charge");
      if (!next.ok()) {
        return next.status();
      }
      legacy_reorder_payload_charge = next.value();
    }
  } else {
    for (const auto& reorder : reorder_plans) {
      const auto plan_bound_handle_path =
        std::find_if(data_edge_plans.begin(), data_edge_plans.end(), [&](const DataEdgePlan& edge) {
          return edge.producer_node_id() == reorder.node_id() &&
                 edge.producer_port_name() == reorder.ordered_output_port();
        });
      if (plan_bound_handle_path == data_edge_plans.end()) {
        return validation("M3.7 plan-bound artifacts may not mix a legacy opaque ReorderPlan; every ReorderPlan "
                          "must bind its selected output to one DataEdgePlan.");
      }
    }
  }

  std::vector<EdgeCapacity> edge_capacities;
  edge_capacities.reserve(specification.edge_capacities.size());
  std::vector<std::string_view> edge_ids;
  edge_ids.reserve(specification.edge_capacities.size());
  for (std::size_t index = 0; index < specification.edge_capacities.size(); ++index) {
    auto edge = to_edge_capacity(specification.edge_capacities[index], index);
    if (!edge.ok()) {
      return edge.status();
    }
    edge_ids.emplace_back(edge.value().edge_id());
    edge_capacities.push_back(std::move(edge).value());
  }
  std::sort(edge_ids.begin(), edge_ids.end());
  if (std::adjacent_find(edge_ids.begin(), edge_ids.end()) != edge_ids.end()) {
    return validation("edge_capacities must not contain duplicate edge_id values.");
  }
  for (const auto edge_id : edge_ids) {
    if (std::binary_search(data_edge_ids.begin(), data_edge_ids.end(), edge_id)) {
      return validation("an explicit buffer_handle DataEdgePlan must not also use the legacy EdgeCapacity model.");
    }
  }
  auto resources = ResourceVector::create(specification.resource_vector, "resource_vector");
  if (!resources.ok()) {
    return resources.status();
  }
  auto required_host_normal = checked_add(total_key_slot_host_metadata, total_reorder_host_metadata,
                                          "ExecutionPlan KeySlot/Reorder host metadata charges");
  if (!required_host_normal.ok()) {
    return required_host_normal.status();
  }
  required_host_normal = checked_add(required_host_normal.value(), legacy_reorder_payload_charge,
                                     "ExecutionPlan legacy Reorder payload charges");
  if (!required_host_normal.ok()) {
    return required_host_normal.status();
  }
  required_host_normal =
    checked_add(required_host_normal.value(), total_pool_physical_charge, "ExecutionPlan BufferPool physical charges");
  if (!required_host_normal.ok()) {
    return required_host_normal.status();
  }
  required_host_normal =
    checked_add(required_host_normal.value(), total_data_edge_host_metadata, "ExecutionPlan DataEdge metadata charges");
  if (!required_host_normal.ok()) {
    return required_host_normal.status();
  }
  if (resources.value().host_normal_bytes() < required_host_normal.value()) {
    return validation("resource_vector.host_normal_bytes must cover KeySlot/Reorder metadata, legacy reorder payload, "
                      "each BufferPool physical slab, each DataEdge control slab, and each firing-lease ABI staging "
                      "slab exactly once.");
  }
  auto required_descriptors = checked_add(total_reorder_descriptors, total_pool_descriptors,
                                          "ExecutionPlan Reorder/BufferPool descriptor charges");
  if (!required_descriptors.ok()) {
    return required_descriptors.status();
  }
  required_descriptors =
    checked_add(required_descriptors.value(), total_data_edge_descriptors, "ExecutionPlan DataEdge descriptor charges");
  if (!required_descriptors.ok()) {
    return required_descriptors.status();
  }
  if (resources.value().descriptor_count() < required_descriptors.value()) {
    return validation("resource_vector.descriptor_count must cover every ReorderPlan, BufferPoolPlan, and "
                      "DataEdgePlan descriptor charge.");
  }
  auto terminal_occurrences = required_quantity(specification.terminal_occurrences, "terminal_occurrences");
  if (!terminal_occurrences.ok()) {
    return terminal_occurrences.status();
  }
  if (!has_nonempty_unique_strings(specification.proof_obligations)) {
    return validation("proof_obligations must contain unique non-empty identifiers.");
  }
  if (!reorder_plans.empty()) {
    for (const auto required :
         {kM3CompletedFrameSlotBindingProofObligation, kM3StrictDenseAllTuplesEoiRuntimeAssumption}) {
      if (std::find(specification.proof_obligations.begin(), specification.proof_obligations.end(), required) ==
          specification.proof_obligations.end()) {
        return validation("M3 ReorderPlans require obligation '" + std::string(required) + "'.");
      }
    }
  }
  if (!data_edge_plans.empty()) {
    for (const auto required :
         {kM37PlanBoundDataPlaneProofObligation, kM37SinglePhysicalPayloadChargeRuntimeAssumption}) {
      if (std::find(specification.proof_obligations.begin(), specification.proof_obligations.end(), required) ==
          specification.proof_obligations.end()) {
        return validation("M3.7 BufferPoolPlan/DataEdgePlan artifacts require obligation '" + std::string(required) +
                          "'.");
      }
    }
  }
  return ExecutionPlan{std::move(digest),
                       std::move(inputs).value(),
                       specification.execution_profile,
                       std::move(key_slot_tables),
                       std::move(reorder_plans),
                       std::move(buffer_pool_plans),
                       std::move(data_edge_plans),
                       std::move(edge_capacities),
                       std::move(resources).value(),
                       std::move(terminal_occurrences).value(),
                       specification.proof_obligations};
}

Result<VerificationRecord> VerificationRecord::create(ArtifactDigest digest,
                                                      const VerificationRecordSpec& specification) {
  if (specification.schema_version != kVerificationRecordSchemaVersion) {
    return validation("VerificationRecord schema_version must be '" + std::string(kVerificationRecordSchemaVersion) +
                      "'.");
  }
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
  if (specification.schema_version != kAdmissionRecordSchemaVersion) {
    return validation("AdmissionRecord schema_version must be '" + std::string(kAdmissionRecordSchemaVersion) + "'.");
  }
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
  return TypeDescriptor::create(
    {
      .type_id = std::string(kCompletedFrameSlotContextFrameTypeId),
      .revision = 1U,
      .abi_descriptor_digest = std::string(kCompletedFrameSlotContextAbiDescriptorDigest),
      .payload_schema_digest = std::string(kCompletedFrameSlotContextPayloadSchemaDigest),
      .payload_kind = PayloadKind::buffer_handle,
      .element_type = ElementType::complex_int16,
      .rank = 3U,
      .dimensions = {"channel", "ky", "kx"},
      .layout = LayoutKind::channel_major_contiguous,
      .strides = StrideKind::canonical,
      .explicit_byte_strides = {},
      .allowed_memory_domains = {TypeMemoryDomain::host_normal},
      .min_alignment_bytes = 64U,
      .mutability = PayloadMutability::immutable_after_publish,
      .metadata_schema_digest = std::string(kCompletedFrameSlotContextMetadataSchemaDigest),
    },
    "completed FrameSlotContext TypeDescriptor");
}

Result<OperatorContract> OperatorContract::create(const OperatorContractSpec& specification) {
  if (specification.schema_version != kOperatorContractSchemaVersion) {
    return validation("OperatorContract schema_version must be '" + std::string(kOperatorContractSchemaVersion) + "'.");
  }
  if (specification.operator_id.empty() || specification.operator_revision.empty()) {
    return validation("OperatorContract operator_id and operator_revision must not be empty.");
  }
  auto abi_major = required_quantity(specification.provider_abi_major, "provider_abi_major");
  if (!abi_major.ok()) {
    return abi_major.status();
  }
  if (specification.supported_profiles.empty() || !has_unique_values(specification.supported_profiles)) {
    return validation("supported_profiles must contain unique execution profiles.");
  }
  for (const auto profile : specification.supported_profiles) {
    if (!is_valid(profile)) {
      return validation("supported_profiles contains an invalid execution profile.");
    }
  }
  if (specification.ports.empty()) {
    return validation("ports must contain at least one declared port.");
  }
  std::vector<std::string> port_names;
  port_names.reserve(specification.ports.size());
  for (std::size_t index = 0; index < specification.ports.size(); ++index) {
    const auto& port = specification.ports[index];
    const std::string prefix = "ports[" + std::to_string(index) + "]";
    if (port.name.empty() || !is_valid(port.direction) || !is_valid(port.cardinality)) {
      return validation(prefix + " must declare a non-empty name and valid direction/cardinality.");
    }
    auto layouts = validate_unique_optional_strings(port.layout_capabilities, prefix + ".layout_capabilities");
    if (!layouts.ok()) {
      return layouts;
    }
    auto metadata = validate_unique_optional_strings(port.metadata_capabilities, prefix + ".metadata_capabilities");
    if (!metadata.ok()) {
      return metadata;
    }
    port_names.push_back(port.name);
  }
  if (!has_nonempty_unique_strings(port_names)) {
    return validation("ports must not contain duplicate port names.");
  }

  auto execution = validate_operator_execution(specification.execution);
  if (!execution.ok()) {
    return execution;
  }
  auto batch = validate_operator_batch(specification.batch);
  if (!batch.ok()) {
    return batch;
  }
  if (specification.batch.max_items > specification.execution.max_items_per_activation) {
    return validation("batch.max_items must not exceed execution.max_items_per_activation.");
  }
  auto rates = validate_operator_rates(specification.rates, specification.ports);
  if (!rates.ok()) {
    return rates;
  }
  auto static_batch_feasibility =
    validate_static_batch_activation_feasibility(specification.rates, specification.batch, specification.execution);
  if (!static_batch_feasibility.ok()) {
    return static_batch_feasibility;
  }
  auto resources = validate_operator_resources(specification.resources);
  if (!resources.ok()) {
    return resources;
  }
  auto calibration = validate_calibration(specification.calibration);
  if (!calibration.ok()) {
    return calibration;
  }
  auto join = validate_join(specification.join, specification.ports, specification.supported_profiles);
  if (!join.ok()) {
    return join;
  }
  auto reorder = validate_reorder(specification.reorder, specification.execution, specification.batch,
                                  specification.ports, specification.rates);
  if (!reorder.ok()) {
    return reorder;
  }
  auto terminal = validate_terminal_contract(specification.terminal);
  if (!terminal.ok()) {
    return terminal;
  }
  auto output_coverage =
    validate_output_resource_coverage(specification.resources, specification.rates, specification.terminal);
  if (!output_coverage.ok()) {
    return output_coverage;
  }

  return OperatorContract{specification.operator_id,    specification.operator_revision,
                          std::move(abi_major).value(), specification.supported_profiles,
                          specification.ports,          specification.execution,
                          specification.batch,          specification.rates,
                          specification.resources,      specification.calibration,
                          specification.join,           specification.reorder,
                          specification.terminal};
}

bool OperatorContract::supports(const ExecutionProfile profile) const noexcept {
  return std::find(supported_profiles_.begin(), supported_profiles_.end(), profile) != supported_profiles_.end();
}

} // namespace ksj::recon
