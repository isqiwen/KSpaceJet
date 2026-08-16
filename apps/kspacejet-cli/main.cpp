#include "provider_init.hpp"

#include "kspacejet/base/status.hpp"
#include "kspacejet/base/version.hpp"
#include "kspacejet/logging/logging.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"

#include <CLI/CLI.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitInvalidPipeline = 3;
constexpr int kExitProviderInitFailure = 5;
constexpr std::uintmax_t kMaximumPipelineBytes = 64U * 1024U * 1024U;

constexpr std::string_view kProgramName{"ksj"};
constexpr std::string_view kProgramRole{"KSpaceJet command-line interface"};
constexpr std::string_view kProgramUsage{"ksj <command> [options]"};
constexpr std::string_view kProgramCommands{
  "Commands:\n"
  "  pipeline validate <pipeline.json> [--format text|json]  Validate a PipelineDefinition.\n"
  "  provider init <provider-slug> <operator-id> --output <parent-dir>  Create a Provider scaffold."};

enum class OutputFormat {
  text,
  json,
};

[[nodiscard]] OutputFormat output_format_for(const std::string_view value) noexcept {
  return value == "json" ? OutputFormat::json : OutputFormat::text;
}

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
      case '\b':
        stream << "\\b";
        break;
      case '\f':
        stream << "\\f";
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
          stream << "\\u00" << kHexDigits[(character >> 4U) & 0x0fU] << kHexDigits[character & 0x0fU];
        } else {
          stream.put(static_cast<char>(character));
        }
        break;
    }
  }
  stream.put('"');
}

void print_help(const OutputFormat format) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.program-help\",\"program\":";
    write_json_string(std::cout, kProgramName);
    std::cout << ",\"role\":";
    write_json_string(std::cout, kProgramRole);
    std::cout << ",\"usage\":";
    write_json_string(std::cout, kProgramUsage);
    std::cout << ",\"commands\":";
    write_json_string(std::cout, kProgramCommands);
    std::cout << "}\n";
    return;
  }

  std::cout << kProgramName << " — " << kProgramRole << "\n\n"
            << "Usage: " << kProgramUsage << "\n\n"
            << kProgramCommands << '\n';
}

void print_subcommand_help(const OutputFormat format, const std::string_view command_path, const CLI::App& command) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.program-help\",\"program\":";
    write_json_string(std::cout, kProgramName);
    std::cout << ",\"command\":";
    write_json_string(std::cout, command_path);
    std::cout << ",\"usage\":";
    write_json_string(std::cout, command.help());
    std::cout << "}\n";
    return;
  }

  std::cout << command.help();
}

void print_version(const OutputFormat format) {
  const auto build = ksj::base::build_info();
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.version\",\"program\":";
    write_json_string(std::cout, kProgramName);
    std::cout << ",\"project\":";
    write_json_string(std::cout, build.project_name);
    std::cout << ",\"version\":";
    write_json_string(std::cout, build.version);
    std::cout << ",\"compiler\":";
    write_json_string(std::cout, build.compiler);
    std::cout << ",\"build_type\":";
    write_json_string(std::cout, build.build_type);
    std::cout << "}\n";
    return;
  }

  std::cout << kProgramName << ' ' << ksj::base::build_summary() << '\n';
}

void print_cli_error(const OutputFormat format, const std::string_view message) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.error\",\"code\":\"invalid_argument\",\"message\":";
    write_json_string(std::cout, message);
    std::cout << "}\n";
    return;
  }

  std::cerr << "invalid_argument: " << message << '\n';
}

[[nodiscard]] std::string_view suggestion_for(const ksj::base::StatusCode code) noexcept {
  switch (code) {
    case ksj::base::StatusCode::parse_error:
      return "Provide well-formed JSON; PipelineDefinition rejects floating-point JSON values.";
    case ksj::base::StatusCode::validation_error:
      return "Correct the reported PipelineDefinition field and run validation again.";
    case ksj::base::StatusCode::io_error:
    case ksj::base::StatusCode::not_found:
      return "Provide a readable regular pipeline JSON file.";
    case ksj::base::StatusCode::invalid_argument:
      return "Use: ksj pipeline validate <pipeline.json> [--format text|json].";
    default:
      return "Inspect the diagnostic and retry after correcting the input.";
  }
}

void print_validation_error(const OutputFormat format, const std::string_view input_path,
                            const ksj::base::Status& status) {
  const auto code = ksj::base::to_string(status.code());
  const auto suggestion = suggestion_for(status.code());

  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.pipeline-validation-report\",\"valid\":false,\"input\":";
    write_json_string(std::cout, input_path);
    std::cout << ",\"diagnostics\":[{\"code\":";
    write_json_string(std::cout, code);
    std::cout << ",\"message\":";
    write_json_string(std::cout, status.message());
    std::cout << ",\"suggestion\":";
    write_json_string(std::cout, suggestion);
    std::cout << "}]}\n";
    return;
  }

  std::cerr << "PipelineDefinition validation failed\n"
            << "  input: " << input_path << '\n'
            << "  code: " << code << '\n'
            << "  message: " << status.message() << '\n'
            << "  suggestion: " << suggestion << '\n';
}

void print_validation_success(const OutputFormat format, const std::string_view input_path,
                              const ksj::recon::graph::PipelineDefinition& definition) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.pipeline-validation-report\",\"valid\":true,\"input\":";
    write_json_string(std::cout, input_path);
    std::cout << ",\"pipeline\":{\"id\":";
    write_json_string(std::cout, definition.id());
    std::cout << ",\"display_name\":";
    write_json_string(std::cout, definition.display_name());
    std::cout << "},\"canonical_digest\":";
    write_json_string(std::cout, definition.digest().value());
    std::cout << ",\"counts\":{\"providers\":" << definition.providers().size()
              << ",\"nodes\":" << definition.nodes().size() << ",\"edges\":" << definition.edges().size()
              << ",\"ingress_ports\":" << definition.ingress_ports().size()
              << ",\"egress_ports\":" << definition.egress_ports().size()
              << ",\"calibration_bindings\":" << definition.calibration_bindings().size() << "}}\n";
    return;
  }

  std::cout << "PipelineDefinition is valid\n"
            << "  input: " << input_path << '\n'
            << "  id: " << definition.id() << '\n'
            << "  display name: " << definition.display_name() << '\n'
            << "  canonical digest: " << definition.digest().value() << '\n'
            << "  providers: " << definition.providers().size() << '\n'
            << "  nodes: " << definition.nodes().size() << '\n'
            << "  edges: " << definition.edges().size() << '\n';
}

[[nodiscard]] ksj::base::Result<std::string> read_pipeline_file(const std::string_view input_path) {
  const std::filesystem::path path{std::string(input_path)};
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    if (error) {
      return ksj::base::Status::IoError("cannot inspect pipeline file '" + std::string(input_path) +
                                        "': " + error.message());
    }
    return ksj::base::Status::IoError("pipeline input '" + std::string(input_path) + "' is not a regular file");
  }

  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return ksj::base::Status::IoError("cannot determine pipeline file size for '" + std::string(input_path) +
                                      "': " + error.message());
  }
  if (size > kMaximumPipelineBytes) {
    return ksj::base::Status::InvalidArgument("pipeline input '" + std::string(input_path) + "' exceeds the " +
                                              std::to_string(kMaximumPipelineBytes) + " byte safety limit");
  }
  if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    return ksj::base::Status::InvalidArgument("pipeline input is too large for this process");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return ksj::base::Status::IoError("cannot open pipeline file '" + std::string(input_path) + "'");
  }

  std::string document(static_cast<std::size_t>(size), '\0');
  if (!document.empty()) {
    input.read(document.data(), static_cast<std::streamsize>(document.size()));
    if (!input) {
      return ksj::base::Status::IoError("cannot read complete pipeline file '" + std::string(input_path) + "'");
    }
  }
  return document;
}

[[nodiscard]] int run_pipeline_validate(const std::string_view input_path, const OutputFormat format) {
  if (input_path.empty()) {
    print_validation_error(
      format, "", ksj::base::Status::InvalidArgument("pipeline validate requires exactly one <pipeline.json> input"));
    return kExitUsage;
  }

  auto document = read_pipeline_file(input_path);
  if (!document.ok()) {
    KSJ_LOG_WARN("pipeline validation failed for [{}]: {}", input_path, document.status());
    print_validation_error(format, input_path, document.status());
    return document.status().code() == ksj::base::StatusCode::invalid_argument ? kExitUsage : kExitInvalidPipeline;
  }

  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document.value());
  if (!definition.ok()) {
    KSJ_LOG_WARN("pipeline validation failed for [{}]: {}", input_path, definition.status());
    print_validation_error(format, input_path, definition.status());
    return kExitInvalidPipeline;
  }
  KSJ_LOG_INFO("pipeline validation completed for [{}]", input_path);
  print_validation_success(format, input_path, definition.value());
  return kExitSuccess;
}

void print_provider_init_error(const OutputFormat format, const std::string_view provider_slug,
                               const std::string_view operator_id, const std::string_view message) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.provider-init-report\",\"created\":false,\"provider_slug\":";
    write_json_string(std::cout, provider_slug);
    std::cout << ",\"operator_id\":";
    write_json_string(std::cout, operator_id);
    std::cout << ",\"diagnostic\":{\"code\":\"provider_init_failed\",\"message\":";
    write_json_string(std::cout, message);
    std::cout << "}}\n";
    return;
  }

  std::cerr << "Provider initialization failed\n"
            << "  message: " << message << '\n';
}

void print_provider_init_success(const OutputFormat format, const std::string_view provider_slug,
                                 const std::string_view operator_id, const std::filesystem::path& provider_directory) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"kspacejet.provider-init-report\",\"created\":true,\"provider_slug\":";
    write_json_string(std::cout, provider_slug);
    std::cout << ",\"operator_id\":";
    write_json_string(std::cout, operator_id);
    std::cout << ",\"provider_directory\":";
    write_json_string(std::cout, provider_directory.string());
    std::cout << ",\"unresolved_contract_placeholders\":[\"@INPUT_PORT@\",\"@INPUT_TYPE_REF@\",\"@OUTPUT_PORT@\","
                 "\"@OUTPUT_TYPE_REF@\"]}\n";
    return;
  }

  std::cout << "Provider scaffold created\n"
            << "  provider: kspacejet-" << provider_slug << '\n'
            << "  operator: " << operator_id << '\n'
            << "  path: " << provider_directory.string() << '\n'
            << "  next: replace @INPUT_PORT@, @INPUT_TYPE_REF@, @OUTPUT_PORT@, and @OUTPUT_TYPE_REF@ in the "
               "OperatorContract.\n";
}

[[nodiscard]] int run_provider_init(const std::string_view provider_slug, const std::string_view operator_id,
                                    const std::string_view output_parent, const OutputFormat format) {
  if (provider_slug.empty() || operator_id.empty() || output_parent.empty()) {
    print_provider_init_error(format, provider_slug, operator_id,
                              "Use: ksj provider init <provider-slug> <operator-id> --output <parent-dir>.");
    return kExitUsage;
  }

  try {
    const auto result = ksj::cli::initialize_provider({
      .provider_slug = std::string(provider_slug),
      .operator_id = std::string(operator_id),
      .output_parent = std::filesystem::path{std::string(output_parent)},
    });
    if (result.outcome == ksj::cli::ProviderInitOutcome::success) {
      KSJ_LOG_INFO("created Provider scaffold [{}] for Operator [{}]", result.provider_directory.string(), operator_id);
      print_provider_init_success(format, provider_slug, operator_id, result.provider_directory);
      return kExitSuccess;
    }

    if (result.outcome != ksj::cli::ProviderInitOutcome::invalid_request) {
      KSJ_LOG_ERROR("Provider scaffold creation failed for [{}]/[{}]: {}", provider_slug, operator_id, result.message);
    }
    print_provider_init_error(format, provider_slug, operator_id, result.message);
    return result.outcome == ksj::cli::ProviderInitOutcome::invalid_request ? kExitUsage : kExitProviderInitFailure;
  } catch (const std::exception& error) {
    KSJ_LOG_ERROR("Provider scaffold creation raised an unexpected exception for [{}]/[{}]: {}", provider_slug,
                  operator_id, error.what());
    print_provider_init_error(format, provider_slug, operator_id,
                              std::string("unexpected Provider scaffold failure: ") + error.what());
    return kExitProviderInitFailure;
  }
}

} // namespace

int main(int argc, char* argv[]) {
  (void)ksj::logging::ConfigureDefaultConsole(kProgramName);
  KSJ_LOG_INFO("{} started", kProgramName);

  bool show_help = false;
  bool show_version = false;
  bool no_color = false;
  std::string requested_output_format{"text"};
  std::string pipeline_input;
  std::string provider_slug;
  std::string operator_id;
  std::string output_parent;

  CLI::App app{std::string(kProgramRole), std::string(kProgramName)};
  app.set_help_flag("");
  app.add_flag("-h,--help", show_help, "Show usage information");
  app.add_flag("--version", show_version, "Show version information");
  app.add_flag("--no-color", no_color, "Disable terminal color output");
  app.add_option("--format", requested_output_format, "Output format: text or json")
    ->check(CLI::IsMember({"text", "json"}))
    ->take_last();
  app.fallthrough();
  app.require_subcommand(0U, 1U);

  auto* const pipeline = app.add_subcommand("pipeline", "Pipeline tools");
  pipeline->fallthrough();
  auto* const pipeline_validate = pipeline->add_subcommand("validate", "Validate a PipelineDefinition JSON file");
  pipeline_validate->fallthrough();
  pipeline_validate->add_option("pipeline.json", pipeline_input, "PipelineDefinition JSON file");

  auto* const provider = app.add_subcommand("provider", "Provider development tools");
  provider->fallthrough();
  auto* const provider_init = provider->add_subcommand("init", "Create a Provider starter scaffold");
  provider_init->fallthrough();
  provider_init->add_option("provider-slug", provider_slug,
                            "Lowercase hyphenated Provider directory slug, without kspacejet-");
  provider_init->add_option("operator-id", operator_id, "Lowercase underscore-separated Operator identifier");
  provider_init->add_option("-o,--output", output_parent, "Existing directory that will contain the new Provider");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    print_cli_error(output_format_for(requested_output_format), error.what());
    return kExitUsage;
  }

  (void)no_color;
  const auto format = output_format_for(requested_output_format);
  if (show_help) {
    if (*provider_init) {
      print_subcommand_help(format, "provider init", *provider_init);
    } else if (*provider) {
      print_subcommand_help(format, "provider", *provider);
    } else if (*pipeline_validate) {
      print_subcommand_help(format, "pipeline validate", *pipeline_validate);
    } else if (*pipeline) {
      print_subcommand_help(format, "pipeline", *pipeline);
    } else {
      print_help(format);
    }
    return kExitSuccess;
  }
  if (show_version) {
    print_version(format);
    return kExitSuccess;
  }
  if (*pipeline_validate) {
    return run_pipeline_validate(pipeline_input, format);
  }
  if (*provider_init) {
    return run_provider_init(provider_slug, operator_id, output_parent, format);
  }
  if (*pipeline) {
    print_cli_error(format, "pipeline requires a subcommand; use 'ksj pipeline --help'.");
    return kExitUsage;
  }
  if (*provider) {
    print_cli_error(format, "provider requires a subcommand; use 'ksj provider --help'.");
    return kExitUsage;
  }

  print_help(format);
  return kExitSuccess;
}
