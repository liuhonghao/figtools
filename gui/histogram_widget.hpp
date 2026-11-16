// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#ifndef FIGTOOLS_GUI_HISTOGRAM_WIDGET_HPP_
#define FIGTOOLS_GUI_HISTOGRAM_WIDGET_HPP_

#include <QImage>
#include <QWidget>

class QPaintEvent;

namespace figtools::gui {

// RGB histogram of the processed result. Before/after comparison lives on the
// canvas; this panel answers "did the tonal range survive the pipeline".
class HistogramWidget : public QWidget {
  Q_OBJECT

 public:
  explicit HistogramWidget(QWidget* parent = nullptr);

  void setImage(const QImage& image);
  void clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QImage image_;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_HISTOGRAM_WIDGET_HPP_
