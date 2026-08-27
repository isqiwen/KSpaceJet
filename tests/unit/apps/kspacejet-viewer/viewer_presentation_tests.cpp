#include "inspection_session.hpp"
#include "inspection_canvas.hpp"
#include "arrshow_display.hpp"
#include "arrshow_dimension_controls.hpp"
#include "viewer_presentation.hpp"
#include "viewer_theme.hpp"
#include "viewer_window.hpp"
#include "visualization_derivative_export.hpp"

#include "kspacejet/recon/graph/canonical_json.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointF>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QWidget>

#include <hdf5.h>
#include <ismrmrd/dataset.h>
#include <ismrmrd/ismrmrd.h>
#include <ismrmrd/waveform.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kDatasetGroup{"dataset"};
constexpr std::string_view kImageSeries{"viewer_series"};
constexpr std::string_view kComplexImageSeries{"viewer_complex_series"};
constexpr std::string_view kFixedAxisImageSeries{"viewer_fixed_axis_series"};
constexpr std::string_view kXmlHeader{"<ismrmrdHeader xmlns=\"http://www.ismrm.org/ISMRMRD\"><experimentalConditions>"
                                      "<H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz>"
                                      "</experimentalConditions></ismrmrdHeader>"};
constexpr std::uint16_t kSourceImageWidth = 2'049U;
constexpr std::uint16_t kSourceImageHeight = 1'025U;

struct CartesianKspaceTestPlaneIndices final {
  std::uint16_t encoding_space_ref{0U};
  std::uint16_t kspace_encode_step_2{0U};
  std::uint16_t average{0U};
  std::uint16_t slice{0U};
  std::uint16_t contrast{0U};
  std::uint16_t phase{0U};
  std::uint16_t repetition{0U};
  std::uint16_t set{0U};
  std::uint16_t segment{0U};
  std::array<std::uint16_t, 8U> user{};
  std::uint64_t flags{0U};
};

[[nodiscard]] constexpr std::uint64_t acquisition_flag_mask(const ISMRMRD::ISMRMRD_AcquisitionFlags flag) noexcept {
  return std::uint64_t{1U} << (static_cast<std::uint8_t>(flag) - 1U);
}

constexpr std::string_view kCartesianKspaceXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace>
      <matrixSize><x>4</x><y>3</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>200</x><y>150</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>4</x><y>3</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>200</x><y>150</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <kspace_encoding_step_1><minimum>0</minimum><maximum>2</maximum><center>1</center></kspace_encoding_step_1>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";

constexpr std::string_view kCartesianKspacePartitionLimitedXml = R"xml(
<ismrmrdHeader xmlns="http://www.ismrm.org/ISMRMRD">
  <experimentalConditions><H1resonanceFrequency_Hz>123456789</H1resonanceFrequency_Hz></experimentalConditions>
  <acquisitionSystemInformation><receiverChannels>2</receiverChannels></acquisitionSystemInformation>
  <encoding>
    <trajectory>cartesian</trajectory>
    <encodedSpace>
      <matrixSize><x>4</x><y>3</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>200</x><y>150</y><z>5</z></fieldOfView_mm>
    </encodedSpace>
    <reconSpace>
      <matrixSize><x>4</x><y>3</y><z>1</z></matrixSize>
      <fieldOfView_mm><x>200</x><y>150</y><z>5</z></fieldOfView_mm>
    </reconSpace>
    <encodingLimits>
      <kspace_encoding_step_1><minimum>0</minimum><maximum>2</maximum><center>1</center></kspace_encoding_step_1>
      <kspace_encoding_step_2><minimum>0</minimum><maximum>0</maximum><center>0</center></kspace_encoding_step_2>
    </encodingLimits>
  </encoding>
</ismrmrdHeader>
)xml";

[[nodiscard]] std::filesystem::path native_path(const QString& value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  const auto utf8 = value.toUtf8();
  return std::filesystem::path(utf8.constData());
#endif
}

using CartesianKspaceDimension = ksj::viewer::CartesianKspaceDimension;
using CartesianKspaceAxes = ksj::viewer::CartesianKspaceAxes;
using CartesianKspaceCoordinate = ksj::viewer::CartesianKspaceCoordinate;
using CartesianKspaceRequest = ksj::viewer::CartesianKspaceRequest;
using CartesianKspaceKind = ksj::viewer::CartesianKspaceAcquisitionKind;
using ImageCoordinate = ksj::viewer::ImageCoordinate;
using ImageDimension = ksj::viewer::ImageDimension;
using ImageRequest = ksj::viewer::ImageRequest;

[[nodiscard]] CartesianKspaceRequest
make_cartesian_kspace_request(const CartesianKspaceKind kind = CartesianKspaceKind::imaging, const int coil = 0,
                              const ksj::viewer::ArrShowDisplaySettings display_settings = {}) {
  CartesianKspaceCoordinate coordinate;
  ksj::viewer::set_cartesian_kspace_coordinate_value(coordinate, CartesianKspaceDimension::coil, coil);
  return {.acquisition_kind = kind, .coordinate = coordinate, .display_settings = display_settings};
}

[[nodiscard]] CartesianKspaceRequest
make_cartesian_kspace_request(const CartesianKspaceKind kind, const CartesianKspaceAxes axes,
                              const CartesianKspaceCoordinate coordinate,
                              const ksj::viewer::ArrShowDisplaySettings display_settings = {}) {
  return {.acquisition_kind = kind, .axes = axes, .coordinate = coordinate, .display_settings = display_settings};
}

[[nodiscard]] ImageRequest make_image_request(const QString& series_id, const std::uint32_t ordinal = 0U,
                                              const std::uint16_t z = 0U, const std::uint16_t channel = 0U,
                                              const ksj::viewer::ImageDisplaySettings display_settings = {}) {
  ImageCoordinate coordinate;
  ksj::viewer::set_image_coordinate_value(coordinate, ImageDimension::z, z);
  ksj::viewer::set_image_coordinate_value(coordinate, ImageDimension::channel, channel);
  return {.series_id = series_id, .ordinal = ordinal, .coordinate = coordinate, .display_settings = display_settings};
}

void append_synthetic_image(ISMRMRD::Dataset& dataset) {
  ISMRMRD::Image<float> image(kSourceImageWidth, kSourceImageHeight, 1U, 1U);
  std::fill(image.getDataPtr(), image.getDataPtr() + image.getNumberOfDataElements(), 0.0F);
  image(0U, 0U, 0U, 0U) = 1.0F;
  image(kSourceImageWidth - 1U, kSourceImageHeight - 1U, 0U, 0U) = 100.0F;
  auto& header = image.getHead();
  header.image_index = 5U;
  header.image_series_index = 7U;
  image.setAttributeString("");
  dataset.appendImage(std::string(kImageSeries), image);
}

void append_synthetic_complex_image(ISMRMRD::Dataset& dataset) {
  ISMRMRD::Image<std::complex<float>> image(2U, 2U, 1U, 1U);
  image(0U, 0U, 0U, 0U) = {1.0F, 0.0F};
  image(1U, 0U, 0U, 0U) = {0.0F, 2.0F};
  image(0U, 1U, 0U, 0U) = {-0.5F, 0.0F};
  image(1U, 1U, 0U, 0U) = {0.0F, -0.25F};
  image.setAttributeString("");
  dataset.appendImage(std::string(kComplexImageSeries), image);
}

void append_synthetic_waveform(ISMRMRD::Dataset& dataset) {
  ISMRMRD::Waveform waveform(2U, 1U);
  waveform.head.measurement_uid = 31U;
  waveform.head.scan_counter = 17U;
  waveform.head.time_stamp = 23U;
  waveform.head.sample_time_us = 2.5F;
  waveform.head.waveform_id = 7U;
  waveform.data[0U] = 1U;
  waveform.data[1U] = 2U;
  dataset.appendWaveform(waveform);
}

void write_synthetic_dataset(const std::filesystem::path& path, const std::string_view group = kDatasetGroup,
                             const bool write_image = true, const bool write_waveform = false) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));

  ISMRMRD::Acquisition acquisition(3U, 2U, 2U);
  acquisition.scan_counter() = 17U;
  acquisition.idx().slice = 3U;
  for (std::uint16_t channel = 0U; channel < acquisition.active_channels(); ++channel) {
    for (std::uint16_t sample = 0U; sample < acquisition.number_of_samples(); ++sample) {
      const auto value = static_cast<float>(sample + channel * acquisition.number_of_samples());
      acquisition.data(sample, channel) = {value, 10.0F + value};
    }
  }
  for (std::uint16_t sample = 0U; sample < acquisition.number_of_samples(); ++sample) {
    acquisition.traj(0U, sample) = static_cast<float>(sample);
    acquisition.traj(1U, sample) = static_cast<float>(sample + 10U);
  }
  dataset.appendAcquisition(acquisition);

  if (write_image) {
    append_synthetic_image(dataset);
  }
  if (write_waveform) {
    append_synthetic_waveform(dataset);
  }
}

void write_complex_image_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(kDatasetGroup).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  append_synthetic_complex_image(dataset);
}

void write_fixed_axis_image_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(kDatasetGroup).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  ISMRMRD::Image<float> image(4U, 3U, 3U, 2U);
  for (std::uint16_t channel = 0U; channel < 2U; ++channel) {
    for (std::uint16_t z = 0U; z < 3U; ++z) {
      for (std::uint16_t y = 0U; y < 3U; ++y) {
        for (std::uint16_t x = 0U; x < 4U; ++x) {
          image(x, y, z, channel) = static_cast<float>(x + 10U * y + 100U * z + 1'000U * channel);
        }
      }
    }
  }
  image.setAttributeString("");
  dataset.appendImage(std::string(kFixedAxisImageSeries), image);
}

void append_cartesian_kspace_line(ISMRMRD::Dataset& dataset, const std::uint16_t ky,
                                  const std::array<std::complex<float>, 4U>& coil_zero,
                                  const std::array<std::complex<float>, 4U>& coil_one,
                                  const CartesianKspaceTestPlaneIndices plane = {}) {
  ISMRMRD::Acquisition acquisition(4U, 2U, 0U);
  acquisition.center_sample() = 2U;
  acquisition.idx().kspace_encode_step_1 = ky;
  acquisition.encoding_space_ref() = plane.encoding_space_ref;
  acquisition.idx().kspace_encode_step_2 = plane.kspace_encode_step_2;
  acquisition.idx().average = plane.average;
  acquisition.idx().slice = plane.slice;
  acquisition.idx().contrast = plane.contrast;
  acquisition.idx().phase = plane.phase;
  acquisition.idx().repetition = plane.repetition;
  acquisition.idx().set = plane.set;
  acquisition.idx().segment = plane.segment;
  for (std::size_t index = 0U; index < plane.user.size(); ++index) {
    acquisition.idx().user[index] = plane.user[index];
  }
  auto header = acquisition.getHead();
  header.flags = plane.flags;
  acquisition.setHead(header);
  for (std::uint16_t sample = 0U; sample < 4U; ++sample) {
    acquisition.data(sample, 0U) = coil_zero[sample];
    acquisition.data(sample, 1U) = coil_one[sample];
  }
  dataset.appendAcquisition(acquisition);
}

void write_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspaceXml));

  const std::array<std::complex<float>, 4U> ky_zero_coil_zero{
    std::complex<float>{3.0F, 0.0F}, std::complex<float>{3.0F, 0.0F}, std::complex<float>{3.0F, 0.0F},
    std::complex<float>{3.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> ky_zero_coil_one{
    std::complex<float>{4.0F, 0.0F}, std::complex<float>{4.0F, 0.0F}, std::complex<float>{4.0F, 0.0F},
    std::complex<float>{4.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> repeated_ky_zero_coil_zero{
    std::complex<float>{6.0F, 0.0F}, std::complex<float>{6.0F, 0.0F}, std::complex<float>{6.0F, 0.0F},
    std::complex<float>{6.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> repeated_ky_zero_coil_one{
    std::complex<float>{8.0F, 0.0F}, std::complex<float>{8.0F, 0.0F}, std::complex<float>{8.0F, 0.0F},
    std::complex<float>{8.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> ky_two_coil_zero{
    std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
    std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> ky_two_coil_one{
    std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
    std::complex<float>{2.0F, 0.0F}};

  // Deliberately non-monotonic ky order, repeated ky=0, omitted ky=1, and
  // variants for every non-planar plane index. The viewer must internally
  // isolate the first readout × phase-encode coordinate, preserve empty
  // cells, and disclose multiple source contributions without exposing an
  // acquisition-record selector.
  append_cartesian_kspace_line(dataset, 2U, ky_two_coil_zero, ky_two_coil_one);
  append_cartesian_kspace_line(dataset, 0U, ky_zero_coil_zero, ky_zero_coil_one);
  append_cartesian_kspace_line(dataset, 0U, repeated_ky_zero_coil_zero, repeated_ky_zero_coil_one);
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.encoding_space_ref = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.kspace_encode_step_2 = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.average = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.slice = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.contrast = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.phase = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.repetition = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.set = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one, {.segment = 1U});
  append_cartesian_kspace_line(dataset, 1U, ky_zero_coil_zero, ky_zero_coil_one,
                               {.user = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U}});
}

void write_auxiliary_then_imaging_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspaceXml));

  const std::array<std::complex<float>, 4U> coil_zero{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                                      std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> coil_one{std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
                                                     std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}};

  // This intentionally mirrors cart_t1.mrd: a noise line outside the XML
  // imaging ky limit appears before valid imaging lines. Imaging remains the
  // default, while every standard auxiliary flag membership is available from
  // the explicit acquisition-type chooser. The final line deliberately has
  // two auxiliary flags so Navigation and Surface-coil correction must both
  // include it; a selector cannot reduce flags to one priority category.
  append_cartesian_kspace_line(dataset, 3U, coil_zero, coil_one,
                               {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT)});
  append_cartesian_kspace_line(
    dataset, 1U, coil_zero, coil_one,
    {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_SURFACECOILCORRECTIONSCAN_DATA)});
  append_cartesian_kspace_line(dataset, 1U, coil_zero, coil_one,
                               {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION)});
  append_cartesian_kspace_line(dataset, 0U, coil_zero, coil_one,
                               {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_NAVIGATION_DATA)});
  append_cartesian_kspace_line(dataset, 0U, coil_zero, coil_one);
  append_cartesian_kspace_line(dataset, 1U, coil_zero, coil_one);
  append_cartesian_kspace_line(
    dataset, 2U, coil_zero, coil_one,
    {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_PARALLEL_CALIBRATION_AND_IMAGING)});
  append_cartesian_kspace_line(
    dataset, 2U, coil_zero, coil_one,
    {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_NAVIGATION_DATA) |
              acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_SURFACECOILCORRECTIONSCAN_DATA)});
}

void write_auxiliary_only_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspaceXml));

  const std::array<std::complex<float>, 4U> coil_zero{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                                      std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> coil_one{std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
                                                     std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}};
  append_cartesian_kspace_line(dataset, 3U, coil_zero, coil_one,
                               {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_NOISE_MEASUREMENT)});
  append_cartesian_kspace_line(
    dataset, 1U, coil_zero, coil_one,
    {.flags = acquisition_flag_mask(ISMRMRD::ISMRMRD_ACQ_IS_SURFACECOILCORRECTIONSCAN_DATA)});
}

void write_out_of_bounds_imaging_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspaceXml));

  const std::array<std::complex<float>, 4U> coil_zero{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                                      std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> coil_one{std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
                                                     std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}};
  append_cartesian_kspace_line(dataset, 3U, coil_zero, coil_one);
}

void write_out_of_bounds_partition_imaging_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspacePartitionLimitedXml));

  const std::array<std::complex<float>, 4U> coil_zero{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                                      std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> coil_one{std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
                                                     std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}};
  append_cartesian_kspace_line(dataset, 0U, coil_zero, coil_one, {.kspace_encode_step_2 = 1U});
}

void write_downsampled_readout_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspaceXml));

  constexpr std::uint16_t sample_count = 2'049U;
  ISMRMRD::Acquisition acquisition(sample_count, 1U, 0U);
  acquisition.center_sample() = sample_count / 2U;
  acquisition.idx().kspace_encode_step_1 = 0U;
  for (std::uint16_t sample = 0U; sample < sample_count; ++sample) {
    acquisition.data(sample, 0U) = {static_cast<float>(sample), 0.0F};
  }
  dataset.appendAcquisition(acquisition);
}

void write_invalid_then_valid_imaging_cartesian_kspace_dataset(const std::filesystem::path& path) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  dataset.writeHeader(std::string(kCartesianKspaceXml));

  const std::array<std::complex<float>, 4U> coil_zero{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                                      std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> coil_one{std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
                                                     std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}};
  // The first imaging plane is unusable because its ky lies outside the XML
  // limit. A later independent segment remains valid and must become the
  // automatic readout × phase-encode plane instead of making the whole type
  // fail.
  append_cartesian_kspace_line(dataset, 3U, coil_zero, coil_one);
  append_cartesian_kspace_line(dataset, 0U, coil_zero, coil_one, {.segment = 1U});
  append_cartesian_kspace_line(dataset, 1U, coil_zero, coil_one, {.segment = 1U});
}

void write_synthetic_image_artifact(const std::filesystem::path& path, const std::string_view group = kDatasetGroup) {
  const auto filename = path.string();
  ISMRMRD::Dataset dataset(filename.c_str(), std::string(group).c_str(), true);
  dataset.writeHeader(std::string(kXmlHeader));
  append_synthetic_image(dataset);
}

void create_empty_root_group(const std::filesystem::path& path, const std::string_view group) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto empty_group = H5Gcreate2(file, std::string(group).c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(empty_group, 0);
  EXPECT_GE(H5Gclose(empty_group), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

void write_uint32_attribute(const std::filesystem::path& path, const std::string_view object_path,
                            const std::string_view attribute_name, const std::uint32_t value) {
  const auto filename = path.string();
  const auto file = H5Fopen(filename.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
  ASSERT_GE(file, 0);
  const auto object = H5Oopen(file, std::string(object_path).c_str(), H5P_DEFAULT);
  ASSERT_GE(object, 0);
  const auto dataspace = H5Screate(H5S_SCALAR);
  ASSERT_GE(dataspace, 0);
  const auto attribute =
    H5Acreate2(object, std::string(attribute_name).c_str(), H5T_NATIVE_UINT32, dataspace, H5P_DEFAULT, H5P_DEFAULT);
  ASSERT_GE(attribute, 0);
  ASSERT_GE(H5Awrite(attribute, H5T_NATIVE_UINT32, &value), 0);
  EXPECT_GE(H5Aclose(attribute), 0);
  EXPECT_GE(H5Sclose(dataspace), 0);
  EXPECT_GE(H5Oclose(object), 0);
  EXPECT_GE(H5Fclose(file), 0);
}

[[nodiscard]] QString valid_pipeline_definition() {
  return QStringLiteral(R"json(
{
  "kind": "PipelineDefinition",
  "pipeline": {
    "id": "org.example.viewer-test",
    "display_name": "Viewer parse-only test"
  },
  "input_profile": {
    "kind": "ismrmrd-hdf5",
    "dataset_group": "dataset"
  },
  "allowed_profiles": ["offline-reference"],
  "parameters": {},
  "provider_requirements": [
    {
      "alias": "example",
      "provider_id": "org.example.viewer-provider"
    }
  ],
  "nodes": [
    {
      "id": "prepare",
      "operator": {
        "provider": "example",
        "id": "prepare"
      },
      "config": {}
    },
    {
      "id": "reconstruct",
      "operator": {
        "provider": "example",
        "id": "reconstruct"
      },
      "config": {}
    }
  ],
  "edges": [
    {
      "id": "prepared_kspace",
      "from": {
        "node": "prepare",
        "port": "prepared"
      },
      "to": {
        "node": "reconstruct",
        "port": "kspace"
      }
    },
    {
      "id": "prepared_weights",
      "from": {
        "node": "prepare",
        "port": "weights"
      },
      "to": {
        "node": "reconstruct",
        "port": "weights"
      }
    }
  ],
  "bindings": {
    "ingress": [
      {
        "id": "kspace",
        "type": "ksj.kspace-frame",
        "to": {
          "node": "prepare",
          "port": "kspace"
        }
      }
    ],
    "egress": [
      {
        "id": "images",
        "type": "ksj.image-frame",
        "from": {
          "node": "reconstruct",
          "port": "image"
        }
      }
    ],
    "calibration": [
      {
        "id": "prepared_calibration",
        "producer": {
          "node": "prepare",
          "port": "calibration"
        },
        "consumers": [
          {
            "node": "reconstruct",
            "port": "calibration"
          }
        ]
      }
    ],
    "merge": []
  },
  "annotations": {}
}
)json");
}

[[nodiscard]] QString calibration_only_pipeline_definition() {
  auto definition = QJsonDocument::fromJson(valid_pipeline_definition().toUtf8()).object();
  definition.insert(QStringLiteral("edges"), QJsonArray{});
  return QString::fromUtf8(QJsonDocument(definition).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString fan_in_pipeline_definition() {
  const auto endpoint = [](const QString& node, const QString& port) {
    QJsonObject value;
    value.insert(QStringLiteral("node"), node);
    value.insert(QStringLiteral("port"), port);
    return value;
  };

  auto definition = QJsonDocument::fromJson(valid_pipeline_definition().toUtf8()).object();
  auto bindings = definition.value(QStringLiteral("bindings")).toObject();
  auto ingress = bindings.value(QStringLiteral("ingress")).toArray();
  QJsonObject weights;
  weights.insert(QStringLiteral("id"), QStringLiteral("weights"));
  weights.insert(QStringLiteral("type"), QStringLiteral("ksj.kspace-frame"));
  weights.insert(QStringLiteral("to"), endpoint(QStringLiteral("prepare"), QStringLiteral("weights")));
  ingress.append(weights);
  bindings.insert(QStringLiteral("ingress"), ingress);
  definition.insert(QStringLiteral("bindings"), bindings);
  return QString::fromUtf8(QJsonDocument(definition).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString graph_over_limit_pipeline_definition() {
  constexpr int node_count = 257;
  const auto node_id = [](const int index) {
    return QStringLiteral("node_%1").arg(index, 3, 10, QLatin1Char('0'));
  };
  const auto endpoint = [](const QString& node, const QString& port) {
    QJsonObject value;
    value.insert(QStringLiteral("node"), node);
    value.insert(QStringLiteral("port"), port);
    return value;
  };

  auto definition = QJsonDocument::fromJson(valid_pipeline_definition().toUtf8()).object();
  QJsonArray nodes;
  QJsonArray edges;
  for (int index = 0; index < node_count; ++index) {
    QJsonObject operator_reference;
    operator_reference.insert(QStringLiteral("provider"), QStringLiteral("example"));
    operator_reference.insert(QStringLiteral("id"), QStringLiteral("passthrough"));
    QJsonObject node;
    node.insert(QStringLiteral("id"), node_id(index));
    node.insert(QStringLiteral("operator"), operator_reference);
    node.insert(QStringLiteral("config"), QJsonObject{});
    nodes.append(node);
    if (index == 0) {
      continue;
    }
    QJsonObject edge;
    edge.insert(QStringLiteral("id"), QStringLiteral("edge_%1").arg(index, 3, 10, QLatin1Char('0')));
    edge.insert(QStringLiteral("from"), endpoint(node_id(index - 1), QStringLiteral("output")));
    edge.insert(QStringLiteral("to"), endpoint(node_id(index), QStringLiteral("input")));
    edges.append(edge);
  }

  QJsonObject ingress;
  ingress.insert(QStringLiteral("id"), QStringLiteral("input"));
  ingress.insert(QStringLiteral("type"), QStringLiteral("ksj.kspace-frame"));
  ingress.insert(QStringLiteral("to"), endpoint(node_id(0), QStringLiteral("input")));
  QJsonObject egress;
  egress.insert(QStringLiteral("id"), QStringLiteral("output"));
  egress.insert(QStringLiteral("type"), QStringLiteral("ksj.image-frame"));
  egress.insert(QStringLiteral("from"), endpoint(node_id(node_count - 1), QStringLiteral("output")));
  QJsonObject bindings;
  bindings.insert(QStringLiteral("ingress"), QJsonArray{ingress});
  bindings.insert(QStringLiteral("egress"), QJsonArray{egress});
  bindings.insert(QStringLiteral("calibration"), QJsonArray{});
  bindings.insert(QStringLiteral("merge"), QJsonArray{});

  definition.insert(QStringLiteral("nodes"), nodes);
  definition.insert(QStringLiteral("edges"), edges);
  definition.insert(QStringLiteral("bindings"), bindings);
  return QString::fromUtf8(QJsonDocument(definition).toJson(QJsonDocument::Compact));
}

void write_file(const QString& path, const QByteArray& contents) {
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly)) << file.errorString().toStdString();
  ASSERT_EQ(file.write(contents), static_cast<qint64>(contents.size())) << file.errorString().toStdString();
}

[[nodiscard]] QByteArray read_file(const QString& path) {
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly)) << file.errorString().toStdString();
  return file.readAll();
}

[[nodiscard]] QApplication& viewer_application() {
  static QTemporaryDir settings_directory;
  static const auto configured_settings_path = [] {
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_directory.path());
    return true;
  }();
  static_cast<void>(configured_settings_path);
  if (auto* application = qobject_cast<QApplication*>(QCoreApplication::instance()); application != nullptr) {
    return *application;
  }

  static int argc = 1;
  static char application_name[] = "ksj-viewer-presentation-tests";
  static char* argv[] = {application_name, nullptr};
  static QApplication application(argc, argv);
  return application;
}

class ViewerFileDialogSettingsScope final {
public:
  explicit ViewerFileDialogSettingsScope(QSettings& settings) : settings_(settings) { clear(); }

  ~ViewerFileDialogSettingsScope() { clear(); }

  ViewerFileDialogSettingsScope(const ViewerFileDialogSettingsScope&) = delete;
  ViewerFileDialogSettingsScope& operator=(const ViewerFileDialogSettingsScope&) = delete;

private:
  void clear() const {
    settings_.remove(QStringLiteral("file_dialogs/last_open_directory"));
    settings_.remove(QStringLiteral("file_dialogs/recent_files"));
    settings_.sync();
  }

  QSettings& settings_;
};

template <typename Widget>
[[nodiscard]] Widget* find_named_widget(QWidget& parent, std::initializer_list<const char*> object_names) {
  for (const auto* object_name : object_names) {
    if (auto* widget = parent.findChild<Widget*>(QString::fromLatin1(object_name)); widget != nullptr) {
      return widget;
    }
  }
  return nullptr;
}

[[nodiscard]] const QGraphicsPathItem* find_pipeline_graph_edge(const QGraphicsScene& scene, const QString& edge_id) {
  for (const auto* item : scene.items()) {
    const auto* path = dynamic_cast<const QGraphicsPathItem*>(item);
    if (path != nullptr && path->zValue() == 0.0 && path->data(0).toString() == edge_id) {
      return path;
    }
  }
  return nullptr;
}

} // namespace

TEST(KSpaceJetViewerTheme, KeepsNativeComboAndSpinAffordances) {
  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);

  const auto theme = application.styleSheet();
  EXPECT_FALSE(theme.contains(QStringLiteral("QComboBox {")));
  EXPECT_FALSE(theme.contains(QStringLiteral("QAbstractSpinBox {")));
  EXPECT_FALSE(theme.contains(QStringLiteral("QComboBox::drop-down")));
  EXPECT_FALSE(theme.contains(QStringLiteral("QAbstractSpinBox::")));

  QComboBox combo;
  combo.addItem(QStringLiteral("Imaging data"));
  combo.resize(160, 24);
  QStyleOptionComboBox combo_option;
  combo_option.initFrom(&combo);
  combo_option.rect = combo.rect();
  combo_option.currentText = combo.currentText();
  const auto combo_arrow =
    combo.style()->subControlRect(QStyle::CC_ComboBox, &combo_option, QStyle::SC_ComboBoxArrow, &combo);
  EXPECT_FALSE(combo_arrow.isEmpty());
  EXPECT_TRUE(combo.rect().contains(combo_arrow));
  EXPECT_GE(combo_arrow.left(), combo.width() / 2);

  QDoubleSpinBox spin;
  spin.setRange(0.0, 100.0);
  spin.setValue(50.0);
  spin.resize(160, 24);
  QStyleOptionSpinBox spin_option;
  spin_option.initFrom(&spin);
  spin_option.rect = spin.rect();
  spin_option.buttonSymbols = spin.buttonSymbols();
  const auto spin_up = spin.style()->subControlRect(QStyle::CC_SpinBox, &spin_option, QStyle::SC_SpinBoxUp, &spin);
  const auto spin_down = spin.style()->subControlRect(QStyle::CC_SpinBox, &spin_option, QStyle::SC_SpinBoxDown, &spin);
  EXPECT_FALSE(spin_up.isEmpty());
  EXPECT_FALSE(spin_down.isEmpty());
  EXPECT_TRUE(spin.rect().contains(spin_up));
  EXPECT_TRUE(spin.rect().contains(spin_down));
  EXPECT_TRUE(spin_up.intersected(spin_down).isEmpty());
  EXPECT_GE(spin_up.left(), spin.width() / 2);
  EXPECT_GE(spin_down.left(), spin.width() / 2);
}

TEST(KSpaceJetViewerTheme, UsesWhiteOnlyForEditableFixedIndexDimensionCells) {
  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);

  QWidget controls_surface;
  controls_surface.setProperty("surfaceRole", QStringLiteral("controls"));
  controls_surface.resize(280, 112);
  auto* dimensions = new ksj::viewer::ArrShowDimensionStrip(&controls_surface, QStringLiteral("kspace"));
  dimensions->setGeometry(12, 8, 250, 96);

  const ksj::viewer::ArrShowDimensionSpec readout{
    .identifier = QStringLiteral("readout"),
    .label = QStringLiteral("Readout"),
    .abbreviation = QStringLiteral("RO"),
    .observed_values = {0},
    .current_value = 0,
    .displayed_extent = 64,
    .selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::first,
  };
  const ksj::viewer::ArrShowDimensionSpec phase_encode{
    .identifier = QStringLiteral("phase-encode"),
    .label = QStringLiteral("Phase encode"),
    .abbreviation = QStringLiteral("PE"),
    .observed_values = {0},
    .current_value = 0,
    .displayed_extent = 64,
    .selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::second,
  };
  const ksj::viewer::ArrShowDimensionSpec coil{
    .identifier = QStringLiteral("coil"),
    .label = QStringLiteral("Raw coil"),
    .abbreviation = QStringLiteral("Co"),
    .observed_values = {0, 1},
    .current_value = 0,
    .displayed_extent = 2,
  };
  const ksj::viewer::ArrShowDimensionSpec average{
    .identifier = QStringLiteral("average"),
    .label = QStringLiteral("Average"),
    .abbreviation = QStringLiteral("Avg"),
    .observed_values = {0, 1},
    .current_value = 0,
    .displayed_extent = 2,
  };
  dimensions->set_dimensions({readout, phase_encode, coil, average}, QStringLiteral("average"));

  controls_surface.show();
  application.processEvents();

  const auto* readout_value = find_named_widget<QLineEdit>(*dimensions, {"kspaceDimensionReadoutValue"});
  const auto* coil_value = find_named_widget<QLineEdit>(*dimensions, {"kspaceDimensionCoilValue"});
  const auto* average_value = find_named_widget<QLineEdit>(*dimensions, {"kspaceDimensionAverageValue"});
  const auto* readout_abbreviation =
    find_named_widget<QToolButton>(*dimensions, {"kspaceDimensionReadoutAbbreviation"});
  const auto* readout_increment = find_named_widget<QToolButton>(*dimensions, {"kspaceDimensionReadoutUp"});
  const auto* readout_decrement = find_named_widget<QToolButton>(*dimensions, {"kspaceDimensionReadoutDown"});
  const auto* readout_extent = find_named_widget<QToolButton>(*dimensions, {"kspaceDimensionReadoutLabel"});
  const auto* average_abbreviation =
    find_named_widget<QToolButton>(*dimensions, {"kspaceDimensionAverageAbbreviation"});
  const auto* average_extent = find_named_widget<QToolButton>(*dimensions, {"kspaceDimensionAverageLabel"});
  ASSERT_NE(readout_value, nullptr);
  ASSERT_NE(coil_value, nullptr);
  ASSERT_NE(average_value, nullptr);
  ASSERT_NE(readout_abbreviation, nullptr);
  ASSERT_NE(readout_increment, nullptr);
  ASSERT_NE(readout_decrement, nullptr);
  ASSERT_NE(readout_extent, nullptr);
  ASSERT_NE(average_abbreviation, nullptr);
  ASSERT_NE(average_extent, nullptr);
  ASSERT_TRUE(readout_value->isReadOnly());
  ASSERT_FALSE(coil_value->isReadOnly());
  ASSERT_FALSE(average_value->isReadOnly());
  EXPECT_FALSE(readout_value->property("arrShowDimensionFixedIndexInput").toBool());
  EXPECT_TRUE(coil_value->property("arrShowDimensionFixedIndexInput").toBool());

  QImage rendered(controls_surface.size(), QImage::Format_ARGB32_Premultiplied);
  rendered.fill(Qt::transparent);
  {
    QPainter painter(&rendered);
    controls_surface.render(&painter);
  }
  const auto right_interior_color = [&controls_surface, &rendered](const QWidget* widget) {
    const auto position = widget->mapTo(&controls_surface, QPoint{widget->width() - 5, widget->height() / 2});
    return rendered.pixelColor(position);
  };
  const auto controls_background = QColor(QStringLiteral("#f3f3f3"));
  EXPECT_EQ(right_interior_color(readout_abbreviation), controls_background);
  EXPECT_EQ(right_interior_color(readout_increment), controls_background);
  EXPECT_EQ(right_interior_color(readout_value), controls_background);
  EXPECT_EQ(right_interior_color(readout_decrement), controls_background);
  EXPECT_EQ(right_interior_color(readout_extent), controls_background);
  EXPECT_EQ(right_interior_color(coil_value), QColor(QStringLiteral("#ffffff")));
  EXPECT_EQ(right_interior_color(average_abbreviation), controls_background);
  EXPECT_EQ(right_interior_color(average_extent), controls_background);
  EXPECT_EQ(right_interior_color(average_value), QColor(QStringLiteral("#1c5fa8")));

  controls_surface.close();
  application.processEvents();
}

TEST(KSpaceJetViewerCanvas, KeepsArrShowStyleInteractionsInsideTheBoundedDisplayDerivative) {
  auto& application = viewer_application();
  ksj::viewer::InspectionCanvas canvas;
  canvas.resize(480, 320);
  canvas.show();

  QImage derivative(8, 6, QImage::Format_Grayscale8);
  derivative.fill(0U);
  derivative.setPixel(4, 3, 255U);
  canvas.set_display_image(derivative, QStringLiteral("empty"));
  application.processEvents();

  EXPECT_FALSE(canvas.pixmap(Qt::ReturnByValue).isNull());
  EXPECT_EQ(canvas.zoom_percent(), 100);

  QPoint probed{-1, -1};
  int browse_step = 0;
  int browse_dimension_step = 0;
  QPointF window_drag{-1.0, -1.0};
  bool window_finished = false;
  int reset_count = 0;
  int reported_zoom = 0;
  canvas.set_probe_callback([&probed](const QPoint display_pixel) {
    probed = display_pixel;
  });
  canvas.set_browse_step_callback([&browse_step](const int step) {
    browse_step += step;
  });
  canvas.set_browse_dimension_callback([&browse_dimension_step](const int step) {
    browse_dimension_step += step;
  });
  canvas.set_window_level_callback([&window_drag, &window_finished](const QPointF delta, const bool finished) {
    window_drag = delta;
    window_finished = finished;
  });
  canvas.set_reset_window_callback([&reset_count] {
    ++reset_count;
  });
  canvas.set_zoom_changed_callback([&reported_zoom](const int percent) {
    reported_zoom = percent;
  });

  const QPointF center{canvas.width() * 0.5, canvas.height() * 0.5};
  QMouseEvent probe_event(QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &probe_event);
  EXPECT_GE(probed.x(), 0);
  EXPECT_GE(probed.y(), 0);
  EXPECT_LT(probed.x(), derivative.width());
  EXPECT_LT(probed.y(), derivative.height());

  QWheelEvent browse_wheel(center, center, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                           false);
  QApplication::sendEvent(&canvas, &browse_wheel);
  EXPECT_EQ(browse_step, 1);

  QKeyEvent next_dimension(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &next_dimension);
  EXPECT_EQ(browse_dimension_step, 1);
  QKeyEvent previous_dimension(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &previous_dimension);
  EXPECT_EQ(browse_dimension_step, 0);

  QWheelEvent zoom_wheel(center, center, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase,
                         false);
  QApplication::sendEvent(&canvas, &zoom_wheel);
  EXPECT_EQ(canvas.zoom_percent(), 150);
  EXPECT_EQ(reported_zoom, 150);
  canvas.fit_to_view();
  EXPECT_EQ(canvas.zoom_percent(), 100);

  QMouseEvent press(QEvent::MouseButtonPress, center, Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &press);
  QMouseEvent drag(QEvent::MouseMove, center + QPointF{17.0, 13.0}, Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &drag);
  const auto moved_window_drag = window_drag;
  EXPECT_GT(window_drag.x(), 0.0);
  EXPECT_GT(window_drag.y(), 0.0);
  EXPECT_FALSE(window_finished);
  QMouseEvent release(QEvent::MouseButtonRelease, center + QPointF{17.0, 13.0}, Qt::MiddleButton, Qt::NoButton,
                      Qt::NoModifier);
  QApplication::sendEvent(&canvas, &release);
  EXPECT_NEAR(window_drag.x(), moved_window_drag.x(), 1.0e-9);
  EXPECT_NEAR(window_drag.y(), moved_window_drag.y(), 1.0e-9);
  EXPECT_TRUE(window_finished);

  QMouseEvent reset_middle_press(QEvent::MouseButtonPress, center, Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &reset_middle_press);
  QMouseEvent reset_right_press(QEvent::MouseButtonPress, center, Qt::RightButton, Qt::MiddleButton | Qt::RightButton,
                                Qt::NoModifier);
  QApplication::sendEvent(&canvas, &reset_right_press);
  EXPECT_EQ(reset_count, 1);

  QMouseEvent double_click(QEvent::MouseButtonDblClick, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&canvas, &double_click);
  EXPECT_EQ(reset_count, 2);

  canvas.close();
  application.processEvents();
}

TEST(KSpaceJetViewerArrShowDimensions, ShowsOnlyObservedVaryingNonAxisDimensions) {
  auto& application = viewer_application();
  ksj::viewer::ArrShowDimensionStrip dimensions(nullptr, QStringLiteral("kspace"));
  const ksj::viewer::ArrShowDimensionSpec coil{
    .identifier = QStringLiteral("coil"),
    .label = QStringLiteral("Raw coil"),
    .abbreviation = QStringLiteral("Co"),
    .tool_tip = QStringLiteral("Selected raw acquisition coil."),
    .observed_values = {0},
    .current_value = 0,
    .displayed_extent = 1,
  };
  const ksj::viewer::ArrShowDimensionSpec user_zero{
    .identifier = QStringLiteral("user-0"),
    .label = QStringLiteral("User 0"),
    .tool_tip = QStringLiteral("Source-defined ISMRMRD user counter 0."),
    .observed_values = {0},
    .current_value = 0,
    .displayed_extent = 1,
  };
  dimensions.set_dimensions({coil, user_zero});
  application.processEvents();

  EXPECT_FALSE(dimensions.has_dimensions());
  EXPECT_EQ(dimensions.active_dimension_identifier(), QString{});
  EXPECT_EQ(find_named_widget<QLineEdit>(dimensions, {"kspaceDimensionCoilValue"}), nullptr);
  EXPECT_EQ(find_named_widget<QToolButton>(dimensions, {"kspaceDimensionCoilAbbreviation"}), nullptr);
  EXPECT_EQ(find_named_widget<QLineEdit>(dimensions, {"kspaceDimensionUser0Value"}), nullptr);

  auto varying_coil = coil;
  varying_coil.observed_values = {0, 1};
  varying_coil.displayed_extent = 2;
  dimensions.set_dimensions({varying_coil, user_zero});
  application.processEvents();

  const auto* value = find_named_widget<QLineEdit>(dimensions, {"kspaceDimensionCoilValue"});
  const auto* increment = find_named_widget<QAbstractButton>(dimensions, {"kspaceDimensionCoilUp"});
  const auto* decrement = find_named_widget<QAbstractButton>(dimensions, {"kspaceDimensionCoilDown"});
  const auto* abbreviation = find_named_widget<QToolButton>(dimensions, {"kspaceDimensionCoilAbbreviation"});
  const auto* extent = find_named_widget<QAbstractButton>(dimensions, {"kspaceDimensionCoilLabel"});
  ASSERT_NE(value, nullptr);
  ASSERT_NE(increment, nullptr);
  ASSERT_NE(decrement, nullptr);
  ASSERT_NE(abbreviation, nullptr);
  ASSERT_NE(extent, nullptr);
  EXPECT_TRUE(dimensions.has_dimensions());
  EXPECT_EQ(abbreviation->text(), QStringLiteral("Co"));
  EXPECT_TRUE(abbreviation->toolTip().contains(QStringLiteral("Raw coil")));
  EXPECT_FALSE(abbreviation->toolTip().contains(QStringLiteral("idx.")));
  EXPECT_EQ(value->text(), QStringLiteral("0"));
  EXPECT_EQ(extent->text(), QStringLiteral("2"));
  EXPECT_TRUE(increment->isEnabled());
  EXPECT_FALSE(decrement->isEnabled());
  EXPECT_EQ(find_named_widget<QLineEdit>(dimensions, {"kspaceDimensionUser0Value"}), nullptr);

  // Left/right traverses exactly the displayed, varying controls. The singleton
  // User 0 dimension is absent and therefore cannot become active.
  EXPECT_EQ(dimensions.active_dimension_identifier(), QStringLiteral("coil"));
  EXPECT_TRUE(dimensions.focus_relative_dimension(1));
  EXPECT_EQ(dimensions.active_dimension_identifier(), QStringLiteral("coil"));
  EXPECT_TRUE(dimensions.step_active_dimension(1));
}

TEST(KSpaceJetViewerArrShowDimensions, UsesColumnOrderForPlaneAxesAndOnlyClearsActiveDimensionForTheTwoAxisPlane) {
  auto& application = viewer_application();
  ksj::viewer::ArrShowDimensionStrip dimensions(nullptr, QStringLiteral("kspace"));
  const ksj::viewer::ArrShowDimensionSpec readout{
    .identifier = QStringLiteral("readout"),
    .label = QStringLiteral("Readout"),
    .abbreviation = QStringLiteral("RO"),
    .tool_tip = QStringLiteral("Rendered readout axis."),
    .observed_values = {0},
    .current_value = 0,
    .displayed_extent = 512,
    .selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::first,
  };
  const ksj::viewer::ArrShowDimensionSpec phase_encode{
    .identifier = QStringLiteral("phase-encode"),
    .label = QStringLiteral("Phase encode"),
    .abbreviation = QStringLiteral("PE"),
    .tool_tip = QStringLiteral("Rendered phase-encode axis."),
    .observed_values = {0},
    .current_value = 0,
    .displayed_extent = 128,
    .selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::second,
  };
  const ksj::viewer::ArrShowDimensionSpec coil{
    .identifier = QStringLiteral("coil"),
    .label = QStringLiteral("Raw coil"),
    .abbreviation = QStringLiteral("Co"),
    .tool_tip = QStringLiteral("Selected raw acquisition coil."),
    .observed_values = {0, 1},
    .current_value = 0,
    .displayed_extent = 2,
  };
  const ksj::viewer::ArrShowDimensionSpec average{
    .identifier = QStringLiteral("average"),
    .label = QStringLiteral("Average"),
    .abbreviation = QStringLiteral("Avg"),
    .tool_tip = QStringLiteral("Selected ISMRMRD average coordinate."),
    .observed_values = {0, 1},
    .current_value = 0,
    .displayed_extent = 2,
  };

  dimensions.set_dimensions({readout, phase_encode, coil, average});
  application.processEvents();
  EXPECT_EQ(dimensions.active_dimension_identifier(), QStringLiteral("coil"));

  const auto* readout_value = find_named_widget<QLineEdit>(dimensions, {"kspaceDimensionReadoutValue"});
  const auto* phase_encode_value = find_named_widget<QLineEdit>(dimensions, {"kspaceDimensionPhaseEncodeValue"});
  ASSERT_NE(readout_value, nullptr);
  ASSERT_NE(phase_encode_value, nullptr);
  EXPECT_EQ(readout_value->text(), QStringLiteral(":"));
  EXPECT_EQ(phase_encode_value->text(), QStringLiteral(":"));
  EXPECT_TRUE(readout_value->isReadOnly());
  EXPECT_TRUE(phase_encode_value->isReadOnly());

  // A plane itself has exactly two ':' controls and therefore no active
  // navigation dimension. As soon as a third, fixed coordinate is present,
  // arrShow-style navigation selects it by default.
  dimensions.set_dimensions({readout, phase_encode});
  application.processEvents();
  EXPECT_EQ(dimensions.active_dimension_identifier(), QString{});
  EXPECT_FALSE(dimensions.step_active_dimension(1));
  EXPECT_FALSE(dimensions.focus_relative_dimension(1));

  dimensions.set_dimensions({readout, phase_encode}, QStringLiteral("phase-encode"));
  application.processEvents();
  EXPECT_EQ(dimensions.active_dimension_identifier(), QString{});
  const auto* phase_encode_abbreviation =
    find_named_widget<QToolButton>(dimensions, {"kspaceDimensionPhaseEncodeAbbreviation"});
  ASSERT_NE(phase_encode_abbreviation, nullptr);
  ASSERT_NE(phase_encode_abbreviation->parentWidget(), nullptr);
  EXPECT_FALSE(phase_encode_abbreviation->parentWidget()->property("arrShowDimensionActive").toBool());

  dimensions.set_dimensions({readout, phase_encode, coil}, QStringLiteral("readout"));
  application.processEvents();
  EXPECT_EQ(dimensions.active_dimension_identifier(), QStringLiteral("coil"));

  dimensions.set_dimensions({readout, phase_encode, coil, average});
  application.processEvents();
  EXPECT_EQ(dimensions.active_dimension_identifier(), QStringLiteral("coil"));

  const auto initial_axes = ksj::viewer::arrshow_plane_axes_in_column_order({readout, phase_encode, coil, average});
  ASSERT_TRUE(initial_axes.has_value());
  EXPECT_EQ(initial_axes->x_identifier, QStringLiteral("readout"));
  EXPECT_EQ(initial_axes->y_identifier, QStringLiteral("phase-encode"));

  auto inverted_tags_readout = readout;
  auto inverted_tags_phase = phase_encode;
  inverted_tags_readout.selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::second;
  inverted_tags_phase.selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::first;
  const auto column_order_axes =
    ksj::viewer::arrshow_plane_axes_in_column_order({inverted_tags_readout, inverted_tags_phase, coil, average});
  ASSERT_TRUE(column_order_axes.has_value());
  EXPECT_EQ(column_order_axes->x_identifier, QStringLiteral("readout"));
  EXPECT_EQ(column_order_axes->y_identifier, QStringLiteral("phase-encode"));

  QString selected_dimension;
  ksj::viewer::ArrShowDimensionSelectionTag selected_tag = ksj::viewer::ArrShowDimensionSelectionTag::none;
  dimensions.set_selection_tag_changed_callback(
    [&selected_dimension, &selected_tag](const QString& identifier, const auto selection_tag) {
      selected_dimension = identifier;
      selected_tag = selection_tag;
    });
  auto* coil_extent = find_named_widget<QToolButton>(dimensions, {"kspaceDimensionCoilLabel"});
  ASSERT_NE(coil_extent, nullptr);
  EXPECT_TRUE(coil_extent->toolTip().contains(QStringLiteral("blue ':' tag")));
  EXPECT_TRUE(coil_extent->toolTip().contains(QStringLiteral("red ':' tag")));
  QMouseEvent select_first(QEvent::MouseButtonRelease, QPointF{4.0, 4.0}, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(coil_extent, &select_first);
  EXPECT_EQ(selected_dimension, QStringLiteral("coil"));
  EXPECT_EQ(selected_tag, ksj::viewer::ArrShowDimensionSelectionTag::first);
  QMouseEvent select_second(QEvent::MouseButtonRelease, QPointF{4.0, 4.0}, Qt::RightButton, Qt::NoButton,
                            Qt::NoModifier);
  QApplication::sendEvent(coil_extent, &select_second);
  EXPECT_EQ(selected_dimension, QStringLiteral("coil"));
  EXPECT_EQ(selected_tag, ksj::viewer::ArrShowDimensionSelectionTag::second);

  ksj::viewer::ArrShowDimensionSelection selection{.first_identifier = QStringLiteral("readout"),
                                                   .second_identifier = QStringLiteral("phase-encode")};
  ksj::viewer::arrshow_update_dimension_selection(selection, QStringLiteral("coil"),
                                                  ksj::viewer::ArrShowDimensionSelectionTag::second);
  EXPECT_EQ(selection.first_identifier, QStringLiteral("readout"));
  EXPECT_EQ(selection.second_identifier, QStringLiteral("coil"));
  auto coil_plane_readout = readout;
  auto coil_plane_phase = phase_encode;
  auto coil_plane_coil = coil;
  coil_plane_readout.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(selection, readout.identifier);
  coil_plane_phase.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(selection, phase_encode.identifier);
  coil_plane_coil.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(selection, coil.identifier);
  const auto coil_plane_axes =
    ksj::viewer::arrshow_plane_axes_in_column_order({coil_plane_readout, coil_plane_phase, coil_plane_coil});
  ASSERT_TRUE(coil_plane_axes.has_value());
  EXPECT_EQ(coil_plane_axes->x_identifier, QStringLiteral("readout"));
  EXPECT_EQ(coil_plane_axes->y_identifier, QStringLiteral("coil"));

  // The blue/red tag identity follows the selected dimension, not its
  // displayed column number.  After earlier replacements the two ':' columns
  // can be Co/Avg (columns three/four); replacing blue with RO leaves Avg,
  // and the visible column order still determines RO × Avg.
  ksj::viewer::ArrShowDimensionSelection moved_selection{.first_identifier = QStringLiteral("coil"),
                                                         .second_identifier = QStringLiteral("average")};
  ksj::viewer::arrshow_update_dimension_selection(moved_selection, QStringLiteral("readout"),
                                                  ksj::viewer::ArrShowDimensionSelectionTag::first);
  EXPECT_EQ(moved_selection.first_identifier, QStringLiteral("readout"));
  EXPECT_EQ(moved_selection.second_identifier, QStringLiteral("average"));
  auto moved_readout = readout;
  auto moved_phase = phase_encode;
  auto moved_coil = coil;
  auto moved_average = average;
  moved_readout.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(moved_selection, readout.identifier);
  moved_phase.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(moved_selection, phase_encode.identifier);
  moved_coil.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(moved_selection, coil.identifier);
  moved_average.selection_tag = ksj::viewer::arrshow_dimension_selection_tag(moved_selection, average.identifier);
  const auto moved_axes =
    ksj::viewer::arrshow_plane_axes_in_column_order({moved_readout, moved_phase, moved_coil, moved_average});
  ASSERT_TRUE(moved_axes.has_value());
  EXPECT_EQ(moved_axes->x_identifier, QStringLiteral("readout"));
  EXPECT_EQ(moved_axes->y_identifier, QStringLiteral("average"));
}

TEST(KSpaceJetViewerArrShowDimensions, SupportsLockedNativeImageAxesWithoutPermittingAdditionalColonAxes) {
  auto& application = viewer_application();
  ksj::viewer::ArrShowDimensionStrip dimensions(nullptr, QStringLiteral("image"));
  const ksj::viewer::ArrShowDimensionSpec x{
    .identifier = QStringLiteral("x"),
    .label = QStringLiteral("X"),
    .abbreviation = QStringLiteral("X"),
    .observed_values = {0, 1, 2, 3},
    .current_value = 0,
    .displayed_extent = 4,
    .selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::first,
    .selection_change_enabled = false,
  };
  const ksj::viewer::ArrShowDimensionSpec y{
    .identifier = QStringLiteral("y"),
    .label = QStringLiteral("Y"),
    .abbreviation = QStringLiteral("Y"),
    .observed_values = {0, 1, 2},
    .current_value = 0,
    .displayed_extent = 3,
    .selection_tag = ksj::viewer::ArrShowDimensionSelectionTag::second,
    .selection_change_enabled = false,
  };
  const ksj::viewer::ArrShowDimensionSpec z{
    .identifier = QStringLiteral("z"),
    .label = QStringLiteral("Z"),
    .abbreviation = QStringLiteral("Z"),
    .observed_values = {0, 1, 2},
    .current_value = 0,
    .displayed_extent = 3,
    .selection_change_enabled = false,
  };
  const ksj::viewer::ArrShowDimensionSpec channel{
    .identifier = QStringLiteral("channel"),
    .label = QStringLiteral("Channel"),
    .abbreviation = QStringLiteral("Ch"),
    .observed_values = {0, 1},
    .current_value = 0,
    .displayed_extent = 2,
    .selection_change_enabled = false,
  };

  dimensions.set_dimensions({x, y, z, channel});
  application.processEvents();
  const auto axes = ksj::viewer::arrshow_plane_axes_in_column_order({x, y, z, channel});
  ASSERT_TRUE(axes.has_value());
  EXPECT_EQ(axes->x_identifier, QStringLiteral("x"));
  EXPECT_EQ(axes->y_identifier, QStringLiteral("y"));

  const auto* x_value = find_named_widget<QLineEdit>(dimensions, {"imageDimensionXValue"});
  const auto* y_value = find_named_widget<QLineEdit>(dimensions, {"imageDimensionYValue"});
  const auto* z_value = find_named_widget<QLineEdit>(dimensions, {"imageDimensionZValue"});
  auto* z_up = find_named_widget<QAbstractButton>(dimensions, {"imageDimensionZUp"});
  const auto* x_extent = find_named_widget<QToolButton>(dimensions, {"imageDimensionXLabel"});
  const auto* y_extent = find_named_widget<QToolButton>(dimensions, {"imageDimensionYLabel"});
  const auto* z_extent = find_named_widget<QToolButton>(dimensions, {"imageDimensionZLabel"});
  ASSERT_NE(x_value, nullptr);
  ASSERT_NE(y_value, nullptr);
  ASSERT_NE(z_value, nullptr);
  ASSERT_NE(z_up, nullptr);
  ASSERT_NE(x_extent, nullptr);
  ASSERT_NE(y_extent, nullptr);
  ASSERT_NE(z_extent, nullptr);
  EXPECT_EQ(x_value->text(), QStringLiteral(":"));
  EXPECT_EQ(y_value->text(), QStringLiteral(":"));
  EXPECT_TRUE(x_value->isReadOnly());
  EXPECT_TRUE(y_value->isReadOnly());
  EXPECT_EQ(z_value->text(), QStringLiteral("0"));
  EXPECT_FALSE(x_extent->isEnabled());
  EXPECT_FALSE(y_extent->isEnabled());
  EXPECT_FALSE(z_extent->isEnabled());
  EXPECT_TRUE(z_up->isEnabled());

  QString selection_request;
  dimensions.set_selection_tag_changed_callback([&selection_request](const QString& identifier, const auto) {
    selection_request = identifier;
  });
  QMouseEvent attempt_third_axis(QEvent::MouseButtonRelease, QPointF{4.0, 4.0}, Qt::LeftButton, Qt::NoButton,
                                 Qt::NoModifier);
  QApplication::sendEvent(const_cast<QToolButton*>(z_extent), &attempt_third_axis);
  EXPECT_TRUE(selection_request.isEmpty());
  EXPECT_EQ(z_value->text(), QStringLiteral("0"));
}

TEST(KSpaceJetViewerArrShowDisplay, SharesComplexPhaseRangeAndIndependentWindowSemantics) {
  const std::array<double, 5U> real_values{1.0, 0.0, -1.0, 0.0, 0.0};
  const std::array<double, 5U> imaginary_values{0.0, 1.0, 0.0, -1.0, 0.0};
  const auto real = std::span<const double>{real_values};
  const auto imaginary = std::span<const double>{imaginary_values};

  EXPECT_EQ(ksj::viewer::arrshow_display_component_label(ksj::viewer::ArrShowDisplayComponent::complex),
            QStringLiteral("Complex (M)"));
  EXPECT_EQ(ksj::viewer::arrshow_display_component_identifier(ksj::viewer::ArrShowDisplayComponent::phase),
            QStringLiteral("phase"));
  EXPECT_EQ(ksj::viewer::arrshow_phase_representation_label(ksj::viewer::ArrShowPhaseRepresentation::degrees),
            QStringLiteral("Degrees"));
  EXPECT_EQ(ksj::viewer::arrshow_range_calculation_identifier(ksj::viewer::ArrShowRangeCalculation::minimum_maximum),
            QStringLiteral("min-max"));
  EXPECT_EQ(ksj::viewer::arrshow_window_persistence_identifier(ksj::viewer::ArrShowWindowPersistence::reset),
            QStringLiteral("per-plane"));
  EXPECT_EQ(ksj::viewer::arrshow_window_persistence_identifier(ksj::viewer::ArrShowWindowPersistence::relative),
            QStringLiteral("relative"));
  EXPECT_EQ(ksj::viewer::arrshow_window_persistence_identifier(ksj::viewer::ArrShowWindowPersistence::absolute),
            QStringLiteral("absolute"));

  ksj::viewer::ArrShowDisplaySettings complex_settings;
  EXPECT_EQ(complex_settings.component, ksj::viewer::ArrShowDisplayComponent::complex);
  EXPECT_EQ(complex_settings.phase_representation, ksj::viewer::ArrShowPhaseRepresentation::degrees);
  EXPECT_EQ(complex_settings.range_calculation, ksj::viewer::ArrShowRangeCalculation::minimum_maximum);
  EXPECT_EQ(complex_settings.value_window.persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_EQ(complex_settings.phase_window.persistence, ksj::viewer::ArrShowWindowPersistence::relative);

  ksj::viewer::ArrShowDisplayResult complex;
  QString error;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(real, imaginary, 5, 1, complex_settings, complex, error))
    << error.toStdString();
  EXPECT_EQ(complex.image.format(), QImage::Format_RGB32);
  EXPECT_DOUBLE_EQ(complex.source_minimum, 0.0);
  EXPECT_DOUBLE_EQ(complex.source_maximum, 1.0);
  EXPECT_DOUBLE_EQ(complex.applied_window_center, 0.5);
  EXPECT_DOUBLE_EQ(complex.applied_window_width, 1.0);
  EXPECT_EQ(complex.window_persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_NE(complex.image.pixelColor(0, 0), complex.image.pixelColor(1, 0));
  EXPECT_NE(complex.image.pixelColor(1, 0), complex.image.pixelColor(2, 0));
  EXPECT_EQ(complex.image.pixelColor(4, 0), QColor(Qt::black));

  auto absolute_value_settings = complex_settings;
  absolute_value_settings.value_window = {
    .persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 0.5, .width = 2.0};
  ksj::viewer::ArrShowDisplayResult absolute_value;
  ASSERT_TRUE(
    ksj::viewer::render_arrshow_display(real, imaginary, 5, 1, absolute_value_settings, absolute_value, error))
    << error.toStdString();
  EXPECT_EQ(absolute_value.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_NE(absolute_value.image, complex.image);

  auto phase_settings = complex_settings;
  phase_settings.component = ksj::viewer::ArrShowDisplayComponent::phase;
  phase_settings.phase_window = {
    .persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 0.0, .width = 360.0};
  ksj::viewer::ArrShowDisplayResult phase_degrees;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(real, imaginary, 5, 1, phase_settings, phase_degrees, error))
    << error.toStdString();
  EXPECT_EQ(phase_degrees.image.format(), QImage::Format_RGB32);
  EXPECT_EQ(phase_degrees.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_DOUBLE_EQ(phase_degrees.applied_window_center, 0.0);
  EXPECT_DOUBLE_EQ(phase_degrees.applied_window_width, 360.0);
  EXPECT_NEAR(phase_degrees.source_minimum, -90.0, 1.0e-12);
  EXPECT_NEAR(phase_degrees.source_maximum, 180.0, 1.0e-12);

  auto phase_with_other_value_window = phase_settings;
  phase_with_other_value_window.value_window = {
    .persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 1.0, .width = 0.25};
  ksj::viewer::ArrShowDisplayResult phase_same_independent_window;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(real, imaginary, 5, 1, phase_with_other_value_window,
                                                  phase_same_independent_window, error))
    << error.toStdString();
  EXPECT_EQ(phase_same_independent_window.image, phase_degrees.image);

  auto phase_radians_settings = phase_settings;
  ksj::viewer::arrshow_convert_phase_window(phase_radians_settings, ksj::viewer::ArrShowPhaseRepresentation::radians);
  EXPECT_EQ(phase_radians_settings.phase_representation, ksj::viewer::ArrShowPhaseRepresentation::radians);
  EXPECT_EQ(phase_radians_settings.phase_window.persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_DOUBLE_EQ(phase_radians_settings.phase_window.center, 0.0);
  EXPECT_NEAR(phase_radians_settings.phase_window.width, 2.0 * std::numbers::pi_v<double>, 1.0e-12);
  ksj::viewer::ArrShowDisplayResult phase_radians;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(real, imaginary, 5, 1, phase_radians_settings, phase_radians, error))
    << error.toStdString();
  EXPECT_NEAR(phase_radians.source_minimum, -std::numbers::pi_v<double> * 0.5, 1.0e-12);
  EXPECT_NEAR(phase_radians.source_maximum, std::numbers::pi_v<double>, 1.0e-12);
  EXPECT_EQ(phase_radians.image, phase_degrees.image);

  ksj::viewer::arrshow_convert_phase_window(phase_radians_settings, ksj::viewer::ArrShowPhaseRepresentation::degrees);
  EXPECT_EQ(phase_radians_settings.phase_representation, ksj::viewer::ArrShowPhaseRepresentation::degrees);
  EXPECT_NEAR(phase_radians_settings.phase_window.width, 360.0, 1.0e-12);
  ksj::viewer::ArrShowDisplayResult phase_degrees_round_trip;
  ASSERT_TRUE(
    ksj::viewer::render_arrshow_display(real, imaginary, 5, 1, phase_radians_settings, phase_degrees_round_trip, error))
    << error.toStdString();
  EXPECT_EQ(phase_degrees_round_trip.image, phase_degrees.image);

  const std::array<double, 4U> outlier_values{0.0, 1.0, 2.0, 100.0};
  const std::array<double, 4U> zero_imaginary{0.0, 0.0, 0.0, 0.0};
  auto minimum_maximum_settings = complex_settings;
  minimum_maximum_settings.component = ksj::viewer::ArrShowDisplayComponent::magnitude;
  ksj::viewer::ArrShowDisplayResult minimum_maximum;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{outlier_values},
                                                  std::span<const double>{zero_imaginary}, 4, 1,
                                                  minimum_maximum_settings, minimum_maximum, error))
    << error.toStdString();
  EXPECT_DOUBLE_EQ(minimum_maximum.source_minimum, 0.0);
  EXPECT_DOUBLE_EQ(minimum_maximum.source_maximum, 100.0);

  auto percentile_settings = minimum_maximum_settings;
  percentile_settings.range_calculation = ksj::viewer::ArrShowRangeCalculation::percentile;
  percentile_settings.percentile = 75.0;
  ksj::viewer::ArrShowDisplayResult percentile;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{outlier_values},
                                                  std::span<const double>{zero_imaginary}, 4, 1, percentile_settings,
                                                  percentile, error))
    << error.toStdString();
  EXPECT_GT(percentile.source_minimum, minimum_maximum.source_minimum);
  EXPECT_LT(percentile.source_maximum, minimum_maximum.source_maximum);
}

TEST(KSpaceJetViewerArrShowDisplay, UsesRawMagnitudeForComplexPurePhaseDetection) {
  const std::array<double, 4U> real_values{1.0, 1.0, 1.0, 100.0};
  const std::array<double, 4U> imaginary_values{0.0, 0.0, 0.0, 0.0};
  ksj::viewer::ArrShowDisplaySettings settings;
  settings.component = ksj::viewer::ArrShowDisplayComponent::complex;
  settings.range_calculation = ksj::viewer::ArrShowRangeCalculation::percentile;
  settings.percentile = 50.0;

  ksj::viewer::ArrShowDisplayResult result;
  QString error;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(
    std::span<const double>{real_values}, std::span<const double>{imaginary_values}, 4, 1, settings, result, error))
    << error.toStdString();
  EXPECT_EQ(result.window_persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_DOUBLE_EQ(result.source_minimum, 1.0);
  EXPECT_DOUBLE_EQ(result.source_maximum, 1.0);
  EXPECT_DOUBLE_EQ(result.applied_window_center, 1.0);
  EXPECT_DOUBLE_EQ(result.applied_window_width, 0.0);
  EXPECT_EQ(result.image.pixelColor(0, 0), QColor(Qt::black));
  EXPECT_NE(result.image.pixelColor(3, 0), QColor(Qt::black));
}

TEST(KSpaceJetViewerArrShowDisplay, AppliesResetRelativeAndAbsoluteWindowPersistenceAcrossChangedRanges) {
  const std::array<double, 2U> first_real_values{0.0, 10.0};
  const std::array<double, 2U> second_real_values{100.0, 200.0};
  const std::array<double, 2U> imaginary_values{0.0, 0.0};

  ksj::viewer::ArrShowDisplaySettings reset_settings;
  reset_settings.component = ksj::viewer::ArrShowDisplayComponent::magnitude;
  reset_settings.value_window.persistence = ksj::viewer::ArrShowWindowPersistence::reset;
  ksj::viewer::arrshow_set_active_window_value(reset_settings, 0.0, 10.0, 2.5, 5.0);
  EXPECT_TRUE(reset_settings.value_window.has_current_window);

  QString error;
  ksj::viewer::ArrShowDisplayResult first_reset;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{first_real_values},
                                                  std::span<const double>{imaginary_values}, 2, 1, reset_settings,
                                                  first_reset, error))
    << error.toStdString();
  EXPECT_EQ(first_reset.window_persistence, ksj::viewer::ArrShowWindowPersistence::reset);
  EXPECT_DOUBLE_EQ(first_reset.applied_window_center, 2.5);
  EXPECT_DOUBLE_EQ(first_reset.applied_window_width, 5.0);

  ksj::viewer::arrshow_prepare_active_window_for_new_plane(reset_settings);
  EXPECT_FALSE(reset_settings.value_window.has_current_window);
  ksj::viewer::ArrShowDisplayResult second_reset;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{second_real_values},
                                                  std::span<const double>{imaginary_values}, 2, 1, reset_settings,
                                                  second_reset, error))
    << error.toStdString();
  EXPECT_EQ(second_reset.window_persistence, ksj::viewer::ArrShowWindowPersistence::reset);
  EXPECT_DOUBLE_EQ(second_reset.applied_window_center, 150.0);
  EXPECT_DOUBLE_EQ(second_reset.applied_window_width, 100.0);

  ksj::viewer::ArrShowDisplaySettings relative_settings;
  relative_settings.component = ksj::viewer::ArrShowDisplayComponent::magnitude;
  ksj::viewer::arrshow_set_active_window_value(relative_settings, 0.0, 10.0, 2.5, 5.0);
  EXPECT_EQ(relative_settings.value_window.persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_NEAR(relative_settings.value_window.relative_center, 0.25, 1.0e-12);
  EXPECT_NEAR(relative_settings.value_window.relative_width, 0.5, 1.0e-12);

  ksj::viewer::ArrShowDisplayResult first_relative;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{first_real_values},
                                                  std::span<const double>{imaginary_values}, 2, 1, relative_settings,
                                                  first_relative, error))
    << error.toStdString();
  EXPECT_EQ(first_relative.window_persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_DOUBLE_EQ(first_relative.applied_window_center, 2.5);
  EXPECT_DOUBLE_EQ(first_relative.applied_window_width, 5.0);

  ksj::viewer::arrshow_prepare_active_window_for_new_plane(relative_settings);
  EXPECT_TRUE(relative_settings.value_window.has_current_window);
  ksj::viewer::ArrShowDisplayResult second_relative;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{second_real_values},
                                                  std::span<const double>{imaginary_values}, 2, 1, relative_settings,
                                                  second_relative, error))
    << error.toStdString();
  EXPECT_EQ(second_relative.window_persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_DOUBLE_EQ(second_relative.applied_window_center, 125.0);
  EXPECT_DOUBLE_EQ(second_relative.applied_window_width, 50.0);
  EXPECT_NEAR((second_relative.applied_window_center - second_relative.source_minimum) /
                (second_relative.source_maximum - second_relative.source_minimum),
              0.25, 1.0e-12);
  EXPECT_NEAR(second_relative.applied_window_width / (second_relative.source_maximum - second_relative.source_minimum),
              0.5, 1.0e-12);

  auto absolute_settings = relative_settings;
  absolute_settings.value_window.persistence = ksj::viewer::ArrShowWindowPersistence::absolute;
  ksj::viewer::ArrShowDisplayResult first_absolute;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{first_real_values},
                                                  std::span<const double>{imaginary_values}, 2, 1, absolute_settings,
                                                  first_absolute, error))
    << error.toStdString();
  ksj::viewer::arrshow_prepare_active_window_for_new_plane(absolute_settings);
  EXPECT_TRUE(absolute_settings.value_window.has_current_window);
  ksj::viewer::ArrShowDisplayResult second_absolute;
  ASSERT_TRUE(ksj::viewer::render_arrshow_display(std::span<const double>{second_real_values},
                                                  std::span<const double>{imaginary_values}, 2, 1, absolute_settings,
                                                  second_absolute, error))
    << error.toStdString();
  EXPECT_EQ(first_absolute.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_EQ(second_absolute.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_DOUBLE_EQ(first_absolute.applied_window_center, 2.5);
  EXPECT_DOUBLE_EQ(second_absolute.applied_window_center, 2.5);
  EXPECT_DOUBLE_EQ(first_absolute.applied_window_width, 5.0);
  EXPECT_DOUBLE_EQ(second_absolute.applied_window_width, 5.0);
}

TEST(KSpaceJetViewerWindow, PresentsHdfViewInspiredReadOnlyWorkbenchAtDesktopSize) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto pipeline_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-pipeline.json"));
  write_file(pipeline_path, valid_pipeline_definition().toUtf8());

  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);
  QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("KSpaceJet"),
                     QStringLiteral("ksj-viewer"));
  const ViewerFileDialogSettingsScope settings_scope(settings);

  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  EXPECT_EQ(window.size(), QSize(1'280, 800));
  EXPECT_GT(application.palette().color(QPalette::Active, QPalette::Window).lightness(), 128);

  EXPECT_NE(find_named_widget<QWidget>(window, {"viewerRoot"}), nullptr);
  EXPECT_NE(find_named_widget<QWidget>(window, {"viewerMenuBar", "viewerToolbar"}), nullptr);
  EXPECT_NE(find_named_widget<QWidget>(window, {"viewerFileBar"}), nullptr);
  EXPECT_NE(find_named_widget<QWidget>(window, {"offlineReadonlyBadge"}), nullptr);

  const auto* source_file_bar = find_named_widget<QLineEdit>(window, {"sourceFileBar"});
  ASSERT_NE(source_file_bar, nullptr);
  EXPECT_TRUE(source_file_bar->placeholderText().contains(QStringLiteral("ISMRMRD")));
  EXPECT_EQ(find_named_widget<QAbstractButton>(window, {"recentSourcesButton"}), nullptr);
  const auto* file_menu = window.findChild<QMenu*>(QStringLiteral("viewerFileMenu"));
  ASSERT_NE(file_menu, nullptr);
  const auto* recent_files_menu = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
  ASSERT_NE(recent_files_menu, nullptr);
  EXPECT_TRUE(file_menu->actions().contains(recent_files_menu->menuAction()));
  EXPECT_FALSE(recent_files_menu->isEnabled());
  EXPECT_NE(find_named_widget<QAbstractButton>(window, {"clearFileBarButton"}), nullptr);

  const auto* object_inspector = find_named_widget<QTabWidget>(window, {"objectInspector"});
  ASSERT_NE(object_inspector, nullptr);
  EXPECT_EQ(object_inspector->count(), 6);
  EXPECT_EQ(object_inspector->tabText(0), QStringLiteral("Object Attribute Info"));
  EXPECT_EQ(object_inspector->tabText(1), QStringLiteral("General Object Info"));
  EXPECT_EQ(object_inspector->tabText(2), QStringLiteral("K-space"));
  EXPECT_EQ(object_inspector->tabText(3), QStringLiteral("XML"));
  EXPECT_EQ(object_inspector->tabText(4), QStringLiteral("Image"));
  EXPECT_EQ(object_inspector->tabText(5), QStringLiteral("Pipeline"));
  EXPECT_TRUE(object_inspector->isTabVisible(0));
  EXPECT_TRUE(object_inspector->isTabVisible(1));
  EXPECT_FALSE(object_inspector->isTabVisible(2));
  EXPECT_FALSE(object_inspector->isTabVisible(3));
  EXPECT_FALSE(object_inspector->isTabVisible(4));
  EXPECT_FALSE(object_inspector->isTabVisible(5));
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_NE(find_named_widget<QScrollArea>(window, {"generalObjectInfo"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectNameField"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectPathField"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectTypeField"}), nullptr);
  EXPECT_NE(find_named_widget<QLineEdit>(window, {"objectAccessField"}), nullptr);
  EXPECT_NE(find_named_widget<QTableWidget>(window, {"objectSemanticInfoTable"}), nullptr);
  EXPECT_NE(find_named_widget<QTableWidget>(window, {"objectMembersInfoTable"}), nullptr);
  EXPECT_NE(find_named_widget<QTableWidget>(window, {"objectAttributesInfo"}), nullptr);

  EXPECT_EQ(find_named_widget<QTabWidget>(window, {"viewerDataViews"}), nullptr);
  EXPECT_EQ(find_named_widget<QWidget>(window, {"viewerDataSurface"}), nullptr);
  EXPECT_EQ(find_named_widget<QWidget>(window, {"metadataOverviewCard"}), nullptr);
  EXPECT_EQ(find_named_widget<QWidget>(window, {"metadataSeriesCard"}), nullptr);

  EXPECT_NE(window.findChild<QAction*>(QStringLiteral("openMrdAction")), nullptr);
  EXPECT_NE(window.findChild<QAction*>(QStringLiteral("openPipelineAction")), nullptr);
  const auto* close_source = window.findChild<QAction*>(QStringLiteral("closeMrdAction"));
  auto* inspect_object = window.findChild<QAction*>(QStringLiteral("inspectObjectAction"));
  const auto* open_as = window.findChild<QAction*>(QStringLiteral("openAsAction"));
  const auto* copy_path = window.findChild<QAction*>(QStringLiteral("copyObjectPathAction"));
  ASSERT_NE(close_source, nullptr);
  ASSERT_NE(inspect_object, nullptr);
  ASSERT_NE(open_as, nullptr);
  ASSERT_NE(copy_path, nullptr);
  EXPECT_FALSE(close_source->isEnabled());
  EXPECT_FALSE(inspect_object->isEnabled());
  EXPECT_FALSE(open_as->isEnabled());
  EXPECT_FALSE(copy_path->isEnabled());

  auto* dataset_navigation = find_named_widget<QTreeWidget>(window, {"semanticObjectTree"});
  ASSERT_NE(dataset_navigation, nullptr);
  const auto find_root = [dataset_navigation](const QString& name) -> QTreeWidgetItem* {
    for (int index = 0; index < dataset_navigation->topLevelItemCount(); ++index) {
      auto* candidate = dataset_navigation->topLevelItem(index);
      if (candidate != nullptr && candidate->text(0) == name) {
        return candidate;
      }
    }
    return nullptr;
  };
  ASSERT_EQ(dataset_navigation->topLevelItemCount(), 1);
  EXPECT_TRUE(dataset_navigation->topLevelItem(0)->text(0).contains(QStringLiteral("No ISMRMRD source open")));
  EXPECT_FALSE(dataset_navigation->topLevelItem(0)->flags().testFlag(Qt::ItemIsEnabled));
  EXPECT_EQ(find_root(QStringLiteral("Pipeline")), nullptr);

  const auto* export_button = find_named_widget<QAbstractButton>(window, {"exportDisplayButton"});
  ASSERT_NE(export_button, nullptr);
  EXPECT_FALSE(export_button->isEnabled());
  const auto* kspace_view_reset = find_named_widget<QAbstractButton>(window, {"kspaceViewResetButton"});
  ASSERT_NE(kspace_view_reset, nullptr);
  EXPECT_EQ(kspace_view_reset->text(), QStringLiteral("Reset"));
  EXPECT_FALSE(kspace_view_reset->isEnabled());
  EXPECT_EQ(window.findChild<QAbstractButton*>(QStringLiteral("renderKspaceButton")), nullptr);
  const auto* kspace_dimensions = find_named_widget<QWidget>(window, {"kspaceDimensions"});
  ASSERT_NE(kspace_dimensions, nullptr);
  EXPECT_FALSE(kspace_dimensions->isEnabled());
  EXPECT_EQ(window.findChild<QComboBox*>(QStringLiteral("kspaceCoilSelector")), nullptr);
  EXPECT_EQ(window.findChild<QLineEdit*>(QStringLiteral("kspaceDimensionCoilValue")), nullptr);
  const auto* kspace_canvas = find_named_widget<QLabel>(window, {"kspaceCanvas"});
  const auto* kspace_window_center = find_named_widget<QDoubleSpinBox>(window, {"kspaceWindowCenter"});
  const auto* kspace_window_width = find_named_widget<QDoubleSpinBox>(window, {"kspaceWindowWidth"});
  const auto* kspace_zoom = find_named_widget<QSpinBox>(window, {"kspaceZoomPercent"});
  ASSERT_NE(kspace_canvas, nullptr);
  ASSERT_NE(kspace_window_center, nullptr);
  ASSERT_NE(kspace_window_width, nullptr);
  ASSERT_NE(kspace_zoom, nullptr);
  EXPECT_FALSE(kspace_window_center->isEnabled());
  EXPECT_FALSE(kspace_window_width->isEnabled());
  EXPECT_FALSE(kspace_zoom->isEnabled());
  const auto* image_inspect = find_named_widget<QAbstractButton>(window, {"imageInspectButton"});
  ASSERT_NE(image_inspect, nullptr);
  EXPECT_FALSE(image_inspect->isEnabled());
  const auto* image_cine = find_named_widget<QAbstractButton>(window, {"imageCineButton"});
  ASSERT_NE(image_cine, nullptr);
  EXPECT_FALSE(image_cine->isEnabled());
  const auto* image_window_persistence = find_named_widget<QComboBox>(window, {"imageWindowPersistence"});
  ASSERT_NE(image_window_persistence, nullptr);
  EXPECT_EQ(image_window_persistence->currentData().toInt(),
            static_cast<int>(ksj::viewer::ArrShowWindowPersistence::relative));
  EXPECT_FALSE(image_window_persistence->isEnabled());
  const auto* window_center = find_named_widget<QDoubleSpinBox>(window, {"imageWindowCenter"});
  const auto* window_width = find_named_widget<QDoubleSpinBox>(window, {"imageWindowWidth"});
  const auto* image_zoom = find_named_widget<QSpinBox>(window, {"imageZoomPercent"});
  const auto* image_component = find_named_widget<QComboBox>(window, {"imageComponentSelector"});
  const auto* image_canvas = find_named_widget<QLabel>(window, {"imageCanvas"});
  ASSERT_NE(window_center, nullptr);
  ASSERT_NE(window_width, nullptr);
  ASSERT_NE(image_zoom, nullptr);
  ASSERT_NE(image_component, nullptr);
  ASSERT_NE(image_canvas, nullptr);
  EXPECT_FALSE(window_center->isEnabled());
  EXPECT_FALSE(window_width->isEnabled());
  EXPECT_FALSE(image_zoom->isEnabled());
  EXPECT_FALSE(image_component->isEnabled());

  EXPECT_NE(find_named_widget<QWidget>(window, {"metadataEmptyState"}), nullptr);
  const auto* info_panel = find_named_widget<QPlainTextEdit>(window, {"viewerInfoPanel"});
  ASSERT_NE(info_panel, nullptr);
  EXPECT_TRUE(info_panel->toPlainText().contains(QStringLiteral("read-only"), Qt::CaseInsensitive));

  QString pipeline_error;
  ASSERT_TRUE(window.open_pipeline_source(pipeline_path, pipeline_error)) << pipeline_error.toStdString();
  application.processEvents();

  auto* pipeline_item = find_root(QStringLiteral("Pipeline"));
  ASSERT_NE(pipeline_item, nullptr);
  EXPECT_TRUE(pipeline_item->flags().testFlag(Qt::ItemIsEnabled));
  dataset_navigation->setCurrentItem(pipeline_item);
  application.processEvents();
  ASSERT_TRUE(inspect_object->isEnabled());
  inspect_object->trigger();
  application.processEvents();
  EXPECT_TRUE(object_inspector->isTabVisible(5));
  EXPECT_EQ(object_inspector->tabText(object_inspector->currentIndex()), QStringLiteral("Pipeline"));
  const auto* graph_view = find_named_widget<QGraphicsView>(window, {"pipelineGraphCanvas"});
  ASSERT_NE(graph_view, nullptr);
  ASSERT_NE(graph_view->scene(), nullptr);
  EXPECT_FALSE(graph_view->scene()->sceneRect().isEmpty());
  EXPECT_GT(graph_view->scene()->items().size(), 12);
  const auto* prepared_kspace = find_pipeline_graph_edge(*graph_view->scene(), QStringLiteral("prepared_kspace"));
  const auto* prepared_weights = find_pipeline_graph_edge(*graph_view->scene(), QStringLiteral("prepared_weights"));
  const auto* calibration =
    find_pipeline_graph_edge(*graph_view->scene(), QStringLiteral("prepared_calibration:reconstruct.calibration"));
  ASSERT_NE(prepared_kspace, nullptr);
  ASSERT_NE(prepared_weights, nullptr);
  ASSERT_NE(calibration, nullptr);
  ASSERT_GT(prepared_kspace->path().elementCount(), 1);
  ASSERT_GT(prepared_weights->path().elementCount(), 1);
  ASSERT_GT(calibration->path().elementCount(), 1);
  EXPECT_NE(prepared_kspace->path().elementAt(0).y, prepared_weights->path().elementAt(0).y);
  EXPECT_NE(prepared_kspace->path().elementAt(0).y, calibration->path().elementAt(0).y);
  EXPECT_EQ(calibration->pen().style(), Qt::DashLine);
  EXPECT_NE(find_named_widget<QWidget>(window, {"pipelineGraphCard"}), nullptr);
  const auto* graph_legend = find_named_widget<QLabel>(window, {"pipelineGraphLegend"});
  ASSERT_NE(graph_legend, nullptr);
  EXPECT_TRUE(graph_legend->text().contains(QStringLiteral("calibration")));

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerWindow, RemembersOnlySuccessfulOpenDirectoriesForLaterFileDialogs) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto mrd_directory = QDir(temporary_directory.path()).filePath(QStringLiteral("mrd-input"));
  const auto pipeline_directory = QDir(temporary_directory.path()).filePath(QStringLiteral("pipeline-input"));
  ASSERT_TRUE(QDir().mkpath(mrd_directory));
  ASSERT_TRUE(QDir().mkpath(pipeline_directory));
  const auto dataset_path = QDir(mrd_directory).filePath(QStringLiteral("remembered-source.mrd"));
  const auto pipeline_path = QDir(pipeline_directory).filePath(QStringLiteral("remembered-pipeline.json"));
  write_synthetic_dataset(native_path(dataset_path), "dataset_1", false);
  write_file(pipeline_path, valid_pipeline_definition().toUtf8());

  auto& application = viewer_application();
  QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("KSpaceJet"),
                     QStringLiteral("ksj-viewer"));
  const ViewerFileDialogSettingsScope settings_scope(settings);
  const auto settings_key = QStringLiteral("file_dialogs/last_open_directory");

  ksj::viewer::ViewerWindow window;
  QString error;
  ASSERT_TRUE(window.open_mrd_source(dataset_path, error)) << error.toStdString();
  application.processEvents();
  settings.sync();
  EXPECT_EQ(settings.value(settings_key).toString(), QDir::cleanPath(mrd_directory));

  EXPECT_FALSE(window.open_mrd_source(QDir(mrd_directory).filePath(QStringLiteral("missing.mrd")), error));
  settings.sync();
  EXPECT_EQ(settings.value(settings_key).toString(), QDir::cleanPath(mrd_directory));

  ASSERT_TRUE(window.open_pipeline_source(pipeline_path, error)) << error.toStdString();
  application.processEvents();
  settings.sync();
  EXPECT_EQ(settings.value(settings_key).toString(), QDir::cleanPath(pipeline_directory));
}

TEST(KSpaceJetViewerWindow, PersistsFiveTypedRecentFilesAfterSuccessfulOpensOnly) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto mrd_directory = QDir(temporary_directory.path()).filePath(QStringLiteral("mrd-input"));
  const auto pipeline_directory = QDir(temporary_directory.path()).filePath(QStringLiteral("pipeline-input"));
  ASSERT_TRUE(QDir().mkpath(mrd_directory));
  ASSERT_TRUE(QDir().mkpath(pipeline_directory));

  const auto mrd_path = QDir(mrd_directory).filePath(QStringLiteral("recent-source.mrd"));
  write_synthetic_dataset(native_path(mrd_path), "dataset_1", false);
  std::array<QString, 5U> pipeline_paths;
  for (std::size_t index = 0U; index < pipeline_paths.size(); ++index) {
    pipeline_paths[index] =
      QDir(pipeline_directory).filePath(QStringLiteral("recent-pipeline-%1.json").arg(static_cast<int>(index)));
    write_file(pipeline_paths[index], valid_pipeline_definition().toUtf8());
  }

  auto& application = viewer_application();
  QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("KSpaceJet"),
                     QStringLiteral("ksj-viewer"));
  const ViewerFileDialogSettingsScope settings_scope(settings);
  const auto recent_files_key = QStringLiteral("file_dialogs/recent_files");

  const auto read_recent_records = [&settings, &recent_files_key] {
    settings.sync();
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(settings.value(recent_files_key).toString().toUtf8(), &parse_error);
    EXPECT_EQ(parse_error.error, QJsonParseError::NoError);
    EXPECT_TRUE(document.isArray());
    return document.array();
  };
  const auto assert_record = [](const QJsonObject& record, const QString& expected_path, const QString& expected_kind) {
    EXPECT_EQ(record.value(QStringLiteral("path")).toString(), QDir::cleanPath(expected_path));
    EXPECT_EQ(record.value(QStringLiteral("kind")).toString(), expected_kind);
  };

  {
    ksj::viewer::ViewerWindow window;
    QString error;
    ASSERT_TRUE(window.open_mrd_source(mrd_path, error)) << error.toStdString();
    application.processEvents();

    auto records = read_recent_records();
    ASSERT_EQ(records.size(), 1);
    assert_record(records.at(0).toObject(), mrd_path, QStringLiteral("mrd"));

    EXPECT_FALSE(window.open_mrd_source(QDir(mrd_directory).filePath(QStringLiteral("missing.mrd")), error));
    EXPECT_FALSE(window.open_pipeline_source(QDir(pipeline_directory).filePath(QStringLiteral("missing.json")), error));
    records = read_recent_records();
    ASSERT_EQ(records.size(), 1);
    assert_record(records.at(0).toObject(), mrd_path, QStringLiteral("mrd"));

    ASSERT_TRUE(window.open_pipeline_source(pipeline_paths[0], error)) << error.toStdString();
    ASSERT_TRUE(window.open_mrd_source(mrd_path, error)) << error.toStdString();
    application.processEvents();
    records = read_recent_records();
    ASSERT_EQ(records.size(), 2);
    assert_record(records.at(0).toObject(), mrd_path, QStringLiteral("mrd"));
    assert_record(records.at(1).toObject(), pipeline_paths[0], QStringLiteral("pipeline"));

    for (std::size_t index = 1U; index < pipeline_paths.size(); ++index) {
      ASSERT_TRUE(window.open_pipeline_source(pipeline_paths[index], error)) << error.toStdString();
    }
    application.processEvents();

    records = read_recent_records();
    ASSERT_EQ(records.size(), 5);
    assert_record(records.at(0).toObject(), pipeline_paths[4], QStringLiteral("pipeline"));
    assert_record(records.at(1).toObject(), pipeline_paths[3], QStringLiteral("pipeline"));
    assert_record(records.at(2).toObject(), pipeline_paths[2], QStringLiteral("pipeline"));
    assert_record(records.at(3).toObject(), pipeline_paths[1], QStringLiteral("pipeline"));
    assert_record(records.at(4).toObject(), mrd_path, QStringLiteral("mrd"));

    const auto* recent_files_menu = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    ASSERT_NE(recent_files_menu, nullptr);
    ASSERT_TRUE(recent_files_menu->isEnabled());
    const auto recent_actions = recent_files_menu->actions();
    ASSERT_EQ(recent_actions.size(), 5);
    EXPECT_EQ(recent_actions.at(0)->data().toString(), QDir::cleanPath(pipeline_paths[4]));
    EXPECT_TRUE(recent_actions.at(0)->property("isPipelineRecentFile").toBool());
    EXPECT_EQ(recent_actions.at(4)->data().toString(), QDir::cleanPath(mrd_path));
    EXPECT_FALSE(recent_actions.at(4)->property("isPipelineRecentFile").toBool());
  }

  {
    ksj::viewer::ViewerWindow restored_window;
    auto* recent_files_menu = restored_window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    ASSERT_NE(recent_files_menu, nullptr);
    ASSERT_TRUE(recent_files_menu->isEnabled());
    const auto recent_actions = recent_files_menu->actions();
    ASSERT_EQ(recent_actions.size(), 5);
    EXPECT_EQ(recent_actions.at(0)->data().toString(), QDir::cleanPath(pipeline_paths[4]));
    EXPECT_TRUE(recent_actions.at(0)->property("isPipelineRecentFile").toBool());
    EXPECT_EQ(recent_actions.at(4)->data().toString(), QDir::cleanPath(mrd_path));
    EXPECT_FALSE(recent_actions.at(4)->property("isPipelineRecentFile").toBool());

    recent_actions.at(0)->trigger();
    application.processEvents();
    application.processEvents();
    const auto* object_inspector = find_named_widget<QTabWidget>(restored_window, {"objectInspector"});
    ASSERT_NE(object_inspector, nullptr);
    EXPECT_TRUE(object_inspector->isTabVisible(5));
    EXPECT_EQ(object_inspector->tabText(5), QStringLiteral("Pipeline"));
  }

  ASSERT_TRUE(QFile::remove(pipeline_paths[4]));
  {
    ksj::viewer::ViewerWindow pruned_window;
    const auto* recent_files_menu = pruned_window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    ASSERT_NE(recent_files_menu, nullptr);
    const auto recent_actions = recent_files_menu->actions();
    ASSERT_EQ(recent_actions.size(), 4);
    EXPECT_EQ(recent_actions.at(0)->data().toString(), QDir::cleanPath(pipeline_paths[3]));
    EXPECT_TRUE(recent_actions.at(0)->property("isPipelineRecentFile").toBool());
  }
}

TEST(KSpaceJetViewerWindow, LaysOutCalibrationDependenciesAndBoundsPipelineGraph) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto calibration_path = QDir(temporary_directory.path()).filePath(QStringLiteral("calibration-only.json"));
  const auto fan_in_path = QDir(temporary_directory.path()).filePath(QStringLiteral("fan-in.json"));
  const auto over_limit_path = QDir(temporary_directory.path()).filePath(QStringLiteral("over-limit-graph.json"));
  write_file(calibration_path, calibration_only_pipeline_definition().toUtf8());
  write_file(fan_in_path, fan_in_pipeline_definition().toUtf8());
  write_file(over_limit_path, graph_over_limit_pipeline_definition().toUtf8());

  auto& application = viewer_application();
  ksj::viewer::ViewerWindow window;
  QString error;
  ASSERT_TRUE(window.open_pipeline_source(calibration_path, error)) << error.toStdString();
  application.processEvents();

  const auto* graph_view = find_named_widget<QGraphicsView>(window, {"pipelineGraphCanvas"});
  ASSERT_NE(graph_view, nullptr);
  ASSERT_NE(graph_view->scene(), nullptr);
  const auto* calibration =
    find_pipeline_graph_edge(*graph_view->scene(), QStringLiteral("prepared_calibration:reconstruct.calibration"));
  ASSERT_NE(calibration, nullptr);
  const auto& calibration_path_geometry = calibration->path();
  ASSERT_GT(calibration_path_geometry.elementCount(), 1);
  EXPECT_LT(calibration_path_geometry.elementAt(0).x,
            calibration_path_geometry.elementAt(calibration_path_geometry.elementCount() - 1).x);

  ASSERT_TRUE(window.open_pipeline_source(fan_in_path, error)) << error.toStdString();
  application.processEvents();
  const auto* kspace_ingress = find_pipeline_graph_edge(*graph_view->scene(), QStringLiteral("kspace"));
  const auto* weights_ingress = find_pipeline_graph_edge(*graph_view->scene(), QStringLiteral("weights"));
  ASSERT_NE(kspace_ingress, nullptr);
  ASSERT_NE(weights_ingress, nullptr);
  const auto& kspace_path_geometry = kspace_ingress->path();
  const auto& weights_path_geometry = weights_ingress->path();
  ASSERT_GT(kspace_path_geometry.elementCount(), 1);
  ASSERT_GT(weights_path_geometry.elementCount(), 1);
  EXPECT_NE(kspace_path_geometry.elementAt(kspace_path_geometry.elementCount() - 1).y,
            weights_path_geometry.elementAt(weights_path_geometry.elementCount() - 1).y);

  ASSERT_TRUE(window.open_pipeline_source(over_limit_path, error)) << error.toStdString();
  application.processEvents();
  const auto graph_items = graph_view->scene()->items();
  ASSERT_EQ(graph_items.size(), 1);
  const auto* bound_message = dynamic_cast<const QGraphicsTextItem*>(graph_items.constFirst());
  ASSERT_NE(bound_message, nullptr);
  EXPECT_TRUE(bound_message->toPlainText().contains(QStringLiteral("no partial graph is shown")));

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerWindow, SelectsSemanticObjectsBeforeExplicitInspectOpensTheirTypedView) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-workbench.mrd"));
  write_synthetic_dataset(native_path(dataset_path), "dataset_1", false);
  write_synthetic_dataset(native_path(dataset_path), "dataset_2", true);
  write_synthetic_dataset(native_path(dataset_path), "dataset_3", false, true);
  write_uint32_attribute(native_path(dataset_path), "/dataset_2/data", "acquisition_count_hint", 42U);

  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);
  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  QString error;
  ASSERT_TRUE(window.open_mrd_source(dataset_path, error)) << error.toStdString();
  application.processEvents();

  const auto* source_file_bar = find_named_widget<QLineEdit>(window, {"sourceFileBar"});
  auto* tree = find_named_widget<QTreeWidget>(window, {"semanticObjectTree"});
  auto* object_inspector = find_named_widget<QTabWidget>(window, {"objectInspector"});
  auto* object_path = find_named_widget<QLineEdit>(window, {"objectPathField"});
  auto* semantic_table = find_named_widget<QTableWidget>(window, {"objectSemanticInfoTable"});
  auto* attributes_status = find_named_widget<QLabel>(window, {"objectAttributesStatus"});
  auto* attributes = find_named_widget<QTableWidget>(window, {"objectAttributesInfo"});
  auto* inspect = window.findChild<QAction*>(QStringLiteral("inspectObjectAction"));
  auto* close_source = window.findChild<QAction*>(QStringLiteral("closeMrdAction"));
  ASSERT_NE(source_file_bar, nullptr);
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(object_inspector, nullptr);
  ASSERT_NE(object_path, nullptr);
  ASSERT_NE(semantic_table, nullptr);
  ASSERT_NE(attributes_status, nullptr);
  ASSERT_NE(attributes, nullptr);
  ASSERT_NE(inspect, nullptr);
  ASSERT_NE(close_source, nullptr);
  EXPECT_EQ(source_file_bar->text(), dataset_path);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_FALSE(object_inspector->isTabVisible(2));
  EXPECT_FALSE(object_inspector->isTabVisible(3));
  EXPECT_FALSE(object_inspector->isTabVisible(4));
  EXPECT_FALSE(object_inspector->isTabVisible(5));

  tree->setCurrentItem(tree->topLevelItem(0));
  application.processEvents();
  ASSERT_EQ(semantic_table->rowCount(), 3);
  EXPECT_EQ(semantic_table->verticalScrollBar()->maximum(), 0);
  const auto last_semantic_row = semantic_table->rowCount() - 1;
  EXPECT_GE(semantic_table->viewport()->height(),
            semantic_table->rowViewportPosition(last_semantic_row) + semantic_table->rowHeight(last_semantic_row));

  const auto find_container = [tree](const QString& container_path) -> QTreeWidgetItem* {
    auto* source_item = tree->topLevelItem(0);
    if (source_item == nullptr) {
      return nullptr;
    }
    for (int index = 0; index < source_item->childCount(); ++index) {
      auto* candidate = source_item->child(index);
      if (candidate != nullptr && candidate->data(0, Qt::UserRole + 1).toString() == container_path) {
        return candidate;
      }
    }
    return nullptr;
  };
  const auto find_semantic_child = [](QTreeWidgetItem* parent, const QString& prefix) -> QTreeWidgetItem* {
    if (parent == nullptr) {
      return nullptr;
    }
    for (int index = 0; index < parent->childCount(); ++index) {
      auto* candidate = parent->child(index);
      if (candidate != nullptr && candidate->text(0).startsWith(prefix)) {
        return candidate;
      }
    }
    return nullptr;
  };

  auto* first_container = find_container(QStringLiteral("/dataset_1"));
  ASSERT_NE(first_container, nullptr);
  EXPECT_EQ(first_container->text(0), QStringLiteral("/dataset_1"));
  EXPECT_FALSE(first_container->text(0).contains(QStringLiteral("[RAW]")));
  EXPECT_FALSE(first_container->text(0).contains(QStringLiteral("acquisition(s)")));
  EXPECT_TRUE(first_container->toolTip(0).contains(QStringLiteral("Acquisition records: 1")));
  EXPECT_TRUE(first_container->toolTip(0).contains(QStringLiteral("Image series: 0")));
  EXPECT_EQ(first_container->data(0, Qt::AccessibleTextRole).toString(), QStringLiteral("/dataset_1"));
  EXPECT_EQ(first_container->data(0, Qt::AccessibleDescriptionRole).toString(), first_container->toolTip(0));
  EXPECT_EQ(find_semantic_child(first_container, QStringLiteral("Images")), nullptr);
  EXPECT_EQ(find_semantic_child(first_container, QStringLiteral("Waveforms")), nullptr);

  auto* second_container = find_container(QStringLiteral("/dataset_2"));
  ASSERT_NE(second_container, nullptr);
  EXPECT_EQ(second_container->text(0), QStringLiteral("/dataset_2"));
  EXPECT_FALSE(second_container->text(0).contains(QStringLiteral("[IMAGE]")));
  EXPECT_TRUE(second_container->toolTip(0).contains(QStringLiteral("Image series: 1")));
  auto* header_item = find_semantic_child(second_container, QStringLiteral("Header / XML"));
  auto* acquisitions_item = find_semantic_child(second_container, QStringLiteral("Acquisitions / K-space"));
  ASSERT_NE(header_item, nullptr);
  ASSERT_NE(acquisitions_item, nullptr);
  EXPECT_EQ(find_semantic_child(second_container, QStringLiteral("Waveforms")), nullptr);

  auto* waveform_container = find_container(QStringLiteral("/dataset_3"));
  ASSERT_NE(waveform_container, nullptr);
  EXPECT_EQ(find_semantic_child(waveform_container, QStringLiteral("Images")), nullptr);
  auto* waveforms_item = find_semantic_child(waveform_container, QStringLiteral("Waveforms"));
  ASSERT_NE(waveforms_item, nullptr);
  EXPECT_EQ(waveforms_item->text(0), QStringLiteral("Waveforms (discovered)"));
  EXPECT_FALSE(waveforms_item->flags().testFlag(Qt::ItemIsEnabled));
  EXPECT_FALSE(waveforms_item->flags().testFlag(Qt::ItemIsSelectable));

  tree->setCurrentItem(header_item);
  application.processEvents();
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_FALSE(object_inspector->isTabVisible(3));
  EXPECT_EQ(object_path->text(), QStringLiteral("/dataset_2/xml"));
  EXPECT_TRUE(inspect->isEnabled());
  EXPECT_EQ(attributes->rowCount(), 0);
  EXPECT_TRUE(attributes_status->text().contains(QStringLiteral("No HDF5 attributes"), Qt::CaseInsensitive));

  tree->setCurrentItem(acquisitions_item);
  application.processEvents();
  EXPECT_EQ(object_path->text(), QStringLiteral("/dataset_2/data"));
  ASSERT_EQ(attributes->rowCount(), 1);
  EXPECT_EQ(attributes->item(0, 0)->text(), QStringLiteral("acquisition_count_hint"));
  EXPECT_EQ(attributes->item(0, 2)->text(), QStringLiteral("1"));
  EXPECT_EQ(attributes->item(0, 3)->text(), QStringLiteral("42"));
  EXPECT_TRUE(attributes_status->text().contains(QStringLiteral("1 HDF5 attribute"), Qt::CaseInsensitive));

  tree->setCurrentItem(header_item);
  application.processEvents();
  EXPECT_EQ(attributes->rowCount(), 0);
  EXPECT_TRUE(attributes_status->text().contains(QStringLiteral("No HDF5 attributes"), Qt::CaseInsensitive));

  inspect->trigger();
  application.processEvents();
  EXPECT_TRUE(object_inspector->isTabVisible(3));
  EXPECT_EQ(object_inspector->currentIndex(), 3);
  auto* xml_view_modes = find_named_widget<QTabWidget>(window, {"metadataXmlViewModes"});
  const auto* xml_outline = find_named_widget<QTreeWidget>(window, {"metadataXmlOutline"});
  const auto* xml_preview = find_named_widget<QPlainTextEdit>(window, {"metadataXmlPreview"});
  const auto* xml_summary = find_named_widget<QLabel>(window, {"metadataXmlSummary"});
  ASSERT_NE(xml_view_modes, nullptr);
  ASSERT_NE(xml_outline, nullptr);
  ASSERT_NE(xml_preview, nullptr);
  ASSERT_NE(xml_summary, nullptr);
  EXPECT_EQ(xml_view_modes->count(), 2);
  EXPECT_EQ(xml_view_modes->tabText(0), QStringLiteral("XML Tree"));
  EXPECT_EQ(xml_view_modes->tabText(1), QStringLiteral("XML Text"));
  EXPECT_EQ(xml_view_modes->currentIndex(), 0);
  EXPECT_TRUE(xml_outline->isVisible());
  EXPECT_EQ(xml_outline->columnWidth(0), 360);
  EXPECT_EQ(xml_outline->header()->sectionResizeMode(0), QHeaderView::Interactive);
  EXPECT_FALSE(xml_preview->isVisible());
  EXPECT_TRUE(xml_preview->isReadOnly());
  EXPECT_TRUE(xml_summary->isVisible());
  ASSERT_GT(xml_outline->topLevelItemCount(), 0);
  EXPECT_EQ(xml_outline->topLevelItem(0)->text(0), QStringLiteral("ismrmrdHeader"));
  EXPECT_TRUE(xml_summary->text().contains(QStringLiteral("/dataset_2")));
  EXPECT_TRUE(xml_preview->toPlainText().contains(QStringLiteral("ismrmrdHeader")));
  xml_view_modes->setCurrentIndex(1);
  application.processEvents();
  EXPECT_FALSE(xml_outline->isVisible());
  EXPECT_TRUE(xml_preview->isVisible());

  second_container = find_container(QStringLiteral("/dataset_2"));
  ASSERT_NE(second_container, nullptr);
  acquisitions_item = find_semantic_child(second_container, QStringLiteral("Acquisitions / K-space"));
  auto* images_item = find_semantic_child(second_container, QStringLiteral("Images"));
  ASSERT_NE(acquisitions_item, nullptr);
  ASSERT_NE(images_item, nullptr);

  tree->setCurrentItem(acquisitions_item);
  application.processEvents();
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_TRUE(object_inspector->isTabVisible(2));
  EXPECT_EQ(object_inspector->tabText(2), QStringLiteral("K-space"));
  object_inspector->setCurrentIndex(2);
  application.processEvents();
  EXPECT_EQ(object_inspector->currentIndex(), 2);

  tree->setCurrentItem(images_item);
  application.processEvents();
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_FALSE(object_inspector->isTabVisible(2));
  EXPECT_TRUE(object_inspector->isTabVisible(4));
  EXPECT_TRUE(object_path->text().contains(QStringLiteral("/dataset_2")));
  EXPECT_TRUE(inspect->isEnabled());

  inspect->trigger();
  application.processEvents();
  EXPECT_EQ(object_inspector->currentIndex(), 4);
  EXPECT_EQ(attributes->columnCount(), 4);
  EXPECT_EQ(attributes->rowCount(), 0);
  EXPECT_TRUE(attributes_status->text().contains(QStringLiteral("semantic collection"), Qt::CaseInsensitive));
  const auto* image_summary = find_named_widget<QPlainTextEdit>(window, {"imageSummary"});
  ASSERT_NE(image_summary, nullptr);
  EXPECT_TRUE(image_summary->toPlainText().contains(QStringLiteral("MetaAttributes: none")));

  close_source->trigger();
  application.processEvents();
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  EXPECT_TRUE(tree->topLevelItem(0)->text(0).contains(QStringLiteral("No ISMRMRD source open")));
  EXPECT_FALSE(close_source->isEnabled());
  EXPECT_EQ(object_inspector->currentIndex(), 1);
  EXPECT_FALSE(object_inspector->isTabVisible(2));
  EXPECT_FALSE(object_inspector->isTabVisible(3));
  EXPECT_FALSE(object_inspector->isTabVisible(4));

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerWindow, BrowsesObservedCartesianDimensionsWithArrShowControlsWithoutARecordSelector) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-cartesian-window.mrd"));
  write_cartesian_kspace_dataset(native_path(dataset_path));

  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);
  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  QString error;
  ASSERT_TRUE(window.open_mrd_source(dataset_path, error)) << error.toStdString();
  application.processEvents();

  auto* tree = find_named_widget<QTreeWidget>(window, {"semanticObjectTree"});
  auto* object_inspector = find_named_widget<QTabWidget>(window, {"objectInspector"});
  auto* inspect = window.findChild<QAction*>(QStringLiteral("inspectObjectAction"));
  auto* canvas = find_named_widget<QLabel>(window, {"kspaceCanvas"});
  const auto* acquisition_type = find_named_widget<QComboBox>(window, {"kspaceAcquisitionTypeSelector"});
  const auto* display_controls_row = find_named_widget<QWidget>(window, {"kspaceDisplayControlsRow"});
  const auto* dimensions_row = find_named_widget<QWidget>(window, {"kspaceDimensionsRow"});
  const auto* dimensions_label = find_named_widget<QLabel>(window, {"kspaceDimensionsLabel"});
  const auto* dimensions = find_named_widget<QWidget>(window, {"kspaceDimensions"});
  auto* readout_axis = find_named_widget<QLineEdit>(window, {"kspaceDimensionReadoutValue"});
  auto* phase_encode_axis = find_named_widget<QLineEdit>(window, {"kspaceDimensionPhaseEncodeValue"});
  const auto* readout_extent = find_named_widget<QAbstractButton>(window, {"kspaceDimensionReadoutLabel"});
  const auto* phase_encode_extent = find_named_widget<QAbstractButton>(window, {"kspaceDimensionPhaseEncodeLabel"});
  auto* average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  auto* average_up = find_named_widget<QAbstractButton>(window, {"kspaceDimensionAverageUp"});
  auto* average_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionAverageDown"});
  auto* slice_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionSliceValue"});
  auto* slice_up = find_named_widget<QAbstractButton>(window, {"kspaceDimensionSliceUp"});
  auto* slice_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionSliceDown"});
  const auto* slice_extent = find_named_widget<QAbstractButton>(window, {"kspaceDimensionSliceLabel"});
  auto* coil_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionCoilValue"});
  auto* coil_up = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilUp"});
  auto* coil_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilDown"});
  const auto* coil_extent = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilLabel"});
  const auto* component = find_named_widget<QComboBox>(window, {"kspaceComponentSelector"});
  const auto* kspace_window_persistence = find_named_widget<QComboBox>(window, {"kspaceWindowPersistence"});
  auto* kspace_view_reset = find_named_widget<QAbstractButton>(window, {"kspaceViewResetButton"});
  const auto* export_display = find_named_widget<QAbstractButton>(window, {"exportDisplayButton"});
  auto* kspace_window_center = find_named_widget<QDoubleSpinBox>(window, {"kspaceWindowCenter"});
  auto* kspace_window_width = find_named_widget<QDoubleSpinBox>(window, {"kspaceWindowWidth"});
  auto* kspace_window_reset = find_named_widget<QAbstractButton>(window, {"kspaceWindowResetButton"});
  auto* kspace_zoom = find_named_widget<QSpinBox>(window, {"kspaceZoomPercent"});
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(object_inspector, nullptr);
  ASSERT_NE(inspect, nullptr);
  ASSERT_NE(canvas, nullptr);
  ASSERT_NE(acquisition_type, nullptr);
  ASSERT_NE(display_controls_row, nullptr);
  ASSERT_NE(dimensions_row, nullptr);
  ASSERT_NE(dimensions_label, nullptr);
  ASSERT_NE(dimensions, nullptr);
  ASSERT_NE(readout_axis, nullptr);
  ASSERT_NE(phase_encode_axis, nullptr);
  ASSERT_NE(readout_extent, nullptr);
  ASSERT_NE(phase_encode_extent, nullptr);
  ASSERT_NE(average_value, nullptr);
  ASSERT_NE(average_up, nullptr);
  ASSERT_NE(average_down, nullptr);
  ASSERT_NE(slice_value, nullptr);
  ASSERT_NE(slice_up, nullptr);
  ASSERT_NE(slice_down, nullptr);
  ASSERT_NE(slice_extent, nullptr);
  ASSERT_NE(coil_value, nullptr);
  ASSERT_NE(coil_up, nullptr);
  ASSERT_NE(coil_down, nullptr);
  ASSERT_NE(coil_extent, nullptr);
  EXPECT_TRUE(dimensions->isAncestorOf(coil_value));
  ASSERT_NE(component, nullptr);
  ASSERT_NE(kspace_window_persistence, nullptr);
  ASSERT_NE(kspace_view_reset, nullptr);
  EXPECT_EQ(kspace_view_reset->text(), QStringLiteral("Reset"));
  EXPECT_EQ(window.findChild<QAbstractButton*>(QStringLiteral("renderKspaceButton")), nullptr);
  ASSERT_NE(export_display, nullptr);
  EXPECT_NE(window.findChild<QWidget*>(QStringLiteral("kspaceCanvasCard")), nullptr);
  EXPECT_EQ(window.findChild<QWidget*>(QStringLiteral("kspaceViewSplitter")), nullptr);
  EXPECT_EQ(window.findChild<QWidget*>(QStringLiteral("kspaceDetailsCard")), nullptr);
  EXPECT_EQ(window.findChild<QPlainTextEdit*>(QStringLiteral("kspaceSummary")), nullptr);
  EXPECT_EQ(window.findChild<QTableWidget*>(QStringLiteral("kspacePlaneTable")), nullptr);
  ASSERT_NE(kspace_window_center, nullptr);
  ASSERT_NE(kspace_window_width, nullptr);
  ASSERT_NE(kspace_window_reset, nullptr);
  ASSERT_NE(kspace_zoom, nullptr);

  auto* source = tree->topLevelItem(0);
  ASSERT_NE(source, nullptr);
  ASSERT_EQ(source->childCount(), 1);
  auto* container = source->child(0);
  ASSERT_NE(container, nullptr);
  QTreeWidgetItem* acquisitions = nullptr;
  for (int index = 0; index < container->childCount(); ++index) {
    auto* child = container->child(index);
    if (child != nullptr && child->text(0).startsWith(QStringLiteral("Acquisitions / K-space"))) {
      acquisitions = child;
      break;
    }
  }
  ASSERT_NE(acquisitions, nullptr);
  tree->setCurrentItem(acquisitions);
  application.processEvents();
  ASSERT_TRUE(inspect->isEnabled());
  EXPECT_EQ(object_inspector->indexOf(find_named_widget<QWidget>(window, {"kspacePage"})), 2);
  EXPECT_TRUE(object_inspector->isTabVisible(2));
  inspect->trigger();
  application.processEvents();

  EXPECT_EQ(object_inspector->currentIndex(), 2);
  EXPECT_EQ(object_inspector->tabText(2), QStringLiteral("K-space"));
  EXPECT_EQ(acquisition_type->parentWidget(), display_controls_row);
  EXPECT_EQ(component->parentWidget(), display_controls_row);
  EXPECT_EQ(kspace_window_persistence->parentWidget(), display_controls_row);
  EXPECT_EQ(kspace_window_center->parentWidget(), display_controls_row);
  EXPECT_EQ(kspace_window_width->parentWidget(), display_controls_row);
  EXPECT_EQ(kspace_window_reset->parentWidget(), display_controls_row);
  EXPECT_EQ(kspace_zoom->parentWidget(), display_controls_row);
  EXPECT_EQ(kspace_view_reset->parentWidget(), display_controls_row);
  EXPECT_EQ(dimensions->parentWidget(), dimensions_row);
  EXPECT_EQ(dimensions_label->text(), QStringLiteral("Dimensions"));
  EXPECT_LT(display_controls_row->geometry().bottom(), dimensions_row->geometry().top());
  EXPECT_EQ(window.findChild<QSpinBox*>(QStringLiteral("acquisitionOrdinal")), nullptr);
  EXPECT_EQ(window.findChild<QTableWidget*>(QStringLiteral("acquisitionHeaderTable")), nullptr);
  EXPECT_TRUE(acquisition_type->isEnabled());
  ASSERT_EQ(acquisition_type->count(), 1);
  EXPECT_EQ(acquisition_type->currentData().toInt(),
            static_cast<int>(ksj::viewer::CartesianKspaceAcquisitionKind::imaging));
  EXPECT_TRUE(acquisition_type->currentText().startsWith(QStringLiteral("Imaging data")));
  EXPECT_EQ(window.findChild<QComboBox*>(QStringLiteral("kspacePlaneSelector")), nullptr);
  EXPECT_EQ(window.findChild<QComboBox*>(QStringLiteral("kspaceCoilSelector")), nullptr);
  EXPECT_TRUE(dimensions->isEnabled());
  EXPECT_EQ(readout_axis->text(), QStringLiteral(":"));
  EXPECT_TRUE(readout_axis->isReadOnly());
  EXPECT_EQ(phase_encode_axis->text(), QStringLiteral(":"));
  EXPECT_TRUE(phase_encode_axis->isReadOnly());
  EXPECT_EQ(readout_extent->text(), QStringLiteral("4"));
  EXPECT_EQ(phase_encode_extent->text(), QStringLiteral("3"));
  EXPECT_EQ(average_value->text(), QStringLiteral("0"));
  EXPECT_EQ(slice_value->text(), QStringLiteral("0"));
  EXPECT_EQ(slice_extent->text(), QStringLiteral("2"));
  EXPECT_TRUE(slice_up->isEnabled());
  EXPECT_FALSE(slice_down->isEnabled());
  struct KspaceDimensionExpectation final {
    QString suffix;
    QString abbreviation;
    QString label;
    QString value;
    QString extent;
    bool read_only{false};
    bool increment_enabled{false};
    bool decrement_enabled{false};
  };
  // The two display axes are always present. A non-axis ISMRMRD coordinate
  // appears only when this data actually has more than one observed value.
  const std::array<KspaceDimensionExpectation, 12U> expected_dimensions{{
    {QStringLiteral("Readout"), QStringLiteral("RO"), QStringLiteral("Readout"), QStringLiteral(":"),
     QStringLiteral("4"), true, false, false},
    {QStringLiteral("PhaseEncode"), QStringLiteral("PE"), QStringLiteral("Phase encode"), QStringLiteral(":"),
     QStringLiteral("3"), true, false, false},
    {QStringLiteral("Coil"), QStringLiteral("Co"), QStringLiteral("Raw coil"), QStringLiteral("0"), QStringLiteral("2"),
     false, true, false},
    {QStringLiteral("Partition"), QStringLiteral("Par"), QStringLiteral("Partition"), QStringLiteral("0"),
     QStringLiteral("2"), false, true, false},
    {QStringLiteral("Average"), QStringLiteral("Avg"), QStringLiteral("Average"), QStringLiteral("0"),
     QStringLiteral("2"), false, true, false},
    {QStringLiteral("Slice"), QStringLiteral("Slc"), QStringLiteral("Slice"), QStringLiteral("0"), QStringLiteral("2"),
     false, true, false},
    {QStringLiteral("Contrast"), QStringLiteral("Con"), QStringLiteral("Contrast"), QStringLiteral("0"),
     QStringLiteral("2"), false, true, false},
    {QStringLiteral("PhysiologicalPhase"), QStringLiteral("Pha"), QStringLiteral("Physiological phase"),
     QStringLiteral("0"), QStringLiteral("2"), false, true, false},
    {QStringLiteral("Repetition"), QStringLiteral("Rep"), QStringLiteral("Repetition"), QStringLiteral("0"),
     QStringLiteral("2"), false, true, false},
    {QStringLiteral("Set"), QStringLiteral("Set"), QStringLiteral("Set"), QStringLiteral("0"), QStringLiteral("2"),
     false, true, false},
    {QStringLiteral("Segment"), QStringLiteral("Seg"), QStringLiteral("Segment"), QStringLiteral("0"),
     QStringLiteral("2"), false, true, false},
    {QStringLiteral("User7"), QStringLiteral("U7"), QStringLiteral("User 7"), QStringLiteral("0"), QStringLiteral("2"),
     false, true, false},
  }};
  QStringList expected_value_names;
  QStringList expected_abbreviation_names;
  QStringList expected_label_names;
  for (const auto& expectation : expected_dimensions) {
    const auto base_name = QStringLiteral("kspaceDimension") + expectation.suffix;
    const auto* dimension_value = window.findChild<QLineEdit*>(base_name + QStringLiteral("Value"));
    const auto* dimension_increment = window.findChild<QAbstractButton*>(base_name + QStringLiteral("Up"));
    const auto* dimension_decrement = window.findChild<QAbstractButton*>(base_name + QStringLiteral("Down"));
    const auto* dimension_abbreviation = window.findChild<QToolButton*>(base_name + QStringLiteral("Abbreviation"));
    const auto* dimension_extent = window.findChild<QAbstractButton*>(base_name + QStringLiteral("Label"));
    ASSERT_NE(dimension_value, nullptr) << base_name.toStdString();
    ASSERT_NE(dimension_increment, nullptr) << base_name.toStdString();
    ASSERT_NE(dimension_decrement, nullptr) << base_name.toStdString();
    ASSERT_NE(dimension_abbreviation, nullptr) << base_name.toStdString();
    ASSERT_NE(dimension_extent, nullptr) << base_name.toStdString();
    EXPECT_EQ(dimension_abbreviation->text(), expectation.abbreviation) << base_name.toStdString();
    EXPECT_TRUE(dimension_abbreviation->toolTip().contains(expectation.label)) << base_name.toStdString();
    EXPECT_FALSE(dimension_abbreviation->toolTip().contains(QStringLiteral("idx."))) << base_name.toStdString();
    EXPECT_EQ(dimension_value->text(), expectation.value) << base_name.toStdString();
    EXPECT_EQ(dimension_value->isReadOnly(), expectation.read_only) << base_name.toStdString();
    EXPECT_EQ(dimension_extent->text(), expectation.extent) << base_name.toStdString();
    EXPECT_EQ(dimension_increment->isEnabled(), expectation.increment_enabled) << base_name.toStdString();
    EXPECT_EQ(dimension_decrement->isEnabled(), expectation.decrement_enabled) << base_name.toStdString();
    EXPECT_FALSE(dimension_value->toolTip().contains(QStringLiteral("idx."))) << base_name.toStdString();
    expected_value_names.append(base_name + QStringLiteral("Value"));
    expected_abbreviation_names.append(base_name + QStringLiteral("Abbreviation"));
    expected_label_names.append(base_name + QStringLiteral("Label"));
  }
  QStringList actual_value_names;
  QStringList actual_abbreviation_names;
  QStringList actual_label_names;
  for (const auto* dimension_value : dimensions->findChildren<QLineEdit*>()) {
    if (dimension_value->objectName().startsWith(QStringLiteral("kspaceDimension")) &&
        dimension_value->objectName().endsWith(QStringLiteral("Value"))) {
      actual_value_names.append(dimension_value->objectName());
    }
  }
  for (const auto* dimension_abbreviation : dimensions->findChildren<QToolButton*>()) {
    if (dimension_abbreviation->objectName().startsWith(QStringLiteral("kspaceDimension")) &&
        dimension_abbreviation->objectName().endsWith(QStringLiteral("Abbreviation"))) {
      actual_abbreviation_names.append(dimension_abbreviation->objectName());
    }
  }
  for (const auto* dimension_label : dimensions->findChildren<QAbstractButton*>()) {
    if (dimension_label->objectName().startsWith(QStringLiteral("kspaceDimension")) &&
        dimension_label->objectName().endsWith(QStringLiteral("Label"))) {
      actual_label_names.append(dimension_label->objectName());
    }
  }
  std::sort(expected_value_names.begin(), expected_value_names.end());
  std::sort(expected_abbreviation_names.begin(), expected_abbreviation_names.end());
  std::sort(expected_label_names.begin(), expected_label_names.end());
  std::sort(actual_value_names.begin(), actual_value_names.end());
  std::sort(actual_abbreviation_names.begin(), actual_abbreviation_names.end());
  std::sort(actual_label_names.begin(), actual_label_names.end());
  EXPECT_EQ(actual_value_names, expected_value_names);
  EXPECT_EQ(actual_abbreviation_names, expected_abbreviation_names);
  EXPECT_EQ(actual_label_names, expected_label_names);
  for (const auto& singleton_suffix :
       {QStringLiteral("EncodingSpace"), QStringLiteral("User0"), QStringLiteral("User1"), QStringLiteral("User2"),
        QStringLiteral("User3"), QStringLiteral("User4"), QStringLiteral("User5"), QStringLiteral("User6")}) {
    const auto base_name = QStringLiteral("kspaceDimension") + singleton_suffix;
    EXPECT_EQ(window.findChild<QLineEdit*>(base_name + QStringLiteral("Value")), nullptr);
    EXPECT_EQ(window.findChild<QAbstractButton*>(base_name + QStringLiteral("Label")), nullptr);
  }
  EXPECT_TRUE(kspace_view_reset->isEnabled());
  EXPECT_EQ(coil_value->text(), QStringLiteral("0"));
  EXPECT_EQ(coil_extent->text(), QStringLiteral("2"));
  EXPECT_TRUE(coil_up->isEnabled());
  EXPECT_FALSE(coil_down->isEnabled());
  EXPECT_FALSE(coil_value->toolTip().contains(QStringLiteral("idx.")));
  EXPECT_EQ(component->currentData().toInt(), static_cast<int>(ksj::viewer::ArrShowDisplayComponent::complex));
  EXPECT_FALSE(canvas->pixmap(Qt::ReturnByValue).isNull());
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());
  EXPECT_TRUE(export_display->isEnabled());

  const auto assert_current_coil_is_rendered = [&](const int expected_coil) {
    QCoreApplication::sendPostedEvents();
    application.processEvents();
    EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
    const auto* current_coil = find_named_widget<QLineEdit>(window, {"kspaceDimensionCoilValue"});
    ASSERT_NE(current_coil, nullptr);
    EXPECT_EQ(current_coil->text(), QString::number(expected_coil));
    EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());
    EXPECT_FALSE(canvas->pixmap(Qt::ReturnByValue).isNull());
    EXPECT_TRUE(export_display->isEnabled());
  };

  coil_up->click();
  assert_current_coil_is_rendered(1);

  coil_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilDown"});
  ASSERT_NE(coil_down, nullptr);
  coil_down->click();
  assert_current_coil_is_rendered(0);

  coil_up = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilUp"});
  ASSERT_NE(coil_up, nullptr);
  coil_up->click();
  assert_current_coil_is_rendered(1);

  slice_up->click();
  application.processEvents();
  EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
  slice_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionSliceValue"});
  ASSERT_NE(slice_value, nullptr);
  EXPECT_EQ(slice_value->text(), QStringLiteral("1"));
  // Coil is an arrShow dimension too. This coordinate exposes both source
  // coils, so navigating a plane coordinate retains the selected Coil=1.
  assert_current_coil_is_rendered(1);
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());

  // arrShow value changers stop at their actual observed endpoints. The
  // source has only Slice={0, 1}; a second increment must not wrap to 0.
  slice_up = find_named_widget<QAbstractButton>(window, {"kspaceDimensionSliceUp"});
  slice_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionSliceDown"});
  ASSERT_NE(slice_up, nullptr);
  ASSERT_NE(slice_down, nullptr);
  EXPECT_FALSE(slice_up->isEnabled());
  EXPECT_TRUE(slice_down->isEnabled());
  slice_up->click();
  application.processEvents();
  slice_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionSliceValue"});
  ASSERT_NE(slice_value, nullptr);
  EXPECT_EQ(slice_value->text(), QStringLiteral("1"));

  // Average=1 and Slice=1 are separately observed, but their Cartesian
  // product is not. Switching the Average changer must choose the actual
  // Average=1 plane and therefore restore Slice=0 rather than synthesize one.
  average_up = find_named_widget<QAbstractButton>(window, {"kspaceDimensionAverageUp"});
  ASSERT_NE(average_up, nullptr);
  EXPECT_TRUE(average_up->isEnabled());
  average_up->click();
  application.processEvents();
  average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  slice_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionSliceValue"});
  ASSERT_NE(average_value, nullptr);
  ASSERT_NE(slice_value, nullptr);
  EXPECT_EQ(average_value->text(), QStringLiteral("1"));
  EXPECT_EQ(slice_value->text(), QStringLiteral("0"));
  EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
  assert_current_coil_is_rendered(1);
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());

  const QPointF canvas_center = canvas->rect().center();
  average_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionAverageDown"});
  ASSERT_NE(average_down, nullptr);
  average_down->click();
  application.processEvents();
  average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  ASSERT_NE(average_value, nullptr);
  EXPECT_EQ(average_value->text(), QStringLiteral("0"));

  // The canvas keeps arrShow's normal-wheel dimension stepping. Average was
  // made active above, so a positive wheel step selects its next *observed*
  // value, not the next full coordinate tuple.
  QWheelEvent kspace_wheel(canvas_center, canvas_center, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas, &kspace_wheel);
  application.processEvents();
  average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  slice_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionSliceValue"});
  ASSERT_NE(average_value, nullptr);
  ASSERT_NE(slice_value, nullptr);
  EXPECT_EQ(average_value->text(), QStringLiteral("1"));
  EXPECT_EQ(slice_value->text(), QStringLiteral("0"));
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());
  const auto wheel_derivative = canvas->pixmap(Qt::ReturnByValue).toImage();
  QWheelEvent kspace_boundary_wheel(canvas_center, canvas_center, QPoint{}, QPoint{0, 120}, Qt::NoButton,
                                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas, &kspace_boundary_wheel);
  application.processEvents();
  average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  ASSERT_NE(average_value, nullptr);
  EXPECT_EQ(average_value->text(), QStringLiteral("1"));
  QWheelEvent kspace_zoom_wheel(canvas_center, canvas_center, QPoint{}, QPoint{0, 120}, Qt::NoButton,
                                Qt::ControlModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas, &kspace_zoom_wheel);
  average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  ASSERT_NE(average_value, nullptr);
  EXPECT_EQ(average_value->text(), QStringLiteral("1"));
  EXPECT_GT(kspace_zoom->value(), 100);
  kspace_window_center->setValue(2.0);
  kspace_window_width->setValue(1.0);
  application.processEvents();
  EXPECT_NE(canvas->pixmap(Qt::ReturnByValue).toImage(), wheel_derivative);
  kspace_view_reset->click();
  application.processEvents();
  EXPECT_EQ(kspace_zoom->value(), 100);
  EXPECT_DOUBLE_EQ(kspace_window_center->value(), 2.0);
  EXPECT_DOUBLE_EQ(kspace_window_width->value(), 1.0);
  kspace_window_reset->click();
  application.processEvents();
  EXPECT_EQ(canvas->pixmap(Qt::ReturnByValue).toImage(), wheel_derivative);

  // The arrShow convention uses Left/Right to choose which dimension normal
  // wheel movement changes. From Average, Right selects Slice; the next wheel
  // step must therefore reach the actual Slice=1 coordinate and reset Average
  // to 0 rather than traversing a hidden tuple list.
  canvas->setFocus(Qt::OtherFocusReason);
  QKeyEvent next_dimension(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
  QApplication::sendEvent(canvas, &next_dimension);
  QWheelEvent slice_wheel(canvas_center, canvas_center, QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas, &slice_wheel);
  application.processEvents();
  average_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionAverageValue"});
  slice_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionSliceValue"});
  ASSERT_NE(average_value, nullptr);
  ASSERT_NE(slice_value, nullptr);
  EXPECT_EQ(average_value->text(), QStringLiteral("0"));
  EXPECT_EQ(slice_value->text(), QStringLiteral("1"));

  // A third `:` selection replaces the requested arrShow tag. Here the
  // second tag moves from PE to Co; X/Y are then normalized by column order,
  // so PE becomes editable and PE=0 produces a real RO × Co plane.
  auto* coil_axis_label = find_named_widget<QToolButton>(window, {"kspaceDimensionCoilLabel"});
  ASSERT_NE(coil_axis_label, nullptr);
  QMouseEvent select_coil_as_y(QEvent::MouseButtonRelease, QPointF{4.0, 4.0}, Qt::RightButton, Qt::NoButton,
                               Qt::NoModifier);
  QApplication::sendEvent(coil_axis_label, &select_coil_as_y);
  application.processEvents();

  readout_axis = find_named_widget<QLineEdit>(window, {"kspaceDimensionReadoutValue"});
  auto* coil_axis = find_named_widget<QLineEdit>(window, {"kspaceDimensionCoilValue"});
  phase_encode_axis = find_named_widget<QLineEdit>(window, {"kspaceDimensionPhaseEncodeValue"});
  ASSERT_NE(readout_axis, nullptr);
  ASSERT_NE(coil_axis, nullptr);
  ASSERT_NE(phase_encode_axis, nullptr);
  EXPECT_EQ(readout_axis->text(), QStringLiteral(":"));
  EXPECT_EQ(coil_axis->text(), QStringLiteral(":"));
  EXPECT_TRUE(readout_axis->isReadOnly());
  EXPECT_TRUE(coil_axis->isReadOnly());
  EXPECT_FALSE(phase_encode_axis->isReadOnly());
  EXPECT_NE(phase_encode_axis->text(), QStringLiteral(":"));

  for (int attempt = 0; attempt < 3 && phase_encode_axis->text() != QStringLiteral("0"); ++attempt) {
    auto* phase_encode_down = find_named_widget<QAbstractButton>(window, {"kspaceDimensionPhaseEncodeDown"});
    ASSERT_NE(phase_encode_down, nullptr);
    ASSERT_TRUE(phase_encode_down->isEnabled());
    phase_encode_down->click();
    application.processEvents();
    phase_encode_axis = find_named_widget<QLineEdit>(window, {"kspaceDimensionPhaseEncodeValue"});
    ASSERT_NE(phase_encode_axis, nullptr);
  }
  EXPECT_EQ(phase_encode_axis->text(), QStringLiteral("0"));
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());
  EXPECT_FALSE(canvas->pixmap(Qt::ReturnByValue).isNull());

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerWindow, LocksNativeImageXYAxesAndBrowsesOnlyLaterImageDimensions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-fixed-image-axes.mrd"));
  write_fixed_axis_image_dataset(native_path(dataset_path));

  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);
  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  QString error;
  ASSERT_TRUE(window.open_mrd_source(dataset_path, error)) << error.toStdString();
  application.processEvents();

  const auto* dimensions = find_named_widget<QWidget>(window, {"imageDimensions"});
  auto* x = find_named_widget<QLineEdit>(window, {"imageDimensionXValue"});
  auto* y = find_named_widget<QLineEdit>(window, {"imageDimensionYValue"});
  auto* z = find_named_widget<QLineEdit>(window, {"imageDimensionZValue"});
  auto* channel = find_named_widget<QLineEdit>(window, {"imageDimensionChannelValue"});
  const auto* x_label = find_named_widget<QToolButton>(window, {"imageDimensionXLabel"});
  const auto* y_label = find_named_widget<QToolButton>(window, {"imageDimensionYLabel"});
  const auto* z_label = find_named_widget<QToolButton>(window, {"imageDimensionZLabel"});
  const auto* channel_label = find_named_widget<QToolButton>(window, {"imageDimensionChannelLabel"});
  auto* z_up = find_named_widget<QAbstractButton>(window, {"imageDimensionZUp"});
  auto* inspect = find_named_widget<QAbstractButton>(window, {"imageInspectButton"});
  auto* canvas = find_named_widget<QLabel>(window, {"imageCanvas"});
  ASSERT_NE(dimensions, nullptr);
  ASSERT_NE(x, nullptr);
  ASSERT_NE(y, nullptr);
  ASSERT_NE(z, nullptr);
  ASSERT_NE(channel, nullptr);
  ASSERT_NE(x_label, nullptr);
  ASSERT_NE(y_label, nullptr);
  ASSERT_NE(z_label, nullptr);
  ASSERT_NE(channel_label, nullptr);
  ASSERT_NE(z_up, nullptr);
  ASSERT_NE(inspect, nullptr);
  ASSERT_NE(canvas, nullptr);
  EXPECT_TRUE(dimensions->isAncestorOf(z));
  EXPECT_EQ(x->text(), QStringLiteral(":"));
  EXPECT_EQ(y->text(), QStringLiteral(":"));
  EXPECT_TRUE(x->isReadOnly());
  EXPECT_TRUE(y->isReadOnly());
  EXPECT_EQ(z->text(), QStringLiteral("0"));
  EXPECT_EQ(channel->text(), QStringLiteral("0"));
  EXPECT_FALSE(x_label->isEnabled());
  EXPECT_FALSE(y_label->isEnabled());
  EXPECT_FALSE(z_label->isEnabled());
  EXPECT_FALSE(channel_label->isEnabled());

  QMouseEvent attempt_z_axis(QEvent::MouseButtonRelease, QPointF{4.0, 4.0}, Qt::LeftButton, Qt::NoButton,
                             Qt::NoModifier);
  QApplication::sendEvent(const_cast<QToolButton*>(z_label), &attempt_z_axis);
  application.processEvents();
  EXPECT_EQ(x->text(), QStringLiteral(":"));
  EXPECT_EQ(y->text(), QStringLiteral(":"));
  EXPECT_EQ(z->text(), QStringLiteral("0"));

  inspect->click();
  application.processEvents();
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());
  z_up->click();
  application.processEvents();
  z = find_named_widget<QLineEdit>(window, {"imageDimensionZValue"});
  ASSERT_NE(z, nullptr);
  EXPECT_EQ(z->text(), QStringLiteral("1"));
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerWindow, DefaultsAnAuxiliaryOnlyContainerToItsFirstRenderableKspaceType) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-auxiliary-only-cartesian-window.mrd"));
  write_auxiliary_only_cartesian_kspace_dataset(native_path(dataset_path));

  auto& application = viewer_application();
  ksj::viewer::apply_viewer_theme(application);
  ksj::viewer::ViewerWindow window;
  window.resize(1'280, 800);
  window.show();
  application.processEvents();

  QString error;
  ASSERT_TRUE(window.open_mrd_source(dataset_path, error)) << error.toStdString();
  application.processEvents();

  auto* tree = find_named_widget<QTreeWidget>(window, {"semanticObjectTree"});
  auto* object_inspector = find_named_widget<QTabWidget>(window, {"objectInspector"});
  auto* inspect = window.findChild<QAction*>(QStringLiteral("inspectObjectAction"));
  auto* acquisition_type = find_named_widget<QComboBox>(window, {"kspaceAcquisitionTypeSelector"});
  const auto* dimensions = find_named_widget<QWidget>(window, {"kspaceDimensions"});
  const auto* coil_value = find_named_widget<QLineEdit>(window, {"kspaceDimensionCoilValue"});
  const auto* coil_extent = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilLabel"});
  const auto* kspace_view_reset = find_named_widget<QAbstractButton>(window, {"kspaceViewResetButton"});
  const auto* canvas = find_named_widget<QLabel>(window, {"kspaceCanvas"});
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(object_inspector, nullptr);
  ASSERT_NE(inspect, nullptr);
  ASSERT_NE(acquisition_type, nullptr);
  ASSERT_NE(dimensions, nullptr);
  ASSERT_NE(coil_value, nullptr);
  ASSERT_NE(coil_extent, nullptr);
  EXPECT_TRUE(dimensions->isAncestorOf(coil_value));
  ASSERT_NE(kspace_view_reset, nullptr);
  EXPECT_EQ(kspace_view_reset->text(), QStringLiteral("Reset"));
  EXPECT_EQ(window.findChild<QAbstractButton*>(QStringLiteral("renderKspaceButton")), nullptr);
  EXPECT_EQ(window.findChild<QWidget*>(QStringLiteral("kspaceDetailsCard")), nullptr);
  EXPECT_EQ(window.findChild<QPlainTextEdit*>(QStringLiteral("kspaceSummary")), nullptr);
  EXPECT_EQ(window.findChild<QTableWidget*>(QStringLiteral("kspacePlaneTable")), nullptr);
  ASSERT_NE(canvas, nullptr);

  auto* source = tree->topLevelItem(0);
  ASSERT_NE(source, nullptr);
  ASSERT_EQ(source->childCount(), 1);
  auto* container = source->child(0);
  ASSERT_NE(container, nullptr);
  QTreeWidgetItem* acquisitions = nullptr;
  for (int index = 0; index < container->childCount(); ++index) {
    auto* child = container->child(index);
    if (child != nullptr && child->text(0).startsWith(QStringLiteral("Acquisitions / K-space"))) {
      acquisitions = child;
      break;
    }
  }
  ASSERT_NE(acquisitions, nullptr);
  tree->setCurrentItem(acquisitions);
  application.processEvents();
  ASSERT_TRUE(inspect->isEnabled());
  inspect->trigger();
  application.processEvents();

  EXPECT_EQ(object_inspector->currentIndex(), 2);
  EXPECT_TRUE(acquisition_type->isEnabled());
  ASSERT_EQ(acquisition_type->currentData().toInt(),
            static_cast<int>(ksj::viewer::CartesianKspaceAcquisitionKind::noise_measurement));
  EXPECT_TRUE(acquisition_type->currentText().startsWith(QStringLiteral("Noise measurement")));
  EXPECT_EQ(window.findChild<QComboBox*>(QStringLiteral("kspaceCoilSelector")), nullptr);
  EXPECT_EQ(coil_value->text(), QStringLiteral("0"));
  EXPECT_EQ(coil_extent->text(), QStringLiteral("2"));
  EXPECT_TRUE(kspace_view_reset->isEnabled());
  EXPECT_FALSE(canvas->pixmap(Qt::ReturnByValue).isNull());
  EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());

  const auto assert_current_type_is_rendered = [&](const ksj::viewer::CartesianKspaceAcquisitionKind expected_kind) {
    QCoreApplication::sendPostedEvents();
    application.processEvents();
    EXPECT_EQ(QApplication::activeModalWidget(), nullptr);
    EXPECT_EQ(acquisition_type->currentData().toInt(), static_cast<int>(expected_kind));
    const auto* selected_coil = find_named_widget<QLineEdit>(window, {"kspaceDimensionCoilValue"});
    const auto* selected_coil_extent = find_named_widget<QAbstractButton>(window, {"kspaceDimensionCoilLabel"});
    ASSERT_NE(selected_coil, nullptr);
    ASSERT_NE(selected_coil_extent, nullptr);
    EXPECT_EQ(selected_coil->text(), QStringLiteral("0"));
    EXPECT_EQ(selected_coil_extent->text(), QStringLiteral("2"));
    EXPECT_TRUE(kspace_view_reset->isEnabled());
    EXPECT_TRUE(canvas->property("hasDisplayImage").toBool());
    EXPECT_FALSE(canvas->pixmap(Qt::ReturnByValue).isNull());
  };
  assert_current_type_is_rendered(ksj::viewer::CartesianKspaceAcquisitionKind::noise_measurement);

  const auto surface_index =
    acquisition_type->findData(static_cast<int>(ksj::viewer::CartesianKspaceAcquisitionKind::surface_coil_correction));
  ASSERT_GE(surface_index, 0);
  acquisition_type->setCurrentIndex(surface_index);
  assert_current_type_is_rendered(ksj::viewer::CartesianKspaceAcquisitionKind::surface_coil_correction);

  const auto noise_index =
    acquisition_type->findData(static_cast<int>(ksj::viewer::CartesianKspaceAcquisitionKind::noise_measurement));
  ASSERT_GE(noise_index, 0);
  acquisition_type->setCurrentIndex(noise_index);
  assert_current_type_is_rendered(ksj::viewer::CartesianKspaceAcquisitionKind::noise_measurement);

  window.close();
  application.processEvents();
}

TEST(KSpaceJetViewerPresentation, InspectsMetadataAndImagesAndRejectsNonCartesianKspaceAsABoundedDisplayDerivative) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-source.mrd"));
  write_synthetic_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset"));
  const auto opened_source_path = session.source_path();
  EXPECT_FALSE(session.open_mrd(QString{}, error));
  EXPECT_EQ(error, QStringLiteral("an ISMRMRD file path is required"));
  EXPECT_TRUE(session.is_open());
  EXPECT_EQ(session.source_path(), opened_source_path);

  const auto metadata = ksj::viewer::make_metadata_presentation(session);
  EXPECT_TRUE(metadata.summary.contains(QStringLiteral("Acquisitions: 1")));
  EXPECT_TRUE(metadata.xml_preview.contains(QStringLiteral("ismrmrdHeader")));
  ASSERT_EQ(metadata.csv_rows.size(), 1);
  EXPECT_EQ(metadata.csv_rows.front().front(), QString::fromLatin1(kImageSeries.data(), kImageSeries.size()));
  EXPECT_EQ(metadata.details.value(QStringLiteral("view")).toString(), QStringLiteral("metadata"));
  EXPECT_EQ(metadata.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(metadata.details.value(QStringLiteral("source")).toString(),
            QStringLiteral("%1 (container /dataset)").arg(opened_source_path));

  ksj::viewer::KspacePresentation kspace;
  EXPECT_FALSE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), kspace, error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_TRUE(kspace.image.isNull());

  ksj::viewer::ImagePresentation image;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session, make_image_request(QString::fromLatin1(kImageSeries.data(), kImageSeries.size())), image, error))
    << error.toStdString();
  EXPECT_EQ(image.source_dimensions, (std::array<std::uint16_t, 4U>{kSourceImageWidth, kSourceImageHeight, 1U, 1U}));
  EXPECT_FALSE(image.image.isNull());
  EXPECT_LT(image.image.width(), static_cast<int>(kSourceImageWidth));
  EXPECT_LE(image.image.width(), 2'048);
  EXPECT_LE(image.image.height(), 2'048);
  EXPECT_LE(image.image.sizeInBytes(), static_cast<qsizetype>(2U * 1024U * 1024U));
  EXPECT_LE(image.csv_rows.size(), 4'096);
  EXPECT_EQ(image.details.value(QStringLiteral("view")).toString(), QStringLiteral("image"));
  EXPECT_EQ(image.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(image.component, ksj::viewer::ArrShowDisplayComponent::real);
  EXPECT_EQ(image.details.value(QStringLiteral("component_fallback")).toString(),
            QStringLiteral("real-source-uses-arrshow-real"));
  EXPECT_EQ(image.details.value(QStringLiteral("display_engine")).toString(), QStringLiteral("arrshow-port"));
  EXPECT_EQ(image.window_persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_EQ(image.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("relative"));
  EXPECT_EQ(image.csv_columns,
            (QStringList{QStringLiteral("axis_x_coordinate"), QStringLiteral("axis_y_coordinate"),
                         QStringLiteral("real"), QStringLiteral("imaginary"), QStringLiteral("magnitude"),
                         QStringLiteral("phase_degrees"), QStringLiteral("phase_radians")}));

  ksj::viewer::ImagePresentation absolute_window_image;
  const ksj::viewer::ImageDisplaySettings absolute_window{
    .value_window = {.persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 0.5, .width = 1.0},
  };
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session,
    make_image_request(QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U, 0U, 0U, absolute_window),
    absolute_window_image, error))
    << error.toStdString();
  EXPECT_EQ(absolute_window_image.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_EQ(absolute_window_image.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("absolute"));
  EXPECT_DOUBLE_EQ(absolute_window_image.applied_window_center, 0.5);
  EXPECT_DOUBLE_EQ(absolute_window_image.applied_window_width, 1.0);
  EXPECT_NE(absolute_window_image.image.pixelColor(0, 0), image.image.pixelColor(0, 0));
  EXPECT_EQ(absolute_window_image.csv_columns, image.csv_columns);
  EXPECT_EQ(absolute_window_image.csv_rows, image.csv_rows);

  const ksj::viewer::ImageDisplaySettings invalid_window{
    .value_window = {.persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 0.0, .width = 0.0},
  };
  EXPECT_FALSE(ksj::viewer::make_image_presentation(
    session,
    make_image_request(QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U, 0U, 0U, invalid_window),
    absolute_window_image, error));
  EXPECT_EQ(error, QStringLiteral("arrShow absolute C/W width must be finite and greater than zero"));
}

TEST(KSpaceJetViewerPresentation, ProjectsComplexImageComponentsOnlyAsBoundedDisplayDerivatives) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-complex-image.mrd"));
  write_complex_image_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();
  const auto series_id = QString::fromLatin1(kComplexImageSeries.data(), kComplexImageSeries.size());

  EXPECT_EQ(ksj::viewer::arrshow_display_component_label(ksj::viewer::ArrShowDisplayComponent::complex),
            QStringLiteral("Complex (M)"));
  EXPECT_TRUE(ksj::viewer::image_arrshow_component_supported(ksj::viewer::ArrShowDisplayComponent::phase,
                                                             ksj::ismrmrd::ImageDataType::complex_32));
  EXPECT_FALSE(ksj::viewer::image_arrshow_component_supported(ksj::viewer::ArrShowDisplayComponent::phase,
                                                              ksj::ismrmrd::ImageDataType::real_32));

  ksj::viewer::ImagePresentation complex;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(session, make_image_request(series_id), complex, error))
    << error.toStdString();
  EXPECT_EQ(complex.component, ksj::viewer::ArrShowDisplayComponent::complex);
  EXPECT_EQ(complex.image.format(), QImage::Format_RGB32);
  EXPECT_EQ(complex.csv_columns,
            (QStringList{QStringLiteral("axis_x_coordinate"), QStringLiteral("axis_y_coordinate"),
                         QStringLiteral("real"), QStringLiteral("imaginary"), QStringLiteral("magnitude"),
                         QStringLiteral("phase_degrees"), QStringLiteral("phase_radians"), QStringLiteral("red"),
                         QStringLiteral("green"), QStringLiteral("blue")}));
  EXPECT_EQ(complex.details.value(QStringLiteral("display_component")).toString(), QStringLiteral("complex"));
  EXPECT_EQ(complex.details.value(QStringLiteral("display_engine")).toString(), QStringLiteral("arrshow-port"));
  EXPECT_EQ(complex.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(complex.details.value(QStringLiteral("phase_colormap")).toString(),
            QStringLiteral("arrshow-martin-phase-256"));
  EXPECT_EQ(complex.details.value(QStringLiteral("csv_colour_columns")).toString(),
            QStringLiteral("C/W-dependent RGB visualization derivative; raw image CSV columns remain source values"));
  EXPECT_EQ(complex.details.value(QStringLiteral("colour_mapping")).toString(),
            QStringLiteral("arrshow-martin-phase-times-magnitude-window"));
  EXPECT_EQ(complex.details.value(QStringLiteral("brightness_window_component")).toString(),
            QStringLiteral("magnitude"));
  EXPECT_NE(complex.image.pixelColor(0, 0), complex.image.pixelColor(1, 0));
  EXPECT_NE(complex.image.pixelColor(1, 0), complex.image.pixelColor(0, 1));

  const ksj::viewer::ImageDisplaySettings absolute_complex_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::complex,
    .value_window = {.persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 1.0, .width = 4.0},
  };
  ksj::viewer::ImagePresentation absolute_complex;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session, make_image_request(series_id, 0U, 0U, 0U, absolute_complex_settings), absolute_complex, error))
    << error.toStdString();
  EXPECT_EQ(absolute_complex.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_EQ(absolute_complex.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("absolute"));
  EXPECT_EQ(absolute_complex.details.value(QStringLiteral("csv_colour_columns")).toString(),
            QStringLiteral("C/W-dependent RGB visualization derivative; raw image CSV columns remain source values"));
  EXPECT_NE(absolute_complex.image, complex.image);
  EXPECT_EQ(absolute_complex.csv_columns, complex.csv_columns);
  const auto image_raw_column_count = complex.csv_columns.indexOf(QStringLiteral("phase_radians")) + 1;
  ASSERT_GT(image_raw_column_count, 0);
  ASSERT_EQ(absolute_complex.csv_rows.size(), complex.csv_rows.size());
  EXPECT_EQ(complex.csv_columns.mid(image_raw_column_count),
            (QStringList{QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue")}));
  for (qsizetype row_index = 0; row_index < complex.csv_rows.size(); ++row_index) {
    const auto& automatic_row = complex.csv_rows.at(row_index);
    const auto& absolute_row = absolute_complex.csv_rows.at(row_index);
    ASSERT_GE(automatic_row.size(), image_raw_column_count);
    ASSERT_GE(absolute_row.size(), image_raw_column_count);
    for (int column_index = 0; column_index < image_raw_column_count; ++column_index) {
      EXPECT_EQ(absolute_row.at(column_index), automatic_row.at(column_index));
    }
  }

  const ksj::viewer::ImageDisplaySettings magnitude_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::magnitude,
  };
  ksj::viewer::ImagePresentation magnitude;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session, make_image_request(series_id, 0U, 0U, 0U, magnitude_settings), magnitude, error))
    << error.toStdString();
  EXPECT_EQ(magnitude.component, ksj::viewer::ArrShowDisplayComponent::magnitude);
  EXPECT_EQ(magnitude.image.format(), QImage::Format_Grayscale8);
  EXPECT_EQ(magnitude.csv_columns.size(), 7);
  EXPECT_EQ(magnitude.details.value(QStringLiteral("display_component")).toString(), QStringLiteral("magnitude"));

  const ksj::viewer::ImageDisplaySettings real_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::real,
  };
  ksj::viewer::ImagePresentation real;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(session, make_image_request(series_id, 0U, 0U, 0U, real_settings),
                                                   real, error))
    << error.toStdString();
  EXPECT_EQ(real.component, ksj::viewer::ArrShowDisplayComponent::real);
  EXPECT_EQ(real.csv_columns, magnitude.csv_columns);
  EXPECT_LT(real.source_minimum, 0.0);
  EXPECT_GT(real.source_maximum, 0.0);

  const ksj::viewer::ImageDisplaySettings imaginary_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::imaginary,
  };
  ksj::viewer::ImagePresentation imaginary;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session, make_image_request(series_id, 0U, 0U, 0U, imaginary_settings), imaginary, error))
    << error.toStdString();
  EXPECT_EQ(imaginary.component, ksj::viewer::ArrShowDisplayComponent::imaginary);
  EXPECT_EQ(imaginary.csv_columns, magnitude.csv_columns);
  EXPECT_LT(imaginary.source_minimum, 0.0);
  EXPECT_GT(imaginary.source_maximum, 0.0);

  const ksj::viewer::ImageDisplaySettings phase_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::phase,
    .phase_representation = ksj::viewer::ArrShowPhaseRepresentation::radians,
    .phase_window = {.persistence = ksj::viewer::ArrShowWindowPersistence::absolute,
                     .center = 0.0,
                     .width = 2.0 * std::numbers::pi_v<double>},
  };
  ksj::viewer::ImagePresentation phase;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(session, make_image_request(series_id, 0U, 0U, 0U, phase_settings),
                                                   phase, error))
    << error.toStdString();
  EXPECT_EQ(phase.component, ksj::viewer::ArrShowDisplayComponent::phase);
  EXPECT_EQ(phase.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_EQ(phase.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("absolute"));
  EXPECT_EQ(phase.details.value(QStringLiteral("phase_unit")).toString(), QStringLiteral("radians"));
  EXPECT_EQ(phase.details.value(QStringLiteral("csv_colour_columns")).toString(),
            QStringLiteral("C/W-dependent RGB visualization derivative; raw image CSV columns remain source values"));
  EXPECT_NEAR(phase.source_minimum, -std::numbers::pi_v<double> * 0.5, 1.0e-12);
  EXPECT_NEAR(phase.source_maximum, std::numbers::pi_v<double>, 1.0e-12);
  EXPECT_EQ(phase.csv_columns.size(), 10);
  EXPECT_EQ(phase.csv_columns.at(6), QStringLiteral("phase_radians"));

  const auto real_dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-real-image.mrd"));
  write_synthetic_dataset(native_path(real_dataset_path));
  ksj::viewer::InspectionSession real_session;
  ASSERT_TRUE(real_session.open_mrd(real_dataset_path, error)) << error.toStdString();
  ksj::viewer::ImagePresentation real_fallback;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    real_session,
    make_image_request(QString::fromLatin1(kImageSeries.data(), kImageSeries.size()), 0U, 0U, 0U, phase_settings),
    real_fallback, error))
    << error.toStdString();
  EXPECT_EQ(real_fallback.component, ksj::viewer::ArrShowDisplayComponent::real);
  EXPECT_EQ(real_fallback.details.value(QStringLiteral("requested_display_component")).toString(),
            QStringLiteral("phase"));
  EXPECT_EQ(real_fallback.details.value(QStringLiteral("component_fallback")).toString(),
            QStringLiteral("real-source-uses-arrshow-real"));
  EXPECT_TRUE(real_fallback.details.value(QStringLiteral("phase_colormap")).isUndefined());
}

TEST(KSpaceJetViewerPresentation, RejectsCartesianGridWhenXmlDeclaresANonCartesianEncoding) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-radial-declaration.mrd"));

  const auto filename = native_path(dataset_path).string();
  ISMRMRD::Dataset dataset(filename.c_str(), "dataset", true);
  auto non_cartesian_xml = std::string(kCartesianKspaceXml);
  constexpr std::string_view kCartesianTrajectory{"<trajectory>cartesian</trajectory>"};
  const auto trajectory_offset = non_cartesian_xml.find(kCartesianTrajectory);
  ASSERT_NE(trajectory_offset, std::string::npos);
  non_cartesian_xml.replace(trajectory_offset, kCartesianTrajectory.size(), "<trajectory>radial</trajectory>");
  dataset.writeHeader(non_cartesian_xml);

  const std::array<std::complex<float>, 4U> coil_zero{std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F},
                                                      std::complex<float>{1.0F, 0.0F}, std::complex<float>{1.0F, 0.0F}};
  const std::array<std::complex<float>, 4U> coil_one{std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F},
                                                     std::complex<float>{2.0F, 0.0F}, std::complex<float>{2.0F, 0.0F}};
  // The payload has no trajectory. This deliberately reaches the XML
  // trajectory declaration branch rather than the raw-header rejection.
  append_cartesian_kspace_line(dataset, 0U, coil_zero, coil_one);

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  ksj::viewer::KspacePresentation presentation;
  EXPECT_FALSE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), presentation, error));
  EXPECT_TRUE(error.contains(QStringLiteral("no renderable Cartesian acquisition")));
  EXPECT_TRUE(presentation.image.isNull());
}

TEST(KSpaceJetViewerPresentation, ListsIndependentAcquisitionTypeMembershipsAndDefaultsToImagingData) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-auxiliary-then-imaging-cartesian.mrd"));
  write_auxiliary_then_imaging_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  using Kind = ksj::viewer::CartesianKspaceAcquisitionKind;
  QList<ksj::viewer::CartesianKspaceAcquisitionKindOption> options;
  ASSERT_TRUE(ksj::viewer::cartesian_kspace_acquisition_kind_options(session, options, error)) << error.toStdString();
  ASSERT_EQ(options.size(), 5);
  ASSERT_EQ(options.first().kind, Kind::imaging);
  EXPECT_EQ(options.first().label, QStringLiteral("Imaging data"));
  EXPECT_EQ(options.first().matching_acquisition_count, 3U);
  const auto option_for = [&options](const Kind kind) -> const ksj::viewer::CartesianKspaceAcquisitionKindOption* {
    const auto iterator = std::find_if(options.cbegin(), options.cend(), [kind](const auto& option) {
      return option.kind == kind;
    });
    return iterator == options.cend() ? nullptr : &*iterator;
  };
  const auto* noise_option = option_for(Kind::noise_measurement);
  const auto* calibration_option = option_for(Kind::parallel_calibration);
  const auto* navigation_option = option_for(Kind::navigation);
  const auto* surface_option = option_for(Kind::surface_coil_correction);
  ASSERT_NE(noise_option, nullptr);
  ASSERT_NE(calibration_option, nullptr);
  ASSERT_NE(navigation_option, nullptr);
  ASSERT_NE(surface_option, nullptr);
  EXPECT_EQ(noise_option->matching_acquisition_count, 1U);
  EXPECT_EQ(calibration_option->matching_acquisition_count, 1U);
  // The navigation|surface-coil source record is intentionally included in
  // both independent flag-membership views.
  EXPECT_EQ(navigation_option->matching_acquisition_count, 2U);
  EXPECT_EQ(surface_option->matching_acquisition_count, 2U);

  ksj::viewer::CartesianKspaceCatalog imaging_catalog;
  ASSERT_TRUE(ksj::viewer::cartesian_kspace_catalog(session, Kind::imaging, imaging_catalog, error))
    << error.toStdString();
  EXPECT_EQ(imaging_catalog.matching_acquisition_count, 3U);
  const auto coil_dimension =
    std::find_if(imaging_catalog.dimensions.cbegin(), imaging_catalog.dimensions.cend(), [](const auto& dimension) {
      return dimension.dimension == CartesianKspaceDimension::coil;
    });
  ASSERT_NE(coil_dimension, imaging_catalog.dimensions.cend());
  EXPECT_EQ(coil_dimension->observed_values, (QList<int>{0, 1}));

  ksj::viewer::KspacePresentation imaging;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), imaging, error))
    << error.toStdString();
  EXPECT_EQ(imaging.image.size(), QSize(4, 3));
  EXPECT_EQ(imaging.details.value(QStringLiteral("acquisition_kind")).toString(), QStringLiteral("imaging"));
  EXPECT_TRUE(imaging.details.value(QStringLiteral("acquisition_kind_is_imaging")).toBool());
  EXPECT_EQ(imaging.details.value(QStringLiteral("phase_encode_range_source")).toString(),
            QStringLiteral("xml_imaging_encoding_limit"));
  EXPECT_EQ(imaging.details.value(QStringLiteral("matching_acquisition_count")).toInt(), 3);
  EXPECT_EQ(imaging.details.value(QStringLiteral("source_complex_values")).toInt(), 12);
  EXPECT_EQ(
    imaging.details.value(QStringLiteral("source_grid")).toObject().value(QStringLiteral("axis_y_values")).toArray(),
    (QJsonArray{0, 1, 2}));
  EXPECT_TRUE(imaging.summary.contains(QStringLiteral("Selected ISMRMRD acquisition type: Imaging data")));

  ksj::viewer::KspacePresentation noise;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(Kind::noise_measurement), noise, error))
    << error.toStdString();
  EXPECT_EQ(noise.image.size(), QSize(4, 1));
  EXPECT_EQ(noise.details.value(QStringLiteral("acquisition_kind")).toString(), QStringLiteral("noise_measurement"));
  EXPECT_FALSE(noise.details.value(QStringLiteral("acquisition_kind_is_imaging")).toBool());
  EXPECT_EQ(noise.details.value(QStringLiteral("phase_encode_range_source")).toString(),
            QStringLiteral("observed_selected_acquisition_kind"));
  EXPECT_EQ(noise.details.value(QStringLiteral("matching_acquisition_count")).toInt(), 1);
  EXPECT_EQ(
    noise.details.value(QStringLiteral("source_grid")).toObject().value(QStringLiteral("axis_y_values")).toArray(),
    (QJsonArray{3}));
  EXPECT_TRUE(noise.summary.contains(QStringLiteral("explicitly selected auxiliary raw data")));

  ksj::viewer::KspacePresentation navigation;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(Kind::navigation),
                                                              navigation, error))
    << error.toStdString();
  EXPECT_EQ(navigation.image.size(), QSize(4, 3));
  EXPECT_EQ(navigation.details.value(QStringLiteral("matching_acquisition_count")).toInt(), 2);
  EXPECT_EQ(
    navigation.details.value(QStringLiteral("source_grid")).toObject().value(QStringLiteral("axis_y_values")).toArray(),
    (QJsonArray{0, 1, 2}));

  ksj::viewer::KspacePresentation surface_coil;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(Kind::surface_coil_correction), surface_coil, error))
    << error.toStdString();
  EXPECT_EQ(surface_coil.image.size(), QSize(4, 2));
  EXPECT_EQ(surface_coil.details.value(QStringLiteral("matching_acquisition_count")).toInt(), 2);
  EXPECT_EQ(surface_coil.details.value(QStringLiteral("source_grid"))
              .toObject()
              .value(QStringLiteral("axis_y_values"))
              .toArray(),
            (QJsonArray{1, 2}));
}

TEST(KSpaceJetViewerPresentation, ListsOnlyRenderableTypesForAnAuxiliaryOnlyContainer) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-auxiliary-only-cartesian.mrd"));
  write_auxiliary_only_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  using Kind = ksj::viewer::CartesianKspaceAcquisitionKind;
  QList<ksj::viewer::CartesianKspaceAcquisitionKindOption> options;
  ASSERT_TRUE(ksj::viewer::cartesian_kspace_acquisition_kind_options(session, options, error)) << error.toStdString();
  ASSERT_EQ(options.size(), 2);
  EXPECT_EQ(options.first().kind, Kind::noise_measurement);
  EXPECT_EQ(options.first().matching_acquisition_count, 1U);
  EXPECT_EQ(options.at(1).kind, Kind::surface_coil_correction);
  EXPECT_EQ(options.at(1).matching_acquisition_count, 1U);
  EXPECT_EQ(std::find_if(options.cbegin(), options.cend(),
                         [](const auto& option) {
                           return option.kind == Kind::imaging;
                         }),
            options.cend());

  ksj::viewer::CartesianKspaceCatalog imaging_catalog;
  EXPECT_FALSE(ksj::viewer::cartesian_kspace_catalog(session, Kind::imaging, imaging_catalog, error));
  EXPECT_TRUE(error.contains(QStringLiteral("no Imaging data acquisitions")));

  ksj::viewer::KspacePresentation presentation;
  EXPECT_FALSE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), presentation, error));
  EXPECT_TRUE(error.contains(QStringLiteral("no Imaging data acquisitions")));
  EXPECT_TRUE(presentation.image.isNull());

  ksj::viewer::CartesianKspaceCatalog noise_catalog;
  ASSERT_TRUE(ksj::viewer::cartesian_kspace_catalog(session, Kind::noise_measurement, noise_catalog, error))
    << error.toStdString();
  ASSERT_EQ(noise_catalog.entries.size(), 1);
  EXPECT_EQ(noise_catalog.entries.first().active_channel_count, 2U);
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(Kind::noise_measurement), presentation, error))
    << error.toStdString();
  EXPECT_EQ(presentation.image.size(), QSize(4, 1));
  EXPECT_EQ(presentation.details.value(QStringLiteral("acquisition_kind")).toString(),
            QStringLiteral("noise_measurement"));
  EXPECT_EQ(presentation.details.value(QStringLiteral("source_grid"))
              .toObject()
              .value(QStringLiteral("axis_y_values"))
              .toArray(),
            (QJsonArray{3}));
}

TEST(KSpaceJetViewerPresentation, RetainsTheXmlPhaseEncodeLimitForImageBearingAcquisitions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-out-of-bounds-imaging-cartesian.mrd"));
  write_out_of_bounds_imaging_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  ksj::viewer::KspacePresentation presentation;
  EXPECT_FALSE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), presentation, error));
  EXPECT_TRUE(error.contains(QStringLiteral("kspace_encode_step_1 is outside the XML encoding limit")));
  EXPECT_TRUE(presentation.image.isNull());
}

TEST(KSpaceJetViewerPresentation, RetainsTheXmlPartitionLimitForImageBearingAcquisitions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-out-of-bounds-partition-imaging-cartesian.mrd"));
  write_out_of_bounds_partition_imaging_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  ksj::viewer::KspacePresentation presentation;
  EXPECT_FALSE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), presentation, error));
  EXPECT_TRUE(error.contains(QStringLiteral("kspace_encode_step_2 is outside the XML encoding limit")));
  EXPECT_TRUE(presentation.image.isNull());
}

TEST(KSpaceJetViewerPresentation, ReportsActualSourceCoordinateRangesForDownsampledAxes) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-downsampled-readout-cartesian.mrd"));
  write_downsampled_readout_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  ksj::viewer::KspacePresentation presentation;
  ASSERT_TRUE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), presentation, error))
    << error.toStdString();
  EXPECT_EQ(presentation.image.size(), QSize(2'048, 3));
  EXPECT_EQ(presentation.details.value(QStringLiteral("source_grid")).toObject().value(QStringLiteral("width")).toInt(),
            2'049);
  EXPECT_EQ(
    presentation.details.value(QStringLiteral("display_grid")).toObject().value(QStringLiteral("width")).toInt(),
    2'048);
  ASSERT_FALSE(presentation.csv_rows.isEmpty());
  EXPECT_EQ(presentation.csv_rows.at(0).at(2), QStringLiteral("-1024"));
  EXPECT_EQ(presentation.csv_rows.at(0).at(3), QStringLiteral("-1023"));
  EXPECT_EQ(presentation.csv_rows.at(1).at(2), QStringLiteral("-1022"));
  EXPECT_EQ(presentation.csv_rows.at(1).at(3), QStringLiteral("-1022"));
}

TEST(KSpaceJetViewerPresentation, SkipsAnInvalidImagingPlaneWhenAnotherImagingPlaneIsRenderable) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path =
    QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-invalid-then-valid-imaging-cartesian.mrd"));
  write_invalid_then_valid_imaging_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  QList<ksj::viewer::CartesianKspaceAcquisitionKindOption> options;
  ASSERT_TRUE(ksj::viewer::cartesian_kspace_acquisition_kind_options(session, options, error)) << error.toStdString();
  ASSERT_EQ(options.size(), 1);
  EXPECT_EQ(options.first().kind, ksj::viewer::CartesianKspaceAcquisitionKind::imaging);
  EXPECT_EQ(options.first().matching_acquisition_count, 3U);

  ksj::viewer::KspacePresentation presentation;
  ASSERT_TRUE(
    ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), presentation, error))
    << error.toStdString();
  EXPECT_EQ(presentation.image.size(), QSize(4, 3));
  EXPECT_EQ(presentation.details.value(QStringLiteral("matching_acquisition_count")).toInt(), 2);
  EXPECT_TRUE(presentation.summary.contains(QStringLiteral("segment=1")));
}

TEST(KSpaceJetViewerPresentation, DiscoversSparseCoordinatesAndProjectsReadoutByCoilAtFixedPhaseEncode) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-plane-coordinates.mrd"));
  write_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  using Kind = ksj::viewer::CartesianKspaceAcquisitionKind;
  ksj::viewer::CartesianKspaceCatalog catalog;
  ASSERT_TRUE(ksj::viewer::cartesian_kspace_catalog(session, Kind::imaging, catalog, error)) << error.toStdString();

  // The XML declares one encoding. The invalid encoding-space-ref=1 source is
  // rejected, while all actual renderable values remain available as sparse
  // coordinates rather than a synthetic Cartesian product.
  EXPECT_EQ(catalog.matching_acquisition_count, 13U);
  ASSERT_EQ(catalog.entries.size(), 12);
  const auto observed_values = [&catalog](const CartesianKspaceDimension dimension) -> const QList<int>* {
    const auto iterator =
      std::find_if(catalog.dimensions.cbegin(), catalog.dimensions.cend(), [dimension](const auto& item) {
        return item.dimension == dimension;
      });
    return iterator == catalog.dimensions.cend() ? nullptr : &iterator->observed_values;
  };
  ASSERT_NE(observed_values(CartesianKspaceDimension::readout), nullptr);
  ASSERT_NE(observed_values(CartesianKspaceDimension::phase_encode), nullptr);
  ASSERT_NE(observed_values(CartesianKspaceDimension::coil), nullptr);
  EXPECT_EQ(*observed_values(CartesianKspaceDimension::readout), (QList<int>{-2, -1, 0, 1}));
  EXPECT_EQ(*observed_values(CartesianKspaceDimension::phase_encode), (QList<int>{0, 1, 2}));
  EXPECT_EQ(*observed_values(CartesianKspaceDimension::coil), (QList<int>{0, 1}));
  EXPECT_EQ(*observed_values(CartesianKspaceDimension::encoding_space), (QList<int>{0}));
  EXPECT_EQ(*observed_values(CartesianKspaceDimension::slice), (QList<int>{0, 1}));
  EXPECT_EQ(*observed_values(CartesianKspaceDimension::user_7), (QList<int>{0, 1}));

  CartesianKspaceCoordinate sparse_request;
  ksj::viewer::set_cartesian_kspace_coordinate_value(sparse_request, CartesianKspaceDimension::coil, 1);
  ksj::viewer::set_cartesian_kspace_coordinate_value(sparse_request, CartesianKspaceDimension::average, 1);
  ksj::viewer::set_cartesian_kspace_coordinate_value(sparse_request, CartesianKspaceDimension::slice, 1);
  CartesianKspaceCoordinate sparse_resolved;
  ASSERT_TRUE(ksj::viewer::resolve_cartesian_kspace_coordinate(catalog, CartesianKspaceAxes{}, sparse_request,
                                                               CartesianKspaceDimension::slice, sparse_resolved, error))
    << error.toStdString();
  EXPECT_EQ(ksj::viewer::cartesian_kspace_coordinate_value(sparse_resolved, CartesianKspaceDimension::slice), 1);
  EXPECT_EQ(ksj::viewer::cartesian_kspace_coordinate_value(sparse_resolved, CartesianKspaceDimension::average), 0);
  EXPECT_EQ(ksj::viewer::cartesian_kspace_coordinate_value(sparse_resolved, CartesianKspaceDimension::coil), 1);

  ksj::viewer::KspacePresentation sparse_plane;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(Kind::imaging, CartesianKspaceAxes{}, sparse_resolved), sparse_plane, error))
    << error.toStdString();
  EXPECT_EQ(sparse_plane.image.size(), QSize(4, 1));
  EXPECT_EQ(
    sparse_plane.details.value(QStringLiteral("fixed_coordinates")).toObject().value(QStringLiteral("slice")).toInt(),
    1);
  EXPECT_EQ(sparse_plane.details.value(QStringLiteral("coil_channel")).toInt(), 1);

  CartesianKspaceCoordinate readout_by_coil_coordinate;
  ksj::viewer::set_cartesian_kspace_coordinate_value(readout_by_coil_coordinate, CartesianKspaceDimension::phase_encode,
                                                     0);
  const CartesianKspaceAxes readout_by_coil_axes{.x = CartesianKspaceDimension::readout,
                                                 .y = CartesianKspaceDimension::coil};
  ksj::viewer::KspacePresentation readout_by_coil;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(Kind::imaging, readout_by_coil_axes, readout_by_coil_coordinate),
    readout_by_coil, error))
    << error.toStdString();
  EXPECT_EQ(readout_by_coil.image.size(), QSize(4, 2));
  EXPECT_EQ(readout_by_coil.details.value(QStringLiteral("axis_x")).toString(), QStringLiteral("readout"));
  EXPECT_EQ(readout_by_coil.details.value(QStringLiteral("axis_y")).toString(), QStringLiteral("coil"));
  EXPECT_EQ(readout_by_coil.details.value(QStringLiteral("coil_mode")).toString(), QStringLiteral("axis"));
  EXPECT_FALSE(readout_by_coil.details.contains(QStringLiteral("coil_channel")));
  const auto fixed_coordinates = readout_by_coil.details.value(QStringLiteral("fixed_coordinates")).toObject();
  EXPECT_EQ(fixed_coordinates.value(QStringLiteral("phase-encode")).toInt(), 0);
  EXPECT_FALSE(fixed_coordinates.contains(QStringLiteral("coil")));
  ASSERT_EQ(readout_by_coil.csv_rows.size(), 8);
  EXPECT_NEAR(readout_by_coil.csv_rows.at(0).at(6).toDouble(), 4.5, 1.0e-9);
  EXPECT_NEAR(readout_by_coil.csv_rows.at(4).at(6).toDouble(), 6.0, 1.0e-9);
}

TEST(KSpaceJetViewerPresentation, RendersRawSingleCoilCartesianKspaceWithArrShowComplexDefault) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-cartesian-kspace.mrd"));
  write_cartesian_kspace_dataset(native_path(dataset_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();

  ksj::viewer::KspacePresentation invalid_axes;
  const CartesianKspaceAxes same_axes{.x = CartesianKspaceDimension::readout, .y = CartesianKspaceDimension::readout};
  EXPECT_FALSE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(CartesianKspaceKind::imaging, same_axes, CartesianKspaceCoordinate{}),
    invalid_axes, error));
  EXPECT_TRUE(error.contains(QStringLiteral("two distinct Cartesian K-space display dimensions")));

  ksj::viewer::KspacePresentation unknown_axes;
  const CartesianKspaceAxes sentinel_axes{.x = CartesianKspaceDimension::readout, .y = CartesianKspaceDimension::count};
  EXPECT_FALSE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(CartesianKspaceKind::imaging, sentinel_axes, CartesianKspaceCoordinate{}),
    unknown_axes, error));
  EXPECT_TRUE(error.contains(QStringLiteral("known Cartesian K-space display dimensions")));

  ksj::viewer::KspacePresentation complex;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(session, make_cartesian_kspace_request(), complex, error))
    << error.toStdString();
  EXPECT_EQ(complex.image.size(), QSize(4, 3));
  EXPECT_EQ(complex.component, ksj::viewer::ArrShowDisplayComponent::complex);
  EXPECT_EQ(complex.details.value(QStringLiteral("view")).toString(), QStringLiteral("cartesian-k-space"));
  EXPECT_EQ(complex.details.value(QStringLiteral("display_component")).toString(), QStringLiteral("complex"));
  EXPECT_EQ(complex.details.value(QStringLiteral("display_engine")).toString(), QStringLiteral("arrshow-port"));
  EXPECT_EQ(complex.details.value(QStringLiteral("range_calculation")).toString(), QStringLiteral("min-max"));
  EXPECT_EQ(complex.details.value(QStringLiteral("phase_representation")).toString(), QStringLiteral("degrees"));
  EXPECT_EQ(complex.window_persistence, ksj::viewer::ArrShowWindowPersistence::relative);
  EXPECT_EQ(complex.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("relative"));
  EXPECT_EQ(complex.details.value(QStringLiteral("coil_mode")).toString(), QStringLiteral("single-coil"));
  EXPECT_EQ(complex.details.value(QStringLiteral("coil_channel")).toInt(), 0);
  EXPECT_FALSE(complex.details.contains(QStringLiteral("reference_acquisition_ordinal")));
  EXPECT_FALSE(complex.details.contains(QStringLiteral("frame")));
  EXPECT_EQ(complex.details.value(QStringLiteral("axis_x")).toString(), QStringLiteral("readout"));
  EXPECT_EQ(complex.details.value(QStringLiteral("axis_y")).toString(), QStringLiteral("phase-encode"));
  EXPECT_EQ(
    complex.details.value(QStringLiteral("fixed_coordinates")).toObject().value(QStringLiteral("partition")).toInt(),
    0);
  EXPECT_EQ(complex.details.value(QStringLiteral("matching_acquisition_count")).toInt(), 3);
  EXPECT_EQ(complex.details.value(QStringLiteral("source_complex_values")).toInt(), 12);
  EXPECT_EQ(complex.details.value(QStringLiteral("occupied_display_cells")).toInt(), 8);
  EXPECT_EQ(complex.details.value(QStringLiteral("empty_display_cells")).toInt(), 4);
  EXPECT_EQ(complex.details.value(QStringLiteral("multi_contribution_display_cells")).toInt(), 4);
  EXPECT_EQ(
    complex.details.value(QStringLiteral("source_grid")).toObject().value(QStringLiteral("axis_x_values")).toArray(),
    (QJsonArray{-2, -1, 0, 1}));
  EXPECT_EQ(complex.details.value(QStringLiteral("source_grid")).toObject().value(QStringLiteral("height")).toInt(), 3);
  EXPECT_TRUE(complex.summary.contains(QStringLiteral("No FFT")));
  EXPECT_TRUE(complex.summary.contains(QStringLiteral("multiple contributions")));
  EXPECT_TRUE(complex.details.value(QStringLiteral("representation")).toString().contains(QStringLiteral("no FFT")));
  EXPECT_EQ(complex.details.value(QStringLiteral("phase_colormap")).toString(),
            QStringLiteral("arrshow-martin-phase-256"));
  EXPECT_EQ(complex.details.value(QStringLiteral("csv_colour_columns")).toString(),
            QStringLiteral("C/W-dependent RGB visualization derivative; raw complex CSV columns remain source values"));

  ksj::viewer::KspacePresentation coil_one;
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(CartesianKspaceKind::imaging, 1), coil_one, error))
    << error.toStdString();
  EXPECT_EQ(coil_one.details.value(QStringLiteral("coil_channel")).toInt(), 1);
  ASSERT_FALSE(coil_one.csv_rows.isEmpty());
  EXPECT_NEAR(coil_one.csv_rows.at(0).at(6).toDouble(), 6.0, 1.0e-9);

  ASSERT_EQ(complex.csv_columns.size(), 14);
  ASSERT_EQ(complex.csv_rows.size(), 12);
  EXPECT_EQ(complex.csv_rows.at(0).at(0), QStringLiteral("0"));
  EXPECT_EQ(complex.csv_rows.at(0).at(1), QStringLiteral("0"));
  EXPECT_EQ(complex.csv_rows.at(0).at(2), QStringLiteral("-2"));
  EXPECT_EQ(complex.csv_rows.at(0).at(3), QStringLiteral("-2"));
  EXPECT_EQ(complex.csv_rows.at(0).at(4), QStringLiteral("0"));
  EXPECT_EQ(complex.csv_rows.at(0).at(5), QStringLiteral("0"));
  EXPECT_NEAR(complex.csv_rows.at(0).at(6).toDouble(), 4.5, 1.0e-9);
  EXPECT_DOUBLE_EQ(complex.csv_rows.at(0).at(7).toDouble(), 0.0);
  EXPECT_NEAR(complex.csv_rows.at(0).at(8).toDouble(), 4.5, 1.0e-9);
  EXPECT_DOUBLE_EQ(complex.csv_rows.at(0).at(9).toDouble(), 0.0);
  EXPECT_EQ(complex.csv_rows.at(0).at(10), QStringLiteral("2"));
  EXPECT_EQ(complex.csv_rows.at(4).at(10), QStringLiteral("0"));
  EXPECT_DOUBLE_EQ(complex.csv_rows.at(4).at(8).toDouble(), 0.0);
  EXPECT_NE(complex.image.pixelColor(0, 0), QColor(Qt::gray));

  ksj::viewer::KspacePresentation absolute_complex;
  const ksj::viewer::ArrShowDisplaySettings absolute_complex_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::complex,
    .value_window = {.persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 2.0, .width = 2.0},
  };
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(CartesianKspaceKind::imaging, 0, absolute_complex_settings),
    absolute_complex, error))
    << error.toStdString();
  EXPECT_EQ(absolute_complex.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_EQ(absolute_complex.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("absolute"));
  EXPECT_EQ(absolute_complex.details.value(QStringLiteral("csv_colour_columns")).toString(),
            QStringLiteral("C/W-dependent RGB visualization derivative; raw complex CSV columns remain source values"));
  EXPECT_NE(absolute_complex.image, complex.image);
  EXPECT_EQ(absolute_complex.csv_columns, complex.csv_columns);
  ASSERT_EQ(absolute_complex.csv_rows.size(), complex.csv_rows.size());
  const auto raw_column_count = complex.csv_columns.indexOf(QStringLiteral("contribution_count")) + 1;
  ASSERT_GT(raw_column_count, 0);
  EXPECT_EQ(complex.csv_columns.mid(raw_column_count),
            (QStringList{QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue")}));
  for (qsizetype row_index = 0; row_index < complex.csv_rows.size(); ++row_index) {
    const auto& automatic_row = complex.csv_rows.at(row_index);
    const auto& absolute_row = absolute_complex.csv_rows.at(row_index);
    ASSERT_GE(automatic_row.size(), raw_column_count);
    ASSERT_GE(absolute_row.size(), raw_column_count);
    for (int column_index = 0; column_index < raw_column_count; ++column_index) {
      EXPECT_EQ(absolute_row.at(column_index), automatic_row.at(column_index));
    }
  }

  ksj::viewer::KspacePresentation real;
  const ksj::viewer::ArrShowDisplaySettings real_settings{.component = ksj::viewer::ArrShowDisplayComponent::real};
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(CartesianKspaceKind::imaging, 0, real_settings), real, error))
    << error.toStdString();
  EXPECT_EQ(real.image.format(), QImage::Format_Grayscale8);
  EXPECT_EQ(real.details.value(QStringLiteral("display_component")).toString(), QStringLiteral("real"));

  ksj::viewer::KspacePresentation phase;
  const ksj::viewer::ArrShowDisplaySettings phase_settings{
    .component = ksj::viewer::ArrShowDisplayComponent::phase,
    .phase_window = {.persistence = ksj::viewer::ArrShowWindowPersistence::absolute, .center = 0.0, .width = 360.0},
  };
  ASSERT_TRUE(ksj::viewer::make_cartesian_kspace_presentation(
    session, make_cartesian_kspace_request(CartesianKspaceKind::imaging, 0, phase_settings), phase, error))
    << error.toStdString();
  EXPECT_EQ(phase.image.format(), QImage::Format_RGB32);
  EXPECT_EQ(phase.window_persistence, ksj::viewer::ArrShowWindowPersistence::absolute);
  EXPECT_EQ(phase.details.value(QStringLiteral("window_mode")).toString(), QStringLiteral("absolute"));
  EXPECT_EQ(phase.details.value(QStringLiteral("phase_unit")).toString(), QStringLiteral("degrees"));
  EXPECT_EQ(phase.details.value(QStringLiteral("csv_colour_columns")).toString(),
            QStringLiteral("C/W-dependent RGB visualization derivative; raw complex CSV columns remain source values"));
  EXPECT_DOUBLE_EQ(phase.applied_window_center, 0.0);
  EXPECT_DOUBLE_EQ(phase.applied_window_width, 360.0);
}

TEST(KSpaceJetViewerPresentation, DiscoversAndSwitchesOnlyReadableStandardContainers) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto dataset_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-multigroup.mrd"));
  write_synthetic_dataset(native_path(dataset_path), "dataset_1", false);
  write_synthetic_dataset(native_path(dataset_path), "dataset_2", true);
  create_empty_root_group(native_path(dataset_path), "dataset");

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(dataset_path, error)) << error.toStdString();
  const auto& containers = session.available_containers();
  const auto find_container = [&containers](const std::string_view path) {
    return std::find_if(containers.begin(), containers.end(), [path](const auto& descriptor) {
      return descriptor.path == std::string(path);
    });
  };
  const auto raw_container_1 = find_container("/dataset_1");
  ASSERT_NE(raw_container_1, containers.end());
  EXPECT_TRUE(raw_container_1->has_header);
  EXPECT_TRUE(raw_container_1->has_acquisitions);
  EXPECT_FALSE(raw_container_1->has_images);

  const auto image_container_2 = find_container("/dataset_2/viewer_series");
  ASSERT_NE(image_container_2, containers.end());
  EXPECT_FALSE(image_container_2->has_header);
  EXPECT_TRUE(image_container_2->has_images);

  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_1"));
  EXPECT_TRUE(session.metadata().image_series.empty());

  ASSERT_TRUE(session.select_container(QStringLiteral("/dataset_2"), error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_2"));
  ASSERT_EQ(session.metadata().image_series.size(), 1U);

  ASSERT_TRUE(session.select_container(QStringLiteral("/dataset_2/viewer_series"), error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_2/viewer_series"));
  EXPECT_EQ(session.metadata().acquisition_count, 0U);
  ASSERT_EQ(session.metadata().image_series.size(), 1U);
  EXPECT_EQ(session.metadata().image_series.front().series_id, std::string(kImageSeries));

  ksj::viewer::ImagePresentation standalone_image;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session, make_image_request(QString::fromLatin1(kImageSeries.data(), kImageSeries.size())), standalone_image,
    error))
    << error.toStdString();
  EXPECT_FALSE(standalone_image.image.isNull());

  EXPECT_FALSE(session.select_container(QStringLiteral("/dataset"), error));
  EXPECT_EQ(error, QStringLiteral("the selected container is not a readable standard ISMRMRD container"));
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset_2/viewer_series"));
  EXPECT_EQ(session.metadata().image_series.size(), 1U);
}

TEST(KSpaceJetViewerPresentation, OpensPureStandardImageArtifactWithoutAcquisitions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto artifact_path = QDir(temporary_directory.path()).filePath(QStringLiteral("viewer-image-artifact.mrd"));
  write_synthetic_image_artifact(native_path(artifact_path));

  ksj::viewer::InspectionSession session;
  QString error;
  ASSERT_TRUE(session.open_mrd(artifact_path, error)) << error.toStdString();
  EXPECT_EQ(session.container_path(), QStringLiteral("/dataset"));
  EXPECT_EQ(session.metadata().acquisition_count, 0U);
  ASSERT_EQ(session.metadata().image_series.size(), 1U);

  const auto& containers = session.available_containers();
  const auto container = std::find_if(containers.begin(), containers.end(), [](const auto& descriptor) {
    return descriptor.path == "/dataset";
  });
  ASSERT_NE(container, containers.end());
  EXPECT_TRUE(container->has_header);
  EXPECT_FALSE(container->has_acquisitions);
  EXPECT_TRUE(container->has_images);

  ksj::viewer::ImagePresentation image;
  ASSERT_TRUE(ksj::viewer::make_image_presentation(
    session, make_image_request(QString::fromLatin1(kImageSeries.data(), kImageSeries.size())), image, error))
    << error.toStdString();
  EXPECT_FALSE(image.image.isNull());
  EXPECT_EQ(image.source_dimensions, (std::array<std::uint16_t, 4U>{kSourceImageWidth, kSourceImageHeight, 1U, 1U}));
}

TEST(KSpaceJetViewerPresentation, ParsesOnlyBoundedPipelineDefinitions) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();
  const auto pipeline_path = QDir(temporary_directory.path()).filePath(QStringLiteral("pipeline.json"));
  write_file(pipeline_path, valid_pipeline_definition().toUtf8());

  ksj::viewer::PipelinePresentation presentation;
  QString error;
  ASSERT_TRUE(ksj::viewer::load_pipeline_presentation(pipeline_path, presentation, error)) << error.toStdString();
  EXPECT_TRUE(presentation.summary.contains(QStringLiteral("Parsed only")));
  EXPECT_TRUE(presentation.summary.contains(QStringLiteral("no Provider resolution")));
  EXPECT_TRUE(presentation.canonical_json.contains(QStringLiteral("org.example.viewer-test")));
  EXPECT_EQ(presentation.details.value(QStringLiteral("view")).toString(), QStringLiteral("pipeline"));
  EXPECT_EQ(presentation.details.value(QStringLiteral("input_profile")).toString(), QStringLiteral("ismrmrd-hdf5"));
  EXPECT_EQ(presentation.details.value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(presentation.details.value(QStringLiteral("graph_kind")).toString(), QStringLiteral("authored-dag"));
  ASSERT_EQ(presentation.graph_nodes.size(), 4);
  EXPECT_EQ(presentation.graph_nodes.at(0).kind, ksj::viewer::PipelineGraphNodeKind::ingress);
  EXPECT_EQ(presentation.graph_nodes.at(0).key, QStringLiteral("ingress:kspace"));
  EXPECT_EQ(presentation.graph_nodes.at(1).kind, ksj::viewer::PipelineGraphNodeKind::operator_node);
  EXPECT_EQ(presentation.graph_nodes.at(1).key, QStringLiteral("node:prepare"));
  EXPECT_TRUE(presentation.graph_nodes.at(1).detail.contains(QStringLiteral("Provider alias: example")));
  EXPECT_EQ(presentation.graph_nodes.at(3).kind, ksj::viewer::PipelineGraphNodeKind::egress);
  EXPECT_EQ(presentation.graph_nodes.at(3).key, QStringLiteral("egress:images"));
  ASSERT_EQ(presentation.graph_edges.size(), 5);
  EXPECT_EQ(presentation.graph_edges.at(0).kind, ksj::viewer::PipelineGraphEdgeKind::ingress);
  EXPECT_EQ(presentation.graph_edges.at(1).kind, ksj::viewer::PipelineGraphEdgeKind::data);
  EXPECT_EQ(presentation.graph_edges.at(1).id, QStringLiteral("prepared_kspace"));
  EXPECT_EQ(presentation.graph_edges.at(1).source_key, QStringLiteral("node:prepare"));
  EXPECT_EQ(presentation.graph_edges.at(1).source_port, QStringLiteral("prepared"));
  EXPECT_EQ(presentation.graph_edges.at(1).target_key, QStringLiteral("node:reconstruct"));
  EXPECT_EQ(presentation.graph_edges.at(1).target_port, QStringLiteral("kspace"));
  EXPECT_EQ(presentation.graph_edges.at(2).kind, ksj::viewer::PipelineGraphEdgeKind::data);
  EXPECT_EQ(presentation.graph_edges.at(2).id, QStringLiteral("prepared_weights"));
  EXPECT_EQ(presentation.graph_edges.at(3).kind, ksj::viewer::PipelineGraphEdgeKind::calibration);
  EXPECT_EQ(presentation.graph_edges.at(3).source_key, QStringLiteral("node:prepare"));
  EXPECT_EQ(presentation.graph_edges.at(3).target_key, QStringLiteral("node:reconstruct"));
  EXPECT_EQ(presentation.graph_edges.at(4).kind, ksj::viewer::PipelineGraphEdgeKind::egress);
  const auto graph_nodes = presentation.details.value(QStringLiteral("graph_nodes")).toArray();
  const auto graph_edges = presentation.details.value(QStringLiteral("graph_edges")).toArray();
  EXPECT_EQ(graph_nodes.size(), presentation.graph_nodes.size());
  EXPECT_EQ(graph_edges.size(), presentation.graph_edges.size());

  const auto over_limit_path = QDir(temporary_directory.path()).filePath(QStringLiteral("over-limit.json"));
  const auto over_limit_bytes =
    static_cast<qsizetype>(ksj::recon::graph::kPipelineDefinitionJsonParseLimits.max_document_bytes + 1U);
  write_file(over_limit_path, QByteArray(over_limit_bytes, ' '));
  EXPECT_FALSE(ksj::viewer::load_pipeline_presentation(over_limit_path, presentation, error));
  EXPECT_TRUE(error.contains(QStringLiteral("exceeds the")));
  EXPECT_TRUE(error.contains(QStringLiteral("parser limit")));
}

TEST(KSpaceJetViewerPresentation, ExportsOnlyLabelledVisualizationDerivatives) {
  QTemporaryDir temporary_directory;
  ASSERT_TRUE(temporary_directory.isValid()) << temporary_directory.errorString().toStdString();

  ksj::viewer::VisualizationDerivative derivative;
  derivative.view_name = QStringLiteral("synthetic-image");
  derivative.source_description = QStringLiteral("synthetic ISMRMRD source");
  derivative.image = QImage(3, 2, QImage::Format_Grayscale8);
  ASSERT_FALSE(derivative.image.isNull());
  derivative.image.fill(127U);
  derivative.csv_columns = {QStringLiteral("x"), QStringLiteral("magnitude")};
  derivative.csv_rows = {{QStringLiteral("0"), QStringLiteral("1.25")}};
  derivative.details.insert(QStringLiteral("view"), QStringLiteral("synthetic-image"));

  const QDir directory(temporary_directory.path());
  QString error;

  const auto png_path = directory.filePath(QStringLiteral("derivative.png"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, png_path,
                                                           ksj::viewer::VisualizationExportFormat::png, error))
    << error.toStdString();
  QImageReader png_reader(png_path);
  ASSERT_TRUE(png_reader.canRead()) << png_reader.errorString().toStdString();
  EXPECT_EQ(png_reader.text(QStringLiteral("KSpaceJet.ArtifactKind")), QStringLiteral("visualization-derivative"));
  EXPECT_EQ(png_reader.text(QStringLiteral("KSpaceJet.ViewName")), derivative.view_name);
  EXPECT_EQ(png_reader.text(QStringLiteral("KSpaceJet.SourceDescription")), derivative.source_description);
  const auto decoded_png = png_reader.read();
  ASSERT_FALSE(decoded_png.isNull()) << png_reader.errorString().toStdString();
  EXPECT_EQ(decoded_png.size(), derivative.image.size());

  const auto svg_path = directory.filePath(QStringLiteral("derivative.svg"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, svg_path,
                                                           ksj::viewer::VisualizationExportFormat::svg, error))
    << error.toStdString();
  const auto svg = read_file(svg_path);
  EXPECT_TRUE(svg.contains("KSpaceJet.ArtifactKind"));
  EXPECT_TRUE(svg.contains("visualization-derivative"));
  EXPECT_TRUE(svg.contains("synthetic ISMRMRD source"));

  const auto csv_path = directory.filePath(QStringLiteral("derivative.csv"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, csv_path,
                                                           ksj::viewer::VisualizationExportFormat::csv, error))
    << error.toStdString();
  const auto csv = read_file(csv_path);
  EXPECT_TRUE(csv.contains("# KSpaceJet.ArtifactKind=visualization-derivative"));
  EXPECT_TRUE(csv.contains("# KSpaceJet.ViewName=\"synthetic-image\""));
  EXPECT_TRUE(csv.contains("# KSpaceJet.SourceDescription=\"synthetic ISMRMRD source\""));

  const auto json_path = directory.filePath(QStringLiteral("derivative.json"));
  ASSERT_TRUE(ksj::viewer::export_visualization_derivative(derivative, json_path,
                                                           ksj::viewer::VisualizationExportFormat::json, error))
    << error.toStdString();
  QJsonParseError parse_error;
  const auto json = QJsonDocument::fromJson(read_file(json_path), &parse_error);
  ASSERT_EQ(parse_error.error, QJsonParseError::NoError) << parse_error.errorString().toStdString();
  ASSERT_TRUE(json.isObject());
  EXPECT_EQ(json.object().value(QStringLiteral("artifact_kind")).toString(),
            QStringLiteral("visualization-derivative"));
  EXPECT_EQ(json.object().value(QStringLiteral("view_name")).toString(), derivative.view_name);
  EXPECT_EQ(json.object().value(QStringLiteral("source_description")).toString(), derivative.source_description);

  const auto rejected_mrd_path = directory.filePath(QStringLiteral("not-an-artifact.mrd"));
  EXPECT_FALSE(ksj::viewer::export_visualization_derivative(derivative, rejected_mrd_path,
                                                            ksj::viewer::VisualizationExportFormat::png, error));
  EXPECT_TRUE(error.contains(QStringLiteral("MRI artifact extensions")));
  EXPECT_FALSE(QFile::exists(rejected_mrd_path));

  const auto mismatched_path = directory.filePath(QStringLiteral("wrong-extension.json"));
  EXPECT_FALSE(ksj::viewer::export_visualization_derivative(derivative, mismatched_path,
                                                            ksj::viewer::VisualizationExportFormat::png, error));
  EXPECT_TRUE(error.contains(QStringLiteral("must use the .png extension")));
  EXPECT_FALSE(QFile::exists(mismatched_path));

  auto no_image = derivative;
  no_image.image = {};
  const auto no_image_path = directory.filePath(QStringLiteral("missing-image.png"));
  EXPECT_FALSE(ksj::viewer::export_visualization_derivative(no_image, no_image_path,
                                                            ksj::viewer::VisualizationExportFormat::png, error));
  EXPECT_TRUE(error.contains(QStringLiteral("require a non-null image")));
  EXPECT_FALSE(QFile::exists(no_image_path));
}
