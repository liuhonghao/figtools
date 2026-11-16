// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "theme.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QWidget>

namespace figtools::gui::theme {
namespace {

const char* kSheet = R"(
QMainWindow { background: #F0EADC; }
QWidget { color: #241F18; selection-background-color: #F1D8C6; selection-color: #241F18; }

QToolTip {
  background: #2B2620; color: #F7F2E6; border: 1px solid #2B2620; padding: 4px 7px;
}

/* ---- menu ------------------------------------------------------------- */
QMenuBar { background: #FBF8F2; color: #6D6355; padding: 2px 6px; border: none; }
QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 3px; }
QMenuBar::item:selected { background: #EFE8D9; color: #241F18; }
QMenu { background: #FFFCF6; border: 1px solid #DCD1BC; padding: 5px; }
QMenu::item { padding: 6px 28px 6px 12px; border-radius: 3px; color: #3A342B; }
QMenu::item:selected { background: #F4E2D4; color: #7A3A16; }
QMenu::item:disabled { color: #B5AA98; }
QMenu::separator { height: 1px; background: #EDE4D3; margin: 5px 8px; }
QMenu::section { color: #94897A; padding: 6px 12px 3px 12px; }

/* ---- top bar ---------------------------------------------------------- */
QWidget#TopBar { background: #FBF8F2; border-bottom: 1px solid #E4DAC6; }
QLabel#Brand { color: #241F18; font-size: 13px; font-weight: 600; }
QLabel#BrandSub { color: #94897A; font-size: 11px; }
QLabel#TopMeta { color: #94897A; font-size: 11px; }

/* ---- panels ----------------------------------------------------------- */
QFrame#PanelLeft { background: #FBF8F2; border-right: 1px solid #E4DAC6; }
QFrame#PanelRight { background: #FBF8F2; border-left: 1px solid #E4DAC6; }
QFrame#Card { background: #FFFCF6; border: 1px solid #E4DAC6; border-radius: 6px; }
QFrame#Card[state="off"] { background: #F7F2E6; border-color: #EBE2D0; }
QFrame#Hairline { background: #EDE4D3; border: none; }
QLabel#CardIndex { color: #B5AA98; font-size: 10px; }
QLabel#CardTitle { color: #241F18; font-size: 12px; }
QLabel#CardTitle[state="off"] { color: #94897A; }
QLabel#ParamLabel { color: #6D6355; font-size: 11px; }
QLabel#Section { color: #94897A; font-size: 10px; }
QLabel#Meta { color: #94897A; font-size: 11px; }
QLabel#MetaStrong { color: #4A4237; font-size: 11px; }
QLabel#KvKey { color: #6D6355; font-size: 11px; }
QLabel#KvValue { color: #2B2620; font-size: 11px; }
QLabel#Hint { color: #94897A; font-size: 11px; }

/* ---- buttons ---------------------------------------------------------- */
QPushButton {
  background: #FFFCF6; color: #3A342B; border: 1px solid #DCD1BC;
  border-radius: 4px; padding: 5px 12px;
}
QPushButton:hover { background: #F7F1E4; border-color: #C9BBA2; }
QPushButton:pressed { background: #EFE8D9; }
QPushButton:checked { background: #F4E2D4; border-color: #C98A5E; color: #7A3A16; }
QPushButton:disabled { background: #F7F2E6; border-color: #E8DFCE; color: #B5AA98; }
QPushButton#Primary {
  background: #B4562A; border-color: #A04A23; color: #FFFFFF; font-weight: 600;
}
QPushButton#Primary:hover { background: #C05F31; }
QPushButton#Primary:pressed { background: #9C4720; }
QPushButton#Quiet { background: transparent; border-color: transparent; color: #6D6355; }
QPushButton#Quiet:hover { background: #F0E9DB; color: #241F18; }

QToolButton {
  background: transparent; color: #6D6355; border: 1px solid transparent;
  border-radius: 4px; padding: 4px 7px;
}
QToolButton:hover { background: #F1EADD; color: #241F18; }
QToolButton:pressed { background: #E9E0CF; }
QToolButton::menu-indicator { image: none; width: 0; height: 0; }
QToolButton:checked { background: #F4E2D4; border-color: #DDAE8B; color: #7A3A16; }

QFrame#Segmented { background: #F2EBDD; border: 1px solid #E4DAC6; border-radius: 5px; }
QFrame#Segmented QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 5px 12px; color: #7A7062; }
QFrame#Segmented QToolButton:hover { color: #241F18; background: #F9F4E8; }
QFrame#Segmented QToolButton:checked { background: #FFFFFF; border-color: #DFD4BE; color: #241F18; }

/* ---- inputs ----------------------------------------------------------- */
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
  background: #FFFFFF; border: 1px solid #DCD1BC; border-radius: 4px;
  padding: 3px 6px; color: #241F18; min-height: 19px;
}
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: #C4B69C; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus { border-color: #B4562A; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView {
  background: #FFFCF6; border: 1px solid #DCD1BC; outline: none;
  selection-background-color: #F4E2D4; selection-color: #7A3A16;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
  subcontrol-origin: border; subcontrol-position: top right; width: 14px;
  background: #F7F2E6; border-left: 1px solid #E4DAC6; border-top-right-radius: 4px;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
  subcontrol-origin: border; subcontrol-position: bottom right; width: 14px;
  background: #F7F2E6; border-left: 1px solid #E4DAC6; border-bottom-right-radius: 4px;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: #EFE8D9; }

QDoubleSpinBox#Value, QSpinBox#Value {
  background: transparent; border: 1px solid transparent; color: #4A4237;
  padding: 1px 1px 1px 0;
}
QDoubleSpinBox#Value:hover, QSpinBox#Value:hover {
  background: #FFFFFF; border-color: #DFD4BE; color: #241F18;
}
QDoubleSpinBox#Value:focus, QSpinBox#Value:focus {
  background: #FFFFFF; border-color: #B4562A; color: #241F18;
}

/* ---- sliders ---------------------------------------------------------- */
QSlider::groove:horizontal { height: 3px; background: #E0D5C1; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #B4562A; border-radius: 2px; }
QSlider::handle:horizontal {
  background: #FFFFFF; border: 2px solid #B4562A;
  width: 9px; height: 9px; margin: -5px 0; border-radius: 6px;
}
QSlider::handle:horizontal:hover { border-color: #8F3F1B; }
QSlider::handle:horizontal:pressed { background: #B4562A; }

/* ---- tabs ------------------------------------------------------------- */
QTabWidget::pane { border: none; background: transparent; }
QTabBar { background: transparent; }
QTabBar::tab {
  background: transparent; color: #94897A; padding: 9px 13px; margin-right: 2px;
  border: none; border-bottom: 2px solid transparent;
}
QTabBar::tab:hover { color: #4A4237; }
QTabBar::tab:selected { color: #241F18; border-bottom: 2px solid #B4562A; }

/* ---- lists ------------------------------------------------------------ */
QListWidget, QListView {
  background: #FFFFFF; border: 1px solid #E4DAC6; border-radius: 5px; padding: 3px;
  outline: none;
}
QListWidget::item { padding: 5px 7px; border-radius: 3px; color: #3A342B; }
QListWidget::item:hover { background: #F7F1E4; }
QListWidget::item:selected { background: #F4E2D4; color: #7A3A16; }

/* ---- scrollbars ------------------------------------------------------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px 1px; }
QScrollBar::handle:vertical { background: #D8CCB6; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #C6B79C; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 1px 2px; }
QScrollBar::handle:horizontal { background: #D8CCB6; border-radius: 4px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: #C6B79C; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- misc ------------------------------------------------------------- */
QCheckBox { spacing: 6px; color: #3A342B; }
QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #CFC3AC; border-radius: 3px; background: #FFFFFF; }
QCheckBox::indicator:hover { border-color: #B4562A; }
QCheckBox::indicator:checked { background: #B4562A; border-color: #B4562A; }

QProgressBar { background: #E4DAC6; border: none; border-radius: 2px; height: 3px; }
QProgressBar::chunk { background: #B4562A; border-radius: 2px; }
QProgressBar#Status { background: #E4DAC6; max-height: 3px; }
QProgressBar#BatchProgress { background: #EDE4D3; border: none; border-radius: 3px;
                             min-height: 6px; max-height: 6px; }
QProgressBar#BatchProgress::chunk { background: #B4562A; border-radius: 3px; }

QSplitter::handle { background: #E4DAC6; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:hover { background: #C08A66; }

QStatusBar { background: #F7F2E6; border-top: 1px solid #E4DAC6; color: #94897A; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #94897A; font-size: 11px; }
QStatusBar QLabel#StatusValue { color: #4A4237; font-size: 11px; }
QStatusBar QLabel#StatusMessage { color: #94897A; font-size: 11px; }
)";

QString mono_family() {
  static const QString family = []() {
    const QStringList preferred{QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                                QStringLiteral("JetBrains Mono"), QStringLiteral("Menlo"),
                                QStringLiteral("DejaVu Sans Mono")};
    const QStringList installed = QFontDatabase::families();
    for (const QString& candidate : preferred) {
      if (installed.contains(candidate)) {
        return candidate;
      }
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
  }();
  return family;
}

void set_color(QPalette& palette, QPalette::ColorRole role, const char* hex) {
  palette.setColor(role, QColor(hex));
}

}  // namespace

QColor colour(const char* hex) { return QColor(QString::fromLatin1(hex)); }

QFont ui_font(int pixel_size, int weight) {
  QFont font(QStringLiteral("Microsoft YaHei UI"));
  if (!QFontDatabase::families().contains(font.family())) {
    font = QApplication::font();
  }
  font.setPixelSize(pixel_size);
  font.setWeight(static_cast<QFont::Weight>(weight));
  font.setStyleStrategy(QFont::PreferAntialias);
  return font;
}

QFont mono_font(int pixel_size, int weight) {
  QFont font(mono_family());
  font.setPixelSize(pixel_size);
  font.setWeight(static_cast<QFont::Weight>(weight));
  font.setStyleHint(QFont::Monospace);
  return font;
}

void apply(QApplication& app) {
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

  QPalette palette;
  set_color(palette, QPalette::Window, kBase);
  set_color(palette, QPalette::WindowText, kText);
  set_color(palette, QPalette::Base, kInput);
  set_color(palette, QPalette::AlternateBase, "#FBF7EE");
  set_color(palette, QPalette::ToolTipBase, "#2B2620");
  set_color(palette, QPalette::ToolTipText, "#F7F2E6");
  set_color(palette, QPalette::Text, kText);
  set_color(palette, QPalette::Button, "#FFFCF6");
  set_color(palette, QPalette::ButtonText, "#3A342B");
  set_color(palette, QPalette::BrightText, "#FFFFFF");
  set_color(palette, QPalette::Highlight, "#B4562A");
  set_color(palette, QPalette::HighlightedText, "#FFFFFF");
  set_color(palette, QPalette::Link, kAccent);
  set_color(palette, QPalette::PlaceholderText, "#B5AA98");
  set_color(palette, QPalette::Light, "#FFFFFF");
  set_color(palette, QPalette::Midlight, "#F1EADD");
  set_color(palette, QPalette::Mid, "#E4DAC6");
  set_color(palette, QPalette::Dark, "#CFC3AC");
  set_color(palette, QPalette::Shadow, "#8A7B63");

  palette.setColor(QPalette::Disabled, QPalette::Text, "#B5AA98");
  palette.setColor(QPalette::Disabled, QPalette::WindowText, "#B5AA98");
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, "#B5AA98");
  app.setPalette(palette);

  app.setFont(ui_font(12));
  app.setStyleSheet(style_sheet());
}

QString style_sheet() { return QString::fromUtf8(kSheet); }

QLabel* section_label(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("Section"));
  QFont font = ui_font(10, QFont::DemiBold);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 1.1);
  label->setFont(font);
  return label;
}

QWidget* hairline(QWidget* parent) {
  auto* rule = new QWidget(parent);
  rule->setObjectName(QStringLiteral("Hairline"));
  rule->setFixedHeight(1);
  rule->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  return rule;
}

QIcon icon(Glyph glyph, const QColor& tint, int size) {
  constexpr int kGrid = 16;
  const qreal dpr = 2.0;
  QPixmap pixmap(static_cast<int>(size * dpr), static_cast<int>(size * dpr));
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.scale(dpr * size / kGrid, dpr * size / kGrid);

  QPen pen(tint);
  pen.setWidthF(1.35);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  const QRectF box(2.4, 3.6, 11.2, 8.8);
  switch (glyph) {
    case Glyph::kOpen:
      painter.drawPolyline(QPolygonF{QPointF(2.2, 12.4), QPointF(2.2, 4.2), QPointF(6.2, 4.2),
                                     QPointF(7.6, 6.0), QPointF(13.8, 6.0), QPointF(13.8, 12.4),
                                     QPointF(2.2, 12.4)});
      break;
    case Glyph::kExport:
      painter.drawLine(QPointF(8, 2.8), QPointF(8, 9.6));
      painter.drawPolyline(QPolygonF{QPointF(5.2, 7.0), QPointF(8, 9.8), QPointF(10.8, 7.0)});
      painter.drawPolyline(QPolygonF{QPointF(3.2, 11.2), QPointF(3.2, 13.4), QPointF(12.8, 13.4),
                                     QPointF(12.8, 11.2)});
      break;
    case Glyph::kSample:
      painter.drawRoundedRect(QRectF(2.2, 3.6, 11.6, 8.8), 1.3, 1.3);
      painter.drawPolyline(QPolygonF{QPointF(3.6, 11.0), QPointF(6.6, 7.4), QPointF(8.8, 10.0),
                                     QPointF(10.4, 8.4), QPointF(12.6, 11.0)});
      painter.setBrush(tint);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPointF(10.9, 6.0), 1.0, 1.0);
      break;
    case Glyph::kFit:
      painter.drawPolyline(QPolygonF{QPointF(2.6, 6.2), QPointF(2.6, 2.8), QPointF(6.0, 2.8)});
      painter.drawPolyline(QPolygonF{QPointF(10.0, 2.8), QPointF(13.4, 2.8), QPointF(13.4, 6.2)});
      painter.drawPolyline(QPolygonF{QPointF(13.4, 9.8), QPointF(13.4, 13.2), QPointF(10.0, 13.2)});
      painter.drawPolyline(QPolygonF{QPointF(6.0, 13.2), QPointF(2.6, 13.2), QPointF(2.6, 9.8)});
      break;
    case Glyph::kActual: {
      QFont font = mono_font(7);
      painter.setFont(font);
      painter.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, QStringLiteral("1:1"));
      break;
    }
    case Glyph::kSplit:
      painter.drawRoundedRect(box, 1.4, 1.4);
      painter.drawLine(QPointF(8, 3.6), QPointF(8, 12.4));
      painter.setBrush(QColor(tint.red(), tint.green(), tint.blue(), 40));
      painter.setPen(Qt::NoPen);
      painter.drawRect(QRectF(2.4, 3.6, 5.6, 8.8));
      break;
    case Glyph::kSide:
      painter.drawRoundedRect(QRectF(2.2, 3.6, 5.2, 8.8), 1.2, 1.2);
      painter.drawRoundedRect(QRectF(8.6, 3.6, 5.2, 8.8), 1.2, 1.2);
      break;
    case Glyph::kResult:
      painter.drawRoundedRect(box, 1.4, 1.4);
      painter.setBrush(QColor(tint.red(), tint.green(), tint.blue(), 40));
      painter.setPen(Qt::NoPen);
      painter.drawRect(QRectF(2.4, 3.6, 11.2, 8.8));
      break;
    case Glyph::kMore:
      painter.setPen(Qt::NoPen);
      painter.setBrush(tint);
      for (int i = 0; i < 3; ++i) {
        painter.drawEllipse(QPointF(3.9 + static_cast<double>(i) * 4.1, 8.0), 1.55, 1.55);
      }
      break;
    case Glyph::kUndo: {
      QPainterPath path;
      path.moveTo(13.2, 11.8);
      path.cubicTo(13.2, 6.6, 10.4, 4.4, 6.4, 4.4);
      painter.drawPath(path);
      painter.drawPolyline(QPolygonF{QPointF(8.8, 2.2), QPointF(6.2, 4.4), QPointF(8.8, 6.6)});
      break;
    }
    case Glyph::kRedo: {
      QPainterPath path;
      path.moveTo(2.8, 11.8);
      path.cubicTo(2.8, 6.6, 5.6, 4.4, 9.6, 4.4);
      painter.drawPath(path);
      painter.drawPolyline(QPolygonF{QPointF(7.2, 2.2), QPointF(9.8, 4.4), QPointF(7.2, 6.6)});
      break;
    }
    case Glyph::kPlus:
      painter.drawLine(QPointF(8, 3.6), QPointF(8, 12.4));
      painter.drawLine(QPointF(3.6, 8), QPointF(12.4, 8));
      break;
  }
  painter.end();
  return QIcon(pixmap);
}

}  // namespace figtools::gui::theme
