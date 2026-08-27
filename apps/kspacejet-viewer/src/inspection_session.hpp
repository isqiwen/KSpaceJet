#pragma once

#include "kspacejet/ismrmrd/inspection_reader.hpp"

#include <QString>

#include <vector>

namespace ksj::viewer {

// Owns one read-only InspectionReader and the user-selected standard MRD
// container identity.
// It deliberately exposes no retained acquisition or image-pixel payload:
// callers must build a bounded display derivative inside the reader callback.
class InspectionSession final {
public:
  InspectionSession() = default;

  InspectionSession(const InspectionSession&) = delete;
  InspectionSession& operator=(const InspectionSession&) = delete;
  InspectionSession(InspectionSession&&) noexcept = default;
  InspectionSession& operator=(InspectionSession&&) noexcept = default;

  // Opens a source by recursively discovering its bounded standard MRD
  // containers and selecting the first readable container, including a
  // standalone standard image series. This never falls back to a guessed path
  // such as "/dataset".
  [[nodiscard]] bool open_mrd(const QString& file_path, QString& error);

  // Changes only to a readable container previously discovered from
  // source_path(). The current reader remains intact if that container can no
  // longer open.
  [[nodiscard]] bool select_container(const QString& container_path, QString& error);
  [[nodiscard]] bool is_open() const noexcept;

  [[nodiscard]] const QString& source_path() const noexcept;
  [[nodiscard]] const QString& container_path() const noexcept;
  [[nodiscard]] const std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor>&
  available_containers() const noexcept;
  [[nodiscard]] const ksj::ismrmrd::InspectionDatasetMetadata& metadata() const noexcept;

  [[nodiscard]] ksj::ismrmrd::InspectionReader& reader() noexcept;
  [[nodiscard]] const ksj::ismrmrd::InspectionReader& reader() const noexcept;

private:
  ksj::ismrmrd::InspectionReader reader_;
  QString source_path_;
  QString container_path_;
  std::vector<ksj::ismrmrd::InspectionMrdContainerDescriptor> available_containers_;
};

} // namespace ksj::viewer
