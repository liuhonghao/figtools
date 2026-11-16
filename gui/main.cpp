// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

#include "main_window.hpp"
#include "theme.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "1.0.0"
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
  // Shared capture hook for modal dialogs: grab the active window, close it
  // (which leaves its nested event loop) and quit.
  const auto capture_dialog = [&arguments, &window](const QString& path) {
    const int delay = arguments.contains(QStringLiteral("--delay"))
                          ? arguments.at(arguments.indexOf(QStringLiteral("--delay")) + 1).toInt()
                          : 900;
    QTimer::singleShot(delay, &window, [path]() {
      QWidget* target = QApplication::activeWindow();
      if (target != nullptr) {
        target->grab().save(path);
        target->close();
      }
      QApplication::quit();
    });
  };

  const int batch_index = arguments.indexOf(QStringLiteral("--batch-dialog"));
  if (batch_index >= 0 && batch_index + 1 < arguments.size()) {
    const QString directory = arguments.at(batch_index + 1);
    const int shot_index = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shot_index >= 0 && shot_index + 1 < arguments.size()) {
      capture_dialog(arguments.at(shot_index + 1));
    }
    window.openBatchDialog(directory);
    return 0;
  }

  const int train_index = arguments.indexOf(QStringLiteral("--train-dialog"));
  if (train_index >= 0 && train_index + 1 < arguments.size()) {
    const QString directory = arguments.at(train_index + 1);
    const int shot_index = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shot_index >= 0 && shot_index + 1 < arguments.size()) {
      capture_dialog(arguments.at(shot_index + 1));
    }
    window.openTrainDialog(directory);
    return 0;
  }

  const int open_index = arguments.indexOf(QStringLiteral("--open"));
  if (open_index >= 0 && open_index + 1 < arguments.size()) {
    const QString path = arguments.at(open_index + 1);
    QTimer::singleShot(300, &window, [&window, path]() { window.loadPath(path); });
  }

  const int batch_infer_index = arguments.indexOf(QStringLiteral("--batch-infer-dialog"));
  if (batch_infer_index >= 0 && batch_infer_index + 1 < arguments.size()) {
    const QString directory = arguments.at(batch_infer_index + 1);
    const int shot_index = arguments.indexOf(QStringLiteral("--screenshot"));
    if (shot_index >= 0 && shot_index + 1 < arguments.size()) {
      capture_dialog(arguments.at(shot_index + 1));
    }
    window.openBatchInferDialog(directory);
    return 0;
  }

  const int detect_index = arguments.indexOf(QStringLiteral("--demo-detect"));
  if (detect_index >= 0 && detect_index + 1 < arguments.size()) {
    const QString directory = arguments.at(detect_index + 1);
    QTimer::singleShot(500, &window, [&window, directory]() { window.loadModel(directory); });
    QTimer::singleShot(900, &window, [&window]() { window.runDetection(); });
  }

  const int infer_index = arguments.indexOf(QStringLiteral("--demo-infer"));
  if (infer_index >= 0 && infer_index + 1 < arguments.size()) {
    const QString directory = arguments.at(infer_index + 1);
    QTimer::singleShot(500, &window, [&window, directory]() {
      window.loadModel(directory);
    });
    QTimer::singleShot(800, &window, [&window]() { window.runInference(); });
  }

  const int edit_index = arguments.indexOf(QStringLiteral("--demo-edit"));
  if (edit_index >= 0) {
    const bool undo = arguments.contains(QStringLiteral("--undo"));
    const bool redo = arguments.contains(QStringLiteral("--redo"));
    QTimer::singleShot(500, &window, [&window]() {
      // The "add operator" control is the only button that owns a menu.
      for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->menu() == nullptr) {
          continue;
        }
        for (QAction* action : button->menu()->actions()) {
          if (!action->isSeparator() && action->isEnabled()) {
            action->trigger();
            break;
          }
        }
        break;
      }
    });
    if (undo || redo) {
      QTimer::singleShot(900, &window, [&window, redo]() {
        for (QAction* action : window.findChildren<QAction*>()) {
          const QString prefix = redo ? QStringLiteral("重做") : QStringLiteral("撤销");
          if (action->text().startsWith(prefix) && action->isEnabled()) {
            action->trigger();
            break;
          }
        }
      });
    }
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
