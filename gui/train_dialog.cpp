// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "train_dialog.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QColor>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "python_env.hpp"
#include "theme.hpp"

namespace figtools::gui {
namespace {

QString format_metric(double value) { return QString::number(value, 'f', 3); }

}  // namespace

TrainDialog::TrainDialog(const QString& data_hint, QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("训练模型"));
  setModal(true);
  resize(700, 620);
  buildUi(data_hint);
}

TrainDialog::~TrainDialog() {
  if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
    process_->kill();
    process_->waitForFinished(3000);
  }
}

void TrainDialog::buildUi(const QString& data_hint) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(12);

  auto* title = new QLabel(tr("训练模型"), this);
  title->setFont(theme::ui_font(16, QFont::DemiBold));
  root->addWidget(title);

  auto* subtitle = new QLabel(
      tr("用 PyTorch 训练分类模型；数据目录可以是“每类一个子文件夹”，也可以是导出的数据集快照。"),
      this);
  subtitle->setObjectName(QStringLiteral("Meta"));
  subtitle->setFont(theme::ui_font(11));
  subtitle->setWordWrap(true);
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
  const auto inline_label = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("Meta"));
    label->setFont(theme::ui_font(11));
    return label;
  };

  data_edit_ = new QLineEdit(this);
  data_edit_->setPlaceholderText(tr("数据目录（每类一个子文件夹）"));
  auto* data_button = new QPushButton(tr("浏览…"), this);
  data_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("数据目录")), 0, 0);
  grid->addWidget(data_edit_, 0, 1);
  grid->addWidget(data_button, 0, 2);

  output_edit_ = new QLineEdit(this);
  output_edit_->setPlaceholderText(tr("模型包输出目录"));
  auto* output_button = new QPushButton(tr("浏览…"), this);
  output_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("模型输出")), 1, 0);
  grid->addWidget(output_edit_, 1, 1);
  grid->addWidget(output_button, 1, 2);

  arch_combo_ = new QComboBox(this);
  arch_combo_->addItem(QStringLiteral("ResNet-18"), QStringLiteral("resnet18"));
  arch_combo_->addItem(tr("MobileNetV3-Small"), QStringLiteral("mobilenet_v3_small"));
  arch_combo_->addItem(tr("小型 CNN（快速）"), QStringLiteral("smallcnn"));
  arch_combo_->setFixedWidth(170);
  input_spin_ = new QSpinBox(this);
  input_spin_->setRange(48, 512);
  input_spin_->setSingleStep(16);
  input_spin_->setValue(224);
  input_spin_->setFixedWidth(84);
  input_spin_->setSuffix(QStringLiteral(" px"));
  auto* arch_row = new QHBoxLayout();
  arch_row->setSpacing(8);
  arch_row->addWidget(arch_combo_);
  arch_row->addSpacing(10);
  arch_row->addWidget(inline_label(tr("输入")));
  arch_row->addWidget(input_spin_);
  arch_row->addStretch(1);
  grid->addWidget(field_label(tr("模型")), 2, 0);
  grid->addLayout(arch_row, 2, 1, 1, 2);

  epochs_spin_ = new QSpinBox(this);
  epochs_spin_->setRange(1, 1000);
  epochs_spin_->setValue(20);
  epochs_spin_->setFixedWidth(74);
  batch_spin_ = new QSpinBox(this);
  batch_spin_->setRange(1, 256);
  batch_spin_->setValue(16);
  batch_spin_->setFixedWidth(74);
  lr_spin_ = new QDoubleSpinBox(this);
  lr_spin_->setRange(1e-5, 1.0);
  lr_spin_->setDecimals(4);
  lr_spin_->setSingleStep(0.0005);
  lr_spin_->setValue(0.001);
  lr_spin_->setFixedWidth(96);
  auto* optim_row = new QHBoxLayout();
  optim_row->setSpacing(8);
  optim_row->addWidget(inline_label(tr("轮数")));
  optim_row->addWidget(epochs_spin_);
  optim_row->addSpacing(8);
  optim_row->addWidget(inline_label(tr("批大小")));
  optim_row->addWidget(batch_spin_);
  optim_row->addSpacing(8);
  optim_row->addWidget(inline_label(tr("学习率")));
  optim_row->addWidget(lr_spin_);
  optim_row->addStretch(1);
  grid->addWidget(field_label(tr("优化")), 3, 0);
  grid->addLayout(optim_row, 3, 1, 1, 2);

  device_combo_ = new QComboBox(this);
  device_combo_->addItem(tr("自动"), QStringLiteral("auto"));
  device_combo_->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
  device_combo_->addItem(QStringLiteral("CUDA"), QStringLiteral("cuda"));
  device_combo_->setFixedWidth(120);
  auto* device_row = new QHBoxLayout();
  device_row->setSpacing(8);
  device_row->addWidget(device_combo_);
  device_row->addStretch(1);
  grid->addWidget(field_label(tr("设备")), 4, 0);
  grid->addLayout(device_row, 4, 1, 1, 2);

  python_edit_ = new QLineEdit(this);
  auto* python_button = new QPushButton(tr("浏览…"), this);
  python_button->setFixedWidth(68);
  grid->addWidget(field_label(tr("Python")), 5, 0);
  grid->addWidget(python_edit_, 5, 1);
  grid->addWidget(python_button, 5, 2);

  root->addLayout(grid);

  dataset_label_ = new QLabel(tr("尚未开始"), this);
  dataset_label_->setObjectName(QStringLiteral("Meta"));
  dataset_label_->setFont(theme::ui_font(11));
  root->addWidget(dataset_label_);

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

  log_ = new QListWidget(this);
  log_->setFont(theme::mono_font(11));
  log_->setSelectionMode(QAbstractItemView::NoSelection);
  log_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  root->addWidget(log_, 1);
  appendLog(tr("选择数据目录与输出位置，然后点“开始训练”。"), theme::colour(theme::kTextFaint));

  auto* buttons = new QHBoxLayout();
  buttons->setSpacing(8);
  open_button_ = new QPushButton(tr("打开模型目录"), this);
  open_button_->setEnabled(false);
  cancel_button_ = new QPushButton(tr("关闭"), this);
  start_button_ = new QPushButton(tr("开始训练"), this);
  start_button_->setObjectName(QStringLiteral("Primary"));
  start_button_->setMinimumWidth(104);
  buttons->addWidget(open_button_);
  buttons->addStretch(1);
  buttons->addWidget(cancel_button_);
  buttons->addWidget(start_button_);
  root->addLayout(buttons);

  connect(data_button, &QPushButton::clicked, this, &TrainDialog::browseData);
  connect(output_button, &QPushButton::clicked, this, &TrainDialog::browseOutput);
  connect(python_button, &QPushButton::clicked, this, &TrainDialog::browsePython);
  connect(start_button_, &QPushButton::clicked, this, &TrainDialog::start);
  connect(cancel_button_, &QPushButton::clicked, this, &TrainDialog::cancel);
  connect(open_button_, &QPushButton::clicked, this, &TrainDialog::openOutput);

  QSettings settings;
  data_edit_->setText(data_hint.isEmpty()
                          ? settings.value(QStringLiteral("train/data")).toString()
                          : data_hint);
  output_edit_->setText(settings.value(QStringLiteral("train/output")).toString());
  python_edit_->setText(settings.value(QStringLiteral("python/executable"),
                                       resolve_python())
                            .toString());
  const QString arch = settings.value(QStringLiteral("train/arch"), QStringLiteral("resnet18")).toString();
  if (const int index = arch_combo_->findData(arch); index >= 0) {
    arch_combo_->setCurrentIndex(index);
  }
  input_spin_->setValue(settings.value(QStringLiteral("train/input"), 224).toInt());
  epochs_spin_->setValue(settings.value(QStringLiteral("train/epochs"), 20).toInt());
  batch_spin_->setValue(settings.value(QStringLiteral("train/batch"), 16).toInt());
  lr_spin_->setValue(settings.value(QStringLiteral("train/lr"), 0.001).toDouble());
  const QString device = settings.value(QStringLiteral("train/device"), QStringLiteral("auto")).toString();
  if (const int index = device_combo_->findData(device); index >= 0) {
    device_combo_->setCurrentIndex(index);
  }
}

void TrainDialog::updateDefaultsForData() {
  const QString data = data_edit_->text();
  if (data.isEmpty() || !output_edit_->text().isEmpty()) {
    return;
  }
  const QFileInfo info(data);
  const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("MMdd-HHmm"));
  output_edit_->setText(QDir::home().filePath(
      QStringLiteral("figtools/models/%1-%2").arg(info.fileName(), stamp)));
}

void TrainDialog::browseData() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择数据目录"), data_edit_->text());
  if (directory.isEmpty()) {
    return;
  }
  data_edit_->setText(directory);
  output_edit_->clear();
  updateDefaultsForData();
}

void TrainDialog::browseOutput() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择模型输出目录"), output_edit_->text());
  if (!directory.isEmpty()) {
    output_edit_->setText(directory);
  }
}

void TrainDialog::browsePython() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("选择 Python 解释器"), QFileInfo(python_edit_->text()).absolutePath(),
      tr("可执行文件 (*.exe);;所有文件 (*.*)"));
  if (!path.isEmpty()) {
    python_edit_->setText(path);
  }
}

void TrainDialog::start() {
  if (running_) {
    return;
  }
  updateDefaultsForData();

  const QString data = data_edit_->text();
  const QString output = output_edit_->text();
  const QString python = python_edit_->text();
  if (data.isEmpty() || output.isEmpty() || python.isEmpty()) {
    appendLog(tr("数据目录、模型输出和 Python 解释器都必须填写。"), theme::colour(theme::kDanger));
    return;
  }
  const QString scripts = python_scripts_dir();
  if (!QFileInfo::exists(scripts + QStringLiteral("/train.py"))) {
    appendLog(tr("找不到训练脚本：%1/train.py").arg(scripts), theme::colour(theme::kDanger));
    return;
  }
  QDir().mkpath(output);

  QJsonObject job;
  job.insert(QStringLiteral("data"), QDir::toNativeSeparators(data));
  job.insert(QStringLiteral("output"), QDir::toNativeSeparators(output));
  job.insert(QStringLiteral("arch"), arch_combo_->currentData().toString());
  job.insert(QStringLiteral("input_size"), input_spin_->value());
  job.insert(QStringLiteral("epochs"), epochs_spin_->value());
  job.insert(QStringLiteral("batch_size"), batch_spin_->value());
  job.insert(QStringLiteral("lr"), lr_spin_->value());
  job.insert(QStringLiteral("device"), device_combo_->currentData().toString());
  job.insert(QStringLiteral("seed"), 2026);
  job.insert(QStringLiteral("val_ratio"), 0.2);

  const QString job_path = output + QStringLiteral("/job.json");
  QFile job_file(job_path);
  if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    appendLog(tr("无法写入 %1").arg(job_path), theme::colour(theme::kDanger));
    return;
  }
  job_file.write(QJsonDocument(job).toJson(QJsonDocument::Indented));
  job_file.close();

  QSettings settings;
  settings.setValue(QStringLiteral("train/data"), data);
  settings.setValue(QStringLiteral("train/output"), output);
  settings.setValue(QStringLiteral("train/arch"), arch_combo_->currentData());
  settings.setValue(QStringLiteral("train/input"), input_spin_->value());
  settings.setValue(QStringLiteral("train/epochs"), epochs_spin_->value());
  settings.setValue(QStringLiteral("train/batch"), batch_spin_->value());
  settings.setValue(QStringLiteral("train/lr"), lr_spin_->value());
  settings.setValue(QStringLiteral("train/device"), device_combo_->currentData());
  settings.setValue(QStringLiteral("python/executable"), python);

  log_->clear();
  succeeded_ = false;
  open_button_->setEnabled(false);
  last_model_dir_.clear();
  stdout_buffer_.clear();
  progress_->setRange(1, epochs_spin_->value());
  progress_->setValue(0);
  progress_->show();
  status_label_->setText(tr("启动训练进程…"));
  appendLog(QStringLiteral("%1 train.py --job %2").arg(python, QFileInfo(job_path).fileName()),
            theme::colour(theme::kTextFaint));
  setRunning(true);

  if (process_ == nullptr) {
    process_ = new QProcess(this);
    process_->setWorkingDirectory(scripts);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process_, &QProcess::readyReadStandardOutput, this, &TrainDialog::readStdout);
    connect(process_, &QProcess::readyReadStandardError, this, &TrainDialog::readStderr);
    connect(process_, &QProcess::finished, this,
            [this](int exit_code, QProcess::ExitStatus status) {
              handleFinished(exit_code, static_cast<int>(status));
            });
  }
  process_->start(python, {QStringLiteral("train.py"), QStringLiteral("--job"), job_path});
  if (!process_->waitForStarted(5000)) {
    appendLog(tr("无法启动 Python：%1").arg(process_->errorString()),
              theme::colour(theme::kDanger));
    setRunning(false);
  }
}

void TrainDialog::cancel() {
  if (!running_) {
    reject();
    return;
  }
  status_label_->setText(tr("正在停止训练…"));
  cancel_button_->setEnabled(false);
  if (process_ != nullptr) {
    process_->kill();
  }
}

void TrainDialog::openOutput() {
  if (!last_model_dir_.isEmpty()) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(last_model_dir_));
  }
}

void TrainDialog::readStdout() {
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

void TrainDialog::readStderr() {
  const QString text = QString::fromUtf8(process_->readAllStandardError());
  for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    // The worker mirrors every epoch to stderr for people running it by hand.
    // Here the structured "epoch" event already produced a line.
    if (line.trimmed().startsWith(QStringLiteral("epoch "))) {
      continue;
    }
    appendLog(line.trimmed(), theme::colour(theme::kTextFaint));
  }
}

void TrainDialog::handleEvent(const QString& line) {
  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(line.toUtf8(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    appendLog(line, theme::colour(theme::kTextFaint));
    return;
  }
  const QJsonObject event = document.object();
  const QString name = event.value(QStringLiteral("event")).toString();

  if (name == QStringLiteral("dataset")) {
    const QJsonArray classes = event.value(QStringLiteral("classes")).toArray();
    QStringList names;
    for (const QJsonValue& value : classes) {
      names << value.toString();
    }
    dataset_label_->setText(tr("%1 类 · 训练 %2 · 验证 %3 · 测试 %4 · 跳过 %5")
                                .arg(classes.size())
                                .arg(event.value(QStringLiteral("train")).toInt())
                                .arg(event.value(QStringLiteral("val")).toInt())
                                .arg(event.value(QStringLiteral("test")).toInt())
                                .arg(event.value(QStringLiteral("skipped")).toInt()));
    appendLog(tr("类别：%1").arg(names.join(QStringLiteral(", "))),
              theme::colour(theme::kTextDim));
  } else if (name == QStringLiteral("epoch")) {
    const int epoch = event.value(QStringLiteral("epoch")).toInt();
    const int total = event.value(QStringLiteral("epochs")).toInt();
    progress_->setRange(1, std::max(1, total));
    progress_->setValue(epoch);
    status_label_->setText(tr("第 %1/%2 轮  ·  loss %3  ·  验证 %4")
                               .arg(epoch)
                               .arg(total)
                               .arg(format_metric(event.value(QStringLiteral("train_loss")).toDouble()),
                                    format_metric(event.value(QStringLiteral("val_accuracy")).toDouble())));
    appendLog(tr("轮 %1/%2  loss %3  train %4  val %5")
                  .arg(epoch)
                  .arg(total)
                  .arg(format_metric(event.value(QStringLiteral("train_loss")).toDouble()),
                       format_metric(event.value(QStringLiteral("train_accuracy")).toDouble()),
                       format_metric(event.value(QStringLiteral("val_accuracy")).toDouble())),
              theme::colour(theme::kTextDim));
  } else if (name == QStringLiteral("done")) {
    succeeded_ = true;
    last_model_dir_ = event.value(QStringLiteral("model_dir")).toString();
    const double val = event.value(QStringLiteral("val_accuracy")).toDouble();
    const double test = event.value(QStringLiteral("test_accuracy")).toDouble();
    const double seconds = event.value(QStringLiteral("seconds")).toDouble();
    status_label_->setText(tr("训练完成 · 验证 %1 · 测试 %2 · %3 s")
                               .arg(format_metric(val), format_metric(test))
                               .arg(seconds, 0, 'f', 1));
    appendLog(tr("模型已写入 %1").arg(last_model_dir_), theme::colour(theme::kSuccess));
    open_button_->setEnabled(!last_model_dir_.isEmpty());
    setRunning(false);
    emit modelReady(last_model_dir_);
  } else if (name == QStringLiteral("error")) {
    appendLog(event.value(QStringLiteral("message")).toString(), theme::colour(theme::kDanger));
    status_label_->setText(tr("训练失败"));
  }
}

void TrainDialog::handleFinished(int exit_code, int exit_status) {
  Q_UNUSED(exit_status);
  if (running_) {
    setRunning(false);
    if (!succeeded_) {
      status_label_->setText(tr("训练结束，未产出模型（退出码 %1）").arg(exit_code));
      appendLog(tr("进程退出：%1").arg(exit_code), theme::colour(theme::kDanger));
    }
  }
  if (cancel_button_->text() == tr("取消")) {
    cancel_button_->setText(tr("关闭"));
    cancel_button_->setEnabled(true);
  }
}

void TrainDialog::appendLog(const QString& text, const QColor& colour) {
  auto* item = new QListWidgetItem(text, log_);
  item->setForeground(colour);
  log_->scrollToBottom();
}

void TrainDialog::setRunning(bool running) {
  running_ = running;
  for (QWidget* widget : {static_cast<QWidget*>(data_edit_), static_cast<QWidget*>(output_edit_),
                          static_cast<QWidget*>(python_edit_), static_cast<QWidget*>(arch_combo_),
                          static_cast<QWidget*>(device_combo_), static_cast<QWidget*>(input_spin_),
                          static_cast<QWidget*>(epochs_spin_), static_cast<QWidget*>(batch_spin_),
                          static_cast<QWidget*>(lr_spin_)}) {
    widget->setEnabled(!running);
  }
  start_button_->setEnabled(!running);
  start_button_->setText(running ? tr("训练中…") : tr("开始训练"));
  cancel_button_->setText(running ? tr("取消") : tr("关闭"));
  cancel_button_->setEnabled(true);
}

void TrainDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  // Headless smoke test: start as soon as the dialog is shown.
  if (qEnvironmentVariableIsSet("FIGTOOLS_TRAIN_AUTOSTART") && !running_) {
    QTimer::singleShot(300, this, &TrainDialog::start);
  }
}

void TrainDialog::closeEvent(QCloseEvent* event) {
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
