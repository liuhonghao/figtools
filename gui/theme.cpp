// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "theme.hpp"

#include <QApplication>
#include <QMap>
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

Mode g_mode = Mode::kCool;  // graphite chrome is the default
Accent g_accent = Accent::kTeal;

struct Palette {
  const char* base; const char* panel; const char* surface; const char* canvas;
  const char* canvas2; const char* hover; const char* line; const char* line_strong;
  const char* text; const char* text_dim; const char* text_faint; const char* text_ghost;
  const char* accent; const char* accent_hover; const char* accent_press;
  const char* accent_soft; const char* accent_ink; const char* warning;
  const char* danger; const char* success; const char* info;
  const char* tooltip_bg; const char* tooltip_text; const char* subtle;
  const char* menu_hover; const char* menu_bg; const char* body_text;
  const char* selected_bg; const char* selected_border; const char* shadow;
};

// Porcelain chrome. The reference is a precision instrument under neutral
// daylight: warm paper surfaces, graphite type, and a dark image well.
constexpr Palette kWarm{
    "#F1EEE8", "#F8F6F1", "#FBFAF7", "#0B0F14", "#161C23", "#F3EFE8", "#DED7CB",
    "#C9C1B4", "#1B2026", "#56606B", "#8A929C", "#A9A296", "#0E7F77", "#0B6A63",
    "#08554F", "#E2F1ED", "#073E3B", "#A8741C", "#A33A32", "#2E8B62", "#3B5FA8",
    "#0B0E12", "#E8EDF2", "#E9E4DB", "#E9E3D7", "#FBFAF7", "#2B333B", "#E2F1ED",
    "#0E7F77", "#CFC7B9"};

// Graphite chrome. Neutral blue-grey surfaces recede; the central image well
// is almost black; one accent marks current state and data is left alone.
constexpr Palette kCool{
    /*base*/ "#0B0E12", /*panel*/ "#0F141A", /*surface*/ "#141920", /*canvas*/ "#07090C",
    /*canvas2*/ "#0E1217", /*hover*/ "#1A212A", /*line*/ "#27303A", /*line_strong*/ "#35404C",
    /*text*/ "#E8EDF2", /*text_dim*/ "#A5B0BC", /*text_faint*/ "#7C8894", /*text_ghost*/ "#596471",
    /*accent*/ "#4FD8C4", /*accent_hover*/ "#6BE4D2", /*accent_press*/ "#2FB6A5",
    /*accent_soft*/ "#123B37", /*accent_ink*/ "#DFFBF4", /*warning*/ "#E3B45E",
    /*danger*/ "#E36B6B", /*success*/ "#58C98B", /*info*/ "#7D9BE8",
    /*tooltip_bg*/ "#05070A", /*tooltip_text*/ "#E8EDF2", /*subtle*/ "#1A212A",
    /*menu_hover*/ "#222B35", /*menu_bg*/ "#141920", /*body_text*/ "#D7DEE5",
    /*selected_bg*/ "#123B37", /*selected_border*/ "#4FD8C4", /*shadow*/ "#06080B"};

struct AccentTone {
  const char* accent; const char* hover; const char* press; const char* soft; const char* ink;
};
struct AccentSpec {
  const char* name;
  AccentTone dark;
  AccentTone light;
};

// Every accent has an explicit dark/light pair. The swatch is the dark value.
constexpr AccentSpec kAccents[] = {
    {"青瓷", {"#4FD8C4", "#6BE4D2", "#2FB6A5", "#123B37", "#DFFBF4"},
             {"#0E7F77", "#0B6A63", "#08554F", "#E2F1ED", "#073E3B"}},
    {"琥珀", {"#E3B45E", "#F0C778", "#C2923D", "#3A2E18", "#FFF2D8"},
             {"#A8741C", "#8D6014", "#744E0E", "#F5EAD3", "#4A3408"}},
    {"靛蓝", {"#7D9BE8", "#98B0F0", "#5E7DC8", "#1C2A44", "#E6EEFF"},
             {"#3B5FA8", "#314F8D", "#274073", "#E4EAF7", "#1B2C52"}},
    {"紫晶", {"#B49AE8", "#C7B0F0", "#9278C8", "#2A2140", "#F0E7FF"},
             {"#6C4FA8", "#5A408E", "#493373", "#ECE6F7", "#35245A"}},
    {"绯红", {"#E08A9C", "#EDA3B2", "#C26C7E", "#3B2027", "#FFE5EA"},
             {"#A84E62", "#8F3F52", "#763143", "#F6E4E8", "#5A2230"}},
};

const AccentSpec& accent_spec(Accent value) {
  const int index = static_cast<int>(value);
  if (index < 0 || index >= kAccentCount) {
    return kAccents[0];
  }
  return kAccents[index];
}

const Palette& palette() { return g_mode == Mode::kWarm ? kWarm : kCool; }

QMap<QString, QString> colour_map() {
  const Palette& p = palette();
  QMap<QString, QString> values{
      {"base", p.base}, {"panel", p.panel}, {"surface", p.surface}, {"canvas", p.canvas},
      {"canvas2", p.canvas2}, {"hover", p.hover}, {"line", p.line},
      {"line_strong", p.line_strong}, {"text", p.text}, {"text_dim", p.text_dim},
      {"text_faint", p.text_faint}, {"text_ghost", p.text_ghost}, {"accent", p.accent},
      {"accent_hover", p.accent_hover}, {"accent_press", p.accent_press},
      {"accent_soft", p.accent_soft}, {"accent_ink", p.accent_ink}, {"warning", p.warning},
      {"danger", p.danger}, {"success", p.success}, {"info", p.info},
      {"tooltip_bg", p.tooltip_bg}, {"tooltip_text", p.tooltip_text}, {"subtle", p.subtle},
      {"menu_hover", p.menu_hover}, {"menu_bg", p.menu_bg}, {"body_text", p.body_text},
      {"selected_bg", p.selected_bg}, {"selected_border", p.selected_border},
      {"shadow", p.shadow}};
  const AccentSpec& a = accent_spec(g_accent);
  const AccentTone& tone = g_mode == Mode::kWarm ? a.light : a.dark;
  values["accent"] = tone.accent;
  values["accent_hover"] = tone.hover;
  values["accent_press"] = tone.press;
  values["accent_soft"] = tone.soft;
  values["accent_ink"] = tone.ink;
  values["selected_bg"] = tone.soft;
  values["selected_border"] = tone.accent;
  values["info"] = tone.accent;
  return values;
}

QString substitute(QString sheet) {
  const QMap<QString, QString> values = colour_map();
  for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
    sheet.replace("{{" + it.key() + "}}", it.value());
  }
  return sheet;
}


const char* kSheet = R"(
QMainWindow, QWidget#Workbench { background: {{base}}; }
QWidget { color: {{text}}; selection-background-color: {{selected_bg}}; selection-color: {{text}}; }

QToolTip {
  background: {{tooltip_bg}}; color: {{tooltip_text}}; border: 1px solid {{line}}; padding: 5px 8px;
}

/* ---- application chrome ------------------------------------------------ */
QWidget#TopBar {
  background: {{base}};
  border-bottom: 1px solid {{line}};
}
QLabel#Brand { color: {{text}}; font-size: 13px; font-weight: 700; }
QLabel#BrandSub { color: {{text_faint}}; font-size: 9px; letter-spacing: 1.6px; }
QLabel#TopStatus { color: {{success}}; font-size: 10px; }
QToolButton#TopMenu, QToolButton#ThemeAccent, QToolButton#ThemeMode {
  background: transparent; border: 1px solid transparent; border-radius: 4px;
  color: {{text_dim}}; padding: 4px 9px; min-height: 26px;
}
QToolButton#TopMenu:hover, QToolButton#ThemeAccent:hover, QToolButton#ThemeMode:hover {
  background: {{hover}}; border-color: {{line}}; color: {{text}};
}
QPushButton#SearchButton {
  background: {{surface}}; border: 1px solid {{line}}; border-radius: 4px;
  color: {{text_faint}}; padding: 0 12px; min-height: 28px; max-height: 28px;
  text-align: left;
}
QPushButton#SearchButton:hover { border-color: {{line_strong}}; color: {{text_dim}}; }

QWidget#ModeBar {
  background: {{panel}};
  border-bottom: 1px solid {{line_strong}};
}
QToolButton#WorkspaceTab {
  background: transparent; border: none; border-bottom: 2px solid transparent;
  border-radius: 0; padding: 0 14px; min-height: 50px;
  color: {{text_faint}}; font-size: 12px;
}
QToolButton#WorkspaceTab:hover { color: {{text}}; background: {{hover}}; }
QToolButton#WorkspaceTab:checked {
  color: {{text}}; border-bottom: 2px solid {{accent}}; background: transparent;
}
QToolButton#ModeAction {
  background: transparent; border: 1px solid transparent; border-radius: 4px;
  color: {{text_dim}}; padding: 4px 9px; min-height: 26px;
}
QToolButton#ModeAction:hover { background: {{hover}}; border-color: {{line}}; color: {{text}}; }
QToolButton#ModeAction:disabled { color: {{text_ghost}}; }
QWidget#ViewBar { background: {{panel}}; border-bottom: 1px solid {{line}}; }

/* ---- menu ------------------------------------------------------------- */
QMenuBar { background: {{panel}}; color: {{text_dim}}; padding: 2px 5px; border: none; }
QMenuBar::item { background: transparent; padding: 4px 9px; border-radius: 2px; }
QMenuBar::item:selected { background: {{menu_hover}}; color: {{text}}; }
QMenu { background: {{surface}}; border: 1px solid {{line_strong}}; padding: 5px; }
QMenu::item { padding: 6px 26px 6px 11px; border-radius: 2px; color: {{body_text}}; }
QMenu::item:selected { background: {{accent_soft}}; color: {{accent_ink}}; }
QMenu::item:disabled { color: {{text_ghost}}; }
QMenu::separator { height: 1px; background: {{line}}; margin: 5px 7px; }
QMenu::section { color: {{text_faint}}; padding: 6px 11px 3px; }

/* ---- ribbon ----------------------------------------------------------- */
QWidget#Ribbon { background: {{panel}}; border-bottom: 1px solid {{line_strong}}; }
QTabWidget#RibbonTabs { background: transparent; border: none; }
QTabWidget#RibbonTabs::pane { border: none; background: {{panel}}; min-height: 62px; }
QTabWidget#RibbonTabs > QStackedWidget { background: {{panel}}; }
QTabBar#RibbonBar { background: transparent; border-bottom: 1px solid {{line}}; }
QTabBar#RibbonBar::tab {
  background: transparent; color: {{text_dim}}; padding: 8px 14px;
  border: none; border-bottom: 2px solid transparent; margin-right: 1px;
}
QTabBar#RibbonBar::tab:hover { background: {{hover}}; color: {{text}}; }
QTabBar#RibbonBar::tab:selected { color: {{accent}}; border-bottom: 2px solid {{accent}}; }
QWidget#RibbonPage { background: {{panel}}; }
QFrame#RibbonGroup { background: transparent; border: none; }
QLabel#RibbonGroupTitle { color: {{text_faint}}; font-size: 9px; }

/* ---- quick access ----------------------------------------------------- */
QWidget#QuickBar { background: {{panel}}; border-bottom: 1px solid {{line}}; }
QLabel#TopMeta { color: {{text_faint}}; font-size: 10px; }

/* ---- docked panels ---------------------------------------------------- */
QFrame#Panel { background: {{panel}}; }
QFrame#PanelLeft { background: {{panel}}; border-right: 1px solid {{line_strong}}; }
QFrame#PanelRight { background: {{panel}}; border-left: 1px solid {{line_strong}}; }
QFrame#PanelBottom { background: {{panel}}; border-top: 1px solid {{line_strong}}; }
QWidget#DockTitle { background: {{panel}}; border-bottom: 1px solid {{line}}; }
QLabel#DockTitleText { color: {{text_dim}}; font-size: 10px; font-weight: 600; letter-spacing: .6px; }
QFrame#Card { background: {{surface}}; border: 1px solid {{line}}; border-radius: 4px; }
QFrame#Card[state="off"] { background: {{subtle}}; border-color: {{line}}; }
QFrame#Hairline { background: {{line}}; border: none; }

QLabel#CardIndex { color: {{text_faint}}; font-size: 10px; font-family: "Consolas", monospace; }
QLabel#CardTitle { color: {{text}}; font-size: 11px; font-weight: 600; }
QLabel#CardTitle[state="off"] { color: {{text_faint}}; }
QLabel#ParamLabel { color: {{text_dim}}; font-size: 10px; }
QLabel#Section { color: {{text_faint}}; font-size: 9px; }
QLabel#Meta { color: {{text_faint}}; font-size: 10px; }
QLabel#MetaStrong { color: {{body_text}}; font-size: 10px; font-weight: 600; }
QLabel#KvKey { color: {{text_dim}}; font-size: 10px; }
QLabel#KvValue { color: {{text}}; font-size: 10px; font-family: "Consolas", monospace; }
QLabel#Hint { color: {{text_faint}}; font-size: 10px; }

/* ---- buttons ---------------------------------------------------------- */
QPushButton {
  background: {{surface}}; color: {{body_text}}; border: 1px solid {{line_strong}};
  border-radius: 4px; padding: 4px 11px; min-height: 20px;
}
QPushButton:hover { background: {{hover}}; border-color: {{line_strong}}; }
QPushButton:pressed { background: {{accent_press}}; }
QPushButton:checked { background: {{accent_soft}}; border-color: {{accent}}; color: {{accent_ink}}; }
QPushButton:disabled { background: {{subtle}}; border-color: {{line}}; color: {{text_ghost}}; }
QPushButton#Primary {
  background: {{accent}}; border-color: {{accent_press}}; color: {{surface}}; font-weight: 600;
  padding: 4px 12px;
}
QPushButton#Primary:hover { background: {{accent_hover}}; }
QPushButton#Primary:pressed { background: {{accent_press}}; }
QPushButton#Quiet { background: transparent; border-color: transparent; color: {{text_dim}}; }
QPushButton#Quiet:hover { background: {{hover}}; color: {{text}}; }

QToolButton {
  background: transparent; color: {{text_dim}}; border: 1px solid transparent;
  border-radius: 4px; padding: 4px 7px;
}
QToolButton:hover { background: {{hover}}; color: {{text}}; }
QToolButton:pressed { background: {{accent_press}}; }
QToolButton:checked { background: {{accent_soft}}; border-color: {{selected_border}}; color: {{accent_ink}}; }
QToolButton::menu-indicator { image: none; width: 0; height: 0; }
QToolButton#CardMore { padding: 0; color: {{text_faint}}; font-weight: 600; }
QToolButton#CardMore:hover { color: {{text}}; background: transparent; }

QToolButton#RibbonLarge {
  background: transparent; border: 1px solid transparent; border-radius: 2px;
  padding: 5px 8px; color: {{body_text}};
}
QToolButton#RibbonLarge:hover { background: {{hover}}; border-color: {{line}}; }
QToolButton#RibbonLarge:checked { background: {{accent_soft}}; border-color: {{selected_border}}; color: {{accent_ink}}; }
QToolButton#RibbonLarge:disabled { color: {{text_ghost}}; }

QFrame#Segmented { background: transparent; border: none; }
QFrame#Segmented QToolButton { border: none; border-radius: 3px; padding: 4px 9px; color: {{text_faint}}; }
QFrame#Segmented QToolButton:hover { color: {{text}}; background: {{hover}}; }
QFrame#Segmented QToolButton:checked { background: {{accent_soft}}; color: {{accent_ink}}; }

/* ---- inputs ----------------------------------------------------------- */
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
  background: {{surface}}; border: 1px solid {{line_strong}}; border-radius: 4px;
  padding: 3px 6px; color: {{text}}; min-height: 20px;
}
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover { border-color: {{line_strong}}; }
QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus { border-color: {{accent}}; }
QComboBox::drop-down { border: none; width: 14px; }
QComboBox QAbstractItemView {
  background: {{surface}}; border: 1px solid {{line_strong}}; outline: none;
  selection-background-color: {{accent_soft}}; selection-color: {{accent_ink}};
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
  subcontrol-origin: border; subcontrol-position: top right; width: 13px;
  background: {{subtle}}; border-left: 1px solid {{line}};
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
  subcontrol-origin: border; subcontrol-position: bottom right; width: 13px;
  background: {{subtle}}; border-left: 1px solid {{line}};
}
QDoubleSpinBox#Value, QSpinBox#Value {
  background: transparent; border: 1px solid transparent; color: {{body_text}}; padding: 1px;
}
QDoubleSpinBox#Value:hover, QSpinBox#Value:hover { background: {{surface}}; border-color: {{line}}; }
QDoubleSpinBox#Value:focus, QSpinBox#Value:focus { background: {{surface}}; border-color: {{accent}}; }

/* ---- sliders ---------------------------------------------------------- */
QSlider::groove:horizontal { height: 3px; background: {{line}}; border-radius: 1px; }
QSlider::sub-page:horizontal { background: {{accent}}; border-radius: 1px; }
QSlider::handle:horizontal {
  background: {{surface}}; border: 1px solid {{accent}}; width: 8px; height: 8px;
  margin: -4px 0; border-radius: 5px;
}
QSlider::handle:horizontal:hover { border-width: 2px; }

/* ---- tabs / lists / trees --------------------------------------------- */
QTabWidget::pane { border: none; background: transparent; }
QTabBar { background: transparent; }
QTabBar::tab {
  background: transparent; color: {{text_faint}}; padding: 7px 12px; margin-right: 1px;
  border: none; border-bottom: 2px solid transparent;
}
QTabBar::tab:hover { color: {{body_text}}; }
QTabBar::tab:selected { color: {{text}}; border-bottom: 2px solid {{accent}}; }

QListWidget, QListView, QTreeWidget {
  background: {{surface}}; border: 1px solid {{line}}; border-radius: 4px; padding: 2px; outline: none;
}
QListWidget::item, QTreeWidget::item { padding: 4px 6px; border-radius: 3px; color: {{body_text}}; }
QListWidget::item:hover, QTreeWidget::item:hover { background: {{subtle}}; }
QListWidget::item:selected, QTreeWidget::item:selected { background: {{accent_soft}}; color: {{accent_ink}}; }
QTreeWidget { border: none; }
QTreeWidget::branch { background: transparent; }

/* ---- scrollbars ------------------------------------------------------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 1px; }
QScrollBar::handle:vertical { background: {{line_strong}}; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: {{line_strong}}; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 1px; }
QScrollBar::handle:horizontal { background: {{line_strong}}; border-radius: 4px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- progress / splitter / status ------------------------------------- */
QProgressBar { background: {{line}}; border: none; border-radius: 1px; height: 3px; }
QProgressBar::chunk { background: {{accent}}; border-radius: 1px; }
QProgressBar#Status { background: {{line}}; max-height: 3px; }
QProgressBar#BatchProgress { background: {{line}}; border: none; border-radius: 2px;
                             min-height: 5px; max-height: 5px; }
QProgressBar#BatchProgress::chunk { background: {{accent}}; border-radius: 2px; }
QSplitter::handle { background: {{line_strong}}; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background: {{accent}}; }
QStatusBar { background: {{panel}}; border-top: 1px solid {{line}}; color: {{text_faint}}; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: {{text_faint}}; font-size: 10px; }
QStatusBar QLabel#StatusValue { color: {{body_text}}; font-family: "Consolas", monospace; font-size: 10px; }
QStatusBar QLabel#StatusMessage { color: {{text_dim}}; font-size: 10px; }

QCheckBox { spacing: 5px; color: {{body_text}}; }
QCheckBox::indicator { width: 12px; height: 12px; border: 1px solid {{line_strong}}; border-radius: 1px; background: {{surface}}; }
QCheckBox::indicator:checked { background: {{accent}}; border-color: {{accent}}; }

/* ---- training workbench: task cards + config panels -------------------- */
QToolButton#TaskCard {
  background: {{surface}};
  border: 1px solid {{line}};
  border-radius: 5px;
  color: {{body_text}};
  padding: 10px 6px;
  text-align: center;
}
QToolButton#TaskCard:hover { border-color: {{line_strong}}; background: {{hover}}; }
QFrame#ConfigCard {
  background: {{surface}};
  border: 1px solid {{line}};
  border-radius: 5px;
}
QPushButton#AddStepButton {
  background: {{subtle}};
  border: 1px solid {{line_strong}};
  border-radius: 4px;
  color: {{body_text}};
  padding: 5px 10px;
}
QPushButton#AddStepButton:hover {
  background: {{hover}};
  border-color: {{accent}};
  color: {{accent_ink}};
}

/* ---- collapsible inspector sections ------------------------------------ */
QGroupBox#InspectSection {
  border: 1px solid {{line}};
  border-radius: 4px;
  margin-top: 12px;
  padding: 2px;
  background: {{surface}};
}
QGroupBox#InspectSection::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  padding: 0 6px;
  color: {{text_dim}};
  font-size: 11px;
}
QGroupBox#InspectSection::indicator {
  width: 7px; height: 7px;
  border: 1px solid {{line_strong}};
  border-radius: 1px;
  background: transparent;
}
QGroupBox#InspectSection::indicator:checked { background: {{accent}}; border-color: {{accent}}; }
QLabel#Meta { color: {{text_dim}}; }
QTabWidget#TrainTabs::pane { border: none; background: transparent; }

/* ---- training progress: slim, precise, with an accent fill -------------- */
QProgressBar#BatchProgress {
  border: none;
  background: {{line}};
  height: 6px;
  border-radius: 3px;
  margin: 2px 0 2px 0;
  text-align: center;
}
QProgressBar#BatchProgress::chunk {
  background: {{accent}};
  border-radius: 3px;
  min-width: 4px;
}
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

QColor colour(const char* key) {
  if (key == nullptr) {
    return QColor();
  }
  const QString name = QString::fromLatin1(key);
  const QMap<QString, QString> values = colour_map();
  auto it = values.constFind(name);
  if (it != values.constEnd()) {
    return QColor(it.value());
  }
  return QColor(name);  // literal #hex fallback
}

QColor colour(const QString& key) { return colour(key.toUtf8().constData()); }

void set_mode(Mode value) { g_mode = value; }
Mode mode() { return g_mode; }
QString mode_name() { return g_mode == Mode::kWarm ? QStringLiteral("浅色") : QStringLiteral("深色"); }

void set_accent(Accent value) {
  const int index = static_cast<int>(value);
  if (index >= 0 && index < kAccentCount) {
    g_accent = value;
  }
}
Accent accent() { return g_accent; }
QString accent_name() { return accent_name(g_accent); }
QString accent_name(Accent value) {
  return QString::fromUtf8(accent_spec(value).name);
}
QColor accent_colour(Accent value, bool light) {
  const AccentSpec& spec = accent_spec(value);
  return QColor(light ? spec.light.accent : spec.dark.accent);
}

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
  const QMap<QString, QString> values = colour_map();
  const auto get = [&values](const char* key) -> QColor {
    return QColor(values.value(QLatin1String(key)));
  };
  QPalette palette;
  set_color(palette, QPalette::Window, get("base").name().toUtf8().constData());
  set_color(palette, QPalette::WindowText, get("text").name().toUtf8().constData());
  set_color(palette, QPalette::Base, get("surface").name().toUtf8().constData());
  set_color(palette, QPalette::AlternateBase, get("subtle").name().toUtf8().constData());
  set_color(palette, QPalette::ToolTipBase, get("tooltip_bg").name().toUtf8().constData());
  set_color(palette, QPalette::ToolTipText, get("tooltip_text").name().toUtf8().constData());
  set_color(palette, QPalette::Text, get("text").name().toUtf8().constData());
  set_color(palette, QPalette::Button, get("surface").name().toUtf8().constData());
  set_color(palette, QPalette::ButtonText, get("body_text").name().toUtf8().constData());
  set_color(palette, QPalette::BrightText, "#FFFFFF");
  set_color(palette, QPalette::Highlight, get("accent").name().toUtf8().constData());
  set_color(palette, QPalette::HighlightedText, get("surface").name().toUtf8().constData());
  set_color(palette, QPalette::Link, get("accent").name().toUtf8().constData());
  set_color(palette, QPalette::PlaceholderText, get("text_ghost").name().toUtf8().constData());
  set_color(palette, QPalette::Light, "#FFFFFF");
  set_color(palette, QPalette::Midlight, get("subtle").name().toUtf8().constData());
  set_color(palette, QPalette::Mid, get("line").name().toUtf8().constData());
  set_color(palette, QPalette::Dark, get("line_strong").name().toUtf8().constData());
  set_color(palette, QPalette::Shadow, get("shadow").name().toUtf8().constData());
  palette.setColor(QPalette::Disabled, QPalette::Text, colour(kTextGhost));
  palette.setColor(QPalette::Disabled, QPalette::WindowText, colour(kTextGhost));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, colour(kTextGhost));
  app.setPalette(palette);
  app.setFont(ui_font(12));
  app.setStyleSheet(style_sheet());
}

QString style_sheet() { return substitute(QString::fromUtf8(kSheet)); }

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
    case Glyph::kTheme: {
      painter.drawEllipse(QPointF(8, 8), 5.4, 5.4);
      painter.setBrush(tint);
      painter.setPen(Qt::NoPen);
      painter.drawPie(QRectF(2.6, 2.6, 10.8, 10.8), 90 * 16, 180 * 16);
      break;
    }
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
