#include "inspection_session.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace {

[[nodiscard]] std::filesystem::path native_path(const QString& value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  return std::filesystem::u8path(value.toUtf8().constData());
#endif
}

} // namespace

namespace ksj::viewer {

bool InspectionSession::open_mrd(const QString& file_path, QString& error) {
  error.clear();

  const auto trimmed_path = file_path.trimmed();
  if (trimmed_path.isEmpty()) {
    error = QStringLiteral("an ISMRMRD file path is required");
    return false;
  }

  std::string native_error;
  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> next_containers;
  if (!ksj::ismrmrd::InspectionReader::discover_mrd_containers(native_path(trimmed_path), {}, next_containers,
                                                               native_error)) {
    error = QString::fromUtf8(native_error.data(), static_cast<qsizetype>(native_error.size()));
    return false;
  }

  if (next_containers.empty()) {
    error = QStringLiteral("no readable standard ISMRMRD container is available for inspection");
    return false;
  }
  const auto& selected = next_containers.front();

  const auto selected_path = QString::fromUtf8(selected.path.data(), static_cast<qsizetype>(selected.path.size()));
  ksj::ismrmrd::InspectionReader next_reader;
  if (!next_reader.open(native_path(trimmed_path), selected.path, {}, native_error)) {
    error = QString::fromUtf8(native_error.data(), static_cast<qsizetype>(native_error.size()));
    return false;
  }

  reader_ = std::move(next_reader);
  source_path_ = trimmed_path;
  container_path_ = selected_path;
  available_containers_ = std::move(next_containers);
  return true;
}

bool InspectionSession::select_container(const QString& container_path, QString& error) {
  error.clear();
  const auto trimmed_path = container_path.trimmed();
  if (!is_open() || source_path_.isEmpty()) {
    error = QStringLiteral("open an ISMRMRD source before selecting a container");
    return false;
  }
  if (trimmed_path.isEmpty()) {
    error = QStringLiteral("an ISMRMRD container path is required");
    return false;
  }

  const auto path_utf8 = trimmed_path.toUtf8();
  const auto selected =
    std::find_if(available_containers_.begin(), available_containers_.end(), [&path_utf8](const auto& descriptor) {
      return descriptor.path == path_utf8.constData();
    });
  if (selected == available_containers_.end()) {
    error = QStringLiteral("the selected container is not a readable standard ISMRMRD container");
    return false;
  }
  if (trimmed_path == container_path_) {
    return true;
  }

  std::string native_error;
  ksj::ismrmrd::InspectionReader next_reader;
  if (!next_reader.open(native_path(source_path_), selected->path, {}, native_error)) {
    error = QString::fromUtf8(native_error.data(), static_cast<qsizetype>(native_error.size()));
    return false;
  }

  reader_ = std::move(next_reader);
  container_path_ = trimmed_path;
  return true;
}

bool InspectionSession::is_open() const noexcept {
  return reader_.is_open();
}

const QString& InspectionSession::source_path() const noexcept {
  return source_path_;
}

const QString& InspectionSession::container_path() const noexcept {
  return container_path_;
}

const std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor>&
InspectionSession::available_containers() const noexcept {
  return available_containers_;
}

const ksj::ismrmrd::InspectionDatasetMetadata& InspectionSession::metadata() const noexcept {
  return reader_.metadata();
}

bool InspectionSession::read_object_attributes(
  const QString& container_path, const ksj::ismrmrd::InspectionObjectLocator& object,
  std::vector<ksj::ismrmrd::InspectionObjectAttributeDescriptor>& attributes, QString& error) {
  attributes.clear();
  error.clear();
  const auto trimmed_path = container_path.trimmed();
  if (!is_open() || source_path_.isEmpty()) {
    error = QStringLiteral("open an ISMRMRD source before inspecting object attributes");
    return false;
  }
  if (trimmed_path.isEmpty()) {
    error = QStringLiteral("an ISMRMRD container path is required for object attributes");
    return false;
  }

  const auto path_utf8 = trimmed_path.toUtf8();
  const auto selected =
    std::find_if(available_containers_.begin(), available_containers_.end(), [&path_utf8](const auto& descriptor) {
      return descriptor.path == path_utf8.constData();
    });
  if (selected == available_containers_.end()) {
    error = QStringLiteral("the selected container is not a readable standard ISMRMRD container");
    return false;
  }

  std::string native_error;
  if (trimmed_path == container_path_) {
    if (reader_.read_object_attributes(object, attributes, native_error)) {
      return true;
    }
  } else {
    ksj::ismrmrd::InspectionReader temporary_reader;
    if (temporary_reader.open(native_path(source_path_), selected->path, {}, native_error) &&
        temporary_reader.read_object_attributes(object, attributes, native_error)) {
      return true;
    }
  }
  attributes.clear();
  error = QString::fromUtf8(native_error.data(), static_cast<qsizetype>(native_error.size()));
  return false;
}

ksj::ismrmrd::InspectionReader& InspectionSession::reader() noexcept {
  return reader_;
}

const ksj::ismrmrd::InspectionReader& InspectionSession::reader() const noexcept {
  return reader_;
}

} // namespace ksj::viewer
