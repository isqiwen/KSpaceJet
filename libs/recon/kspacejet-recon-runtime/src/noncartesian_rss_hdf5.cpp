#include "kspacejet/recon/runtime/noncartesian_rss_hdf5.hpp"

#include "kspacejet/ismrmrd/dataset_reader.hpp"
#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/graph/execution_plan_compiler.hpp"
#include "kspacejet/recon/graph/operator_contract_json.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/node_planning_requirements.hpp"
#include "kspacejet/recon/runtime/provider_node_instance.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_plan_storage.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <ismrmrd/ismrmrd.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {
namespace {

using ksj::base::Result;
using ksj::base::Status;

constexpr std::uint32_t kMinimumImageDimension = 2U;
constexpr std::uint32_t kMaximumImageDimension = 512U;
constexpr std::uint32_t kMaximumChannels = 64U;
constexpr std::uint32_t kMaximumSamples = 65536U;
constexpr std::uint64_t kMaximumDirectAdjointWork = UINT64_C(1) << 28U;
constexpr std::uint64_t kTerminalEpoch = 1U;
constexpr std::size_t kContractFileMaximumBytes = 256U * 1024U;
constexpr std::uint64_t kMachineBudgetHeadroomBytes = 16U * 1024U * 1024U;
constexpr Quantity kFramePayloadAlignmentBytes = 64U;
constexpr char kNoncartesianProviderId[] = "org.kspacejet.noncartesian-recon";
constexpr char kNoncartesianOperatorId[] = "noncartesian_adjoint_reconstruct";
constexpr char kCoilCombineProviderId[] = "org.kspacejet.coil-combine";
constexpr char kCoilCombineOperatorId[] = "coil_combine_rss";
constexpr char kReconstructNodeId[] = "reconstruct";
constexpr char kCombineNodeId[] = "combine";
constexpr char kKspaceIngressId[] = "kspace";
constexpr char kTrajectoryIngressId[] = "trajectory";
constexpr char kImageEgressId[] = "images";

struct Shape final {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t channels{0U};
  std::uint32_t total_samples{0U};
  std::uint32_t max_samples_per_acquisition{0U};
  Quantity kspace_bytes{0U};
  Quantity trajectory_bytes{0U};
  Quantity coil_image_bytes{0U};
  Quantity image_bytes{0U};
  Quantity adjoint_scratch_bytes{0U};
  Quantity max_decoder_staging_bytes{0U};
  Quantity kspace_charged_bytes{0U};
  Quantity trajectory_charged_bytes{0U};
  Quantity coil_image_charged_bytes{0U};
  Quantity image_charged_bytes{0U};
};

struct Preflight final {
  ScanDescriptor scan_descriptor;
  Shape shape;
  std::uint32_t acquisitions_read{0U};
  ArtifactDigest source_xml_digest;
  ArtifactDigest normalized_scan_facts_digest;
};

struct PlanningArtifacts final {
  TargetEnvelope target_envelope;
  MachinePolicy machine_policy;
  graph::PlanArtifactDigests digests;
};

struct ResolvedGraphInputs final {
  graph::ResolvedPipeline pipeline;
  OperatorContract noncartesian_contract;
  OperatorContract coil_combine_contract;
};

struct DeclaredNoncartesianGeometry final {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
};

struct AcquisitionFacts final {
  std::uint32_t samples{0U};
  std::uint32_t channels{0U};
  Quantity decoder_staging_bytes{0U};
};

[[nodiscard]] Result<Quantity> checked_product(const Quantity left, const Quantity right, const std::string_view name) {
  if (left != 0U && right > std::numeric_limits<Quantity>::max() / left) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 " + std::string(name) + " overflows a quantity");
  }
  return left * right;
}

[[nodiscard]] Result<Quantity> checked_sum(const Quantity left, const Quantity right, const std::string_view name) {
  if (right > std::numeric_limits<Quantity>::max() - left) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 " + std::string(name) + " overflows a quantity");
  }
  return left + right;
}

[[nodiscard]] Result<Quantity> aligned_payload_capacity(const Quantity logical_bytes, const Quantity alignment,
                                                        const std::string_view name) {
  if (logical_bytes == 0U || alignment == 0U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 " + std::string(name) + " has zero payload/alignment");
  }
  const auto remainder = logical_bytes % alignment;
  if (remainder == 0U)
    return logical_bytes;
  return checked_sum(logical_bytes, alignment - remainder, name);
}

[[nodiscard]] Result<Shape> make_shape(const std::uint32_t rows, const std::uint32_t cols, const std::uint32_t channels,
                                       const std::uint32_t total_samples,
                                       const std::uint32_t max_samples_per_acquisition,
                                       const Quantity max_decoder_staging_bytes) {
  if (rows < kMinimumImageDimension || rows > kMaximumImageDimension || cols < kMinimumImageDimension ||
      cols > kMaximumImageDimension || channels == 0U || channels > kMaximumChannels || total_samples == 0U ||
      total_samples > kMaximumSamples || max_samples_per_acquisition == 0U ||
      max_samples_per_acquisition > total_samples || max_decoder_staging_bytes == 0U) {
    return Status::InvalidArgument(
      "Non-Cartesian RSS HDF5 supports 2-D images in [2,512], channels in [1,64], and total samples in [1,65536]");
  }
  auto pixels = checked_product(rows, cols, "image pixel count");
  if (!pixels.ok())
    return pixels.status();
  auto coil_pixels = checked_product(pixels.value(), channels, "coil-image pixel count");
  if (!coil_pixels.ok())
    return coil_pixels.status();
  auto direct_work = checked_product(coil_pixels.value(), total_samples, "direct-adjoint work");
  if (!direct_work.ok())
    return direct_work.status();
  if (direct_work.value() > kMaximumDirectAdjointWork) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 exceeds the bounded direct-adjoint Provider work limit");
  }
  auto kspace_elements = checked_product(channels, total_samples, "k-space element count");
  if (!kspace_elements.ok())
    return kspace_elements.status();
  auto kspace = checked_product(kspace_elements.value(), sizeof(std::complex<float>), "k-space bytes");
  if (!kspace.ok())
    return kspace.status();
  auto trajectory_elements = checked_product(total_samples, 2U, "trajectory element count");
  if (!trajectory_elements.ok())
    return trajectory_elements.status();
  auto trajectory = checked_product(trajectory_elements.value(), sizeof(float), "trajectory bytes");
  if (!trajectory.ok())
    return trajectory.status();
  auto coil_images = checked_product(coil_pixels.value(), sizeof(std::complex<float>), "coil-image bytes");
  if (!coil_images.ok())
    return coil_images.status();
  auto image = checked_product(pixels.value(), sizeof(float), "RSS image bytes");
  if (!image.ok())
    return image.status();
  auto scratch = checked_product(pixels.value(), sizeof(std::complex<float>), "adjoint scratch bytes");
  if (!scratch.ok())
    return scratch.status();
  auto kspace_charge = aligned_payload_capacity(kspace.value(), kFramePayloadAlignmentBytes, "k-space pool capacity");
  if (!kspace_charge.ok())
    return kspace_charge.status();
  auto trajectory_charge =
    aligned_payload_capacity(trajectory.value(), kFramePayloadAlignmentBytes, "trajectory pool capacity");
  if (!trajectory_charge.ok())
    return trajectory_charge.status();
  auto coil_charge =
    aligned_payload_capacity(coil_images.value(), kFramePayloadAlignmentBytes, "coil-image pool capacity");
  if (!coil_charge.ok())
    return coil_charge.status();
  auto image_charge = aligned_payload_capacity(image.value(), kFramePayloadAlignmentBytes, "image pool capacity");
  if (!image_charge.ok())
    return image_charge.status();
  return Shape{.rows = rows,
               .cols = cols,
               .channels = channels,
               .total_samples = total_samples,
               .max_samples_per_acquisition = max_samples_per_acquisition,
               .kspace_bytes = kspace.value(),
               .trajectory_bytes = trajectory.value(),
               .coil_image_bytes = coil_images.value(),
               .image_bytes = image.value(),
               .adjoint_scratch_bytes = scratch.value(),
               .max_decoder_staging_bytes = max_decoder_staging_bytes,
               .kspace_charged_bytes = kspace_charge.value(),
               .trajectory_charged_bytes = trajectory_charge.value(),
               .coil_image_charged_bytes = coil_charge.value(),
               .image_charged_bytes = image_charge.value()};
}

[[nodiscard]] bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
  std::error_code error;
  const auto absolute_left = std::filesystem::absolute(left, error);
  if (error)
    return left.lexically_normal() == right.lexically_normal();
  error.clear();
  const auto absolute_right = std::filesystem::absolute(right, error);
  if (error)
    return left.lexically_normal() == right.lexically_normal();
  return absolute_left.lexically_normal() == absolute_right.lexically_normal();
}

[[nodiscard]] Status validate_config(const NoncartesianRssHdf5ReconstructionConfig& config) {
  if (config.input_file.empty() || config.output_image_file.empty() || config.noncartesian_provider_module.empty() ||
      config.coil_combine_provider_module.empty() || config.noncartesian_operator_contract.empty() ||
      config.coil_combine_operator_contract.empty() || config.dataset_group.empty()) {
    return Status::InvalidArgument(
      "Non-Cartesian RSS HDF5 requires input/output, both explicit Provider modules/contracts, and a dataset group");
  }
  if ((!config.output_metadata_file.empty() && same_path(config.output_image_file, config.output_metadata_file)) ||
      same_path(config.input_file, config.output_image_file) ||
      (!config.output_metadata_file.empty() && same_path(config.input_file, config.output_metadata_file))) {
    return Status::InvalidArgument(
      "Non-Cartesian RSS HDF5 input, image output, and metadata output must be distinct files");
  }
  return Status::Ok();
}

[[nodiscard]] Status hdf5_io_error(const std::string_view message) {
  return Status::IoError("Non-Cartesian RSS HDF5 input failed: " + std::string(message));
}

[[nodiscard]] std::string json_string(const std::string_view value) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(value.size() + 2U);
  result.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        result += "\\\\";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (character < 0x20U) {
          result += "\\u00";
          result.push_back(kHex[(character >> 4U) & 0x0FU]);
          result.push_back(kHex[character & 0x0FU]);
        } else {
          result.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  result.push_back('"');
  return result;
}

[[nodiscard]] Result<ArtifactDigest> canonical_digest(const std::string_view domain, const std::string_view document,
                                                      const std::string_view field_name) {
  auto canonical = graph::canonicalize_json(document);
  if (!canonical.ok())
    return canonical.status();
  return graph::domain_separated_sha256_digest(domain, canonical.value(), field_name);
}

[[nodiscard]] Result<std::string> read_contract_file(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return Status::InvalidArgument("Non-Cartesian RSS HDF5 OperatorContract path is not a regular file: " +
                                   path.string());
  }
  const auto bytes = std::filesystem::file_size(path, error);
  if (error)
    return Status::IoError("unable to determine OperatorContract file size: " + path.string());
  if (bytes == 0U || bytes > kContractFileMaximumBytes || bytes > std::numeric_limits<std::size_t>::max()) {
    return Status::InvalidArgument("Non-Cartesian RSS HDF5 OperatorContract file is empty or exceeds 256 KiB: " +
                                   path.string());
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open())
    return Status::IoError("unable to open OperatorContract file: " + path.string());
  std::string result(static_cast<std::size_t>(bytes), '\0');
  stream.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(result.size())) {
    return Status::IoError("unable to read complete OperatorContract file: " + path.string());
  }
  return result;
}

[[nodiscard]] Status validate_noncartesian_contract(const OperatorContract& contract) {
  if (contract.operator_id() != kNoncartesianOperatorId || contract.ports().size() != 3U) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 OperatorContract does not describe noncartesian_adjoint_reconstruct");
  }
  const auto& kspace = contract.ports()[0U];
  const auto& trajectory = contract.ports()[1U];
  const auto& coil_images = contract.ports()[2U];
  if (kspace.direction != PortDirection::input || kspace.name != "kspace" ||
      kspace.type_descriptor.type_ref().value() != types::kNoncartesianKspaceFrameTypeRef ||
      trajectory.direction != PortDirection::input || trajectory.name != "trajectory" ||
      trajectory.type_descriptor.type_ref().value() != types::kTrajectoryFrameTypeRef ||
      coil_images.direction != PortDirection::output || coil_images.name != "coil_images" ||
      coil_images.type_descriptor.type_ref().value() != types::kCoilImageFrameTypeRef) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 OperatorContract ports do not match the required typed direct-adjoint route");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_coil_combine_contract(const OperatorContract& contract) {
  if (contract.operator_id() != kCoilCombineOperatorId || contract.ports().size() != 2U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 OperatorContract does not describe coil_combine_rss");
  }
  const auto& coil_images = contract.ports()[0U];
  const auto& image = contract.ports()[1U];
  if (coil_images.direction != PortDirection::input || coil_images.name != "coil_images" ||
      coil_images.type_descriptor.type_ref().value() != types::kCoilImageFrameTypeRef ||
      image.direction != PortDirection::output || image.name != "image" ||
      image.type_descriptor.type_ref().value() != types::kImageFrameTypeRef) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 OperatorContract ports do not match the required RSS combine route");
  }
  return Status::Ok();
}

[[nodiscard]] Result<ArtifactDigest> artifact_digest(const ksj::provider::loader::Digest256& digest,
                                                     const std::string_view field_name) {
  constexpr char kHex[] = "0123456789abcdef";
  std::string encoded{"sha256:"};
  encoded.reserve(71U);
  for (const auto value : digest) {
    encoded.push_back(kHex[(value >> 4U) & 0x0FU]);
    encoded.push_back(kHex[value & 0x0FU]);
  }
  return ArtifactDigest::parse(encoded, field_name);
}

[[nodiscard]] Result<graph::ResolvedProvider> resolve_provider(const std::filesystem::path& module_path,
                                                               const std::string_view alias,
                                                               const std::string_view expected_provider_id,
                                                               const std::string_view expected_operator_id) {
  auto module = ksj::provider::loader::ProviderModule::load(
    module_path, {.host_build_id = "KSpaceJet noncartesian-rss HDF5 resolver"});
  if (!module.ok())
    return module.status();
  const auto* descriptor = module.value().descriptor();
  if (descriptor == nullptr || descriptor->provider_id != expected_provider_id) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 module does not expose required Provider '" +
                                   std::string(expected_provider_id) + "'");
  }
  const auto found = std::find_if(descriptor->operators.begin(), descriptor->operators.end(),
                                  [expected_operator_id](const auto& candidate) {
                                    return candidate.operator_id == expected_operator_id;
                                  });
  if (found == descriptor->operators.end()) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 module omits required Operator '" +
                                   std::string(expected_operator_id) + "'");
  }
  auto bundle = artifact_digest(descriptor->bundle_digest, "Non-Cartesian RSS HDF5 Provider bundle digest");
  if (!bundle.ok())
    return bundle.status();
  return graph::ResolvedProvider{.alias = std::string(alias),
                                 .provider_id = std::string(expected_provider_id),
                                 .bundle_digest = std::move(bundle).value(),
                                 .operators = {{.id = std::string(expected_operator_id)}}};
}

[[nodiscard]] Result<ResolvedGraphInputs> make_graph_inputs(const NoncartesianRssHdf5ReconstructionConfig& config,
                                                            const Shape& shape) {
  auto noncartesian_json = read_contract_file(config.noncartesian_operator_contract);
  if (!noncartesian_json.ok())
    return noncartesian_json.status();
  auto noncartesian_contract = graph::parse_operator_contract_json(noncartesian_json.value());
  if (!noncartesian_contract.ok())
    return noncartesian_contract.status();
  const auto noncartesian_status = validate_noncartesian_contract(noncartesian_contract.value());
  if (!noncartesian_status.ok())
    return noncartesian_status;

  auto coil_json = read_contract_file(config.coil_combine_operator_contract);
  if (!coil_json.ok())
    return coil_json.status();
  auto coil_contract = graph::parse_operator_contract_json(coil_json.value());
  if (!coil_contract.ok())
    return coil_contract.status();
  const auto coil_status = validate_coil_combine_contract(coil_contract.value());
  if (!coil_status.ok())
    return coil_status;

  auto noncartesian_provider = resolve_provider(config.noncartesian_provider_module, "noncartesian",
                                                kNoncartesianProviderId, kNoncartesianOperatorId);
  if (!noncartesian_provider.ok())
    return noncartesian_provider.status();
  auto coil_provider = resolve_provider(config.coil_combine_provider_module, "coilcombine", kCoilCombineProviderId,
                                        kCoilCombineOperatorId);
  if (!coil_provider.ok())
    return coil_provider.status();

  const auto noncartesian_dimensions =
    "{\"channels\":" + std::to_string(shape.channels) + ",\"image_cols\":" + std::to_string(shape.cols) +
    ",\"image_rows\":" + std::to_string(shape.rows) + ",\"sample_count\":" + std::to_string(shape.total_samples) + "}";
  const auto coil_dimensions = "{\"channels\":" + std::to_string(shape.channels) +
                               ",\"cols\":" + std::to_string(shape.cols) + ",\"rows\":" + std::to_string(shape.rows) +
                               "}";
  const auto pipeline_document =
    "{"
    "\"kind\":\"PipelineDefinition\","
    "\"pipeline\":{\"id\":\"org.kspacejet.noncartesian-rss\",\"display_name\":\"Non-Cartesian RSS reconstruction\"},"
    "\"allowed_profiles\":[\"offline-reference\"],"
    "\"parameters\":{},"
    "\"provider_requirements\":["
    "{\"alias\":\"noncartesian\",\"provider_id\":\"org.kspacejet.noncartesian-recon\"},"
    "{\"alias\":\"coilcombine\",\"provider_id\":\"org.kspacejet.coil-combine\"}"
    "],"
    "\"nodes\":["
    "{\"id\":\"reconstruct\",\"operator\":{\"provider\":\"noncartesian\",\"id\":\"noncartesian_adjoint_reconstruct\"},"
    "\"config\":" +
    noncartesian_dimensions +
    "},"
    "{\"id\":\"combine\",\"operator\":{\"provider\":\"coilcombine\",\"id\":\"coil_combine_rss\"},\"config\":" +
    coil_dimensions +
    "}"
    "],"
    "\"edges\":[{\"id\":\"coil_images\",\"from\":{\"node\":\"reconstruct\",\"port\":\"coil_images\"},"
    "\"to\":{\"node\":\"combine\",\"port\":\"coil_images\"}}],"
    "\"bindings\":{"
    "\"ingress\":["
    "{\"id\":\"kspace\",\"type\":\"ksj.noncartesian-kspace-frame\",\"to\":{\"node\":\"reconstruct\",\"port\":"
    "\"kspace\"}},"
    "{\"id\":\"trajectory\",\"type\":\"ksj.trajectory-frame\",\"to\":{\"node\":\"reconstruct\",\"port\":\"trajectory\"}"
    "}"
    "],"
    "\"egress\":[{\"id\":\"images\",\"type\":\"ksj.image-frame\",\"from\":{\"node\":\"combine\",\"port\":\"image\"}}],"
    "\"calibration\":[],\"merge\":[]},"
    "\"annotations\":{}"
    "}";
  auto definition = graph::PipelineDefinition::parse_json(pipeline_document);
  if (!definition.ok())
    return definition.status();
  auto pipeline = graph::ResolvedPipeline::resolve(
    std::move(definition).value(), {std::move(noncartesian_provider).value(), std::move(coil_provider).value()});
  if (!pipeline.ok())
    return pipeline.status();
  return ResolvedGraphInputs{.pipeline = std::move(pipeline).value(),
                             .noncartesian_contract = std::move(noncartesian_contract).value(),
                             .coil_combine_contract = std::move(coil_contract).value()};
}

[[nodiscard]] Result<NodePlanningRequirements> make_noncartesian_requirements(const OperatorContract& contract,
                                                                              const Shape& shape) {
  auto input_bytes =
    checked_sum(shape.kspace_charged_bytes, shape.trajectory_charged_bytes, "direct-adjoint batch charged bytes");
  if (!input_bytes.ok())
    return input_bytes.status();
  return NodePlanningRequirements::create(
    {.execution = {.input_granularity = InputGranularity::frame,
                   .partition_key = {},
                   .max_active_keys = 1U,
                   .max_in_flight = 1U,
                   .max_items_per_activation = 2U},
     .batch = {.min_items = 2U, .preferred_items = 2U, .max_items = 2U, .max_charged_bytes = input_bytes.value()},
     .rates =
       {.kind = RateKind::sdf,
        .static_phases =
          {{.inputs = {{.port_name = "kspace", .items = 1U, .charged_bytes = shape.kspace_charged_bytes},
                       {.port_name = "trajectory", .items = 1U, .charged_bytes = shape.trajectory_charged_bytes}},
            .outputs = {{.port_name = "coil_images", .items = 1U, .charged_bytes = shape.coil_image_charged_bytes}}}}},
     .resources = {.scratch_charged_bytes_per_firing = shape.adjoint_scratch_bytes,
                   .per_key_state_charged_bytes = 0U,
                   .per_scan_workspace_charged_bytes = 0U,
                   .retention_charged_bytes = 0U,
                   .output_items = 1U,
                   .output_charged_bytes = shape.coil_image_charged_bytes,
                   .cpu_permits = 1U,
                   .memory_domain = MemoryDomain::host},
     .terminal = {.normal_max_output_items = 0U,
                  .normal_max_output_charged_bytes = 0U,
                  .normal_max_async_tokens = 0U,
                  .cancel_max_async_tokens = 0U}},
    contract);
}

[[nodiscard]] Result<NodePlanningRequirements> make_coil_combine_requirements(const OperatorContract& contract,
                                                                              const Shape& shape) {
  return NodePlanningRequirements::create(
    {.execution = {.input_granularity = InputGranularity::frame,
                   .partition_key = {},
                   .max_active_keys = 1U,
                   .max_in_flight = 1U,
                   .max_items_per_activation = 1U},
     .batch =
       {.min_items = 1U, .preferred_items = 1U, .max_items = 1U, .max_charged_bytes = shape.coil_image_charged_bytes},
     .rates =
       {.kind = RateKind::sdf,
        .static_phases =
          {{.inputs = {{.port_name = "coil_images", .items = 1U, .charged_bytes = shape.coil_image_charged_bytes}},
            .outputs = {{.port_name = "image", .items = 1U, .charged_bytes = shape.image_charged_bytes}}}}},
     .resources = {.scratch_charged_bytes_per_firing = 0U,
                   .per_key_state_charged_bytes = 0U,
                   .per_scan_workspace_charged_bytes = 0U,
                   .retention_charged_bytes = 0U,
                   .output_items = 1U,
                   .output_charged_bytes = shape.image_charged_bytes,
                   .cpu_permits = 1U,
                   .memory_domain = MemoryDomain::host},
     .terminal = {.normal_max_output_items = 0U,
                  .normal_max_output_charged_bytes = 0U,
                  .normal_max_async_tokens = 0U,
                  .cancel_max_async_tokens = 0U}},
    contract);
}

[[nodiscard]] Result<PlanningArtifacts> make_planning_artifacts(const Preflight& preflight) {
  const auto& shape = preflight.shape;
  auto budget =
    checked_sum(shape.kspace_charged_bytes, shape.trajectory_charged_bytes, "machine budget ingress frames");
  if (!budget.ok())
    return budget.status();
  budget = checked_sum(budget.value(), shape.coil_image_charged_bytes, "machine budget coil images");
  if (!budget.ok())
    return budget.status();
  budget = checked_sum(budget.value(), shape.image_charged_bytes, "machine budget image");
  if (!budget.ok())
    return budget.status();
  budget = checked_sum(budget.value(), shape.adjoint_scratch_bytes, "machine budget adjoint scratch");
  if (!budget.ok())
    return budget.status();
  auto doubled = checked_product(budget.value(), 2U, "machine budget double-buffer allowance");
  if (!doubled.ok())
    return doubled.status();
  auto host_budget = checked_sum(doubled.value(), kMachineBudgetHeadroomBytes, "machine budget headroom");
  if (!host_budget.ok())
    return host_budget.status();

  const auto max_frame_bytes = std::max(shape.kspace_charged_bytes, shape.trajectory_charged_bytes);
  auto target =
    TargetEnvelope::create({.max_xml_bytes = preflight.scan_descriptor.source_xml_bytes(),
                            .max_frame_charged_bytes = max_frame_bytes,
                            .max_image_charged_bytes = shape.image_charged_bytes,
                            .max_decoder_staging_bytes = shape.max_decoder_staging_bytes,
                            .max_samples_per_acquisition = shape.max_samples_per_acquisition,
                            .max_trajectory_dimensions = 2U,
                            .max_active_channels = shape.channels,
                            .max_channel_groups = 1U,
                            .max_dynamic_keys_per_scan = 1U,
                            .max_active_scans = 1U,
                            .calibration_horizon_items = 0U,
                            .calibration_horizon_charged_bytes = 0U,
                            .arrival_envelope = {.max_acquisitions_per_second = preflight.acquisitions_read,
                                                 .max_burst_acquisitions = preflight.acquisitions_read},
                            .sink_service_assumption = {.minimum_drain_items_per_second = 1U,
                                                        .max_pause_us = 0U,
                                                        .slow_sink_policy = SlowSinkPolicy::fail,
                                                        .transport_staging_bytes = shape.image_charged_bytes}});
  if (!target.ok())
    return target.status();
  auto policy = MachinePolicy::create({.resource_capacity = {.domains = {.host_normal_bytes = host_budget.value(),
                                                                         .host_pinned_bytes = 0U,
                                                                         .host_hugepage_bytes = 0U,
                                                                         .shared_host_bytes = 0U,
                                                                         .spool_bytes = 0U,
                                                                         .transport_bytes = 0U,
                                                                         .descriptor_count = 1024U,
                                                                         .async_token_count = 0U,
                                                                         .cpu_leaf_permits = 2U,
                                                                         .backend_gang_permits = 0U,
                                                                         .provider_private_permits = 0U,
                                                                         .io_slots = 0U},
                                                             .host_total_cap_bytes = host_budget.value()},
                                       .numa_domain_count = 1U,
                                       .allowed_memory_domains = {MemoryDomain::host},
                                       .allowed_profiles = {ExecutionProfile::offline_reference},
                                       .scheduler_policy = SchedulerPolicy::fifo});
  if (!policy.ok())
    return policy.status();

  const auto target_document =
    "{\"arrival\":{\"max_acquisitions_per_second\":" + std::to_string(preflight.acquisitions_read) +
    ",\"max_burst_acquisitions\":" + std::to_string(preflight.acquisitions_read) +
    "},\"calibration_horizon_charged_bytes\":0,\"calibration_horizon_items\":0,\"max_active_channels\":" +
    std::to_string(shape.channels) +
    ",\"max_active_scans\":1,\"max_channel_groups\":1,"
    "\"max_decoder_staging_bytes\":" +
    std::to_string(shape.max_decoder_staging_bytes) +
    ",\"max_dynamic_keys_per_scan\":1,"
    "\"max_frame_charged_bytes\":" +
    std::to_string(max_frame_bytes) + ",\"max_image_charged_bytes\":" + std::to_string(shape.image_charged_bytes) +
    ",\"max_samples_per_acquisition\":" + std::to_string(shape.max_samples_per_acquisition) +
    ",\"max_trajectory_dimensions\":2,\"max_xml_bytes\":" +
    std::to_string(preflight.scan_descriptor.source_xml_bytes()) +
    ",\"sink\":{\"max_pause_us\":0,\"minimum_drain_items_per_second\":1,\"slow_sink_policy\":\"fail\","
    "\"transport_staging_bytes\":" +
    std::to_string(shape.image_charged_bytes) + "}}";
  auto target_digest = canonical_digest("kspacejet:artifact:noncartesian-rss-hdf5-target-envelope", target_document,
                                        "Non-Cartesian RSS HDF5 target envelope input");
  if (!target_digest.ok())
    return target_digest.status();
  const auto machine_document =
    "{\"allowed_memory_domains\":[\"host\"],\"allowed_profiles\":[\"offline-reference\"],\"host_total_cap_bytes\":" +
    std::to_string(host_budget.value()) +
    ",\"numa_domain_count\":1,\"resources\":{\"async_token_count\":0,\"backend_gang_permits\":0,"
    "\"cpu_leaf_permits\":2,\"descriptor_count\":1024,\"host_hugepage_bytes\":0,\"host_normal_bytes\":" +
    std::to_string(host_budget.value()) +
    ",\"host_pinned_bytes\":0,\"io_slots\":0,\"provider_private_permits\":0,\"shared_host_bytes\":0,"
    "\"spool_bytes\":0,\"transport_bytes\":0},\"scheduler_policy\":\"fifo\"}";
  auto machine_digest = canonical_digest("kspacejet:artifact:noncartesian-rss-hdf5-machine-policy", machine_document,
                                         "Non-Cartesian RSS HDF5 machine policy input");
  if (!machine_digest.ok())
    return machine_digest.status();
  return PlanningArtifacts{.target_envelope = std::move(target).value(),
                           .machine_policy = std::move(policy).value(),
                           .digests = {.scan_descriptor = preflight.normalized_scan_facts_digest,
                                       .target_envelope = std::move(target_digest).value(),
                                       .machine_policy = std::move(machine_digest).value()}};
}

[[nodiscard]] bool is_supported_noncartesian_trajectory(const TrajectoryType trajectory) noexcept {
  return trajectory == TrajectoryType::radial || trajectory == TrajectoryType::golden_angle ||
         trajectory == TrajectoryType::spiral || trajectory == TrajectoryType::other;
}

[[nodiscard]] Result<DeclaredNoncartesianGeometry>
derive_declared_noncartesian_geometry(const ScanDescriptor& descriptor) {
  if (descriptor.encodings().size() != 1U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 requires exactly one ISMRMRD encoding");
  }
  const auto& encoding = descriptor.encodings().front();
  const auto& encoded = encoding.encoded_matrix();
  const auto& reconstructed = encoding.recon_matrix();
  if (!is_supported_noncartesian_trajectory(encoding.trajectory()) || encoded.z != 1U || reconstructed.z != 1U ||
      reconstructed.x < kMinimumImageDimension || reconstructed.x > kMaximumImageDimension ||
      reconstructed.y < kMinimumImageDimension || reconstructed.y > kMaximumImageDimension) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 XML must declare one 2-D radial, golden-angle, spiral, or other reconstruction matrix");
  }
  return DeclaredNoncartesianGeometry{.rows = static_cast<std::uint32_t>(reconstructed.y),
                                      .cols = static_cast<std::uint32_t>(reconstructed.x)};
}

[[nodiscard]] Status validate_declared_channels(const ScanDescriptor& descriptor, const std::uint32_t channels) {
  if (descriptor.declared_receiver_channels().has_value() &&
      descriptor.declared_receiver_channels().value() < channels) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 receiverChannels is smaller than acquisition active_channels");
  }
  return Status::Ok();
}

[[nodiscard]] bool product_matches_size(const std::uint64_t left, const std::uint64_t right,
                                        const std::size_t observed_size) noexcept {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  const auto expected = left * right;
  if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
    if (expected > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
      return false;
    }
  }
  return static_cast<std::size_t>(expected) == observed_size;
}

[[nodiscard]] Result<AcquisitionFacts> validate_acquisition(const ksj::ismrmrd::AcquisitionView& acquisition,
                                                            std::optional<std::uint32_t>& expected_channels) {
  const auto& header = acquisition.header;
  if (header.flags != 0U || header.discard_pre != 0U || header.discard_post != 0U) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 accepts only unflagged normal-imaging acquisitions without discarded samples");
  }
  if (header.encoding_space_ref != 0U || header.index.average != 0U || header.index.slice != 0U ||
      header.index.contrast != 0U || header.index.phase != 0U || header.index.repetition != 0U ||
      header.index.set != 0U || header.index.segment != 0U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 accepts exactly one 2-D semantic frame");
  }
  if (header.number_of_samples == 0U || header.active_channels == 0U || header.active_channels > kMaximumChannels ||
      header.available_channels < header.active_channels || header.trajectory_dimensions != 2U) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 acquisition must have samples, [1,64] active channels, and an explicit 2-D trajectory");
  }
  if (!product_matches_size(header.number_of_samples, header.active_channels, acquisition.samples.size()) ||
      !product_matches_size(header.number_of_samples, 2U, acquisition.trajectory.size())) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 acquisition payloads do not match their declared samples, "
                                   "channels, and trajectory dimensions");
  }
  if (!expected_channels.has_value()) {
    expected_channels = header.active_channels;
  } else if (*expected_channels != header.active_channels) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 acquisition active_channels changes within one frame");
  }
  for (const auto value : acquisition.samples) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
      return Status::ValidationError("Non-Cartesian RSS HDF5 rejects non-finite raw complex samples");
    }
  }
  for (const auto value : acquisition.trajectory) {
    if (!std::isfinite(value)) {
      return Status::ValidationError("Non-Cartesian RSS HDF5 rejects non-finite trajectory coordinates");
    }
  }
  auto data_bytes = checked_product(static_cast<Quantity>(header.number_of_samples), header.active_channels,
                                    "acquisition sample count");
  if (!data_bytes.ok())
    return data_bytes.status();
  data_bytes = checked_product(data_bytes.value(), sizeof(std::complex<float>), "acquisition k-space bytes");
  if (!data_bytes.ok())
    return data_bytes.status();
  auto trajectory_bytes =
    checked_product(static_cast<Quantity>(header.number_of_samples), 2U, "acquisition trajectory count");
  if (!trajectory_bytes.ok())
    return trajectory_bytes.status();
  trajectory_bytes = checked_product(trajectory_bytes.value(), sizeof(float), "acquisition trajectory bytes");
  if (!trajectory_bytes.ok())
    return trajectory_bytes.status();
  auto staging = checked_sum(data_bytes.value(), trajectory_bytes.value(), "acquisition decoder staging bytes");
  if (!staging.ok())
    return staging.status();
  return AcquisitionFacts{
    .samples = header.number_of_samples, .channels = header.active_channels, .decoder_staging_bytes = staging.value()};
}

[[nodiscard]] Result<Preflight> preflight_input(const NoncartesianRssHdf5ReconstructionConfig& config) {
  ksj::ismrmrd::DatasetReader reader;
  std::string reader_error;
  if (!reader.open(config.input_file, config.dataset_group, reader_error))
    return hdf5_io_error(reader_error);
  if (reader.metadata().xml_header.empty()) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 requires an ISMRMRD XML header");
  }
  auto scan = ScanDescriptor::parse_ismrmrd_xml(reader.metadata().xml_header);
  if (!scan.ok())
    return scan.status();
  auto geometry = derive_declared_noncartesian_geometry(scan.value());
  if (!geometry.ok())
    return geometry.status();

  std::optional<std::uint32_t> channels;
  Quantity total_samples{0U};
  std::uint32_t max_samples_per_acquisition{0U};
  Quantity max_decoder_staging_bytes{0U};
  std::uint32_t acquisitions{0U};
  Status callback_status = Status::Ok();
  const auto iteration = reader.for_each_acquisition(
    [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
      auto facts = validate_acquisition(acquisition, channels);
      if (!facts.ok()) {
        callback_status = facts.status();
        return false;
      }
      auto next_samples = checked_sum(total_samples, facts.value().samples, "total trajectory sample count");
      if (!next_samples.ok() || next_samples.value() > kMaximumSamples) {
        callback_status = next_samples.ok()
                            ? Status::ValidationError("Non-Cartesian RSS HDF5 total samples exceed 65536")
                            : next_samples.status();
        return false;
      }
      if (acquisitions == std::numeric_limits<std::uint32_t>::max()) {
        callback_status = Status::ValidationError("Non-Cartesian RSS HDF5 acquisition count overflows uint32");
        return false;
      }
      total_samples = next_samples.value();
      max_samples_per_acquisition = std::max(max_samples_per_acquisition, facts.value().samples);
      max_decoder_staging_bytes = std::max(max_decoder_staging_bytes, facts.value().decoder_staging_bytes);
      ++acquisitions;
      return true;
    },
    reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::failed)
    return hdf5_io_error(reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::stopped) {
    return callback_status.ok() ? Status::Unavailable("Non-Cartesian RSS HDF5 preflight stopped before EndOfInput")
                                : callback_status;
  }
  if (!callback_status.ok())
    return callback_status;
  if (!channels.has_value() || acquisitions == 0U || total_samples == 0U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 requires at least one complete acquisition");
  }
  auto shape =
    make_shape(geometry.value().rows, geometry.value().cols, *channels, static_cast<std::uint32_t>(total_samples),
               max_samples_per_acquisition, max_decoder_staging_bytes);
  if (!shape.ok())
    return shape.status();
  const auto declared_channels = validate_declared_channels(scan.value(), shape.value().channels);
  if (!declared_channels.ok())
    return declared_channels;

  const auto xml_document = "{\"ismrmrd_xml\":" + json_string(reader.metadata().xml_header) + "}";
  auto xml_digest = canonical_digest("kspacejet:artifact:noncartesian-rss-hdf5-source-xml", xml_document,
                                     "Non-Cartesian RSS HDF5 source XML input");
  if (!xml_digest.ok())
    return xml_digest.status();
  const auto facts_document =
    "{\"acquisitions\":" + std::to_string(acquisitions) + ",\"channels\":" + std::to_string(shape.value().channels) +
    ",\"cols\":" + std::to_string(shape.value().cols) +
    ",\"max_samples_per_acquisition\":" + std::to_string(shape.value().max_samples_per_acquisition) +
    ",\"rows\":" + std::to_string(shape.value().rows) + ",\"samples\":" + std::to_string(shape.value().total_samples) +
    ",\"source_xml_digest\":" + json_string(xml_digest.value().value()) + "}";
  auto facts_digest = canonical_digest("kspacejet:artifact:noncartesian-rss-hdf5-normalized-scan-facts", facts_document,
                                       "Non-Cartesian RSS HDF5 normalized scan facts");
  if (!facts_digest.ok())
    return facts_digest.status();
  return Preflight{.scan_descriptor = std::move(scan).value(),
                   .shape = std::move(shape).value(),
                   .acquisitions_read = acquisitions,
                   .source_xml_digest = std::move(xml_digest).value(),
                   .normalized_scan_facts_digest = std::move(facts_digest).value()};
}

[[nodiscard]] Status replay_into_executor(const NoncartesianRssHdf5ReconstructionConfig& config,
                                          const Preflight& preflight, SynchronousGraphExecutor& executor) {
  ksj::ismrmrd::DatasetReader reader;
  std::string reader_error;
  if (!reader.open(config.input_file, config.dataset_group, reader_error))
    return hdf5_io_error(reader_error);
  const auto replay_xml_document = "{\"ismrmrd_xml\":" + json_string(reader.metadata().xml_header) + "}";
  auto replay_xml_digest = canonical_digest("kspacejet:artifact:noncartesian-rss-hdf5-source-xml", replay_xml_document,
                                            "Non-Cartesian RSS HDF5 replay XML input");
  if (!replay_xml_digest.ok())
    return replay_xml_digest.status();
  if (replay_xml_digest.value() != preflight.source_xml_digest) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 XML changed between preflight and replay");
  }

  auto kspace_ingress = executor.try_acquire_ingress(kKspaceIngressId);
  if (!kspace_ingress.ok())
    return kspace_ingress.status();
  auto trajectory_ingress = executor.try_acquire_ingress(kTrajectoryIngressId);
  if (!trajectory_ingress.ok())
    return trajectory_ingress.status();
  auto kspace_payload = kspace_ingress.value().writable_payload();
  if (!kspace_payload.ok())
    return kspace_payload.status();
  auto trajectory_payload = trajectory_ingress.value().writable_payload();
  if (!trajectory_payload.ok())
    return trajectory_payload.status();
  if (kspace_payload.value().size() < preflight.shape.kspace_bytes ||
      trajectory_payload.value().size() < preflight.shape.trajectory_bytes) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 ingress pool is smaller than the frozen frame shape");
  }

  auto* const kspace_destination = kspace_payload.value().data();
  auto* const trajectory_destination = trajectory_payload.value().data();
  std::optional<std::uint32_t> expected_channels{preflight.shape.channels};
  std::uint32_t acquisitions{0U};
  std::uint32_t written_samples{0U};
  std::uint32_t max_samples_per_acquisition{0U};
  Quantity max_decoder_staging_bytes{0U};
  Status callback_status = Status::Ok();
  const auto iteration = reader.for_each_acquisition(
    [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
      auto facts = validate_acquisition(acquisition, expected_channels);
      if (!facts.ok()) {
        callback_status = facts.status();
        return false;
      }
      if (facts.value().samples > preflight.shape.total_samples - written_samples ||
          acquisitions == std::numeric_limits<std::uint32_t>::max()) {
        callback_status = Status::ValidationError("Non-Cartesian RSS HDF5 input changed between preflight and replay");
        return false;
      }
      const auto local_samples = static_cast<std::size_t>(facts.value().samples);
      const auto total_samples = static_cast<std::size_t>(preflight.shape.total_samples);
      const auto sample_offset = static_cast<std::size_t>(written_samples);
      for (std::uint32_t channel = 0U; channel < preflight.shape.channels; ++channel) {
        const auto source_offset = static_cast<std::size_t>(channel) * local_samples;
        const auto destination_offset =
          (static_cast<std::size_t>(channel) * total_samples + sample_offset) * sizeof(std::complex<float>);
        std::memcpy(kspace_destination + destination_offset, acquisition.samples.data() + source_offset,
                    local_samples * sizeof(std::complex<float>));
      }
      const auto trajectory_offset = sample_offset * 2U * sizeof(float);
      std::memcpy(trajectory_destination + trajectory_offset, acquisition.trajectory.data(),
                  local_samples * 2U * sizeof(float));
      written_samples += facts.value().samples;
      max_samples_per_acquisition = std::max(max_samples_per_acquisition, facts.value().samples);
      max_decoder_staging_bytes = std::max(max_decoder_staging_bytes, facts.value().decoder_staging_bytes);
      ++acquisitions;
      return true;
    },
    reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::failed)
    return hdf5_io_error(reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::stopped || !callback_status.ok()) {
    return callback_status.ok() ? Status::Unavailable("Non-Cartesian RSS HDF5 replay stopped before EndOfInput")
                                : callback_status;
  }
  if (acquisitions != preflight.acquisitions_read || written_samples != preflight.shape.total_samples ||
      max_samples_per_acquisition != preflight.shape.max_samples_per_acquisition ||
      max_decoder_staging_bytes != preflight.shape.max_decoder_staging_bytes) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 input changed between preflight and replay");
  }

  const DataItemIdentity identity{};
  const auto kspace_commit =
    kspace_ingress.value().seal_and_commit(preflight.shape.kspace_bytes, ksj::base::ConstByteSpan{}, identity);
  if (!kspace_commit.ok())
    return kspace_commit;
  const auto trajectory_commit =
    trajectory_ingress.value().seal_and_commit(preflight.shape.trajectory_bytes, ksj::base::ConstByteSpan{}, identity);
  if (!trajectory_commit.ok())
    return trajectory_commit;
  const auto kspace_end = executor.end_ingress(kKspaceIngressId);
  if (!kspace_end.ok())
    return kspace_end;
  return executor.end_ingress(kTrajectoryIngressId);
}

[[nodiscard]] const graph::PipelineNode* find_pipeline_node(const graph::ResolvedPipeline& pipeline,
                                                            const std::string_view node_id) noexcept {
  const auto found = std::find_if(pipeline.definition().nodes().begin(), pipeline.definition().nodes().end(),
                                  [node_id](const graph::PipelineNode& node) {
                                    return node.id == node_id;
                                  });
  return found == pipeline.definition().nodes().end() ? nullptr : &*found;
}

[[nodiscard]] Result<std::unique_ptr<ProviderNodeInstance>>
make_provider_node(const ExecutionPlan& plan, const graph::ResolvedPipeline& pipeline,
                   const std::filesystem::path& module_path, const std::string_view node_id,
                   const ArtifactDigest& normalized_scan_facts_digest, const std::uint64_t execution_context_id) {
  const auto* node = find_pipeline_node(pipeline, node_id);
  if (node == nullptr || node->canonical_config.empty()) {
    return Status::InternalError(
      "Non-Cartesian RSS HDF5 resolved pipeline omitted a required canonical Provider configuration");
  }
  return ProviderNodeInstance::create(
    plan, {.module_path = module_path,
           .node_id = std::string(node_id),
           .canonical_config = node->canonical_config,
           .start_facts = {.normalized_scan_facts_digest = normalized_scan_facts_digest,
                           .execution_plan_digest = plan.digest(),
                           .run_id = "noncartesian-rss-hdf5",
                           .scan_instance_id = "noncartesian-rss-hdf5",
                           .terminal_epoch = kTerminalEpoch},
           .execution_context_id = execution_context_id,
           .resource_domain_id = 1U,
           .max_backend_concurrency = 1U,
           .numa_node = 0U,
           .device_ordinal = 0U,
           .key_state = {.semantic_key = {}, .placement_key = 0U, .generation = 1U, .home_shard = 0U}});
}

[[nodiscard]] Result<SynchronousFiringResult> fire_node(SynchronousGraphExecutor& executor,
                                                        ProviderNodeInstance& provider, const std::string_view node_id,
                                                        const std::uint64_t resource_occurrence_id) {
  auto invocation = provider.invocation();
  if (!invocation.ok())
    return invocation.status();
  return executor.try_fire(node_id, {.provider_invocation = std::move(invocation).value(),
                                     .resource_occurrence_id = resource_occurrence_id,
                                     .slot_generation = 1U,
                                     .terminal_epoch = kTerminalEpoch});
}

[[nodiscard]] Status finish_node(SynchronousGraphExecutor& executor, ProviderNodeInstance& provider,
                                 const std::string_view node_id, const std::uint64_t resource_occurrence_id) {
  auto invocation = provider.invocation();
  if (!invocation.ok())
    return invocation.status();
  auto terminal = executor.try_finish_node(node_id, {.provider_invocation = std::move(invocation).value(),
                                                     .resource_occurrence_id = resource_occurrence_id,
                                                     .slot_generation = 1U,
                                                     .terminal_epoch = kTerminalEpoch});
  if (!terminal.ok())
    return terminal.status();
  return provider.complete_normal_terminal(terminal.value());
}

[[nodiscard]] Status write_binary_file(const std::filesystem::path& path, const ksj::base::ConstByteSpan bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open())
    return Status::IoError("unable to open Non-Cartesian RSS output image: " + path.string());
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  if (!stream)
    return Status::IoError("unable to write Non-Cartesian RSS output image: " + path.string());
  return Status::Ok();
}

[[nodiscard]] Status write_metadata_file(const std::filesystem::path& path,
                                         const NoncartesianRssHdf5ReconstructionReport& report) {
  if (path.empty())
    return Status::Ok();
  const auto document =
    "{\"acquisitions_read\":" + std::to_string(report.acquisitions_read) +
    ",\"channels\":" + std::to_string(report.channels) + ",\"cols\":" + std::to_string(report.cols) +
    ",\"execution_plan_digest\":" + json_string(report.execution_plan_digest) +
    ",\"image_layout\":\"row_major_contiguous\",\"image_payload_bytes\":" + std::to_string(report.image_payload_bytes) +
    ",\"operators\":[\"noncartesian_adjoint_reconstruct\",\"coil_combine_rss\"],\"rows\":" +
    std::to_string(report.rows) + ",\"samples_read\":" + std::to_string(report.samples_read) +
    ",\"verification_record_digest\":" + json_string(report.verification_record_digest) + "}";
  auto canonical = graph::canonicalize_json(document);
  if (!canonical.ok())
    return canonical.status();
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open())
    return Status::IoError("unable to open Non-Cartesian RSS metadata output: " + path.string());
  stream.write(canonical.value().data(), static_cast<std::streamsize>(canonical.value().size()));
  stream.put('\n');
  stream.flush();
  if (!stream)
    return Status::IoError("unable to write Non-Cartesian RSS metadata output: " + path.string());
  return Status::Ok();
}

} // namespace

Result<NoncartesianRssHdf5ReconstructionReport>
reconstruct_noncartesian_rss_hdf5(const NoncartesianRssHdf5ReconstructionConfig& config) {
  try {
    const auto config_status = validate_config(config);
    if (!config_status.ok())
      return config_status;
    auto preflight = preflight_input(config);
    if (!preflight.ok())
      return preflight.status();
    auto graph_inputs = make_graph_inputs(config, preflight.value().shape);
    if (!graph_inputs.ok())
      return graph_inputs.status();
    auto planning = make_planning_artifacts(preflight.value());
    if (!planning.ok())
      return planning.status();
    auto reconstruct_requirements =
      make_noncartesian_requirements(graph_inputs.value().noncartesian_contract, preflight.value().shape);
    if (!reconstruct_requirements.ok())
      return reconstruct_requirements.status();
    auto combine_requirements =
      make_coil_combine_requirements(graph_inputs.value().coil_combine_contract, preflight.value().shape);
    if (!combine_requirements.ok())
      return combine_requirements.status();

    const graph::PlanBuildRequest request{
      .resolved_pipeline = graph_inputs.value().pipeline,
      .requested_profile = ExecutionProfile::offline_reference,
      .scan_descriptor = preflight.value().scan_descriptor,
      .target_envelope = planning.value().target_envelope,
      .machine_policy = planning.value().machine_policy,
      .artifact_digests = planning.value().digests,
      .operator_contract_bindings = {{.node_id = kReconstructNodeId,
                                      .contract = graph_inputs.value().noncartesian_contract},
                                     {.node_id = kCombineNodeId,
                                      .contract = graph_inputs.value().coil_combine_contract}},
      .node_planning_requirements = {{.node_id = kReconstructNodeId,
                                      .requirements = std::move(reconstruct_requirements).value()},
                                     {.node_id = kCombineNodeId,
                                      .requirements = std::move(combine_requirements).value()}},
    };
    auto compiled = graph::ExecutionPlanCompiler::compile(request);
    if (!compiled.ok())
      return compiled.status();
    auto verification = graph::ExecutionPlanVerifier::verify(compiled.value().plan, request);
    if (!verification.ok())
      return verification.status();
    auto storage = SynchronousGraphPlanStorage::create(compiled.value().plan);
    if (!storage.ok())
      return storage.status();
    auto ledger = std::make_shared<ResourceVectorLedger>(planning.value().machine_policy.resource_capacity());
    auto executor = SynchronousGraphExecutor::create(compiled.value().plan, verification.value(),
                                                     storage.value()->executor_storage(), ledger);
    if (!executor.ok())
      return executor.status();
    auto reconstruct_provider =
      make_provider_node(compiled.value().plan, graph_inputs.value().pipeline, config.noncartesian_provider_module,
                         kReconstructNodeId, preflight.value().normalized_scan_facts_digest, 1U);
    if (!reconstruct_provider.ok())
      return reconstruct_provider.status();
    auto combine_provider =
      make_provider_node(compiled.value().plan, graph_inputs.value().pipeline, config.coil_combine_provider_module,
                         kCombineNodeId, preflight.value().normalized_scan_facts_digest, 2U);
    if (!combine_provider.ok())
      return combine_provider.status();

    const auto replay = replay_into_executor(config, preflight.value(), *executor.value());
    if (!replay.ok()) {
      static_cast<void>(executor.value()->abort());
      return replay;
    }
    auto reconstructed = fire_node(*executor.value(), *reconstruct_provider.value(), kReconstructNodeId, 1U);
    if (!reconstructed.ok() || reconstructed.value().outcome != SynchronousFiringOutcome::done) {
      const auto failure = executor.value()->snapshot().last_error;
      static_cast<void>(executor.value()->abort());
      if (!reconstructed.ok())
        return reconstructed.status();
      return failure.ok()
               ? Status::StateError("Non-Cartesian RSS adjoint Provider did not finish its normal frame firing")
               : failure;
    }
    auto combined = fire_node(*executor.value(), *combine_provider.value(), kCombineNodeId, 2U);
    if (!combined.ok() || combined.value().outcome != SynchronousFiringOutcome::done) {
      const auto failure = executor.value()->snapshot().last_error;
      static_cast<void>(executor.value()->abort());
      if (!combined.ok())
        return combined.status();
      return failure.ok()
               ? Status::StateError("Non-Cartesian RSS coil-combine Provider did not finish its normal frame firing")
               : failure;
    }
    const auto reconstruct_end = finish_node(*executor.value(), *reconstruct_provider.value(), kReconstructNodeId, 3U);
    if (!reconstruct_end.ok()) {
      static_cast<void>(executor.value()->abort());
      return reconstruct_end;
    }
    const auto combine_end = finish_node(*executor.value(), *combine_provider.value(), kCombineNodeId, 4U);
    if (!combine_end.ok()) {
      static_cast<void>(executor.value()->abort());
      return combine_end;
    }

    auto image = executor.value()->try_acquire_egress(kImageEgressId);
    if (!image.ok())
      return image.status();
    auto expected_type = types::image_frame();
    if (!expected_type.ok())
      return expected_type.status();
    auto payload = image.value().payload();
    if (!payload.ok())
      return payload.status();
    if (image.value().type_descriptor() == nullptr ||
        !image.value().type_descriptor()->exactly_matches(expected_type.value()) ||
        payload.value().size() != preflight.value().shape.image_bytes) {
      return Status::ValidationError("Non-Cartesian RSS HDF5 egress did not contain one exact ksj.image-frame payload");
    }
    std::vector<ksj::base::byte> output(payload.value().begin(), payload.value().end());
    const auto acknowledged = image.value().acknowledge_consumed();
    if (!acknowledged.ok())
      return acknowledged;
    if (executor.value()->egress_poll_kind(kImageEgressId) != FixedBufferEdgePollKind::completed) {
      return Status::StateError("Non-Cartesian RSS HDF5 egress did not close after its one expected image");
    }

    NoncartesianRssHdf5ReconstructionReport report{
      .rows = preflight.value().shape.rows,
      .cols = preflight.value().shape.cols,
      .channels = preflight.value().shape.channels,
      .acquisitions_read = preflight.value().acquisitions_read,
      .samples_read = preflight.value().shape.total_samples,
      .image_payload_bytes = preflight.value().shape.image_bytes,
      .execution_plan_digest = compiled.value().plan.digest().value(),
      .verification_record_digest = verification.value().digest().value(),
    };
    const auto image_write = write_binary_file(config.output_image_file, output);
    if (!image_write.ok())
      return image_write;
    const auto metadata_write = write_metadata_file(config.output_metadata_file, report);
    if (!metadata_write.ok())
      return metadata_write;
    return report;
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory("Non-Cartesian RSS HDF5 reconstruction exhausted host memory");
  } catch (const std::exception& exception) {
    return Status::InternalError("Non-Cartesian RSS HDF5 reconstruction threw: " + std::string(exception.what()));
  } catch (...) {
    return Status::InternalError("Non-Cartesian RSS HDF5 reconstruction threw an unknown exception");
  }
}

} // namespace ksj::recon::runtime
