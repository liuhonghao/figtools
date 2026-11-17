// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "image_canvas.hpp"

#include "theme.hpp"
#include <algorithm>

#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace figtools::gui {
namespace {

constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 16.0;
constexpr double kDividerHitWidth = 10.0;

constexpr double kPadding = 24.0;

QColor kFrameBorder{36, 31, 24, 34};
QColor kChipFill{255, 253, 248, 238};
QColor kChipBorder{36, 31, 24, 30};
QColor kChipText{0x3A, 0x34, 0x2B};

}  // namespace

ImageCanvas::ImageCanvas(QWidget* parent) : QWidget(parent) {
  setMinimumSize(420, 320);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::CrossCursor);
  flicker_timer_ = new QTimer(this);
  flicker_timer_->setInterval(flicker_interval_ms_);
  connect(flicker_timer_, &QTimer::timeout, this, [this]() {
    flicker_show_after_ = !flicker_show_after_;
    update();
  });
}

void ImageCanvas::setImages(const QImage& before, const QImage& after) {
  const bool first = before_.isNull();
  before_ = before;
  after_ = after;
  rebuildDifference();
  if (first || auto_fit_) {
    zoom_ = 1.0;
    pan_ = QPointF(0.0, 0.0);
  }
  update();
}

void ImageCanvas::clearImages() {
  before_ = QImage();
  after_ = QImage();
  update();
}

void ImageCanvas::setMode(Mode mode) {
  mode_ = mode;
  if (mode_ == Mode::kFlicker) {
    flicker_show_after_ = false;
    flicker_timer_->start();
  } else if (flicker_timer_ != nullptr) {
    flicker_timer_->stop();
  }
  update();
}

void ImageCanvas::setFlickerInterval(int milliseconds) {
  flicker_interval_ms_ = std::max(80, milliseconds);
  if (flicker_timer_ != nullptr) {
    flicker_timer_->setInterval(flicker_interval_ms_);
  }
}

void ImageCanvas::rebuildDifference() {
  difference_image_ = QImage();
  if (before_.isNull() || after_.isNull() ||
      before_.size() != after_.size()) {
    return;
  }
  const int w = before_.width(), h = before_.height();
  difference_image_ = QImage(w, h, QImage::Format_RGB32);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const QRgb a = before_.pixel(x, y);
      const QRgb b = after_.pixel(x, y);
      const int diff = std::abs(static_cast<int>(qRed(a)) - static_cast<int>(qRed(b))) +
                       std::abs(static_cast<int>(qGreen(a)) - static_cast<int>(qGreen(b))) +
                       std::abs(static_cast<int>(qBlue(a)) - static_cast<int>(qBlue(b)));
      // Map 0..765 to a black -> red -> yellow -> white heat scale.
      const int v = std::min(255, diff * 2);
      int r = 0, g = 0, bl = 0;
      if (v < 128) {
        r = v * 2;
      } else if (v < 220) {
        r = 255;
        g = (v - 128) * 3;
      } else {
        r = 255;
        g = 255;
        bl = (v - 220) * 3;
      }
      difference_image_.setPixel(x, y, qRgb(r, g, bl));
    }
  }
}

void ImageCanvas::setSegmentationPreview(const std::vector<QPointF>& polygon) {
  preview_polygon_ = polygon;
  update();
}

void ImageCanvas::clearSegmentationPreview() {
  preview_polygon_.clear();
  update();
}

void ImageCanvas::setStatusCallback(std::function<void(const QString&)> callback) {
  status_callback_ = std::move(callback);
}

void ImageCanvas::setCursorCallback(std::function<void(QPoint, bool)> callback) {
  cursor_callback_ = std::move(callback);
}

void ImageCanvas::setZoomCallback(std::function<void(double)> callback) {
  zoom_callback_ = std::move(callback);
}

void ImageCanvas::setTool(Tool tool) {
  tool_ = tool;
  polygon_draft_.clear();
  drawing_rect_ = false;
  measuring_ = false;
  measure_start_ = QPointF();
  measure_end_ = QPointF();
  setCursor(tool == Tool::kNone ? Qt::CrossCursor : Qt::CrossCursor);
  update();
}

void ImageCanvas::setAnnotationSet(AnnotationSet* annotations) {
  annotations_ = annotations;
  selected_ = -1;
  update();
}

void ImageCanvas::setActiveClass(int class_id) {
  active_class_ = class_id;
}

void ImageCanvas::setAnnotationsVisible(bool visible) {
  annotations_visible_ = visible;
  update();
}

void ImageCanvas::setAnnotationCallback(std::function<void()> callback) {
  annotation_callback_ = std::move(callback);
}

void ImageCanvas::setSelectedIndex(int index) {
  selected_ = index;
  update();
}

void ImageCanvas::setInferenceResult(const QString& label, double confidence) {
  inference_label_ = label;
  inference_confidence_ = confidence;
  update();
}

void ImageCanvas::clearInferenceResult() {
  inference_label_.clear();
  inference_confidence_ = -1.0;
  update();
}

void ImageCanvas::setDetections(std::vector<Detection> detections) {
  detections_ = std::move(detections);
  update();
}

void ImageCanvas::clearDetections() {
  detections_.clear();
  update();
}

void ImageCanvas::fitToWindow() {
  zoom_ = 1.0;
  pan_ = QPointF(0.0, 0.0);
  auto_fit_ = true;
  update();
  if (zoom_callback_) {
    zoom_callback_(zoom_);
  }
}

void ImageCanvas::zoomToActualSize() {
  if (after_.isNull()) {
    return;
  }
  const QRectF viewport = rect();
  const QRectF target = (mode_ == Mode::kSide) ? QRectF(0, 0, viewport.width() / 2.0, viewport.height())
                                               : viewport;
  const double base = fitScale(after_.size(), target.adjusted(kPadding, kPadding, -kPadding, -kPadding));
  if (base <= 0.0) {
    return;
  }
  zoom_ = 1.0 / base;
  auto_fit_ = false;
  pan_ = QPointF(0.0, 0.0);
  update();
  if (zoom_callback_) {
    zoom_callback_(zoom_);
  }
}

double ImageCanvas::fitScale(const QSize& image_size, const QRectF& viewport) const {
  if (image_size.isEmpty() || viewport.isEmpty()) {
    return 1.0;
  }
  const double sx = viewport.width() / static_cast<double>(image_size.width());
  const double sy = viewport.height() / static_cast<double>(image_size.height());
  return std::min(sx, sy);
}

QRectF ImageCanvas::fittedRect(const QSize& image_size, const QRectF& viewport,
                               double scale) const {
  if (image_size.isEmpty()) {
    return {};
  }
  const double width = static_cast<double>(image_size.width()) * scale;
  const double height = static_cast<double>(image_size.height()) * scale;
  return QRectF(viewport.center().x() - width / 2.0 + pan_.x(),
                viewport.center().y() - height / 2.0 + pan_.y(), width, height);
}

double ImageCanvas::dividerX() const {
  return static_cast<double>(width()) * split_;
}

QPoint ImageCanvas::toImagePoint(QPoint widget_point) const {
  if (after_.isNull()) {
    return {};
  }
  const QRectF viewport = rect();
  QRectF target = viewport;
  if (mode_ == Mode::kSplit || mode_ == Mode::kResult) {
    const double base = fitScale(after_.size(), viewport.adjusted(kPadding, kPadding, -kPadding, -kPadding));
    const double scale = base * zoom_;
    const QRectF drawn = fittedRect(after_.size(), viewport, scale);
    if (drawn.width() <= 0.0) {
      return {};
    }
    const double fx = (static_cast<double>(widget_point.x()) - drawn.left()) / drawn.width();
    const double fy = (static_cast<double>(widget_point.y()) - drawn.top()) / drawn.height();
    return QPoint(static_cast<int>(fx * after_.width()), static_cast<int>(fy * after_.height()));
  }
  const QRectF left(0, 0, viewport.width() / 2.0, viewport.height());
  const double base = fitScale(before_.size(), left.adjusted(kPadding, kPadding, -kPadding, -kPadding));
  const double scale = base * zoom_;
  const QRectF drawn = fittedRect(before_.size(), left, scale);
  if (drawn.width() <= 0.0) {
    return {};
  }
  const double fx = (static_cast<double>(widget_point.x()) - drawn.left()) / drawn.width();
  const double fy = (static_cast<double>(widget_point.y()) - drawn.top()) / drawn.height();
  return QPoint(static_cast<int>(fx * before_.width()), static_cast<int>(fy * before_.height()));
}

void ImageCanvas::drawDetections(QPainter& painter) {
  if (detections_.empty() || after_.isNull()) {
    return;
  }
  const QRectF target = displayedImageRect();
  if (target.isEmpty()) {
    return;
  }
  const double sx = target.width() / static_cast<double>(after_.width());
  const double sy = target.height() / static_cast<double>(after_.height());

  // Detection boxes use result-image coordinates. In split / side modes the
  // plate shares one geometry, so the boxes are drawn across the full plate;
  // clipping them to the result half would hide targets that happen to sit on
  // the source side of the divider.
  painter.save();
  painter.setClipRect(target);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor colour = theme::colour(theme::kAccent);
  for (const Detection& detection : detections_) {
    const QRectF box(
        target.left() + static_cast<double>(detection.box.left()) * sx,
        target.top() + static_cast<double>(detection.box.top()) * sy,
        static_cast<double>(detection.box.width()) * sx,
        static_cast<double>(detection.box.height()) * sy);
    painter.setPen(QPen(colour, 2.0));
    QColor fill = colour;
    fill.setAlpha(26);
    painter.setBrush(fill);
    painter.drawRect(box);

    const QString label = tr("%1 %2%")
                              .arg(detection.label)
                              .arg(detection.confidence * 100.0, 0, 'f', 0);
    painter.setFont(theme::ui_font(10, QFont::DemiBold));
    const int text_width = painter.fontMetrics().horizontalAdvance(label) + 12;
    const QRectF tag(box.left(), std::max(0.0, box.top() - 18.0), text_width, 17);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(tag, 3, 3);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(tag, Qt::AlignCenter, label);
  }
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.restore();
}

void ImageCanvas::setSegments(std::vector<Segment> segments) {
  segments_ = std::move(segments);
  update();
}

void ImageCanvas::clearSegments() {
  segments_.clear();
  update();
}

void ImageCanvas::drawSegments(QPainter& painter) {
  if (segments_.empty() || after_.isNull()) {
    return;
  }
  const QRectF target = displayedImageRect();
  if (target.isEmpty()) {
    return;
  }
  const double sx = target.width() / static_cast<double>(after_.width());
  const double sy = target.height() / static_cast<double>(after_.height());
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (const Segment& segment : segments_) {
    if (segment.polygon.size() < 3) {
      continue;
    }
    QPolygonF polygon;
    polygon.reserve(static_cast<qsizetype>(segment.polygon.size()));
    for (const QPointF& point : segment.polygon) {
      polygon << QPointF(target.left() + static_cast<double>(point.x()) * sx,
                         target.top() + static_cast<double>(point.y()) * sy);
    }
    QColor fill = theme::colour(theme::kInfo);
    fill.setAlpha(52);
    painter.setPen(QPen(theme::colour(theme::kInfo), 1.6));
    painter.setBrush(fill);
    painter.drawPolygon(polygon);

    const QRectF bounds = polygon.boundingRect();
    const QString label = QStringLiteral("%1 %2%")
                              .arg(segment.label)
                              .arg(segment.confidence * 100.0, 0, 'f', 0);
    painter.setFont(theme::ui_font(10, QFont::DemiBold));
    const int text_width = painter.fontMetrics().horizontalAdvance(label) + 12;
    const QRectF tag(bounds.left(), std::max(0.0, bounds.top() - 18.0), text_width, 17);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme::colour(theme::kInfo));
    painter.drawRoundedRect(tag, 3, 3);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(tag, Qt::AlignCenter, label);
  }
  painter.setRenderHint(QPainter::Antialiasing, false);
}

void ImageCanvas::drawEmptyState(QPainter& painter) {
  const QRectF box = QRectF(rect()).adjusted(60, 60, -60, -60);
  QPen pen(theme::colour("#D2C5AE"));
  pen.setStyle(Qt::DashLine);
  pen.setWidthF(1.0);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(box, 8, 8);

  painter.setPen(theme::colour(theme::kTextDim));
  painter.setFont(theme::ui_font(13));
  painter.drawText(box.adjusted(0, 0, 0, -22), Qt::AlignCenter,
                   tr("将图像拖到这里，或按 Ctrl+O 打开"));

  painter.setPen(theme::colour(theme::kTextFaint));
  painter.setFont(theme::ui_font(11));
  painter.drawText(box.adjusted(0, 26, 0, 0), Qt::AlignCenter,
                   tr("中间分界线可拖动，对比处理前后"));
}

QRectF ImageCanvas::displayedImageRect() const {
  if (after_.isNull()) {
    return {};
  }
  const QRectF viewport = rect();
  if (mode_ == Mode::kSide) {
    const QRectF right(viewport.width() / 2.0, 0, viewport.width() / 2.0, viewport.height());
    const QRectF left(0, 0, viewport.width() / 2.0, viewport.height());
    const double base = std::min(fitScale(before_.size(), left.adjusted(kPadding, kPadding, -kPadding, -kPadding)),
                                 fitScale(after_.size(), right.adjusted(kPadding, kPadding, -kPadding, -kPadding)));
    return fittedRect(after_.size(), right, base * zoom_);
  }
  const QRectF content = viewport.adjusted(kPadding, kPadding, -kPadding, -kPadding);
  return fittedRect(after_.size(), viewport, fitScale(after_.size(), content) * zoom_);
}

QPointF ImageCanvas::toImagePointF(QPointF widget_point) const {
  const QRectF target = displayedImageRect();
  if (target.isEmpty() || after_.isNull()) {
    return {};
  }
  const double fx = (widget_point.x() - target.left()) / target.width();
  const double fy = (widget_point.y() - target.top()) / target.height();
  return QPointF(std::clamp(fx, 0.0, 1.0) * after_.width(), std::clamp(fy, 0.0, 1.0) * after_.height());
}

int ImageCanvas::hitTest(QPointF image_point) const {
  if (annotations_ == nullptr) {
    return -1;
  }
  const auto& items = annotations_->items();
  for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i) {
    const Point2f probe{static_cast<float>(image_point.x()), static_cast<float>(image_point.y())};
    if (items[static_cast<std::size_t>(i)].shape.contains(probe)) {
      return i;
    }
  }
  return -1;
}

double ImageCanvas::screenToImageRadius() const {
  const QRectF target = displayedImageRect();
  if (target.isEmpty() || after_.isNull()) {
    return 4.0;
  }
  const double sx = target.width() / static_cast<double>(after_.width());
  const double sy = target.height() / static_cast<double>(after_.height());
  const double scale = std::min(sx, sy);
  return scale > 1e-6 ? 7.0 / scale : 1e6;
}

ImageCanvas::EditHandle ImageCanvas::hitHandle(QPointF image_point) const {
  EditHandle result;
  if (annotations_ == nullptr || selected_ < 0 ||
      selected_ >= static_cast<int>(annotations_->items().size())) {
    return result;
  }
  const Annotation& annotation = annotations_->items()[static_cast<std::size_t>(selected_)];
  const double radius = screenToImageRadius();
  const auto near = [&](Point2f p) {
    const double dx = static_cast<double>(p.x) - image_point.x();
    const double dy = static_cast<double>(p.y) - image_point.y();
    return dx * dx + dy * dy <= radius * radius;
  };

  if (annotation.shape.type == ShapeType::kRect) {
    const RectF& rect = annotation.shape.rect;
    const Point2f corners[] = {
        {rect.x, rect.y}, {rect.x + rect.width, rect.y},
        {rect.x + rect.width, rect.y + rect.height}, {rect.x, rect.y + rect.height}};
    for (int i = 0; i < 4; ++i) {
      if (near(corners[i])) {
        result.annotation = selected_;
        result.vertex = i;
        return result;
      }
    }
    result.annotation = selected_;  // inside -> move whole shape
    return result;
  }

  const auto& points = annotation.shape.points;
  for (int i = 0; i < static_cast<int>(points.size()); ++i) {
    if (near(points[static_cast<std::size_t>(i)])) {
      result.annotation = selected_;
      result.vertex = i;
      return result;
    }
  }
  // Edge grab: used by double-click to insert a new vertex.
  for (int i = 0; i < static_cast<int>(points.size()); ++i) {
    const Point2f a = points[static_cast<std::size_t>(i)];
    const Point2f b = points[static_cast<std::size_t>((i + 1) % points.size())];
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-6) continue;
    const double t = std::clamp(((image_point.x() - a.x) * dx + (image_point.y() - a.y) * dy) / len2,
                                0.0, 1.0);
    const double px = a.x + t * dx, py = a.y + t * dy;
    if (std::hypot(px - image_point.x(), py - image_point.y()) <= radius) {
      result.annotation = selected_;
      result.vertex = i;
      result.edge = true;
      return result;
    }
  }
  if (annotation.shape.contains({static_cast<float>(image_point.x()),
                                 static_cast<float>(image_point.y())})) {
    result.annotation = selected_;  // inside polygon -> move whole shape
  }
  return result;
}

void ImageCanvas::applyHandleDrag(const EditHandle& handle, QPointF image_point) {
  if (annotations_ == nullptr || handle.annotation < 0 ||
      handle.annotation >= static_cast<int>(annotations_->items().size())) {
    return;
  }
  Shape& shape = annotations_->items()[static_cast<std::size_t>(handle.annotation)].shape;
  const Point2f now{static_cast<float>(image_point.x()), static_cast<float>(image_point.y())};

  if (shape.type == ShapeType::kRect) {
    RectF& rect = shape.rect;
    if (handle.vertex < 0) {  // move whole
      const float dx = now.x - drag_original_shape_.rect.x;
      const float dy = now.y - drag_original_shape_.rect.y;
      rect = drag_original_shape_.rect;
      rect.x += dx; rect.y += dy;
      return;
    }
    // Corner drag: anchor the opposite corner.
    const RectF& original = drag_original_shape_.rect;
    const float left = original.x, top = original.y;
    const float right = original.x + original.width, bottom = original.y + original.height;
    float x1 = left, y1 = top, x2 = right, y2 = bottom;
    switch (handle.vertex) {
      case 0: x1 = now.x; y1 = now.y; break;
      case 1: x2 = now.x; y1 = now.y; break;
      case 2: x2 = now.x; y2 = now.y; break;
      case 3: x1 = now.x; y2 = now.y; break;
      default: break;
    }
    rect.x = std::min(x1, x2);
    rect.y = std::min(y1, y2);
    rect.width = std::abs(x2 - x1);
    rect.height = std::abs(y2 - y1);
    return;
  }

  auto& points = shape.points;
  if (handle.vertex < 0) {  // move whole polygon
    const float dx = now.x - drag_original_shape_.points[0].x;
    const float dy = now.y - drag_original_shape_.points[0].y;
    for (std::size_t i = 0; i < points.size(); ++i) {
      points[i].x = drag_original_shape_.points[i].x + dx;
      points[i].y = drag_original_shape_.points[i].y + dy;
    }
    return;
  }
  if (handle.vertex < static_cast<int>(points.size())) {
    points[static_cast<std::size_t>(handle.vertex)] = now;
  }
}

void ImageCanvas::drawAnnotations(QPainter& painter) {
  if (!annotations_visible_ || annotations_ == nullptr || after_.isNull()) {
    return;
  }
  const QRectF target = displayedImageRect();
  if (target.isEmpty()) {
    return;
  }

  const double sx = target.width() / static_cast<double>(after_.width());
  const double sy = target.height() / static_cast<double>(after_.height());
  const auto map_point = [&](const Point2f& point) {
    return QPointF(target.left() + static_cast<double>(point.x) * sx,
                   target.top() + static_cast<double>(point.y) * sy);
  };

  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto& items = annotations_->items();
  for (std::size_t i = 0; i < items.size(); ++i) {
    const Annotation& annotation = items[i];
    const LabelClass* label = annotations_->find_class(annotation.class_id);
    const QColor colour(label != nullptr ? QString::fromStdString(label->color)
                                         : QStringLiteral("#185FA5"));
    const bool selected = static_cast<int>(i) == selected_;
    painter.setPen(QPen(colour, selected ? 2.6 : 1.5));
    QColor fill = colour;
    fill.setAlpha(selected ? 56 : 30);
    painter.setBrush(fill);

    QRectF bounds;
    if (annotation.shape.type == ShapeType::kRect) {
      const RectF& rect = annotation.shape.rect;
      bounds = QRectF(map_point(Point2f{rect.x, rect.y}),
                      map_point(Point2f{rect.x + rect.width, rect.y + rect.height})).normalized();
      painter.drawRect(bounds);
    } else {
      QPolygonF polygon;
      for (const Point2f& point : annotation.shape.points) {
        polygon << map_point(point);
      }
      bounds = polygon.boundingRect();
      painter.drawPolygon(polygon);
    }

    const QString name = label != nullptr ? QString::fromStdString(label->name)
                                          : QStringLiteral("class %1").arg(annotation.class_id);
    QFont font = painter.font();
    font.setPointSizeF(8.0);
    painter.setFont(font);
    const int text_width = painter.fontMetrics().horizontalAdvance(name) + 10;
    const QRectF tag(bounds.left(), std::max(0.0, bounds.top() - 17.0), text_width, 16);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawRoundedRect(tag, 3, 3);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(tag, Qt::AlignCenter, name);

    if (selected) {
      painter.setBrush(QColor(255, 255, 255));
      painter.setPen(QPen(colour, 1.2));
      for (const QPointF& corner :
           {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()}) {
        painter.drawRect(QRectF(corner.x() - 2.5, corner.y() - 2.5, 5, 5));
      }
    }
  }

  // Draft shapes.
  if (drawing_rect_) {
    painter.setPen(QPen(theme::colour(theme::kAccent), 1.4, Qt::DashLine));
    painter.setBrush(QColor(0xB4, 0x56, 0x2A, 26));
    const QRectF draft(rect_start_, rect_current_);
    painter.drawRect(draft.normalized());
  } else if (!polygon_draft_.empty()) {
    painter.setPen(QPen(theme::colour(theme::kAccent), 1.4, Qt::DashLine));
    painter.setBrush(QColor(0xB4, 0x56, 0x2A, 26));
    QPolygonF polygon;
    for (const Point2f& point : polygon_draft_) {
      polygon << map_point(point);
    }
    painter.drawPolyline(polygon);
    painter.setBrush(QColor(255, 255, 255));
    for (const QPointF& point : polygon) {
      painter.drawEllipse(point, 3.0, 3.0);
    }
  }
  painter.setRenderHint(QPainter::Antialiasing, false);
}

void ImageCanvas::drawPreview(QPainter& painter) {
  if (preview_polygon_.size() < 3) return;
  const QRectF target = displayedImageRect();
  if (target.isEmpty() || after_.isNull()) return;
  const double sx = target.width() / static_cast<double>(after_.width());
  const double sy = target.height() / static_cast<double>(after_.height());
  QPolygonF polygon;
  for (const QPointF& point : preview_polygon_) {
    polygon << QPointF(target.left() + point.x() * sx, target.top() + point.y() * sy);
  }
  QColor accent = theme::colour(theme::kAccent);
  accent.setAlpha(70);
  painter.setBrush(accent);
  painter.setPen(QPen(theme::colour(theme::kAccent), 2.0, Qt::DashLine));
  painter.drawPolygon(polygon);

  // Vertex handles so the preview reads as an editable proposal.
  painter.setPen(QPen(theme::colour(theme::kAccent), 1.2));
  painter.setBrush(theme::colour(theme::kSurface));
  for (const QPointF& point : polygon) {
    painter.drawEllipse(point, 3.0, 3.0);
  }
}

void ImageCanvas::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  QLinearGradient backdrop(0, 0, 0, static_cast<qreal>(height()));
  backdrop.setColorAt(0.0, theme::colour("#3A3E39"));
  backdrop.setColorAt(1.0, theme::colour(theme::kCanvas));
  painter.fillRect(rect(), backdrop);

  if (before_.isNull() && after_.isNull()) {
    drawEmptyState(painter);
    return;
  }

  // A soft drop shadow separates the plate from the workbench without drawing
  // a hard border around it.
  const auto shadow = [&painter](const QRectF& area) {
    painter.setPen(Qt::NoPen);
    for (int i = 6; i >= 1; --i) {
      painter.setBrush(QColor(74, 58, 32, 6));
      painter.drawRoundedRect(area.adjusted(-i, -i + 3, i, i + 4), 3, 3);
    }
  };
  const auto frame = [&painter](const QRectF& area) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(kFrameBorder, 1.0));
    painter.drawRect(area.adjusted(0.5, 0.5, -0.5, -0.5));
  };

  const QRectF viewport = rect();
  QRectF drawn;

  if (mode_ == Mode::kSide) {
    const QRectF left(0, 0, viewport.width() / 2.0, viewport.height());
    const QRectF right(viewport.width() / 2.0, 0, viewport.width() / 2.0, viewport.height());
    const double base = std::min(
        fitScale(before_.size(), left.adjusted(kPadding, kPadding, -kPadding, -kPadding)),
        fitScale(after_.size(), right.adjusted(kPadding, kPadding, -kPadding, -kPadding)));
    const double scale = base * zoom_;
    const QRectF left_rect = fittedRect(before_.size(), left, scale);
    const QRectF right_rect = fittedRect(after_.size(), right, scale);

    shadow(left_rect);
    shadow(right_rect);
    painter.drawImage(left_rect, before_);
    painter.drawImage(right_rect, after_);
    frame(left_rect);
    frame(right_rect);

    painter.setPen(QPen(theme::colour("#1B2129"), 1.0));
    painter.drawLine(QPointF(viewport.center().x(), 0),
                     QPointF(viewport.center().x(), height()));
    drawn = right_rect;
  } else {
    const QRectF content = viewport.adjusted(kPadding, kPadding, -kPadding, -kPadding);
    const double scale = fitScale(after_.size(), content) * zoom_;
    drawn = fittedRect(after_.size(), viewport, scale);

    shadow(drawn);
    if (mode_ == Mode::kDifference) {
      if (!difference_image_.isNull()) {
        painter.drawImage(drawn, difference_image_);
      } else {
        painter.drawImage(drawn, before_);
      }
    } else if (mode_ == Mode::kFlicker) {
      painter.drawImage(drawn, flicker_show_after_ ? after_ : before_);
    } else {
      painter.drawImage(drawn, before_);
    }

    if (mode_ == Mode::kSplit) {
      const QRectF reveal(drawn.left() + drawn.width() * split_, drawn.top(),
                          drawn.width() * (1.0 - split_), drawn.height());
      painter.save();
      painter.setClipRect(reveal);
      painter.drawImage(drawn, after_);
      painter.restore();

      const double x = drawn.left() + drawn.width() * split_;
      // A dark hairline under a white core: visible on light and dark plates.
      painter.setPen(QPen(QColor(36, 31, 24, 90), 1.0));
      painter.drawLine(QPointF(x + 1, drawn.top()), QPointF(x + 1, drawn.bottom()));
      painter.setPen(QPen(QColor(255, 255, 255, 235), 1.4));
      painter.drawLine(QPointF(x, drawn.top()), QPointF(x, drawn.bottom()));

      const QPointF handle(x, viewport.center().y());
      painter.setPen(QPen(QColor(255, 255, 255, 210), 1.5));
      painter.setBrush(QColor(36, 31, 24, 236));
      painter.drawEllipse(handle, 12.0, 12.0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(255, 255, 255, 235));
      painter.drawPolygon(QPolygonF{handle + QPointF(-6, 0), handle + QPointF(-2, -3.4),
                                    handle + QPointF(-2, 3.4)});
      painter.drawPolygon(QPolygonF{handle + QPointF(6, 0), handle + QPointF(2, -3.4),
                                    handle + QPointF(2, 3.4)});
    } else if (mode_ == Mode::kResult) {
      painter.drawImage(drawn, after_);
    }
    frame(drawn);
  }

  drawAnnotations(painter);
  drawDetections(painter);
  drawSegments(painter);
  if (drawing_box_) {
    const QRectF box = QRectF(box_start_, box_current_).normalized();
    painter.setPen(QPen(theme::colour(theme::kAccent), 1.6, Qt::DashLine));
    painter.setBrush(QBrush());
    painter.drawRect(box);
  }
  drawPreview(painter);

  // Corner chips: dark glass, one accent dot, no drop shadow.
  const auto chip = [&painter](QPointF top_left, const QString& text, const QColor& dot,
                               const QColor& border = kChipBorder) {
    painter.setFont(theme::ui_font(10, QFont::DemiBold));
    const QFontMetricsF metrics(painter.font());
    const qreal width = metrics.horizontalAdvance(text) + 30.0;
    const QRectF box(top_left.x(), top_left.y(), width, 22.0);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(kChipFill);
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(dot);
    painter.drawEllipse(QPointF(box.left() + 10.5, box.center().y()), 2.6, 2.6);
    painter.setPen(kChipText);
    painter.drawText(box.adjusted(17, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
  };

  const QColor before_dot = theme::colour(theme::kTextFaint);
  const QColor after_dot = theme::colour(theme::kAccent);
  if (mode_ == Mode::kSide) {
    chip(QPointF(kPadding + 8, kPadding + 8), tr("原图"), before_dot);
    chip(QPointF(viewport.width() / 2.0 + kPadding + 8, kPadding + 8), tr("结果"), after_dot);
  } else if (mode_ == Mode::kSplit) {
    chip(QPointF(drawn.left() + 10, drawn.top() + 10), tr("原图"), before_dot);
    chip(QPointF(drawn.right() - 78, drawn.top() + 10), tr("结果"), after_dot);
  } else if (mode_ == Mode::kFlicker) {
    chip(QPointF(drawn.left() + 10, drawn.top() + 10),
         flicker_show_after_ ? tr("结果（闪烁）") : tr("原图（闪烁）"),
         flicker_show_after_ ? after_dot : before_dot);
  } else if (mode_ == Mode::kDifference) {
    chip(QPointF(drawn.left() + 10, drawn.top() + 10), tr("差异放大（伪彩）"), after_dot);
  } else {
    chip(QPointF(drawn.left() + 10, drawn.top() + 10), tr("结果"), after_dot);
  }

  if (!inference_label_.isEmpty() && !drawn.isEmpty()) {
    const QString text = tr("%1   %2%")
                             .arg(inference_label_)
                             .arg(inference_confidence_ * 100.0, 0, 'f', 1);
    chip(QPointF(drawn.left() + 10, drawn.bottom() - 32), text,
         theme::colour(theme::kAccent), theme::colour(theme::kAccent));
  }

  painter.setRenderHint(QPainter::Antialiasing, false);
}

void ImageCanvas::finishRect() {
  if (annotations_ == nullptr || after_.isNull()) {
    drawing_rect_ = false;
    return;
  }
  const QPointF a = toImagePointF(rect_start_);
  const QPointF b = toImagePointF(rect_current_);
  const double x = std::min(a.x(), b.x());
  const double y = std::min(a.y(), b.y());
  const double width = std::abs(a.x() - b.x());
  const double height = std::abs(a.y() - b.y());
  drawing_rect_ = false;
  if (width < 3.0 || height < 3.0) {
    update();
    return;
  }
  Annotation annotation;
  annotation.class_id = active_class_;
  annotation.shape.type = ShapeType::kRect;
  annotation.shape.rect = RectF{static_cast<float>(x), static_cast<float>(y),
                                static_cast<float>(width), static_cast<float>(height)};
  annotations_->add(annotation);
  selected_ = static_cast<int>(annotations_->size()) - 1;
  if (annotation_callback_) {
    annotation_callback_();
  }
  update();
}

void ImageCanvas::finishPolygon() {
  if (annotations_ == nullptr || polygon_draft_.size() < 3) {
    polygon_draft_.clear();
    update();
    return;
  }
  Annotation annotation;
  annotation.class_id = active_class_;
  annotation.shape.type = ShapeType::kPolygon;
  annotation.shape.points = polygon_draft_;
  annotations_->add(annotation);
  polygon_draft_.clear();
  selected_ = static_cast<int>(annotations_->size()) - 1;
  if (annotation_callback_) {
    annotation_callback_();
  }
  update();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }

  if (tool_ == Tool::kProbe && !after_.isNull()) {
    const QPointF image_point = toImagePointF(event->position());
    const int x = static_cast<int>(image_point.x()), y = static_cast<int>(image_point.y());
    if (x >= 0 && y >= 0 && x < after_.width() && y < after_.height()) {
      const QRgb rgb = after_.pixel(x, y);
      const int grey = qGray(rgb);
      if (status_callback_) {
        status_callback_(tr("取色 (%1, %2)  RGB %3, %4, %5   灰度 %6")
                             .arg(x).arg(y)
                             .arg(qRed(rgb)).arg(qGreen(rgb)).arg(qBlue(rgb)).arg(grey));
      }
    }
    return;
  }
  if (tool_ == Tool::kMeasure && !after_.isNull()) {
    const QPointF image_point = toImagePointF(event->position());
    if (!measuring_) {
      measure_start_ = image_point;
      measure_end_ = image_point;
      measuring_ = true;
    } else {
      measure_end_ = image_point;
      measuring_ = false;
      if (status_callback_) {
        const double dx = measure_end_.x() - measure_start_.x();
        const double dy = measure_end_.y() - measure_start_.y();
        status_callback_(tr("距离 %1 px  (Δx %2, Δy %3)")
                             .arg(std::hypot(dx, dy), 0, 'f', 1)
                             .arg(dx, 0, 'f', 1).arg(dy, 0, 'f', 1));
      }
    }
    update();
    return;
  }

  if (tool_ == Tool::kWand && !after_.isNull()) {
    const QPointF p = toImagePointF(event->position());
    if (p.x() >= 0 && p.y() >= 0 && p.x() < after_.width() && p.y() < after_.height()) {
      emit segmentationRequested(static_cast<int>(Tool::kWand),
                                 static_cast<int>(p.x()), static_cast<int>(p.y()), 0, 0);
    }
    return;
  }
  if (tool_ == Tool::kGrabCut && !after_.isNull()) {
    drawing_box_ = true;
    box_start_ = event->position();
    box_current_ = box_start_;
    return;
  }
  if (tool_ != Tool::kNone && !after_.isNull()) {
    if (tool_ == Tool::kRect) {
      drawing_rect_ = true;
      rect_start_ = event->position();
      rect_current_ = rect_start_;
    } else {
      const QPointF image_point = toImagePointF(event->position());
      polygon_draft_.push_back(
          Point2f{static_cast<float>(image_point.x()), static_cast<float>(image_point.y())});
      update();
    }
    return;
  }

  if (tool_ == Tool::kNone && annotations_ != nullptr && !after_.isNull()) {
    const QPointF image_point = toImagePointF(event->position());
    // First check whether the click grabbed a selected shape's handle.
    EditHandle handle = hitHandle(image_point);
    if (handle.annotation >= 0) {
      selected_ = handle.annotation;
      active_handle_ = handle;
      dragging_handle_ = true;
      drag_original_shape_ = annotations_->items()[static_cast<std::size_t>(selected_)].shape;
      if (annotation_callback_) annotation_callback_();
      update();
      return;
    }
    const int index = hitTest(image_point);
    if (index >= 0) {
      selected_ = index;
      // Allow moving a freshly selected shape by dragging immediately.
      active_handle_ = EditHandle{index, -1, false};
      dragging_handle_ = true;
      drag_original_shape_ = annotations_->items()[static_cast<std::size_t>(index)].shape;
      if (annotation_callback_) annotation_callback_();
      update();
      return;
    }
    selected_ = -1;
  }

  if (mode_ == Mode::kSplit &&
      std::abs(static_cast<double>(event->position().x()) - dividerX()) < kDividerHitWidth) {
    dragging_divider_ = true;
    return;
  }
  panning_ = true;
  last_mouse_ = event->pos();
  setCursor(Qt::ClosedHandCursor);
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  if (tool_ == Tool::kPolygon) {
    finishPolygon();
    return;
  }
  // In select mode, double-clicking a polygon edge inserts a vertex there.
  if (tool_ == Tool::kNone && annotations_ != nullptr && selected_ >= 0) {
    const QPointF image_point = toImagePointF(event->position());
    const EditHandle handle = hitHandle(image_point);
    if (handle.edge && handle.annotation == selected_) {
      auto& points = annotations_->items()[static_cast<std::size_t>(selected_)].shape.points;
      points.insert(points.begin() + handle.vertex + 1,
                    Point2f{static_cast<float>(image_point.x()),
                            static_cast<float>(image_point.y())});
      if (annotation_callback_) annotation_callback_();
      emit annotationEdited();
      update();
      return;
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (drawing_box_) {
    box_current_ = event->position();
    update();
    return;
  }
  if (dragging_handle_) {
    applyHandleDrag(active_handle_, toImagePointF(event->position()));
    update();
    return;
  }
  if (drawing_rect_) {
    rect_current_ = event->position();
    update();
    return;
  }
  if (dragging_divider_) {
    split_ = std::clamp(static_cast<double>(event->position().x()) / static_cast<double>(width()),
                        0.02, 0.98);
    update();
    if (status_callback_) {
      status_callback_(tr("分界 %1%").arg(static_cast<int>(split_ * 100.0)));
    }
    return;
  }
  if (panning_) {
    pan_ += QPointF(event->pos() - last_mouse_);
    last_mouse_ = event->pos();
    auto_fit_ = false;
    update();
  } else if (tool_ == Tool::kNone && mode_ == Mode::kSplit &&
             std::abs(static_cast<double>(event->position().x()) - dividerX()) < kDividerHitWidth) {
    setCursor(Qt::SplitHCursor);
  } else {
    setCursor(Qt::CrossCursor);
  }

  if (tool_ == Tool::kMeasure && measuring_) {
    measure_end_ = toImagePointF(event->position());
    update();
    if (status_callback_) {
      const double dx = measure_end_.x() - measure_start_.x();
      const double dy = measure_end_.y() - measure_start_.y();
      status_callback_(tr("距离 %1 px  (Δx %2, Δy %3)")
                           .arg(std::hypot(dx, dy), 0, 'f', 1)
                           .arg(dx, 0, 'f', 1).arg(dy, 0, 'f', 1));
    }
  } else if (tool_ == Tool::kProbe && !after_.isNull()) {
    const QPointF ip = toImagePointF(event->position());
    const int x = static_cast<int>(ip.x()), y = static_cast<int>(ip.y());
    if (x >= 0 && y >= 0 && x < after_.width() && y < after_.height()) {
      const QRgb rgb = after_.pixel(x, y);
      if (status_callback_) {
        status_callback_(tr("取色 (%1, %2)  RGB %3, %4, %5   灰度 %6")
                             .arg(x).arg(y).arg(qRed(rgb)).arg(qGreen(rgb))
                             .arg(qBlue(rgb)).arg(qGray(rgb)));
      }
    }
  }

  if (cursor_callback_) {
    const QPoint point = toImagePoint(event->pos());
    const bool inside = !after_.isNull() && point.x() >= 0 && point.y() >= 0 &&
                        point.x() < after_.width() && point.y() < after_.height();
    cursor_callback_(point, inside);
  }
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
  Q_UNUSED(event);
  if (drawing_box_) {
    drawing_box_ = false;
    const QPointF a = toImagePointF(box_start_);
    const QPointF b = toImagePointF(box_current_);
    const QRectF box = QRectF(a, b).normalized();
    if (box.width() >= 4 && box.height() >= 4) {
      emit segmentationRequested(static_cast<int>(Tool::kGrabCut),
                                 static_cast<int>(box.left()), static_cast<int>(box.top()),
                                 static_cast<int>(box.width()), static_cast<int>(box.height()));
    }
    update();
    return;
  }
  if (dragging_handle_) {
    dragging_handle_ = false;
    active_handle_ = EditHandle{};
    if (annotation_callback_) annotation_callback_();
    emit annotationEdited();
    setCursor(Qt::CrossCursor);
    return;
  }
  if (drawing_rect_) {
    finishRect();
    return;
  }
  dragging_divider_ = false;
  panning_ = false;
  setCursor(Qt::CrossCursor);
}

void ImageCanvas::keyPressEvent(QKeyEvent* event) {
  const int key = event->key();
  const bool ctrl = event->modifiers() & Qt::ControlModifier;

  // Tool selection (labelme / CVAT style single-key shortcuts).
  if (!ctrl && (key == Qt::Key_V || key == Qt::Key_Space)) {
    if (key == Qt::Key_Space) {
      space_panning_ = true;
      setCursor(Qt::OpenHandCursor);
    }
    setTool(Tool::kNone);
    emit toolChanged(Tool::kNone);
    return;
  }
  if (!ctrl && key == Qt::Key_R) {
    setTool(Tool::kRect); emit toolChanged(Tool::kRect); return;
  }
  if (!ctrl && key == Qt::Key_C) {
    setTool(Tool::kPolygon); emit toolChanged(Tool::kPolygon); return;
  }
  if (!ctrl && key == Qt::Key_P) {
    setTool(Tool::kProbe); emit toolChanged(Tool::kProbe); return;
  }
  if (!ctrl && key == Qt::Key_M) {
    setTool(Tool::kMeasure); emit toolChanged(Tool::kMeasure); return;
  }

  // Number keys select the first ten classes.
  if (!ctrl && key >= Qt::Key_1 && key <= Qt::Key_9) {
    emit classShortcut(key - Qt::Key_1);
    return;
  }
  if (!ctrl && key == Qt::Key_0) {
    emit classShortcut(9);
    return;
  }

  if (ctrl && key == Qt::Key_S) {
    emit saveRequested();
    return;
  }
  if (!ctrl && (key == Qt::Key_A || key == Qt::Key_Left)) {
    emit prevImageRequested();
    return;
  }
  if (!ctrl && (key == Qt::Key_D || key == Qt::Key_Right)) {
    emit nextImageRequested();
    return;
  }
  if (key == Qt::Key_Escape) {
    polygon_draft_.clear();
    drawing_rect_ = false;
    selected_ = -1;
    update();
    return;
  }
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    finishPolygon();
    return;
  }
  if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) &&
      annotations_ != nullptr && selected_ >= 0) {
    annotations_->remove_at(static_cast<std::size_t>(selected_));
    selected_ = -1;
    if (annotation_callback_) annotation_callback_();
    emit annotationEdited();
    update();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ImageCanvas::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space) {
    space_panning_ = false;
    setCursor(tool_ == Tool::kNone ? Qt::CrossCursor : Qt::CrossCursor);
  }
  QWidget::keyReleaseEvent(event);
}

void ImageCanvas::wheelEvent(QWheelEvent* event) {
  const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
  if (steps == 0.0) {
    return;
  }
  const double factor = std::pow(1.18, steps);
  zoom_ = std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
  auto_fit_ = false;
  update();
  if (zoom_callback_) {
    zoom_callback_(zoom_);
  }
  if (status_callback_) {
    status_callback_(tr("缩放 %1%").arg(static_cast<int>(zoom_ * 100.0)));
  }
}

void ImageCanvas::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update();
}

}  // namespace figtools::gui
