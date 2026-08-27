#pragma once

#include <QList>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>

namespace ksj::viewer {

// arrShow has two independently assigned ':' selection tags. The tags choose
// the two display dimensions, while their left-to-right column order defines
// the horizontal/X and vertical/Y axes.
enum class ArrShowDimensionSelectionTag : std::uint8_t {
  none,
  first,
  second,
};

// A read-only viewer adaptation of arrShow's per-dimension value changers.
// It deliberately keeps the useful navigation shape (abbreviation, +, value,
// -, extent) but does not expose arrShow's mutable-array or MATLAB workspace
// operations.
struct ArrShowDimensionSpec final {
  QString identifier;
  QString label;
  QString abbreviation;
  QString tool_tip;
  QList<int> observed_values;
  int current_value{0};
  // The bottom extent cell shows, for a sparse ISMRMRD coordinate, the number
  // of observed values, never an XML-invented maximum. A ':' axis uses its
  // current rendered grid extent.
  int displayed_extent{0};
  ArrShowDimensionSelectionTag selection_tag{ArrShowDimensionSelectionTag::none};
  // Image axes may be fixed by their source format.  Such a dimension still
  // uses the shared strip, but its extent cell cannot change ':' membership.
  bool selection_change_enabled{true};
};

// The two arrShow ':' selection tags. This state expresses membership in the
// display plane only; arrshow_plane_axes_in_column_order() assigns X/Y.
struct ArrShowDimensionSelection final {
  QString first_identifier;
  QString second_identifier;
};

[[nodiscard]] ArrShowDimensionSelectionTag arrshow_dimension_selection_tag(const ArrShowDimensionSelection& selection,
                                                                           const QString& identifier);
void arrshow_update_dimension_selection(ArrShowDimensionSelection& selection, const QString& identifier,
                                        ArrShowDimensionSelectionTag selection_tag);

// The ordered two-dimensional display plane derived from visible arrShow
// columns. Selection tags determine membership only: the first selected
// column is X and the second selected column is Y.
struct ArrShowDimensionPlaneAxes final {
  QString x_identifier;
  QString y_identifier;
};

[[nodiscard]] std::optional<ArrShowDimensionPlaneAxes>
arrshow_plane_axes_in_column_order(const QList<ArrShowDimensionSpec>& dimensions);

class ArrShowDimensionControl;

// A horizontally ordered strip of arrShow-style dimension controls. It keeps
// exactly two caller-selected ':' tags and only non-axis dimensions
// with multiple real, selectable values; the strip never assumes values form a
// dense Cartesian array or invents an unavailable coordinate.
class ArrShowDimensionStrip final : public QWidget {
public:
  using ValueChangedCallback = std::function<void(const QString& identifier, int value)>;
  using SelectionTagChangedCallback =
    std::function<void(const QString& identifier, ArrShowDimensionSelectionTag selection_tag)>;

  explicit ArrShowDimensionStrip(QWidget* parent = nullptr, QString object_name_prefix = {});

  void set_dimensions(QList<ArrShowDimensionSpec> dimensions, QString preferred_active_identifier = {});
  void set_value_changed_callback(ValueChangedCallback callback);
  void set_selection_tag_changed_callback(SelectionTagChangedCallback callback);

  [[nodiscard]] bool has_dimensions() const noexcept;
  [[nodiscard]] QString active_dimension_identifier() const;
  [[nodiscard]] bool step_active_dimension(int step);
  [[nodiscard]] bool focus_relative_dimension(int step);

private:
  void clear_active_dimension();
  void set_active_dimension(const QString& identifier);
  void handle_value_change(const QString& identifier, int value);
  void handle_selection_tag_change(const QString& identifier, ArrShowDimensionSelectionTag selection_tag);

  QList<ArrShowDimensionControl*> controls_;
  QWidget* controls_host_ = nullptr;
  QWidget* empty_state_ = nullptr;
  QString active_dimension_identifier_;
  QString object_name_prefix_;
  ValueChangedCallback value_changed_callback_;
  SelectionTagChangedCallback selection_tag_changed_callback_;
};

} // namespace ksj::viewer
