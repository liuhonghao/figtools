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
#include <QVBoxLayout>
#include <QWidget>

namespace figtools::gui::theme {
namespace {

const char* kSheet = R"(
QMainWindow, QWidget#Workbench { background: #EAE6DE; }
QWidget { color: #23211C; selection-background-color: #E6CFC0; selection-color: #23211C; }

QToolTip {
  background: #2A2823; color: #F6F3EC; border: 1px solid #2A2823; padding: 5px 8px;
}

/* ---- menu ------------------------------------------------------------- */
QMenuBar { background: #F7F5F0; color: #655F53; padding: 2px 5px; border: none; }
QMenuBar::item { background: transparent; padding: 4px 9px; border-radius: 2px; }
QMenuBar::item:selected { background: #E9E3D7; color: #23211C; }
QMenu { background: #FFFFFF; border: 1px solid #C6BEAE; padding: 5px; }
QMenu::item { padding: 6px 26px 6px 11px; border-radius: 2px; color: #3A372F; }
QMenu::item:selected { background: #F5E5D9; color: #7A3A16; }
QMenu::item:disabled { color: #B7B0A1; }
QMenu::separator { height: 1px; background: #E4DED2; margin: 5px 7px; }
QMenu::section { color: #918A7C; padding: 6px 11px 3px; }

/* ---- ribbon ----------------------------------------------------------- */
QWidget#Ribbon { background: #F7F5F0; border-bottom: 1px solid #C6BEAE; }
QTabWidget#RibbonTabs { background: transparent; border: none; }
QTabWidget#RibbonTabs::pane { border: none; background: #F7F5F0; min-height: 62px; }
QTabWidget#RibbonTabs > QStackedWidget { background: #F7F5F0; }
QTabBar#RibbonBar { background: transparent; border-bottom: 1px solid #DCD6C9; }
QTabBar#RibbonBar::tab {
  background: transparent; color: #655F53; padding: 8px 14px;
  border: none; border-bottom: 2px solid transparent; margin-right: 1px;
}
QTabBar#RibbonBar::tab:hover { background: #EFEAE0; color: #23211C; }
QTabBar#RibbonBar::tab:selected { color: #B4562A; border-bottom: 2px solid #B4562A; }
QWidget#RibbonPage { background: #F7F5F0; }
QFrame#RibbonGroup { background: transparent; border: none; }
QLabel#RibbonGroupTitle { color: #918A7C; font-size: 9px; }

/* ---- quick access ----------------------------------------------------- */
QWidget#QuickBar { background: #F7F5F0; border-bottom: 1px solid #E4DED2; }
QLabel#Brand { color: #23211C; font-size: 13px; font-weight: 700; }
QLabel#BrandSub { color: #918A7C; font-size: 10px; }
QLabel#TopMeta { color: #918A7C; font-size: 10px; }

/* ---- docked panels ---------------------------------------------------- */
QFrame#Panel { background: #F7F5F0; }
QFrame#PanelLeft { background: #F7F5F0; border-right: 1px solid #C6BEAE; }
QFrame#PanelRight { background: #F7F5F0; border-left: 1px solid #C6BEAE; }
QFrame#PanelBottom { background: #F7F5F0; border-top: 1px solid #C6BEAE; }
QWidget#DockTitle { background: #EFEAE0; border-bottom: 1px solid #DCD6C9; }
QLabel#DockTitleText { color: #655F53; font-size: 10px; font-weight: 600; }
QFrame#Card { background: #FFFFFF; border: 1px solid #DCD6C9; border-radius: 2px; }
QFrame#Card[state="off"] { background: #F2EFE8; border-color: #E4DED2; }
QFrame#Hairline { background: #DCD6C9; border: none; }

QLabel#CardIndex { color: #918A7C; font-size: 10px; font-family: "Consolas", monospace; }
QLabel#CardTitle { color: #23211C; font-size: 11px; font-weight: 600; }
QLabel#CardTitle[state="off"] { color: #918A7C; }
QLabel#ParamLabel { color: #655F53; font-size: 10px; }
QLabel#Section { color: #918A7C; font-size: 9px; }
QLabel#Meta { color: #918A7C; font-size: 10px; }
QLabel#MetaStrong { color: #3A372F; font-size: 10px; font-weight: 600; }
QLabel#KvKey { color: #655F53; font-size: 10px; }
QLabel#KvValue { color: #23211C; font-size: 10px; font-family: "Consolas", monospace; }
QLabel#Hint { color: #918A7C; font-size: 10px; }

/* ---- buttons ---------------------------------------------------------- */
QPushButton {
  background: #FFFFFF; color: #3A372F; border: 1px solid #C6BEAE;
  border-radius: 2px; padding: 5px 11px;
}
QPushButton:hover { background: #F7F3EA; border-color: #A9A08D; }
QPushButton:pressed { background: #EDE6D8; }
QPushButton:checked { background: #F5E5D9; border-color: #C98A5E; color: #7A3A16; }
QPushButton:disabled { background: #F2EFE8; border-color: #E4DED2; color: #B7B0A1; }
QPushButton#Primary {
  background: #B4562A; border-color: #9C4720; color: #FFFFFF; font-weight: 600;
}
QPushButton#Primary:hover { background: #C05F31; }
QPushButton#Primary:pressed { background: #9C4720; }
QPushButton#Quiet { background: transparent; border-color: transparent; color: #655F53; }
QPushButton#Quiet:hover { background: #EDE7DA; color: #23211C; }

QToolButton {
  background: transparent; color: #655F53; border: 1px solid transparent;
  border-radius: 2px; padding: 4px 7px;
}
QToolButton:hover { background: #EDE7DA; color: #23211C; }
QToolButton:pressed { background: #E4DCCB; }
QToolButton:checked { background: #F5E5D9; border-color: #DDAE8B; color: #7A3A16; }
QToolButton::menu-indicator { image: none; width: 0; height: 0; }

QToolButton#RibbonLarge {
  background: transparent; border: 1px solid transparent; border-radius: 2px;
  padding: 5px 8px; color: #3A372F;
}
QToolButton#RibbonLarge:hover { background: #EDE7DA; border-color: #DCD6C9; }
QToolButton#RibbonLarge:checked { background: #F5E5D9; border-color: #DDAE8B; color: #7A3A16; }
QToolButton#RibbonLarge:disabled { color: #B7B0A1; }

QFrame#Segmented { background: #EAE4D8; border: 1px solid #DCD6C9; border-radius: 2px; }
QFrame#Segmented QToolButton { border: 1px solid transparent; border-radius: 2px; padding: 4px 10px; color: #655F53; }
QFrame#Segmented QToolButton:hover { color: #23211C; background: #F5F1E8; }
QFrame#Segmented QToolButton:checked { background: #FFFFFF; border-color: #C6BEAE; color: #23211C; }

/* ---- inputs ----------------------------------------------------------- */
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
  background: #FFFFFF; border: 1px solid #C6BEAE; border-radius: 2px;
  padding: 3px 6px; color: #23211C; min-height: 19px;
}
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: #A9A08D; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus { border-color: #B4562A; }
QComboBox::drop-down { border: none; width: 14px; }
QComboBox QAbstractItemView {
  background: #FFFFFF; border: 1px solid #C6BEAE; outline: none;
  selection-background-color: #F5E5D9; selection-color: #7A3A16;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
  subcontrol-origin: border; subcontrol-position: top right; width: 13px;
  background: #F2EFE8; border-left: 1px solid #DCD6C9;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
  subcontrol-origin: border; subcontrol-position: bottom right; width: 13px;
  background: #F2EFE8; border-left: 1px solid #DCD6C9;
}
QDoubleSpinBox#Value, QSpinBox#Value {
  background: transparent; border: 1px solid transparent; color: #3A372F; padding: 1px;
}
QDoubleSpinBox#Value:hover, QSpinBox#Value:hover { background: #FFFFFF; border-color: #DCD6C9; }
QDoubleSpinBox#Value:focus, QSpinBox#Value:focus { background: #FFFFFF; border-color: #B4562A; }

/* ---- sliders ---------------------------------------------------------- */
QSlider::groove:horizontal { height: 3px; background: #DCD6C9; border-radius: 1px; }
QSlider::sub-page:horizontal { background: #B4562A; border-radius: 1px; }
QSlider::handle:horizontal {
  background: #FFFFFF; border: 1px solid #B4562A; width: 8px; height: 8px;
  margin: -4px 0; border-radius: 5px;
}
QSlider::handle:horizontal:hover { border-width: 2px; }

/* ---- tabs / lists / trees --------------------------------------------- */
QTabWidget::pane { border: none; background: transparent; }
QTabBar { background: transparent; }
QTabBar::tab {
  background: transparent; color: #918A7C; padding: 8px 12px; margin-right: 1px;
  border: none; border-bottom: 2px solid transparent;
}
QTabBar::tab:hover { color: #3A372F; }
QTabBar::tab:selected { color: #23211C; border-bottom: 2px solid #B4562A; }

QListWidget, QListView, QTreeWidget {
  background: #FFFFFF; border: 1px solid #DCD6C9; border-radius: 2px; padding: 2px; outline: none;
}
QListWidget::item, QTreeWidget::item { padding: 4px 6px; border-radius: 2px; color: #3A372F; }
QListWidget::item:hover, QTreeWidget::item:hover { background: #F2EFE8; }
QListWidget::item:selected, QTreeWidget::item:selected { background: #F5E5D9; color: #7A3A16; }
QTreeWidget { border: none; }
QTreeWidget::branch { background: transparent; }

/* ---- scrollbars ------------------------------------------------------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 1px; }
QScrollBar::handle:vertical { background: #CFC8B8; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #B9AF9B; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 1px; }
QScrollBar::handle:horizontal { background: #CFC8B8; border-radius: 4px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- progress / splitter / status ------------------------------------- */
QProgressBar { background: #E4DED2; border: none; border-radius: 1px; height: 3px; }
QProgressBar::chunk { background: #B4562A; border-radius: 1px; }
QProgressBar#Status { background: #E4DED2; max-height: 3px; }
QProgressBar#BatchProgress { background: #E4DED2; border: none; border-radius: 2px;
                             min-height: 5px; max-height: 5px; }
QProgressBar#BatchProgress::chunk { background: #B4562A; border-radius: 2px; }
QSplitter::handle { background: #C6BEAE; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background: #B4562A; }
QStatusBar { background: #F2EFE8; border-top: 1px solid #C6BEAE; color: #918A7C; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #918A7C; font-size: 10px; }
QStatusBar QLabel#StatusValue { color: #3A372F; font-family: "Consolas", monospace; font-size: 10px; }
QStatusBar QLabel#StatusMessage { color: #655F53; font-size: 10px; }

QCheckBox { spacing: 5px; color: #3A372F; }
QCheckBox::indicator { width: 12px; height: 12px; border: 1px solid #C6BEAE; border-radius: 1px; background: #FFFFFF; }
QCheckBox::indicator:checked { background: #B4562A; border-color: #B4562A; }
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
  set_color(palette, QPalette::Base, kSurface);
  set_color(palette, QPalette::AlternateBase, "#F7F5F0");
  set_color(palette, QPalette::ToolTipBase, "#2A2823");
  set_color(palette, QPalette::ToolTipText, "#F6F3EC");
  set_color(palette, QPalette::Text, kText);
  set_color(palette, QPalette::Button, kSurface);
  set_color(palette, QPalette::ButtonText, "#3A372F");
  set_color(palette, QPalette::BrightText, "#FFFFFF");
  set_color(palette, QPalette::Highlight, kAccent);
  set_color(palette, QPalette::HighlightedText, "#FFFFFF");
  set_color(palette, QPalette::Link, kAccent);
  set_color(palette, QPalette::PlaceholderText, kTextGhost);
  set_color(palette, QPalette::Light, "#FFFFFF");
  set_color(palette, QPalette::Midlight, "#F2EFE8");
  set_color(palette, QPalette::Mid, kLine);
  set_color(palette, QPalette::Dark, kLineStrong);
  set_color(palette, QPalette::Shadow, "#8A8474");
  palette.setColor(QPalette::Disabled, QPalette::Text, kTextGhost);
  palette.setColor(QPalette::Disabled, QPalette::WindowText, kTextGhost);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, kTextGhost);
  app.setPalette(palette);
  app.setFont(ui_font(11));
  app.setStyleSheet(style_sheet());
}

QString style_sheet() { return QString::fromUtf8(kSheet); }

QLabel* section_label(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setObjectName(QStringLiteral("Section"));
  QFont font = ui_font(9, QFont::DemiBold);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
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

QWidget* dock_title(const QString& text, QWidget* parent) {
  auto* bar = new QWidget(parent);
  bar->setObjectName(QStringLiteral("DockTitle"));
  bar->setFixedHeight(24);
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(9, 0, 6, 0);
  layout->setSpacing(6);
  auto* label = new QLabel(text, bar);
  label->setObjectName(QStringLiteral("DockTitleText"));
  layout->addWidget(label);
  layout->addStretch(1);
  return bar;
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
  pen.setWidthF(1.3);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  QRectF box(2.4, 3.2, 11.2, 9.6);
  switch (glyph) {
    case Glyph::kOpen:
      painter.drawPolyline(QPolygonF{QPointF(2.2, 12.4), QPointF(2.2, 4.2), QPointF(6.2, 4.2),
                                     QPointF(7.6, 6.0), QPointF(13.8, 6.0), QPointF(13.8, 12.4),
                                     QPointF(2.2, 12.4)});
      break;
    case Glyph::kSave:
      painter.drawRoundedRect(QRectF(2.4, 3.0, 11.2, 10), 1.0, 1.0);
      painter.drawRect(QRectF(5.0, 3.0, 6.0, 4.0));
      painter.drawLine(QPointF(5.0, 11.6), QPointF(11.0, 11.6));
      break;
    case Glyph::kExport:
      painter.drawLine(QPointF(8, 2.6), QPointF(8, 9.6));
      painter.drawPolyline(QPolygonF{QPointF(5.2, 7.0), QPointF(8, 9.8), QPointF(10.8, 7.0)});
      painter.drawPolyline(QPolygonF{QPointF(3.2, 11.2), QPointF(3.2, 13.4), QPointF(12.8, 13.4),
                                     QPointF(12.8, 11.2)});
      break;
    case Glyph::kSample:
      painter.drawRoundedRect(QRectF(2.2, 3.4, 11.6, 9.2), 1.0, 1.0);
      painter.drawPolyline(QPolygonF{QPointF(3.6, 10.8), QPointF(6.6, 7.2), QPointF(8.8, 9.8),
                                     QPointF(10.4, 8.2), QPointF(12.6, 10.8)});
      painter.setBrush(tint);
      painter.setPen(Qt::NoPen);
      painter.drawEllipse(QPointF(10.9, 5.8), 1.0, 1.0);
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
      painter.drawRoundedRect(box, 1.0, 1.0);
      painter.drawLine(QPointF(8, 3.2), QPointF(8, 12.8));
      break;
    case Glyph::kSide:
      painter.drawRoundedRect(QRectF(2.2, 3.2, 5.2, 9.6), 1.0, 1.0);
      painter.drawRoundedRect(QRectF(8.6, 3.2, 5.2, 9.6), 1.0, 1.0);
      break;
    case Glyph::kResult:
      painter.drawRoundedRect(box, 1.0, 1.0);
      painter.setBrush(QColor(tint.red(), tint.green(), tint.blue(), 45));
      painter.setPen(Qt::NoPen);
      painter.drawRect(QRectF(2.4, 3.2, 11.2, 9.6));
      break;
    case Glyph::kFolder:
      painter.drawPolyline(QPolygonF{QPointF(2.0, 4.2), QPointF(6.4, 4.2), QPointF(7.8, 6.0),
                                     QPointF(14.0, 6.0), QPointF(14.0, 12.4), QPointF(2.0, 12.4),
                                     QPointF(2.0, 4.2)});
      break;
    case Glyph::kImage:
      painter.drawRoundedRect(QRectF(2.2, 3.2, 11.6, 9.6), 1.0, 1.0);
      painter.drawEllipse(QPointF(5.6, 6.4), 1.0, 1.0);
      painter.drawPolyline(QPolygonF{QPointF(3.0, 11.6), QPointF(6.6, 8.2), QPointF(9.0, 10.4),
                                     QPointF(11.4, 8.6), QPointF(13.0, 11.6)});
      break;
    case Glyph::kLayers:
      painter.drawPolyline(QPolygonF{QPointF(8.0, 2.4), QPointF(14.0, 5.6), QPointF(8.0, 8.8),
                                     QPointF(2.0, 5.6), QPointF(8.0, 2.4)});
      painter.drawPolyline(QPolygonF{QPointF(2.0, 9.4), QPointF(8.0, 12.6), QPointF(14.0, 9.4)});
      break;
    case Glyph::kAnnotate:
      painter.drawRect(QRectF(2.6, 3.4, 10.8, 9.2));
      painter.drawPolyline(QPolygonF{QPointF(2.6, 8.0), QPointF(13.4, 8.0)});
      painter.drawPolyline(QPolygonF{QPointF(8.0, 3.4), QPointF(8.0, 12.6)});
      break;
    case Glyph::kDataset:
      for (int i = 0; i < 3; ++i) {
        painter.drawRoundedRect(QRectF(2.4, 3.0 + i * 3.4, 11.2, 2.6), 0.6, 0.6);
      }
      break;
    case Glyph::kModel:
      painter.drawPolyline(QPolygonF{QPointF(8.0, 2.2), QPointF(13.6, 5.2), QPointF(13.6, 10.8),
                                     QPointF(8.0, 13.8), QPointF(2.4, 10.8), QPointF(2.4, 5.2),
                                     QPointF(8.0, 2.2)});
      painter.drawPolyline(QPolygonF{QPointF(2.4, 5.2), QPointF(8.0, 8.2), QPointF(13.6, 5.2)});
      painter.drawLine(QPointF(8.0, 8.2), QPointF(8.0, 13.8));
      break;
    case Glyph::kTrain: {
      painter.drawPolyline(QPolygonF{QPointF(2.2, 12.6), QPointF(6.2, 8.0), QPointF(9.0, 10.0),
                                     QPointF(13.8, 4.0)});
      painter.drawPolyline(QPolygonF{QPointF(11.0, 4.0), QPointF(13.8, 4.0), QPointF(13.8, 6.8)});
      break;
    }
    case Glyph::kDetect:
      painter.drawEllipse(QPointF(7.2, 7.2), 4.4, 4.4);
      painter.drawLine(QPointF(10.4, 10.4), QPointF(13.8, 13.8));
      break;
    case Glyph::kSettings:
      painter.drawEllipse(QPointF(8, 8), 4.0, 4.0);
      painter.drawEllipse(QPointF(8, 8), 1.6, 1.6);
      for (int i = 0; i < 4; ++i) {
        const double angle = i * 1.5707963;
        painter.drawLine(QPointF(8 + 5.0 * qCos(angle), 8 + 5.0 * qSin(angle)),
                         QPointF(8 + 6.6 * qCos(angle), 8 + 6.6 * qSin(angle)));
      }
      break;
    case Glyph::kChevron:
      painter.drawPolyline(QPolygonF{QPointF(5.6, 3.6), QPointF(10.4, 8.0), QPointF(5.6, 12.4)});
      break;
    case Glyph::kSearch:
      painter.drawEllipse(QPointF(7.0, 7.0), 4.2, 4.2);
      painter.drawLine(QPointF(10.2, 10.2), QPointF(13.6, 13.6));
      break;
    case Glyph::kPlay:
      painter.setBrush(tint);
      painter.setPen(Qt::NoPen);
      painter.drawPolygon(QPolygonF{QPointF(4.6, 3.0), QPointF(13.0, 8.0), QPointF(4.6, 13.0)});
      break;
    case Glyph::kStop:
      painter.setBrush(tint);
      painter.setPen(Qt::NoPen);
      painter.drawRect(QRectF(4.2, 4.2, 7.6, 7.6));
      break;
    case Glyph::kBatch:
      for (int i = 0; i < 3; ++i) {
        painter.drawRect(QRectF(2.4 + i * 4.2, 2.8 + i * 3.0, 9.0, 8.0));
      }
      break;
    case Glyph::kMore:
      painter.setPen(Qt::NoPen);
      painter.setBrush(tint);
      for (int i = 0; i < 3; ++i) {
        painter.drawEllipse(QPointF(3.9 + static_cast<double>(i) * 4.1, 8.0), 1.5, 1.5);
      }
      break;
    case Glyph::kPlus:
      painter.drawLine(QPointF(8, 3.4), QPointF(8, 12.6));
      painter.drawLine(QPointF(3.4, 8), QPointF(12.6, 8));
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
  }
  painter.end();
  return QIcon(pixmap);
}

}  // namespace figtools::gui::theme
