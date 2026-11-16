// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>

namespace figtools::io {

/// Convert a UTF-8 path string into the platform's native path type.
///
/// All paths entering the C++ core are UTF-8: Qt's QString::toStdString(),
/// JSON manifests and command-line arguments. On Windows the Microsoft/MinGW
/// C runtime interprets narrow paths using the local ANSI code page (GBK on
/// Chinese systems), so a UTF-8 path containing Chinese characters either
/// fails to open or addresses the wrong file. Going through a wide path fixes
/// that without changing every call site.
[[nodiscard]] std::filesystem::path native_path(const std::string& utf8);

/// Inverse of native_path(): native path -> UTF-8 string for logs and JSON.
[[nodiscard]] std::string utf8_path(const std::filesystem::path& path);

}  // namespace figtools::io
