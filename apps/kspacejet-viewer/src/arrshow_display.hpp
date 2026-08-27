#pragma once

#include <QImage>
#include <QString>

#include <cstddef>
#include <span>

namespace ksj::viewer {

// The display vocabulary and defaults intentionally follow arrShow's
// asCmplxChooserClass: Complex is the default for complex source data and
// scalar source data exposes only Magnitude and Real.
enum class ArrShowDisplayComponent {
  magnitude,
  real,
  imaginary,
  complex,
  phase,
};

enum class ArrShowPhaseRepresentation {
  degrees,
  radians,
};

// arrShow's asWindowingClass offers min/max by default and an optional
// symmetric percentile filter. The range strategy is shared by normal and
// phase views while their C/W state remains independent.
enum class ArrShowRangeCalculation {
  minimum_maximum,
  percentile,
};

// asWindowingClass has three mutually distinct cross-plane C/W policies:
// reset on the next plane, preserve the prior relative C/W, or preserve the
// prior absolute C/W.  They must not be collapsed into a single "auto"
// toggle: the first is arrShow's ordinary no-keep state.
enum class ArrShowWindowPersistence {
  reset,
  relative,
  absolute,
};

struct ArrShowWindowSettings {
  // arrShow enables relative C/W after its first image is linked.  The
  // reset and absolute modes remain independently selectable afterwards.
  ArrShowWindowPersistence persistence{ArrShowWindowPersistence::relative};
  double center{0.0};
  double width{0.0};
  double relative_center{0.5};
  double relative_width{1.0};
  // A Reset-mode C/W edit remains visible for the current plane.  The caller
  // clears this transient value before selecting a different plane.
  bool has_current_window{false};
};

// This app-local type only carries bounded presentation settings. It never
// owns MRI source pixels; callers provide samples only while their reader
// callback is valid.
struct ArrShowDisplaySettings {
  ArrShowDisplayComponent component{ArrShowDisplayComponent::complex};
  ArrShowPhaseRepresentation phase_representation{ArrShowPhaseRepresentation::degrees};
  ArrShowRangeCalculation range_calculation{ArrShowRangeCalculation::minimum_maximum};
  double percentile{98.0};
  ArrShowWindowSettings value_window{};
  ArrShowWindowSettings phase_window{.persistence = ArrShowWindowPersistence::relative,
                                     .center = 0.0,
                                     .width = 360.0,
                                     .relative_center = 0.5,
                                     .relative_width = 1.0,
                                     .has_current_window = false};
};

struct ArrShowDisplayResult {
  QImage image;
  double source_minimum{0.0};
  double source_maximum{0.0};
  double applied_window_center{0.0};
  double applied_window_width{0.0};
  ArrShowWindowPersistence window_persistence{ArrShowWindowPersistence::relative};
};

[[nodiscard]] QString arrshow_display_component_identifier(ArrShowDisplayComponent component);
[[nodiscard]] QString arrshow_display_component_label(ArrShowDisplayComponent component);
[[nodiscard]] QString arrshow_display_component_semantics(ArrShowDisplayComponent component);
[[nodiscard]] bool arrshow_display_component_requires_complex(ArrShowDisplayComponent component) noexcept;
[[nodiscard]] bool arrshow_display_component_is_phase(ArrShowDisplayComponent component) noexcept;
[[nodiscard]] QString arrshow_phase_representation_identifier(ArrShowPhaseRepresentation representation);
[[nodiscard]] QString arrshow_phase_representation_label(ArrShowPhaseRepresentation representation);
[[nodiscard]] QString arrshow_range_calculation_identifier(ArrShowRangeCalculation calculation);
[[nodiscard]] QString arrshow_range_calculation_label(ArrShowRangeCalculation calculation, double percentile);
[[nodiscard]] QString arrshow_window_persistence_identifier(ArrShowWindowPersistence persistence);
[[nodiscard]] QString arrshow_window_persistence_label(ArrShowWindowPersistence persistence);
[[nodiscard]] const ArrShowWindowSettings& arrshow_active_window(const ArrShowDisplaySettings& settings) noexcept;
[[nodiscard]] ArrShowWindowSettings& arrshow_active_window(ArrShowDisplaySettings& settings) noexcept;
void arrshow_set_active_window_value(ArrShowDisplaySettings& settings, double source_minimum, double source_maximum,
                                     double center, double width) noexcept;
void arrshow_prepare_active_window_for_new_plane(ArrShowDisplaySettings& settings) noexcept;
void arrshow_convert_phase_window(ArrShowDisplaySettings& settings, ArrShowPhaseRepresentation representation) noexcept;

// Render one already-bounded complex plane with arrShow's default display
// semantics. The two source spans must have width * height values in row-major
// order. The returned QImage is a bounded visualization derivative only.
[[nodiscard]] bool render_arrshow_display(std::span<const double> real_values, std::span<const double> imaginary_values,
                                          int width, int height, const ArrShowDisplaySettings& settings,
                                          ArrShowDisplayResult& result, QString& error);

} // namespace ksj::viewer
