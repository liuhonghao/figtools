// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Locating the Python runtime that owns torch.
//
// The product never bundles Python. Instead it looks for an interpreter the
// user already has, in this order: an explicit setting, FIGTOOLS_PYTHON, the
// usual conda locations filtered by "does this env have torch", then PATH.

#ifndef FIGTOOLS_GUI_PYTHON_ENV_HPP_
#define FIGTOOLS_GUI_PYTHON_ENV_HPP_

#include <QString>

namespace figtools::gui {

[[nodiscard]] QString resolve_python();

// Directory containing train.py / predict.py, copied next to the executable by
// the build.
[[nodiscard]] QString python_scripts_dir();

// One-line description for the training dialog, e.g. "D:\...\python.exe".
[[nodiscard]] QString python_hint(const QString& executable);

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_PYTHON_ENV_HPP_
