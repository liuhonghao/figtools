// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Small controls that Qt does not provide with the right look: a key/value
// readout with tabular figures, and a switch used to enable a pipeline step.

#ifndef FIGTOOLS_GUI_WIDGETS_HPP_
#define FIGTOOLS_GUI_WIDGETS_HPP_

#include <QAbstractButton>
#include <QString>
#include <QWidget>

#include <utility>
#include <vector>

class QGridLayout;
class QVariantAnimation;

namespace figtools::gui {

// Two-column readout: muted key, right-aligned monospaced value. Rebuilt in
// place rather than through rich text so the baselines actually line up.
class KeyValueList : public QWidget {
  Q_OBJECT

 public:
  explicit KeyValueList(QWidget* parent = nullptr);

  void set_rows(const std::vector<std::pair<QString, QString>>& rows);
  void clear();

 private:
  QGridLayout* grid_ = nullptr;
};

// Brand mark: a viewfinder frame around a lens. Drawn rather than loaded from
// an SVG so it can follow the live accent colour and stay sharp at 16–32 px.
class BrandMark : public QWidget {
 public:
  explicit BrandMark(QWidget* parent = nullptr);

  [[nodiscard]] QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
};

// Compact on/off switch. A checkbox reads as "form"; this reads as a control.
class ToggleSwitch : public QAbstractButton {
  Q_OBJECT

 public:
  explicit ToggleSwitch(QWidget* parent = nullptr);

  [[nodiscard]] QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  void animate_to(bool checked);

  qreal position_ = 0.0;
  bool hovered_ = false;
  QVariantAnimation* animation_ = nullptr;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_WIDGETS_HPP_
