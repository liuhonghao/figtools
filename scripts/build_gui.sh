#!/usr/bin/env bash
# Builds the desktop shell without CMake.
#
# This is the fallback path for machines that do not have CMake installed.
# The supported path is `cmake --preset dev -DFIGTOOLS_BUILD_GUI=ON`.
#
# Requirements:
#   * QT_ROOT  - a Qt 6 for MinGW kit (contains include/, lib/, bin/moc.exe)
#   * CXX      - g++ from the *matching* MinGW toolchain (ABI matters)
#
# Defaults assume the toolchain has been copied into .toolchain/ so that the
# build does not depend on paths outside this repository.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

QT_ROOT="${QT_ROOT:-$root/.toolchain/qt}"
CXX="${CXX:-$root/.toolchain/mingw/bin/g++.exe}"
BUILD_DIR="${BUILD_DIR:-$root/build}"
OUT_DIR="$BUILD_DIR/gui"

if [[ ! -x "$QT_ROOT/bin/moc.exe" ]]; then
  echo "error: moc not found under QT_ROOT=$QT_ROOT" >&2
  echo "       set QT_ROOT to a Qt 6 MinGW kit" >&2
  exit 1
fi
if [[ ! -x "$CXX" ]]; then
  echo "error: compiler not found: $CXX" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

echo "==> moc"
"$QT_ROOT/bin/moc.exe" gui/image_canvas.hpp -o "$OUT_DIR/moc_image_canvas.cpp"
"$QT_ROOT/bin/moc.exe" gui/histogram_widget.hpp -o "$OUT_DIR/moc_histogram_widget.cpp"
"$QT_ROOT/bin/moc.exe" gui/main_window.hpp -o "$OUT_DIR/moc_main_window.cpp"
"$QT_ROOT/bin/moc.exe" gui/widgets.hpp -o "$OUT_DIR/moc_widgets.cpp"
"$QT_ROOT/bin/moc.exe" gui/batch_dialog.hpp -o "$OUT_DIR/moc_batch_dialog.cpp"
"$QT_ROOT/bin/moc.exe" gui/train_dialog.hpp -o "$OUT_DIR/moc_train_dialog.cpp"

echo "==> compile and link"
"$CXX" -std=c++20 -O2 -DQT_NO_DEBUG -DUNICODE -D_UNICODE \
  -Iinclude -Isrc -Igui \
  -I"$QT_ROOT/include" -I"$QT_ROOT/include/QtCore" \
  -I"$QT_ROOT/include/QtGui" -I"$QT_ROOT/include/QtWidgets" \
  src/core/*.cpp src/io/*.cpp src/ops/*.cpp src/json/*.cpp \
  src/annotate/*.cpp src/dataset/*.cpp src/batch/*.cpp src/pipeline/*.cpp \
  gui/*.cpp "$OUT_DIR"/moc_*.cpp \
  -L"$QT_ROOT/lib" -lQt6Widgets -lQt6Gui -lQt6Core -lz \
  -o "$OUT_DIR/figtools_gui.exe"

mkdir -p python/ft_train
rm -rf python/ft_train/__pycache__
echo "==> deploy python scripts"
rm -rf "$OUT_DIR/python"
cp -r python "$OUT_DIR/python"

echo "==> deploy runtime (DLLs next to the executable)"
cp "$QT_ROOT/bin/Qt6Widgets.dll" "$QT_ROOT/bin/Qt6Gui.dll" "$QT_ROOT/bin/Qt6Core.dll" "$OUT_DIR/"
for dll in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
  candidate="$(dirname "$CXX")/$dll"
  [[ -f "$candidate" ]] && cp "$candidate" "$OUT_DIR/"
done
mkdir -p "$OUT_DIR/platforms"
cp "$QT_ROOT/plugins/platforms/qwindows.dll" "$OUT_DIR/platforms/"

echo "==> done: $OUT_DIR/figtools_gui.exe"
