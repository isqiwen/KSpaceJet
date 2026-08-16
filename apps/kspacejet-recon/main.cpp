#include "kspacejet/recon/runtime/cartesian_rss_hdf5.hpp"
#include "kspacejet/recon/runtime/noncartesian_rss_hdf5.hpp"

#include "kspacejet/base/status.hpp"
#include "kspacejet/base/version.hpp"
#include "kspacejet/logging/logging.hpp"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitReconstructionFailure = 5;
constexpr std::string_view kProgramName{"ksj-recon"};

enum class OutputFormat {
  text,
  json,
};

struct CartesianRssInvocation final {
  ksj::recon::runtime::CartesianRssHdf5ReconstructionConfig config{};
  OutputFormat output_format{OutputFormat::text};
};

struct NoncartesianRssInvocation final {
  ksj::recon::runtime::NoncartesianRssHdf5ReconstructionConfig config{};
  OutputFormat output_format{OutputFormat::text};
};

struct CartesianRssCliState final {
  CartesianRssInvocation invocation{};
  std::string requested_output_format{"text"};
  ksj::recon::runtime::CartesianRssHdf5NoisePrewhitenBranch noise_branch{};
  ksj::recon::runtime::CartesianRssHdf5PhaseCorrectionBranch phase_branch{};
  ksj::recon::runtime::CartesianRssHdf5CoilCompressionBranch coil_branch{};
  ksj::recon::runtime::CartesianRssHdf5ReadoutOversamplingRemoval crop_branch{};
  bool noise_requested{false};
  bool phase_requested{false};
  bool coil_requested{false};
  bool crop_requested{false};
  bool show_help{false};
};

struct NoncartesianRssCliState final {
  NoncartesianRssInvocation invocation{};
  std::string requested_output_format{"text"};
  bool show_help{false};
};

void write_json_string(std::ostream& stream, const std::string_view value) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  stream.put('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (character < 0x20U) {
          stream << "\\u00" << kHexDigits[(character >> 4U) & 0x0FU] << kHexDigits[character & 0x0FU];
        } else {
          stream.put(static_cast<char>(character));
        }
        break;
    }
  }
  stream.put('"');
}

[[nodiscard]] OutputFormat output_format_from(const std::string_view requested_format) noexcept {
  return requested_format == "json" ? OutputFormat::json : OutputFormat::text;
}

void print_cartesian_rss_usage(const OutputFormat format) {
  constexpr std::string_view kUsage =
    "ksj-recon cartesian-rss --input <scan.h5> --output <image.f32> "
    "--cartesian-provider <module> --cartesian-contract <contract.json> "
    "--coil-combine-provider <module> --coil-combine-contract <contract.json> "
    "[--noise-model-estimate-provider <module> --noise-model-estimate-contract <contract.json> "
    "--noise-prewhiten-provider <module> --noise-prewhiten-contract <contract.json>] "
    "[--phase-correction-estimate-provider <module> --phase-correction-estimate-contract <contract.json> "
    "--phase-correct-provider <module> --phase-correct-contract <contract.json>] "
    "[--coil-compression-basis-estimate-provider <module> "
    "--coil-compression-basis-estimate-contract <contract.json> --coil-compress-provider <module> "
    "--coil-compress-contract <contract.json> --virtual-channel-count <N>] "
    "[--readout-oversampling-remove-provider <module> "
    "--readout-oversampling-remove-contract <contract.json> --readout-offset <N>] "
    "[--metadata <image.json>] [--dataset dataset] [--format text|json]";
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.cartesian-rss-help\",\"usage\":";
    write_json_string(std::cout, kUsage);
    std::cout
      << ",\"constraints\":\"one 2-D Cartesian ISMRMRD frame; enabled calibration acquisitions are "
         "explicitly routed to their matching Provider operators; output is native-endian row-major float32 RSS\"}\n";
    return;
  }
  std::cout << "Usage: " << kUsage << "\n\n"
            << "The Cartesian path derives geometry from the ISMRMRD XML and acquisition headers. Optional noise "
               "prewhitening, phase correction, coil compression, and readout oversampling removal are all-or-nothing "
               "explicit Provider/OperatorContract pairs; no module discovery or fallback algorithm is used.\n";
}

void print_noncartesian_rss_usage(const OutputFormat format) {
  constexpr std::string_view kUsage = "ksj-recon noncartesian-rss --input <scan.h5> --output <image.f32> "
                                      "--noncartesian-provider <module> --noncartesian-contract <contract.json> "
                                      "--coil-combine-provider <module> --coil-combine-contract <contract.json> "
                                      "[--metadata <image.json>] [--dataset dataset] [--format text|json]";
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.noncartesian-rss-help\",\"usage\":";
    write_json_string(std::cout, kUsage);
    std::cout << ",\"constraints\":\"one 2-D non-Cartesian ISMRMRD frame with a finite two-coordinate trajectory "
                 "per acquisition; output is native-endian row-major float32 RSS\"}\n";
    return;
  }
  std::cout << "Usage: " << kUsage << "\n\n"
            << "The non-Cartesian path runs the explicit direct-adjoint reconstruction Provider and RSS coil "
               "combination. It does not infer density compensation, sensitivity maps, or a SENSE model.\n";
}

void print_all_usage() {
  print_cartesian_rss_usage(OutputFormat::text);
  std::cout << '\n';
  print_noncartesian_rss_usage(OutputFormat::text);
}

void print_error(const OutputFormat format, const std::string_view command, const ksj::base::Status& status) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet." << command << "-result\",\"ok\":false,\"code\":";
    write_json_string(std::cout, ksj::base::to_string(status.code()));
    std::cout << ",\"message\":";
    write_json_string(std::cout, status.message());
    std::cout << "}\n";
    return;
  }
  std::cerr << command << " reconstruction failed\n"
            << "  code: " << ksj::base::to_string(status.code()) << '\n'
            << "  message: " << status.message() << '\n';
}

void print_cartesian_success(const OutputFormat format, const CartesianRssInvocation& invocation,
                             const ksj::recon::runtime::CartesianRssHdf5ReconstructionReport& report) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.cartesian-rss-result\",\"ok\":true,\"input\":";
    write_json_string(std::cout, invocation.config.input_file.string());
    std::cout << ",\"output\":";
    write_json_string(std::cout, invocation.config.output_image_file.string());
    std::cout << ",\"metadata\":";
    write_json_string(std::cout, invocation.config.output_metadata_file.string());
    std::cout << ",\"rows\":" << report.rows << ",\"cols\":" << report.cols << ",\"channels\":" << report.channels
              << ",\"acquisitions_read\":" << report.acquisitions_read
              << ",\"image_payload_bytes\":" << report.image_payload_bytes << ",\"execution_plan_digest\":";
    write_json_string(std::cout, report.execution_plan_digest);
    std::cout << ",\"verification_record_digest\":";
    write_json_string(std::cout, report.verification_record_digest);
    std::cout << "}\n";
    return;
  }
  std::cout << "cartesian-rss reconstruction completed\n"
            << "  input: " << invocation.config.input_file.string() << '\n'
            << "  output: " << invocation.config.output_image_file.string() << '\n'
            << "  metadata: " << invocation.config.output_metadata_file.string() << '\n'
            << "  image: " << report.rows << " x " << report.cols << ", " << report.channels
            << " coil inputs, float32 RSS\n"
            << "  acquisitions read: " << report.acquisitions_read << '\n'
            << "  execution plan digest: " << report.execution_plan_digest << '\n'
            << "  verification record digest: " << report.verification_record_digest << '\n';
}

void print_noncartesian_success(const OutputFormat format, const NoncartesianRssInvocation& invocation,
                                const ksj::recon::runtime::NoncartesianRssHdf5ReconstructionReport& report) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.noncartesian-rss-result\",\"ok\":true,\"input\":";
    write_json_string(std::cout, invocation.config.input_file.string());
    std::cout << ",\"output\":";
    write_json_string(std::cout, invocation.config.output_image_file.string());
    std::cout << ",\"metadata\":";
    write_json_string(std::cout, invocation.config.output_metadata_file.string());
    std::cout << ",\"rows\":" << report.rows << ",\"cols\":" << report.cols << ",\"channels\":" << report.channels
              << ",\"acquisitions_read\":" << report.acquisitions_read << ",\"samples_read\":" << report.samples_read
              << ",\"image_payload_bytes\":" << report.image_payload_bytes << ",\"execution_plan_digest\":";
    write_json_string(std::cout, report.execution_plan_digest);
    std::cout << ",\"verification_record_digest\":";
    write_json_string(std::cout, report.verification_record_digest);
    std::cout << "}\n";
    return;
  }
  std::cout << "noncartesian-rss reconstruction completed\n"
            << "  input: " << invocation.config.input_file.string() << '\n'
            << "  output: " << invocation.config.output_image_file.string() << '\n'
            << "  metadata: " << invocation.config.output_metadata_file.string() << '\n'
            << "  image: " << report.rows << " x " << report.cols << ", " << report.channels
            << " coil inputs, float32 RSS\n"
            << "  acquisitions read: " << report.acquisitions_read << '\n'
            << "  samples read: " << report.samples_read << '\n'
            << "  execution plan digest: " << report.execution_plan_digest << '\n'
            << "  verification record digest: " << report.verification_record_digest << '\n';
}

template <typename Value>
CLI::Option* add_value_option(CLI::App& command, const std::string_view name, Value& value,
                              const std::string_view description) {
  return command.add_option(std::string(name), value, std::string(description))->take_last();
}

template <typename Value>
void add_branch_value_option(CLI::App& command, const std::string_view name, Value& value,
                             const std::string_view description, bool& branch_requested) {
  add_value_option(command, name, value, description)->each([&branch_requested](std::string) {
    branch_requested = true;
  });
}

void add_command_controls(CLI::App& command, std::string& requested_output_format, bool& show_help) {
  command.set_help_flag("");
  command.add_flag("-h,--help", show_help, "Print this command's help message and exit");
  auto* format = add_value_option(command, "--format", requested_output_format, "Output format: text or json");
  format->trigger_on_parse()->check(CLI::IsMember({"text", "json"}));
}

[[nodiscard]] CLI::App* register_cartesian_rss(CLI::App& application, CartesianRssCliState& state) {
  auto* command = application.add_subcommand("cartesian-rss", "Reconstruct one 2-D Cartesian RSS image from HDF5");
  add_command_controls(*command, state.requested_output_format, state.show_help);

  auto& config = state.invocation.config;
  add_value_option(*command, "--input", config.input_file, "Input ISMRMRD HDF5 scan");
  add_value_option(*command, "--output", config.output_image_file, "Output float32 RSS image");
  add_value_option(*command, "--metadata", config.output_metadata_file, "Output image metadata JSON sidecar");
  add_value_option(*command, "--cartesian-provider", config.cartesian_provider_module,
                   "Cartesian reconstruction Provider module");
  add_value_option(*command, "--cartesian-contract", config.cartesian_operator_contract,
                   "Cartesian reconstruction OperatorContract");
  add_value_option(*command, "--coil-combine-provider", config.coil_combine_provider_module,
                   "Coil-combine Provider module");
  add_value_option(*command, "--coil-combine-contract", config.coil_combine_operator_contract,
                   "Coil-combine OperatorContract");
  add_value_option(*command, "--dataset", config.dataset_group, "ISMRMRD HDF5 dataset group");

  add_branch_value_option(*command, "--noise-model-estimate-provider",
                          state.noise_branch.noise_model_estimate.provider_module,
                          "Noise-model-estimate Provider module", state.noise_requested);
  add_branch_value_option(*command, "--noise-model-estimate-contract",
                          state.noise_branch.noise_model_estimate.operator_contract,
                          "Noise-model-estimate OperatorContract", state.noise_requested);
  add_branch_value_option(*command, "--noise-prewhiten-provider", state.noise_branch.noise_prewhiten.provider_module,
                          "Noise-prewhiten Provider module", state.noise_requested);
  add_branch_value_option(*command, "--noise-prewhiten-contract", state.noise_branch.noise_prewhiten.operator_contract,
                          "Noise-prewhiten OperatorContract", state.noise_requested);
  add_branch_value_option(*command, "--phase-correction-estimate-provider",
                          state.phase_branch.phase_correction_estimate.provider_module,
                          "Phase-correction-estimate Provider module", state.phase_requested);
  add_branch_value_option(*command, "--phase-correction-estimate-contract",
                          state.phase_branch.phase_correction_estimate.operator_contract,
                          "Phase-correction-estimate OperatorContract", state.phase_requested);
  add_branch_value_option(*command, "--phase-correct-provider", state.phase_branch.phase_correct.provider_module,
                          "Phase-correct Provider module", state.phase_requested);
  add_branch_value_option(*command, "--phase-correct-contract", state.phase_branch.phase_correct.operator_contract,
                          "Phase-correct OperatorContract", state.phase_requested);
  add_branch_value_option(*command, "--coil-compression-basis-estimate-provider",
                          state.coil_branch.coil_compression_basis_estimate.provider_module,
                          "Coil-compression-basis-estimate Provider module", state.coil_requested);
  add_branch_value_option(*command, "--coil-compression-basis-estimate-contract",
                          state.coil_branch.coil_compression_basis_estimate.operator_contract,
                          "Coil-compression-basis-estimate OperatorContract", state.coil_requested);
  add_branch_value_option(*command, "--coil-compress-provider", state.coil_branch.coil_compress.provider_module,
                          "Coil-compress Provider module", state.coil_requested);
  add_branch_value_option(*command, "--coil-compress-contract", state.coil_branch.coil_compress.operator_contract,
                          "Coil-compress OperatorContract", state.coil_requested);
  add_branch_value_option(*command, "--virtual-channel-count", state.coil_branch.virtual_channel_count,
                          "Number of virtual channels after coil compression", state.coil_requested);
  add_branch_value_option(*command, "--readout-oversampling-remove-provider",
                          state.crop_branch.readout_oversampling_remove.provider_module,
                          "Readout-oversampling-removal Provider module", state.crop_requested);
  add_branch_value_option(*command, "--readout-oversampling-remove-contract",
                          state.crop_branch.readout_oversampling_remove.operator_contract,
                          "Readout-oversampling-removal OperatorContract", state.crop_requested);
  add_branch_value_option(*command, "--readout-offset", state.crop_branch.readout_offset,
                          "Samples removed from each readout side", state.crop_requested);
  return command;
}

[[nodiscard]] CLI::App* register_noncartesian_rss(CLI::App& application, NoncartesianRssCliState& state) {
  auto* command =
    application.add_subcommand("noncartesian-rss", "Reconstruct one 2-D non-Cartesian RSS image from HDF5");
  add_command_controls(*command, state.requested_output_format, state.show_help);

  auto& config = state.invocation.config;
  add_value_option(*command, "--input", config.input_file, "Input ISMRMRD HDF5 scan");
  add_value_option(*command, "--output", config.output_image_file, "Output float32 RSS image");
  add_value_option(*command, "--metadata", config.output_metadata_file, "Output image metadata JSON sidecar");
  add_value_option(*command, "--noncartesian-provider", config.noncartesian_provider_module,
                   "Non-Cartesian reconstruction Provider module");
  add_value_option(*command, "--noncartesian-contract", config.noncartesian_operator_contract,
                   "Non-Cartesian reconstruction OperatorContract");
  add_value_option(*command, "--coil-combine-provider", config.coil_combine_provider_module,
                   "Coil-combine Provider module");
  add_value_option(*command, "--coil-combine-contract", config.coil_combine_operator_contract,
                   "Coil-combine OperatorContract");
  add_value_option(*command, "--dataset", config.dataset_group, "ISMRMRD HDF5 dataset group");
  return command;
}

[[nodiscard]] std::optional<ksj::base::Status>
require_provider_selection(const bool requested, const ksj::recon::runtime::CartesianRssHdf5ProviderOperator& selection,
                           const std::string_view option_group) {
  if (requested && (selection.provider_module.empty() || selection.operator_contract.empty())) {
    return ksj::base::Status::InvalidArgument(std::string(option_group) +
                                              " requires both an explicit Provider module and OperatorContract");
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ksj::base::Status> validate_cartesian_optional_branches(CartesianRssCliState& state) {
  if (const auto status = require_provider_selection(state.noise_requested, state.noise_branch.noise_model_estimate,
                                                     "noise_model_estimate");
      status.has_value()) {
    return status;
  }
  if (const auto status =
        require_provider_selection(state.noise_requested, state.noise_branch.noise_prewhiten, "noise_prewhiten");
      status.has_value()) {
    return status;
  }
  if (const auto status = require_provider_selection(
        state.phase_requested, state.phase_branch.phase_correction_estimate, "phase_correction_estimate");
      status.has_value()) {
    return status;
  }
  if (const auto status =
        require_provider_selection(state.phase_requested, state.phase_branch.phase_correct, "phase_correct");
      status.has_value()) {
    return status;
  }
  if (const auto status = require_provider_selection(
        state.coil_requested, state.coil_branch.coil_compression_basis_estimate, "coil_compression_basis_estimate");
      status.has_value()) {
    return status;
  }
  if (const auto status =
        require_provider_selection(state.coil_requested, state.coil_branch.coil_compress, "coil_compress");
      status.has_value()) {
    return status;
  }
  if (const auto status = require_provider_selection(
        state.crop_requested, state.crop_branch.readout_oversampling_remove, "readout_oversampling_remove");
      status.has_value()) {
    return status;
  }
  if (state.coil_requested && state.coil_branch.virtual_channel_count == 0U) {
    return ksj::base::Status::InvalidArgument("coil_compression requires --virtual-channel-count in [1,64]");
  }

  if (state.noise_requested) {
    state.invocation.config.noise_prewhiten = std::move(state.noise_branch);
  }
  if (state.phase_requested) {
    state.invocation.config.phase_correction = std::move(state.phase_branch);
  }
  if (state.coil_requested) {
    state.invocation.config.coil_compression = std::move(state.coil_branch);
  }
  if (state.crop_requested) {
    state.invocation.config.readout_oversampling_removal = std::move(state.crop_branch);
  }
  return std::nullopt;
}

[[nodiscard]] int run_cartesian_rss(CartesianRssCliState& state) {
  auto& invocation = state.invocation;
  invocation.output_format = output_format_from(state.requested_output_format);
  if (state.show_help) {
    print_cartesian_rss_usage(invocation.output_format);
    return kExitSuccess;
  }
  if (const auto validation_error = validate_cartesian_optional_branches(state); validation_error.has_value()) {
    print_error(invocation.output_format, "cartesian-rss", *validation_error);
    if (invocation.output_format == OutputFormat::text) {
      print_cartesian_rss_usage(invocation.output_format);
    }
    return kExitUsage;
  }
  if (invocation.config.output_metadata_file.empty() && !invocation.config.output_image_file.empty()) {
    invocation.config.output_metadata_file = invocation.config.output_image_file.string() + ".json";
  }
  const auto result = ksj::recon::runtime::reconstruct_cartesian_rss_hdf5(invocation.config);
  if (!result.ok()) {
    if (result.status().code() != ksj::base::StatusCode::invalid_argument) {
      KSJ_LOG_ERROR("cartesian-rss reconstruction failed: {}", result.status());
    }
    print_error(invocation.output_format, "cartesian-rss", result.status());
    return result.status().code() == ksj::base::StatusCode::invalid_argument ? kExitUsage : kExitReconstructionFailure;
  }
  KSJ_LOG_INFO("cartesian-rss reconstruction completed: input=[{}], output=[{}]", invocation.config.input_file.string(),
               invocation.config.output_image_file.string());
  print_cartesian_success(invocation.output_format, invocation, result.value());
  return kExitSuccess;
}

[[nodiscard]] int run_noncartesian_rss(NoncartesianRssCliState& state) {
  auto& invocation = state.invocation;
  invocation.output_format = output_format_from(state.requested_output_format);
  if (state.show_help) {
    print_noncartesian_rss_usage(invocation.output_format);
    return kExitSuccess;
  }
  if (invocation.config.output_metadata_file.empty() && !invocation.config.output_image_file.empty()) {
    invocation.config.output_metadata_file = invocation.config.output_image_file.string() + ".json";
  }
  const auto result = ksj::recon::runtime::reconstruct_noncartesian_rss_hdf5(invocation.config);
  if (!result.ok()) {
    if (result.status().code() != ksj::base::StatusCode::invalid_argument) {
      KSJ_LOG_ERROR("noncartesian-rss reconstruction failed: {}", result.status());
    }
    print_error(invocation.output_format, "noncartesian-rss", result.status());
    return result.status().code() == ksj::base::StatusCode::invalid_argument ? kExitUsage : kExitReconstructionFailure;
  }
  KSJ_LOG_INFO("noncartesian-rss reconstruction completed: input=[{}], output=[{}]",
               invocation.config.input_file.string(), invocation.config.output_image_file.string());
  print_noncartesian_success(invocation.output_format, invocation, result.value());
  return kExitSuccess;
}

[[nodiscard]] int report_parse_error(const CLI::ParseError& error, const CartesianRssCliState& cartesian_state,
                                     const CLI::App& cartesian_command,
                                     const NoncartesianRssCliState& noncartesian_state,
                                     const CLI::App& noncartesian_command) {
  if (cartesian_command.parsed()) {
    const auto format = output_format_from(cartesian_state.requested_output_format);
    print_error(format, "cartesian-rss", ksj::base::Status::InvalidArgument(error.what()));
    if (format == OutputFormat::text) {
      print_cartesian_rss_usage(format);
    }
    return kExitUsage;
  }
  if (noncartesian_command.parsed()) {
    const auto format = output_format_from(noncartesian_state.requested_output_format);
    print_error(format, "noncartesian-rss", ksj::base::Status::InvalidArgument(error.what()));
    if (format == OutputFormat::text) {
      print_noncartesian_rss_usage(format);
    }
    return kExitUsage;
  }

  print_error(OutputFormat::text, "ksj-recon", ksj::base::Status::InvalidArgument(error.what()));
  print_all_usage();
  return kExitUsage;
}

} // namespace

int main(int argc, char* argv[]) {
  (void)ksj::logging::ConfigureDefaultConsole(kProgramName);
  KSJ_LOG_INFO("{} started", kProgramName);

  CartesianRssCliState cartesian_state;
  NoncartesianRssCliState noncartesian_state;
  bool show_help{false};
  bool show_version{false};

  CLI::App application{"KSpaceJet offline HDF5 reconstruction commands", "ksj-recon"};
  application.set_help_flag("");
  application.option_defaults()->multi_option_policy(CLI::MultiOptionPolicy::TakeLast);
  application.add_flag("-h,--help", show_help, "Print this help message and exit");
  application.add_flag("--version", show_version, "Print version information and exit");

  auto* cartesian_command = register_cartesian_rss(application, cartesian_state);
  auto* noncartesian_command = register_noncartesian_rss(application, noncartesian_state);
  application.require_subcommand(0, 1);

  try {
    application.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return report_parse_error(error, cartesian_state, *cartesian_command, noncartesian_state, *noncartesian_command);
  }

  if (show_help || argc == 1) {
    print_all_usage();
    return kExitSuccess;
  }
  if (show_version) {
    std::cout << "ksj-recon " << ksj::base::build_summary() << '\n';
    return kExitSuccess;
  }
  if (cartesian_command->parsed()) {
    return run_cartesian_rss(cartesian_state);
  }
  if (noncartesian_command->parsed()) {
    return run_noncartesian_rss(noncartesian_state);
  }

  print_all_usage();
  return kExitSuccess;
}
