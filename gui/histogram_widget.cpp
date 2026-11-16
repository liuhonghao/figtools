// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "histogram_widget.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <array>

namespace figtools::gui {
namespace {

constexpr int kBins = 256;

QPainterPath buildPath(const std::array<int, kBins>& histogram, double scale, const QRectF& area) {
  QPainterPath path;
  path.moveTo(area.left(), area.bottom());
  for (int i = 0; i < kBins; ++i) {
    const double x = area.left() + area.width() * static_cast<double>(i) / (kBins - 1);
    const double y = area.bottom() - static_cast<double>(histogram[i]) * scale * area.height();
    path.lineTo(x, y);
  }
  path.lineTo(area.right(), area.bottom());
  path.closeSubpath();
  return path;
}

}  // namespace

HistogramWidget::HistogramWidget(QWidget* parent) : QWidget(parent) {
  setMinimumHeight(96);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void HistogramWidget::setImage(const QImage& image) {
  image_ = image;
  update();
}

void HistogramWidget::clear() {
  image_ = QImage();
  update();
}

void HistogramWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), Qt::white);
  painter.setPen(QColor(0xEE, 0xEB, 0xDF));
  for (int i = 1; i < 4; ++i) {
    const int y = height() * i / 4;
    painter.drawLine(0, y, width(), y);
  }

  if (image_.isNull()) {
    painter.setPen(QColor(0x8A, 0x8A, 0x7E));
    painter.drawText(rect(), Qt::AlignCenter, tr("无数据"));
    return;
  }

  const QImage rgb = image_.convertToFormat(QImage::Format_RGB32);
  std::array<std::array<int, kBins>, 3> histograms{};
  for (int y = 0; y < rgb.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
    for (int x = 0; x < rgb.width(); ++x) {
      const QRgb pixel = line[x];
      ++histograms[0][static_cast<std::size_t>(qRed(pixel))];
      ++histograms[1][static_cast<std::size_t>(qGreen(pixel))];
      ++histograms[2][static_cast<std::size_t>(qBlue(pixel))];
    }
  }

  int peak = 1;
  for (const auto& channel : histograms) {
    for (const int value : channel) {
      peak = std::max(peak, value);
    }
  }
  const double scale = 1.0 / static_cast<double>(peak);
  const QRectF area = QRectF(rect()).adjusted(6, 8, -6, -6);

  const QColor colours[3] = {QColor(0xD4, 0x53, 0x7E), QColor(0x3B, 0x6D, 0x11),
                             QColor(0x18, 0x5F, 0xA5)};
  painter.setRenderHint(QPainter::Antialiasing, true);
  for (int channel = 0; channel < 3; ++channel) {
    QColor fill = colours[channel];
    fill.setAlpha(38);
    painter.setPen(QPen(colours[channel], 1.2));
    painter.setBrush(fill);
    painter.drawPath(buildPath(histograms[static_cast<std::size_t>(channel)], scale, area));
  }
  painter.setRenderHint(QPainter::Antialiasing, false);
}

}  // namespace figtools::gui
