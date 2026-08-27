#include "arrshow_dimension_controls.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

namespace {

constexpr int k_arrshow_dimension_column_width = 42;

[[nodiscard]] QString object_name_suffix(const QString& identifier) {
  QString result;
  bool uppercase_next = true;
  for (const auto character : identifier) {
    if (!character.isLetterOrNumber()) {
      uppercase_next = true;
      continue;
    }
    result.append(uppercase_next ? character.toUpper() : character);
    uppercase_next = false;
  }
  return result;
}

[[nodiscard]] QString observed_values_text(const QList<int>& values) {
  QStringList text;
  text.reserve(values.size());
  for (const auto value : values) {
    text.append(QString::number(value));
  }
  return text.join(QStringLiteral(", "));
}

void repolish(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

void configure_centered_dimension_text_cell(QToolButton* cell) {
  if (cell == nullptr) {
    return;
  }
  // Qt's text-only tool-button label is drawn with AlignCenter. Set this
  // explicitly on every row so an icon-oriented platform default cannot
  // alter the compact column geometry.
  cell->setToolButtonStyle(Qt::ToolButtonTextOnly);
  // A QVBoxLayout keeps a default tool button at its text-size width.  The
  // button must expand horizontally, otherwise its centred label is centred
  // only in a left-hand sliver instead of in the whole dimension cell.
  cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  cell->setProperty("arrShowDimensionTextCell", true);
}

[[nodiscard]] bool is_visible_dimension(const ksj::viewer::ArrShowDimensionSpec& dimension) {
  return !dimension.identifier.isEmpty() &&
         (dimension.selection_tag != ksj::viewer::ArrShowDimensionSelectionTag::none ||
          dimension.observed_values.size() >= 2);
}

} // namespace

namespace ksj::viewer {

ArrShowDimensionSelectionTag arrshow_dimension_selection_tag(const ArrShowDimensionSelection& selection,
                                                             const QString& identifier) {
  if (!identifier.isEmpty() && selection.first_identifier == identifier) {
    return ArrShowDimensionSelectionTag::first;
  }
  if (!identifier.isEmpty() && selection.second_identifier == identifier) {
    return ArrShowDimensionSelectionTag::second;
  }
  return ArrShowDimensionSelectionTag::none;
}

void arrshow_update_dimension_selection(ArrShowDimensionSelection& selection, const QString& identifier,
                                        const ArrShowDimensionSelectionTag selection_tag) {
  if (identifier.isEmpty()) {
    return;
  }
  switch (selection_tag) {
    case ArrShowDimensionSelectionTag::none:
      if (selection.first_identifier == identifier) {
        selection.first_identifier.clear();
      }
      if (selection.second_identifier == identifier) {
        selection.second_identifier.clear();
      }
      return;
    case ArrShowDimensionSelectionTag::first:
      if (selection.second_identifier == identifier && selection.first_identifier != identifier) {
        std::swap(selection.first_identifier, selection.second_identifier);
        return;
      }
      selection.first_identifier = identifier;
      if (selection.second_identifier == identifier) {
        selection.second_identifier.clear();
      }
      return;
    case ArrShowDimensionSelectionTag::second:
      if (selection.first_identifier == identifier && selection.second_identifier != identifier) {
        std::swap(selection.first_identifier, selection.second_identifier);
        return;
      }
      selection.second_identifier = identifier;
      if (selection.first_identifier == identifier) {
        selection.first_identifier.clear();
      }
      return;
  }
}

std::optional<ArrShowDimensionPlaneAxes>
arrshow_plane_axes_in_column_order(const QList<ArrShowDimensionSpec>& dimensions) {
  ArrShowDimensionPlaneAxes axes;
  int selected_count = 0;
  for (const auto& dimension : dimensions) {
    if (!is_visible_dimension(dimension) || dimension.selection_tag == ArrShowDimensionSelectionTag::none) {
      continue;
    }
    if (selected_count == 0) {
      axes.x_identifier = dimension.identifier;
    } else if (selected_count == 1) {
      axes.y_identifier = dimension.identifier;
    }
    ++selected_count;
  }
  if (selected_count != 2) {
    return std::nullopt;
  }
  return axes;
}

class ArrShowDimensionControl final : public QWidget {
public:
  using ActivatedCallback = std::function<void(const QString& identifier)>;
  using ValueChangedCallback = std::function<void(const QString& identifier, int value)>;
  using SelectionTagChangedCallback =
    std::function<void(const QString& identifier, ArrShowDimensionSelectionTag selection_tag)>;

  ArrShowDimensionControl(const ArrShowDimensionSpec& dimension, const QString& object_name_prefix,
                          ActivatedCallback activated_callback, ValueChangedCallback value_changed_callback,
                          SelectionTagChangedCallback selection_tag_changed_callback, QWidget* parent)
      : QWidget(parent), object_name_prefix_(object_name_prefix), activated_callback_(std::move(activated_callback)),
        value_changed_callback_(std::move(value_changed_callback)),
        selection_tag_changed_callback_(std::move(selection_tag_changed_callback)) {
    setProperty("arrShowDimension", true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setFixedWidth(k_arrshow_dimension_column_width);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);

    abbreviation_ = new QToolButton(this);
    configure_centered_dimension_text_cell(abbreviation_);
    abbreviation_->setObjectName(object_name_prefix_ + QStringLiteral("DimensionAbbreviation"));
    abbreviation_->setProperty("arrShowDimensionAbbreviation", true);
    abbreviation_->setToolTip(tr("Show the full name of this arrShow-style dimension."));
    layout->addWidget(abbreviation_);

    increment_button_ = new QToolButton(this);
    configure_centered_dimension_text_cell(increment_button_);
    increment_button_->setText(QStringLiteral("+"));
    increment_button_->setObjectName(object_name_prefix_ + QStringLiteral("DimensionIncrement"));
    increment_button_->setToolTip(tr("Increase this observed dimension value."));
    layout->addWidget(increment_button_);

    value_ = new QLineEdit(this);
    value_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    value_->setObjectName(object_name_prefix_ + QStringLiteral("DimensionValue"));
    value_->setProperty("arrShowDimensionValue", true);
    value_->installEventFilter(this);
    setFocusProxy(value_);
    layout->addWidget(value_);

    decrement_button_ = new QToolButton(this);
    configure_centered_dimension_text_cell(decrement_button_);
    decrement_button_->setText(QStringLiteral("−"));
    decrement_button_->setObjectName(object_name_prefix_ + QStringLiteral("DimensionDecrement"));
    decrement_button_->setToolTip(tr("Decrease this observed dimension value."));
    layout->addWidget(decrement_button_);

    dimension_label_ = new QToolButton(this);
    configure_centered_dimension_text_cell(dimension_label_);
    dimension_label_->setObjectName(object_name_prefix_ + QStringLiteral("DimensionLabel"));
    dimension_label_->setProperty("arrShowDimensionLabel", true);
    dimension_label_->setToolTip(
      tr("Left-click to choose or replace the blue ':' tag; right-click for the red ':' tag. The colours identify "
         "replacement targets, not X/Y; X/Y follow the selected columns' left-to-right order."));
    dimension_label_->installEventFilter(this);
    layout->addWidget(dimension_label_);

    connect(increment_button_, &QToolButton::clicked, this, [this] {
      activate();
      static_cast<void>(step(1));
    });
    connect(decrement_button_, &QToolButton::clicked, this, [this] {
      activate();
      static_cast<void>(step(-1));
    });
    connect(abbreviation_, &QToolButton::clicked, this, [this] {
      activate();
      value_->setFocus(Qt::MouseFocusReason);
    });
    connect(value_, &QLineEdit::editingFinished, this, [this] {
      activate();
      bool parsed = false;
      const auto requested = value_->text().toInt(&parsed);
      if (!parsed || !observed_values_.contains(requested)) {
        refresh_value();
        return;
      }
      request_value(requested);
    });
    set_dimension(dimension);
  }

  [[nodiscard]] const QString& identifier() const noexcept { return identifier_; }

  [[nodiscard]] bool is_display_axis() const noexcept { return selection_tag_ != ArrShowDimensionSelectionTag::none; }

  [[nodiscard]] bool is_navigable() const noexcept { return !is_display_axis() && observed_values_.size() > 1; }

  [[nodiscard]] bool step(const int step) {
    if (step == 0 || !is_navigable()) {
      return false;
    }
    const auto current_index = observed_values_.indexOf(current_value_);
    if (current_index < 0) {
      return false;
    }
    const auto next_index = current_index + (step > 0 ? 1 : -1);
    if (next_index < 0 || next_index >= observed_values_.size()) {
      return false;
    }
    request_value(observed_values_.at(next_index));
    return true;
  }

  [[nodiscard]] bool select_boundary(const bool maximum) {
    if (!is_navigable()) {
      return false;
    }
    const auto requested = maximum ? observed_values_.back() : observed_values_.front();
    if (requested == current_value_) {
      return false;
    }
    request_value(requested);
    return true;
  }

  void set_active(const bool active) {
    setProperty("arrShowDimensionActive", active);
    repolish(this);
    repolish(abbreviation_);
    repolish(value_);
  }

  void set_dimension(const ArrShowDimensionSpec& dimension) {
    identifier_ = dimension.identifier;
    label_ = dimension.label;
    selection_tag_ = dimension.selection_tag;
    selection_change_enabled_ = dimension.selection_change_enabled;
    displayed_extent_ = dimension.displayed_extent;
    observed_values_ = dimension.observed_values;
    std::sort(observed_values_.begin(), observed_values_.end());
    observed_values_.erase(std::unique(observed_values_.begin(), observed_values_.end()), observed_values_.end());
    if (observed_values_.isEmpty()) {
      current_value_ = dimension.current_value;
    } else if (observed_values_.contains(dimension.current_value)) {
      current_value_ = dimension.current_value;
    } else {
      current_value_ = observed_values_.front();
    }

    const auto suffix = object_name_suffix(identifier_);
    increment_button_->setObjectName(object_name_prefix_ + QStringLiteral("Dimension") + suffix + QStringLiteral("Up"));
    value_->setObjectName(object_name_prefix_ + QStringLiteral("Dimension") + suffix + QStringLiteral("Value"));
    decrement_button_->setObjectName(object_name_prefix_ + QStringLiteral("Dimension") + suffix +
                                     QStringLiteral("Down"));
    abbreviation_->setObjectName(object_name_prefix_ + QStringLiteral("Dimension") + suffix +
                                 QStringLiteral("Abbreviation"));
    dimension_label_->setObjectName(object_name_prefix_ + QStringLiteral("Dimension") + suffix +
                                    QStringLiteral("Label"));
    abbreviation_->setText(dimension.abbreviation.isEmpty() ? label_ : dimension.abbreviation);
    setAccessibleName(label_);
    abbreviation_->setAccessibleName(tr("%1 dimension").arg(label_));
    value_->setAccessibleName(label_);
    increment_button_->setAccessibleName(tr("Increase %1").arg(label_));
    decrement_button_->setAccessibleName(tr("Decrease %1").arg(label_));
    dimension_label_->setAccessibleName(tr("%1 extent").arg(label_));

    const auto values = observed_values_text(observed_values_);
    const auto navigation_hint = tr("Use +/−, Up/Down, PageUp/PageDown, or the mouse wheel. Left/Right changes the "
                                    "active dimension. Only observed values are accepted.");
    const auto selection_tag_hint = selection_change_enabled_
                                      ? tr("Left-click the extent cell to choose or replace the blue ':' tag; "
                                           "right-click to choose or replace the red ':' tag. Colour identifies the "
                                           "replacement target, not X/Y; X/Y follow the selected columns' "
                                           "left-to-right order.")
                                      : tr("This source fixes ':' membership for this dimension. X/Y follow the "
                                           "left-to-right column order of the fixed axes.");
    const auto tool_tip =
      is_display_axis()
        ? tr("%1\n%2\nThis dimension is selected with the %3 ':' tag.")
            .arg(dimension.tool_tip.isEmpty() ? label_ : dimension.tool_tip, selection_tag_hint,
                 selection_tag_ == ArrShowDimensionSelectionTag::first ? tr("blue") : tr("red"))
        : (dimension.tool_tip.isEmpty()
             ? tr("%1. Observed values: %2. %3\n%4").arg(label_, values, navigation_hint, selection_tag_hint)
             : tr("%1\nObserved values: %2\n%3\n%4")
                 .arg(dimension.tool_tip, values, navigation_hint, selection_tag_hint));
    const auto abbreviation_tool_tip = tool_tip.startsWith(label_) ? tool_tip : tr("%1\n%2").arg(label_, tool_tip);
    setToolTip(tool_tip);
    increment_button_->setToolTip(tool_tip);
    decrement_button_->setToolTip(tool_tip);
    abbreviation_->setToolTip(abbreviation_tool_tip);
    value_->setToolTip(tool_tip);
    dimension_label_->setToolTip(tool_tip);
    dimension_label_->setText(displayed_extent_ > 0 ? QString::number(displayed_extent_) : QStringLiteral("?"));
    dimension_label_->setEnabled(selection_change_enabled_);
    const auto selection_tag_text = selection_tag_ == ArrShowDimensionSelectionTag::first    ? QStringLiteral("first")
                                    : selection_tag_ == ArrShowDimensionSelectionTag::second ? QStringLiteral("second")
                                                                                             : QStringLiteral("none");
    setProperty("arrShowDimensionSelectionTag", selection_tag_text);
    dimension_label_->setProperty("arrShowDimensionSelectionTag", selection_tag_text);
    abbreviation_->setProperty("arrShowDimensionSelectionTag", selection_tag_text);
    repolish(this);
    repolish(dimension_label_);
    repolish(abbreviation_);
    refresh_value();
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event == nullptr) {
      return QWidget::eventFilter(watched, event);
    }
    if (watched == dimension_label_) {
      if (event->type() == QEvent::MouseButtonRelease) {
        if (!selection_change_enabled_) {
          return true;
        }
        const auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton) {
          request_selection_tag(ArrShowDimensionSelectionTag::first);
          return true;
        }
        if (mouse_event->button() == Qt::RightButton) {
          request_selection_tag(ArrShowDimensionSelectionTag::second);
          return true;
        }
      }
      return QWidget::eventFilter(watched, event);
    }
    if (watched != value_) {
      return QWidget::eventFilter(watched, event);
    }
    switch (event->type()) {
      case QEvent::FocusIn:
        activate();
        break;
      case QEvent::Wheel:
        {
          const auto* wheel_event = static_cast<QWheelEvent*>(event);
          if (wheel_event->angleDelta().y() != 0) {
            activate();
            static_cast<void>(step(wheel_event->angleDelta().y() > 0 ? 1 : -1));
            return true;
          }
          break;
        }
      case QEvent::KeyPress:
        {
          const auto* key_event = static_cast<QKeyEvent*>(event);
          switch (key_event->key()) {
            case Qt::Key_Up:
            case Qt::Key_Plus:
            case Qt::Key_Equal:
              activate();
              static_cast<void>(step(1));
              return true;
            case Qt::Key_Down:
            case Qt::Key_Minus:
              activate();
              static_cast<void>(step(-1));
              return true;
            case Qt::Key_PageUp:
              activate();
              static_cast<void>(select_boundary(false));
              return true;
            case Qt::Key_PageDown:
              activate();
              static_cast<void>(select_boundary(true));
              return true;
            case Qt::Key_Left:
              if (activated_callback_) {
                activated_callback_(QStringLiteral("__previous__"));
              }
              return true;
            case Qt::Key_Right:
              if (activated_callback_) {
                activated_callback_(QStringLiteral("__next__"));
              }
              return true;
            default:
              break;
          }
          break;
        }
      default:
        break;
    }
    return QWidget::eventFilter(watched, event);
  }

private:
  void activate() {
    if (activated_callback_) {
      activated_callback_(identifier_);
    }
  }

  void request_value(const int value) {
    if (!is_navigable() || value == current_value_ || !observed_values_.contains(value)) {
      refresh_value();
      return;
    }
    if (value_changed_callback_) {
      value_changed_callback_(identifier_, value);
    }
  }

  void request_selection_tag(const ArrShowDimensionSelectionTag selection_tag) {
    if (selection_tag_changed_callback_ && selection_tag != ArrShowDimensionSelectionTag::none) {
      selection_tag_changed_callback_(identifier_, selection_tag);
    }
  }

  void refresh_value() {
    const auto fixed_index_input = !is_display_axis();
    value_->setText(fixed_index_input ? QString::number(current_value_) : QStringLiteral(":"));
    value_->setReadOnly(!fixed_index_input);
    value_->setProperty("arrShowDimensionFixedIndexInput", fixed_index_input);
    repolish(value_);
    const auto current_index = observed_values_.indexOf(current_value_);
    increment_button_->setEnabled(is_navigable() && current_index >= 0 && current_index + 1 < observed_values_.size());
    decrement_button_->setEnabled(is_navigable() && current_index > 0);
  }

  QString object_name_prefix_;
  QString identifier_;
  QString label_;
  QList<int> observed_values_;
  int current_value_{0};
  int displayed_extent_{0};
  ArrShowDimensionSelectionTag selection_tag_{ArrShowDimensionSelectionTag::none};
  bool selection_change_enabled_{true};
  QToolButton* abbreviation_ = nullptr;
  QToolButton* increment_button_ = nullptr;
  QLineEdit* value_ = nullptr;
  QToolButton* decrement_button_ = nullptr;
  QToolButton* dimension_label_ = nullptr;
  ActivatedCallback activated_callback_;
  ValueChangedCallback value_changed_callback_;
  SelectionTagChangedCallback selection_tag_changed_callback_;
};

ArrShowDimensionStrip::ArrShowDimensionStrip(QWidget* parent, QString object_name_prefix)
    : QWidget(parent), object_name_prefix_(std::move(object_name_prefix)) {
  setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(3);

  controls_host_ = new QWidget(this);
  controls_host_->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
  auto* controls_layout = new QHBoxLayout(controls_host_);
  controls_layout->setContentsMargins(0, 0, 0, 0);
  controls_layout->setSpacing(3);
  controls_layout->addStretch(1);
  layout->addWidget(controls_host_, 1);

  empty_state_ = new QLabel(tr("No plane dimensions"), this);
  empty_state_->setProperty("textRole", QStringLiteral("inspectorHint"));
  layout->addWidget(empty_state_, 1);
  controls_host_->hide();
}

void ArrShowDimensionStrip::set_dimensions(QList<ArrShowDimensionSpec> dimensions,
                                           QString preferred_active_identifier) {
  dimensions.erase(std::remove_if(dimensions.begin(), dimensions.end(),
                                  [](const auto& dimension) {
                                    return !is_visible_dimension(dimension);
                                  }),
                   dimensions.end());

  const auto same_shape =
    controls_.size() == dimensions.size() && std::equal(controls_.cbegin(), controls_.cend(), dimensions.cbegin(),
                                                        [](const auto* control, const auto& dimension) {
                                                          return control->identifier() == dimension.identifier;
                                                        });
  if (!same_shape) {
    auto* layout = static_cast<QHBoxLayout*>(controls_host_->layout());
    for (auto* control : controls_) {
      layout->removeWidget(control);
      delete control;
    }
    controls_.clear();
    for (const auto& dimension : dimensions) {
      auto* control = new ArrShowDimensionControl(
        dimension, object_name_prefix_,
        [this](const QString& identifier) {
          if (identifier == QStringLiteral("__previous__")) {
            static_cast<void>(focus_relative_dimension(-1));
          } else if (identifier == QStringLiteral("__next__")) {
            static_cast<void>(focus_relative_dimension(1));
          } else {
            set_active_dimension(identifier);
          }
        },
        [this](const QString& identifier, const int value) {
          handle_value_change(identifier, value);
        },
        [this](const QString& identifier, const ArrShowDimensionSelectionTag selection_tag) {
          handle_selection_tag_change(identifier, selection_tag);
        },
        controls_host_);
      layout->insertWidget(layout->count() - 1, control);
      controls_.append(control);
    }
  } else {
    for (qsizetype index = 0; index < dimensions.size(); ++index) {
      controls_.at(index)->set_dimension(dimensions.at(index));
    }
  }

  if (controls_.isEmpty()) {
    clear_active_dimension();
    controls_host_->hide();
    empty_state_->show();
    return;
  }
  empty_state_->hide();
  controls_host_->show();
  const auto is_selectable_identifier = [this](const QString& identifier) {
    const auto found = std::find_if(controls_.cbegin(), controls_.cend(), [&identifier](const auto* control) {
      return control->identifier() == identifier;
    });
    return found != controls_.cend() && !(*found)->is_display_axis();
  };
  const auto first_non_axis = [this]() -> ArrShowDimensionControl* {
    const auto found = std::find_if(controls_.cbegin(), controls_.cend(), [](const auto* control) {
      return !control->is_display_axis();
    });
    return found == controls_.cend() ? nullptr : *found;
  };
  // A complete two-axis ':' plane has no independent dimension to browse.
  // The axes may be in arbitrary columns after reassignment.
  if (first_non_axis() == nullptr) {
    clear_active_dimension();
    return;
  }
  if (!preferred_active_identifier.isEmpty() && is_selectable_identifier(preferred_active_identifier)) {
    set_active_dimension(preferred_active_identifier);
  } else if (!is_selectable_identifier(active_dimension_identifier_)) {
    const auto default_control =
      controls_.size() >= 3 && !controls_.at(2)->is_display_axis() ? controls_.at(2) : first_non_axis();
    set_active_dimension(default_control->identifier());
  } else {
    set_active_dimension(active_dimension_identifier_);
  }
}

void ArrShowDimensionStrip::set_value_changed_callback(ValueChangedCallback callback) {
  value_changed_callback_ = std::move(callback);
}

void ArrShowDimensionStrip::set_selection_tag_changed_callback(SelectionTagChangedCallback callback) {
  selection_tag_changed_callback_ = std::move(callback);
}

bool ArrShowDimensionStrip::has_dimensions() const noexcept {
  return !controls_.isEmpty();
}

QString ArrShowDimensionStrip::active_dimension_identifier() const {
  return active_dimension_identifier_;
}

bool ArrShowDimensionStrip::step_active_dimension(const int step) {
  if (step == 0 || active_dimension_identifier_.isEmpty()) {
    return false;
  }
  const auto control = std::find_if(controls_.cbegin(), controls_.cend(), [this](const auto* candidate) {
    return candidate->identifier() == active_dimension_identifier_;
  });
  return control != controls_.cend() && (*control)->step(step);
}

bool ArrShowDimensionStrip::focus_relative_dimension(const int step) {
  if (step == 0 || active_dimension_identifier_.isEmpty()) {
    return false;
  }
  QList<ArrShowDimensionControl*> selectable;
  for (auto* control : controls_) {
    if (!control->is_display_axis()) {
      selectable.append(control);
    }
  }
  if (selectable.isEmpty()) {
    return false;
  }
  const auto current = std::find_if(selectable.cbegin(), selectable.cend(), [this](const auto* control) {
    return control->identifier() == active_dimension_identifier_;
  });
  const auto current_index =
    current == selectable.cend() ? 0 : static_cast<int>(std::distance(selectable.cbegin(), current));
  const auto count = selectable.size();
  const auto next_index = (current_index + (step > 0 ? 1 : count - 1)) % count;
  auto* next = selectable.at(next_index);
  set_active_dimension(next->identifier());
  next->setFocus(Qt::OtherFocusReason);
  return true;
}

void ArrShowDimensionStrip::clear_active_dimension() {
  active_dimension_identifier_.clear();
  for (auto* control : controls_) {
    control->set_active(false);
  }
}

void ArrShowDimensionStrip::set_active_dimension(const QString& identifier) {
  const auto found = std::find_if(controls_.cbegin(), controls_.cend(), [&identifier](const auto* control) {
    return control->identifier() == identifier;
  });
  if (found == controls_.cend() || (*found)->is_display_axis()) {
    return;
  }
  active_dimension_identifier_ = identifier;
  for (auto* control : controls_) {
    control->set_active(control->identifier() == active_dimension_identifier_);
  }
}

void ArrShowDimensionStrip::handle_value_change(const QString& identifier, const int value) {
  set_active_dimension(identifier);
  if (value_changed_callback_) {
    value_changed_callback_(identifier, value);
  }
}

void ArrShowDimensionStrip::handle_selection_tag_change(const QString& identifier,
                                                        const ArrShowDimensionSelectionTag selection_tag) {
  if (selection_tag_changed_callback_ && selection_tag != ArrShowDimensionSelectionTag::none) {
    selection_tag_changed_callback_(identifier, selection_tag);
  }
}

} // namespace ksj::viewer
