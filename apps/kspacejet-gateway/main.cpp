#include "kspacejet/base/version.hpp"
#include "kspacejet/logging/logging.hpp"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitServiceFailure = 5;

constexpr std::string_view kProgramName{"ksj-gateway"};
constexpr std::string_view kProgramRole{"KSpaceJet external-system integration gateway"};
constexpr std::string_view kProgramUsage{"ksj-gateway --config <gateway.json> [--format text|json]"};
constexpr std::string_view kProgramCommands{
  "Responsibilities: external-session authentication, connector supervision, routing, and transparent "
  "public MRD/ISMRMRD session forwarding"};

enum class OutputFormat {
  text,
  json,
};

[[nodiscard]] OutputFormat output_format_for(const std::string_view value) noexcept {
  return value == "json" ? OutputFormat::json : OutputFormat::text;
}

void print_json_string(std::ostream& stream, const std::string_view value) {
  stream.put('"');
  for (const char character : value) {
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
        stream.put(character);
        break;
    }
  }
  stream.put('"');
}

void print_help(const OutputFormat format) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.program-help\",\"program\":";
    print_json_string(std::cout, kProgramName);
    std::cout << ",\"role\":";
    print_json_string(std::cout, kProgramRole);
    std::cout << ",\"usage\":";
    print_json_string(std::cout, kProgramUsage);
    std::cout << ",\"commands\":";
    print_json_string(std::cout, kProgramCommands);
    std::cout << ",\"status\":\"scaffold\"}\n";
    return;
  }

  std::cout << kProgramName << " — " << kProgramRole << "\n\n"
            << "Usage: " << kProgramUsage << "\n\n"
            << kProgramCommands << "\n\n"
            << "This executable is a KSpaceJet application skeleton. Use --version or --help; "
               "operational commands are enabled as their shared runtime contracts are implemented.\n";
}

void print_version(const OutputFormat format) {
  const auto build = ksj::base::build_info();
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.version\",\"program\":";
    print_json_string(std::cout, kProgramName);
    std::cout << ",\"project\":";
    print_json_string(std::cout, build.project_name);
    std::cout << ",\"version\":";
    print_json_string(std::cout, build.version);
    std::cout << ",\"compiler\":";
    print_json_string(std::cout, build.compiler);
    std::cout << ",\"build_type\":";
    print_json_string(std::cout, build.build_type);
    std::cout << "}\n";
    return;
  }

  std::cout << kProgramName << ' ' << ksj::base::build_summary() << '\n';
}

void print_error(const OutputFormat format, const std::string_view code, const std::string_view message) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.error\",\"code\":";
    print_json_string(std::cout, code);
    std::cout << ",\"message\":";
    print_json_string(std::cout, message);
    std::cout << "}\n";
    return;
  }

  std::cerr << code << ": " << message << '\n';
}

} // namespace

int main(int argc, char* argv[]) {
  (void)ksj::logging::ConfigureDefaultConsole(kProgramName);
  KSJ_LOG_INFO("{} started", kProgramName);

  bool show_help = false;
  bool show_version = false;
  std::string format{"text"};
  std::string config_path;

  CLI::App app{std::string(kProgramRole), std::string(kProgramName)};
  app.set_help_flag("");
  app.add_flag("-h,--help", show_help, "Show usage information");
  app.add_flag("--version", show_version, "Show version information");
  app.add_option("--format", format, "Output format: text or json")
    ->check(CLI::IsMember({"text", "json"}))
    ->take_last();
  const auto* config_option = app.add_option("--config", config_path, "Gateway configuration JSON file");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    print_error(output_format_for(format), "invalid_argument", error.what());
    return kExitUsage;
  }

  const auto format_value = output_format_for(format);

  if (show_help || (argc == 1)) {
    print_help(format_value);
    return kExitSuccess;
  }
  if (show_version) {
    print_version(format_value);
    return kExitSuccess;
  }
  if (config_option->count() != 0U) {
    print_error(format_value, "unimplemented",
                "the requested operational behavior is not available until its shared runtime is implemented");
    return kExitServiceFailure;
  }

  print_help(format_value);
  return kExitSuccess;
}
