// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "train_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
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
#include <QHeaderView>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
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
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "python_env.hpp"
#include "theme.hpp"
#include "metrics_chart.hpp"

namespace figtools::gui {
namespace {

QString format_metric(double value) { return QString::number(value, 'f', 3); }

}  // namespace

TrainDialog::TrainDialog(const QString& data_hint, QWidget* parent) : QWidget(parent) {
  setWindowTitle(tr("训练模型"));
  resize(980, 680);
  buildUi(data_hint);
}

TrainDialog::~TrainDialog() {
  if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
    process_->kill();
    process_->waitForFinished(3000);
  }
}

void TrainDialog::setDataHint(const QString& hint) {
  if (data_edit_ != nullptr && !hint.isEmpty() && running_ == false) {
    data_edit_->setText(hint);
    updateDefaultsForData();
  }
}

void TrainDialog::replayLog(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  resetMetrics(true);
  while (!file.atEnd()) {
    const QString line = QString::fromUtf8(file.readLine()).trimmed();
    if (line.startsWith(QLatin1Char('{'))) {
      // Only replay informational events; "done" would emit modelReady and
      // trigger a model load in the middle of a UI capture.
      if (!line.contains(QStringLiteral("\"event\": \"epoch\""))) {
        continue;  // replay only epoch events for the monitor preview
      }
      handleEvent(line);
    }
  }
  file.close();
  if (tabs_ != nullptr) {
    tabs_->setCurrentIndex(1);  // show the live monitor page with replayed data
  }
}

void TrainDialog::buildUi(const QString& data_hint) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 18);
  root->setSpacing(8);

  // ---- header --------------------------------------------------------------
  auto* title = new QLabel(tr("训练工作台"), this);
  title->setFont(theme::ui_font(19, QFont::DemiBold));
  root->addWidget(title);

  auto* subtitle = new QLabel(
      tr("配置训练任务并实时观察训练进程。分类 / 检测 / 分割共用同一套流程。"),
      this);
  subtitle->setObjectName(QStringLiteral("Meta"));
  subtitle->setFont(theme::ui_font(11));
  root->addWidget(subtitle);

  tabs_ = new QTabWidget(this);
  tabs_->setObjectName(QStringLiteral("TrainTabs"));
  tabs_->setDocumentMode(true);

  // ======================= PAGE 1: CONFIGURATION ============================
  auto* setup_page = new QWidget(tabs_);
  setup_page->setObjectName(QStringLiteral("TrainSetupPage"));
  auto* setup_outer = new QVBoxLayout(setup_page);
  setup_outer->setContentsMargins(4, 14, 4, 4);
  setup_outer->setSpacing(16);

  const auto field_label = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("Meta"));
    label->setFont(theme::ui_font(11));
    return label;
  };
  const auto inline_label = [this](const QString& text) {
    auto* label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("Meta"));
    label->setFont(theme::ui_font(11));
    return label;
  };
  const auto browse_button = [this](const QString& text = tr("浏览…")) {
    auto* button = new QPushButton(text, this);
    button->setFixedWidth(72);
    return button;
  };

  // --- task type as three large selectable cards ---------------------------
  setup_outer->addWidget(theme::section_label(tr("01  选择任务类型"), setup_page));
  auto* task_cards = new QHBoxLayout();
  task_cards->setSpacing(10);
  task_cards->setContentsMargins(0, 0, 0, 0);
  struct TaskCard {
    const char* title;
    const char* detail;
    const char* value;
    theme::Glyph glyph;
  };
  const TaskCard cards[] = {
      {QT_TR_NOOP("图像分类"), QT_TR_NOOP("整图一个标签"), "classifier", theme::Glyph::kModel},
      {QT_TR_NOOP("YOLO 检测"), QT_TR_NOOP("标注框 → 检测模型"), "yolo", theme::Glyph::kDetect},
      {QT_TR_NOOP("检测基线"), QT_TR_NOOP("无需额外依赖"), "detector", theme::Glyph::kAnnotate},
  };
  task_buttons_.clear();
  for (const TaskCard& card : cards) {
    auto* button = new QToolButton(setup_page);
    button->setObjectName(QStringLiteral("TaskCard"));
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIcon(theme::icon(card.glyph, theme::colour(theme::kTextDim), 26));
    button->setIconSize(QSize(26, 26));
    button->setText(tr("%1\n%2").arg(tr(card.title), tr(card.detail)));
    button->setFont(theme::ui_font(11));
    button->setMinimumSize(150, 96);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    button->setCursor(Qt::PointingHandCursor);
    button->setProperty("taskValue", QLatin1String(card.value));
    task_cards->addWidget(button);
    task_buttons_.push_back(button);
  }
  setup_outer->addLayout(task_cards);

  // task_combo_ stays as the single source of truth; cards just select it.
  task_combo_ = new QComboBox(setup_page);
  task_combo_->addItem(tr("图像分类"), QStringLiteral("classifier"));
  task_combo_->addItem(tr("YOLO 检测"), QStringLiteral("yolo"));
  task_combo_->addItem(tr("检测基线"), QStringLiteral("detector"));
  task_combo_->hide();
  for (QToolButton* button : task_buttons_) {
    connect(button, &QToolButton::clicked, this, [this, button]() {
      const QString value = button->property("taskValue").toString();
      const int index = task_combo_->findData(value);
      if (index >= 0) {
        task_combo_->setCurrentIndex(index);
      }
      syncTaskCards();
    });
  }
  connect(task_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this]() {
            syncTaskCards();
            applyTaskDefaults();
          });

  // --- two columns: data/output | model/hyperparameters --------------------
  auto* columns = new QHBoxLayout();
  columns->setSpacing(16);

  auto* data_card = new QFrame(setup_page);
  data_card->setObjectName(QStringLiteral("ConfigCard"));
  auto* data_layout = new QVBoxLayout(data_card);
  data_layout->setContentsMargins(16, 12, 16, 14);
  data_layout->setSpacing(14);
  data_layout->addWidget(theme::section_label(tr("02  数据与输出"), data_card));

  auto* data_row = new QHBoxLayout();
  data_row->setSpacing(8);
  data_edit_ = new QLineEdit(data_card);
  data_edit_->setPlaceholderText(tr("数据集目录（类文件夹 / 快照 / YOLO data.yaml）"));
  auto* data_button = browse_button();
  data_row->addWidget(field_label(tr("数据")));
  data_row->addWidget(data_edit_, 1);
  data_row->addWidget(data_button);
  data_layout->addLayout(data_row);

  auto* output_row = new QHBoxLayout();
  output_row->setSpacing(8);
  output_edit_ = new QLineEdit(data_card);
  output_edit_->setPlaceholderText(tr("模型包输出目录"));
  auto* output_button = browse_button();
  output_row->addWidget(field_label(tr("输出")));
  output_row->addWidget(output_edit_, 1);
  output_row->addWidget(output_button);
  data_layout->addLayout(output_row);

  auto* python_row = new QHBoxLayout();
  python_row->setSpacing(8);
  python_edit_ = new QLineEdit(data_card);
  auto* python_button = browse_button();
  python_row->addWidget(field_label(tr("Python")));
  python_row->addWidget(python_edit_, 1);
  python_row->addWidget(python_button);
  data_layout->addLayout(python_row);
  columns->addWidget(data_card, 1);

  auto* model_card = new QFrame(setup_page);
  model_card->setObjectName(QStringLiteral("ConfigCard"));
  auto* model_layout = new QVBoxLayout(model_card);
  model_layout->setContentsMargins(16, 12, 16, 14);
  model_layout->setSpacing(14);
  model_layout->addWidget(theme::section_label(tr("03  模型与训练参数"), model_card));

  auto* version_row = new QHBoxLayout();
  version_row->setSpacing(8);
  yolo_version_combo_ = new QComboBox(model_card);
  const auto add_yolo = [this](const QString& label, const QString& value) {
    yolo_version_combo_->addItem(label, value);
  };
  add_yolo(tr("YOLO26n · 最新（推荐）"), QStringLiteral("yolo26n"));
  add_yolo(tr("YOLO26s / m / l"), QStringLiteral("yolo26s"));
  add_yolo(tr("YOLO12n"), QStringLiteral("yolo12n"));
  add_yolo(tr("YOLO11n · 稳定"), QStringLiteral("yolo11n"));
  add_yolo(tr("YOLO11s / m"), QStringLiteral("yolo11s"));
  add_yolo(tr("YOLOv8n / s"), QStringLiteral("yolov8n"));
  add_yolo(tr("YOLOv10 / v9 / v5 / v3"), QStringLiteral("yolov10n"));
  yolo_version_combo_->setMinimumWidth(220);
  yolo_from_scratch_ = new QCheckBox(tr("从零训练"), model_card);
  yolo_from_scratch_->setToolTip(
      tr("勾选：本地模型结构，无需下载；取消：加载预训练权重（需联网）"));
  yolo_from_scratch_->setChecked(true);
  version_row->addWidget(field_label(tr("版本")));
  version_row->addWidget(yolo_version_combo_, 1);
  version_row->addWidget(yolo_from_scratch_);
  model_layout->addLayout(version_row);

  arch_combo_ = new QComboBox(model_card);
  arch_combo_->addItem(QStringLiteral("ResNet-18"), QStringLiteral("resnet18"));
  arch_combo_->addItem(tr("MobileNetV3-Small"), QStringLiteral("mobilenet_v3_small"));
  arch_combo_->addItem(tr("小型 CNN（快速）"), QStringLiteral("smallcnn"));
  arch_combo_->setMinimumWidth(150);
  input_spin_ = new QSpinBox(model_card);
  input_spin_->setRange(48, 2048);
  input_spin_->setSingleStep(32);
  input_spin_->setSuffix(QStringLiteral(" px"));
  auto* arch_row = new QHBoxLayout();
  arch_row->setSpacing(8);
  arch_row->addWidget(field_label(tr("架构")));
  arch_row->addWidget(arch_combo_, 1);
  arch_row->addWidget(inline_label(tr("输入")));
  arch_row->addWidget(input_spin_);
  model_layout->addLayout(arch_row);

  epochs_spin_ = new QSpinBox(model_card);
  epochs_spin_->setRange(1, 1000);
  batch_spin_ = new QSpinBox(model_card);
  batch_spin_->setRange(1, 256);
  lr_spin_ = new QDoubleSpinBox(model_card);
  lr_spin_->setRange(1e-5, 1.0);
  lr_spin_->setDecimals(4);
  lr_spin_->setSingleStep(0.0005);
  auto* optim_row = new QHBoxLayout();
  optim_row->setSpacing(8);
  optim_row->addWidget(field_label(tr("优化")));
  optim_row->addWidget(inline_label(tr("轮数")));
  optim_row->addWidget(epochs_spin_);
  optim_row->addWidget(inline_label(tr("批大小")));
  optim_row->addWidget(batch_spin_);
  optim_row->addWidget(inline_label(tr("学习率")));
  optim_row->addWidget(lr_spin_, 1);
  optim_row->addSpacing(12);
  model_layout->addLayout(optim_row);

  device_combo_ = new QComboBox(model_card);
  device_combo_->addItem(tr("自动"), QStringLiteral("auto"));
  device_combo_->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
  device_combo_->addItem(QStringLiteral("CUDA"), QStringLiteral("cuda"));
  auto* device_row = new QHBoxLayout();
  device_row->setSpacing(8);
  device_row->addWidget(field_label(tr("设备")));
  device_row->addWidget(device_combo_, 1);
  model_layout->addLayout(device_row);
  columns->addWidget(model_card, 1);
  setup_outer->addLayout(columns, 1);
  columns->setAlignment(Qt::AlignTop);
  tabs_->addTab(setup_page, tr("训练配置"));

  // ======================= PAGE 2: LIVE MONITOR =============================
  // Left: large full training log. Right: metrics table on top, curves below.
  auto* monitor_page = new QWidget(tabs_);
  auto* monitor_layout = new QVBoxLayout(monitor_page);
  monitor_layout->setContentsMargins(4, 10, 4, 4);
  monitor_layout->setSpacing(8);

  dataset_label_ = new QLabel(tr("尚未开始"), monitor_page);
  dataset_label_->setObjectName(QStringLiteral("Meta"));
  dataset_label_->setFont(theme::ui_font(11));
  monitor_layout->addWidget(dataset_label_);

  auto* monitor_cols = new QHBoxLayout();
  monitor_cols->setSpacing(12);

  // -- left: large training log ---------------------------------------------
  auto* log_panel = new QFrame(monitor_page);
  log_panel->setObjectName(QStringLiteral("ConfigCard"));
  auto* log_layout = new QVBoxLayout(log_panel);
  log_layout->setContentsMargins(0, 0, 0, 0);
  log_layout->setSpacing(0);
  log_layout->addWidget(theme::dock_title(tr("完整训练日志"), log_panel));
  log_ = new QListWidget(log_panel);
  log_->setFont(theme::mono_font(11));
  log_->setSelectionMode(QAbstractItemView::NoSelection);
  log_->setSpacing(3);
  log_layout->addWidget(log_, 1);
  monitor_cols->addWidget(log_panel, 4);

  // -- right: vertical split (metrics table above, curves below) ------------
  auto* right_panel = new QWidget(monitor_page);
  auto* right_layout = new QVBoxLayout(right_panel);
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(12);

  auto* metrics_panel = new QFrame(right_panel);
  metrics_panel->setObjectName(QStringLiteral("ConfigCard"));
  auto* metrics_layout = new QVBoxLayout(metrics_panel);
  metrics_layout->setContentsMargins(0, 0, 0, 0);
  metrics_layout->setSpacing(0);
  metrics_layout->addWidget(theme::dock_title(tr("每轮指标"), metrics_panel));
  metrics_table_ = new QTableWidget(metrics_panel);
  metrics_table_->setColumnCount(5);
  metrics_table_->setHorizontalHeaderLabels(
      {tr("轮次"), tr("mAP50"), tr("mAP50-95"), tr("box_loss"), tr("cls_loss")});
  metrics_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  metrics_table_->verticalHeader()->hide();
  metrics_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  metrics_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  metrics_table_->setAlternatingRowColors(true);
  metrics_table_->setFont(theme::mono_font(10));
  metrics_layout->addWidget(metrics_table_, 1);
  right_layout->addWidget(metrics_panel, 1);

  auto* chart_panel = new QFrame(right_panel);
  chart_panel->setObjectName(QStringLiteral("ConfigCard"));
  auto* chart_layout = new QVBoxLayout(chart_panel);
  chart_layout->setContentsMargins(0, 0, 0, 0);
  chart_layout->setSpacing(0);
  chart_layout->addWidget(theme::dock_title(tr("训练曲线"), chart_panel));
  chart_ = new MetricsChart(chart_panel);
  chart_layout->addWidget(chart_, 1);
  right_layout->addWidget(chart_panel, 1);

  monitor_cols->addWidget(right_panel, 5);
  monitor_layout->addLayout(monitor_cols, 1);
  tabs_->addTab(monitor_page, tr("实时训练"));

  root->addWidget(tabs_, 1);

  // A single slim status line: concise state on the left, progress on the right.
  auto* status_row = new QHBoxLayout();
  status_row->setSpacing(12);
  status_label_ = new QLabel(tr("就绪"), this);
  status_label_->setObjectName(QStringLiteral("Meta"));
  status_label_->setFont(theme::ui_font(11));
  status_row->addWidget(status_label_);
  progress_ = new QProgressBar(this);
  progress_->setObjectName(QStringLiteral("BatchProgress"));
  progress_->setRange(0, 100);
  progress_->setValue(0);
  progress_->setTextVisible(false);
  progress_->setFixedWidth(240);
  status_row->addWidget(progress_);
  status_row->addStretch(1);
  root->addLayout(status_row);

  appendLog(tr("选择数据与参数后点“开始训练”，训练过程在此实时显示。"),
            theme::colour(theme::kTextFaint));

  auto* buttons = new QHBoxLayout();
  buttons->setSpacing(8);
  open_button_ = new QPushButton(tr("打开模型目录"), this);
  open_button_->setEnabled(false);
  cancel_button_ = new QPushButton(tr("返回画布"), this);
  start_button_ = new QPushButton(tr("开始训练"), this);
  start_button_->setObjectName(QStringLiteral("Primary"));
  start_button_->setMinimumWidth(120);
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
  const QString saved_task = settings.value(QStringLiteral("train/detail_task"), QStringLiteral("classifier")).toString();
  if (const int index = task_combo_->findData(saved_task); index >= 0) {
    task_combo_->setCurrentIndex(index);
  }
  const QString saved_yolo = settings.value(QStringLiteral("train/yolo_version"), QStringLiteral("yolo26n")).toString();
  if (const int index = yolo_version_combo_->findData(saved_yolo); index >= 0) {
    yolo_version_combo_->setCurrentIndex(index);
  }
  applyTaskDefaults();
  const QString arch = settings.value(QStringLiteral("train/arch"), QStringLiteral("resnet18")).toString();
  if (const int index = arch_combo_->findData(arch); index >= 0) {
    arch_combo_->setCurrentIndex(index);
  }
  const QString kind = taskKind();
  const int default_input = kind == QLatin1String("yolo") ? 640
                            : (kind == QLatin1String("detector") ? 64 : 224);
  const int default_epochs = kind == QLatin1String("yolo") ? 50
                             : (kind == QLatin1String("detector") ? 15 : 20);
  const int default_batch = kind == QLatin1String("yolo") ? 16
                            : (kind == QLatin1String("detector") ? 32 : 16);
  input_spin_->setValue(settings.value(QStringLiteral("train/input"), default_input).toInt());
  epochs_spin_->setValue(settings.value(QStringLiteral("train/epochs"), default_epochs).toInt());
  batch_spin_->setValue(settings.value(QStringLiteral("train/batch"), default_batch).toInt());
  lr_spin_->setValue(settings.value(QStringLiteral("train/lr"), 0.001).toDouble());
  const QString device = settings.value(QStringLiteral("train/device"), QStringLiteral("auto")).toString();
  if (const int index = device_combo_->findData(device); index >= 0) {
    device_combo_->setCurrentIndex(index);
  }
}

QString TrainDialog::taskKind() const {
  return task_combo_ != nullptr ? task_combo_->currentData().toString()
                                : QStringLiteral("classifier");
}

bool TrainDialog::isDetection() const {
  const QString kind = taskKind();
  return kind == QLatin1String("detector") || kind == QLatin1String("yolo");
}

void TrainDialog::syncTaskCards() {
  const QString current = taskKind();
  for (QToolButton* button : task_buttons_) {
    if (button == nullptr) {
      continue;
    }
    const bool active = button->property("taskValue").toString() == current;
    button->setChecked(active);
    button->setStyleSheet(active ? QStringLiteral(
        "QToolButton#TaskCard { border: 2px solid %1; border-radius: 6px; "
        "background: %2; color: %3; padding: 8px; }")
        .arg(theme::colour(theme::kAccent).name(),
             theme::colour(theme::kAccentSoft).name(),
             theme::colour(theme::kAccentInk).name())
        : QString());
  }
}

void TrainDialog::applyTaskDefaults() {
  const bool yolo_task = taskKind() == QLatin1String("yolo");
  if (yolo_version_combo_ != nullptr) {
    yolo_version_combo_->setVisible(yolo_task);
    yolo_from_scratch_->setVisible(yolo_task);
  }
  // The architecture combo only applies to classifiers; YOLO uses its own
  // version selector and the patch baseline always uses the small CNN.
  if (arch_combo_ != nullptr) {
    arch_combo_->setEnabled(!yolo_task);
    const int patch_index = arch_combo_->findData(QStringLiteral("smallcnn"));
    if (taskKind() == QLatin1String("detector") && patch_index >= 0) {
      arch_combo_->setCurrentIndex(patch_index);
    }
  }
  if (yolo_task) {
    data_edit_->setPlaceholderText(tr("数据集快照目录（含 dataset.json 与标注框）"));
    input_spin_->setValue(640);
    epochs_spin_->setValue(50);
    batch_spin_->setValue(16);
  } else if (taskKind() == QLatin1String("detector")) {
    data_edit_->setPlaceholderText(tr("数据集快照目录（含 dataset.json 与标注）"));
    input_spin_->setValue(64);
    epochs_spin_->setValue(15);
    batch_spin_->setValue(32);
  } else {
    data_edit_->setPlaceholderText(tr("数据目录（每类一个子文件夹）"));
    input_spin_->setValue(224);
    epochs_spin_->setValue(20);
    batch_spin_->setValue(16);
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
  const QString kind = taskKind();
  const QString script = kind == QLatin1String("yolo") ? QStringLiteral("train_yolo.py")
                         : (kind == QLatin1String("detector") ? QStringLiteral("train_detect.py")
                                                              : QStringLiteral("train.py"));
  if (!QFileInfo::exists(scripts + QLatin1Char('/') + script)) {
    appendLog(tr("找不到训练脚本：%1/%2").arg(scripts, script), theme::colour(theme::kDanger));
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
  if (kind == QLatin1String("yolo")) {
    // An advanced/optional override: point at a pre-converted YOLO dataset
    // (data.yaml) instead of exporting the snapshot. Used for large external
    // datasets like TTPLA where copying images is impractical.
    const QString prepared =
        QSettings().value(QStringLiteral("train/data_yaml")).toString();
    if (!prepared.isEmpty()) {
      job.insert(QStringLiteral("data_yaml"), QDir::toNativeSeparators(prepared));
    }
    job.insert(QStringLiteral("imgsz"), input_spin_->value());
    const QString version = yolo_version_combo_->currentData().toString();
    // From scratch uses the local YAML (no download); otherwise ask for the
    // pretrained .pt (needs network and converges in far fewer epochs).
    job.insert(QStringLiteral("variant"),
               yolo_from_scratch_->isChecked() ? version + QStringLiteral(".yaml")
                                              : version + QStringLiteral(".pt"));
  } else if (kind == QLatin1String("detector")) {
    job.insert(QStringLiteral("patch_size"), input_spin_->value());
    job.insert(QStringLiteral("positives_per_box"), 3);
    job.insert(QStringLiteral("negatives"), 3);
  }

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
  settings.setValue(QStringLiteral("train/detail_task"), task_combo_->currentData());
  settings.setValue(QStringLiteral("train/yolo_version"), yolo_version_combo_->currentData());
  settings.setValue(QStringLiteral("train/arch"), arch_combo_->currentData());
  settings.setValue(QStringLiteral("train/input"), input_spin_->value());
  settings.setValue(QStringLiteral("train/epochs"), epochs_spin_->value());
  settings.setValue(QStringLiteral("train/batch"), batch_spin_->value());
  settings.setValue(QStringLiteral("train/lr"), lr_spin_->value());
  settings.setValue(QStringLiteral("train/device"), device_combo_->currentData());
  settings.setValue(QStringLiteral("python/executable"), python);

  log_->clear();
  const bool yolo_task = taskKind() == QLatin1String("yolo");
  resetMetrics(yolo_task);
  if (tabs_ != nullptr) {
    tabs_->setCurrentIndex(1);  // jump to live monitoring as training begins
  }
  succeeded_ = false;
  open_button_->setEnabled(false);
  last_model_dir_.clear();
  stdout_buffer_.clear();
  progress_->setRange(1, epochs_spin_->value());
  progress_->setValue(0);
  progress_->show();
  status_label_->setText(tr("启动训练进程…"));
  appendLog(QStringLiteral("%1 %2 --job %3")
                .arg(python, script, QFileInfo(job_path).fileName()),
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
  process_->start(python, {script, QStringLiteral("--job"), job_path});
  if (!process_->waitForStarted(5000)) {
    appendLog(tr("无法启动 Python：%1").arg(process_->errorString()),
              theme::colour(theme::kDanger));
    setRunning(false);
  }
}

void TrainDialog::cancel() {
  if (!running_) {
    emit closed();
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
  for (QString line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    line = line.trimmed();
    // Ultralytics floods stderr with model summaries, AMP checks and progress
    // bars. The worker already reports structured progress over stdout, so
    // keep the log readable by surfacing only warnings and errors here.
    const QString lowered = line.toLower();
    if (lowered.contains(QStringLiteral("error")) ||
        lowered.contains(QStringLiteral("warning")) ||
        lowered.contains(QStringLiteral("traceback")) ||
        lowered.contains(QStringLiteral("exception")) ||
        lowered.contains(QStringLiteral("failed"))) {
      appendLog(line, theme::colour(theme::kWarning));
    }
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
    // Classifiers emit "epochs"; YOLO emits "total_epochs". Likewise the
    // quality field is val_accuracy for classifiers and mAP50 for YOLO.
    const int total = event.contains(QStringLiteral("total_epochs"))
                          ? event.value(QStringLiteral("total_epochs")).toInt()
                          : event.value(QStringLiteral("epochs")).toInt();
    const bool yolo = event.contains(QStringLiteral("mAP50"));
    const double loss = yolo ? event.value(QStringLiteral("box_loss")).toDouble()
                             : event.value(QStringLiteral("train_loss")).toDouble();
    const double score = yolo ? event.value(QStringLiteral("mAP50")).toDouble()
                              : event.value(QStringLiteral("val_accuracy")).toDouble();
    progress_->setRange(1, std::max(1, total));
    progress_->setValue(epoch);
    const QString loss_text = loss > 0.0 ? format_metric(loss) : QStringLiteral("—");
    status_label_->setText(tr("第 %1/%2 轮   ·   mAP50 %3")
                               .arg(epoch)
                               .arg(total)
                               .arg(format_metric(score)));
    addMetricRow(event, yolo);
    if (yolo) {
      appendLog(tr("Epoch %1/%2   mAP50 %3   mAP50-95 %4")
                    .arg(epoch)
                    .arg(total)
                    .arg(format_metric(score),
                         format_metric(event.value(QStringLiteral("mAP50_95")).toDouble())),
                theme::colour(theme::kTextDim));
    } else {
      appendLog(tr("Epoch %1/%2   loss %3   train %4   val %5")
                    .arg(epoch)
                    .arg(total)
                    .arg(loss_text,
                         format_metric(event.value(QStringLiteral("train_accuracy")).toDouble()),
                         format_metric(score)),
                theme::colour(theme::kTextDim));
    }
  } else if (name == QStringLiteral("done")) {
    succeeded_ = true;
    last_model_dir_ = event.value(QStringLiteral("model_dir")).toString();
    const bool yolo_done = event.contains(QStringLiteral("mAP50"));
    const double val = yolo_done ? event.value(QStringLiteral("mAP50")).toDouble()
                                 : event.value(QStringLiteral("val_accuracy")).toDouble();
    const double test = event.value(QStringLiteral("test_accuracy")).toDouble();
    const double seconds = event.value(QStringLiteral("seconds")).toDouble();
    status_label_->setText(tr("训练完成 · %1 %2%3 · %4 s")
                               .arg(yolo_done ? tr("mAP50") : tr("验证"))
                               .arg(format_metric(val))
                               .arg(test > 0.0 ? tr(" · 测试 %1").arg(format_metric(test))
                                               : QString())
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

void TrainDialog::resetMetrics(bool yolo) {
  history_.clear();
  if (metrics_table_ != nullptr) {
    metrics_table_->setRowCount(0);
    metrics_table_->setHorizontalHeaderLabels(
        yolo ? QStringList({tr("轮次"), tr("mAP50"), tr("mAP50-95"), tr("box_loss"), tr("cls_loss")})
             : QStringList({tr("轮次"), tr("验证准确率"), tr("训练准确率"), tr("loss"), tr("—")}));
  }
  if (chart_ != nullptr) {
    chart_->setLossLabel(yolo ? tr("box_loss") : tr("loss"));
    chart_->setScoreLabel(yolo ? tr("mAP50") : tr("val_acc"));
    chart_->clear();
  }
}

void TrainDialog::addMetricRow(const QJsonObject& event, bool yolo) {
  if (metrics_table_ == nullptr || chart_ == nullptr) {
    return;
  }
  const int epoch = event.value(QStringLiteral("epoch")).toInt();
  double loss = yolo ? event.value(QStringLiteral("box_loss")).toDouble()
                     : event.value(QStringLiteral("train_loss")).toDouble();
  const double score = yolo ? event.value(QStringLiteral("mAP50")).toDouble()
                            : event.value(QStringLiteral("val_accuracy")).toDouble();
  const double second = yolo ? event.value(QStringLiteral("mAP50_95")).toDouble()
                             : event.value(QStringLiteral("train_accuracy")).toDouble();
  const double cls = yolo ? event.value(QStringLiteral("cls_loss")).toDouble() : 0.0;

  const int row = metrics_table_->rowCount();
  metrics_table_->insertRow(row);
  const auto cell = [this, row](int column, const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    metrics_table_->setItem(row, column, item);
  };
  cell(0, QString::number(epoch));
  cell(1, format_metric(score));
  cell(2, yolo ? format_metric(second) : format_metric(second));
  cell(3, loss > 0.0 ? format_metric(loss) : QStringLiteral("—"));
  cell(4, yolo && cls > 0.0 ? format_metric(cls) : QStringLiteral("—"));

  history_.push_back({static_cast<double>(epoch), loss, score});
  chart_->setData(history_);
  metrics_table_->scrollToBottom();
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
  QWidget::showEvent(event);
  // Headless smoke test: start as soon as the dialog is shown.
  if (qEnvironmentVariableIsSet("FIGTOOLS_TRAIN_AUTOSTART") && !running_) {
    QTimer::singleShot(300, this, &TrainDialog::start);
  }
}


}  // namespace figtools::gui
