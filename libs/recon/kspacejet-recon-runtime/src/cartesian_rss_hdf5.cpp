#include "kspacejet/recon/runtime/cartesian_rss_hdf5.hpp"

#include "kspacejet/ismrmrd/dataset_reader.hpp"
#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/recon/graph/canonical_json.hpp"
#include "kspacejet/recon/graph/execution_plan_compiler.hpp"
#include "kspacejet/recon/graph/operator_contract_json.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/node_planning_requirements.hpp"
#include "kspacejet/recon/runtime/cartesian_frame_slot.hpp"
#include "kspacejet/recon/runtime/host_frame_assembler.hpp"
#include "kspacejet/recon/runtime/provider_node_instance.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_plan_storage.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <ismrmrd/ismrmrd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <complex>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace ksj::recon::runtime {
namespace {

using ksj::base::Result;
using ksj::base::Status;

constexpr std::uint32_t kMinimumDimension = 2U;
constexpr std::uint32_t kMaximumDimension = 512U;
constexpr std::uint32_t kMaximumChannels = 64U;
constexpr std::uint64_t kTerminalEpoch = 1U;
constexpr std::size_t kContractFileMaximumBytes = 256U * 1024U;
constexpr std::uint64_t kMachineBudgetHeadroomBytes = 16U * 1024U * 1024U;
constexpr Quantity kFramePayloadAlignmentBytes = 64U;
constexpr char kCartesianProviderId[] = "org.kspacejet.cartesian-recon";
constexpr char kCartesianOperatorId[] = "cartesian_ifft2_coil_images";
constexpr char kCoilCombineProviderId[] = "org.kspacejet.coil-combine";
constexpr char kCoilCombineOperatorId[] = "coil_combine_rss";
constexpr char kCalibrationProviderId[] = "org.kspacejet.calibration";
constexpr char kConditioningProviderId[] = "org.kspacejet.kspace-conditioning";
constexpr char kNoiseModelEstimateOperatorId[] = "noise_model_estimate";
constexpr char kNoisePrewhitenOperatorId[] = "noise_prewhiten";
constexpr char kPhaseCorrectionEstimateOperatorId[] = "phase_correction_estimate";
constexpr char kPhaseCorrectOperatorId[] = "phase_correct";
constexpr char kCoilCompressionBasisEstimateOperatorId[] = "coil_compression_basis_estimate";
constexpr char kCoilCompressOperatorId[] = "coil_compress";
constexpr char kReadoutOversamplingRemoveOperatorId[] = "readout_oversampling_remove";
constexpr char kNoiseEstimateNodeId[] = "noise_estimate";
constexpr char kNoisePrewhitenNodeId[] = "noise_prewhiten";
constexpr char kPhaseEstimateNodeId[] = "phase_estimate";
constexpr char kPhaseCorrectNodeId[] = "phase_correct";
constexpr char kCoilBasisEstimateNodeId[] = "coil_basis_estimate";
constexpr char kCoilCompressNodeId[] = "coil_compress";
constexpr char kReadoutCropNodeId[] = "readout_crop";
constexpr char kReconstructNodeId[] = "reconstruct";
constexpr char kCombineNodeId[] = "combine";
constexpr char kKspaceIngressId[] = "kspace";
constexpr char kNoiseIngressId[] = "noise";
constexpr char kPhaseIngressId[] = "phase";
constexpr char kCoilCalibrationIngressId[] = "coil_calibration";
constexpr char kImageEgressId[] = "images";
constexpr char kNoiseModelBindingId[] = "noise_model";
constexpr char kPhaseModelBindingId[] = "phase_model";
constexpr char kCoilBasisBindingId[] = "coil_basis";
constexpr Quantity kMaximumCalibrationInputBytes = 64U * 1024U * 1024U;

struct Shape final {
  std::uint32_t rows{0U};
  std::uint32_t input_cols{0U};
  std::uint32_t cols{0U};
  std::uint32_t physical_channels{0U};
  std::uint32_t channels{0U};
  Quantity raw_kspace_bytes{0U};
  Quantity raw_kspace_charged_bytes{0U};
  Quantity compressed_kspace_bytes{0U};
  Quantity compressed_kspace_charged_bytes{0U};
  Quantity kspace_bytes{0U};
  Quantity coil_image_bytes{0U};
  Quantity image_bytes{0U};
  Quantity kspace_charged_bytes{0U};
  Quantity coil_image_charged_bytes{0U};
  Quantity image_charged_bytes{0U};
  Quantity cartesian_scratch_bytes{0U};
  Quantity line_bytes{0U};
};

struct CalibrationLane final {
  std::uint32_t line_count{0U};
  Quantity payload_bytes{0U};
  Quantity charged_bytes{0U};
};

// The one implemented route has one source of spatial geometry: the ISMRMRD
// encoding declaration.  Acquisition headers then prove that every raw line
// actually conforms to that declaration.  Keep this separate from Shape so
// no public/CLI configuration can accidentally become a competing source.
struct FrameDimensions final {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
};

struct Preflight final {
  ScanDescriptor scan_descriptor;
  Shape shape;
  std::uint32_t acquisitions_read{0U};
  CalibrationLane noise;
  CalibrationLane phase;
  CalibrationLane coil;
  ArtifactDigest source_xml_digest;
  ArtifactDigest normalized_scan_facts_digest;
};

struct PlanningArtifacts final {
  TargetEnvelope target_envelope;
  MachinePolicy machine_policy;
  graph::PlanArtifactDigests digests;
};

struct RuntimeNode final {
  std::string node_id;
  std::filesystem::path provider_module;
  OperatorContract contract;
};

struct ResolvedGraphInputs final {
  graph::ResolvedPipeline pipeline;
  std::vector<RuntimeNode> nodes;
  std::vector<std::string> estimator_node_ids;
  std::vector<std::string> data_node_ids;
};

[[nodiscard]] Result<Quantity> checked_product(const Quantity left, const Quantity right, const std::string_view name) {
  if (left != 0U && right > std::numeric_limits<Quantity>::max() / left) {
    return Status::ValidationError("Cartesian RSS HDF5 " + std::string(name) + " overflows a quantity");
  }
  return left * right;
}

[[nodiscard]] Result<Quantity> checked_sum(const Quantity left, const Quantity right, const std::string_view name) {
  if (right > std::numeric_limits<Quantity>::max() - left) {
    return Status::ValidationError("Cartesian RSS HDF5 " + std::string(name) + " overflows a quantity");
  }
  return left + right;
}

// Buffer pool slots are aligned to the exact frozen TypeDescriptor alignment.
// A Provider may seal a shorter logical payload, but the plan must reserve an
// aligned physical capacity.  Keep the two facts separate so a 2x2 float
// image remains a 16-byte output rather than being misreported as 64 bytes.
[[nodiscard]] Result<Quantity> aligned_payload_capacity(const Quantity logical_bytes, const Quantity alignment,
                                                        const std::string_view name) {
  if (logical_bytes == 0U || alignment == 0U) {
    return Status::ValidationError("Cartesian RSS HDF5 " + std::string(name) + " has zero payload/alignment");
  }
  const auto remainder = logical_bytes % alignment;
  if (remainder == 0U)
    return logical_bytes;
  return checked_sum(logical_bytes, alignment - remainder, name);
}

[[nodiscard]] Result<Shape> make_shape(const std::uint32_t rows, const std::uint32_t input_cols,
                                       const std::uint32_t cols, const std::uint32_t physical_channels,
                                       const std::uint32_t channels) {
  if (rows < kMinimumDimension || rows > kMaximumDimension || input_cols < kMinimumDimension ||
      input_cols > kMaximumDimension || cols < kMinimumDimension || cols > kMaximumDimension ||
      physical_channels == 0U || physical_channels > kMaximumChannels || channels == 0U ||
      channels > kMaximumChannels) {
    return Status::InvalidArgument(
      "Cartesian RSS HDF5 supports rows/readout in [2,512] and physical/final channels in [1,64]");
  }
  auto input_pixels = checked_product(rows, input_cols, "raw pixel count");
  if (!input_pixels.ok())
    return input_pixels.status();
  auto output_pixels = checked_product(rows, cols, "reconstruction pixel count");
  if (!output_pixels.ok())
    return output_pixels.status();
  auto raw_channel_pixels = checked_product(input_pixels.value(), physical_channels, "raw multi-channel pixel count");
  if (!raw_channel_pixels.ok())
    return raw_channel_pixels.status();
  auto compressed_channel_pixels =
    checked_product(input_pixels.value(), channels, "compressed multi-channel pixel count");
  if (!compressed_channel_pixels.ok())
    return compressed_channel_pixels.status();
  auto final_channel_pixels = checked_product(output_pixels.value(), channels, "final multi-channel pixel count");
  if (!final_channel_pixels.ok())
    return final_channel_pixels.status();
  auto raw_kspace = checked_product(raw_channel_pixels.value(), sizeof(std::complex<float>), "raw k-space bytes");
  if (!raw_kspace.ok())
    return raw_kspace.status();
  auto compressed_kspace =
    checked_product(compressed_channel_pixels.value(), sizeof(std::complex<float>), "compressed k-space bytes");
  if (!compressed_kspace.ok())
    return compressed_kspace.status();
  auto final_kspace = checked_product(final_channel_pixels.value(), sizeof(std::complex<float>), "final k-space bytes");
  if (!final_kspace.ok())
    return final_kspace.status();
  auto image = checked_product(output_pixels.value(), sizeof(float), "RSS image bytes");
  if (!image.ok())
    return image.status();
  auto frame_workspace_elements = checked_product(output_pixels.value(), 2U, "IFFT frame workspace element count");
  if (!frame_workspace_elements.ok())
    return frame_workspace_elements.status();
  auto line_workspace_elements =
    checked_product(static_cast<Quantity>(std::max(rows, cols)), 2U, "IFFT line workspace element count");
  if (!line_workspace_elements.ok())
    return line_workspace_elements.status();
  auto scratch_elements =
    checked_sum(frame_workspace_elements.value(), line_workspace_elements.value(), "IFFT workspace element count");
  if (!scratch_elements.ok())
    return scratch_elements.status();
  auto scratch = checked_product(scratch_elements.value(), sizeof(std::complex<float>), "IFFT workspace bytes");
  if (!scratch.ok())
    return scratch.status();
  auto line_elements = checked_product(input_cols, physical_channels, "acquisition line element count");
  if (!line_elements.ok())
    return line_elements.status();
  auto line = checked_product(line_elements.value(), sizeof(std::complex<float>), "acquisition line bytes");
  if (!line.ok())
    return line.status();
  auto raw_charge =
    aligned_payload_capacity(raw_kspace.value(), kFramePayloadAlignmentBytes, "raw k-space pool capacity");
  if (!raw_charge.ok())
    return raw_charge.status();
  auto compressed_charge = aligned_payload_capacity(compressed_kspace.value(), kFramePayloadAlignmentBytes,
                                                    "compressed k-space pool capacity");
  if (!compressed_charge.ok())
    return compressed_charge.status();
  auto kspace_charge =
    aligned_payload_capacity(final_kspace.value(), kFramePayloadAlignmentBytes, "final k-space pool capacity");
  if (!kspace_charge.ok())
    return kspace_charge.status();
  auto coil_charge =
    aligned_payload_capacity(final_kspace.value(), kFramePayloadAlignmentBytes, "coil-image pool capacity");
  if (!coil_charge.ok())
    return coil_charge.status();
  auto image_charge = aligned_payload_capacity(image.value(), kFramePayloadAlignmentBytes, "image pool capacity");
  if (!image_charge.ok())
    return image_charge.status();
  return Shape{.rows = rows,
               .input_cols = input_cols,
               .cols = cols,
               .physical_channels = physical_channels,
               .channels = channels,
               .raw_kspace_bytes = raw_kspace.value(),
               .raw_kspace_charged_bytes = raw_charge.value(),
               .compressed_kspace_bytes = compressed_kspace.value(),
               .compressed_kspace_charged_bytes = compressed_charge.value(),
               .kspace_bytes = final_kspace.value(),
               .coil_image_bytes = final_kspace.value(),
               .image_bytes = image.value(),
               .kspace_charged_bytes = kspace_charge.value(),
               .coil_image_charged_bytes = coil_charge.value(),
               .image_charged_bytes = image_charge.value(),
               .cartesian_scratch_bytes = scratch.value(),
               .line_bytes = line.value()};
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

[[nodiscard]] Status validate_provider_operator(const CartesianRssHdf5ProviderOperator& selection,
                                                const std::string_view branch_name) {
  if (selection.provider_module.empty() || selection.operator_contract.empty()) {
    return Status::InvalidArgument("Cartesian RSS HDF5 " + std::string(branch_name) +
                                   " requires both an explicit Provider module and OperatorContract");
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_config(const CartesianRssHdf5ReconstructionConfig& config) {
  if (config.input_file.empty() || config.output_image_file.empty() || config.cartesian_provider_module.empty() ||
      config.coil_combine_provider_module.empty() || config.cartesian_operator_contract.empty() ||
      config.coil_combine_operator_contract.empty() || config.dataset_group.empty()) {
    return Status::InvalidArgument(
      "Cartesian RSS HDF5 requires input/output, both explicit Provider modules/contracts, and a dataset group");
  }
  if ((!config.output_metadata_file.empty() && same_path(config.output_image_file, config.output_metadata_file)) ||
      same_path(config.input_file, config.output_image_file) ||
      (!config.output_metadata_file.empty() && same_path(config.input_file, config.output_metadata_file))) {
    return Status::InvalidArgument(
      "Cartesian RSS HDF5 input, image output, and metadata output must be distinct files");
  }
  if (config.noise_prewhiten.has_value()) {
    const auto estimator =
      validate_provider_operator(config.noise_prewhiten->noise_model_estimate, "noise_model_estimate");
    if (!estimator.ok())
      return estimator;
    const auto apply = validate_provider_operator(config.noise_prewhiten->noise_prewhiten, "noise_prewhiten");
    if (!apply.ok())
      return apply;
  }
  if (config.phase_correction.has_value()) {
    const auto estimator =
      validate_provider_operator(config.phase_correction->phase_correction_estimate, "phase_correction_estimate");
    if (!estimator.ok())
      return estimator;
    const auto apply = validate_provider_operator(config.phase_correction->phase_correct, "phase_correct");
    if (!apply.ok())
      return apply;
  }
  if (config.coil_compression.has_value()) {
    const auto estimator = validate_provider_operator(config.coil_compression->coil_compression_basis_estimate,
                                                      "coil_compression_basis_estimate");
    if (!estimator.ok())
      return estimator;
    const auto apply = validate_provider_operator(config.coil_compression->coil_compress, "coil_compress");
    if (!apply.ok())
      return apply;
    if (config.coil_compression->virtual_channel_count == 0U ||
        config.coil_compression->virtual_channel_count > kMaximumChannels) {
      return Status::InvalidArgument("Cartesian RSS HDF5 coil_compression virtual_channel_count must be in [1,64]");
    }
  }
  if (config.readout_oversampling_removal.has_value()) {
    const auto crop = validate_provider_operator(config.readout_oversampling_removal->readout_oversampling_remove,
                                                 "readout_oversampling_remove");
    if (!crop.ok())
      return crop;
  }
  return Status::Ok();
}

[[nodiscard]] Status hdf5_io_error(const std::string_view message) {
  return Status::IoError("Cartesian RSS HDF5 input failed: " + std::string(message));
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
    return Status::InvalidArgument("Cartesian RSS HDF5 OperatorContract path is not a regular file: " + path.string());
  }
  const auto bytes = std::filesystem::file_size(path, error);
  if (error)
    return Status::IoError("unable to determine OperatorContract file size: " + path.string());
  if (bytes == 0U || bytes > kContractFileMaximumBytes || bytes > std::numeric_limits<std::size_t>::max()) {
    return Status::InvalidArgument("Cartesian RSS HDF5 OperatorContract file is empty or exceeds 256 KiB: " +
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

struct ExpectedContractPort final {
  std::string_view name;
  std::string_view type;
  PortDirection direction;
};

[[nodiscard]] Status validate_contract_ports(const OperatorContract& contract,
                                             const std::string_view expected_operator_id,
                                             const std::span<const ExpectedContractPort> expected_ports) {
  if (contract.operator_id() != expected_operator_id || contract.ports().size() != expected_ports.size()) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 OperatorContract does not describe the required typed operator '" +
      std::string(expected_operator_id) + "'");
  }
  for (std::size_t index = 0U; index < expected_ports.size(); ++index) {
    const auto& actual = contract.ports()[index];
    const auto& expected = expected_ports[index];
    if (actual.direction != expected.direction || actual.name != expected.name ||
        actual.type_descriptor.type_ref().value() != expected.type) {
      return Status::ValidationError(
        "Cartesian RSS HDF5 OperatorContract ports do not match the required typed route for '" +
        std::string(expected_operator_id) + "'");
    }
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
  auto module = ksj::provider::loader::ProviderModule::load(module_path,
                                                            {.host_build_id = "KSpaceJet cartesian-rss HDF5 resolver"});
  if (!module.ok())
    return module.status();
  const auto* descriptor = module.value().descriptor();
  if (descriptor == nullptr || descriptor->provider_id != expected_provider_id) {
    return Status::ValidationError("Cartesian RSS HDF5 module does not expose required Provider '" +
                                   std::string(expected_provider_id) + "'");
  }
  const auto found = std::find_if(descriptor->operators.begin(), descriptor->operators.end(),
                                  [expected_operator_id](const auto& candidate) {
                                    return candidate.operator_id == expected_operator_id;
                                  });
  if (found == descriptor->operators.end()) {
    return Status::ValidationError("Cartesian RSS HDF5 module omits required Operator '" +
                                   std::string(expected_operator_id) + "'");
  }
  auto bundle = artifact_digest(descriptor->bundle_digest, "Cartesian RSS HDF5 Provider bundle digest");
  if (!bundle.ok())
    return bundle.status();
  return graph::ResolvedProvider{.alias = std::string(alias),
                                 .provider_id = std::string(expected_provider_id),
                                 .bundle_digest = std::move(bundle).value(),
                                 .operators = {{.id = std::string(expected_operator_id)}}};
}

struct GraphNodeSpec final {
  std::string node_id;
  std::string provider_alias;
  std::string provider_id;
  std::string operator_id;
  std::string canonical_config;
  std::filesystem::path provider_module;
  std::filesystem::path operator_contract;
  std::vector<ExpectedContractPort> ports;
};

struct DataRoute final {
  std::string node_id;
  std::string input_port;
  std::string output_port;
};

struct CalibrationRoute final {
  std::string binding_id;
  std::string producer_node;
  std::string producer_port;
  std::string consumer_node;
  std::string consumer_port;
};

[[nodiscard]] Result<ResolvedGraphInputs> make_graph_inputs(const CartesianRssHdf5ReconstructionConfig& config,
                                                            const Shape& shape) {
  const auto frame_config = "{\"channels\":" + std::to_string(shape.channels) +
                            ",\"cols\":" + std::to_string(shape.cols) + ",\"rows\":" + std::to_string(shape.rows) + "}";
  const auto raw_frame_config = "{\"channel_count\":" + std::to_string(shape.physical_channels) +
                                ",\"cols\":" + std::to_string(shape.input_cols) +
                                ",\"rows\":" + std::to_string(shape.rows) + "}";
  std::vector<GraphNodeSpec> specs;
  std::vector<DataRoute> data_routes;
  std::vector<CalibrationRoute> calibration_routes;
  std::vector<std::string> estimator_node_ids;

  const auto add_estimator = [&](GraphNodeSpec specification) {
    estimator_node_ids.push_back(specification.node_id);
    specs.push_back(std::move(specification));
  };

  if (config.noise_prewhiten.has_value()) {
    add_estimator({.node_id = kNoiseEstimateNodeId,
                   .provider_alias = "noise_estimator",
                   .provider_id = kCalibrationProviderId,
                   .operator_id = kNoiseModelEstimateOperatorId,
                   .canonical_config = "{\"channel_count\":" + std::to_string(shape.physical_channels) + "}",
                   .provider_module = config.noise_prewhiten->noise_model_estimate.provider_module,
                   .operator_contract = config.noise_prewhiten->noise_model_estimate.operator_contract,
                   .ports = {{"noise_calibration", types::kNoiseCalibrationFrameTypeRef, PortDirection::input},
                             {"noise_model", types::kNoiseModelTypeRef, PortDirection::output}}});
    specs.push_back({.node_id = kNoisePrewhitenNodeId,
                     .provider_alias = "noise_prewhitener",
                     .provider_id = kConditioningProviderId,
                     .operator_id = kNoisePrewhitenOperatorId,
                     .canonical_config = raw_frame_config,
                     .provider_module = config.noise_prewhiten->noise_prewhiten.provider_module,
                     .operator_contract = config.noise_prewhiten->noise_prewhiten.operator_contract,
                     .ports = {{"kspace", types::kKspaceFrameTypeRef, PortDirection::input},
                               {"noise_model", types::kNoiseModelTypeRef, PortDirection::input},
                               {"prewhitened_kspace", types::kKspaceFrameTypeRef, PortDirection::output}}});
    data_routes.push_back(
      {.node_id = kNoisePrewhitenNodeId, .input_port = "kspace", .output_port = "prewhitened_kspace"});
    calibration_routes.push_back({.binding_id = kNoiseModelBindingId,
                                  .producer_node = kNoiseEstimateNodeId,
                                  .producer_port = "noise_model",
                                  .consumer_node = kNoisePrewhitenNodeId,
                                  .consumer_port = "noise_model"});
  }
  if (config.phase_correction.has_value()) {
    add_estimator({.node_id = kPhaseEstimateNodeId,
                   .provider_alias = "phase_estimator",
                   .provider_id = kCalibrationProviderId,
                   .operator_id = kPhaseCorrectionEstimateOperatorId,
                   .canonical_config = "{\"channel_count\":" + std::to_string(shape.physical_channels) +
                                       ",\"readout_sample_count\":" + std::to_string(shape.input_cols) + "}",
                   .provider_module = config.phase_correction->phase_correction_estimate.provider_module,
                   .operator_contract = config.phase_correction->phase_correction_estimate.operator_contract,
                   .ports = {{"phase_reference", types::kPhaseReferenceFrameTypeRef, PortDirection::input},
                             {"phase_model", types::kPhaseModelTypeRef, PortDirection::output}}});
    specs.push_back({.node_id = kPhaseCorrectNodeId,
                     .provider_alias = "phase_corrector",
                     .provider_id = kConditioningProviderId,
                     .operator_id = kPhaseCorrectOperatorId,
                     .canonical_config = raw_frame_config,
                     .provider_module = config.phase_correction->phase_correct.provider_module,
                     .operator_contract = config.phase_correction->phase_correct.operator_contract,
                     .ports = {{"kspace", types::kKspaceFrameTypeRef, PortDirection::input},
                               {"phase_model", types::kPhaseModelTypeRef, PortDirection::input},
                               {"phase_corrected_kspace", types::kKspaceFrameTypeRef, PortDirection::output}}});
    data_routes.push_back(
      {.node_id = kPhaseCorrectNodeId, .input_port = "kspace", .output_port = "phase_corrected_kspace"});
    calibration_routes.push_back({.binding_id = kPhaseModelBindingId,
                                  .producer_node = kPhaseEstimateNodeId,
                                  .producer_port = "phase_model",
                                  .consumer_node = kPhaseCorrectNodeId,
                                  .consumer_port = "phase_model"});
  }
  if (config.coil_compression.has_value()) {
    const auto virtual_channels = config.coil_compression->virtual_channel_count;
    add_estimator({.node_id = kCoilBasisEstimateNodeId,
                   .provider_alias = "coil_basis_estimator",
                   .provider_id = kCalibrationProviderId,
                   .operator_id = kCoilCompressionBasisEstimateOperatorId,
                   .canonical_config = "{\"physical_channel_count\":" + std::to_string(shape.physical_channels) +
                                       ",\"virtual_channel_count\":" + std::to_string(virtual_channels) + "}",
                   .provider_module = config.coil_compression->coil_compression_basis_estimate.provider_module,
                   .operator_contract = config.coil_compression->coil_compression_basis_estimate.operator_contract,
                   .ports = {{"kspace", types::kKspaceFrameTypeRef, PortDirection::input},
                             {"coil_basis", types::kCoilCompressionBasisTypeRef, PortDirection::output}}});
    specs.push_back({.node_id = kCoilCompressNodeId,
                     .provider_alias = "coil_compressor",
                     .provider_id = kConditioningProviderId,
                     .operator_id = kCoilCompressOperatorId,
                     .canonical_config = "{\"cols\":" + std::to_string(shape.input_cols) +
                                         ",\"physical_channel_count\":" + std::to_string(shape.physical_channels) +
                                         ",\"rows\":" + std::to_string(shape.rows) +
                                         ",\"virtual_channel_count\":" + std::to_string(virtual_channels) + "}",
                     .provider_module = config.coil_compression->coil_compress.provider_module,
                     .operator_contract = config.coil_compression->coil_compress.operator_contract,
                     .ports = {{"kspace", types::kKspaceFrameTypeRef, PortDirection::input},
                               {"coil_basis", types::kCoilCompressionBasisTypeRef, PortDirection::input},
                               {"compressed_kspace", types::kKspaceFrameTypeRef, PortDirection::output}}});
    data_routes.push_back({.node_id = kCoilCompressNodeId, .input_port = "kspace", .output_port = "compressed_kspace"});
    calibration_routes.push_back({.binding_id = kCoilBasisBindingId,
                                  .producer_node = kCoilBasisEstimateNodeId,
                                  .producer_port = "coil_basis",
                                  .consumer_node = kCoilCompressNodeId,
                                  .consumer_port = "coil_basis"});
  }
  if (config.readout_oversampling_removal.has_value()) {
    specs.push_back(
      {.node_id = kReadoutCropNodeId,
       .provider_alias = "readout_cropper",
       .provider_id = kConditioningProviderId,
       .operator_id = kReadoutOversamplingRemoveOperatorId,
       .canonical_config =
         "{\"channel_count\":" + std::to_string(shape.channels) +
         ",\"input_cols\":" + std::to_string(shape.input_cols) + ",\"output_cols\":" + std::to_string(shape.cols) +
         ",\"readout_offset\":" + std::to_string(config.readout_oversampling_removal->readout_offset) +
         ",\"rows\":" + std::to_string(shape.rows) + "}",
       .provider_module = config.readout_oversampling_removal->readout_oversampling_remove.provider_module,
       .operator_contract = config.readout_oversampling_removal->readout_oversampling_remove.operator_contract,
       .ports = {{"kspace", types::kKspaceFrameTypeRef, PortDirection::input},
                 {"cropped_kspace", types::kKspaceFrameTypeRef, PortDirection::output}}});
    data_routes.push_back({.node_id = kReadoutCropNodeId, .input_port = "kspace", .output_port = "cropped_kspace"});
  }
  specs.push_back({.node_id = kReconstructNodeId,
                   .provider_alias = "cartesian",
                   .provider_id = kCartesianProviderId,
                   .operator_id = kCartesianOperatorId,
                   .canonical_config = frame_config,
                   .provider_module = config.cartesian_provider_module,
                   .operator_contract = config.cartesian_operator_contract,
                   .ports = {{"kspace", types::kKspaceFrameTypeRef, PortDirection::input},
                             {"coil_images", types::kCoilImageFrameTypeRef, PortDirection::output}}});
  data_routes.push_back({.node_id = kReconstructNodeId, .input_port = "kspace", .output_port = "coil_images"});
  specs.push_back({.node_id = kCombineNodeId,
                   .provider_alias = "coilcombine",
                   .provider_id = kCoilCombineProviderId,
                   .operator_id = kCoilCombineOperatorId,
                   .canonical_config = frame_config,
                   .provider_module = config.coil_combine_provider_module,
                   .operator_contract = config.coil_combine_operator_contract,
                   .ports = {{"coil_images", types::kCoilImageFrameTypeRef, PortDirection::input},
                             {"image", types::kImageFrameTypeRef, PortDirection::output}}});
  data_routes.push_back({.node_id = kCombineNodeId, .input_port = "coil_images", .output_port = "image"});

  std::vector<graph::ResolvedProvider> providers;
  providers.reserve(specs.size());
  std::vector<RuntimeNode> runtime_nodes;
  runtime_nodes.reserve(specs.size());
  for (const auto& specification : specs) {
    auto json = read_contract_file(specification.operator_contract);
    if (!json.ok())
      return json.status();
    auto contract = graph::parse_operator_contract_json(json.value());
    if (!contract.ok())
      return contract.status();
    const auto contract_status =
      validate_contract_ports(contract.value(), specification.operator_id, specification.ports);
    if (!contract_status.ok())
      return contract_status;
    auto provider = resolve_provider(specification.provider_module, specification.provider_alias,
                                     specification.provider_id, specification.operator_id);
    if (!provider.ok())
      return provider.status();
    providers.push_back(std::move(provider).value());
    runtime_nodes.push_back({.node_id = specification.node_id,
                             .provider_module = specification.provider_module,
                             .contract = std::move(contract).value()});
  }

  std::string provider_requirements;
  std::string nodes;
  const auto append_json = [](std::string& array, const std::string& value) {
    if (!array.empty())
      array.push_back(',');
    array += value;
  };
  for (const auto& specification : specs) {
    append_json(provider_requirements, "{\"alias\":" + json_string(specification.provider_alias) +
                                         ",\"provider_id\":" + json_string(specification.provider_id) + "}");
    append_json(nodes, "{\"id\":" + json_string(specification.node_id) +
                         ",\"operator\":{\"provider\":" + json_string(specification.provider_alias) +
                         ",\"id\":" + json_string(specification.operator_id) +
                         "},\"config\":" + specification.canonical_config + "}");
  }

  std::string edges;
  for (std::size_t index = 1U; index < data_routes.size(); ++index) {
    const auto& source = data_routes[index - 1U];
    const auto& target = data_routes[index];
    append_json(edges,
                "{\"id\":\"data_" + std::to_string(index) + "\",\"from\":{\"node\":" + json_string(source.node_id) +
                  ",\"port\":" + json_string(source.output_port) + "},\"to\":{\"node\":" + json_string(target.node_id) +
                  ",\"port\":" + json_string(target.input_port) + "}}");
  }

  std::string ingress;
  append_json(ingress, "{\"id\":\"kspace\",\"type\":\"ksj.kspace-frame\",\"to\":{\"node\":" +
                         json_string(data_routes.front().node_id) +
                         ",\"port\":" + json_string(data_routes.front().input_port) + "}}");
  if (config.noise_prewhiten.has_value()) {
    append_json(ingress,
                "{\"id\":\"noise\",\"type\":\"ksj.noise-calibration-frame\",\"to\":{\"node\":\"noise_estimate\","
                "\"port\":\"noise_calibration\"}}");
  }
  if (config.phase_correction.has_value()) {
    append_json(ingress, "{\"id\":\"phase\",\"type\":\"ksj.phase-reference-frame\",\"to\":{\"node\":\"phase_estimate\","
                         "\"port\":\"phase_reference\"}}");
  }
  if (config.coil_compression.has_value()) {
    append_json(ingress,
                "{\"id\":\"coil_calibration\",\"type\":\"ksj.kspace-frame\",\"to\":{\"node\":\"coil_basis_estimate\","
                "\"port\":\"kspace\"}}");
  }

  std::string calibration;
  for (const auto& route : calibration_routes) {
    append_json(calibration, "{\"id\":" + json_string(route.binding_id) + ",\"producer\":{\"node\":" +
                               json_string(route.producer_node) + ",\"port\":" + json_string(route.producer_port) +
                               "},\"consumers\":[{\"node\":" + json_string(route.consumer_node) +
                               ",\"port\":" + json_string(route.consumer_port) + "}]}");
  }

  const auto pipeline_document =
    "{\"kind\":\"PipelineDefinition\",\"pipeline\":{\"id\":\"org.kspacejet.cartesian-rss\","
    "\"display_name\":\"Cartesian RSS reconstruction\"},\"allowed_profiles\":[\"offline-reference\"],"
    "\"parameters\":{},\"provider_requirements\":[" +
    provider_requirements + "],\"nodes\":[" + nodes + "],\"edges\":[" + edges + "],\"bindings\":{\"ingress\":[" +
    ingress +
    "],\"egress\":[{\"id\":\"images\",\"type\":\"ksj.image-frame\",\"from\":{\"node\":\"combine\","
    "\"port\":\"image\"}}],\"calibration\":[" +
    calibration + "],\"merge\":[]},\"annotations\":{}}";
  auto definition = graph::PipelineDefinition::parse_json(pipeline_document);
  if (!definition.ok())
    return definition.status();
  auto pipeline = graph::ResolvedPipeline::resolve(std::move(definition).value(), std::move(providers));
  if (!pipeline.ok())
    return pipeline.status();
  std::vector<std::string> data_node_ids;
  data_node_ids.reserve(data_routes.size());
  for (const auto& route : data_routes) {
    data_node_ids.push_back(route.node_id);
  }
  return ResolvedGraphInputs{.pipeline = std::move(pipeline).value(),
                             .nodes = std::move(runtime_nodes),
                             .estimator_node_ids = std::move(estimator_node_ids),
                             .data_node_ids = std::move(data_node_ids)};
}

struct InputRequirement final {
  std::string_view port_name;
  Quantity charged_bytes{0U};
};

[[nodiscard]] Result<NodePlanningRequirements>
make_frame_requirements(const OperatorContract& contract, const std::span<const InputRequirement> inputs,
                        const std::string_view output_port, const Quantity output_bytes, const Quantity scratch_bytes) {
  if (inputs.empty() || output_bytes == 0U) {
    return Status::InternalError("Cartesian RSS HDF5 attempted to plan an empty node activation");
  }
  Quantity aggregate_input_bytes{0U};
  RatePhaseSpec phase;
  phase.inputs.reserve(inputs.size());
  for (const auto& input : inputs) {
    if (input.port_name.empty() || input.charged_bytes == 0U) {
      return Status::InternalError("Cartesian RSS HDF5 attempted to plan a zero-byte node input");
    }
    auto aggregate = checked_sum(aggregate_input_bytes, input.charged_bytes, "node input envelope");
    if (!aggregate.ok())
      return aggregate.status();
    aggregate_input_bytes = aggregate.value();
    phase.inputs.push_back(
      {.port_name = std::string(input.port_name), .items = 1U, .charged_bytes = input.charged_bytes});
  }
  phase.outputs.push_back({.port_name = std::string(output_port), .items = 1U, .charged_bytes = output_bytes});
  const auto input_count = static_cast<Quantity>(inputs.size());
  return NodePlanningRequirements::create({.execution = {.input_granularity = InputGranularity::frame,
                                                         .partition_key = {},
                                                         .max_active_keys = 1U,
                                                         .max_in_flight = 1U,
                                                         .max_items_per_activation = input_count},
                                           .batch = {.min_items = input_count,
                                                     .preferred_items = input_count,
                                                     .max_items = input_count,
                                                     .max_charged_bytes = aggregate_input_bytes},
                                           .rates = {.kind = RateKind::sdf, .static_phases = {std::move(phase)}},
                                           .resources = {.scratch_charged_bytes_per_firing = scratch_bytes,
                                                         .per_key_state_charged_bytes = 0U,
                                                         .per_scan_workspace_charged_bytes = 0U,
                                                         .retention_charged_bytes = 0U,
                                                         .output_items = 1U,
                                                         .output_charged_bytes = output_bytes,
                                                         .cpu_permits = 1U,
                                                         .memory_domain = MemoryDomain::host},
                                           .terminal = {.normal_max_output_items = 0U,
                                                        .normal_max_output_charged_bytes = 0U,
                                                        .normal_max_async_tokens = 0U,
                                                        .cancel_max_async_tokens = 0U}},
                                          contract);
}

[[nodiscard]] Result<Quantity> complex_matrix_charged_bytes(const Quantity rows, const Quantity cols,
                                                            const std::string_view name) {
  auto elements = checked_product(rows, cols, std::string(name) + " element count");
  if (!elements.ok())
    return elements.status();
  auto logical_bytes = checked_product(elements.value(), sizeof(std::complex<float>), std::string(name) + " bytes");
  if (!logical_bytes.ok())
    return logical_bytes.status();
  return aligned_payload_capacity(logical_bytes.value(), kFramePayloadAlignmentBytes,
                                  std::string(name) + " pool capacity");
}

[[nodiscard]] const RuntimeNode* find_runtime_node(const ResolvedGraphInputs& graph_inputs,
                                                   const std::string_view node_id) noexcept {
  const auto found =
    std::find_if(graph_inputs.nodes.begin(), graph_inputs.nodes.end(), [node_id](const RuntimeNode& node) {
      return node.node_id == node_id;
    });
  return found == graph_inputs.nodes.end() ? nullptr : &*found;
}

struct CartesianPlanningBindings final {
  std::vector<graph::OperatorContractBinding> contracts;
  std::vector<NodePlanningRequirementsBinding> requirements;
};

[[nodiscard]] Result<CartesianPlanningBindings>
make_cartesian_planning_bindings(const ResolvedGraphInputs& graph_inputs, const Preflight& preflight) {
  const auto& shape = preflight.shape;
  auto noise_model_bytes =
    complex_matrix_charged_bytes(shape.physical_channels, shape.physical_channels, "noise model");
  if (!noise_model_bytes.ok())
    return noise_model_bytes.status();
  auto phase_model_bytes = complex_matrix_charged_bytes(shape.physical_channels, shape.input_cols, "phase model");
  if (!phase_model_bytes.ok())
    return phase_model_bytes.status();
  auto coil_basis_bytes =
    complex_matrix_charged_bytes(shape.channels, shape.physical_channels, "coil-compression basis");
  if (!coil_basis_bytes.ok())
    return coil_basis_bytes.status();

  CartesianPlanningBindings result;
  result.contracts.reserve(graph_inputs.nodes.size());
  result.requirements.reserve(graph_inputs.nodes.size());
  const auto add = [&](const std::string_view node_id, const std::span<const InputRequirement> inputs,
                       const std::string_view output_port, const Quantity output_bytes,
                       const Quantity scratch_bytes) -> Status {
    const auto* node = find_runtime_node(graph_inputs, node_id);
    if (node == nullptr) {
      return Status::InternalError("Cartesian RSS HDF5 graph omitted planning node '" + std::string(node_id) + "'");
    }
    auto requirements = make_frame_requirements(node->contract, inputs, output_port, output_bytes, scratch_bytes);
    if (!requirements.ok())
      return requirements.status();
    result.contracts.push_back({.node_id = node->node_id, .contract = node->contract});
    result.requirements.push_back({.node_id = node->node_id, .requirements = std::move(requirements).value()});
    return Status::Ok();
  };

  if (std::find(graph_inputs.estimator_node_ids.begin(), graph_inputs.estimator_node_ids.end(), kNoiseEstimateNodeId) !=
      graph_inputs.estimator_node_ids.end()) {
    if (preflight.noise.charged_bytes == 0U) {
      return Status::InternalError("Cartesian RSS HDF5 graph enables noise estimation without a calibration ingress");
    }
    const Quantity scratch = static_cast<Quantity>(24U) * shape.physical_channels * shape.physical_channels +
                             static_cast<Quantity>(12U) * shape.physical_channels;
    const std::array inputs{InputRequirement{"noise_calibration", preflight.noise.charged_bytes}};
    const auto status = add(kNoiseEstimateNodeId, inputs, "noise_model", noise_model_bytes.value(), scratch);
    if (!status.ok())
      return status;
  }
  if (std::find(graph_inputs.estimator_node_ids.begin(), graph_inputs.estimator_node_ids.end(), kPhaseEstimateNodeId) !=
      graph_inputs.estimator_node_ids.end()) {
    if (preflight.phase.charged_bytes == 0U) {
      return Status::InternalError("Cartesian RSS HDF5 graph enables phase estimation without a calibration ingress");
    }
    const std::array inputs{InputRequirement{"phase_reference", preflight.phase.charged_bytes}};
    const auto status = add(kPhaseEstimateNodeId, inputs, "phase_model", phase_model_bytes.value(), 0U);
    if (!status.ok())
      return status;
  }
  if (std::find(graph_inputs.estimator_node_ids.begin(), graph_inputs.estimator_node_ids.end(),
                kCoilBasisEstimateNodeId) != graph_inputs.estimator_node_ids.end()) {
    if (preflight.coil.charged_bytes == 0U) {
      return Status::InternalError(
        "Cartesian RSS HDF5 graph enables coil-basis estimation without a calibration ingress");
    }
    const Quantity scratch = static_cast<Quantity>(24U) * shape.physical_channels * shape.physical_channels +
                             static_cast<Quantity>(4U) * shape.physical_channels;
    const std::array inputs{InputRequirement{"kspace", preflight.coil.charged_bytes}};
    const auto status = add(kCoilBasisEstimateNodeId, inputs, "coil_basis", coil_basis_bytes.value(), scratch);
    if (!status.ok())
      return status;
  }

  Quantity current_kspace_bytes = shape.raw_kspace_charged_bytes;
  for (const auto& node_id : graph_inputs.data_node_ids) {
    Status status = Status::Ok();
    if (node_id == kNoisePrewhitenNodeId) {
      const std::array inputs{InputRequirement{"kspace", current_kspace_bytes},
                              InputRequirement{"noise_model", noise_model_bytes.value()}};
      status = add(node_id, inputs, "prewhitened_kspace", shape.raw_kspace_charged_bytes, 0U);
      current_kspace_bytes = shape.raw_kspace_charged_bytes;
    } else if (node_id == kPhaseCorrectNodeId) {
      const std::array inputs{InputRequirement{"kspace", current_kspace_bytes},
                              InputRequirement{"phase_model", phase_model_bytes.value()}};
      status = add(node_id, inputs, "phase_corrected_kspace", shape.raw_kspace_charged_bytes, 0U);
      current_kspace_bytes = shape.raw_kspace_charged_bytes;
    } else if (node_id == kCoilCompressNodeId) {
      const std::array inputs{InputRequirement{"kspace", current_kspace_bytes},
                              InputRequirement{"coil_basis", coil_basis_bytes.value()}};
      status = add(node_id, inputs, "compressed_kspace", shape.compressed_kspace_charged_bytes, 0U);
      current_kspace_bytes = shape.compressed_kspace_charged_bytes;
    } else if (node_id == kReadoutCropNodeId) {
      const std::array inputs{InputRequirement{"kspace", current_kspace_bytes}};
      status = add(node_id, inputs, "cropped_kspace", shape.kspace_charged_bytes, 0U);
      current_kspace_bytes = shape.kspace_charged_bytes;
    } else if (node_id == kReconstructNodeId) {
      if (current_kspace_bytes != shape.kspace_charged_bytes) {
        return Status::InternalError(
          "Cartesian RSS HDF5 graph reaches Cartesian reconstruction with a raw readout shape but no crop node");
      }
      const std::array inputs{InputRequirement{"kspace", current_kspace_bytes}};
      status = add(node_id, inputs, "coil_images", shape.coil_image_charged_bytes, shape.cartesian_scratch_bytes);
    } else if (node_id == kCombineNodeId) {
      const std::array inputs{InputRequirement{"coil_images", shape.coil_image_charged_bytes}};
      status = add(node_id, inputs, "image", shape.image_charged_bytes, 0U);
    } else {
      return Status::InternalError("Cartesian RSS HDF5 graph has an unknown data node '" + node_id + "'");
    }
    if (!status.ok())
      return status;
  }
  if (result.contracts.size() != graph_inputs.nodes.size() || result.requirements.size() != graph_inputs.nodes.size()) {
    return Status::InternalError("Cartesian RSS HDF5 graph planning bindings do not exactly cover its nodes");
  }
  return result;
}

[[nodiscard]] Result<PlanningArtifacts> make_planning_artifacts(const Preflight& preflight, const Quantity node_count) {
  if (node_count < 2U || node_count > 9U) {
    return Status::InternalError("Cartesian RSS HDF5 graph has an invalid bounded node count");
  }
  const auto& shape = preflight.shape;
  Quantity budget_base{0U};
  const auto add_budget = [&](const Quantity bytes, const std::string_view name) -> Status {
    auto total = checked_sum(budget_base, bytes, name);
    if (!total.ok())
      return total.status();
    budget_base = total.value();
    return Status::Ok();
  };
  const auto raw_stage_count = static_cast<Quantity>(1U + (preflight.noise.line_count != 0U ? 1U : 0U) +
                                                     (preflight.phase.line_count != 0U ? 1U : 0U));
  for (Quantity index = 0U; index < raw_stage_count; ++index) {
    const auto added = add_budget(shape.raw_kspace_charged_bytes, "machine budget raw k-space stage");
    if (!added.ok())
      return added;
  }
  if (preflight.coil.line_count != 0U) {
    const auto added =
      add_budget(shape.compressed_kspace_charged_bytes, "machine budget coil-compressed k-space stage");
    if (!added.ok())
      return added;
  }
  if (shape.input_cols != shape.cols) {
    const auto added = add_budget(shape.kspace_charged_bytes, "machine budget readout-cropped k-space stage");
    if (!added.ok())
      return added;
  }
  for (const auto [bytes, name] : std::array{
         std::pair{shape.coil_image_charged_bytes, std::string_view{"machine budget coil images"}},
         std::pair{shape.image_charged_bytes, std::string_view{"machine budget RSS image"}},
         std::pair{shape.cartesian_scratch_bytes, std::string_view{"machine budget Cartesian scratch"}},
       }) {
    const auto added = add_budget(bytes, name);
    if (!added.ok())
      return added;
  }

  Quantity calibration_horizon_items{0U};
  Quantity calibration_horizon_bytes{0U};
  const auto add_calibration = [&](const CalibrationLane& lane, const Quantity artifact_bytes,
                                   const Quantity scratch_bytes, const std::string_view name) -> Status {
    if (lane.line_count == 0U || lane.payload_bytes == 0U || lane.charged_bytes == 0U || artifact_bytes == 0U) {
      return Status::InternalError("Cartesian RSS HDF5 enabled calibration has no bounded lane capacity");
    }
    const auto artifact_charge = aligned_payload_capacity(artifact_bytes, kFramePayloadAlignmentBytes, name);
    if (!artifact_charge.ok())
      return artifact_charge.status();
    for (const auto bytes : {lane.charged_bytes, lane.charged_bytes, artifact_charge.value(), scratch_bytes}) {
      const auto added = add_budget(bytes, name);
      if (!added.ok())
        return added;
    }
    auto horizon = checked_sum(calibration_horizon_bytes, artifact_charge.value(), "calibration artifact horizon");
    if (!horizon.ok())
      return horizon.status();
    calibration_horizon_bytes = horizon.value();
    ++calibration_horizon_items;
    return Status::Ok();
  };
  if (preflight.noise.line_count != 0U) {
    auto elements = checked_product(shape.physical_channels, shape.physical_channels, "noise model elements");
    if (!elements.ok())
      return elements.status();
    auto bytes = checked_product(elements.value(), sizeof(std::complex<float>), "noise model bytes");
    if (!bytes.ok())
      return bytes.status();
    const auto scratch = static_cast<Quantity>(24U) * shape.physical_channels * shape.physical_channels +
                         static_cast<Quantity>(12U) * shape.physical_channels;
    const auto added = add_calibration(preflight.noise, bytes.value(), scratch, "machine budget noise calibration");
    if (!added.ok())
      return added;
  }
  if (preflight.phase.line_count != 0U) {
    auto elements = checked_product(shape.physical_channels, shape.input_cols, "phase model elements");
    if (!elements.ok())
      return elements.status();
    auto bytes = checked_product(elements.value(), sizeof(std::complex<float>), "phase model bytes");
    if (!bytes.ok())
      return bytes.status();
    const auto added = add_calibration(preflight.phase, bytes.value(), 0U, "machine budget phase calibration");
    if (!added.ok())
      return added;
  }
  if (preflight.coil.line_count != 0U) {
    auto elements = checked_product(shape.channels, shape.physical_channels, "coil basis elements");
    if (!elements.ok())
      return elements.status();
    auto bytes = checked_product(elements.value(), sizeof(std::complex<float>), "coil basis bytes");
    if (!bytes.ok())
      return bytes.status();
    const auto scratch = static_cast<Quantity>(24U) * shape.physical_channels * shape.physical_channels +
                         static_cast<Quantity>(4U) * shape.physical_channels;
    const auto added = add_calibration(preflight.coil, bytes.value(), scratch, "machine budget coil calibration");
    if (!added.ok())
      return added;
  }
  auto doubled = checked_product(budget_base, 2U, "machine budget double-buffer allowance");
  if (!doubled.ok())
    return doubled.status();
  auto host_budget = checked_sum(doubled.value(), kMachineBudgetHeadroomBytes, "machine budget headroom");
  if (!host_budget.ok())
    return host_budget.status();

  auto target = TargetEnvelope::create(
    {.max_xml_bytes = preflight.scan_descriptor.source_xml_bytes(),
     .max_frame_charged_bytes =
       std::max({shape.raw_kspace_charged_bytes, shape.compressed_kspace_charged_bytes, shape.kspace_charged_bytes}),
     .max_image_charged_bytes = shape.image_charged_bytes,
     .max_decoder_staging_bytes = shape.line_bytes,
     .max_samples_per_acquisition = shape.input_cols,
     .max_trajectory_dimensions = 0U,
     .max_active_channels = shape.physical_channels,
     .max_channel_groups = 1U,
     .max_dynamic_keys_per_scan = 1U,
     .max_active_scans = 1U,
     .calibration_horizon_items = calibration_horizon_items,
     .calibration_horizon_charged_bytes = calibration_horizon_bytes,
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
                                                                         .cpu_leaf_permits = node_count,
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
    "},"
    "\"calibration_horizon_charged_bytes\":" +
    std::to_string(calibration_horizon_bytes) +
    ",\"calibration_horizon_items\":" + std::to_string(calibration_horizon_items) +
    ","
    "\"max_active_channels\":" +
    std::to_string(shape.physical_channels) +
    ",\"max_active_scans\":1,"
    "\"max_channel_groups\":1,\"max_decoder_staging_bytes\":" +
    std::to_string(shape.line_bytes) + ",\"max_dynamic_keys_per_scan\":1,\"max_frame_charged_bytes\":" +
    std::to_string(
      std::max({shape.raw_kspace_charged_bytes, shape.compressed_kspace_charged_bytes, shape.kspace_charged_bytes})) +
    ",\"max_image_charged_bytes\":" + std::to_string(shape.image_charged_bytes) +
    ",\"max_samples_per_acquisition\":" + std::to_string(shape.input_cols) +
    ",\"max_trajectory_dimensions\":0,\"max_xml_bytes\":" +
    std::to_string(preflight.scan_descriptor.source_xml_bytes()) +
    ",\"sink\":{\"max_pause_us\":0,\"minimum_drain_items_per_second\":1,\"slow_sink_policy\":\"fail\","
    "\"transport_staging_bytes\":" +
    std::to_string(shape.image_charged_bytes) + "}}";
  auto target_digest = canonical_digest("kspacejet:artifact:cartesian-rss-hdf5-target-envelope", target_document,
                                        "Cartesian RSS HDF5 target envelope input");
  if (!target_digest.ok())
    return target_digest.status();
  const auto machine_document =
    "{\"allowed_memory_domains\":[\"host\"],\"allowed_profiles\":[\"offline-reference\"],"
    "\"host_total_cap_bytes\":" +
    std::to_string(host_budget.value()) +
    ",\"numa_domain_count\":1,\"resources\":{\"async_token_count\":0,\"backend_gang_permits\":0,"
    "\"cpu_leaf_permits\":" +
    std::to_string(node_count) +
    ",\"descriptor_count\":1024,\"host_hugepage_bytes\":0,"
    "\"host_normal_bytes\":" +
    std::to_string(host_budget.value()) +
    ",\"host_pinned_bytes\":0,\"io_slots\":0,\"provider_private_permits\":0,\"shared_host_bytes\":0,"
    "\"spool_bytes\":0,\"transport_bytes\":0},\"scheduler_policy\":\"fifo\"}";
  auto machine_digest = canonical_digest("kspacejet:artifact:cartesian-rss-hdf5-machine-policy", machine_document,
                                         "Cartesian RSS HDF5 machine policy input");
  if (!machine_digest.ok())
    return machine_digest.status();

  return PlanningArtifacts{.target_envelope = std::move(target).value(),
                           .machine_policy = std::move(policy).value(),
                           .digests = {.scan_descriptor = preflight.normalized_scan_facts_digest,
                                       .target_envelope = std::move(target_digest).value(),
                                       .machine_policy = std::move(machine_digest).value()}};
}

struct DeclaredCartesianGeometry final {
  std::uint32_t rows{0U};
  std::uint32_t input_cols{0U};
  std::uint32_t cols{0U};
};

[[nodiscard]] Result<DeclaredCartesianGeometry>
derive_declared_cartesian_geometry(const ScanDescriptor& descriptor,
                                   const CartesianRssHdf5ReconstructionConfig& config) {
  if (descriptor.encodings().size() != 1U) {
    return Status::ValidationError("Cartesian RSS HDF5 requires exactly one ISMRMRD encoding");
  }
  const auto& encoding = descriptor.encodings().front();
  const auto& encoded = encoding.encoded_matrix();
  const auto& reconstructed = encoding.recon_matrix();
  if (encoding.trajectory() != TrajectoryType::cartesian || encoded.z != 1U || reconstructed.z != 1U ||
      encoded.y != reconstructed.y || encoded.x < kMinimumDimension || encoded.x > kMaximumDimension ||
      encoded.y < kMinimumDimension || encoded.y > kMaximumDimension || reconstructed.x < kMinimumDimension ||
      reconstructed.x > kMaximumDimension) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 XML must declare one bounded 2-D Cartesian encoded/reconstruction matrix");
  }
  if (!config.readout_oversampling_removal.has_value() && encoded.x != reconstructed.x) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 requires matching encoded/reconstruction readout matrices unless readout crop is enabled");
  }
  if (config.readout_oversampling_removal.has_value()) {
    const auto offset = config.readout_oversampling_removal->readout_offset;
    if (encoded.x < reconstructed.x || offset > encoded.x - reconstructed.x) {
      return Status::ValidationError(
        "Cartesian RSS HDF5 readout crop offset/output does not fit the XML encoded and reconstruction matrices");
    }
  }
  const auto& ky_limit = encoding.limits().at(EncodingLimitDimension::kspace_encode_step_1);
  if (ky_limit.has_value() && (ky_limit->minimum() != 0U || ky_limit->maximum() != encoded.y - 1U)) {
    return Status::ValidationError("Cartesian RSS HDF5 XML kspace_encode_step_1 limit does not match its matrix rows");
  }
  return DeclaredCartesianGeometry{.rows = static_cast<std::uint32_t>(encoded.y),
                                   .input_cols = static_cast<std::uint32_t>(encoded.x),
                                   .cols = static_cast<std::uint32_t>(reconstructed.x)};
}

[[nodiscard]] Status validate_declared_channels(const ScanDescriptor& descriptor, const std::uint32_t channels) {
  if (descriptor.declared_receiver_channels().has_value() &&
      descriptor.declared_receiver_channels().value() < channels) {
    return Status::ValidationError("Cartesian RSS HDF5 receiverChannels is smaller than acquisition active_channels");
  }
  return Status::Ok();
}

enum class AcquisitionLane : std::uint8_t {
  imaging,
  noise,
  phase_reference,
  coil_calibration,
  coil_calibration_and_imaging,
};

[[nodiscard]] constexpr std::uint64_t acquisition_flag_bit(const ISMRMRD::ISMRMRD_AcquisitionFlags flag) noexcept {
  return UINT64_C(1) << (static_cast<std::uint64_t>(flag) - 1U);
}

[[nodiscard]] Result<AcquisitionLane> classify_acquisition_lane(const std::uint64_t flags,
                                                                const CartesianRssHdf5ReconstructionConfig& config) {
  if (flags == 0U) {
    return AcquisitionLane::imaging;
  }
  if (flags == acquisition_flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT)) {
    if (!config.noise_prewhiten.has_value()) {
      return Status::ValidationError("Cartesian RSS HDF5 received noise calibration but noise_prewhiten is disabled");
    }
    return AcquisitionLane::noise;
  }
  if (flags == acquisition_flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PHASECORR_DATA)) {
    if (!config.phase_correction.has_value()) {
      return Status::ValidationError("Cartesian RSS HDF5 received phase reference but phase_correction is disabled");
    }
    return AcquisitionLane::phase_reference;
  }
  if (flags == acquisition_flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION)) {
    if (!config.coil_compression.has_value()) {
      return Status::ValidationError(
        "Cartesian RSS HDF5 received parallel calibration but coil_compression is disabled");
    }
    return AcquisitionLane::coil_calibration;
  }
  if (flags == acquisition_flag_bit(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING)) {
    if (!config.coil_compression.has_value()) {
      return Status::ValidationError(
        "Cartesian RSS HDF5 received combined parallel-calibration/imaging data but coil_compression is disabled");
    }
    return AcquisitionLane::coil_calibration_and_imaging;
  }
  return Status::ValidationError("Cartesian RSS HDF5 accepts only unflagged imaging and exactly one enabled noise, "
                                 "phase, or parallel-calibration flag");
}

[[nodiscard]] Result<AcquisitionLane> validate_acquisition(const ksj::ismrmrd::AcquisitionView& acquisition,
                                                           const DeclaredCartesianGeometry& declared_geometry,
                                                           const CartesianRssHdf5ReconstructionConfig& config,
                                                           std::optional<std::uint32_t>& physical_channels) {
  const auto& header = acquisition.header;
  auto lane = classify_acquisition_lane(header.flags, config);
  if (!lane.ok())
    return lane.status();
  if (header.trajectory_dimensions != 0U || !acquisition.trajectory.empty() || header.discard_pre != 0U ||
      header.discard_post != 0U) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 accepts no trajectory or discarded samples on imaging or enabled calibration acquisitions");
  }
  if (header.encoding_space_ref != 0U || header.index.kspace_encode_step_2 != 0U || header.index.average != 0U ||
      header.index.slice != 0U || header.index.contrast != 0U || header.index.phase != 0U ||
      header.index.repetition != 0U || header.index.set != 0U || header.index.segment != 0U) {
    return Status::ValidationError("Cartesian RSS HDF5 accepts exactly one 2-D semantic frame");
  }
  if (header.number_of_samples < kMinimumDimension || header.number_of_samples > kMaximumDimension ||
      header.active_channels == 0U || header.active_channels > kMaximumChannels ||
      header.available_channels < header.active_channels) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 acquisition samples/channels do not describe a supported full active-channel frame");
  }
  if (header.number_of_samples != declared_geometry.input_cols) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 acquisition number_of_samples does not match the XML Cartesian readout matrix");
  }
  const auto expected_samples = static_cast<std::uint64_t>(header.number_of_samples) * header.active_channels;
  if (expected_samples != acquisition.samples.size()) {
    return Status::ValidationError("Cartesian RSS HDF5 acquisition payload does not match samples * active_channels");
  }
  if (!physical_channels.has_value()) {
    physical_channels = header.active_channels;
  } else if (*physical_channels != header.active_channels) {
    return Status::ValidationError("Cartesian RSS HDF5 acquisition active_channels changes within one frame");
  }
  for (const auto value : acquisition.samples) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
      return Status::ValidationError("Cartesian RSS HDF5 rejects non-finite raw complex samples");
    }
  }
  return lane.value();
}

[[nodiscard]] Result<CalibrationLane> make_calibration_lane(const std::uint32_t line_count, const Shape& shape,
                                                            const std::string_view lane_name) {
  if (line_count == 0U) {
    return CalibrationLane{};
  }
  auto bytes = checked_product(line_count, shape.line_bytes, std::string(lane_name) + " payload bytes");
  if (!bytes.ok())
    return bytes.status();
  if (bytes.value() > kMaximumCalibrationInputBytes) {
    return Status::ValidationError("Cartesian RSS HDF5 " + std::string(lane_name) +
                                   " exceeds the Calibration Provider 64 MiB input bound");
  }
  auto charged = aligned_payload_capacity(bytes.value(), kFramePayloadAlignmentBytes,
                                          std::string(lane_name) + " ingress pool capacity");
  if (!charged.ok())
    return charged.status();
  return CalibrationLane{.line_count = line_count, .payload_bytes = bytes.value(), .charged_bytes = charged.value()};
}

[[nodiscard]] Result<Preflight> preflight_input(const CartesianRssHdf5ReconstructionConfig& config) {
  ksj::ismrmrd::DatasetReader reader;
  std::string reader_error;
  if (!reader.open(config.input_file, config.dataset_group, reader_error))
    return hdf5_io_error(reader_error);
  if (reader.metadata().xml_header.empty()) {
    return Status::ValidationError("Cartesian RSS HDF5 requires an ISMRMRD XML header");
  }
  auto scan = ScanDescriptor::parse_ismrmrd_xml(reader.metadata().xml_header);
  if (!scan.ok())
    return scan.status();
  auto geometry = derive_declared_cartesian_geometry(scan.value(), config);
  if (!geometry.ok())
    return geometry.status();

  std::vector<bool> seen_lines(geometry.value().rows, false);
  std::optional<std::uint32_t> physical_channels;
  std::uint32_t acquisitions{0U};
  std::uint32_t noise_lines{0U};
  std::uint32_t phase_lines{0U};
  std::uint32_t coil_lines{0U};
  Status callback_status = Status::Ok();
  const auto iteration = reader.for_each_acquisition(
    [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
      const auto lane = validate_acquisition(acquisition, geometry.value(), config, physical_channels);
      if (!lane.ok()) {
        callback_status = lane.status();
        return false;
      }
      if (lane.value() == AcquisitionLane::imaging || lane.value() == AcquisitionLane::coil_calibration_and_imaging) {
        if (acquisition.header.index.kspace_encode_step_1 >= geometry.value().rows) {
          callback_status =
            Status::ValidationError("Cartesian RSS HDF5 ky coordinate is outside the XML Cartesian matrix");
          return false;
        }
        const auto ky = static_cast<std::size_t>(acquisition.header.index.kspace_encode_step_1);
        if (seen_lines[ky]) {
          callback_status = Status::ValidationError("Cartesian RSS HDF5 received a duplicate imaging ky acquisition");
          return false;
        }
        seen_lines[ky] = true;
      }
      const auto increment = [&](std::uint32_t& count, const std::string_view lane_name) -> bool {
        if (count == std::numeric_limits<std::uint32_t>::max()) {
          callback_status = Status::ValidationError("Cartesian RSS HDF5 " + std::string(lane_name) +
                                                    " acquisition count overflows uint32");
          return false;
        }
        ++count;
        return true;
      };
      if (lane.value() == AcquisitionLane::noise && !increment(noise_lines, "noise calibration"))
        return false;
      if (lane.value() == AcquisitionLane::phase_reference && !increment(phase_lines, "phase reference"))
        return false;
      if ((lane.value() == AcquisitionLane::coil_calibration ||
           lane.value() == AcquisitionLane::coil_calibration_and_imaging) &&
          !increment(coil_lines, "coil calibration")) {
        return false;
      }
      if (acquisitions == std::numeric_limits<std::uint32_t>::max()) {
        callback_status = Status::ValidationError("Cartesian RSS HDF5 acquisition count overflows uint32");
        return false;
      }
      ++acquisitions;
      return true;
    },
    reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::failed)
    return hdf5_io_error(reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::stopped) {
    return callback_status.ok() ? Status::Unavailable("Cartesian RSS HDF5 preflight stopped before EndOfInput")
                                : callback_status;
  }
  if (!callback_status.ok())
    return callback_status;
  if (!physical_channels.has_value() || std::find(seen_lines.begin(), seen_lines.end(), false) != seen_lines.end()) {
    return Status::ValidationError("Cartesian RSS HDF5 requires every ky line exactly once in one full frame");
  }
  if (config.noise_prewhiten.has_value() && noise_lines == 0U) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 noise_prewhiten requires at least one noise calibration acquisition");
  }
  if (config.phase_correction.has_value() && phase_lines == 0U) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 phase_correction requires at least one phase-reference acquisition");
  }
  if (config.coil_compression.has_value() && coil_lines == 0U) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 coil_compression requires at least one parallel calibration acquisition");
  }
  const auto final_channels =
    config.coil_compression.has_value() ? config.coil_compression->virtual_channel_count : *physical_channels;
  if (final_channels > *physical_channels) {
    return Status::ValidationError(
      "Cartesian RSS HDF5 coil_compression virtual_channel_count exceeds acquisition physical channels");
  }
  auto shape = make_shape(geometry.value().rows, geometry.value().input_cols, geometry.value().cols, *physical_channels,
                          final_channels);
  if (!shape.ok())
    return shape.status();
  const auto declared_channels = validate_declared_channels(scan.value(), shape.value().physical_channels);
  if (!declared_channels.ok())
    return declared_channels;
  auto noise = make_calibration_lane(noise_lines, shape.value(), "noise calibration");
  if (!noise.ok())
    return noise.status();
  auto phase = make_calibration_lane(phase_lines, shape.value(), "phase reference");
  if (!phase.ok())
    return phase.status();
  auto coil = make_calibration_lane(coil_lines, shape.value(), "coil calibration");
  if (!coil.ok())
    return coil.status();

  const auto xml_document = "{\"ismrmrd_xml\":" + json_string(reader.metadata().xml_header) + "}";
  auto xml_digest = canonical_digest("kspacejet:artifact:cartesian-rss-hdf5-source-xml", xml_document,
                                     "Cartesian RSS HDF5 source XML input");
  if (!xml_digest.ok())
    return xml_digest.status();
  const auto facts_document =
    "{\"acquisitions\":" + std::to_string(acquisitions) + ",\"coil_calibration_lines\":" + std::to_string(coil_lines) +
    ",\"final_channels\":" + std::to_string(shape.value().channels) +
    ",\"input_cols\":" + std::to_string(shape.value().input_cols) + ",\"noise_lines\":" + std::to_string(noise_lines) +
    ",\"phase_lines\":" + std::to_string(phase_lines) +
    ",\"physical_channels\":" + std::to_string(shape.value().physical_channels) +
    ",\"reconstruction_cols\":" + std::to_string(shape.value().cols) +
    ",\"rows\":" + std::to_string(shape.value().rows) +
    ",\"source_xml_digest\":" + json_string(xml_digest.value().value()) + "}";
  auto facts_digest = canonical_digest("kspacejet:artifact:cartesian-rss-hdf5-normalized-scan-facts", facts_document,
                                       "Cartesian RSS HDF5 normalized scan facts");
  if (!facts_digest.ok())
    return facts_digest.status();
  return Preflight{.scan_descriptor = std::move(scan).value(),
                   .shape = std::move(shape).value(),
                   .acquisitions_read = acquisitions,
                   .noise = std::move(noise).value(),
                   .phase = std::move(phase).value(),
                   .coil = std::move(coil).value(),
                   .source_xml_digest = std::move(xml_digest).value(),
                   .normalized_scan_facts_digest = std::move(facts_digest).value()};
}

[[nodiscard]] Result<HostFrameAssemblerConfig> make_host_config(const Shape& shape, const std::uint32_t line_count,
                                                                const std::uint32_t slot_id,
                                                                const std::string_view scan_instance_id) {
  if (line_count == 0U) {
    return Status::InternalError("Cartesian RSS HDF5 attempted to create an empty host ingress lane");
  }
  try {
    CartesianFrameSlotConfig slot{
      .slot_id = slot_id,
      .dimensions = {.readout_samples = shape.input_cols,
                     .phase_encode_1 = line_count,
                     .phase_encode_2 = 1U,
                     .channels = shape.physical_channels,
                     .bytes_per_sample = static_cast<std::uint32_t>(sizeof(std::complex<float>))},
      .completion = {},
      .resource_upper_bound = {.max_total_arrivals = line_count,
                               .max_duplicate_arrivals = 0U,
                               .max_payload_bytes = shape.line_bytes},
      .duplicate_policy = DuplicateAcquisitionPolicy::reject,
      .incomplete_policy = IncompleteFramePolicy::fail,
    };
    slot.completion.required_indices.reserve(line_count);
    for (std::uint32_t ky = 0U; ky < line_count; ++ky) {
      slot.completion.required_indices.push_back({.phase_encode_1 = ky, .phase_encode_2 = 0U});
    }
    return HostFrameAssemblerConfig{.scan_instance_id = std::string(scan_instance_id),
                                    .frame_slots = {std::move(slot)}};
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory("unable to allocate Cartesian RSS HDF5 frame coverage");
  }
}

struct ReplayLane final {
  HostFrameAssembler* host{nullptr};
  CompletedFrameIngressBridge* bridge{nullptr};
  std::uint32_t expected_lines{0U};
  std::uint32_t next_line{0U};
  std::optional<FrameAssemblyLease> assembly;
};

struct ReplayLanes final {
  ReplayLane imaging;
  std::optional<ReplayLane> noise;
  std::optional<ReplayLane> phase;
  std::optional<ReplayLane> coil;
};

[[nodiscard]] Status begin_replay_lane(ReplayLane& lane, const FrameSlotContext& context) {
  if (lane.host == nullptr || lane.bridge == nullptr || lane.expected_lines == 0U || lane.assembly.has_value()) {
    return Status::InternalError("Cartesian RSS HDF5 replay lane is not initialized");
  }
  auto assembly = lane.host->try_begin_frame(context);
  if (!assembly.ok())
    return assembly.status();
  lane.assembly.emplace(std::move(assembly).value());
  return Status::Ok();
}

[[nodiscard]] Status scatter_replay_lane(ReplayLane& lane, const ksj::base::ConstByteSpan payload,
                                         const std::string_view lane_name) {
  if (!lane.assembly.has_value() || lane.next_line >= lane.expected_lines) {
    return Status::ValidationError("Cartesian RSS HDF5 replay " + std::string(lane_name) +
                                   " lane differs from preflight coverage");
  }
  const auto scatter = lane.assembly->scatter({.phase_encode_1 = lane.next_line, .phase_encode_2 = 0U}, payload);
  if (!scatter.ok())
    return scatter;
  ++lane.next_line;
  return Status::Ok();
}

[[nodiscard]] Status scatter_imaging_replay_lane(ReplayLane& lane, const std::uint32_t ky,
                                                 const ksj::base::ConstByteSpan payload) {
  if (!lane.assembly.has_value() || ky >= lane.expected_lines || lane.next_line >= lane.expected_lines) {
    return Status::ValidationError("Cartesian RSS HDF5 replay imaging ky differs from preflight coverage");
  }
  const auto scatter = lane.assembly->scatter({.phase_encode_1 = ky, .phase_encode_2 = 0U}, payload);
  if (!scatter.ok())
    return scatter;
  ++lane.next_line;
  return Status::Ok();
}

[[nodiscard]] Status seal_publish_and_close_replay_lane(ReplayLane& lane, const FrameSlotContext& context) {
  if (lane.host == nullptr || lane.bridge == nullptr || !lane.assembly.has_value() ||
      lane.next_line != lane.expected_lines) {
    return Status::ValidationError("Cartesian RSS HDF5 replay lane did not receive its exact preflight line count");
  }
  auto complete = std::move(*lane.assembly).seal_complete();
  lane.assembly.reset();
  if (!complete.ok())
    return complete.status();
  const auto published = lane.bridge->publish(std::move(complete).value(), make_data_item_identity(context, 0U));
  if (!published.ok())
    return published;
  return lane.bridge->end_of_input();
}

void abort_replay_lanes(ReplayLanes& lanes) noexcept {
  const auto abort = [](ReplayLane& lane) noexcept {
    if (lane.bridge != nullptr) {
      static_cast<void>(lane.bridge->abort());
    } else if (lane.host != nullptr) {
      static_cast<void>(lane.host->abort());
    }
  };
  abort(lanes.imaging);
  if (lanes.noise.has_value())
    abort(*lanes.noise);
  if (lanes.phase.has_value())
    abort(*lanes.phase);
  if (lanes.coil.has_value())
    abort(*lanes.coil);
}

[[nodiscard]] Status replay_into_hosts(const CartesianRssHdf5ReconstructionConfig& config, const Preflight& preflight,
                                       ReplayLanes& lanes) {
  const FrameSlotContext context{};
  for (ReplayLane* lane : {&lanes.imaging, lanes.noise ? &*lanes.noise : nullptr, lanes.phase ? &*lanes.phase : nullptr,
                           lanes.coil ? &*lanes.coil : nullptr}) {
    if (lane == nullptr)
      continue;
    const auto begun = begin_replay_lane(*lane, context);
    if (!begun.ok()) {
      abort_replay_lanes(lanes);
      return begun;
    }
  }

  ksj::ismrmrd::DatasetReader reader;
  std::string reader_error;
  if (!reader.open(config.input_file, config.dataset_group, reader_error)) {
    abort_replay_lanes(lanes);
    return hdf5_io_error(reader_error);
  }
  const auto replay_xml_document = "{\"ismrmrd_xml\":" + json_string(reader.metadata().xml_header) + "}";
  auto replay_xml_digest = canonical_digest("kspacejet:artifact:cartesian-rss-hdf5-source-xml", replay_xml_document,
                                            "Cartesian RSS HDF5 replay XML input");
  if (!replay_xml_digest.ok()) {
    abort_replay_lanes(lanes);
    return replay_xml_digest.status();
  }
  if (replay_xml_digest.value() != preflight.source_xml_digest) {
    abort_replay_lanes(lanes);
    return Status::ValidationError("Cartesian RSS HDF5 XML changed between preflight and replay");
  }
  std::uint32_t acquisitions{0U};
  Status callback_status = Status::Ok();
  const DeclaredCartesianGeometry declared_geometry{
    .rows = preflight.shape.rows, .input_cols = preflight.shape.input_cols, .cols = preflight.shape.cols};
  std::optional<std::uint32_t> expected_channels{preflight.shape.physical_channels};
  std::vector<bool> replay_coverage(preflight.shape.rows, false);
  const auto iteration = reader.for_each_acquisition(
    [&](const ksj::ismrmrd::AcquisitionView& acquisition) {
      const auto lane = validate_acquisition(acquisition, declared_geometry, config, expected_channels);
      if (!lane.ok()) {
        callback_status = lane.status();
        return false;
      }
      const auto sample_bytes = std::as_bytes(acquisition.samples);
      const auto payload = ksj::base::ConstByteSpan{sample_bytes.data(), sample_bytes.size()};
      const auto scatter_imaging = [&]() -> Status {
        if (acquisition.header.index.kspace_encode_step_1 >= preflight.shape.rows) {
          return Status::ValidationError("Cartesian RSS HDF5 replay imaging ky is outside the frozen matrix");
        }
        const auto ky = static_cast<std::size_t>(acquisition.header.index.kspace_encode_step_1);
        if (replay_coverage[ky]) {
          return Status::ValidationError("Cartesian RSS HDF5 replay received a duplicate imaging ky acquisition");
        }
        replay_coverage[ky] = true;
        return scatter_imaging_replay_lane(lanes.imaging, static_cast<std::uint32_t>(ky), payload);
      };
      Status scatter = Status::Ok();
      if (lane.value() == AcquisitionLane::imaging || lane.value() == AcquisitionLane::coil_calibration_and_imaging) {
        scatter = scatter_imaging();
      }
      if (scatter.ok() && lane.value() == AcquisitionLane::noise && lanes.noise.has_value()) {
        scatter = scatter_replay_lane(*lanes.noise, payload, "noise calibration");
      }
      if (scatter.ok() && lane.value() == AcquisitionLane::phase_reference && lanes.phase.has_value()) {
        scatter = scatter_replay_lane(*lanes.phase, payload, "phase reference");
      }
      if (scatter.ok() &&
          (lane.value() == AcquisitionLane::coil_calibration ||
           lane.value() == AcquisitionLane::coil_calibration_and_imaging) &&
          lanes.coil.has_value()) {
        scatter = scatter_replay_lane(*lanes.coil, payload, "coil calibration");
      }
      if (!scatter.ok()) {
        callback_status = scatter;
        return false;
      }
      if (acquisitions == std::numeric_limits<std::uint32_t>::max()) {
        callback_status = Status::ValidationError("Cartesian RSS HDF5 replay acquisition count overflows uint32");
        return false;
      }
      ++acquisitions;
      return true;
    },
    reader_error);
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::failed) {
    abort_replay_lanes(lanes);
    return hdf5_io_error(reader_error);
  }
  if (iteration == ksj::ismrmrd::AcquisitionIterationResult::stopped || !callback_status.ok()) {
    abort_replay_lanes(lanes);
    return callback_status.ok() ? Status::Unavailable("Cartesian RSS HDF5 replay stopped before EndOfInput")
                                : callback_status;
  }
  if (acquisitions != preflight.acquisitions_read ||
      std::find(replay_coverage.begin(), replay_coverage.end(), false) != replay_coverage.end()) {
    abort_replay_lanes(lanes);
    return Status::ValidationError("Cartesian RSS HDF5 input changed between preflight and replay");
  }
  for (ReplayLane* lane : {lanes.noise ? &*lanes.noise : nullptr, lanes.phase ? &*lanes.phase : nullptr,
                           lanes.coil ? &*lanes.coil : nullptr, &lanes.imaging}) {
    if (lane == nullptr)
      continue;
    const auto sealed = seal_publish_and_close_replay_lane(*lane, context);
    if (!sealed.ok()) {
      abort_replay_lanes(lanes);
      return sealed;
    }
  }
  return Status::Ok();
}

[[nodiscard]] std::vector<ksj::base::byte> semantic_key_bytes(const FrameSlotContext& context) {
  std::vector<ksj::base::byte> result(sizeof(std::uint16_t) * 7U);
  const std::array values{
    context.semantic_key.encoding_space, context.semantic_key.slice, context.semantic_key.contrast,
    context.semantic_key.repetition,     context.semantic_key.set,   context.semantic_key.phase,
    context.semantic_key.average};
  for (std::size_t index = 0U; index < values.size(); ++index) {
    result[index * 2U] = static_cast<ksj::base::byte>(values[index] & 0xFFU);
    result[index * 2U + 1U] = static_cast<ksj::base::byte>((values[index] >> 8U) & 0xFFU);
  }
  return result;
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
    return Status::InternalError("Cartesian RSS HDF5 resolved pipeline omitted a required canonical Provider config");
  }
  const FrameSlotContext context{};
  return ProviderNodeInstance::create(plan,
                                      {.module_path = module_path,
                                       .node_id = std::string(node_id),
                                       .canonical_config = node->canonical_config,
                                       .start_facts = {.normalized_scan_facts_digest = normalized_scan_facts_digest,
                                                       .execution_plan_digest = plan.digest(),
                                                       .run_id = "cartesian-rss-hdf5",
                                                       .scan_instance_id = "cartesian-rss-hdf5",
                                                       .terminal_epoch = kTerminalEpoch},
                                       .execution_context_id = execution_context_id,
                                       .resource_domain_id = 1U,
                                       .max_backend_concurrency = 1U,
                                       .numa_node = 0U,
                                       .device_ordinal = 0U,
                                       .key_state = {.semantic_key = semantic_key_bytes(context),
                                                     .placement_key = context.placement_key,
                                                     .generation = 1U,
                                                     .home_shard = 0U}});
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
    return Status::IoError("unable to open Cartesian RSS output image: " + path.string());
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  stream.flush();
  if (!stream)
    return Status::IoError("unable to write Cartesian RSS output image: " + path.string());
  return Status::Ok();
}

[[nodiscard]] Status write_metadata_file(const std::filesystem::path& path,
                                         const CartesianRssHdf5ReconstructionReport& report,
                                         const std::span<const RuntimeNode> nodes) {
  if (path.empty())
    return Status::Ok();
  if (nodes.empty()) {
    return Status::InternalError("Cartesian RSS HDF5 metadata requires the resolved graph nodes");
  }
  std::string operators;
  for (const auto& node : nodes) {
    if (!operators.empty())
      operators.push_back(',');
    operators += json_string(node.contract.operator_id());
  }
  const auto document =
    "{\"acquisitions_read\":" + std::to_string(report.acquisitions_read) +
    ",\"channels\":" + std::to_string(report.channels) + ",\"cols\":" + std::to_string(report.cols) +
    ",\"execution_plan_digest\":" + json_string(report.execution_plan_digest) +
    ",\"image_layout\":\"row_major_contiguous\",\"image_payload_bytes\":" + std::to_string(report.image_payload_bytes) +
    ",\"operators\":[" + operators + "],\"rows\":" + std::to_string(report.rows) +
    ",\"verification_record_digest\":" + json_string(report.verification_record_digest) + "}";
  auto canonical = graph::canonicalize_json(document);
  if (!canonical.ok())
    return canonical.status();
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open())
    return Status::IoError("unable to open Cartesian RSS metadata output: " + path.string());
  stream.write(canonical.value().data(), static_cast<std::streamsize>(canonical.value().size()));
  stream.put('\n');
  stream.flush();
  if (!stream)
    return Status::IoError("unable to write Cartesian RSS metadata output: " + path.string());
  return Status::Ok();
}

} // namespace

Result<CartesianRssHdf5ReconstructionReport>
reconstruct_cartesian_rss_hdf5(const CartesianRssHdf5ReconstructionConfig& config) {
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
    auto planning_bindings = make_cartesian_planning_bindings(graph_inputs.value(), preflight.value());
    if (!planning_bindings.ok())
      return planning_bindings.status();
    auto planning =
      make_planning_artifacts(preflight.value(), static_cast<Quantity>(graph_inputs.value().nodes.size()));
    if (!planning.ok())
      return planning.status();

    auto frozen_bindings = std::move(planning_bindings).value();

    const graph::PlanBuildRequest request{
      .resolved_pipeline = graph_inputs.value().pipeline,
      .requested_profile = ExecutionProfile::offline_reference,
      .scan_descriptor = preflight.value().scan_descriptor,
      .target_envelope = planning.value().target_envelope,
      .machine_policy = planning.value().machine_policy,
      .artifact_digests = planning.value().digests,
      .operator_contract_bindings = std::move(frozen_bindings.contracts),
      .node_planning_requirements = std::move(frozen_bindings.requirements),
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

    auto executor_instance = std::move(executor).value();
    struct ActiveProvider final {
      std::string node_id;
      std::unique_ptr<ProviderNodeInstance> instance;
    };
    std::vector<ActiveProvider> providers;
    providers.reserve(graph_inputs.value().nodes.size());
    std::uint64_t execution_context_id = 1U;
    for (const auto& node : graph_inputs.value().nodes) {
      auto provider =
        make_provider_node(compiled.value().plan, graph_inputs.value().pipeline, node.provider_module, node.node_id,
                           preflight.value().normalized_scan_facts_digest, execution_context_id);
      if (!provider.ok())
        return provider.status();
      providers.push_back({.node_id = node.node_id, .instance = std::move(provider).value()});
      ++execution_context_id;
    }
    const auto provider_for = [&providers](const std::string_view node_id) -> ProviderNodeInstance* {
      const auto found = std::find_if(providers.begin(), providers.end(), [node_id](const ActiveProvider& provider) {
        return provider.node_id == node_id;
      });
      return found == providers.end() ? nullptr : found->instance.get();
    };

    auto imaging_host_config =
      make_host_config(preflight.value().shape, preflight.value().shape.rows, 1U, "cartesian-rss-hdf5-imaging");
    if (!imaging_host_config.ok())
      return imaging_host_config.status();
    auto imaging_host_result = HostFrameAssembler::create(std::move(imaging_host_config).value());
    if (!imaging_host_result.ok())
      return imaging_host_result.status();
    auto imaging_host = std::move(imaging_host_result).value();
    auto imaging_bridge_result =
      CompletedFrameIngressBridge::create(*executor_instance, kKspaceIngressId, *imaging_host);
    if (!imaging_bridge_result.ok())
      return imaging_bridge_result.status();
    auto imaging_bridge = std::move(imaging_bridge_result).value();

    std::unique_ptr<HostFrameAssembler> noise_host;
    std::optional<CompletedFrameIngressBridge> noise_bridge;
    if (preflight.value().noise.line_count != 0U) {
      auto host_config =
        make_host_config(preflight.value().shape, preflight.value().noise.line_count, 2U, "cartesian-rss-hdf5-noise");
      if (!host_config.ok())
        return host_config.status();
      auto host = HostFrameAssembler::create(std::move(host_config).value());
      if (!host.ok())
        return host.status();
      noise_host = std::move(host).value();
      auto bridge = CompletedFrameIngressBridge::create(*executor_instance, kNoiseIngressId, *noise_host);
      if (!bridge.ok())
        return bridge.status();
      noise_bridge.emplace(std::move(bridge).value());
    }
    std::unique_ptr<HostFrameAssembler> phase_host;
    std::optional<CompletedFrameIngressBridge> phase_bridge;
    if (preflight.value().phase.line_count != 0U) {
      auto host_config =
        make_host_config(preflight.value().shape, preflight.value().phase.line_count, 3U, "cartesian-rss-hdf5-phase");
      if (!host_config.ok())
        return host_config.status();
      auto host = HostFrameAssembler::create(std::move(host_config).value());
      if (!host.ok())
        return host.status();
      phase_host = std::move(host).value();
      auto bridge = CompletedFrameIngressBridge::create(*executor_instance, kPhaseIngressId, *phase_host);
      if (!bridge.ok())
        return bridge.status();
      phase_bridge.emplace(std::move(bridge).value());
    }
    std::unique_ptr<HostFrameAssembler> coil_host;
    std::optional<CompletedFrameIngressBridge> coil_bridge;
    if (preflight.value().coil.line_count != 0U) {
      auto host_config = make_host_config(preflight.value().shape, preflight.value().coil.line_count, 4U,
                                          "cartesian-rss-hdf5-coil-calibration");
      if (!host_config.ok())
        return host_config.status();
      auto host = HostFrameAssembler::create(std::move(host_config).value());
      if (!host.ok())
        return host.status();
      coil_host = std::move(host).value();
      auto bridge = CompletedFrameIngressBridge::create(*executor_instance, kCoilCalibrationIngressId, *coil_host);
      if (!bridge.ok())
        return bridge.status();
      coil_bridge.emplace(std::move(bridge).value());
    }

    ReplayLanes replay_lanes{.imaging = {.host = imaging_host.get(),
                                         .bridge = &imaging_bridge,
                                         .expected_lines = preflight.value().shape.rows}};
    if (noise_host != nullptr && noise_bridge.has_value()) {
      replay_lanes.noise.emplace(ReplayLane{
        .host = noise_host.get(), .bridge = &*noise_bridge, .expected_lines = preflight.value().noise.line_count});
    }
    if (phase_host != nullptr && phase_bridge.has_value()) {
      replay_lanes.phase.emplace(ReplayLane{
        .host = phase_host.get(), .bridge = &*phase_bridge, .expected_lines = preflight.value().phase.line_count});
    }
    if (coil_host != nullptr && coil_bridge.has_value()) {
      replay_lanes.coil.emplace(ReplayLane{
        .host = coil_host.get(), .bridge = &*coil_bridge, .expected_lines = preflight.value().coil.line_count});
    }
    const auto replay = replay_into_hosts(config, preflight.value(), replay_lanes);
    if (!replay.ok()) {
      static_cast<void>(executor_instance->abort());
      return replay;
    }

    const auto fire_required = [&executor_instance, &provider_for](const std::string_view node_id,
                                                                   const std::uint64_t occurrence) -> Status {
      auto* provider = provider_for(node_id);
      if (provider == nullptr) {
        return Status::InternalError("Cartesian RSS HDF5 runtime omitted Provider node '" + std::string(node_id) + "'");
      }
      auto firing = fire_node(*executor_instance, *provider, node_id, occurrence);
      if (!firing.ok())
        return firing.status();
      if (firing.value().outcome != SynchronousFiringOutcome::done) {
        const auto failure = executor_instance->snapshot().last_error;
        return failure.ok() ? Status::StateError("Cartesian RSS HDF5 Provider '" + std::string(node_id) +
                                                 "' did not finish its normal frame firing")
                            : failure;
      }
      return Status::Ok();
    };
    const auto finish_required = [&executor_instance, &provider_for](const std::string_view node_id,
                                                                     const std::uint64_t occurrence) -> Status {
      auto* provider = provider_for(node_id);
      if (provider == nullptr) {
        return Status::InternalError("Cartesian RSS HDF5 runtime omitted Provider node '" + std::string(node_id) + "'");
      }
      return finish_node(*executor_instance, *provider, node_id, occurrence);
    };

    std::uint64_t resource_occurrence_id = 1U;
    for (const auto& node_id : graph_inputs.value().estimator_node_ids) {
      const auto fired = fire_required(node_id, resource_occurrence_id++);
      if (!fired.ok()) {
        static_cast<void>(executor_instance->abort());
        return fired;
      }
    }
    for (const auto& node_id : graph_inputs.value().estimator_node_ids) {
      const auto finished = finish_required(node_id, resource_occurrence_id++);
      if (!finished.ok()) {
        static_cast<void>(executor_instance->abort());
        return finished;
      }
    }
    for (const auto& node_id : graph_inputs.value().data_node_ids) {
      const auto fired = fire_required(node_id, resource_occurrence_id++);
      if (!fired.ok()) {
        static_cast<void>(executor_instance->abort());
        return fired;
      }
    }
    for (const auto& node_id : graph_inputs.value().data_node_ids) {
      const auto finished = finish_required(node_id, resource_occurrence_id++);
      if (!finished.ok()) {
        static_cast<void>(executor_instance->abort());
        return finished;
      }
    }

    auto image = executor_instance->try_acquire_egress(kImageEgressId);
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
      return Status::ValidationError("Cartesian RSS HDF5 egress did not contain one exact ksj.image-frame payload");
    }
    std::vector<ksj::base::byte> output(payload.value().begin(), payload.value().end());
    const auto acknowledged = image.value().acknowledge_consumed();
    if (!acknowledged.ok())
      return acknowledged;
    if (executor_instance->egress_poll_kind(kImageEgressId) != FixedBufferEdgePollKind::completed) {
      return Status::StateError("Cartesian RSS HDF5 egress did not close after its one expected image");
    }

    CartesianRssHdf5ReconstructionReport report{
      .rows = preflight.value().shape.rows,
      .cols = preflight.value().shape.cols,
      .channels = preflight.value().shape.channels,
      .acquisitions_read = preflight.value().acquisitions_read,
      .image_payload_bytes = preflight.value().shape.image_bytes,
      .execution_plan_digest = compiled.value().plan.digest().value(),
      .verification_record_digest = verification.value().digest().value(),
    };
    const auto image_write = write_binary_file(config.output_image_file, output);
    if (!image_write.ok())
      return image_write;
    const auto metadata_write = write_metadata_file(config.output_metadata_file, report, graph_inputs.value().nodes);
    if (!metadata_write.ok())
      return metadata_write;
    return report;
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory("Cartesian RSS HDF5 reconstruction exhausted host memory");
  } catch (const std::exception& exception) {
    return Status::InternalError("Cartesian RSS HDF5 reconstruction threw: " + std::string(exception.what()));
  } catch (...) {
    return Status::InternalError("Cartesian RSS HDF5 reconstruction threw an unknown exception");
  }
}

} // namespace ksj::recon::runtime
