#include "inspection_canvas.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMinimumZoomPercent = 25;
constexpr int kMaximumZoomPercent = 800;
constexpr qreal kCanvasMargin = 18.0;
constexpr qreal kOverlayMargin = 9.0;

[[nodiscard]] int clamped_zoom(const int percent) {
  return std::clamp(percent, kMinimumZoomPercent, kMaximumZoomPercent);
}

[[nodiscard]] QString elided_text(const QFontMetrics& metrics, const QString& text, const int width) {
  return metrics.elidedText(text, Qt::ElideRight, std::max(0, width));
}

} // namespace

namespace ksj::viewer {

InspectionCanvas::InspectionCanvas(QWidget* parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(360, 300);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setProperty("surfaceRole", QStringLiteral("imageCanvas"));
}

void InspectionCanvas::set_display_image(const QImage& image, const QString& empty_text,
                                         const bool preserve_interaction) {
  display_image_ = image;
  empty_text_ = empty_text;
  if (!preserve_interaction) {
    cursor_pixel_ = {-1, -1};
    pan_offset_ = {};
    panning_ = false;
    adjusting_window_ = false;
  }
  setProperty("hasDisplayImage", !display_image_.isNull());

  // Keep QLabel's standard pixmap state populated for accessibility tooling
  // and the existing widget corpus. PaintEvent owns the interactive canvas
  // rendering, so the pixmap itself is never scaled or treated as raw data.
  if (display_image_.isNull()) {
    QLabel::setPixmap({});
  } else {
    QLabel::setPixmap(QPixmap::fromImage(display_image_));
  }
  QLabel::setText({});
  if (!preserve_interaction && probe_callback_) {
    probe_callback_(cursor_pixel_);
  }
  update();
}

void InspectionCanvas::set_overlay_text(const QString& text) {
  overlay_text_ = text;
  update();
}

void InspectionCanvas::set_interaction_help(const QString& text) {
  interaction_help_ = text;
  update();
}

void InspectionCanvas::set_zoom_percent(const int percent) {
  change_zoom_percent(percent, false);
}

int InspectionCanvas::zoom_percent() const noexcept {
  return zoom_percent_;
}

void InspectionCanvas::fit_to_view() {
  pan_offset_ = {};
  change_zoom_percent(100, false);
  update();
}

void InspectionCanvas::set_browse_step_callback(BrowseStepCallback callback) {
  browse_step_callback_ = std::move(callback);
}

void InspectionCanvas::set_browse_dimension_callback(BrowseDimensionCallback callback) {
  browse_dimension_callback_ = std::move(callback);
}

void InspectionCanvas::set_probe_callback(ProbeCallback callback) {
  probe_callback_ = std::move(callback);
}

void InspectionCanvas::set_window_level_callback(WindowLevelCallback callback) {
  window_level_callback_ = std::move(callback);
}

void InspectionCanvas::set_reset_window_callback(SimpleCallback callback) {
  reset_window_callback_ = std::move(callback);
}

void InspectionCanvas::set_zoom_changed_callback(ZoomChangedCallback callback) {
  zoom_changed_callback_ = std::move(callback);
}

QRectF InspectionCanvas::rendered_image_rect() const {
  if (display_image_.isNull() || display_image_.width() <= 0 || display_image_.height() <= 0) {
    return {};
  }

  const auto available = contentsRect().adjusted(static_cast<int>(kCanvasMargin), static_cast<int>(kCanvasMargin),
                                                 -static_cast<int>(kCanvasMargin), -static_cast<int>(kCanvasMargin));
  if (available.width() <= 0 || available.height() <= 0) {
    return {};
  }

  const auto fit_scale = std::min(static_cast<qreal>(available.width()) / static_cast<qreal>(display_image_.width()),
                                  static_cast<qreal>(available.height()) / static_cast<qreal>(display_image_.height()));
  const auto scale = std::max<qreal>(0.001, fit_scale * static_cast<qreal>(zoom_percent_) / 100.0);
  const QSizeF displayed_size{static_cast<qreal>(display_image_.width()) * scale,
                              static_cast<qreal>(display_image_.height()) * scale};
  const QPointF center = QRectF(available).center() + pan_offset_;
  return {center.x() - displayed_size.width() * 0.5, center.y() - displayed_size.height() * 0.5, displayed_size.width(),
          displayed_size.height()};
}

QPoint InspectionCanvas::display_pixel_at(const QPoint& position) const {
  const auto image_rect = rendered_image_rect();
  if (image_rect.isEmpty() || !image_rect.contains(QPointF(position))) {
    return {-1, -1};
  }
  const auto relative_x = (static_cast<qreal>(position.x()) - image_rect.left()) / image_rect.width();
  const auto relative_y = (static_cast<qreal>(position.y()) - image_rect.top()) / image_rect.height();
  return {std::clamp(static_cast<int>(relative_x * display_image_.width()), 0, display_image_.width() - 1),
          std::clamp(static_cast<int>(relative_y * display_image_.height()), 0, display_image_.height() - 1)};
}

QPointF InspectionCanvas::display_delta_at(const QPoint& start, const QPoint& current) const {
  const auto image_rect = rendered_image_rect();
  if (image_rect.isEmpty() || display_image_.isNull()) {
    return {};
  }
  const auto device_delta = QPointF(current - start);
  return {device_delta.x() * static_cast<qreal>(display_image_.width()) / image_rect.width(),
          device_delta.y() * static_cast<qreal>(display_image_.height()) / image_rect.height()};
}

void InspectionCanvas::clamp_pan_offset() {
  if (display_image_.isNull()) {
    pan_offset_ = {};
    return;
  }
  const auto available = contentsRect().adjusted(static_cast<int>(kCanvasMargin), static_cast<int>(kCanvasMargin),
                                                 -static_cast<int>(kCanvasMargin), -static_cast<int>(kCanvasMargin));
  if (available.width() <= 0 || available.height() <= 0) {
    pan_offset_ = {};
    return;
  }
  const auto fit_scale = std::min(static_cast<qreal>(available.width()) / static_cast<qreal>(display_image_.width()),
                                  static_cast<qreal>(available.height()) / static_cast<qreal>(display_image_.height()));
  const auto scale = std::max<qreal>(0.001, fit_scale * static_cast<qreal>(zoom_percent_) / 100.0);
  const auto maximum_x = std::max<qreal>(
    0.0, (static_cast<qreal>(display_image_.width()) * scale - static_cast<qreal>(available.width())) * 0.5);
  const auto maximum_y = std::max<qreal>(
    0.0, (static_cast<qreal>(display_image_.height()) * scale - static_cast<qreal>(available.height())) * 0.5);
  pan_offset_.setX(std::clamp(pan_offset_.x(), -maximum_x, maximum_x));
  pan_offset_.setY(std::clamp(pan_offset_.y(), -maximum_y, maximum_y));
}

void InspectionCanvas::update_probe(const QPoint& position) {
  cursor_pixel_ = display_pixel_at(position);
  if (probe_callback_) {
    probe_callback_(cursor_pixel_);
  }
  update();
}

void InspectionCanvas::change_zoom_percent(const int percent, const bool notify) {
  const auto next_zoom = clamped_zoom(percent);
  if (next_zoom == zoom_percent_) {
    return;
  }
  zoom_percent_ = next_zoom;
  clamp_pan_offset();
  if (notify && zoom_changed_callback_) {
    zoom_changed_callback_(zoom_percent_);
  }
  update();
}

void InspectionCanvas::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event)
  QPainter painter(this);
  painter.fillRect(rect(), QColor(QStringLiteral("#111519")));
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const auto image_rect = rendered_image_rect();
  if (display_image_.isNull() || image_rect.isEmpty()) {
    painter.setPen(QColor(QStringLiteral("#d4dde4")));
    painter.drawText(contentsRect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, empty_text_);
    return;
  }

  painter.drawImage(image_rect, display_image_);
  painter.setPen(QPen(QColor(QStringLiteral("#778a98")), 1.0));
  painter.drawRect(image_rect.adjusted(0.5, 0.5, -0.5, -0.5));

  if (cursor_pixel_.x() >= 0 && cursor_pixel_.y() >= 0) {
    const auto cursor_x =
      image_rect.left() + (static_cast<qreal>(cursor_pixel_.x()) + 0.5) * image_rect.width() / display_image_.width();
    const auto cursor_y =
      image_rect.top() + (static_cast<qreal>(cursor_pixel_.y()) + 0.5) * image_rect.height() / display_image_.height();
    painter.setPen(QPen(QColor(111, 209, 224, 190), 1.0, Qt::DashLine));
    painter.drawLine(QPointF(image_rect.left(), cursor_y), QPointF(image_rect.right(), cursor_y));
    painter.drawLine(QPointF(cursor_x, image_rect.top()), QPointF(cursor_x, image_rect.bottom()));
    painter.setPen(QPen(QColor(QStringLiteral("#d8f3f8")), 1.0));
    painter.drawEllipse(QPointF(cursor_x, cursor_y), 3.0, 3.0);
  }

  painter.setPen(QColor(QStringLiteral("#dfe8ee")));
  const auto metrics = painter.fontMetrics();
  const auto overlay = overlay_text_.isEmpty() ? tr("Bounded display derivative · %1 × %2 · view %3%")
                                                   .arg(display_image_.width())
                                                   .arg(display_image_.height())
                                                   .arg(zoom_percent_)
                                               : overlay_text_;
  painter.drawText(QRectF(kOverlayMargin, kOverlayMargin, width() - 2.0 * kOverlayMargin, metrics.height() + 4.0),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   elided_text(metrics, overlay, width() - static_cast<int>(2.0 * kOverlayMargin)));

  painter.setPen(QColor(QStringLiteral("#aebdc6")));
  const auto help = interaction_help_.isEmpty()
                      ? tr("Wheel item · Ctrl+wheel zoom · drag pan · middle drag C/W · double-click reset")
                      : interaction_help_;
  painter.drawText(QRectF(kOverlayMargin, height() - metrics.height() - kOverlayMargin, width() - 2.0 * kOverlayMargin,
                          metrics.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   elided_text(metrics, help, width() - static_cast<int>(2.0 * kOverlayMargin)));
}

void InspectionCanvas::mousePressEvent(QMouseEvent* event) {
  if (event == nullptr) {
    return;
  }
  setFocus(Qt::MouseFocusReason);
  update_probe(event->position().toPoint());
  if (event->button() == Qt::LeftButton) {
    panning_ = true;
    drag_origin_ = event->position().toPoint();
    drag_start_pan_ = pan_offset_;
    event->accept();
    return;
  }
  if (event->button() == Qt::MiddleButton) {
    adjusting_window_ = true;
    drag_origin_ = event->position().toPoint();
    if (window_level_callback_) {
      window_level_callback_({}, false);
    }
    event->accept();
    return;
  }
  if (event->button() == Qt::RightButton && adjusting_window_ && reset_window_callback_) {
    adjusting_window_ = false;
    reset_window_callback_();
    event->accept();
    return;
  }
  QLabel::mousePressEvent(event);
}

void InspectionCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (event == nullptr) {
    return;
  }
  const auto position = event->position().toPoint();
  if (panning_ && (event->buttons() & Qt::LeftButton) != 0) {
    pan_offset_ = drag_start_pan_ + QPointF(position - drag_origin_);
    clamp_pan_offset();
  }
  if (adjusting_window_ && (event->buttons() & Qt::MiddleButton) != 0 && window_level_callback_) {
    window_level_callback_(display_delta_at(drag_origin_, position), false);
  }
  update_probe(position);
  event->accept();
}

void InspectionCanvas::mouseReleaseEvent(QMouseEvent* event) {
  if (event == nullptr) {
    return;
  }
  const auto position = event->position().toPoint();
  if (event->button() == Qt::LeftButton && panning_) {
    panning_ = false;
    update_probe(position);
    event->accept();
    return;
  }
  if (event->button() == Qt::MiddleButton && adjusting_window_) {
    adjusting_window_ = false;
    if (window_level_callback_) {
      window_level_callback_(display_delta_at(drag_origin_, position), true);
    }
    update_probe(position);
    event->accept();
    return;
  }
  QLabel::mouseReleaseEvent(event);
}

void InspectionCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event != nullptr && !display_image_.isNull() && reset_window_callback_) {
    reset_window_callback_();
    event->accept();
    return;
  }
  QLabel::mouseDoubleClickEvent(event);
}

void InspectionCanvas::wheelEvent(QWheelEvent* event) {
  if (event == nullptr || event->angleDelta().y() == 0) {
    return;
  }
  const auto direction = event->angleDelta().y() > 0 ? 1 : -1;
  if ((event->modifiers() & Qt::ControlModifier) != 0) {
    const auto anchor_position = event->position();
    const auto old_rect = rendered_image_rect();
    const auto anchor_is_on_image = !old_rect.isEmpty() && old_rect.contains(anchor_position);
    const auto anchor_x = anchor_is_on_image ? (anchor_position.x() - old_rect.left()) / old_rect.width() : 0.5;
    const auto anchor_y = anchor_is_on_image ? (anchor_position.y() - old_rect.top()) / old_rect.height() : 0.5;
    const auto factor = direction > 0 ? 1.5 : (1.0 / 1.5);
    change_zoom_percent(static_cast<int>(std::lround(static_cast<qreal>(zoom_percent_) * factor)), true);
    if (anchor_is_on_image) {
      const auto new_rect = rendered_image_rect();
      const QPointF projected_anchor{new_rect.left() + anchor_x * new_rect.width(),
                                     new_rect.top() + anchor_y * new_rect.height()};
      pan_offset_ += anchor_position - projected_anchor;
      clamp_pan_offset();
      update();
    }
  } else if (browse_step_callback_) {
    browse_step_callback_(direction);
  }
  event->accept();
}

void InspectionCanvas::keyPressEvent(QKeyEvent* event) {
  if (event == nullptr) {
    return;
  }
  switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      if (browse_step_callback_) {
        browse_step_callback_(1);
      }
      event->accept();
      return;
    case Qt::Key_Down:
    case Qt::Key_Minus:
      if (browse_step_callback_) {
        browse_step_callback_(-1);
      }
      event->accept();
      return;
    case Qt::Key_Left:
      if (browse_dimension_callback_) {
        browse_dimension_callback_(-1);
        event->accept();
        return;
      }
      break;
    case Qt::Key_Right:
      if (browse_dimension_callback_) {
        browse_dimension_callback_(1);
        event->accept();
        return;
      }
      break;
    case Qt::Key_F:
      pan_offset_ = {};
      change_zoom_percent(100, true);
      event->accept();
      return;
    case Qt::Key_0:
      if (reset_window_callback_) {
        reset_window_callback_();
      }
      event->accept();
      return;
    default:
      QLabel::keyPressEvent(event);
      return;
  }
}

void InspectionCanvas::leaveEvent(QEvent* event) {
  cursor_pixel_ = {-1, -1};
  if (probe_callback_) {
    probe_callback_(cursor_pixel_);
  }
  update();
  QLabel::leaveEvent(event);
}

void InspectionCanvas::resizeEvent(QResizeEvent* event) {
  QLabel::resizeEvent(event);
  clamp_pan_offset();
  update();
}

} // namespace ksj::viewer
