// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QDir>
#include <QTimer>

#include "main_window.hpp"
#include "theme.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "0.6.0"
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
  const int batch_index = arguments.indexOf(QStringLiteral("--batch-dialog"));
  if (batch_index >= 0 && batch_index + 1 < arguments.size()) {
    const QString directory = arguments.at(batch_index + 1);
    const int shot_index = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shot_index >= 0 && shot_index + 1 < arguments.size()) {
      const QString path = arguments.at(shot_index + 1);
      const int delay = arguments.contains(QStringLiteral("--delay"))
                            ? arguments.at(arguments.indexOf(QStringLiteral("--delay")) + 1).toInt()
                            : 900;
      QTimer::singleShot(delay, &window, [path]() {
        QWidget* target = QApplication::activeWindow();
        if (target != nullptr) {
          target->grab().save(path);
          target->close();  // leaves the dialog's nested event loop
        }
        QApplication::quit();
      });
    }
    window.openBatchDialog(directory);
    return 0;
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
