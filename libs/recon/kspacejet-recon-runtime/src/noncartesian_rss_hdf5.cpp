#include "kspacejet/recon/runtime/noncartesian_rss_hdf5.hpp"
#include "kspacejet/recon/runtime/radial_rss_hdf5.hpp"

#include "kspacejet/provider/loader/provider_loader.hpp"
#include "kspacejet/recon/graph/execution_plan_compiler.hpp"
#include "kspacejet/recon/graph/operator_contract_json.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"
#include "kspacejet/recon/node_planning_requirements.hpp"
#include "kspacejet/recon/runtime/ismrmrd_hdf5_replay.hpp"
#include "kspacejet/recon/runtime/ismrmrd_semantic_ingress.hpp"
#include "kspacejet/recon/runtime/ismrmrd_image_artifact_sink.hpp"
#include "kspacejet/recon/runtime/provider_node_instance.hpp"
#include "kspacejet/recon/runtime/resource_vector_ledger.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_executor.hpp"
#include "kspacejet/recon/runtime/synchronous_graph_plan_storage.hpp"
#include "kspacejet/recon/scan_facts.hpp"
#include "kspacejet/recon/type_registry.hpp"

#include <algorithm>
#include <array>
#include <bit>
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
#include <numbers>
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
constexpr char kRadialGriddingOperatorId[] = "radial_gridding_reconstruct";
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
  Quantity reconstruction_scratch_bytes{0U};
  Quantity max_decoder_staging_bytes{0U};
  Quantity kspace_charged_bytes{0U};
  Quantity trajectory_charged_bytes{0U};
  Quantity coil_image_charged_bytes{0U};
  Quantity image_charged_bytes{0U};
};

enum class ReconstructionRoute {
  direct_adjoint,
  radial_gridding,
};

struct RouteConfig final {
  std::filesystem::path input_file;
  std::filesystem::path output_image_file;
  std::filesystem::path reconstruction_provider_module;
  std::filesystem::path coil_combine_provider_module;
  std::filesystem::path reconstruction_operator_contract;
  std::filesystem::path coil_combine_operator_contract;
  std::string dataset_group;
  ReconstructionRoute route{ReconstructionRoute::direct_adjoint};
  RadialHdf5TrajectoryUnits input_trajectory_units{RadialHdf5TrajectoryUnits::unspecified};
};

struct RouteDetails final {
  std::string_view display_name;
  std::string_view route_id;
  std::string_view pipeline_id;
  std::string_view pipeline_display_name;
  std::string_view provider_alias;
  std::string_view operator_id;
  std::string_view run_id;
};

struct RouteReport final {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t encoded_rows{0U};
  std::uint32_t encoded_cols{0U};
  std::uint32_t channels{0U};
  std::uint32_t acquisitions_read{0U};
  std::uint32_t samples_read{0U};
  std::uint64_t image_payload_bytes{0U};
  std::string execution_plan_digest;
  std::string verification_record_digest;
};

[[nodiscard]] RouteDetails route_details(const ReconstructionRoute route) noexcept {
  switch (route) {
    case ReconstructionRoute::direct_adjoint:
      return {.display_name = "Non-Cartesian RSS HDF5",
              .route_id = "noncartesian-rss",
              .pipeline_id = "org.kspacejet.noncartesian-rss",
              .pipeline_display_name = "Non-Cartesian RSS reconstruction",
              .provider_alias = "noncartesian",
              .operator_id = kNoncartesianOperatorId,
              .run_id = "noncartesian-rss-hdf5"};
    case ReconstructionRoute::radial_gridding:
      return {.display_name = "Radial RSS HDF5",
              .route_id = "radial-rss",
              .pipeline_id = "org.kspacejet.radial-rss",
              .pipeline_display_name = "Radial RSS gridding reconstruction",
              .provider_alias = "radial",
              .operator_id = kRadialGriddingOperatorId,
              .run_id = "radial-rss-hdf5"};
  }
  return {};
}

struct PlanningArtifacts final {
  TargetEnvelope target_envelope;
  MachinePolicy machine_policy;
};

struct ResolvedGraphInputs final {
  graph::ResolvedPipeline pipeline;
  std::vector<graph::HostDerivedNodeConfig> effective_node_configs;
  OperatorContract noncartesian_contract;
  OperatorContract coil_combine_contract;
};

struct DeclaredNoncartesianGeometry final {
  std::uint32_t rows{0U};
  std::uint32_t cols{0U};
  std::uint32_t encoded_rows{0U};
  std::uint32_t encoded_cols{0U};
};

struct Preflight final {
  ScanFacts scan_facts;
  DeclaredNoncartesianGeometry geometry;
  Shape shape;
  FrameSlotContext frame_context{};
  std::string source_xml;
  ksj::ismrmrd::AcquisitionHeader image_source_acquisition{};
};

struct AcquisitionFacts final {
  std::uint32_t samples{0U};
  std::uint32_t channels{0U};
  Quantity decoder_staging_bytes{0U};
};

struct SemanticFrameState final {
  std::optional<std::uint32_t> expected_channels;
  std::optional<FrameSlotContext> expected_context;
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
                                       const Quantity max_decoder_staging_bytes, const ReconstructionRoute route) {
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
  if (route == ReconstructionRoute::direct_adjoint) {
    auto direct_work = checked_product(coil_pixels.value(), total_samples, "direct-adjoint work");
    if (!direct_work.ok())
      return direct_work.status();
    if (direct_work.value() > kMaximumDirectAdjointWork) {
      return Status::ValidationError("Non-Cartesian RSS HDF5 exceeds the bounded direct-adjoint Provider work limit");
    }
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
  Result<Quantity> scratch = checked_product(pixels.value(), sizeof(std::complex<float>), "adjoint scratch bytes");
  if (!scratch.ok())
    return scratch.status();
  if (route == ReconstructionRoute::radial_gridding) {
    auto complex_scratch_elements = checked_product(pixels.value(), 2U, "radial gridding image planes");
    if (!complex_scratch_elements.ok())
      return complex_scratch_elements.status();
    const auto maximum_dimension = std::max(rows, cols);
    auto fft_line_elements = checked_product(maximum_dimension, 2U, "radial gridding FFT line buffers");
    if (!fft_line_elements.ok())
      return fft_line_elements.status();
    complex_scratch_elements = checked_sum(complex_scratch_elements.value(), fft_line_elements.value(),
                                           "radial gridding complex workspace elements");
    if (!complex_scratch_elements.ok())
      return complex_scratch_elements.status();
    auto complex_scratch = checked_product(complex_scratch_elements.value(), sizeof(std::complex<float>),
                                           "radial gridding complex workspace bytes");
    if (!complex_scratch.ok())
      return complex_scratch.status();
    auto density_compensation =
      checked_product(total_samples, sizeof(float), "radial gridding density-compensation bytes");
    if (!density_compensation.ok())
      return density_compensation.status();
    scratch = checked_sum(complex_scratch.value(), density_compensation.value(), "radial gridding scratch bytes");
    if (!scratch.ok())
      return scratch.status();
  }
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
               .reconstruction_scratch_bytes = scratch.value(),
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

[[nodiscard]] Status validate_config(const RouteConfig& config) {
  const auto details = route_details(config.route);
  if (config.input_file.empty() || config.output_image_file.empty() || config.reconstruction_provider_module.empty() ||
      config.coil_combine_provider_module.empty() || config.reconstruction_operator_contract.empty() ||
      config.coil_combine_operator_contract.empty() || config.dataset_group.empty()) {
    return Status::InvalidArgument(
      std::string(details.display_name) +
      " requires input/output, both explicit Provider modules/contracts, and a dataset group");
  }
  if (config.route == ReconstructionRoute::radial_gridding &&
      config.input_trajectory_units == RadialHdf5TrajectoryUnits::unspecified) {
    return Status::InvalidArgument("Radial RSS HDF5 requires an explicit cycles_per_fov, radians_per_pixel, or "
                                   "encoded_matrix_index input trajectory unit");
  }
  if (same_path(config.input_file, config.output_image_file)) {
    return Status::InvalidArgument(std::string(details.display_name) +
                                   " input and ISMRMRD image output must be distinct files");
  }
  return Status::Ok();
}

[[nodiscard]] Status hdf5_io_error(const RouteConfig& config, const std::string_view message) {
  return Status::IoError(std::string(route_details(config.route).display_name) +
                         " input failed: " + std::string(message));
}

[[nodiscard]] Status hdf5_source_error(const RouteConfig& config, const Status& source_status) {
  return source_status.code() == ksj::base::StatusCode::io_error ? hdf5_io_error(config, source_status.message())
                                                                 : source_status;
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

[[nodiscard]] Status validate_reconstruction_contract(const OperatorContract& contract, const RouteDetails& details) {
  if (contract.operator_id() != details.operator_id || contract.ports().size() != 3U) {
    return Status::ValidationError(std::string(details.display_name) + " OperatorContract does not describe " +
                                   std::string(details.operator_id));
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
    return Status::ValidationError(std::string(details.display_name) +
                                   " OperatorContract ports do not match the required typed reconstruction route");
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
                                                               const std::string_view expected_operator_id,
                                                               const OperatorContract& expected_contract) {
  if (expected_contract.operator_id() != expected_operator_id) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 OperatorContract identity does not match the required Operator '" +
      std::string(expected_operator_id) + "'");
  }
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
  return graph::ResolvedProvider{
    .alias = std::string(alias),
    .provider_id = std::string(expected_provider_id),
    .bundle_digest = std::move(bundle).value(),
    .operators = {{.id = std::string(expected_operator_id), .contract_digest = expected_contract.artifact_digest()}}};
}

[[nodiscard]] Result<ResolvedGraphInputs> make_graph_inputs(const RouteConfig& config, const Shape& shape) {
  const auto details = route_details(config.route);
  auto reconstruction_json = read_contract_file(config.reconstruction_operator_contract);
  if (!reconstruction_json.ok())
    return reconstruction_json.status();
  auto reconstruction_contract = graph::parse_operator_contract_json(reconstruction_json.value());
  if (!reconstruction_contract.ok())
    return reconstruction_contract.status();
  const auto reconstruction_status = validate_reconstruction_contract(reconstruction_contract.value(), details);
  if (!reconstruction_status.ok())
    return reconstruction_status;

  auto coil_json = read_contract_file(config.coil_combine_operator_contract);
  if (!coil_json.ok())
    return coil_json.status();
  auto coil_contract = graph::parse_operator_contract_json(coil_json.value());
  if (!coil_contract.ok())
    return coil_contract.status();
  const auto coil_status = validate_coil_combine_contract(coil_contract.value());
  if (!coil_status.ok())
    return coil_status;

  auto reconstruction_provider =
    resolve_provider(config.reconstruction_provider_module, details.provider_alias, kNoncartesianProviderId,
                     details.operator_id, reconstruction_contract.value());
  if (!reconstruction_provider.ok())
    return reconstruction_provider.status();
  auto coil_provider = resolve_provider(config.coil_combine_provider_module, "coilcombine", kCoilCombineProviderId,
                                        kCoilCombineOperatorId, coil_contract.value());
  if (!coil_provider.ok())
    return coil_provider.status();

  // The PipelineDefinition remains user-authored: it contains only algorithm
  // selections that do not depend on this particular scan.  Geometry and
  // observed acquisition facts are bound separately after preflight, so a
  // pipeline identity never accidentally claims scan-specific dimensions.
  const std::string reconstruction_authored_config =
    config.route == ReconstructionRoute::radial_gridding
      ? "{\"density_compensation\":\"radial_analytic_ramp\",\"trajectory_units\":\"radians_per_pixel\"}"
      : "{}";
  std::string reconstruction_effective_config =
    "{\"channels\":" + std::to_string(shape.channels) + ",\"image_cols\":" + std::to_string(shape.cols) +
    ",\"image_rows\":" + std::to_string(shape.rows) + ",\"sample_count\":" + std::to_string(shape.total_samples) + "}";
  if (config.route == ReconstructionRoute::radial_gridding) {
    reconstruction_effective_config =
      "{\"channels\":" + std::to_string(shape.channels) +
      ",\"density_compensation\":\"radial_analytic_ramp\",\"image_cols\":" + std::to_string(shape.cols) +
      ",\"image_rows\":" + std::to_string(shape.rows) + ",\"sample_count\":" + std::to_string(shape.total_samples) +
      ",\"trajectory_units\":\"radians_per_pixel\"}";
  }
  const std::string coil_effective_config = "{\"channels\":" + std::to_string(shape.channels) +
                                            ",\"cols\":" + std::to_string(shape.cols) +
                                            ",\"rows\":" + std::to_string(shape.rows) + "}";
  const auto pipeline_document =
    "{"
    "\"kind\":\"PipelineDefinition\","
    "\"pipeline\":{\"id\":" +
    json_string(details.pipeline_id) + ",\"display_name\":" + json_string(details.pipeline_display_name) +
    "},"
    "\"input_profile\":{\"kind\":\"ismrmrd-hdf5\",\"container\":{\"mode\":\"auto\"}},"
    "\"allowed_profiles\":[\"offline-reference\"],"
    "\"parameters\":{},"
    "\"provider_requirements\":["
    "{\"alias\":" +
    json_string(details.provider_alias) +
    ",\"provider_id\":\"org.kspacejet.noncartesian-recon\"},"
    "{\"alias\":\"coilcombine\",\"provider_id\":\"org.kspacejet.coil-combine\"}"
    "],"
    "\"nodes\":["
    "{\"id\":\"reconstruct\",\"operator\":{\"provider\":" +
    json_string(details.provider_alias) + ",\"id\":" + json_string(details.operator_id) +
    "},"
    "\"config\":" +
    reconstruction_authored_config +
    "},"
    "{\"id\":\"combine\",\"operator\":{\"provider\":\"coilcombine\",\"id\":\"coil_combine_rss\"},\"config\":{}"
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
    std::move(definition).value(), {std::move(reconstruction_provider).value(), std::move(coil_provider).value()});
  if (!pipeline.ok())
    return pipeline.status();
  return ResolvedGraphInputs{
    .pipeline = std::move(pipeline).value(),
    .effective_node_configs = {{.node_id = kReconstructNodeId,
                                .canonical_config = std::move(reconstruction_effective_config)},
                               {.node_id = kCombineNodeId, .canonical_config = std::move(coil_effective_config)}},
    .noncartesian_contract = std::move(reconstruction_contract).value(),
    .coil_combine_contract = std::move(coil_contract).value()};
}

[[nodiscard]] Result<NodePlanningRequirements> make_reconstruction_requirements(const OperatorContract& contract,
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
     .resources = {.scratch_charged_bytes_per_firing = shape.reconstruction_scratch_bytes,
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
  budget = checked_sum(budget.value(), shape.reconstruction_scratch_bytes, "machine budget reconstruction scratch");
  if (!budget.ok())
    return budget.status();
  auto doubled = checked_product(budget.value(), 2U, "machine budget double-buffer allowance");
  if (!doubled.ok())
    return doubled.status();
  auto host_budget = checked_sum(doubled.value(), kMachineBudgetHeadroomBytes, "machine budget headroom");
  if (!host_budget.ok())
    return host_budget.status();

  const auto max_frame_bytes = std::max(shape.kspace_charged_bytes, shape.trajectory_charged_bytes);
  auto target = TargetEnvelope::create(
    {.max_xml_bytes = preflight.scan_facts.descriptor().source_xml_bytes(),
     .max_frame_charged_bytes = max_frame_bytes,
     .max_image_charged_bytes = shape.image_charged_bytes,
     .max_decoder_staging_bytes = shape.max_decoder_staging_bytes,
     .max_samples_per_acquisition = preflight.scan_facts.maximum_samples_per_acquisition(),
     .max_trajectory_dimensions = 2U,
     .max_active_channels = preflight.scan_facts.physical_channel_count(),
     .max_channel_groups = 1U,
     .max_dynamic_keys_per_scan = 1U,
     .max_active_scans = 1U,
     .calibration_horizon_items = 0U,
     .calibration_horizon_charged_bytes = 0U,
     .arrival_envelope = {.max_acquisitions_per_second = preflight.scan_facts.acquisition_count(),
                          .max_burst_acquisitions = preflight.scan_facts.acquisition_count()},
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
  return PlanningArtifacts{.target_envelope = std::move(target).value(), .machine_policy = std::move(policy).value()};
}

[[nodiscard]] bool is_supported_noncartesian_trajectory(const TrajectoryType trajectory) noexcept {
  return trajectory == TrajectoryType::radial || trajectory == TrajectoryType::golden_angle ||
         trajectory == TrajectoryType::spiral || trajectory == TrajectoryType::other;
}

[[nodiscard]] Result<DeclaredNoncartesianGeometry>
derive_declared_noncartesian_geometry(const ScanDescriptor& descriptor, const ReconstructionRoute route) {
  if (descriptor.encodings().size() != 1U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 requires exactly one ISMRMRD encoding");
  }
  const auto& encoding = descriptor.encodings().front();
  const auto& encoded = encoding.encoded_matrix();
  const auto& reconstructed = encoding.recon_matrix();
  const auto valid_trajectory = route == ReconstructionRoute::radial_gridding
                                  ? encoding.trajectory() == TrajectoryType::radial
                                  : is_supported_noncartesian_trajectory(encoding.trajectory());
  const auto radial_encoded_geometry = route != ReconstructionRoute::radial_gridding ||
                                       (encoded.x >= kMinimumImageDimension && encoded.x <= kMaximumImageDimension &&
                                        encoded.y >= kMinimumImageDimension && encoded.y <= kMaximumImageDimension);
  const auto radial_reconstruction_geometry =
    route != ReconstructionRoute::radial_gridding ||
    (std::has_single_bit(reconstructed.x) && std::has_single_bit(reconstructed.y));
  if (!valid_trajectory || encoded.z != 1U || reconstructed.z != 1U || !radial_encoded_geometry ||
      !radial_reconstruction_geometry || reconstructed.x < kMinimumImageDimension ||
      reconstructed.x > kMaximumImageDimension || reconstructed.y < kMinimumImageDimension ||
      reconstructed.y > kMaximumImageDimension) {
    if (route == ReconstructionRoute::radial_gridding) {
      return Status::ValidationError(
        "Radial RSS HDF5 XML must declare one 2-D radial reconstruction matrix with power-of-two axes");
    }
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 XML must declare one 2-D radial, golden-angle, spiral, or other reconstruction matrix");
  }
  return DeclaredNoncartesianGeometry{.rows = static_cast<std::uint32_t>(reconstructed.y),
                                      .cols = static_cast<std::uint32_t>(reconstructed.x),
                                      .encoded_rows = static_cast<std::uint32_t>(encoded.y),
                                      .encoded_cols = static_cast<std::uint32_t>(encoded.x)};
}

[[nodiscard]] Result<float> normalize_radial_coordinate(const float coordinate,
                                                        const RadialHdf5TrajectoryUnits input_units,
                                                        const std::uint32_t encoded_dimension) {
  if (!std::isfinite(coordinate)) {
    return Status::ValidationError("Radial RSS HDF5 trajectory coordinate must be finite");
  }
  switch (input_units) {
    case RadialHdf5TrajectoryUnits::cycles_per_fov:
      if (std::fabs(coordinate) > 0.5F) {
        return Status::ValidationError(
          "Radial RSS HDF5 cycles_per_fov trajectory coordinate is outside the inclusive Nyquist interval [-0.5,0.5]");
      }
      return coordinate * (2.0F * std::numbers::pi_v<float>);
    case RadialHdf5TrajectoryUnits::radians_per_pixel:
      if (std::fabs(coordinate) > std::numbers::pi_v<float>) {
        return Status::ValidationError(
          "Radial RSS HDF5 radians_per_pixel trajectory coordinate is outside the inclusive Nyquist interval [-pi,pi]");
      }
      return coordinate;
    case RadialHdf5TrajectoryUnits::encoded_matrix_index:
      {
        if (encoded_dimension < kMinimumImageDimension) {
          return Status::ValidationError("Radial RSS HDF5 encoded_matrix_index requires an XML encoded dimension >= 2");
        }
        const auto half_encoded_dimension = static_cast<float>(encoded_dimension) / 2.0F;
        if (std::fabs(coordinate) > half_encoded_dimension) {
          return Status::ValidationError("Radial RSS HDF5 encoded_matrix_index trajectory coordinate is outside the "
                                         "inclusive encoded Nyquist interval");
        }
        return coordinate * (2.0F * std::numbers::pi_v<float> / static_cast<float>(encoded_dimension));
      }
    case RadialHdf5TrajectoryUnits::unspecified:
      return Status::InvalidArgument("Radial RSS HDF5 trajectory units are unspecified");
  }
  return Status::InvalidArgument("Radial RSS HDF5 trajectory units are invalid");
}

struct CanonicalRadialCoordinate final {
  float row{0.0F};
  float column{0.0F};
};

// ISMRMRD acquisition trajectories are laid out as [kx, ky] in two dimensions.
// The reconstruction payload type is intentionally [row=ky, column=kx]. The
// conversion happens only at this explicit route boundary, after each raw axis
// has been checked and normalized with its own XML encoded dimension.
[[nodiscard]] Result<CanonicalRadialCoordinate>
normalize_radial_coordinate_pair(const float raw_kx, const float raw_ky, const RadialHdf5TrajectoryUnits input_units,
                                 const DeclaredNoncartesianGeometry& geometry) {
  auto column = normalize_radial_coordinate(raw_kx, input_units, geometry.encoded_cols);
  if (!column.ok()) {
    return column.status();
  }
  auto row = normalize_radial_coordinate(raw_ky, input_units, geometry.encoded_rows);
  if (!row.ok()) {
    return row.status();
  }
  return CanonicalRadialCoordinate{.row = row.value(), .column = column.value()};
}

[[nodiscard]] Status validate_radial_trajectory(const NormalizedIsmrmrdAcquisition& normalized,
                                                const RadialHdf5TrajectoryUnits input_units,
                                                const DeclaredNoncartesianGeometry& geometry) {
  for (std::size_t sample = 0U; sample < normalized.trajectory.size() / 2U; ++sample) {
    const auto offset = sample * 2U;
    auto coordinate = normalize_radial_coordinate_pair(normalized.trajectory[offset],
                                                       normalized.trajectory[offset + 1U], input_units, geometry);
    if (!coordinate.ok()) {
      return coordinate.status();
    }
  }
  return Status::Ok();
}

[[nodiscard]] Status validate_declared_channels(const ScanDescriptor& descriptor, const std::uint32_t channels) {
  if (descriptor.declared_receiver_channels().has_value() &&
      descriptor.declared_receiver_channels().value() < channels) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 receiverChannels is smaller than acquisition active_channels");
  }
  return Status::Ok();
}

[[nodiscard]] Result<AcquisitionFacts> validate_acquisition(const ksj::ismrmrd::AcquisitionView& acquisition,
                                                            const NormalizedIsmrmrdAcquisition& normalized,
                                                            SemanticFrameState& frame_state, const RouteConfig& config,
                                                            const DeclaredNoncartesianGeometry& geometry) {
  const auto& header = acquisition.header;
  // This development route freezes exactly one frame from preflighted input;
  // ISMRMRD first/last control flags never act as an implicit completion rule.
  if (normalized.classification.lane != AcquisitionLane::imaging) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 supports only the imaging semantic lane; received " +
                                   std::string(to_string(normalized.classification.lane)));
  }
  const auto frame_context = make_ismrmrd_frame_slot_context(normalized, {.order_key = 0U, .placement_key = 0U});
  if (frame_context.semantic_key.encoding_space != 0U) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 requires encoding_space_ref zero for its one XML encoding");
  }
  if (header.discard_pre != 0U || header.discard_post != 0U) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 accepts only imaging acquisitions without discarded samples");
  }
  const auto samples = normalized.ingress_facts.samples_per_acquisition;
  const auto channels = normalized.ingress_facts.active_channels;
  if (samples == 0U || channels == 0U || channels > kMaximumChannels || header.available_channels < channels ||
      normalized.ingress_facts.trajectory_dimensions != 2U) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 acquisition must have samples, [1,64] active channels, and an explicit 2-D trajectory");
  }
  if (config.route == ReconstructionRoute::radial_gridding) {
    const auto radial_trajectory = validate_radial_trajectory(normalized, config.input_trajectory_units, geometry);
    if (!radial_trajectory.ok())
      return radial_trajectory;
  }
  if (!frame_state.expected_channels.has_value()) {
    frame_state.expected_channels = static_cast<std::uint32_t>(channels);
  } else if (*frame_state.expected_channels != channels) {
    return Status::ValidationError("Non-Cartesian RSS HDF5 acquisition active_channels changes within one frame");
  }
  if (!frame_state.expected_context.has_value()) {
    frame_state.expected_context = frame_context;
  } else if (frame_state.expected_context->semantic_key != frame_context.semantic_key) {
    return Status::ValidationError(
      "Non-Cartesian RSS HDF5 cannot merge acquisitions from mixed FrameSlot semantic contexts");
  }
  auto data_bytes = checked_product(static_cast<Quantity>(samples), channels, "acquisition sample count");
  if (!data_bytes.ok())
    return data_bytes.status();
  data_bytes = checked_product(data_bytes.value(), sizeof(std::complex<float>), "acquisition k-space bytes");
  if (!data_bytes.ok())
    return data_bytes.status();
  auto trajectory_bytes = checked_product(static_cast<Quantity>(samples), 2U, "acquisition trajectory count");
  if (!trajectory_bytes.ok())
    return trajectory_bytes.status();
  trajectory_bytes = checked_product(trajectory_bytes.value(), sizeof(float), "acquisition trajectory bytes");
  if (!trajectory_bytes.ok())
    return trajectory_bytes.status();
  auto staging = checked_sum(data_bytes.value(), trajectory_bytes.value(), "acquisition decoder staging bytes");
  if (!staging.ok())
    return staging.status();
  return AcquisitionFacts{.samples = static_cast<std::uint32_t>(samples),
                          .channels = static_cast<std::uint32_t>(channels),
                          .decoder_staging_bytes = staging.value()};
}

[[nodiscard]] Result<Preflight> preflight_input(const RouteConfig& config, const AcquisitionClassifier& classifier) {
  const auto details = route_details(config.route);
  const IsmrmrdHdf5ReplaySource source({.input_file = config.input_file, .dataset_group = config.dataset_group});
  auto opened = source.open();
  if (!opened.ok())
    return hdf5_source_error(config, opened.status());
  auto session = std::move(opened).value();
  if (session.metadata().xml_header.empty()) {
    return Status::ValidationError(std::string(details.display_name) + " requires an ISMRMRD XML header");
  }
  auto scan = ScanDescriptor::parse_ismrmrd_xml(session.metadata().xml_header);
  if (!scan.ok())
    return scan.status();
  auto geometry = derive_declared_noncartesian_geometry(scan.value(), config.route);
  if (!geometry.ok())
    return geometry.status();

  SemanticFrameState frame_state;
  Quantity total_samples{0U};
  std::uint32_t max_samples_per_acquisition{0U};
  Quantity max_decoder_staging_bytes{0U};
  std::uint32_t acquisitions{0U};
  std::optional<ksj::ismrmrd::AcquisitionHeader> image_source_acquisition;
  Status callback_status = Status::Ok();
  const auto iteration = session.for_each_acquisition([&](const ksj::ismrmrd::AcquisitionView& acquisition) {
    auto normalized = normalize_ismrmrd_acquisition(acquisition, classifier);
    if (!normalized.ok()) {
      callback_status = normalized.status();
      return false;
    }
    auto facts = validate_acquisition(acquisition, normalized.value(), frame_state, config, geometry.value());
    if (!facts.ok()) {
      callback_status = facts.status();
      return false;
    }
    if (!image_source_acquisition.has_value())
      image_source_acquisition = acquisition.header;
    auto next_samples = checked_sum(total_samples, facts.value().samples, "total trajectory sample count");
    if (!next_samples.ok() || next_samples.value() > kMaximumSamples) {
      callback_status = next_samples.ok()
                          ? Status::ValidationError(std::string(details.display_name) + " total samples exceed 65536")
                          : next_samples.status();
      return false;
    }
    if (acquisitions == std::numeric_limits<std::uint32_t>::max()) {
      callback_status =
        Status::ValidationError(std::string(details.display_name) + " acquisition count overflows uint32");
      return false;
    }
    total_samples = next_samples.value();
    max_samples_per_acquisition = std::max(max_samples_per_acquisition, facts.value().samples);
    max_decoder_staging_bytes = std::max(max_decoder_staging_bytes, facts.value().decoder_staging_bytes);
    ++acquisitions;
    return true;
  });
  if (!iteration.ok())
    return hdf5_source_error(config, iteration.status());
  if (iteration.value() == IsmrmrdHdf5ReplayIterationResult::stopped) {
    return callback_status.ok()
             ? Status::Unavailable(std::string(details.display_name) + " preflight stopped before EndOfInput")
             : callback_status;
  }
  if (!callback_status.ok())
    return callback_status;
  if (!frame_state.expected_channels.has_value() || !frame_state.expected_context.has_value() ||
      !image_source_acquisition.has_value() || acquisitions == 0U || total_samples == 0U) {
    return Status::ValidationError(std::string(details.display_name) + " requires at least one complete acquisition");
  }
  auto shape = make_shape(geometry.value().rows, geometry.value().cols, *frame_state.expected_channels,
                          static_cast<std::uint32_t>(total_samples), max_samples_per_acquisition,
                          max_decoder_staging_bytes, config.route);
  if (!shape.ok())
    return shape.status();
  const auto declared_channels = validate_declared_channels(scan.value(), shape.value().channels);
  if (!declared_channels.ok())
    return declared_channels;

  auto scan_facts = ScanFacts::create({.descriptor = std::move(scan).value(),
                                       .source_xml = session.metadata().xml_header,
                                       .acquisition_count = acquisitions,
                                       .physical_channel_count = shape.value().channels,
                                       .maximum_samples_per_acquisition = shape.value().max_samples_per_acquisition,
                                       .trajectory_dimensions = 2U});
  if (!scan_facts.ok())
    return scan_facts.status();
  return Preflight{.scan_facts = std::move(scan_facts).value(),
                   .geometry = geometry.value(),
                   .shape = std::move(shape).value(),
                   .frame_context = *frame_state.expected_context,
                   .source_xml = session.metadata().xml_header,
                   .image_source_acquisition = *image_source_acquisition};
}

[[nodiscard]] Status replay_into_executor(const RouteConfig& config, const Preflight& preflight,
                                          SynchronousGraphExecutor& executor, const AcquisitionClassifier& classifier) {
  const auto details = route_details(config.route);
  const IsmrmrdHdf5ReplaySource source({.input_file = config.input_file, .dataset_group = config.dataset_group});
  auto opened = source.open();
  if (!opened.ok())
    return hdf5_source_error(config, opened.status());
  auto session = std::move(opened).value();
  auto replay_xml_digest = derive_ismrmrd_source_xml_artifact_digest(
    session.metadata().xml_header, std::string(details.display_name) + " replay XML input");
  if (!replay_xml_digest.ok())
    return replay_xml_digest.status();
  if (replay_xml_digest.value() != preflight.scan_facts.source_xml_digest()) {
    return Status::ValidationError(std::string(details.display_name) + " XML changed between preflight and replay");
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
    return Status::ValidationError(std::string(details.display_name) +
                                   " ingress pool is smaller than the frozen frame shape");
  }

  auto* const kspace_destination = kspace_payload.value().data();
  auto* const trajectory_destination = trajectory_payload.value().data();
  SemanticFrameState frame_state{.expected_channels = preflight.shape.channels,
                                 .expected_context = preflight.frame_context};
  std::uint32_t acquisitions{0U};
  std::uint32_t written_samples{0U};
  std::uint32_t max_samples_per_acquisition{0U};
  Quantity max_decoder_staging_bytes{0U};
  Status callback_status = Status::Ok();
  const auto iteration = session.for_each_acquisition([&](const ksj::ismrmrd::AcquisitionView& acquisition) {
    auto normalized = normalize_ismrmrd_acquisition(acquisition, classifier);
    if (!normalized.ok()) {
      callback_status = normalized.status();
      return false;
    }
    auto facts = validate_acquisition(acquisition, normalized.value(), frame_state, config, preflight.geometry);
    if (!facts.ok()) {
      callback_status = facts.status();
      return false;
    }
    if (facts.value().samples > preflight.shape.total_samples - written_samples ||
        acquisitions == std::numeric_limits<std::uint32_t>::max()) {
      callback_status =
        Status::ValidationError(std::string(details.display_name) + " input changed between preflight and replay");
      return false;
    }
    const auto local_samples = static_cast<std::size_t>(facts.value().samples);
    const auto total_samples = static_cast<std::size_t>(preflight.shape.total_samples);
    const auto sample_offset = static_cast<std::size_t>(written_samples);
    for (std::uint32_t channel = 0U; channel < preflight.shape.channels; ++channel) {
      const auto source_offset = static_cast<std::size_t>(channel) * local_samples * sizeof(std::complex<float>);
      const auto destination_offset =
        (static_cast<std::size_t>(channel) * total_samples + sample_offset) * sizeof(std::complex<float>);
      std::memcpy(kspace_destination + destination_offset, normalized.value().sample_bytes.data() + source_offset,
                  local_samples * sizeof(std::complex<float>));
    }
    const auto trajectory_offset = sample_offset * 2U * sizeof(float);
    if (config.route == ReconstructionRoute::direct_adjoint) {
      std::memcpy(trajectory_destination + trajectory_offset, normalized.value().trajectory.data(),
                  local_samples * 2U * sizeof(float));
    } else {
      for (std::size_t sample = 0U; sample < local_samples; ++sample) {
        const auto offset = sample * 2U;
        auto coordinate = normalize_radial_coordinate_pair(normalized.value().trajectory[offset],
                                                           normalized.value().trajectory[offset + 1U],
                                                           config.input_trajectory_units, preflight.geometry);
        if (!coordinate.ok()) {
          callback_status = coordinate.status();
          return false;
        }
        const auto row = coordinate.value().row;
        const auto column = coordinate.value().column;
        std::memcpy(trajectory_destination + trajectory_offset + offset * sizeof(float), &row, sizeof(float));
        std::memcpy(trajectory_destination + trajectory_offset + (offset + 1U) * sizeof(float), &column, sizeof(float));
      }
    }
    written_samples += facts.value().samples;
    max_samples_per_acquisition = std::max(max_samples_per_acquisition, facts.value().samples);
    max_decoder_staging_bytes = std::max(max_decoder_staging_bytes, facts.value().decoder_staging_bytes);
    ++acquisitions;
    return true;
  });
  if (!iteration.ok())
    return hdf5_source_error(config, iteration.status());
  if (iteration.value() == IsmrmrdHdf5ReplayIterationResult::stopped || !callback_status.ok()) {
    return callback_status.ok()
             ? Status::Unavailable(std::string(details.display_name) + " replay stopped before EndOfInput")
             : callback_status;
  }
  if (acquisitions != preflight.scan_facts.acquisition_count() || written_samples != preflight.shape.total_samples ||
      max_samples_per_acquisition != preflight.scan_facts.maximum_samples_per_acquisition() ||
      max_decoder_staging_bytes != preflight.shape.max_decoder_staging_bytes) {
    return Status::ValidationError(std::string(details.display_name) + " input changed between preflight and replay");
  }

  const auto identity = make_data_item_identity(preflight.frame_context, 0U);
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

[[nodiscard]] std::vector<ksj::base::byte> semantic_key_bytes(const FrameSlotContext& context) {
  std::vector<ksj::base::byte> result(sizeof(std::uint16_t) * 8U);
  const std::array values{
    context.semantic_key.encoding_space, context.semantic_key.slice,  context.semantic_key.contrast,
    context.semantic_key.repetition,     context.semantic_key.set,    context.semantic_key.phase,
    context.semantic_key.average,        context.semantic_key.segment};
  for (std::size_t index = 0U; index < values.size(); ++index) {
    result[index * 2U] = static_cast<ksj::base::byte>(values[index] & 0xFFU);
    result[index * 2U + 1U] = static_cast<ksj::base::byte>((values[index] >> 8U) & 0xFFU);
  }
  return result;
}

[[nodiscard]] Result<std::unique_ptr<ProviderNodeInstance>>
make_provider_node(const ExecutionPlan& plan, const graph::EffectivePipelineBinding& effective_pipeline_binding,
                   const std::filesystem::path& module_path, const std::string_view node_id,
                   const ArtifactDigest& scan_facts_digest, const FrameSlotContext& frame_context,
                   const std::uint64_t execution_context_id, const RouteDetails& details) {
  auto effective_config = effective_pipeline_binding.config_for(node_id);
  if (!effective_config.ok()) {
    return effective_config.status();
  }
  if (effective_config.value().empty()) {
    return Status::InternalError("Non-Cartesian RSS HDF5 effective Pipeline binding omitted a required canonical "
                                 "Provider configuration");
  }
  return ProviderNodeInstance::create(plan, {.module_path = module_path,
                                             .node_id = std::string(node_id),
                                             .canonical_config = std::string(effective_config.value()),
                                             .start_facts = {.normalized_scan_facts_digest = scan_facts_digest,
                                                             .execution_plan_digest = plan.digest(),
                                                             .run_id = std::string(details.run_id),
                                                             .scan_instance_id = std::string(details.run_id),
                                                             .terminal_epoch = kTerminalEpoch},
                                             .execution_context_id = execution_context_id,
                                             .resource_domain_id = 1U,
                                             .max_backend_concurrency = 1U,
                                             .numa_node = 0U,
                                             .device_ordinal = 0U,
                                             .key_state = {.semantic_key = semantic_key_bytes(frame_context),
                                                           .placement_key = frame_context.placement_key,
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

[[nodiscard]] Status commit_ismrmrd_image_artifact(const RouteConfig& config, const Preflight& preflight,
                                                   const RouteReport& report, EgressInputLease& image) {
  const auto details = route_details(config.route);
  if (preflight.scan_facts.descriptor().encodings().empty()) {
    return Status::InternalError(std::string(details.display_name) + " ISMRMRD output requires one validated encoding");
  }
  std::vector<std::pair<std::string, std::string>> provenance;
  provenance.reserve(config.route == ReconstructionRoute::radial_gridding ? 14U : 9U);
  provenance.emplace_back("KSpaceJet.Route", details.route_id);
  provenance.emplace_back("KSpaceJet.AcquisitionsRead", std::to_string(report.acquisitions_read));
  provenance.emplace_back("KSpaceJet.SamplesRead", std::to_string(report.samples_read));
  provenance.emplace_back("KSpaceJet.InputChannels", std::to_string(report.channels));
  provenance.emplace_back("KSpaceJet.ExecutionPlanDigest", report.execution_plan_digest);
  provenance.emplace_back("KSpaceJet.VerificationRecordDigest", report.verification_record_digest);
  provenance.emplace_back("KSpaceJet.SourceXmlDigest", preflight.scan_facts.source_xml_digest().value());
  provenance.emplace_back("KSpaceJet.Operator", details.operator_id);
  provenance.emplace_back("KSpaceJet.Operator", "coil_combine_rss");
  if (config.route == ReconstructionRoute::radial_gridding) {
    provenance.emplace_back("KSpaceJet.DensityCompensation", "radial_analytic_ramp");
    provenance.emplace_back("KSpaceJet.InputTrajectoryUnits", to_string(config.input_trajectory_units));
    provenance.emplace_back("KSpaceJet.InputEncodedMatrixColumns", std::to_string(report.encoded_cols));
    provenance.emplace_back("KSpaceJet.InputEncodedMatrixRows", std::to_string(report.encoded_rows));
    provenance.emplace_back("KSpaceJet.TrajectoryUnits", "radians_per_pixel");
  }
  IsmrmrdImageArtifactSink sink{
    config.output_image_file,
    {.source_xml = preflight.source_xml,
     .source_acquisition = preflight.image_source_acquisition,
     .field_of_view_mm = preflight.scan_facts.descriptor().encodings().front().recon_field_of_view_mm(),
     .rows = report.rows,
     .cols = report.cols,
     .provenance_attributes = std::move(provenance)}};
  return sink.commit(image);
}

} // namespace

namespace {

[[nodiscard]] Result<RouteReport> reconstruct_route(const RouteConfig& config) {
  try {
    const auto details = route_details(config.route);
    const auto config_status = validate_config(config);
    if (!config_status.ok())
      return config_status;
    auto classifier = AcquisitionClassifier::create({});
    if (!classifier.ok())
      return classifier.status();
    auto preflight = preflight_input(config, classifier.value());
    if (!preflight.ok())
      return preflight.status();
    auto graph_inputs = make_graph_inputs(config, preflight.value().shape);
    if (!graph_inputs.ok())
      return graph_inputs.status();
    auto planning = make_planning_artifacts(preflight.value());
    if (!planning.ok())
      return planning.status();
    auto reconstruct_requirements =
      make_reconstruction_requirements(graph_inputs.value().noncartesian_contract, preflight.value().shape);
    if (!reconstruct_requirements.ok())
      return reconstruct_requirements.status();
    auto combine_requirements =
      make_coil_combine_requirements(graph_inputs.value().coil_combine_contract, preflight.value().shape);
    if (!combine_requirements.ok())
      return combine_requirements.status();

    auto effective_pipeline_binding = graph::EffectivePipelineBinding::create_from_host_derived_configs(
      graph_inputs.value().pipeline, preflight.value().scan_facts,
      std::move(graph_inputs.value().effective_node_configs));
    if (!effective_pipeline_binding.ok())
      return effective_pipeline_binding.status();

    const graph::PlanBuildRequest request{
      .resolved_pipeline = graph_inputs.value().pipeline,
      .requested_profile = ExecutionProfile::offline_reference,
      .scan_facts = preflight.value().scan_facts,
      .effective_pipeline_binding = effective_pipeline_binding.value(),
      .target_envelope = planning.value().target_envelope,
      .machine_policy = planning.value().machine_policy,
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
    auto reconstruct_provider = make_provider_node(
      compiled.value().plan, effective_pipeline_binding.value(), config.reconstruction_provider_module,
      kReconstructNodeId, preflight.value().scan_facts.digest(), preflight.value().frame_context, 1U, details);
    if (!reconstruct_provider.ok())
      return reconstruct_provider.status();
    auto combine_provider = make_provider_node(
      compiled.value().plan, effective_pipeline_binding.value(), config.coil_combine_provider_module, kCombineNodeId,
      preflight.value().scan_facts.digest(), preflight.value().frame_context, 2U, details);
    if (!combine_provider.ok())
      return combine_provider.status();

    const auto replay = replay_into_executor(config, preflight.value(), *executor.value(), classifier.value());
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
      return failure.ok() ? Status::StateError(std::string(details.display_name) +
                                               " reconstruction Provider did not finish its normal frame firing")
                          : failure;
    }
    auto combined = fire_node(*executor.value(), *combine_provider.value(), kCombineNodeId, 2U);
    if (!combined.ok() || combined.value().outcome != SynchronousFiringOutcome::done) {
      const auto failure = executor.value()->snapshot().last_error;
      static_cast<void>(executor.value()->abort());
      if (!combined.ok())
        return combined.status();
      return failure.ok() ? Status::StateError(std::string(details.display_name) +
                                               " coil-combine Provider did not finish its normal frame firing")
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
    RouteReport report{
      .rows = preflight.value().shape.rows,
      .cols = preflight.value().shape.cols,
      .encoded_rows = preflight.value().geometry.encoded_rows,
      .encoded_cols = preflight.value().geometry.encoded_cols,
      .channels = preflight.value().shape.channels,
      .acquisitions_read = static_cast<std::uint32_t>(preflight.value().scan_facts.acquisition_count()),
      .samples_read = preflight.value().shape.total_samples,
      .image_payload_bytes = preflight.value().shape.image_bytes,
      .execution_plan_digest = compiled.value().plan.digest().value(),
      .verification_record_digest = verification.value().digest().value(),
    };
    const auto image_write = commit_ismrmrd_image_artifact(config, preflight.value(), report, image.value());
    if (!image_write.ok()) {
      static_cast<void>(executor.value()->abort());
      return image_write;
    }
    if (executor.value()->egress_poll_kind(kImageEgressId) != FixedBufferEdgePollKind::completed) {
      return Status::StateError(std::string(details.display_name) +
                                " egress did not close after its one expected image");
    }
    return report;
  } catch (const std::bad_alloc&) {
    return Status::OutOfMemory(std::string(route_details(config.route).display_name) +
                               " reconstruction exhausted host memory");
  } catch (const std::exception& exception) {
    return Status::InternalError(std::string(route_details(config.route).display_name) +
                                 " reconstruction threw: " + std::string(exception.what()));
  } catch (...) {
    return Status::InternalError(std::string(route_details(config.route).display_name) +
                                 " reconstruction threw an unknown exception");
  }
}

} // namespace

const char* to_string(const RadialHdf5TrajectoryUnits value) noexcept {
  switch (value) {
    case RadialHdf5TrajectoryUnits::unspecified:
      return "unspecified";
    case RadialHdf5TrajectoryUnits::cycles_per_fov:
      return "cycles_per_fov";
    case RadialHdf5TrajectoryUnits::radians_per_pixel:
      return "radians_per_pixel";
    case RadialHdf5TrajectoryUnits::encoded_matrix_index:
      return "encoded_matrix_index";
  }
  return "invalid";
}

Result<NoncartesianRssHdf5ReconstructionReport>
reconstruct_noncartesian_rss_hdf5(const NoncartesianRssHdf5ReconstructionConfig& config) {
  auto result = reconstruct_route({.input_file = config.input_file,
                                   .output_image_file = config.output_image_file,
                                   .reconstruction_provider_module = config.noncartesian_provider_module,
                                   .coil_combine_provider_module = config.coil_combine_provider_module,
                                   .reconstruction_operator_contract = config.noncartesian_operator_contract,
                                   .coil_combine_operator_contract = config.coil_combine_operator_contract,
                                   .dataset_group = config.dataset_group,
                                   .route = ReconstructionRoute::direct_adjoint});
  if (!result.ok())
    return result.status();
  return NoncartesianRssHdf5ReconstructionReport{.rows = result.value().rows,
                                                 .cols = result.value().cols,
                                                 .channels = result.value().channels,
                                                 .acquisitions_read = result.value().acquisitions_read,
                                                 .samples_read = result.value().samples_read,
                                                 .image_payload_bytes = result.value().image_payload_bytes,
                                                 .execution_plan_digest = result.value().execution_plan_digest,
                                                 .verification_record_digest =
                                                   result.value().verification_record_digest};
}

Result<RadialRssHdf5ReconstructionReport> reconstruct_radial_rss_hdf5(const RadialRssHdf5ReconstructionConfig& config) {
  auto result = reconstruct_route({.input_file = config.input_file,
                                   .output_image_file = config.output_image_file,
                                   .reconstruction_provider_module = config.radial_provider_module,
                                   .coil_combine_provider_module = config.coil_combine_provider_module,
                                   .reconstruction_operator_contract = config.radial_operator_contract,
                                   .coil_combine_operator_contract = config.coil_combine_operator_contract,
                                   .dataset_group = config.dataset_group,
                                   .route = ReconstructionRoute::radial_gridding,
                                   .input_trajectory_units = config.input_trajectory_units});
  if (!result.ok())
    return result.status();
  return RadialRssHdf5ReconstructionReport{.rows = result.value().rows,
                                           .cols = result.value().cols,
                                           .channels = result.value().channels,
                                           .acquisitions_read = result.value().acquisitions_read,
                                           .samples_read = result.value().samples_read,
                                           .image_payload_bytes = result.value().image_payload_bytes,
                                           .execution_plan_digest = result.value().execution_plan_digest,
                                           .verification_record_digest = result.value().verification_record_digest};
}

} // namespace ksj::recon::runtime
