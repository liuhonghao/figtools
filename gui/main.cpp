// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QDir>
#include <QTimer>

#include "main_window.hpp"
#include "theme.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "0.5.0"
#endif

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("figtools"));
  QApplication::setApplicationVersion(QStringLiteral(FIGTOOLS_VERSION));
  QApplication::setOrganizationName(QStringLiteral("figtools"));

  figtools::gui::theme::apply(app);

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
      window.prepareForCapture();
      const QPixmap pixmap = window.grab();
      pixmap.save(path);
      QApplication::quit();
    });
  }

  return QApplication::exec();
}
