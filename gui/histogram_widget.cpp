// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "histogram_widget.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

#include "theme.hpp"

namespace figtools::gui {
namespace {

constexpr int kBins = 256;

QPainterPath buildPath(const std::array<double, kBins>& histogram, const QRectF& area) {
  QPainterPath path;
  path.moveTo(area.left(), area.bottom());
  for (int i = 0; i < kBins; ++i) {
    const double x = area.left() + area.width() * static_cast<double>(i) / (kBins - 1);
    const double ratio = std::clamp(histogram[i], 0.0, 1.0);
    const double y = area.bottom() - ratio * area.height();
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
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  painter.setPen(QPen(theme::colour(theme::kLine), 1.0));
  painter.setBrush(theme::colour("#FBF6EC"));
  painter.drawRoundedRect(box, 5, 5);

  const QRectF plot = box.adjusted(10, 10, -10, -8);

  painter.setPen(QPen(theme::colour("#EDE4D3"), 1.0));
  for (int i = 1; i < 4; ++i) {
    const double y = plot.top() + plot.height() * static_cast<double>(i) / 4.0;
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
  }

  if (image_.isNull()) {
    painter.setPen(theme::colour(theme::kTextFaint));
    painter.setFont(theme::ui_font(11));
    painter.drawText(plot, Qt::AlignCenter, tr("无数据"));
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

  // A saturated background (paper, sky, a white wall) can own half the pixels
  // and flatten every other bin to zero. A square-root curve with a short
  // moving average keeps the shape readable without inventing data.
  const QColor colours[3] = {QColor(0xA9, 0x50, 0x3C), QColor(0x6B, 0x7F, 0x4A),
                             QColor(0x3F, 0x6E, 0x8C)};
  std::array<std::array<double, kBins>, 3> curves{};
  double peak = 0.0;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    for (int i = 0; i < kBins; ++i) {
      const int lo = std::max(0, i - 8);
      const int hi = std::min(kBins - 1, i + 8);
      double average = 0.0;
      for (int k = lo; k <= hi; ++k) {
        average += static_cast<double>(histograms[channel][static_cast<std::size_t>(k)]);
      }
      average /= static_cast<double>(hi - lo + 1);
      const double value = std::sqrt(average);
      curves[channel][static_cast<std::size_t>(i)] = value;
      peak = std::max(peak, value);
    }
  }
  const double scale = peak > 0.0 ? 1.0 / peak : 0.0;
  for (auto& channel : curves) {
    for (double& value : channel) {
      value *= scale;
    }
  }

  for (std::size_t channel = 0; channel < 3; ++channel) {
    QColor fill = colours[channel];
    fill.setAlpha(42);
    painter.setPen(QPen(colours[channel], 1.0));
    painter.setBrush(fill);
    painter.drawPath(buildPath(curves[channel], plot));
  }

  painter.setPen(QPen(theme::colour("#E0D5C1"), 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
  painter.setRenderHint(QPainter::Antialiasing, false);
}

}  // namespace figtools::gui
