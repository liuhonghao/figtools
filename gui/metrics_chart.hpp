// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Lightweight training-curve widget with dual Y axes: loss on the left, score
// on the right. Pure QWidget paintEvent, no Qt Charts dependency. Each series
// uses its own real value range, so the plotted lines and the axis labels agree
// (a single normalised axis would make the numbers misleading).

#ifndef FIGTOOLS_GUI_METRICS_CHART_HPP_
#define FIGTOOLS_GUI_METRICS_CHART_HPP_

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPolygonF>
#include <QWidget>

#include <algorithm>
#include <vector>

#include "theme.hpp"

namespace figtools::gui {

class MetricsChart : public QWidget {
 public:
  struct Point {
    double epoch = 0.0;
    double loss = 0.0;
    double score = 0.0;
  };

  explicit MetricsChart(QWidget* parent = nullptr) : QWidget(parent) {
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void setData(std::vector<Point> data) {
    data_ = std::move(data);
    update();
  }
  void clear() {
    data_.clear();
    update();
  }
  void setLossLabel(const QString& label) { loss_label_ = label; }
  void setScoreLabel(const QString& label) { score_label_ = label; }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme::colour(theme::kSurface));

    const QColor grid = theme::colour(theme::kLine);
    const QColor text = theme::colour(theme::kTextFaint);
    const QColor loss_colour = theme::colour(theme::kAccent);
    const QColor score_colour = theme::colour(theme::kInfo);
    const int left = 46, right = 46, top = 24, bottom = 28;
    const QRect area = rect().adjusted(left, top, -right, -bottom);
    if (area.width() < 20 || area.height() < 20) {
      return;
    }

    if (data_.empty()) {
      painter.setPen(grid);
      for (int i = 0; i <= 4; ++i) {
        const int y = area.bottom() - static_cast<int>(area.height() * i / 4.0);
        painter.drawLine(area.left(), y, area.right(), y);
      }
      painter.setPen(text);
      painter.drawText(area, Qt::AlignCenter, tr("训练开始后显示 loss / 指标曲线"));
      return;
    }

    // Independent real ranges for each series.
    double loss_min = data_.front().loss, loss_max = data_.front().loss;
    double score_min = data_.front().score, score_max = data_.front().score;
    double epoch_max = 1.0;
    for (const Point& p : data_) {
      loss_min = std::min(loss_min, p.loss);
      loss_max = std::max(loss_max, p.loss);
      score_min = std::min(score_min, p.score);
      score_max = std::max(score_max, p.score);
      epoch_max = std::max(epoch_max, p.epoch);
    }
    // Leave a little headroom so lines do not hug the frame.
    const double loss_pad = std::max(0.01, (loss_max - loss_min) * 0.08);
    const double score_pad = std::max(0.01, (score_max - score_min) * 0.08);
    loss_min -= loss_pad;
    loss_max += loss_pad;
    score_min = std::max(0.0, score_min - score_pad);
    score_max += score_pad;
    const double loss_span = std::max(1e-6, loss_max - loss_min);
    const double score_span = std::max(1e-6, score_max - score_min);

    const auto x_of = [&](double epoch) {
      return area.left() + area.width() * (epoch - 1.0) / std::max(1.0, epoch_max - 1.0);
    };
    const auto loss_y = [&](double value) {
      return area.bottom() - area.height() * (value - loss_min) / loss_span;
    };
    const auto score_y = [&](double value) {
      return area.bottom() - area.height() * (value - score_min) / score_span;
    };

    // Grid lines + both axis labels, at the same 5 levels.
    const QFontMetrics metrics(painter.font());
    for (int i = 0; i <= 4; ++i) {
      const double t = static_cast<double>(i) / 4.0;
      const int y = area.bottom() - static_cast<int>(area.height() * t);
      painter.setPen(grid);
      painter.drawLine(area.left(), y, area.right(), y);
      const QString loss_text = QString::number(loss_min + t * loss_span, 'f', 1);
      const QString score_text = QString::number(score_min + t * score_span, 'f', 2);
      painter.setPen(loss_colour);
      painter.drawText(QRect(2, y - metrics.height() / 2, left - 6, metrics.height()),
                       Qt::AlignRight | Qt::AlignVCenter, loss_text);
      painter.setPen(score_colour);
      painter.drawText(QRect(area.right() + 6, y - metrics.height() / 2,
                             right - 8, metrics.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, score_text);
    }

    QPolygonF loss_poly, score_poly;
    for (const Point& p : data_) {
      loss_poly << QPointF(x_of(p.epoch), loss_y(p.loss));
      score_poly << QPointF(x_of(p.epoch), score_y(p.score));
    }
    painter.setPen(QPen(loss_colour, 2.0));
    painter.drawPolyline(loss_poly);
    painter.setPen(QPen(score_colour, 2.0));
    painter.drawPolyline(score_poly);
    for (int i = 0; i < static_cast<int>(data_.size()); ++i) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(loss_colour);
      painter.drawEllipse(loss_poly[i], 2.8, 2.8);
      painter.setBrush(score_colour);
      painter.drawEllipse(score_poly[i], 2.8, 2.8);
    }

    // Legend with real series names.
    int legend_x = area.left() + 6;
    painter.setPen(text);
    painter.drawText(legend_x, 14, loss_label_);
    legend_x += metrics.horizontalAdvance(loss_label_) + 14;
    painter.drawText(legend_x, 14, score_label_);
    painter.drawText(QRect(area.left(), rect().bottom() - bottom + 2,
                           area.width(), bottom - 4),
                     Qt::AlignCenter, tr("训练轮次 (Epoch)"));
  }

 private:
  std::vector<Point> data_;
  QString loss_label_ = QStringLiteral("loss");
  QString score_label_ = QStringLiteral("score");
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_METRICS_CHART_HPP_
