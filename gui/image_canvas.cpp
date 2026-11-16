// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "image_canvas.hpp"

#include "theme.hpp"

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
}

void ImageCanvas::setImages(const QImage& before, const QImage& after) {
  const bool first = before_.isNull();
  before_ = before;
  after_ = after;
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
    painter.drawImage(drawn, before_);

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
    } else {
      painter.drawImage(drawn, after_);
    }
    frame(drawn);
  }

  drawAnnotations(painter);
  drawDetections(painter);

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

  if (annotations_ != nullptr && !after_.isNull()) {
    const int index = hitTest(toImagePointF(event->position()));
    if (index >= 0) {
      selected_ = index;
      if (annotation_callback_) {
        annotation_callback_();
      }
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
  if (tool_ == Tool::kPolygon && event->button() == Qt::LeftButton) {
    finishPolygon();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
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

  if (cursor_callback_) {
    const QPoint point = toImagePoint(event->pos());
    const bool inside = !after_.isNull() && point.x() >= 0 && point.y() >= 0 &&
                        point.x() < after_.width() && point.y() < after_.height();
    cursor_callback_(point, inside);
  }
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
  Q_UNUSED(event);
  if (drawing_rect_) {
    finishRect();
    return;
  }
  dragging_divider_ = false;
  panning_ = false;
  setCursor(Qt::CrossCursor);
}

void ImageCanvas::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    polygon_draft_.clear();
    drawing_rect_ = false;
    selected_ = -1;
    update();
    return;
  }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    finishPolygon();
    return;
  }
  if (event->key() == Qt::Key_Delete && annotations_ != nullptr && selected_ >= 0) {
    annotations_->remove_at(static_cast<std::size_t>(selected_));
    selected_ = -1;
    if (annotation_callback_) {
      annotation_callback_();
    }
    update();
    return;
  }
  QWidget::keyPressEvent(event);
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
