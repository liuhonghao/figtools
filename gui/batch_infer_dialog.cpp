// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "batch_infer_dialog.hpp"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>

#include <algorithm>
#include <QSettings>
#include <QShowEvent>
#include <QSpinBox>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "python_env.hpp"
#include "widgets.hpp"
#include "theme.hpp"

namespace figtools::gui {

BatchInferDialog::BatchInferDialog(const QString& model_hint, const QString& data_hint,
                                   QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("批量推理"));
  setModal(true);
  resize(690, 570);
  buildUi(model_hint, data_hint);
}

BatchInferDialog::~BatchInferDialog() {
  if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
    process_->kill();
    process_->waitForFinished(3000);
  }
}

void BatchInferDialog::buildUi(const QString& model_hint, const QString& data_hint) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(12);

  auto* title = new QLabel(tr("批量推理"), this);
  title->setFont(theme::ui_font(16, QFont::DemiBold));
  root->addWidget(title);

  auto* subtitle = new QLabel(tr("一个 Python 进程处理整个目录，结果写成 CSV 或 JSONL。"), this);
  subtitle->setObjectName(QStringLiteral("Meta"));
  subtitle->setFont(theme::ui_font(11));
  root->addWidget(subtitle);
  root->addWidget(theme::hairline(this));

  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(10);
  grid->setColumnStretch(1, 1);

  const auto field_label = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("Meta"));
    label->setFont(theme::ui_font(11));
    label->setFixedWidth(64);
    return label;
  };

  model_edit_ = new QLineEdit(this);
  model_edit_->setPlaceholderText(tr("包含 model.pt / model.json 的目录"));
  auto* model_button = new QPushButton(tr("浏览…"), this);
  model_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("模型")), 0, 0);
  grid->addWidget(model_edit_, 0, 1);
  grid->addWidget(model_button, 0, 2);

  input_edit_ = new QLineEdit(this);
  input_edit_->setPlaceholderText(tr("图片目录（或单张图片）"));
  auto* input_button = new QPushButton(tr("浏览…"), this);
  input_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("输入")), 1, 0);
  grid->addWidget(input_edit_, 1, 1);
  grid->addWidget(input_button, 1, 2);

  output_edit_ = new QLineEdit(this);
  output_edit_->setPlaceholderText(tr("predictions.csv 或 .jsonl"));
  auto* output_button = new QPushButton(tr("浏览…"), this);
  output_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("结果")), 2, 0);
  grid->addWidget(output_edit_, 2, 1);
  grid->addWidget(output_button, 2, 2);

  topk_spin_ = new QSpinBox(this);
  topk_spin_->setRange(1, 10);
  topk_spin_->setValue(3);
  topk_spin_->setFixedWidth(70);
  batch_spin_ = new QSpinBox(this);
  batch_spin_->setRange(1, 256);
  batch_spin_->setValue(32);
  batch_spin_->setFixedWidth(70);
  device_combo_ = new QComboBox(this);
  device_combo_->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
  device_combo_->addItem(QStringLiteral("CUDA"), QStringLiteral("cuda"));
  device_combo_->setFixedWidth(100);
  recursive_check_ = new ToggleSwitch(this);
  recursive_check_->setChecked(true);
  recursive_check_->setToolTip(tr("递归扫描输入目录下的子文件夹"));

  auto* options_row = new QHBoxLayout();
  options_row->setSpacing(10);
  options_row->addWidget(new QLabel(tr("Top-K"), this));
  options_row->addWidget(topk_spin_);
  options_row->addWidget(new QLabel(tr("批大小"), this));
  options_row->addWidget(batch_spin_);
  options_row->addWidget(device_combo_);
  options_row->addWidget(recursive_check_);
  options_row->addWidget(new QLabel(tr("包含子目录"), this));
  options_row->addStretch(1);
  grid->addWidget(field_label(tr("选项")), 3, 0);
  grid->addLayout(options_row, 3, 1, 1, 2);

  python_edit_ = new QLineEdit(this);
  auto* python_button = new QPushButton(tr("浏览…"), this);
  python_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("Python")), 4, 0);
  grid->addWidget(python_edit_, 4, 1);
  grid->addWidget(python_button, 4, 2);

  root->addLayout(grid);

  progress_ = new QProgressBar(this);
  progress_->setObjectName(QStringLiteral("BatchProgress"));
  progress_->setRange(0, 1);
  progress_->setValue(0);
  progress_->setTextVisible(false);
  progress_->hide();
  root->addWidget(progress_);

  status_label_ = new QLabel(tr("就绪"), this);
  status_label_->setObjectName(QStringLiteral("Meta"));
  status_label_->setFont(theme::mono_font(11));
  root->addWidget(status_label_);

  root->addWidget(theme::dock_title(tr("运行日志"), this));
  log_ = new QListWidget(this);
  log_->setFont(theme::mono_font(11));
  log_->setSelectionMode(QAbstractItemView::NoSelection);
  log_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  root->addWidget(log_, 1);
  appendLog(tr("选择模型与输入目录，然后点“开始推理”。"), theme::colour(theme::kTextFaint));

  auto* buttons = new QHBoxLayout();
  buttons->setSpacing(8);
  open_button_ = new QPushButton(tr("打开结果"), this);
  open_button_->setEnabled(false);
  cancel_button_ = new QPushButton(tr("关闭"), this);
  start_button_ = new QPushButton(tr("开始推理"), this);
  start_button_->setObjectName(QStringLiteral("Primary"));
  start_button_->setMinimumWidth(104);
  buttons->addWidget(open_button_);
  buttons->addStretch(1);
  buttons->addWidget(cancel_button_);
  buttons->addWidget(start_button_);
  root->addLayout(buttons);

  connect(model_button, &QPushButton::clicked, this, &BatchInferDialog::browseModel);
  connect(input_button, &QPushButton::clicked, this, &BatchInferDialog::browseInput);
  connect(output_button, &QPushButton::clicked, this, &BatchInferDialog::browseOutput);
  connect(python_button, &QPushButton::clicked, this, &BatchInferDialog::browsePython);
  connect(input_edit_, &QLineEdit::textChanged, this, &BatchInferDialog::updateOutputDefault);
  connect(start_button_, &QPushButton::clicked, this, &BatchInferDialog::start);
  connect(cancel_button_, &QPushButton::clicked, this, &BatchInferDialog::cancel);
  connect(open_button_, &QPushButton::clicked, this, &BatchInferDialog::openResult);

  QSettings settings;
  model_edit_->setText(model_hint.isEmpty()
                           ? settings.value(QStringLiteral("batchinfer/model")).toString()
                           : model_hint);
  input_edit_->setText(data_hint.isEmpty()
                           ? settings.value(QStringLiteral("batchinfer/input")).toString()
                           : data_hint);
  output_edit_->setText(settings.value(QStringLiteral("batchinfer/output")).toString());
  python_edit_->setText(settings.value(QStringLiteral("python/executable"), resolve_python()).toString());
  topk_spin_->setValue(settings.value(QStringLiteral("batchinfer/topk"), 3).toInt());
  batch_spin_->setValue(settings.value(QStringLiteral("batchinfer/batch"), 32).toInt());
  recursive_check_->setChecked(settings.value(QStringLiteral("batchinfer/recursive"), false).toBool());
  const QString device = settings.value(QStringLiteral("batchinfer/device"), QStringLiteral("cpu")).toString();
  if (const int index = device_combo_->findData(device); index >= 0) {
    device_combo_->setCurrentIndex(index);
  }
  updateOutputDefault();
}

void BatchInferDialog::updateOutputDefault() {
  const QString input = input_edit_->text();
  if (input.isEmpty() || !output_edit_->text().isEmpty()) {
    return;
  }
  const QFileInfo info(input);
  const QDir parent = info.isDir() ? QDir(input) : info.dir();
  output_edit_->setText(parent.filePath(QStringLiteral("predictions.csv")));
}

void BatchInferDialog::browseModel() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择模型目录"), model_edit_->text());
  if (!directory.isEmpty()) {
    model_edit_->setText(directory);
  }
}

void BatchInferDialog::browseInput() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择输入目录"), input_edit_->text());
  if (!directory.isEmpty()) {
    input_edit_->setText(directory);
    output_edit_->clear();
    updateOutputDefault();
  }
}

void BatchInferDialog::browseOutput() {
  const QString path = QFileDialog::getSaveFileName(
      this, tr("结果文件"), output_edit_->text(),
      tr("CSV (*.csv);;JSON Lines (*.jsonl);;所有文件 (*.*)"));
  if (!path.isEmpty()) {
    output_edit_->setText(path);
  }
}

void BatchInferDialog::browsePython() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("选择 Python 解释器"), QFileInfo(python_edit_->text()).absolutePath(),
      tr("可执行文件 (*.exe);;所有文件 (*.*)"));
  if (!path.isEmpty()) {
    python_edit_->setText(path);
  }
}

void BatchInferDialog::start() {
  if (running_) {
    return;
  }
  updateOutputDefault();
  const QString model = model_edit_->text();
  const QString input = input_edit_->text();
  const QString output = output_edit_->text();
  const QString python = python_edit_->text();
  if (model.isEmpty() || input.isEmpty() || output.isEmpty() || python.isEmpty()) {
    appendLog(tr("模型、输入、结果文件和 Python 解释器都必须填写。"),
              theme::colour(theme::kDanger));
    return;
  }
  const QString scripts = python_scripts_dir();
  if (!QFileInfo::exists(scripts + QStringLiteral("/predict_batch.py"))) {
    appendLog(tr("找不到脚本：%1/predict_batch.py").arg(scripts), theme::colour(theme::kDanger));
    return;
  }

  QJsonObject job;
  job.insert(QStringLiteral("model"), QDir::toNativeSeparators(model));
  job.insert(QStringLiteral("input"), QDir::toNativeSeparators(input));
  job.insert(QStringLiteral("output"), QDir::toNativeSeparators(output));
  job.insert(QStringLiteral("recursive"), recursive_check_->isChecked());
  job.insert(QStringLiteral("topk"), topk_spin_->value());
  job.insert(QStringLiteral("batch_size"), batch_spin_->value());
  job.insert(QStringLiteral("device"), device_combo_->currentData().toString());
  job.insert(QStringLiteral("threshold"), 0.0);

  QFileInfo output_info(output);
  QDir().mkpath(output_info.absolutePath());
  const QString job_path = output + QStringLiteral(".job.json");
  QFile job_file(job_path);
  if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    appendLog(tr("无法写入 %1").arg(job_path), theme::colour(theme::kDanger));
    return;
  }
  job_file.write(QJsonDocument(job).toJson(QJsonDocument::Indented));
  job_file.close();

  QSettings settings;
  settings.setValue(QStringLiteral("batchinfer/model"), model);
  settings.setValue(QStringLiteral("batchinfer/input"), input);
  settings.setValue(QStringLiteral("batchinfer/output"), output);
  settings.setValue(QStringLiteral("batchinfer/topk"), topk_spin_->value());
  settings.setValue(QStringLiteral("batchinfer/batch"), batch_spin_->value());
  settings.setValue(QStringLiteral("batchinfer/device"), device_combo_->currentData());
  settings.setValue(QStringLiteral("batchinfer/recursive"), recursive_check_->isChecked());
  settings.setValue(QStringLiteral("python/executable"), python);

  log_->clear();
  succeeded_ = false;
  open_button_->setEnabled(false);
  result_path_.clear();
  stdout_buffer_.clear();
  progress_->setRange(0, 1);
  progress_->setValue(0);
  progress_->show();
  status_label_->setText(tr("启动推理进程…"));
  appendLog(QStringLiteral("%1 predict_batch.py --job %2")
                .arg(python, QFileInfo(job_path).fileName()),
            theme::colour(theme::kTextFaint));
  setRunning(true);

  if (process_ == nullptr) {
    process_ = new QProcess(this);
    process_->setWorkingDirectory(scripts);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::readyReadStandardOutput, this, &BatchInferDialog::readStdout);
    connect(process_, &QProcess::readyReadStandardError, this, &BatchInferDialog::readStderr);
    connect(process_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
              handleFinished(code, static_cast<int>(status));
            });
  }
  process_->start(python, {QStringLiteral("predict_batch.py"), QStringLiteral("--job"), job_path});
  if (!process_->waitForStarted(5000)) {
    appendLog(tr("无法启动 Python：%1").arg(process_->errorString()),
              theme::colour(theme::kDanger));
    setRunning(false);
  }
}

void BatchInferDialog::cancel() {
  if (!running_) {
    reject();
    return;
  }
  status_label_->setText(tr("正在停止…"));
  cancel_button_->setEnabled(false);
  if (process_ != nullptr) {
    process_->kill();
  }
}

void BatchInferDialog::openResult() {
  if (!result_path_.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(result_path_));
  }
}

void BatchInferDialog::readStdout() {
  stdout_buffer_ += QString::fromUtf8(process_->readAllStandardOutput());
  int newline = stdout_buffer_.indexOf(QLatin1Char('\n'));
  while (newline >= 0) {
    const QString line = stdout_buffer_.left(newline).trimmed();
    stdout_buffer_.remove(0, newline + 1);
    if (!line.isEmpty()) {
      handleEvent(line);
    }
    newline = stdout_buffer_.indexOf(QLatin1Char('\n'));
  }
}

void BatchInferDialog::readStderr() {
  const QString text = QString::fromUtf8(process_->readAllStandardError());
  for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    const QString trimmed = line.trimmed();
    if (QRegularExpression(QStringLiteral(R"(^\d+/\d+$)")).match(trimmed).hasMatch()) {
      continue;  // progress is already reported by "batch" events
    }
    appendLog(trimmed, theme::colour(theme::kTextFaint));
  }
}

void BatchInferDialog::handleEvent(const QString& line) {
  const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8());
  if (!document.isObject()) {
    appendLog(line, theme::colour(theme::kTextFaint));
    return;
  }
  const QJsonObject event = document.object();
  const QString name = event.value(QStringLiteral("event")).toString();

  if (name == QStringLiteral("dataset")) {
    const int total = event.value(QStringLiteral("total")).toInt();
    progress_->setRange(0, std::max(1, total));
    status_label_->setText(tr("%1 张 · %2 类 · %3")
                               .arg(total)
                               .arg(event.value(QStringLiteral("classes")).toArray().size())
                               .arg(event.value(QStringLiteral("device")).toString()));
  } else if (name == QStringLiteral("batch")) {
    const int done = event.value(QStringLiteral("done")).toInt();
    const int total = event.value(QStringLiteral("total")).toInt();
    progress_->setValue(done);
    status_label_->setText(tr("%1 / %2 · %3 s")
                               .arg(done)
                               .arg(total)
                               .arg(event.value(QStringLiteral("seconds")).toDouble(), 0, 'f', 2));
  } else if (name == QStringLiteral("done")) {
    succeeded_ = true;
    result_path_ = event.value(QStringLiteral("output")).toString();
    status_label_->setText(tr("完成 %1 张 · %2 s · 低置信度 %3")
                               .arg(event.value(QStringLiteral("total")).toInt())
                               .arg(event.value(QStringLiteral("seconds")).toDouble(), 0, 'f', 2)
                               .arg(event.value(QStringLiteral("low_confidence")).toInt()));
    appendLog(tr("结果：%1").arg(result_path_), theme::colour(theme::kSuccess));
    open_button_->setEnabled(!result_path_.isEmpty());
    setRunning(false);
  } else if (name == QStringLiteral("error")) {
    appendLog(event.value(QStringLiteral("message")).toString(), theme::colour(theme::kDanger));
    status_label_->setText(tr("批量推理失败"));
  }
}

void BatchInferDialog::handleFinished(int exit_code, int exit_status) {
  Q_UNUSED(exit_status);
  if (running_) {
    setRunning(false);
    if (!succeeded_) {
      status_label_->setText(tr("推理结束，未产出结果（退出码 %1）").arg(exit_code));
    }
  }
  if (cancel_button_->text() == tr("取消")) {
    cancel_button_->setText(tr("关闭"));
    cancel_button_->setEnabled(true);
  }
}

void BatchInferDialog::appendLog(const QString& text, const QColor& colour) {
  auto* item = new QListWidgetItem(text, log_);
  item->setForeground(colour);
  log_->scrollToBottom();
}

void BatchInferDialog::setRunning(bool running) {
  running_ = running;
  for (QWidget* widget : {static_cast<QWidget*>(model_edit_), static_cast<QWidget*>(input_edit_),
                          static_cast<QWidget*>(output_edit_), static_cast<QWidget*>(python_edit_),
                          static_cast<QWidget*>(device_combo_), static_cast<QWidget*>(topk_spin_),
                          static_cast<QWidget*>(batch_spin_),
                          static_cast<QWidget*>(recursive_check_)}) {
    widget->setEnabled(!running);
  }
  start_button_->setEnabled(!running);
  start_button_->setText(running ? tr("推理中…") : tr("开始推理"));
  cancel_button_->setText(running ? tr("取消") : tr("关闭"));
  cancel_button_->setEnabled(true);
}

void BatchInferDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  if (qEnvironmentVariableIsSet("FIGTOOLS_BATCHINFER_AUTOSTART") && !running_) {
    QTimer::singleShot(300, this, &BatchInferDialog::start);
  }
}

void BatchInferDialog::closeEvent(QCloseEvent* event) {
  if (running_) {
    running_ = false;
    if (process_ != nullptr) {
      process_->kill();
      process_->waitForFinished(3000);
    }
  }
  event->accept();
}

}  // namespace figtools::gui
