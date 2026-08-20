#include "kspacejet/base/version.hpp"
#include "kspacejet/logging/logging.hpp"

#include <CLI/CLI.hpp>

#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitOperationUnavailable = 5;

constexpr std::string_view kProgramName{"ksj-research"};
constexpr std::string_view kProgramRole{"KSpaceJet installed research scaffold"};
constexpr std::string_view kProgramUsage{
  "ksj-research <lock|dataset|schedule|case|run|report|claims> [options] [--format text|json]"};
constexpr std::string_view kProgramCommands{
  "Reserved command groups: lock, dataset, schedule, case, run, report, and claims; every operation is "
  "unimplemented"};
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
    std::cout << ",\"status\":\"scaffold\",\"availability\":\"reserved\",\"operations\":\"unimplemented\"}\n";
    return;
  }

  std::cout << kProgramName << " — " << kProgramRole << "\n\n"
            << "Usage: " << kProgramUsage << "\n\n"
            << kProgramCommands << "\n\n"
            << "This executable is an installed scaffold. The listed commands are reserved and every "
               "operational action is unimplemented.\n";
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

  CLI::App app{std::string(kProgramRole), std::string(kProgramName)};
  app.set_help_flag("");
  app.add_flag("-h,--help", show_help, "Show usage information");
  app.add_flag("--version", show_version, "Show version information");
  app.add_option("--format", format, "Output format: text or json")
    ->check(CLI::IsMember({"text", "json"}))
    ->take_last();

  auto* lock = app.add_subcommand("lock", "Reserved scaffold command group");
  lock->fallthrough();
  auto* lock_verify = lock->add_subcommand("verify", "Reserved scaffold action (unimplemented)");
  lock_verify->fallthrough();

  auto* dataset = app.add_subcommand("dataset", "Reserved scaffold command group");
  dataset->fallthrough();
  auto* dataset_freeze = dataset->add_subcommand("freeze", "Reserved scaffold action (unimplemented)");
  dataset_freeze->fallthrough();

  auto* schedule = app.add_subcommand("schedule", "Reserved scaffold command group");
  schedule->fallthrough();
  auto* schedule_compile = schedule->add_subcommand("compile", "Reserved scaffold action (unimplemented)");
  schedule_compile->fallthrough();

  auto* case_command = app.add_subcommand("case", "Reserved scaffold command group");
  case_command->fallthrough();
  auto* case_compile = case_command->add_subcommand("compile", "Reserved scaffold action (unimplemented)");
  case_compile->fallthrough();

  auto* run = app.add_subcommand("run", "Reserved scaffold action (unimplemented)");
  run->fallthrough();

  auto* report = app.add_subcommand("report", "Reserved scaffold action (unimplemented)");
  report->fallthrough();

  auto* claims = app.add_subcommand("claims", "Reserved scaffold command group");
  claims->fallthrough();
  auto* claims_audit = claims->add_subcommand("audit", "Reserved scaffold action (unimplemented)");
  claims_audit->fallthrough();

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
  const bool has_operation = static_cast<bool>(*lock_verify) || static_cast<bool>(*dataset_freeze) ||
                             static_cast<bool>(*schedule_compile) || static_cast<bool>(*case_compile) ||
                             static_cast<bool>(*run) || static_cast<bool>(*report) || static_cast<bool>(*claims_audit);
  if (has_operation) {
    print_error(format_value, "unimplemented", "the requested reserved scaffold operation is unimplemented");
    return kExitOperationUnavailable;
  }
  if (!app.get_subcommands().empty()) {
    print_error(format_value, "invalid_argument", "the selected reserved scaffold command requires an action");
    return kExitUsage;
  }

  print_help(format_value);
  return kExitSuccess;
}
