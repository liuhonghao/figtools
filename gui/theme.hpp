// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Visual language for the desktop shell, with two switchable palettes.
//
// A professional inspection workbench in the spirit of LiDAR360 / WolkenVision:
// calm chrome around a large data view, a Ribbon of grouped commands, a project
// tree, an inspector and an output dock. Both palettes share the same layout,
// type scale and control shapes; only colour changes.

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

enum class Mode { kWarm, kCool };

// The active palette is process-global. Switching calls apply() again so the
// QSS and QPalette are rebuilt from the new colours.
void set_mode(Mode mode);
[[nodiscard]] Mode mode();
[[nodiscard]] QString mode_name();

// Semantic colour keys. Values are resolved by colour() against the active
// palette, so callers never hard-code a hex colour.
inline constexpr const char* kBase = "base";
inline constexpr const char* kPanel = "panel";
inline constexpr const char* kSurface = "surface";
inline constexpr const char* kCanvas = "canvas";
inline constexpr const char* kHover = "hover";
inline constexpr const char* kLine = "line";
inline constexpr const char* kLineStrong = "line_strong";
inline constexpr const char* kText = "text";
inline constexpr const char* kTextDim = "text_dim";
inline constexpr const char* kTextFaint = "text_faint";
inline constexpr const char* kTextGhost = "text_ghost";
inline constexpr const char* kAccent = "accent";
inline constexpr const char* kAccentSoft = "accent_soft";
inline constexpr const char* kAccentInk = "accent_ink";
inline constexpr const char* kWarning = "warning";
inline constexpr const char* kDanger = "danger";
inline constexpr const char* kSuccess = "success";
inline constexpr const char* kInfo = "info";

// Resolves a semantic key (or a literal #hex for backwards compatibility).
[[nodiscard]] QColor colour(const char* key);
[[nodiscard]] QColor colour(const QString& key);

void apply(QApplication& app);
[[nodiscard]] QString style_sheet();

[[nodiscard]] QFont ui_font(int pixel_size, int weight = QFont::Normal);
[[nodiscard]] QFont mono_font(int pixel_size, int weight = QFont::Normal);

[[nodiscard]] QLabel* section_label(const QString& text, QWidget* parent);
[[nodiscard]] QWidget* hairline(QWidget* parent);
[[nodiscard]] QWidget* dock_title(const QString& text, QWidget* parent);

enum class Glyph {
  kOpen, kSave, kExport, kSample, kFit, kActual, kSplit, kSide, kResult,
  kMore, kPlus, kUndo, kRedo, kTheme,
  kFolder, kImage, kLayers, kAnnotate, kDataset, kModel, kTrain, kDetect,
  kSettings, kChevron, kSearch, kPlay, kStop, kBatch,
};

[[nodiscard]] QIcon icon(Glyph glyph, const QColor& tint = colour(kTextDim), int size = 16);

}  // namespace figtools::gui::theme

#endif  // FIGTOOLS_GUI_THEME_HPP_
