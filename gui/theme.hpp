// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Visual language for the desktop shell.
//
// One place owns the palette, the type scale and the control styling so the
// panels cannot drift apart. The reference is a professional inspection tool
// with a warm paper palette: low-saturation cream chrome, ink text, one
// terracotta accent reserved for interaction, hairline rules instead of boxes
// inside boxes, and numerics in a tabular face.

#ifndef FIGTOOLS_GUI_THEME_HPP_
#define FIGTOOLS_GUI_THEME_HPP_

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QString>

class QApplication;
class QLabel;
class QWidget;

namespace figtools::gui::theme {

// Ground and surfaces. Warm, but with enough separation between the window,
// the panels and the cards that the hierarchy reads at a glance.
inline constexpr const char* kBase = "#F0EADC";      // window chrome
inline constexpr const char* kCanvas = "#E6DDCB";    // behind the image
inline constexpr const char* kPanel = "#FBF8F2";     // side panels
inline constexpr const char* kCard = "#FFFCF6";      // raised card
inline constexpr const char* kInput = "#FFFFFF";
inline constexpr const char* kHover = "#F3EDE0";
inline constexpr const char* kLine = "#E4DAC6";
inline constexpr const char* kLineStrong = "#D2C5AE";

// Type: near-black with a warm cast, never pure #000.
inline constexpr const char* kText = "#241F18";
inline constexpr const char* kTextDim = "#6D6355";
inline constexpr const char* kTextFaint = "#94897A";
inline constexpr const char* kTextGhost = "#B5AA98";

// Signal. One accent, used for the active state and the primary action only.
inline constexpr const char* kAccent = "#B4562A";
inline constexpr const char* kAccentSoft = "#F6E3D7";
inline constexpr const char* kAccentInk = "#7A3A16";
inline constexpr const char* kWarning = "#A9781A";
inline constexpr const char* kDanger = "#A33A32";
inline constexpr const char* kSuccess = "#4F7A4A";

[[nodiscard]] QColor colour(const char* hex);

// Applies Fusion + a light palette + the global sheet. The palette matters as
// much as the sheet: dialogs, scroll areas and item views draw from it.
void apply(QApplication& app);

[[nodiscard]] QString style_sheet();

[[nodiscard]] QFont ui_font(int pixel_size, int weight = QFont::Normal);

// Tabular figures. Falls back to the UI face if no mono font is installed.
[[nodiscard]] QFont mono_font(int pixel_size, int weight = QFont::Normal);

// Small caption above a group of rows. Letter spacing is set through QFont
// because Qt style sheets do not expose it.
[[nodiscard]] QLabel* section_label(const QString& text, QWidget* parent);

// One-pixel rule.
[[nodiscard]] QWidget* hairline(QWidget* parent);

enum class Glyph { kOpen, kExport, kSample, kFit, kActual, kSplit, kSide, kResult, kMore, kPlus };

// Icons are drawn rather than borrowed from the platform theme, so the
// toolbar looks the same on Windows, macOS and Linux.
[[nodiscard]] QIcon icon(Glyph glyph, const QColor& tint = QColor(0x6D, 0x63, 0x55), int size = 16);

}  // namespace figtools::gui::theme

#endif  // FIGTOOLS_GUI_THEME_HPP_
