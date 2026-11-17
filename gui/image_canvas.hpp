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
#include <QPointF>
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <QTimer>
#include <functional>
#include <vector>

#include "figtools/annotate/annotation.hpp"

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace figtools::gui {

class ImageCanvas : public QWidget {
  Q_OBJECT

 public:
  enum class Mode { kSplit, kSide, kResult, kFlicker, kDifference };
  enum class Tool { kNone, kRect, kPolygon, kProbe, kMeasure };

  explicit ImageCanvas(QWidget* parent = nullptr);

  void setImages(const QImage& before, const QImage& after);
  void clearImages();
  void setMode(Mode mode);
  void setFlickerInterval(int milliseconds);
  [[nodiscard]] Mode mode() const { return mode_; }

  void fitToWindow();
  void zoomToActualSize();
  [[nodiscard]] double zoom() const { return zoom_; }

  void setTool(Tool tool);
  [[nodiscard]] Tool tool() const { return tool_; }
  void setAnnotationSet(figtools::AnnotationSet* annotations);
  void setActiveClass(int class_id);
  void setAnnotationsVisible(bool visible);
  void setAnnotationCallback(std::function<void()> callback);
  [[nodiscard]] int selected_index() const { return selected_; }
  void setSelectedIndex(int index);

  // A classifier result shown as a chip on the image; empty label clears it.
  void setInferenceResult(const QString& label, double confidence);
  void clearInferenceResult();

  // Detector results are boxes in result-image pixel coordinates. They live in
  // a separate overlay from manual annotations so inference never corrupts the
  // ground truth that feeds training.
  struct Detection {
    QString label;
    double confidence = 0.0;
    QRectF box;
  };
  void setDetections(std::vector<Detection> detections);
  void clearDetections();
  [[nodiscard]] const std::vector<Detection>& detections() const { return detections_; }

  struct Segment {
    QString label;
    double confidence = 0.0;
    std::vector<QPointF> polygon;
  };
  void setSegments(std::vector<Segment> segments);
  void clearSegments();
  [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }

  void setStatusCallback(std::function<void(const QString&)> callback);
  void setCursorCallback(std::function<void(QPoint, bool)> callback);
  void setZoomCallback(std::function<void(double)> callback);

 signals:
  void toolChanged(ImageCanvas::Tool tool);
  void classShortcut(int index);      // 0-based class index from number keys
  void saveRequested();
  void prevImageRequested();
  void nextImageRequested();
  void annotationEdited();            // a shape was moved / reshaped

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  [[nodiscard]] QRectF fittedRect(const QSize& image_size, const QRectF& viewport,
                                  double scale) const;
  [[nodiscard]] double fitScale(const QSize& image_size, const QRectF& viewport) const;
  [[nodiscard]] double dividerX() const;
  [[nodiscard]] QPoint toImagePoint(QPoint widget_point) const;
  void drawEmptyState(QPainter& painter);
  void drawAnnotations(QPainter& painter);
  void drawDetections(QPainter& painter);
  void drawSegments(QPainter& painter);
  [[nodiscard]] QRectF displayedImageRect() const;
  [[nodiscard]] QPointF toImagePointF(QPointF widget_point) const;
  [[nodiscard]] int hitTest(QPointF image_point) const;
  struct EditHandle {
    int annotation = -1;
    int vertex = -1;      // -1 = move whole shape; else vertex/corner index
    bool edge = false;    // true when grabbing a polygon edge (insert point)
  };
  [[nodiscard]] EditHandle hitHandle(QPointF image_point) const;
  [[nodiscard]] double screenToImageRadius() const;
  void applyHandleDrag(const EditHandle& handle, QPointF image_point);
  void rebuildDifference();
  void finishPolygon();
  void finishRect();

  QImage before_;
  QImage after_;
  Mode mode_ = Mode::kSplit;
  double zoom_ = 1.0;
  QPointF pan_;
  double split_ = 0.5;
  bool flicker_show_after_ = false;
  int flicker_interval_ms_ = 450;
  QTimer* flicker_timer_ = nullptr;
  QImage difference_image_;
  bool dragging_divider_ = false;
  bool panning_ = false;
  QPoint last_mouse_;
  bool auto_fit_ = true;

  Tool tool_ = Tool::kNone;
  QPointF measure_start_;  // first point of a distance measurement
  QPointF measure_end_;
  bool measuring_ = false;
  AnnotationSet* annotations_ = nullptr;
  int active_class_ = 0;
  int selected_ = -1;
  bool annotations_visible_ = true;
  bool drawing_rect_ = false;
  QPointF rect_start_;
  QPointF rect_current_;
  std::vector<Point2f> polygon_draft_;
  EditHandle active_handle_;
  figtools::Shape drag_original_shape_;
  bool dragging_handle_ = false;
  bool space_panning_ = false;
  std::function<void()> annotation_callback_;

  std::function<void(const QString&)> status_callback_;
  std::function<void(QPoint, bool)> cursor_callback_;
  std::function<void(double)> zoom_callback_;

  QString inference_label_;
  double inference_confidence_ = -1.0;
  std::vector<Detection> detections_;
  std::vector<Segment> segments_;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_IMAGE_CANVAS_HPP_
