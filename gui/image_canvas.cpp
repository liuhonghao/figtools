// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "image_canvas.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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

QColor kBackground{0xEF, 0xEC, 0xE2};
QColor kPanel{0x2A, 0x2A, 0x26};
QColor kAccent{0x18, 0x5F, 0xA5};

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
  const double base = fitScale(after_.size(), target.adjusted(20, 20, -20, -20));
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
    const double base = fitScale(after_.size(), viewport.adjusted(20, 20, -20, -20));
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
  const double base = fitScale(before_.size(), left.adjusted(20, 20, -20, -20));
  const double scale = base * zoom_;
  const QRectF drawn = fittedRect(before_.size(), left, scale);
  if (drawn.width() <= 0.0) {
    return {};
  }
  const double fx = (static_cast<double>(widget_point.x()) - drawn.left()) / drawn.width();
  const double fy = (static_cast<double>(widget_point.y()) - drawn.top()) / drawn.height();
  return QPoint(static_cast<int>(fx * before_.width()), static_cast<int>(fy * before_.height()));
}

void ImageCanvas::drawEmptyState(QPainter& painter) {
  painter.setPen(QColor(0x8A, 0x8A, 0x7E));
  QFont font = painter.font();
  font.setPointSizeF(font.pointSizeF() + 1.0);
  painter.setFont(font);
  painter.drawText(rect(), Qt::AlignCenter,
                   tr("拖入图像，或点击「示例图」开始\n\n"
                      "拖动画布中间的白色分界线可对比处理前后"));
}

void ImageCanvas::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.fillRect(rect(), kBackground);

  if (before_.isNull() && after_.isNull()) {
    drawEmptyState(painter);
    return;
  }

  const QRectF viewport = rect();
  const QRectF content = viewport.adjusted(20, 20, -20, -20);

  if (mode_ == Mode::kSide) {
    const QRectF left(0, 0, viewport.width() / 2.0, viewport.height());
    const QRectF right(viewport.width() / 2.0, 0, viewport.width() / 2.0, viewport.height());
    const double base = std::min(fitScale(before_.size(), left.adjusted(20, 20, -20, -20)),
                                 fitScale(after_.size(), right.adjusted(20, 20, -20, -20)));
    const double scale = base * zoom_;
    painter.drawImage(fittedRect(before_.size(), left, scale), before_);
    painter.drawImage(fittedRect(after_.size(), right, scale), after_);
    painter.setPen(QColor(255, 255, 255, 180));
    painter.drawLine(QPointF(viewport.center().x(), 0), QPointF(viewport.center().x(), height()));
  } else {
    const double base = fitScale(after_.size(), content);
    const double scale = base * zoom_;
    const QRectF drawn = fittedRect(after_.size(), viewport, scale);

    painter.drawImage(drawn, before_);

    if (mode_ == Mode::kSplit) {
      const QRectF reveal(drawn.left() + drawn.width() * split_, drawn.top(),
                          drawn.width() * (1.0 - split_), drawn.height());
      painter.save();
      painter.setClipRect(reveal);
      painter.drawImage(drawn, after_);
      painter.restore();

      // Divider.
      const double x = dividerX();
      painter.setPen(QPen(QColor(255, 255, 255, 230), 2.0));
      painter.drawLine(QPointF(x, 0), QPointF(x, height()));

      const QPointF handle(x, viewport.center().y());
      painter.setRenderHint(QPainter::Antialiasing, true);
      painter.setBrush(QColor(255, 255, 255, 245));
      painter.setPen(QPen(QColor(0xD2, 0xCE, 0xBD), 1.0));
      painter.drawEllipse(handle, 13.0, 13.0);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(0x8A, 0x8A, 0x7E));
      const QPointF centre = handle;
      painter.drawPolygon(QPolygonF{centre + QPointF(-7, 0), centre + QPointF(-2.5, -4),
                                    centre + QPointF(-2.5, 4)});
      painter.drawPolygon(QPolygonF{centre + QPointF(7, 0), centre + QPointF(2.5, -4),
                                    centre + QPointF(2.5, 4)});
      painter.setRenderHint(QPainter::Antialiasing, false);
    } else {
      painter.drawImage(drawn, after_);
    }
  }

  // Corner badges.
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto badge = [&painter](const QRectF& area, const QString& text, const QColor& accent) {
    QFont font = painter.font();
    font.setPointSizeF(8.5);
    painter.setFont(font);
    const QRectF box(area.left(), area.top(), painter.fontMetrics().horizontalAdvance(text) + 18, 20);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawRoundedRect(box, 4, 4);
    painter.setPen(accent);
    painter.drawText(box.adjusted(9, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
  };
  if (mode_ == Mode::kSide) {
    badge(QRectF(12, 12, 0, 0), tr("原图"), QColor(0x5F, 0x5E, 0x5A));
    badge(QRectF(viewport.width() / 2.0 + 12, 12, 0, 0), tr("结果"), kAccent);
  } else {
    badge(QRectF(12, 12, 0, 0), tr("原图"), QColor(0x5F, 0x5E, 0x5A));
    badge(QRectF(viewport.width() - 92, 12, 0, 0), tr("结果"), kAccent);
  }
  painter.setRenderHint(QPainter::Antialiasing, false);
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (mode_ == Mode::kSplit &&
        std::abs(static_cast<double>(event->position().x()) - dividerX()) < kDividerHitWidth) {
      dragging_divider_ = true;
      return;
    }
    panning_ = true;
    last_mouse_ = event->pos();
    setCursor(Qt::ClosedHandCursor);
  }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
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
  } else if (mode_ == Mode::kSplit &&
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
  dragging_divider_ = false;
  panning_ = false;
  setCursor(Qt::CrossCursor);
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
