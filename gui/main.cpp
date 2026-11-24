// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QDir>
#include <QMenu>
#include <QTabWidget>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>

#include "main_window.hpp"
#include "train_dialog.hpp"
#include "theme.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "1.9.0"
#endif

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("figtools"));
  QApplication::setApplicationVersion(QStringLiteral(FIGTOOLS_VERSION));
  QApplication::setOrganizationName(QStringLiteral("figtools"));

  {
    QSettings settings;
    QString saved = settings.value(QStringLiteral("ui/theme"), QStringLiteral("深色")).toString();
    const QStringList args = app.arguments();
    const int theme_index = args.indexOf(QStringLiteral("--theme"));
    if (theme_index >= 0 && theme_index + 1 < args.size()) {
      saved = args.at(theme_index + 1);
    }
    figtools::gui::theme::set_mode(saved == QStringLiteral("冷色") || saved == QStringLiteral("cool") ||
                                           saved == QStringLiteral("深色") || saved == QStringLiteral("dark")
                                       ? figtools::gui::theme::Mode::kCool
                                       : figtools::gui::theme::Mode::kWarm);

    int accent_index = settings.value(QStringLiteral("ui/accent"), 0).toInt();
    const int accent_arg = args.indexOf(QStringLiteral("--accent"));
    if (accent_arg >= 0 && accent_arg + 1 < args.size()) {
      accent_index = args.at(accent_arg + 1).toInt();
    }
    if (accent_index < 0 || accent_index >= figtools::gui::theme::kAccentCount) {
      accent_index = 0;
    }
    figtools::gui::theme::set_accent(
        static_cast<figtools::gui::theme::Accent>(accent_index));
  }
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

  const int replay_index = arguments.indexOf(QStringLiteral("--replay"));
  const QString replay_log = (replay_index >= 0 && replay_index + 1 < arguments.size())
                                 ? arguments.at(replay_index + 1)
                                 : QString();
  if (arguments.contains(QStringLiteral("--show-train"))) {
    QTimer::singleShot(300, &window, [&window, replay_log]() {
      window.openTrainDialog();
      if (!replay_log.isEmpty()) {
        window.replayTrainingLog(replay_log);
      }
    });
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

  // Open every --open argument as a separate document tab.
  for (int i = 0; i < arguments.size() - 1; ++i) {
    if (arguments.at(i) == QStringLiteral("--open")) {
      const QString path = arguments.at(i + 1);
      QTimer::singleShot(300 + i, &window, [&window, path]() { window.loadPath(path); });
    }
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

  const int segment_index = arguments.indexOf(QStringLiteral("--demo-segment"));
  if (segment_index >= 0 && segment_index + 1 < arguments.size()) {
    const QString directory = arguments.at(segment_index + 1);
    QTimer::singleShot(500, &window, [&window, directory]() { window.loadModel(directory); });
    QTimer::singleShot(1000, &window, [&window]() { window.runSegmentation(); });
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

  const int ribbon_index = arguments.indexOf(QStringLiteral("--ribbon"));
  if (ribbon_index >= 0 && ribbon_index + 1 < arguments.size()) {
    const int page = arguments.at(ribbon_index + 1).toInt();
    QTimer::singleShot(300, &window, [&window, page]() {
      auto* tabs = window.findChild<QTabWidget*>("RibbonTabs");
      if (tabs != nullptr && page >= 0 && page < tabs->count()) {
        tabs->setCurrentIndex(page);
      }
    });
  }

  const int seg_index = arguments.indexOf(QStringLiteral("--test-seg"));
  if (seg_index >= 0 && seg_index + 4 < arguments.size()) {
    const int tool = arguments.at(seg_index + 1).toInt();
    const int sx = arguments.at(seg_index + 2).toInt();
    const int sy = arguments.at(seg_index + 3).toInt();
    const int sz = arguments.at(seg_index + 4).toInt();
    QTimer::singleShot(600, &window, [&window, tool, sx, sy, sz]() {
      window.runInteractiveSegmentation(tool, sx, sy, sz, sz);
    });
  }

  const int mode_index = arguments.indexOf(QStringLiteral("--mode"));
  const int capture_mode = (mode_index >= 0 && mode_index + 1 < arguments.size())
                               ? arguments.at(mode_index + 1).toInt() : -1;
  const int index = arguments.indexOf(QStringLiteral("--screenshot"));
  if (index >= 0 && index + 1 < arguments.size()) {
    const QString path = arguments.at(index + 1);
    const int delay = arguments.contains(QStringLiteral("--delay"))
                          ? arguments.at(arguments.indexOf(QStringLiteral("--delay")) + 1).toInt()
                          : 900;
    QTimer::singleShot(delay, &window, [&window, path, capture_mode]() {
      if (capture_mode >= 0) {
        window.setCompareMode(capture_mode);
      }
      window.prepareForCapture();
      const QPixmap pixmap = window.grab();
      pixmap.save(path);
      QApplication::quit();
    });
  }

  return QApplication::exec();
}
