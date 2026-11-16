// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Visual language for the desktop shell.
//
// A professional inspection workbench in the spirit of LiDAR360 / WolkenVision:
// a calm warm-neutral chrome around a large data view, a Ribbon of grouped
// commands, a project tree on the left, an inspector on the right, an output
// dock at the bottom. One accent reserved for active state and primary action.
// No dark theme, no metallic gradients -- the ground stays warm paper white.

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

// Surfaces: neutral warm greys with a clear value hierarchy. The window is a
// touch darker than the panels so docked areas read as separate plates.
inline constexpr const char* kBase = "#EAE6DE";        // window / ribbon background
inline constexpr const char* kPanel = "#F7F5F0";       // docked panels
inline constexpr const char* kSurface = "#FFFFFF";     // cards, inputs, list rows
inline constexpr const char* kCanvas = "#2A2E2B";      // behind the image (dark for fidelity)
inline constexpr const char* kHover = "#EFEAE0";
inline constexpr const char* kLine = "#DCD6C9";
inline constexpr const char* kLineStrong = "#C6BEAE";

// Type: warm near-black, never pure black.
inline constexpr const char* kText = "#23211C";
inline constexpr const char* kTextDim = "#655F53";
inline constexpr const char* kTextFaint = "#918A7C";
inline constexpr const char* kTextGhost = "#B7B0A1";

// One accent. Used only for active tab, selection, primary button and the
// filled portion of a slider.
inline constexpr const char* kAccent = "#B4562A";
inline constexpr const char* kAccentSoft = "#F5E5D9";
inline constexpr const char* kAccentInk = "#7A3A16";
inline constexpr const char* kWarning = "#A9781A";
inline constexpr const char* kDanger = "#A33A32";
inline constexpr const char* kSuccess = "#4F7A4A";
inline constexpr const char* kInfo = "#3F6E8C";

[[nodiscard]] QColor colour(const char* hex);

void apply(QApplication& app);
[[nodiscard]] QString style_sheet();

[[nodiscard]] QFont ui_font(int pixel_size, int weight = QFont::Normal);
[[nodiscard]] QFont mono_font(int pixel_size, int weight = QFont::Normal);

// Caption above a group of rows (letter spacing set through QFont).
[[nodiscard]] QLabel* section_label(const QString& text, QWidget* parent);
[[nodiscard]] QWidget* hairline(QWidget* parent);

// A docked-panel title bar: "项目" with a small trailing action.
[[nodiscard]] QWidget* dock_title(const QString& text, QWidget* parent);

enum class Glyph {
  kOpen, kSave, kExport, kSample, kFit, kActual, kSplit, kSide, kResult,
  kMore, kPlus, kUndo, kRedo,
  kFolder, kImage, kLayers, kAnnotate, kDataset, kModel, kTrain, kDetect,
  kSettings, kChevron, kSearch, kPlay, kStop, kBatch,
};

[[nodiscard]] QIcon icon(Glyph glyph, const QColor& tint = QColor(0x65, 0x5F, 0x53), int size = 16);

}  // namespace figtools::gui::theme

#endif  // FIGTOOLS_GUI_THEME_HPP_
