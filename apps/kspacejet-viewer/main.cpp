#include "src/viewer_window.hpp"
#include "src/viewer_theme.hpp"
#include "src/visualization_derivative_export.hpp"

#include "kspacejet/base/version.hpp"
#include "kspacejet/logging/logging.hpp"

#include <CLI/CLI.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitUsage = 2;
constexpr int kExitUiFailure = 5;

constexpr std::string_view kProgramName{"ksj-viewer"};
constexpr std::string_view kProgramRole{"KSpaceJet offline inspection desktop application"};
constexpr std::string_view kProgramUsage{"ksj-viewer [--ui-smoke|--export-smoke] [--format text|json]"};
constexpr std::string_view kProgramCommands{
  "Launch the local Qt inspection application. It opens standard ISMRMRD HDF5 data and PipelineDefinition files "
  "through the desktop UI, creates bounded metadata/k-space/image/pipeline views, and exports only explicitly "
  "labelled visualization derivatives. It does not reconstruct, load Providers, or connect a gateway."};

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
    std::cout << ",\"status\":\"inspection\",\"availability\":\"desktop\","
                 "\"operations\":\"metadata,k-space,image,pipeline,visualization-derivative-export\"}\n";
    return;
  }

  std::cout << kProgramName << " - " << kProgramRole << "\n\n"
            << "Usage: " << kProgramUsage << "\n\n"
            << kProgramCommands << '\n';
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

void print_error(const OutputFormat format, const std::string_view code, const std::string_view message) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.error\",\"code\":";
    write_json_string(std::cout, code);
    std::cout << ",\"message\":";
    write_json_string(std::cout, message);
    std::cout << "}\n";
    return;
  }

  std::cerr << code << ": " << message << '\n';
}

void print_ui_smoke_result(const OutputFormat format) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.viewer-ui-smoke\",\"program\":";
    write_json_string(std::cout, kProgramName);
    std::cout << ",\"ok\":true,\"status\":\"inspection\"}\n";
    return;
  }

  std::cout << "ksj-viewer UI smoke passed\n";
}

void print_export_smoke_result(const OutputFormat format) {
  if (format == OutputFormat::json) {
    std::cout << "{\"schema\":\"ksj.viewer-export-smoke\",\"program\":";
    write_json_string(std::cout, kProgramName);
    std::cout << ",\"ok\":true,\"status\":\"inspection\",\"artifact_kind\":\"visualization-derivative\"}\n";
    return;
  }

  std::cout << "ksj-viewer visualization-derivative export smoke passed\n";
}

[[nodiscard]] bool run_export_smoke(QString& error) {
  QTemporaryDir directory;
  if (!directory.isValid()) {
    error = QStringLiteral("could not create a temporary directory for the display-derivative export smoke");
    return false;
  }

  QImage image(4, 3, QImage::Format_Grayscale8);
  image.fill(0U);
  image.setPixel(3, 2, 255U);

  ksj::viewer::VisualizationDerivative derivative;
  derivative.view_name = QStringLiteral("export-smoke");
  derivative.source_description = QStringLiteral("ksj-viewer internal display derivative smoke");
  derivative.image = image;
  derivative.details.insert(QStringLiteral("smoke"), true);

  const auto destination = directory.filePath(QStringLiteral("viewer-display-derivative.png"));
  if (!ksj::viewer::export_visualization_derivative(derivative, destination,
                                                    ksj::viewer::VisualizationExportFormat::png, error)) {
    return false;
  }

  QImageReader reader(destination);
  const auto artifact_kind = reader.text(QStringLiteral("KSpaceJet.ArtifactKind"));
  const auto read_back = reader.read();
  if (read_back.isNull() || read_back.size() != image.size()) {
    error = QStringLiteral("could not read back the PNG visualization derivative");
    return false;
  }
  if (artifact_kind != QStringLiteral("visualization-derivative")) {
    error = QStringLiteral("PNG visualization derivative provenance was not preserved");
    return false;
  }
  return true;
}

[[nodiscard]] int run_ui(int& argc, char** argv, const bool ui_smoke, const bool export_smoke,
                         const OutputFormat format) {
  QApplication application(argc, argv);
  QCoreApplication::setApplicationName(
    QString::fromUtf8(kProgramName.data(), static_cast<qsizetype>(kProgramName.size())));
  ksj::viewer::apply_viewer_theme(application);

  ksj::viewer::ViewerWindow window;
  window.show();

  if (export_smoke) {
    QString error;
    if (!run_export_smoke(error)) {
      print_error(format, "export_failure", error.toUtf8().toStdString());
      return kExitUiFailure;
    }
  }
  if (ui_smoke || export_smoke) {
    QTimer::singleShot(0, &application, &QCoreApplication::quit);
  }

  if (application.exec() != 0) {
    print_error(format, "ui_failure", "the Qt event loop returned a non-zero result");
    return kExitUiFailure;
  }

  if (ui_smoke) {
    print_ui_smoke_result(format);
  }
  if (export_smoke) {
    print_export_smoke_result(format);
  }
  return kExitSuccess;
}

} // namespace

int main(int argc, char* argv[]) {
  (void)ksj::logging::ConfigureDefaultConsole(kProgramName);
  KSJ_LOG_INFO("{} started", kProgramName);

  bool show_help = false;
  bool show_version = false;
  bool ui_smoke = false;
  bool export_smoke = false;
  std::string format{"text"};

  CLI::App app{std::string(kProgramRole), std::string(kProgramName)};
  app.set_help_flag("");
  app.add_flag("-h,--help", show_help, "Show usage information");
  app.add_flag("--version", show_version, "Show version information");
  app.add_flag("--ui-smoke", ui_smoke, "Create and close the Qt main window for deployment verification");
  app.add_flag("--export-smoke", export_smoke,
               "Create, write, and read back a PNG visualization derivative for deployment verification");
  app.add_option("--format", format, "Output format: text or json")
    ->check(CLI::IsMember({"text", "json"}))
    ->take_last();

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    print_error(output_format_for(format), "invalid_argument", error.what());
    return kExitUsage;
  }

  const auto format_value = output_format_for(format);
  if (show_help) {
    print_help(format_value);
    return kExitSuccess;
  }
  if (show_version) {
    print_version(format_value);
    return kExitSuccess;
  }
  if (ui_smoke && export_smoke) {
    print_error(format_value, "invalid_argument", "--ui-smoke and --export-smoke are mutually exclusive");
    return kExitUsage;
  }

  return run_ui(argc, argv, ui_smoke, export_smoke, format_value);
}
