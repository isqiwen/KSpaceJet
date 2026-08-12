#include "kspacejet/program/program.hpp"

#include "kspacejet/base/version.hpp"

#include <iostream>
#include <string_view>

namespace ksj::program {
namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitServiceFailure = 5;

enum class OutputFormat {
  text,
  json,
};

void print_json_string(std::ostream& stream, std::string_view value) {
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

void print_help(const ProgramDescription& description, OutputFormat format) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema_version\":\"ksj.program-help.v1\",\"program\":";
    print_json_string(std::cout, description.executable_name);
    std::cout << ",\"role\":";
    print_json_string(std::cout, description.role);
    std::cout << ",\"usage\":";
    print_json_string(std::cout, description.usage);
    std::cout << ",\"commands\":";
    print_json_string(std::cout, description.commands);
    std::cout << ",\"status\":\"scaffold\"}\n";
    return;
  }

  std::cout << description.executable_name << " — " << description.role << "\n\n"
            << "Usage: " << description.usage << "\n\n"
            << description.commands << "\n\n"
            << "This executable is a KSpaceJet application skeleton. Use --version or --help; "
               "operational commands are enabled as their shared runtime contracts are implemented.\n";
}

void print_version(const ProgramDescription& description, OutputFormat format) {
  const auto build = ksj::base::build_info();
  if (format == OutputFormat::json) {
    std::cout << "{\"schema_version\":\"ksj.version.v1\",\"program\":";
    print_json_string(std::cout, description.executable_name);
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

  std::cout << description.executable_name << ' ' << ksj::base::build_summary() << '\n';
}

void print_error(OutputFormat format, std::string_view code, std::string_view message) {
  if (format == OutputFormat::json) {
    std::cerr << "{\"schema_version\":\"ksj.error.v1\",\"code\":";
    print_json_string(std::cerr, code);
    std::cerr << ",\"message\":";
    print_json_string(std::cerr, message);
    std::cerr << "}\n";
    return;
  }

  std::cerr << code << ": " << message << '\n';
}

} // namespace

int run_program(int argc, char* argv[], const ProgramDescription& description) {
  OutputFormat format = OutputFormat::text;
  bool show_help = argc == 1;
  bool show_version = false;
  bool has_operational_request = false;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--help" || argument == "-h" || argument == "help") {
      show_help = true;
      continue;
    }
    if (argument == "--version" || argument == "version") {
      show_version = true;
      continue;
    }
    if (argument == "--format") {
      if (++index >= argc) {
        print_error(format, "invalid_argument", "--format requires text or json");
        return kExitUsage;
      }
      const std::string_view requested_format{argv[index]};
      if (requested_format == "text") {
        format = OutputFormat::text;
      } else if (requested_format == "json") {
        format = OutputFormat::json;
      } else {
        print_error(format, "invalid_argument", "--format accepts only text or json");
        return kExitUsage;
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
        print_error(format, "invalid_argument", "--format accepts only text or json");
        return kExitUsage;
      }
      continue;
    }
    // Application-specific command and option schemas intentionally belong to
    // their future shared command libraries. Preserve all other arguments as
    // an operational request so a documented invocation such as
    // `ksj-gateway --config gateway.json` reaches the stable scaffold result rather
    // than failing in this common parser.
    has_operational_request = true;
  }

  if (show_help) {
    print_help(description, format);
    return kExitSuccess;
  }
  if (show_version) {
    print_version(description, format);
    return kExitSuccess;
  }

  if (has_operational_request) {
    print_error(format, "unimplemented",
                "the requested operational behavior is not available until its shared runtime is implemented");
    return kExitServiceFailure;
  }

  print_help(description, format);
  return kExitSuccess;
}

} // namespace ksj::program
