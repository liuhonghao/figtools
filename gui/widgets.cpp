// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "widgets.hpp"

#include <QEnterEvent>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QVariantAnimation>

#include "theme.hpp"

namespace figtools::gui {

// ---------------------------------------------------------------------------
// KeyValueList
// ---------------------------------------------------------------------------

KeyValueList::KeyValueList(QWidget* parent) : QWidget(parent) {
  grid_ = new QGridLayout(this);
  grid_->setContentsMargins(0, 0, 0, 0);
  grid_->setHorizontalSpacing(12);
  grid_->setVerticalSpacing(0);
  grid_->setColumnStretch(0, 0);
  grid_->setColumnStretch(1, 1);
}

void KeyValueList::clear() {
  while (grid_->count() > 0) {
    QLayoutItem* item = grid_->takeAt(0);
    if (item->widget() != nullptr) {
      item->widget()->deleteLater();
    }
    delete item;
  }
}

void KeyValueList::set_rows(const std::vector<std::pair<QString, QString>>& rows) {
  clear();
  int row = 0;
  for (const auto& [key, value] : rows) {
    auto* key_label = new QLabel(key, this);
    key_label->setObjectName(QStringLiteral("KvKey"));
    key_label->setFont(theme::ui_font(11));
    key_label->setMinimumHeight(20);

    auto* value_label = new QLabel(value, this);
    value_label->setObjectName(QStringLiteral("KvValue"));
    value_label->setFont(theme::mono_font(11));
    value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    value_label->setMinimumHeight(20);

    grid_->addWidget(key_label, row, 0);
    grid_->addWidget(value_label, row, 1);
    ++row;
  }
}

// ---------------------------------------------------------------------------
// ToggleSwitch
// ---------------------------------------------------------------------------

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_Hover, true);

  animation_ = new QVariantAnimation(this);
  animation_->setDuration(130);
  animation_->setEasingCurve(QEasingCurve::OutCubic);
  connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    position_ = value.toReal();
    update();
  });
  connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::animate_to);
}

QSize ToggleSwitch::sizeHint() const { return QSize(30, 16); }

void ToggleSwitch::animate_to(bool checked) {
  animation_->stop();
  animation_->setStartValue(position_);
  animation_->setEndValue(checked ? 1.0 : 0.0);
  animation_->start();
}

void ToggleSwitch::enterEvent(QEnterEvent* event) {
  hovered_ = true;
  update();
  QAbstractButton::enterEvent(event);
}

void ToggleSwitch::leaveEvent(QEvent* event) {
  hovered_ = false;
  update();
  QAbstractButton::leaveEvent(event);
}

void ToggleSwitch::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const qreal height = 16.0;
  const qreal width = 30.0;
  const QRectF track(0.5, 0.5, width - 1.0, height - 1.0);
  const QColor on = theme::colour(hovered_ ? "#C3602F" : theme::kAccent);
  const QColor off = theme::colour(hovered_ ? "#CDBFA6" : "#D9CEBA");

  QColor fill = off;
  fill.setRedF(off.redF() + (on.redF() - off.redF()) * position_);
  fill.setGreenF(off.greenF() + (on.greenF() - off.greenF()) * position_);
  fill.setBlueF(off.blueF() + (on.blueF() - off.blueF()) * position_);

  painter.setPen(QPen(theme::colour(position_ > 0.5 ? "#9C4720" : "#C9BBA2"), 1.0));
  painter.setBrush(fill);
  painter.drawRoundedRect(track, height / 2.0 - 0.5, height / 2.0 - 0.5);

  const qreal knob_radius = 5.6;
  const qreal knob_x = 3.0 + knob_radius + (width - 6.0 - knob_radius * 2.0) * position_;
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(255, 255, 255, 250));
  painter.drawEllipse(QPointF(knob_x, height / 2.0), knob_radius, knob_radius);
}

}  // namespace figtools::gui
