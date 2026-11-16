// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Batch dialog: pick a folder, run the current pipeline over it, watch progress
// and cancel. The work happens on a std::thread; every UI touch is marshalled
// back with a queued invocation, so the window keeps repainting.

#ifndef FIGTOOLS_GUI_BATCH_DIALOG_HPP_
#define FIGTOOLS_GUI_BATCH_DIALOG_HPP_

#include <QDialog>
#include <QString>

#include <atomic>
#include <thread>

#include "figtools/batch/batch.hpp"
#include "figtools/pipeline/recipe.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace figtools::gui {

class BatchDialog : public QDialog {
  Q_OBJECT

 public:
  BatchDialog(const Recipe& recipe, const QString& suggested_input, QWidget* parent = nullptr);
  ~BatchDialog() override;

 protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  void buildUi(const QString& suggested_input);
  void browseInput();
  void browseOutput();
  void refreshCount();
  void start();
  void cancel();
  void handleProgress(const BatchProgress& progress);
  void handleFinished(const BatchSummary& summary, const QString& fatal_error);
  void setRunning(bool running);
  [[nodiscard]] BatchOptions currentOptions() const;

  Recipe recipe_;
  std::thread worker_;
  std::atomic<bool> cancel_{false};
  bool running_ = false;
  QString last_output_;

  QLineEdit* input_edit_ = nullptr;
  QLineEdit* output_edit_ = nullptr;
  QLineEdit* suffix_edit_ = nullptr;
  QComboBox* format_combo_ = nullptr;
  QCheckBox* recursive_check_ = nullptr;
  QCheckBox* overwrite_check_ = nullptr;
  QLabel* count_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QListWidget* log_ = nullptr;
  QPushButton* start_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QPushButton* open_button_ = nullptr;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_BATCH_DIALOG_HPP_
