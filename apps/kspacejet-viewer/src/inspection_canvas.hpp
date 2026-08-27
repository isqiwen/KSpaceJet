#pragma once

#include <QImage>
#include <QLabel>
#include <QPoint>
#include <QPointF>
#include <QString>

#include <functional>

namespace ksj::viewer {

// A local, bounded-derivative canvas shared by the Image and K-space pages.
// It deliberately owns only a QImage supplied by the presentation layer; it
// never sees, caches, or exposes ISMRMRD payload views.
class InspectionCanvas final : public QLabel {
public:
  // The shared canvas can browse an owning view's ordered items.  The item is
  // intentionally domain-neutral: Image uses image ordinals, while K-space
  // uses observed ISMRMRD encoding-index coordinates.
  using BrowseStepCallback = std::function<void(int step)>;
  // arrShow uses Left/Right to select the active dimension control while
  // wheel, +/- and Up/Down adjust that active dimension. Image leaves this
  // callback unset because it has one ordinal browser; K-space binds it to
  // its arrShow-style non-axis dimension strip.
  using BrowseDimensionCallback = std::function<void(int step)>;
  using ProbeCallback = std::function<void(QPoint display_pixel)>;
  // Delta is expressed in bounded display-pixel coordinates, not device
  // pixels, so C/W uses the same scale at every zoom level.
  using WindowLevelCallback = std::function<void(QPointF drag_delta, bool finished)>;
  using SimpleCallback = std::function<void()>;
  using ZoomChangedCallback = std::function<void(int percent)>;

  explicit InspectionCanvas(QWidget* parent = nullptr);

  void set_display_image(const QImage& image, const QString& empty_text, bool preserve_interaction = false);
  void set_overlay_text(const QString& text);
  void set_interaction_help(const QString& text);
  void set_zoom_percent(int percent);
  [[nodiscard]] int zoom_percent() const noexcept;
  void fit_to_view();

  void set_browse_step_callback(BrowseStepCallback callback);
  void set_browse_dimension_callback(BrowseDimensionCallback callback);
  void set_probe_callback(ProbeCallback callback);
  void set_window_level_callback(WindowLevelCallback callback);
  void set_reset_window_callback(SimpleCallback callback);
  void set_zoom_changed_callback(ZoomChangedCallback callback);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

private:
  [[nodiscard]] QRectF rendered_image_rect() const;
  [[nodiscard]] QPoint display_pixel_at(const QPoint& position) const;
  [[nodiscard]] QPointF display_delta_at(const QPoint& start, const QPoint& current) const;
  void clamp_pan_offset();
  void update_probe(const QPoint& position);
  void change_zoom_percent(int percent, bool notify);

  QImage display_image_;
  QString empty_text_;
  QString overlay_text_;
  QString interaction_help_;
  QPointF pan_offset_;
  QPoint drag_origin_;
  QPointF drag_start_pan_;
  QPoint cursor_pixel_{-1, -1};
  int zoom_percent_{100};
  bool panning_{false};
  bool adjusting_window_{false};
  BrowseStepCallback browse_step_callback_;
  BrowseDimensionCallback browse_dimension_callback_;
  ProbeCallback probe_callback_;
  WindowLevelCallback window_level_callback_;
  SimpleCallback reset_window_callback_;
  ZoomChangedCallback zoom_changed_callback_;
};

} // namespace ksj::viewer
