// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Before/after canvas.
//
// Three comparison modes share one transform so that "which one is sharper"
// is never an artefact of different zoom levels:
//   split  - one image, draggable divider, left = before, right = after
//   side   - two viewports at the same scale
//   result - after only

#ifndef FIGTOOLS_GUI_IMAGE_CANVAS_HPP_
#define FIGTOOLS_GUI_IMAGE_CANVAS_HPP_

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <functional>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace figtools::gui {

class ImageCanvas : public QWidget {
  Q_OBJECT

 public:
  enum class Mode { kSplit, kSide, kResult };

  explicit ImageCanvas(QWidget* parent = nullptr);

  void setImages(const QImage& before, const QImage& after);
  void clearImages();
  void setMode(Mode mode);
  [[nodiscard]] Mode mode() const { return mode_; }

  void fitToWindow();
  void zoomToActualSize();
  [[nodiscard]] double zoom() const { return zoom_; }

  void setStatusCallback(std::function<void(const QString&)> callback);
  void setCursorCallback(std::function<void(QPoint, bool)> callback);
  void setZoomCallback(std::function<void(double)> callback);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  [[nodiscard]] QRectF fittedRect(const QSize& image_size, const QRectF& viewport,
                                  double scale) const;
  [[nodiscard]] double fitScale(const QSize& image_size, const QRectF& viewport) const;
  [[nodiscard]] double dividerX() const;
  [[nodiscard]] QPoint toImagePoint(QPoint widget_point) const;
  void drawEmptyState(QPainter& painter);

  QImage before_;
  QImage after_;
  Mode mode_ = Mode::kSplit;
  double zoom_ = 1.0;
  QPointF pan_;
  double split_ = 0.5;
  bool dragging_divider_ = false;
  bool panning_ = false;
  QPoint last_mouse_;
  bool auto_fit_ = true;

  std::function<void(const QString&)> status_callback_;
  std::function<void(QPoint, bool)> cursor_callback_;
  std::function<void(double)> zoom_callback_;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_IMAGE_CANVAS_HPP_
