// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "batch_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <utility>

#include "theme.hpp"

namespace figtools::gui {
namespace {

QString phase_text(const std::string& phase) {
  if (phase == "load") {
    return QObject::tr("读取");
  }
  if (phase == "process") {
    return QObject::tr("处理");
  }
  if (phase == "save") {
    return QObject::tr("写入");
  }
  if (phase == "skip") {
    return QObject::tr("跳过");
  }
  return QObject::tr("完成");
}

}  // namespace

BatchDialog::BatchDialog(const Recipe& recipe, const QString& suggested_input, QWidget* parent)
    : QDialog(parent), recipe_(recipe) {
  setWindowTitle(tr("批量处理"));
  setModal(true);
  resize(660, 560);
  buildUi(suggested_input);
}

BatchDialog::~BatchDialog() {
  cancel_.store(true);
  if (worker_.joinable()) {
    worker_.join();
  }
}

void BatchDialog::buildUi(const QString& suggested_input) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(12);

  auto* title = new QLabel(tr("批量处理"), this);
  title->setFont(theme::ui_font(16, QFont::DemiBold));
  root->addWidget(title);

  auto* subtitle = new QLabel(tr("把当前管线套用到整个文件夹，输出保留目录结构。"), this);
  subtitle->setObjectName(QStringLiteral("Meta"));
  subtitle->setFont(theme::ui_font(11));
  root->addWidget(subtitle);
  root->addWidget(theme::hairline(this));

  auto* form = new QGridLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(10);
  form->setColumnStretch(1, 1);

  const auto field_label = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("Meta"));
    label->setFont(theme::ui_font(11));
    label->setFixedWidth(60);
    return label;
  };

  input_edit_ = new QLineEdit(this);
  input_edit_->setPlaceholderText(tr("选择包含图片的文件夹"));
  auto* input_button = new QPushButton(tr("浏览…"), this);
  input_button->setFixedWidth(68);

  output_edit_ = new QLineEdit(this);
  output_edit_->setPlaceholderText(tr("输出文件夹，留空则同级的 _out 目录"));
  auto* output_button = new QPushButton(tr("浏览…"), this);
  output_button->setFixedWidth(68);

  form->addWidget(field_label(tr("输入目录")), 0, 0);
  form->addWidget(input_edit_, 0, 1);
  form->addWidget(input_button, 0, 2);
  form->addWidget(field_label(tr("输出目录")), 1, 0);
  form->addWidget(output_edit_, 1, 1);
  form->addWidget(output_button, 1, 2);

  suffix_edit_ = new QLineEdit(this);
  suffix_edit_->setText(QStringLiteral("_out"));
  suffix_edit_->setFixedWidth(96);
  format_combo_ = new QComboBox(this);
  format_combo_->addItem(tr("PNG"), static_cast<int>(OutputFormat::kPng));
  format_combo_->addItem(tr("BMP"), static_cast<int>(OutputFormat::kBmp));
  format_combo_->addItem(tr("PPM"), static_cast<int>(OutputFormat::kPpm));
  format_combo_->setFixedWidth(96);

  auto* naming_row = new QHBoxLayout();
  naming_row->setSpacing(8);
  naming_row->addWidget(new QLabel(tr("后缀"), this));
  naming_row->addWidget(suffix_edit_);
  naming_row->addSpacing(8);
  naming_row->addWidget(new QLabel(tr("格式"), this));
  naming_row->addWidget(format_combo_);
  naming_row->addStretch(1);
  form->addWidget(field_label(tr("文件名")), 2, 0);
  form->addLayout(naming_row, 2, 1, 1, 2);

  recursive_check_ = new QCheckBox(tr("包含子目录"), this);
  overwrite_check_ = new QCheckBox(tr("覆盖已存在的输出"), this);
  auto* options_row = new QHBoxLayout();
  options_row->setSpacing(18);
  options_row->addWidget(recursive_check_);
  options_row->addWidget(overwrite_check_);
  options_row->addStretch(1);
  form->addWidget(field_label(tr("选项")), 3, 0);
  form->addLayout(options_row, 3, 1, 1, 2);

  root->addLayout(form);

  count_label_ = new QLabel(tr("尚未选择输入目录"), this);
  count_label_->setFont(theme::ui_font(11));
  root->addWidget(count_label_);

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
  status_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  root->addWidget(status_label_);

  log_ = new QListWidget(this);
  log_->setFont(theme::ui_font(11));
  log_->setSelectionMode(QAbstractItemView::NoSelection);
  root->addWidget(log_, 1);
  auto* placeholder = new QListWidgetItem(
      tr("尚未开始。点击“开始处理”，当前管线会套用到上面目录中的每张图片。"), log_);
  placeholder->setForeground(theme::colour(theme::kTextFaint));

  auto* buttons = new QHBoxLayout();
  buttons->setSpacing(8);
  open_button_ = new QPushButton(tr("打开输出目录"), this);
  open_button_->setEnabled(false);
  cancel_button_ = new QPushButton(tr("关闭"), this);
  start_button_ = new QPushButton(tr("开始处理"), this);
  start_button_->setObjectName(QStringLiteral("Primary"));
  start_button_->setMinimumWidth(104);
  buttons->addWidget(open_button_);
  buttons->addStretch(1);
  buttons->addWidget(cancel_button_);
  buttons->addWidget(start_button_);
  root->addLayout(buttons);

  connect(input_button, &QPushButton::clicked, this, &BatchDialog::browseInput);
  connect(output_button, &QPushButton::clicked, this, &BatchDialog::browseOutput);
  connect(input_edit_, &QLineEdit::textChanged, this, [this]() { refreshCount(); });
  connect(recursive_check_, &QCheckBox::toggled, this, [this]() { refreshCount(); });
  connect(start_button_, &QPushButton::clicked, this, &BatchDialog::start);
  connect(cancel_button_, &QPushButton::clicked, this, &BatchDialog::cancel);
  connect(open_button_, &QPushButton::clicked, this, [this]() {
    if (!last_output_.isEmpty()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(last_output_));
    }
  });

  QSettings settings;
  input_edit_->setText(suggested_input.isEmpty()
                           ? settings.value(QStringLiteral("batch/input")).toString()
                           : suggested_input);
  output_edit_->setText(settings.value(QStringLiteral("batch/output")).toString());
  suffix_edit_->setText(settings.value(QStringLiteral("batch/suffix"), QStringLiteral("_out")).toString());
  const int format = settings.value(QStringLiteral("batch/format"), 0).toInt();
  if (const int index = format_combo_->findData(format); index >= 0) {
    format_combo_->setCurrentIndex(index);
  }
  recursive_check_->setChecked(settings.value(QStringLiteral("batch/recursive"), false).toBool());
  overwrite_check_->setChecked(settings.value(QStringLiteral("batch/overwrite"), false).toBool());

  refreshCount();
}

void BatchDialog::browseInput() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择输入目录"), input_edit_->text());
  if (directory.isEmpty()) {
    return;
  }
  input_edit_->setText(directory);
  if (output_edit_->text().isEmpty()) {
    const QFileInfo info(directory);
    output_edit_->setText(info.dir().filePath(info.fileName() + QStringLiteral("_out")));
  }
  refreshCount();
}

void BatchDialog::browseOutput() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择输出目录"), output_edit_->text());
  if (!directory.isEmpty()) {
    output_edit_->setText(directory);
  }
}

void BatchDialog::refreshCount() {
  if (running_) {
    return;
  }
  const QString directory = input_edit_->text();
  if (directory.isEmpty()) {
    count_label_->setText(tr("尚未选择输入目录"));
    count_label_->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kTextFaint));
    start_button_->setEnabled(false);
    return;
  }
  auto listed = list_images(directory.toStdString(), recursive_check_->isChecked());
  if (!listed) {
    count_label_->setText(tr("无法读取该目录：%1")
                              .arg(QString::fromStdString(listed.error().message)));
    count_label_->setStyleSheet(QStringLiteral("color:%1;").arg(theme::kDanger));
    start_button_->setEnabled(false);
    return;
  }
  if (output_edit_->text().isEmpty()) {
    const QFileInfo info(directory);
    output_edit_->setText(info.dir().filePath(info.fileName() + QStringLiteral("_out")));
  }
  const std::size_t total = listed.value().size();
  count_label_->setText(tr("共 %1 张图片").arg(total));
  count_label_->setStyleSheet(QStringLiteral("color:%1;").arg(total > 0 ? theme::kTextDim
                                                                      : theme::kWarning));
  start_button_->setEnabled(total > 0);
}

BatchOptions BatchDialog::currentOptions() const {
  BatchOptions options;
  options.input_dir = input_edit_->text().toStdString();
  options.output_dir = output_edit_->text().toStdString();
  options.suffix = suffix_edit_->text().toStdString();
  options.format = static_cast<OutputFormat>(format_combo_->currentData().toInt());
  options.recursive = recursive_check_->isChecked();
  options.overwrite = overwrite_check_->isChecked();
  return options;
}

void BatchDialog::start() {
  const BatchOptions options = currentOptions();
  if (options.input_dir.empty()) {
    return;
  }
  if (options.output_dir.empty()) {
    const QFileInfo info(QString::fromStdString(options.input_dir));
    const QString fallback =
        info.dir().filePath(info.fileName() + QStringLiteral("_out"));
    output_edit_->setText(fallback);
  }

  auto listed = list_images(options.input_dir, options.recursive);
  if (!listed || listed.value().empty()) {
    refreshCount();
    return;
  }

  log_->clear();
  progress_->show();
  progress_->setRange(0, static_cast<int>(listed.value().size()));
  progress_->setValue(0);
  status_label_->setText(tr("准备中…"));
  last_output_ = output_edit_->text();
  setRunning(true);
  cancel_.store(false);

  if (worker_.joinable()) {
    worker_.join();
  }
  Recipe recipe = recipe_;
  const BatchOptions run_options = currentOptions();
  worker_ = std::thread([this, recipe, run_options]() {
    BatchSummary summary;
    const auto status = run_batch(
        recipe, run_options, [this]() { return cancel_.load(); },
        [this](const BatchProgress& progress) {
          QMetaObject::invokeMethod(
              this, [this, progress]() { handleProgress(progress); }, Qt::QueuedConnection);
        },
        &summary);
    const QString error =
        status ? QString() : QString::fromStdString(status.error().message);
    QMetaObject::invokeMethod(
        this, [this, summary, error]() { handleFinished(summary, error); }, Qt::QueuedConnection);
  });
}

void BatchDialog::cancel() {
  if (!running_) {
    reject();
    return;
  }
  cancel_.store(true);
  cancel_button_->setEnabled(false);
  status_label_->setText(tr("正在停止，等待当前文件完成…"));
}

void BatchDialog::handleProgress(const BatchProgress& progress) {
  if (!running_) {
    return;
  }
  progress_->setValue(static_cast<int>(progress.done));
  const QString name = QFileInfo(QString::fromStdString(progress.current_input)).fileName();
  status_label_->setText(tr("%1  %2 / %3  ·  %4")
                             .arg(phase_text(progress.phase))
                             .arg(progress.done)
                             .arg(progress.total)
                             .arg(name));
  if (progress.phase == "done" || progress.phase == "skip") {
    auto* item = new QListWidgetItem(
        QStringLiteral("%1  %2").arg(progress.phase == "skip" ? tr("跳过") : tr("完成"), name),
        log_);
    item->setForeground(progress.phase == "skip" ? theme::colour(theme::kTextFaint)
                                                 : theme::colour(theme::kTextDim));
    log_->scrollToBottom();
  }
}

void BatchDialog::handleFinished(const BatchSummary& summary, const QString& fatal_error) {
  setRunning(false);
  if (worker_.joinable()) {
    worker_.join();
  }
  progress_->setValue(static_cast<int>(summary.total));

  if (!fatal_error.isEmpty()) {
    status_label_->setText(tr("批量处理失败"));
    auto* item = new QListWidgetItem(fatal_error, log_);
    item->setForeground(theme::colour(theme::kDanger));
    return;
  }

  for (const BatchItemResult& result : summary.items) {
    if (result.ok || result.skipped) {
      continue;
    }
    auto* item = new QListWidgetItem(
        QStringLiteral("%1  %2")
            .arg(QFileInfo(QString::fromStdString(result.input_path)).fileName(),
                 QString::fromStdString(result.error)),
        log_);
    item->setForeground(theme::colour(theme::kDanger));
  }

  QString headline = tr("完成 %1 · 跳过 %2 · 失败 %3 · %4 s")
                         .arg(summary.succeeded)
                         .arg(summary.skipped)
                         .arg(summary.failed)
                         .arg(summary.milliseconds / 1000.0, 0, 'f', 1);
  if (summary.cancelled) {
    headline = tr("已取消 · 完成 %1 · 失败 %2").arg(summary.succeeded).arg(summary.failed);
  }
  status_label_->setText(headline);

  if (summary.failed > 0) {
    auto* item = new QListWidgetItem(
        tr("有 %1 个文件失败，原因见上。").arg(summary.failed), log_);
    item->setForeground(theme::colour(theme::kDanger));
  }
  log_->scrollToBottom();
  open_button_->setEnabled(summary.succeeded > 0 && !last_output_.isEmpty());

  QSettings settings;
  settings.setValue(QStringLiteral("batch/input"), input_edit_->text());
  settings.setValue(QStringLiteral("batch/output"), output_edit_->text());
  settings.setValue(QStringLiteral("batch/suffix"), suffix_edit_->text());
  settings.setValue(QStringLiteral("batch/format"), format_combo_->currentData());
  settings.setValue(QStringLiteral("batch/recursive"), recursive_check_->isChecked());
  settings.setValue(QStringLiteral("batch/overwrite"), overwrite_check_->isChecked());
}

void BatchDialog::setRunning(bool running) {
  running_ = running;
  for (QWidget* widget : {static_cast<QWidget*>(input_edit_),
                          static_cast<QWidget*>(output_edit_),
                          static_cast<QWidget*>(suffix_edit_),
                          static_cast<QWidget*>(format_combo_),
                          static_cast<QWidget*>(recursive_check_),
                          static_cast<QWidget*>(overwrite_check_)}) {
    widget->setEnabled(!running);
  }
  start_button_->setEnabled(!running);
  start_button_->setText(running ? tr("处理中…") : tr("开始处理"));
  cancel_button_->setText(running ? tr("取消") : tr("关闭"));
  cancel_button_->setEnabled(true);
  open_button_->setEnabled(!running && open_button_->isEnabled());
}

void BatchDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  // Headless smoke test hook: start immediately so CI can verify the worker
  // thread, progress marshalling and output writing without synthetic clicks.
  if (qEnvironmentVariableIsSet("FIGTOOLS_BATCH_AUTOSTART") && !running_) {
    QTimer::singleShot(200, this, &BatchDialog::start);
  }
}

void BatchDialog::closeEvent(QCloseEvent* event) {
  if (running_) {
    cancel_.store(true);
    QApplication::setOverrideCursor(Qt::BusyCursor);
    if (worker_.joinable()) {
      worker_.join();
    }
    QApplication::restoreOverrideCursor();
    running_ = false;
  }
  event->accept();
}

}  // namespace figtools::gui
