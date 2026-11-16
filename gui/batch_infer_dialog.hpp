// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Batch inference dialog: one Python process classifies every image in a
// folder and writes a CSV / JSONL. Same process protocol as train.py.

#ifndef FIGTOOLS_GUI_BATCH_INFER_DIALOG_HPP_
#define FIGTOOLS_GUI_BATCH_INFER_DIALOG_HPP_

#include <QDialog>
#include <QString>

class QAbstractButton;
class QColor;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace figtools::gui {

class BatchInferDialog : public QDialog {
  Q_OBJECT

 public:
  BatchInferDialog(const QString& model_hint, const QString& data_hint, QWidget* parent = nullptr);
  ~BatchInferDialog() override;

 protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private:
  void buildUi(const QString& model_hint, const QString& data_hint);
  void browseModel();
  void browseInput();
  void browseOutput();
  void browsePython();
  void updateOutputDefault();
  void start();
  void cancel();
  void openResult();
  void readStdout();
  void readStderr();
  void handleEvent(const QString& line);
  void handleFinished(int exit_code, int exit_status);
  void appendLog(const QString& text, const QColor& colour);
  void setRunning(bool running);

  QLineEdit* model_edit_ = nullptr;
  QLineEdit* input_edit_ = nullptr;
  QLineEdit* output_edit_ = nullptr;
  QLineEdit* python_edit_ = nullptr;
  QComboBox* device_combo_ = nullptr;
  QSpinBox* topk_spin_ = nullptr;
  QSpinBox* batch_spin_ = nullptr;
  QAbstractButton* recursive_check_ = nullptr;
  QLabel* status_label_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QListWidget* log_ = nullptr;
  QPushButton* start_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QPushButton* open_button_ = nullptr;
  QProcess* process_ = nullptr;
  QString stdout_buffer_;
  QString result_path_;
  bool running_ = false;
  bool succeeded_ = false;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_BATCH_INFER_DIALOG_HPP_
