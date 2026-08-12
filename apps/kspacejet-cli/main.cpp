#include "kspacejet/program/program.hpp"
#include "kspacejet/recon/graph/pipeline_definition.hpp"

#include "kspacejet/base/status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitInvalidPipeline = 3;
constexpr std::uintmax_t kMaximumPipelineBytes = 64U * 1024U * 1024U;

enum class OutputFormat {
  text,
  json,
};

struct PipelineValidateInvocation {
  std::string input_path;
  OutputFormat output_format{OutputFormat::text};
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

[[nodiscard]] std::string_view suggestion_for(const ksj::base::StatusCode code) noexcept {
  switch (code) {
    case ksj::base::StatusCode::parse_error:
      return "Provide well-formed JSON; PipelineDefinition v1 rejects floating-point JSON values.";
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
    std::cerr << "{\"schema_version\":\"kspacejet.pipeline-validation-report/v1\",\"valid\":false,\"input\":";
    write_json_string(std::cerr, input_path);
    std::cerr << ",\"diagnostics\":[{\"code\":";
    write_json_string(std::cerr, code);
    std::cerr << ",\"message\":";
    write_json_string(std::cerr, status.message());
    std::cerr << ",\"suggestion\":";
    write_json_string(std::cerr, suggestion);
    std::cerr << "}]}\n";
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
    std::cout << "{\"schema_version\":\"kspacejet.pipeline-validation-report/v1\",\"valid\":true,\"input\":";
    write_json_string(std::cout, input_path);
    std::cout << ",\"pipeline\":{\"id\":";
    write_json_string(std::cout, definition.id());
    std::cout << ",\"revision\":";
    write_json_string(std::cout, definition.revision());
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
            << "  revision: " << definition.revision() << '\n'
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

[[nodiscard]] std::optional<int> try_run_pipeline_validate(const int argc, char* argv[]) {
  std::vector<std::string_view> positional_arguments;
  OutputFormat format = OutputFormat::text;
  std::optional<std::string> option_error;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help" || argument == "-h" || argument == "--version" || argument == "version") {
      // Retain the shared skeleton's global help/version behaviour unchanged.
      return std::nullopt;
    }
    if (argument == "--no-color") {
      continue;
    }
    if (argument == "--format") {
      if (++index >= argc) {
        option_error = "--format requires text or json";
        break;
      }
      const std::string_view requested_format{argv[index]};
      if (requested_format == "text") {
        format = OutputFormat::text;
      } else if (requested_format == "json") {
        format = OutputFormat::json;
      } else {
        option_error = "--format accepts only text or json";
      }
      continue;
    }
    constexpr std::string_view kFormatPrefix{"--format="};
    if (argument.starts_with(kFormatPrefix)) {
      const auto requested_format = argument.substr(kFormatPrefix.size());
      if (requested_format == "text") {
        format = OutputFormat::text;
      } else if (requested_format == "json") {
        format = OutputFormat::json;
      } else {
        option_error = "--format accepts only text or json";
      }
      continue;
    }
    positional_arguments.push_back(argument);
  }

  if (positional_arguments.size() < 2U || positional_arguments[0] != "pipeline" ||
      positional_arguments[1] != "validate") {
    return std::nullopt;
  }

  if (option_error.has_value()) {
    print_validation_error(format, "", ksj::base::Status::InvalidArgument(std::move(*option_error)));
    return kExitUsage;
  }
  if (positional_arguments.size() != 3U) {
    print_validation_error(
      format, "", ksj::base::Status::InvalidArgument("pipeline validate requires exactly one <pipeline.json> input"));
    return kExitUsage;
  }

  const PipelineValidateInvocation invocation{
    .input_path = std::string(positional_arguments[2]),
    .output_format = format,
  };
  auto document = read_pipeline_file(invocation.input_path);
  if (!document.ok()) {
    print_validation_error(invocation.output_format, invocation.input_path, document.status());
    return document.status().code() == ksj::base::StatusCode::invalid_argument ? kExitUsage : kExitInvalidPipeline;
  }

  auto definition = ksj::recon::graph::PipelineDefinition::parse_json(document.value());
  if (!definition.ok()) {
    print_validation_error(invocation.output_format, invocation.input_path, definition.status());
    return kExitInvalidPipeline;
  }
  print_validation_success(invocation.output_format, invocation.input_path, definition.value());
  return kExitSuccess;
}

} // namespace

int main(int argc, char* argv[]) {
  if (const auto pipeline_validate_exit_code = try_run_pipeline_validate(argc, argv);
      pipeline_validate_exit_code.has_value()) {
    return *pipeline_validate_exit_code;
  }

  constexpr ksj::program::ProgramDescription kDescription{
    .executable_name = "ksj",
    .role = "KSpaceJet command-line interface",
    .usage = "ksj <command> [options] [--format text|json]",
    .commands = "Commands: version, config, inspect, dataset, stream, pipeline, run, scan, plugin, trace, benchmark, "
                "doctor, support-bundle",
  };
  return ksj::program::run_program(argc, argv, kDescription);
}
