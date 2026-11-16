// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Training dialog. All heavy lifting happens in the Python worker; this class
// owns the process, parses its NDJSON events and never blocks the UI thread.

#ifndef FIGTOOLS_GUI_TRAIN_DIALOG_HPP_
#define FIGTOOLS_GUI_TRAIN_DIALOG_HPP_

#include "metrics_chart.hpp"

#include <QWidget>
#include <QString>

#include <QList>

#include <vector>

class QCheckBox;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProcess;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QToolButton;
class QTableWidget;

namespace figtools::gui {

class TrainDialog : public QWidget {
  Q_OBJECT

 public:
  explicit TrainDialog(const QString& data_hint, QWidget* parent = nullptr);
  ~TrainDialog() override;
  void setDataHint(const QString& hint);
  void replayLog(const QString& path);

 signals:
  // Emitted when a model package has been written successfully.
  void modelReady(const QString& directory);
  /// Emitted when the user asks to leave the training view.
  void closed();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void buildUi(const QString& data_hint);
  void browseData();
  void browseOutput();
  void browsePython();
  void start();
  void cancel();
  void openOutput();
  void readStdout();
  void readStderr();
  void handleFinished(int exit_code, int exit_status);
  void handleEvent(const QString& line);
  void appendLog(const QString& text, const QColor& colour);
  void addMetricRow(const QJsonObject& event, bool yolo);
  void resetMetrics(bool yolo);
  void setRunning(bool running);
  [[nodiscard]] QString taskKind() const;
  [[nodiscard]] bool isDetection() const;
  void applyTaskDefaults();
  void syncTaskCards();
  void updateDefaultsForData();

  QLineEdit* data_edit_ = nullptr;
  QLineEdit* output_edit_ = nullptr;
  QLineEdit* python_edit_ = nullptr;
  QComboBox* task_combo_ = nullptr;
  QComboBox* yolo_version_combo_ = nullptr;
  QCheckBox* yolo_from_scratch_ = nullptr;
  QList<QToolButton*> task_buttons_;
  QComboBox* arch_combo_ = nullptr;
  QComboBox* device_combo_ = nullptr;
  QSpinBox* input_spin_ = nullptr;
  QSpinBox* epochs_spin_ = nullptr;
  QSpinBox* batch_spin_ = nullptr;
  QDoubleSpinBox* lr_spin_ = nullptr;
  QLabel* dataset_label_ = nullptr;
  QLabel* status_label_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QListWidget* log_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QTableWidget* metrics_table_ = nullptr;
  MetricsChart* chart_ = nullptr;
  std::vector<MetricsChart::Point> history_;
  QPushButton* start_button_ = nullptr;
  QPushButton* cancel_button_ = nullptr;
  QPushButton* open_button_ = nullptr;
  QProcess* process_ = nullptr;
  QString stdout_buffer_;
  QString last_model_dir_;
  bool running_ = false;
  bool succeeded_ = false;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_TRAIN_DIALOG_HPP_
