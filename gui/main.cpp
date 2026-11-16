// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QStyleFactory>
#include <QTimer>

#include "main_window.hpp"

namespace {

const char* kStyleSheet = R"(
QMainWindow, QWidget { background: #faf8f2; color: #1c1c19; }
QMenuBar { background: #ffffff; border-bottom: 1px solid #e9e5d8; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: #f3f1e8; border-radius: 4px; }
QMenu { background: #ffffff; border: 1px solid #dbd6c3; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 12px; border-radius: 4px; }
QMenu::item:selected { background: #e6f1fb; color: #0c447c; }
QToolBar { background: #ffffff; border-bottom: 1px solid #e9e5d8; spacing: 4px; padding: 5px 8px; }
QToolButton { padding: 4px 9px; border-radius: 5px; background: transparent; }
QToolButton:hover { background: #f3f1e8; }
QToolButton:pressed { background: #eae7db; }
QGroupBox { background: #ffffff; border: 1px solid #e9e5d8; border-radius: 7px;
            margin-top: 14px; padding-top: 6px; font-weight: normal; }
QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; color: #54544b; }
QComboBox, QSpinBox, QDoubleSpinBox {
  background: #ffffff; border: 1px solid #dbd6c3; border-radius: 4px;
  padding: 2px 6px; min-height: 20px; selection-background-color: #e6f1fb;
}
QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #c9c3ac; }
QComboBox::drop-down { border: none; width: 16px; }
QSlider::groove:horizontal { height: 3px; background: #dbd6c3; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #185fa5; border-radius: 2px; }
QSlider::handle:horizontal {
  background: #ffffff; border: 2px solid #185fa5; width: 11px; height: 11px;
  margin: -5px 0; border-radius: 7px;
}
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #d9d5c6; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: #c6c1b0; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QStatusBar { background: #faf8f2; border-top: 1px solid #e9e5d8; color: #54544b; }
QStatusBar::item { border: none; }
QProgressBar { border: none; background: #e9e5d8; border-radius: 3px; height: 5px; }
QProgressBar::chunk { background: #185fa5; border-radius: 3px; }
QSplitter::handle { background: #e9e5d8; width: 1px; }
)";

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("figtools"));
  QApplication::setApplicationVersion(QStringLiteral("0.2.0"));
  QApplication::setOrganizationName(QStringLiteral("figtools"));
  QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  // Windows ships several CJK faces; pick the UI variant first so Chinese
  // labels do not fall back to a font without the glyphs.
  QFont ui_font(QStringLiteral("Microsoft YaHei UI"), 9);
  ui_font.setStyleStrategy(QFont::PreferAntialias);
  app.setFont(ui_font);

  app.setStyleSheet(QString::fromUtf8(kStyleSheet));

  figtools::gui::MainWindow window;
  window.resize(1440, 900);
  window.show();

  // Headless verification hook: render the window once and exit. Used by CI
  // and by the screenshot script so UI regressions produce a diffable artefact.
  const QStringList arguments = QApplication::arguments();
  const int dataset_index = arguments.indexOf(QStringLiteral("--demo-dataset"));
  if (dataset_index >= 0 && dataset_index + 1 < arguments.size()) {
    const QString path = arguments.at(dataset_index + 1);
    QTimer::singleShot(700, &window, [&window, path]() { window.loadDatasetDirectory(path); });
  }
  if (arguments.contains(QStringLiteral("--demo-annotations"))) {
    QTimer::singleShot(600, &window, [&window]() { window.addDemoAnnotations(); });
  }
  const int index = arguments.indexOf(QStringLiteral("--screenshot"));
  if (index >= 0 && index + 1 < arguments.size()) {
    const QString path = arguments.at(index + 1);
    const int delay = arguments.contains(QStringLiteral("--delay"))
                          ? arguments.at(arguments.indexOf(QStringLiteral("--delay")) + 1).toInt()
                          : 900;
    QTimer::singleShot(delay, &window, [&window, path]() {
      const QPixmap pixmap = window.grab();
      pixmap.save(path);
      QApplication::quit();
    });
  }

  return QApplication::exec();
}
