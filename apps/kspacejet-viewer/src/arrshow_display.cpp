#include "arrshow_display.hpp"

// Display semantics in this file are an independent C++/Qt port of the
// behavior documented by arrShow's asCmplxChooserClass,
// asWindowingClass, complex2rgb and martin_phase functions.
//
// Copyright (C) 2009-2013 Biomedizinische NMR Forschungs GmbH
// Author: Tilman Johannes Sumpf <tsumpf@gwdg.de>
// Distributed under the Boost Software License, Version 1.0.
// See ../THIRD_PARTY_NOTICES.md for the complete notice.

#include <QColor>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace {

using ksj::viewer::ArrShowDisplayComponent;
using ksj::viewer::ArrShowDisplayResult;
using ksj::viewer::ArrShowDisplaySettings;
using ksj::viewer::ArrShowPhaseRepresentation;
using ksj::viewer::ArrShowRangeCalculation;
using ksj::viewer::ArrShowWindowPersistence;
using ksj::viewer::ArrShowWindowSettings;

constexpr int kArrShowColourCount = 256;
constexpr double kPhaseDegreesPerRadian = 180.0 / std::numbers::pi_v<double>;

[[nodiscard]] bool checked_pixel_count(const int width, const int height, std::size_t& count) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const auto unsigned_width = static_cast<std::size_t>(width);
  const auto unsigned_height = static_cast<std::size_t>(height);
  if (unsigned_width > std::numeric_limits<std::size_t>::max() / unsigned_height) {
    return false;
  }
  count = unsigned_width * unsigned_height;
  return true;
}

[[nodiscard]] bool is_known_component(const ArrShowDisplayComponent component) noexcept {
  switch (component) {
    case ArrShowDisplayComponent::magnitude:
    case ArrShowDisplayComponent::real:
    case ArrShowDisplayComponent::imaginary:
    case ArrShowDisplayComponent::complex:
    case ArrShowDisplayComponent::phase:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_known_phase_representation(const ArrShowPhaseRepresentation representation) noexcept {
  switch (representation) {
    case ArrShowPhaseRepresentation::degrees:
    case ArrShowPhaseRepresentation::radians:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_known_range_calculation(const ArrShowRangeCalculation calculation) noexcept {
  switch (calculation) {
    case ArrShowRangeCalculation::minimum_maximum:
    case ArrShowRangeCalculation::percentile:
      return true;
  }
  return false;
}

[[nodiscard]] bool is_known_window_persistence(const ArrShowWindowPersistence persistence) noexcept {
  switch (persistence) {
    case ArrShowWindowPersistence::reset:
    case ArrShowWindowPersistence::relative:
    case ArrShowWindowPersistence::absolute:
      return true;
  }
  return false;
}

struct MartinPhaseColour {
  double red{0.0};
  double green{0.0};
  double blue{0.0};
};

[[nodiscard]] MartinPhaseColour martin_phase_colour(const int index) {
  const auto clamped_index = std::clamp(index, 0, kArrShowColourCount - 1);
  const auto phase = static_cast<double>(clamped_index) * std::numbers::pi_v<double> * 2.0 /
                     static_cast<double>(kArrShowColourCount - 1);
  const auto channel = [phase](const double offset) {
    return std::clamp((std::sin(phase + offset) + 1.0) * 0.5, 0.0, 1.0);
  };
  return {.red = channel(0.0),
          .green = channel(std::numbers::pi_v<double> * 2.0 / 3.0),
          .blue = channel(std::numbers::pi_v<double> * 4.0 / 3.0)};
}

[[nodiscard]] QRgb quantize_martin_phase_colour(const MartinPhaseColour colour, const double brightness = 1.0) {
  const auto scaled_channel = [brightness](const double channel) {
    return static_cast<int>(std::lround(std::clamp(channel * brightness, 0.0, 1.0) * static_cast<double>(UCHAR_MAX)));
  };
  return qRgb(scaled_channel(colour.red), scaled_channel(colour.green), scaled_channel(colour.blue));
}

[[nodiscard]] int martin_phase_index_from_radians(const double phase_radians) {
  if (!std::isfinite(phase_radians)) {
    return 0;
  }
  const auto normalized = static_cast<double>(kArrShowColourCount - 1) * (phase_radians + std::numbers::pi_v<double>) /
                          (std::numbers::pi_v<double> * 2.0);
  return std::clamp(static_cast<int>(std::lround(normalized)), 0, kArrShowColourCount - 1);
}

[[nodiscard]] int martin_phase_index_from_window(const double value, const double low, const double high) {
  if (!std::isfinite(value) || !std::isfinite(low) || !std::isfinite(high) || high <= low) {
    return 0;
  }
  const auto normalized = std::clamp((value - low) / (high - low), 0.0, 1.0);
  return std::clamp(static_cast<int>(std::lround(normalized * static_cast<double>(kArrShowColourCount - 1))), 0,
                    kArrShowColourCount - 1);
}

[[nodiscard]] std::vector<double> finite_values(const std::span<const double> values) {
  std::vector<double> result;
  result.reserve(values.size());
  for (const auto value : values) {
    if (std::isfinite(value)) {
      result.push_back(value);
    }
  }
  return result;
}

[[nodiscard]] double arrshow_percentile(std::vector<double> sorted_values, const double percentile) {
  if (sorted_values.empty()) {
    return 0.0;
  }
  std::sort(sorted_values.begin(), sorted_values.end());
  if (sorted_values.size() == 1U) {
    return sorted_values.front();
  }

  const auto sample_count = static_cast<double>(sorted_values.size());
  const auto first_rank = 50.0 / sample_count;
  const auto last_rank = 100.0 - first_rank;
  if (percentile <= first_rank) {
    return sorted_values.front();
  }
  if (percentile >= last_rank) {
    return sorted_values.back();
  }

  const auto position = percentile * sample_count / 100.0 - 0.5;
  const auto lower_index = static_cast<std::size_t>(std::floor(position));
  const auto upper_index = std::min(lower_index + 1U, sorted_values.size() - 1U);
  const auto lower_rank = 100.0 / sample_count * (static_cast<double>(lower_index) + 0.5);
  const auto upper_rank = 100.0 / sample_count * (static_cast<double>(upper_index) + 0.5);
  const auto interpolation = (percentile - lower_rank) / (upper_rank - lower_rank);
  return sorted_values[lower_index] + (sorted_values[upper_index] - sorted_values[lower_index]) * interpolation;
}

struct DataRange {
  double minimum{0.0};
  double maximum{0.0};
};

[[nodiscard]] DataRange derive_data_range(const std::span<const double> values,
                                          const ArrShowRangeCalculation calculation, const double percentile) {
  const auto finite = finite_values(values);
  if (finite.empty()) {
    return {};
  }

  if (calculation == ArrShowRangeCalculation::minimum_maximum) {
    const auto [minimum, maximum] = std::minmax_element(finite.begin(), finite.end());
    return {.minimum = *minimum, .maximum = *maximum};
  }

  std::vector<double> negated;
  negated.reserve(finite.size());
  for (const auto value : finite) {
    negated.push_back(-value);
  }
  return {.minimum = -arrshow_percentile(std::move(negated), percentile),
          .maximum = arrshow_percentile(finite, percentile)};
}

[[nodiscard]] bool resolve_window(const std::span<const double> values, const ArrShowDisplaySettings& settings,
                                  const ArrShowWindowSettings& requested, DataRange& range, double& center,
                                  double& width, QString& error) {
  range = derive_data_range(values, settings.range_calculation, settings.percentile);
  const auto range_width = range.maximum - range.minimum;
  switch (requested.persistence) {
    case ArrShowWindowPersistence::relative:
      center = range.minimum + requested.relative_center * range_width;
      width = requested.relative_width * range_width;
      return true;
    case ArrShowWindowPersistence::reset:
      if (!requested.has_current_window) {
        center = range.minimum + range_width * 0.5;
        width = range_width;
        return true;
      }
      break;
    case ArrShowWindowPersistence::absolute:
      break;
  }

  center = requested.center;
  width = requested.width;
  if (!std::isfinite(center) || !std::isfinite(width) || width <= 0.0) {
    error = QStringLiteral("arrShow absolute C/W width must be finite and greater than zero");
    return false;
  }
  const auto low = center - width * 0.5;
  const auto high = center + width * 0.5;
  if (!std::isfinite(low) || !std::isfinite(high) || high <= low) {
    error = QStringLiteral("arrShow absolute C/W center and width are outside the supported display range");
    return false;
  }
  return true;
}

[[nodiscard]] bool constant_nonzero(const DataRange range) {
  if (!std::isfinite(range.minimum) || !std::isfinite(range.maximum) || range.maximum == 0.0) {
    return false;
  }
  // This mirrors complex2rgb's round(mi * 1e12) == round(ma * 1e12)
  // condition while avoiding an integer narrowing overflow.
  return std::round(range.minimum * 1.0e12) == std::round(range.maximum * 1.0e12);
}

[[nodiscard]] QImage render_scalar(const std::span<const double> values, const int width, const int height,
                                   const DataRange range, const double center, const double window_width,
                                   QString& error) {
  QImage image(width, height, QImage::Format_Grayscale8);
  if (image.isNull()) {
    error = QStringLiteral("Qt could not allocate the bounded arrShow scalar display image");
    return {};
  }

  const auto low = center - window_width * 0.5;
  const auto high = center + window_width * 0.5;
  const auto has_window = std::isfinite(low) && std::isfinite(high) && high > low;
  const auto show_constant = !has_window && constant_nonzero(range);
  for (int y = 0; y < height; ++y) {
    auto* line = image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      auto normalized = 0.0;
      if (std::isfinite(values[index]) && has_window) {
        normalized = (values[index] - low) / (high - low);
      } else if (std::isfinite(values[index]) && show_constant) {
        normalized = 1.0;
      }
      line[x] = static_cast<uchar>(std::lround(std::clamp(normalized, 0.0, 1.0) * static_cast<double>(UCHAR_MAX)));
    }
  }
  return image;
}

[[nodiscard]] QImage render_complex(const std::span<const double> magnitudes, const std::span<const double> phases,
                                    const int width, const int height, const DataRange source_magnitude_range,
                                    const double center, const double window_width, QString& error) {
  QImage image(width, height, QImage::Format_RGB32);
  if (image.isNull()) {
    error = QStringLiteral("Qt could not allocate the bounded arrShow complex display image");
    return {};
  }

  const auto low = center - window_width * 0.5;
  const auto high = center + window_width * 0.5;
  // complex2rgb decides its pure-phase special case from the unwindowed
  // magnitude image, not from a percentile/manual C/W range.
  const auto pure_phase = constant_nonzero(source_magnitude_range);
  for (int y = 0; y < height; ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      auto brightness = 0.0;
      if (pure_phase && std::isfinite(magnitudes[index])) {
        brightness = 1.0;
      } else if (std::isfinite(magnitudes[index]) && std::isfinite(low) && std::isfinite(high)) {
        // complex2rgb: clamp only below Cmin, divide by Cmax (not Cmax-Cmin),
        // then clamp the resulting brightness.
        const auto clamped_magnitude = std::max(magnitudes[index], low);
        const auto numerator = clamped_magnitude - low;
        // MATLAB evaluates the expression even when Cmax is zero.  Preserve
        // its useful saturated case while retaining a deterministic black
        // result for the otherwise indeterminate 0 / 0 case.
        brightness = high == 0.0 ? (numerator > 0.0 ? 1.0 : 0.0) : numerator / high;
      }
      const auto phase_colour = martin_phase_colour(martin_phase_index_from_radians(phases[index]));
      const auto clipped_brightness = std::clamp(brightness, 0.0, 1.0);
      line[x] = quantize_martin_phase_colour(phase_colour, clipped_brightness);
    }
  }
  return image;
}

[[nodiscard]] QImage render_phase(const std::span<const double> values, const int width, const int height,
                                  const double center, const double window_width, QString& error) {
  QImage image(width, height, QImage::Format_RGB32);
  if (image.isNull()) {
    error = QStringLiteral("Qt could not allocate the bounded arrShow phase display image");
    return {};
  }

  const auto low = center - window_width * 0.5;
  const auto high = center + window_width * 0.5;
  for (int y = 0; y < height; ++y) {
    auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      line[x] =
        quantize_martin_phase_colour(martin_phase_colour(martin_phase_index_from_window(values[index], low, high)));
    }
  }
  return image;
}

} // namespace

namespace ksj::viewer {

QString arrshow_display_component_identifier(const ArrShowDisplayComponent component) {
  switch (component) {
    case ArrShowDisplayComponent::magnitude:
      return QStringLiteral("magnitude");
    case ArrShowDisplayComponent::real:
      return QStringLiteral("real");
    case ArrShowDisplayComponent::imaginary:
      return QStringLiteral("imaginary");
    case ArrShowDisplayComponent::complex:
      return QStringLiteral("complex");
    case ArrShowDisplayComponent::phase:
      return QStringLiteral("phase");
  }
  return QStringLiteral("invalid");
}

QString arrshow_display_component_label(const ArrShowDisplayComponent component) {
  switch (component) {
    case ArrShowDisplayComponent::magnitude:
      return QStringLiteral("Magnitude (m)");
    case ArrShowDisplayComponent::real:
      return QStringLiteral("Real (r)");
    case ArrShowDisplayComponent::imaginary:
      return QStringLiteral("Imaginary (i)");
    case ArrShowDisplayComponent::complex:
      return QStringLiteral("Complex (M)");
    case ArrShowDisplayComponent::phase:
      return QStringLiteral("Phase (p)");
  }
  return QStringLiteral("Invalid component");
}

QString arrshow_display_component_semantics(const ArrShowDisplayComponent component) {
  switch (component) {
    case ArrShowDisplayComponent::magnitude:
      return QStringLiteral("arrShow Magnitude: absolute value with Gray(256) windowing");
    case ArrShowDisplayComponent::real:
      return QStringLiteral("arrShow Real: real component with Gray(256) windowing");
    case ArrShowDisplayComponent::imaginary:
      return QStringLiteral("arrShow Imaginary: imaginary component with Gray(256) windowing");
    case ArrShowDisplayComponent::complex:
      return QStringLiteral("arrShow Complex: martin_phase(256) indexed by phase, multiplied by magnitude C/W");
    case ArrShowDisplayComponent::phase:
      return QStringLiteral("arrShow Phase: martin_phase(256) over the independent phase C/W");
  }
  return QStringLiteral("invalid arrShow display component");
}

bool arrshow_display_component_requires_complex(const ArrShowDisplayComponent component) noexcept {
  switch (component) {
    case ArrShowDisplayComponent::magnitude:
    case ArrShowDisplayComponent::real:
      return false;
    case ArrShowDisplayComponent::imaginary:
    case ArrShowDisplayComponent::complex:
    case ArrShowDisplayComponent::phase:
      return true;
  }
  return true;
}

bool arrshow_display_component_is_phase(const ArrShowDisplayComponent component) noexcept {
  return component == ArrShowDisplayComponent::phase;
}

QString arrshow_phase_representation_identifier(const ArrShowPhaseRepresentation representation) {
  switch (representation) {
    case ArrShowPhaseRepresentation::degrees:
      return QStringLiteral("degrees");
    case ArrShowPhaseRepresentation::radians:
      return QStringLiteral("radians");
  }
  return QStringLiteral("invalid");
}

QString arrshow_phase_representation_label(const ArrShowPhaseRepresentation representation) {
  switch (representation) {
    case ArrShowPhaseRepresentation::degrees:
      return QStringLiteral("Degrees");
    case ArrShowPhaseRepresentation::radians:
      return QStringLiteral("Radians");
  }
  return QStringLiteral("Invalid phase unit");
}

QString arrshow_range_calculation_identifier(const ArrShowRangeCalculation calculation) {
  switch (calculation) {
    case ArrShowRangeCalculation::minimum_maximum:
      return QStringLiteral("min-max");
    case ArrShowRangeCalculation::percentile:
      return QStringLiteral("percentile");
  }
  return QStringLiteral("invalid");
}

QString arrshow_range_calculation_label(const ArrShowRangeCalculation calculation, const double percentile) {
  switch (calculation) {
    case ArrShowRangeCalculation::minimum_maximum:
      return QStringLiteral("Min / max");
    case ArrShowRangeCalculation::percentile:
      return QStringLiteral("%1% percentile").arg(percentile, 0, 'g', 4);
  }
  return QStringLiteral("Invalid range");
}

QString arrshow_window_persistence_identifier(const ArrShowWindowPersistence persistence) {
  switch (persistence) {
    case ArrShowWindowPersistence::reset:
      return QStringLiteral("per-plane");
    case ArrShowWindowPersistence::relative:
      return QStringLiteral("relative");
    case ArrShowWindowPersistence::absolute:
      return QStringLiteral("absolute");
  }
  return QStringLiteral("invalid");
}

QString arrshow_window_persistence_label(const ArrShowWindowPersistence persistence) {
  switch (persistence) {
    case ArrShowWindowPersistence::reset:
      return QStringLiteral("Reset per plane");
    case ArrShowWindowPersistence::relative:
      return QStringLiteral("Keep relative");
    case ArrShowWindowPersistence::absolute:
      return QStringLiteral("Keep absolute");
  }
  return QStringLiteral("Invalid C/W persistence");
}

const ArrShowWindowSettings& arrshow_active_window(const ArrShowDisplaySettings& settings) noexcept {
  return arrshow_display_component_is_phase(settings.component) ? settings.phase_window : settings.value_window;
}

ArrShowWindowSettings& arrshow_active_window(ArrShowDisplaySettings& settings) noexcept {
  return arrshow_display_component_is_phase(settings.component) ? settings.phase_window : settings.value_window;
}

void arrshow_set_active_window_value(ArrShowDisplaySettings& settings, const double source_minimum,
                                     const double source_maximum, const double center, const double width) noexcept {
  auto& window = arrshow_active_window(settings);
  window.center = center;
  window.width = width;
  window.has_current_window = true;
  const auto source_width = source_maximum - source_minimum;
  if (std::isfinite(source_minimum) && std::isfinite(source_maximum) && std::isfinite(center) && std::isfinite(width) &&
      source_width > std::numeric_limits<double>::min()) {
    window.relative_center = (center - source_minimum) / source_width;
    window.relative_width = width / source_width;
  }
}

void arrshow_prepare_active_window_for_new_plane(ArrShowDisplaySettings& settings) noexcept {
  auto& window = arrshow_active_window(settings);
  if (window.persistence == ArrShowWindowPersistence::reset) {
    window.has_current_window = false;
  }
}

void arrshow_convert_phase_window(ArrShowDisplaySettings& settings,
                                  const ArrShowPhaseRepresentation representation) noexcept {
  if (settings.phase_representation == representation) {
    return;
  }
  const auto scale =
    representation == ArrShowPhaseRepresentation::radians ? 1.0 / kPhaseDegreesPerRadian : kPhaseDegreesPerRadian;
  settings.phase_window.center *= scale;
  settings.phase_window.width *= scale;
  settings.phase_representation = representation;
}

bool render_arrshow_display(const std::span<const double> real_values, const std::span<const double> imaginary_values,
                            const int width, const int height, const ArrShowDisplaySettings& settings,
                            ArrShowDisplayResult& result, QString& error) {
  result = {};
  error.clear();

  std::size_t expected_count = 0U;
  if (!checked_pixel_count(width, height, expected_count) || real_values.size() != expected_count ||
      imaginary_values.size() != expected_count) {
    error = QStringLiteral("arrShow display requires matching bounded real and imaginary planes");
    return false;
  }
  if (!is_known_component(settings.component) || !is_known_phase_representation(settings.phase_representation) ||
      !is_known_range_calculation(settings.range_calculation) ||
      !is_known_window_persistence(arrshow_active_window(settings).persistence)) {
    error = QStringLiteral("arrShow display settings contain an unsupported selector");
    return false;
  }
  if (!std::isfinite(settings.percentile) || settings.percentile <= 0.0 || settings.percentile > 100.0) {
    error = QStringLiteral("arrShow percentile must be finite and in the interval (0, 100]");
    return false;
  }

  std::vector<double> magnitudes;
  std::vector<double> phase_radians;
  magnitudes.reserve(expected_count);
  phase_radians.reserve(expected_count);
  for (std::size_t index = 0U; index < expected_count; ++index) {
    const auto real = real_values[index];
    const auto imaginary = imaginary_values[index];
    magnitudes.push_back(std::hypot(real, imaginary));
    phase_radians.push_back(std::atan2(imaginary, real));
  }

  std::span<const double> display_values;
  std::vector<double> phase_values;
  switch (settings.component) {
    case ArrShowDisplayComponent::magnitude:
    case ArrShowDisplayComponent::complex:
      display_values = magnitudes;
      break;
    case ArrShowDisplayComponent::real:
      display_values = real_values;
      break;
    case ArrShowDisplayComponent::imaginary:
      display_values = imaginary_values;
      break;
    case ArrShowDisplayComponent::phase:
      phase_values.reserve(expected_count);
      for (const auto phase : phase_radians) {
        phase_values.push_back(settings.phase_representation == ArrShowPhaseRepresentation::degrees
                                 ? phase * kPhaseDegreesPerRadian
                                 : phase);
      }
      display_values = phase_values;
      break;
  }

  const auto& requested_window = arrshow_active_window(settings);
  DataRange data_range;
  auto center = 0.0;
  auto window_width = 0.0;
  if (!resolve_window(display_values, settings, requested_window, data_range, center, window_width, error)) {
    return false;
  }

  switch (settings.component) {
    case ArrShowDisplayComponent::magnitude:
    case ArrShowDisplayComponent::real:
    case ArrShowDisplayComponent::imaginary:
      result.image = render_scalar(display_values, width, height, data_range, center, window_width, error);
      break;
    case ArrShowDisplayComponent::complex:
      result.image =
        render_complex(magnitudes, phase_radians, width, height,
                       derive_data_range(magnitudes, ArrShowRangeCalculation::minimum_maximum, settings.percentile),
                       center, window_width, error);
      break;
    case ArrShowDisplayComponent::phase:
      result.image = render_phase(display_values, width, height, center, window_width, error);
      break;
  }
  if (result.image.isNull()) {
    if (error.isEmpty()) {
      error = QStringLiteral("arrShow display rendering did not produce a bounded QImage");
    }
    return false;
  }

  result.source_minimum = data_range.minimum;
  result.source_maximum = data_range.maximum;
  result.applied_window_center = center;
  result.applied_window_width = window_width;
  result.window_persistence = requested_window.persistence;
  return true;
}

} // namespace ksj::viewer
