// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Visual language for the desktop shell.
//
// Two independent axes:
//   * base mode  -- dark graphite or warm porcelain chrome;
//   * accent     -- the single colour that marks current / actionable state.
// The canvas stays dark in both base modes so pixels remain the reference.

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

// Accent families. Each one has a dark-chrome and a light-chrome tone so the
// same "theme colour" still reads correctly after switching base mode.
enum class Accent { kTeal, kAmber, kIndigo, kViolet, kRose };
inline constexpr int kAccentCount = 5;

void set_mode(Mode mode);
[[nodiscard]] Mode mode();
[[nodiscard]] QString mode_name();

void set_accent(Accent accent);
[[nodiscard]] Accent accent();
[[nodiscard]] QString accent_name();
[[nodiscard]] QString accent_name(Accent accent);
[[nodiscard]] QColor accent_colour(Accent accent, bool light = false);

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
