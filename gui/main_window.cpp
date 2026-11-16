// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QIcon>
#include <QListWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "figtools/core/color.hpp"
#include "figtools/core/geometry.hpp"
#include "figtools/core/sample_scene.hpp"
#include "figtools/io/image_io.hpp"
#include "figtools/ops/registry.hpp"
#include "histogram_widget.hpp"
#include "image_canvas.hpp"

namespace figtools::gui {
namespace {

constexpr int kPreviewMaxDimension = 1400;

QImage toQImage(const Image& image) {
  if (image.empty()) {
    return {};
  }
  const QImage view(image.data(), image.width(), image.height(),
                    static_cast<int>(image.stride()), QImage::Format_RGBA8888);
  return view.copy();
}

Image makePreviewSource(const Image& source) {
  const int longest = std::max(source.width(), source.height());
  if (longest <= kPreviewMaxDimension) {
    return source;
  }
  const double scale = static_cast<double>(kPreviewMaxDimension) / static_cast<double>(longest);
  const int width = std::max(1, static_cast<int>(std::lround(source.width() * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(source.height() * scale)));
  auto resized = resize(source.view(), width, height, Interp::kBilinear);
  return resized ? std::move(resized.value()) : source;
}

QString format_bytes(std::uint64_t bytes) {
  const double value = static_cast<double>(bytes);
  if (value < 1024.0) {
    return QStringLiteral("%1 B").arg(bytes);
  }
  if (value < 1024.0 * 1024.0) {
    return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
  }
  return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f', 2);
}

QString error_text(const Error& error) {
  return QString::fromStdString(error.message);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(tr("figtools · 图像工作台"));
  resize(1440, 900);
  setAcceptDrops(true);

  preview_timer_ = new QTimer(this);
  preview_timer_->setSingleShot(true);
  preview_timer_->setInterval(120);
  connect(preview_timer_, &QTimer::timeout, this, &MainWindow::runPreview);

  annotations_.set_classes(default_classes());
  if (!annotations_.classes().empty()) {
    active_class_ = annotations_.classes().front().id;
  }

  buildUi();
  buildActions();

  // A default recipe so the first frame is not an empty chain. The values are
  // chosen so the before/after difference is visible without being a caricature.
  Step denoise;
  denoise.op = "median";
  denoise.params.set("radius", 1.0);
  recipe_.steps.push_back(denoise);

  Step contrast;
  contrast.op = "contrast";
  contrast.params.set("factor", 1.22);
  recipe_.steps.push_back(contrast);

  Step sharpen;
  sharpen.op = "unsharp";
  sharpen.params.set("amount", 0.9);
  sharpen.params.set("radius", 1.4);
  sharpen.params.set("threshold", 0.01);
  recipe_.steps.push_back(sharpen);
  rebuildClassList();
  rebuildChain();
  loadSample();
}

void MainWindow::buildUi() {
  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setChildrenCollapsible(false);

  // ---- left: operator chain + annotation ------------------------------------
  auto* left = new QWidget(splitter);
  auto* left_outer = new QVBoxLayout(left);
  left_outer->setContentsMargins(0, 0, 0, 0);
  left_outer->setSpacing(0);
  tabs_ = new QTabWidget(left);
  auto* tabs = tabs_;
  tabs->setDocumentMode(true);
  left_outer->addWidget(tabs);

  auto* chain_page = new QWidget(tabs);
  auto* left_layout = new QVBoxLayout(chain_page);
  left_layout->setContentsMargins(10, 10, 10, 10);
  left_layout->setSpacing(8);

  auto* header = new QHBoxLayout();
  auto* title = new QLabel(tr("处理链"), chain_page);
  QFont title_font = title->font();
  title_font.setBold(true);
  title->setFont(title_font);
  chain_summary_ = new QLabel(tr("0 步"), chain_page);
  chain_summary_->setStyleSheet("color:#8a8a7e;");
  header->addWidget(title);
  header->addStretch(1);
  header->addWidget(chain_summary_);
  left_layout->addLayout(header);

  auto* scroll = new QScrollArea(chain_page);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  chain_container_ = new QWidget(scroll);
  chain_layout_ = new QVBoxLayout(chain_container_);
  chain_layout_->setContentsMargins(0, 0, 0, 0);
  chain_layout_->setSpacing(8);
  chain_layout_->addStretch(1);
  scroll->setWidget(chain_container_);
  left_layout->addWidget(scroll, 1);

  auto* add_button = new QToolButton(chain_page);
  add_button->setText(tr("+ 添加算子"));
  add_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  add_button->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(add_button);
  const auto& registry = Registry::instance();
  std::string current_group;
  for (const Operator* op : registry.all()) {
    if (op->spec.group != current_group) {
      current_group = op->spec.group;
      menu->addSection(QString::fromStdString(current_group));
    }
    auto* action = menu->addAction(QString::fromStdString(op->spec.label + "  " + op->spec.id));
    const std::string id = op->spec.id;
    connect(action, &QAction::triggered, this, [this, id]() { addOperator(QString::fromStdString(id)); });
  }
  add_button->setMenu(menu);
  left_layout->addWidget(add_button);
  tabs->addTab(chain_page, tr("处理"));

  auto* annotate_page = new QWidget(tabs);
  auto* annotate_layout = new QVBoxLayout(annotate_page);
  annotate_layout->setContentsMargins(10, 10, 10, 10);
  annotate_layout->setSpacing(8);

  auto* class_title = new QLabel(tr("类别"), annotate_page);
  class_title->setStyleSheet("color:#54544b; font-weight:bold;");
  annotate_layout->addWidget(class_title);

  class_list_ = new QListWidget(annotate_page);
  class_list_->setMaximumHeight(120);
  class_list_->setStyleSheet("QListWidget { background:#ffffff; border:1px solid #e9e5d8; border-radius:6px; }"
                             "QListWidget::item { padding:5px 6px; }"
                             "QListWidget::item:selected { background:#e6f1fb; color:#0c447c; }");
  annotate_layout->addWidget(class_list_);

  auto* tool_title = new QLabel(tr("工具"), annotate_page);
  tool_title->setStyleSheet("color:#54544b; font-weight:bold;");
  annotate_layout->addWidget(tool_title);

  auto* tool_row = new QHBoxLayout();
  auto* select_tool = new QPushButton(tr("选择"), annotate_page);
  auto* rect_tool = new QPushButton(tr("矩形"), annotate_page);
  auto* polygon_tool = new QPushButton(tr("多边形"), annotate_page);
  for (QPushButton* button : {select_tool, rect_tool, polygon_tool}) {
    button->setCheckable(true);
    button->setMinimumHeight(26);
    tool_row->addWidget(button);
  }
  select_tool->setChecked(true);
  annotate_layout->addLayout(tool_row);

  auto* shape_title = new QLabel(tr("标注（选中后按 Delete 删除）"), annotate_page);
  shape_title->setStyleSheet("color:#54544b; font-weight:bold;");
  annotate_layout->addWidget(shape_title);

  shape_list_ = new QListWidget(annotate_page);
  shape_list_->setStyleSheet("QListWidget { background:#ffffff; border:1px solid #e9e5d8; border-radius:6px; }"
                             "QListWidget::item { padding:4px 6px; }"
                             "QListWidget::item:selected { background:#e6f1fb; color:#0c447c; }");
  annotate_layout->addWidget(shape_list_, 1);

  auto* export_row = new QHBoxLayout();
  auto* coco_button = new QPushButton(tr("导出 COCO"), annotate_page);
  auto* yolo_button = new QPushButton(tr("导出 YOLO"), annotate_page);
  export_row->addWidget(coco_button);
  export_row->addWidget(yolo_button);
  annotate_layout->addLayout(export_row);

  auto* file_row = new QHBoxLayout();
  auto* save_anno = new QPushButton(tr("保存标注"), annotate_page);
  auto* load_anno = new QPushButton(tr("载入标注"), annotate_page);
  file_row->addWidget(save_anno);
  file_row->addWidget(load_anno);
  annotate_layout->addLayout(file_row);

  auto* hint = new QLabel(tr("矩形：拖拽绘制；多边形：逐点单击，双击或回车闭合，Esc 取消。"), annotate_page);
  hint->setWordWrap(true);
  hint->setStyleSheet("color:#8a8a7e; font-size:11px;");
  annotate_layout->addWidget(hint);

  tabs->addTab(annotate_page, tr("标注"));

  connect(class_list_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= annotations_.classes().size()) {
      return;
    }
    active_class_ = annotations_.classes()[static_cast<std::size_t>(row)].id;
    if (canvas_ != nullptr) {
      canvas_->setActiveClass(active_class_);
    }
  });
  connect(select_tool, &QPushButton::clicked, this, [this, select_tool, rect_tool, polygon_tool]() {
    select_tool->setChecked(true);
    rect_tool->setChecked(false);
    polygon_tool->setChecked(false);
    canvas_->setTool(ImageCanvas::Tool::kNone);
  });
  connect(rect_tool, &QPushButton::clicked, this, [this, select_tool, rect_tool, polygon_tool]() {
    select_tool->setChecked(false);
    rect_tool->setChecked(true);
    polygon_tool->setChecked(false);
    canvas_->setTool(ImageCanvas::Tool::kRect);
  });
  connect(polygon_tool, &QPushButton::clicked, this,
          [this, select_tool, rect_tool, polygon_tool]() {
            select_tool->setChecked(false);
            rect_tool->setChecked(false);
            polygon_tool->setChecked(true);
            canvas_->setTool(ImageCanvas::Tool::kPolygon);
          });
  connect(shape_list_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (canvas_ != nullptr) {
      canvas_->setSelectedIndex(row);
    }
  });
  connect(coco_button, &QPushButton::clicked, this, [this]() { exportAnnotations(true); });
  connect(yolo_button, &QPushButton::clicked, this, [this]() { exportAnnotations(false); });
  connect(save_anno, &QPushButton::clicked, this, &MainWindow::saveAnnotations);
  connect(load_anno, &QPushButton::clicked, this, &MainWindow::loadAnnotations);

  // ---- centre: canvas ------------------------------------------------------
  canvas_ = new ImageCanvas(splitter);
  canvas_->setAnnotationSet(&annotations_);
  canvas_->setAnnotationCallback([this]() { refreshAnnotationLists(); });
  canvas_->setStatusCallback([this](const QString& text) { statusBar()->showMessage(text, 1500); });
  canvas_->setZoomCallback([this](double zoom) {
    if (status_zoom_ != nullptr) {
      status_zoom_->setText(tr("缩放 %1%").arg(static_cast<int>(zoom * 100.0)));
    }
  });
  canvas_->setCursorCallback([this](QPoint point, bool inside) {
    if (status_position_ == nullptr) {
      return;
    }
    if (!inside) {
      status_position_->setText(QStringLiteral("—"));
      return;
    }
    status_position_->setText(QStringLiteral("X %1  Y %2").arg(point.x()).arg(point.y()));
  });

  // ---- right: facts --------------------------------------------------------
  auto* right = new QWidget(splitter);
  auto* right_layout = new QVBoxLayout(right);
  right_layout->setContentsMargins(10, 10, 10, 10);
  right_layout->setSpacing(10);

  auto* info_box = new QGroupBox(tr("图像属性"), right);
  auto* info_layout = new QVBoxLayout(info_box);
  info_layout->setContentsMargins(10, 8, 10, 10);
  info_grid_ = new QLabel(info_box);
  info_grid_->setTextFormat(Qt::RichText);
  info_grid_->setWordWrap(true);
  info_layout->addWidget(info_grid_);
  right_layout->addWidget(info_box);

  auto* hist_box = new QGroupBox(tr("直方图"), right);
  auto* hist_layout = new QVBoxLayout(hist_box);
  hist_layout->setContentsMargins(6, 6, 6, 6);
  histogram_ = new HistogramWidget(hist_box);
  hist_layout->addWidget(histogram_);
  right_layout->addWidget(hist_box);

  auto* stats_box = new QGroupBox(tr("统计"), right);
  auto* stats_layout = new QVBoxLayout(stats_box);
  stats_layout->setContentsMargins(10, 8, 10, 10);
  stats_grid_ = new QLabel(stats_box);
  stats_grid_->setTextFormat(Qt::RichText);
  stats_grid_->setWordWrap(true);
  stats_layout->addWidget(stats_grid_);
  right_layout->addWidget(stats_box);
  right_layout->addStretch(1);

  left->setMinimumWidth(300);
  left->setMaximumWidth(360);
  right->setMinimumWidth(250);
  right->setMaximumWidth(320);
  splitter->addWidget(left);
  splitter->addWidget(canvas_);
  splitter->addWidget(right);
  splitter->setStretchFactor(1, 1);

  setCentralWidget(splitter);

  // ---- status bar ----------------------------------------------------------
  status_size_ = new QLabel(tr("—"), this);
  status_zoom_ = new QLabel(tr("缩放 100%"), this);
  status_position_ = new QLabel(QStringLiteral("—"), this);
  status_time_ = new QLabel(tr("—"), this);
  progress_ = new QProgressBar(this);
  progress_->setRange(0, 0);
  progress_->setMaximumWidth(120);
  progress_->setTextVisible(false);
  progress_->hide();

  statusBar()->addWidget(status_size_);
  statusBar()->addPermanentWidget(status_time_);
  statusBar()->addPermanentWidget(status_position_);
  statusBar()->addPermanentWidget(status_zoom_);
  statusBar()->addPermanentWidget(progress_);
}

void MainWindow::buildActions() {
  auto* file_menu = menuBar()->addMenu(tr("文件(&F)"));
  auto* open_action = file_menu->addAction(tr("打开图像…"));
  open_action->setShortcut(QKeySequence::Open);
  open_action->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  connect(open_action, &QAction::triggered, this, [this]() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开图像"), QString(),
        tr("图像 (*.png *.bmp *.ppm *.pgm);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
      loadPath(path);
    }
  });

  auto* sample_action = file_menu->addAction(tr("载入示例图"));
  sample_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
  connect(sample_action, &QAction::triggered, this, &MainWindow::loadSample);

  file_menu->addSeparator();
  auto* export_action = file_menu->addAction(tr("导出结果…"));
  export_action->setShortcut(QKeySequence::Save);
  export_action->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
  connect(export_action, &QAction::triggered, this, &MainWindow::exportImage);

  auto* export_recipe = file_menu->addAction(tr("保存配方…"));
  connect(export_recipe, &QAction::triggered, this, &MainWindow::saveRecipe);
  auto* load_recipe = file_menu->addAction(tr("载入配方…"));
  connect(load_recipe, &QAction::triggered, this, &MainWindow::loadRecipe);

  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("退出"));
  quit_action->setShortcut(QKeySequence::Quit);
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  auto* view_menu = menuBar()->addMenu(tr("视图(&V)"));
  auto* fit_action = view_menu->addAction(tr("适应窗口"));
  fit_action->setShortcut(QKeySequence(QStringLiteral("F")));
  connect(fit_action, &QAction::triggered, this, [this]() { canvas_->fitToWindow(); });
  auto* actual_action = view_menu->addAction(tr("实际像素 1:1"));
  actual_action->setShortcut(QKeySequence(QStringLiteral("1")));
  connect(actual_action, &QAction::triggered, this, [this]() { canvas_->zoomToActualSize(); });

  auto* help_menu = menuBar()->addMenu(tr("帮助(&H)"));
  auto* about_action = help_menu->addAction(tr("关于 figtools"));
  connect(about_action, &QAction::triggered, this, [this]() {
    QMessageBox::about(this, tr("关于 figtools"),
                       tr("<b>figtools</b> 图像工作台<br/>版本 0.2.0<br/><br/>"
                          "内核：确定性算子管线（%1 个算子）<br/>"
                          "对比：分割 / 并排 / 结果三种视图")
                           .arg(static_cast<int>(Registry::instance().size())));
  });

  // Toolbar mirrors the three actions people actually use every minute.
  auto* toolbar = addToolBar(tr("主工具栏"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addAction(open_action);
  toolbar->addAction(sample_action);
  toolbar->addAction(export_action);
  toolbar->addSeparator();

  mode_combo_ = new QComboBox(toolbar);
  mode_combo_->addItem(tr("分割对比"), static_cast<int>(ImageCanvas::Mode::kSplit));
  mode_combo_->addItem(tr("并排对比"), static_cast<int>(ImageCanvas::Mode::kSide));
  mode_combo_->addItem(tr("只看结果"), static_cast<int>(ImageCanvas::Mode::kResult));
  mode_combo_->setMinimumWidth(110);
  connect(mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    canvas_->setMode(static_cast<ImageCanvas::Mode>(mode_combo_->itemData(index).toInt()));
  });
  toolbar->addWidget(mode_combo_);
  toolbar->addWidget(new QLabel(QStringLiteral("  "), toolbar));
  toolbar->addAction(fit_action);
  toolbar->addAction(actual_action);
}

bool MainWindow::loadPath(const QString& path) {
  auto image = load_image(path.toStdString());
  if (!image) {
    QMessageBox::warning(this, tr("无法打开图像"), error_text(image.error()));
    return false;
  }
  source_ = std::move(image.value());
  source_path_ = path;
  annotations_.clear();
  annotations_.set_image_size(0, 0);
  refreshAnnotationLists();
  setWindowTitle(tr("figtools · 图像工作台 — %1").arg(QFileInfo(path).fileName()));
  updateInfoPanel();
  runPreview();
  return true;
}

void MainWindow::loadSample() {
  source_ = make_sample_scene(1200, 800, 2026U);
  source_path_.clear();
  annotations_.clear();
  annotations_.set_image_size(0, 0);
  refreshAnnotationLists();
  setWindowTitle(tr("figtools · 图像工作台 — 示例图"));
  updateInfoPanel();
  runPreview();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event) {
  const auto urls = event->mimeData()->urls();
  if (urls.isEmpty()) {
    return;
  }
  const QString path = urls.first().toLocalFile();
  if (!path.isEmpty()) {
    loadPath(path);
    event->acceptProposedAction();
  }
}

QWidget* MainWindow::buildParamEditor(const ParamSpec& spec, const ParamValue& value,
                                      std::size_t step_index, const QString& key) {
  const std::string key_text = key.toStdString();
  auto* host = new QWidget(chain_container_);
  auto* layout = new QHBoxLayout(host);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  switch (spec.type) {
    case ParamType::kEnum: {
      auto* combo = new QComboBox(host);
      for (const auto& option : spec.enum_values) {
        combo->addItem(QString::fromStdString(option));
      }
      const auto* current = std::get_if<std::string>(&value);
      if (current != nullptr) {
        const int index = combo->findText(QString::fromStdString(*current));
        if (index >= 0) {
          combo->setCurrentIndex(index);
        }
      }
      connect(combo, &QComboBox::currentTextChanged, this,
              [this, step_index, key_text](const QString& text) {
                if (building_chain_) {
                  return;
                }
                recipe_.steps[step_index].params.set(key_text, text.toStdString());
                schedulePreview();
              });
      layout->addWidget(combo, 1);
      break;
    }
    case ParamType::kBool: {
      auto* check = new QCheckBox(host);
      check->setChecked(std::get_if<bool>(&value) != nullptr && std::get<bool>(value));
      connect(check, &QCheckBox::toggled, this, [this, step_index, key_text](bool checked) {
        if (building_chain_) {
          return;
        }
        recipe_.steps[step_index].params.set(key_text, checked);
        schedulePreview();
      });
      layout->addWidget(check, 1);
      break;
    }
    case ParamType::kInteger: {
      auto* spin = new QSpinBox(host);
      spin->setRange(static_cast<int>(spec.min_value), static_cast<int>(spec.max_value));
      spin->setSingleStep(1);
      spin->setValue(static_cast<int>(std::lround(std::get<double>(value))));
      if (!spec.unit.empty()) {
        spin->setSuffix(QStringLiteral(" ") + QString::fromStdString(spec.unit));
      }
      connect(spin, &QSpinBox::valueChanged, this, [this, step_index, key_text](int number) {
        if (building_chain_) {
          return;
        }
        recipe_.steps[step_index].params.set(key_text, static_cast<double>(number));
        schedulePreview();
      });
      layout->addWidget(spin, 1);
      break;
    }
    case ParamType::kNumber:
    default: {
      auto* slider = new QSlider(Qt::Horizontal, host);
      slider->setRange(0, 1000);
      auto* spin = new QDoubleSpinBox(host);
      spin->setDecimals(3);
      spin->setRange(spec.min_value, spec.max_value);
      spin->setSingleStep((spec.max_value - spec.min_value) / 200.0);
      spin->setValue(std::get<double>(value));
      spin->setFixedWidth(76);
      spin->setKeyboardTracking(false);
      if (!spec.unit.empty()) {
        spin->setSuffix(QStringLiteral(" ") + QString::fromStdString(spec.unit));
      }
      const double span = std::max(1e-9, spec.max_value - spec.min_value);
      slider->setValue(static_cast<int>(std::lround((std::get<double>(value) - spec.min_value) /
                                                    span * 1000.0)));

      connect(slider, &QSlider::valueChanged, this,
              [this, spin, spec, span, step_index, key_text](int position) {
                const double number = spec.min_value + span * static_cast<double>(position) / 1000.0;
                const QSignalBlocker blocker(spin);
                spin->setValue(number);
                if (building_chain_) {
                  return;
                }
                recipe_.steps[step_index].params.set(key_text, number);
                schedulePreview();
              });
      connect(spin, &QDoubleSpinBox::valueChanged, this,
              [this, slider, spec, span, step_index, key_text](double number) {
                const QSignalBlocker blocker(slider);
                slider->setValue(static_cast<int>(
                    std::lround((number - spec.min_value) / span * 1000.0)));
                if (building_chain_) {
                  return;
                }
                recipe_.steps[step_index].params.set(key_text, number);
                schedulePreview();
              });

      layout->addWidget(slider, 1);
      layout->addWidget(spin, 0);
      break;
    }
  }
  host->setToolTip(QString::fromStdString(spec.doc));
  return host;
}

void MainWindow::rebuildChain() {
  building_chain_ = true;

  // Drop everything except the trailing stretch.
  while (chain_layout_->count() > 1) {
    QLayoutItem* item = chain_layout_->takeAt(0);
    if (item->widget() != nullptr) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  const auto& registry = Registry::instance();
  for (std::size_t i = 0; i < recipe_.steps.size(); ++i) {
    Step& step = recipe_.steps[i];
    const Operator* op = registry.find(step.op);
    const QString title = op == nullptr
                              ? QString::fromStdString(step.op)
                              : QStringLiteral("%1. %2").arg(i + 1).arg(
                                    QString::fromStdString(op->spec.label));

    auto* box = new QGroupBox(title, chain_container_);
    box->setCheckable(true);
    box->setChecked(step.enabled);
    box->setToolTip(op == nullptr ? QString() : QString::fromStdString(op->spec.summary));
    connect(box, &QGroupBox::toggled, this, [this, i](bool on) {
      if (building_chain_) {
        return;
      }
      recipe_.steps[i].enabled = on;
      schedulePreview();
    });

    auto* box_layout = new QVBoxLayout(box);
    box_layout->setContentsMargins(8, 6, 8, 8);
    box_layout->setSpacing(4);

    if (op != nullptr) {
      auto normalized = registry.normalize(op->spec, step.params);
      const Params& params = normalized ? normalized.value() : step.params;
      for (const ParamSpec& param : op->spec.params) {
        auto* row = new QWidget(box);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(6);

        auto* label = new QLabel(QString::fromStdString(param.label), row);
        label->setMinimumWidth(64);
        label->setStyleSheet("color:#54544b;");
        row_layout->addWidget(label);

        ParamValue current = 0.0;
        if (params.has(param.key)) {
          current = params.values().at(param.key);
        }
        row_layout->addWidget(
            buildParamEditor(param, current, i, QString::fromStdString(param.key)), 1);
        box_layout->addWidget(row);
      }
    }

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    const auto make_button = [box](const QString& text, const QString& tip) {
      auto* button = new QToolButton(box);
      button->setText(text);
      button->setToolTip(tip);
      button->setAutoRaise(true);
      return button;
    };
    auto* up = make_button(QStringLiteral("↑"), tr("上移"));
    auto* down = make_button(QStringLiteral("↓"), tr("下移"));
    auto* remove = make_button(QStringLiteral("✕"), tr("删除"));
    actions->addWidget(up);
    actions->addWidget(down);
    actions->addWidget(remove);
    box_layout->addLayout(actions);

    connect(up, &QToolButton::clicked, this, [this, i]() { moveStep(i, -1); });
    connect(down, &QToolButton::clicked, this, [this, i]() { moveStep(i, 1); });
    connect(remove, &QToolButton::clicked, this, [this, i]() { removeStep(i); });

    chain_layout_->insertWidget(static_cast<int>(i), box);
  }

  building_chain_ = false;
  chain_summary_->setText(tr("%1 步").arg(recipe_.steps.size()));
  schedulePreview();
}

void MainWindow::addOperator(const QString& id) {
  Step step;
  step.op = id.toStdString();
  recipe_.steps.push_back(step);
  rebuildChain();
}

void MainWindow::removeStep(std::size_t index) {
  if (index >= recipe_.steps.size()) {
    return;
  }
  recipe_.steps.erase(recipe_.steps.begin() + static_cast<std::ptrdiff_t>(index));
  rebuildChain();
}

void MainWindow::moveStep(std::size_t index, int delta) {
  const int target = static_cast<int>(index) + delta;
  if (target < 0 || target >= static_cast<int>(recipe_.steps.size())) {
    return;
  }
  std::swap(recipe_.steps[index], recipe_.steps[static_cast<std::size_t>(target)]);
  rebuildChain();
}

void MainWindow::schedulePreview() {
  if (source_.empty()) {
    return;
  }
  preview_timer_->start();
}

void MainWindow::applyRecipeToSource(Image* output, double* milliseconds) {
  QElapsedTimer timer;
  timer.start();
  auto result = execute(recipe_, source_.view(), Registry::instance());
  if (milliseconds != nullptr) {
    *milliseconds = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  }
  if (!result) {
    QMessageBox::warning(this, tr("处理失败"), error_text(result.error()));
    return;
  }
  *output = std::move(result.value().image);
}

void MainWindow::runPreview() {
  if (source_.empty()) {
    return;
  }
  const Image preview_source = makePreviewSource(source_);

  setBusy(true, tr("正在处理…"));
  QElapsedTimer timer;
  timer.start();
  auto result = execute(recipe_, preview_source.view(), Registry::instance());
  last_milliseconds_ = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  setBusy(false);

  if (!result) {
    statusBar()->showMessage(error_text(result.error()), 5000);
    return;
  }

  const int result_width = result.value().image.width();
  const int result_height = result.value().image.height();
  if (annotations_.image_width() != 0 && annotations_.size() > 0 &&
      (annotations_.image_width() != result_width || annotations_.image_height() != result_height)) {
    statusBar()->showMessage(tr("输出尺寸已变化，已有标注仍基于 %1 × %2")
                                 .arg(annotations_.image_width())
                                 .arg(annotations_.image_height()),
                             6000);
  }
  annotations_.set_image_size(result_width, result_height);

  const QImage before = toQImage(preview_source);
  const QImage after = toQImage(result.value().image);
  canvas_->setImages(before, after);
  if (first_frame_) {
    // Deterministic framing: the first frame always fits the viewport, even if
    // a stray scroll event arrived while the window was being mapped.
    canvas_->fitToWindow();
    first_frame_ = false;
  }
  histogram_->setImage(after);

  status_size_->setText(tr("%1 × %2  ·  预览 %3 × %4")
                            .arg(source_.width())
                            .arg(source_.height())
                            .arg(result.value().image.width())
                            .arg(result.value().image.height()));
  status_time_->setText(tr("%1 ms · %2 步").arg(last_milliseconds_, 0, 'f', 1).arg(
      result.value().trace.size()));
  updateInfoPanel();
}

void MainWindow::setBusy(bool busy, const QString& message) {
  if (busy) {
    progress_->show();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    if (!message.isEmpty()) {
      statusBar()->showMessage(message);
    }
  } else {
    progress_->hide();
    QApplication::restoreOverrideCursor();
  }
}

void MainWindow::exportImage() {
  if (source_.empty()) {
    return;
  }
  QString suggestion = source_path_.isEmpty() ? QStringLiteral("figtools_output.png")
                                              : QFileInfo(source_path_).completeBaseName() +
                                                    QStringLiteral("_processed.png");
  const QString path = QFileDialog::getSaveFileName(
      this, tr("导出结果"), suggestion, tr("PNG (*.png);;BMP (*.bmp);;PPM (*.ppm)"));
  if (path.isEmpty()) {
    return;
  }

  setBusy(true, tr("按原始分辨率处理…"));
  Image output;
  double milliseconds = 0.0;
  applyRecipeToSource(&output, &milliseconds);
  setBusy(false);

  if (output.empty()) {
    return;
  }
  auto status = save_image(output, path.toStdString());
  if (!status) {
    QMessageBox::warning(this, tr("导出失败"), error_text(status.error()));
    return;
  }
  statusBar()->showMessage(
      tr("已导出 %1（%2 × %3，%4 ms）")
          .arg(QFileInfo(path).fileName())
          .arg(output.width())
          .arg(output.height())
          .arg(milliseconds, 0, 'f', 1),
      5000);
}

void MainWindow::saveRecipe() {
  const QString path = QFileDialog::getSaveFileName(this, tr("保存配方"),
                                                    QStringLiteral("recipe.json"),
                                                    tr("配方 (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("保存失败"), file.errorString());
    return;
  }
  const std::string text = recipe_.to_json(2);
  file.write(text.data(), static_cast<qint64>(text.size()));
  statusBar()->showMessage(tr("配方已保存：%1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::loadRecipe() {
  const QString path =
      QFileDialog::getOpenFileName(this, tr("载入配方"), QString(), tr("配方 (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("打开失败"), file.errorString());
    return;
  }
  const QByteArray bytes = file.readAll();
  auto recipe = Recipe::from_json(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  if (!recipe) {
    QMessageBox::warning(this, tr("配方无效"), error_text(recipe.error()));
    return;
  }
  recipe_ = std::move(recipe.value());
  rebuildChain();
  statusBar()->showMessage(tr("已载入配方：%1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::updateInfoPanel() {
  if (source_.empty()) {
    info_grid_->setText(tr("<span style='color:#8a8a7e'>尚未载入图像</span>"));
    stats_grid_->setText(tr("<span style='color:#8a8a7e'>—</span>"));
    return;
  }

  QString name = source_path_.isEmpty() ? tr("示例图") : QFileInfo(source_path_).fileName();
  double mean_r = 0.0;
  double mean_g = 0.0;
  double mean_b = 0.0;
  double sum_lap = 0.0;
  int lap_count = 0;
  const ImageView view = source_.view();
  for (int y = 0; y < view.height(); ++y) {
    for (int x = 0; x < view.width(); ++x) {
      const Rgba colour = view.at(x, y);
      mean_r += colour.r;
      mean_g += colour.g;
      mean_b += colour.b;
    }
  }
  for (int y = 1; y < view.height() - 1; y += 2) {
    for (int x = 1; x < view.width() - 1; x += 2) {
      const int centre = static_cast<int>(luma(view.at(x, y)));
      const int up = static_cast<int>(luma(view.at(x, y - 1)));
      const int down = static_cast<int>(luma(view.at(x, y + 1)));
      const int left = static_cast<int>(luma(view.at(x - 1, y)));
      const int right = static_cast<int>(luma(view.at(x + 1, y)));
      sum_lap += std::abs(4 * centre - up - down - left - right);
      ++lap_count;
    }
  }
  const double pixels = static_cast<double>(view.size().pixel_count());
  const double sharpness = lap_count > 0 ? sum_lap / static_cast<double>(lap_count) : 0.0;

  const QString row = QStringLiteral(
      "<tr><td style='color:#8a8a7e;padding-right:10px'>%1</td>"
      "<td align='right' style='font-family:Consolas;color:#1c1c19'>%2</td></tr>");

  QString html = QStringLiteral("<table cellspacing='0' cellpadding='3' width='100%'>");
  html += row.arg(tr("文件")).arg(name.toHtmlEscaped());
  html += row.arg(tr("尺寸")).arg(QStringLiteral("%1 × %2").arg(source_.width()).arg(source_.height()));
  html += row.arg(tr("通道")).arg(tr("RGBA · 8 bit"));
  html += row.arg(tr("像素")).arg(QStringLiteral("%1 万").arg(pixels / 10000.0, 0, 'f', 1));
  html += row.arg(tr("内存")).arg(format_bytes(source_.byte_size()));
  html += QStringLiteral("</table>");
  info_grid_->setText(html);

  QString stats = QStringLiteral("<table cellspacing='0' cellpadding='3' width='100%'>");
  stats += row.arg(tr("均值 R")).arg(static_cast<int>(mean_r / pixels));
  stats += row.arg(tr("均值 G")).arg(static_cast<int>(mean_g / pixels));
  stats += row.arg(tr("均值 B")).arg(static_cast<int>(mean_b / pixels));
  stats += row.arg(tr("锐度")).arg(sharpness, 0, 'f', 1);
  stats += QStringLiteral("</table>");
  stats_grid_->setText(stats);
}


void MainWindow::rebuildClassList() {
  if (class_list_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(class_list_);
  class_list_->clear();
  for (const LabelClass& label : annotations_.classes()) {
    QPixmap swatch(10, 10);
    swatch.fill(QColor(QString::fromStdString(label.color)));
    auto* item = new QListWidgetItem(QIcon(swatch), QString::fromStdString(label.name), class_list_);
    item->setData(Qt::UserRole, label.id);
  }
  if (class_list_->count() > 0) {
    class_list_->setCurrentRow(0);
    active_class_ = annotations_.classes().front().id;
    if (canvas_ != nullptr) {
      canvas_->setActiveClass(active_class_);
    }
  }
}

void MainWindow::refreshAnnotationLists() {
  if (shape_list_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(shape_list_);
  shape_list_->clear();
  for (std::size_t i = 0; i < annotations_.size(); ++i) {
    const Annotation& annotation = annotations_.items()[i];
    const LabelClass* label = annotations_.find_class(annotation.class_id);
    const QString name = label != nullptr ? QString::fromStdString(label->name)
                                          : tr("类别 %1").arg(annotation.class_id);
    const QString type = annotation.shape.type == ShapeType::kRect ? tr("矩形") : tr("多边形");
    shape_list_->addItem(QStringLiteral("%1. %2 · %3").arg(i + 1).arg(name, type));
  }
  const int selected = canvas_ != nullptr ? canvas_->selected_index() : -1;
  if (selected >= 0 && selected < shape_list_->count()) {
    shape_list_->setCurrentRow(selected);
  }
  statusBar()->showMessage(tr("标注 %1 个").arg(annotations_.size()), 2000);
}

void MainWindow::exportAnnotations(bool coco) {
  if (annotations_.size() == 0) {
    QMessageBox::information(this, tr("没有标注"), tr("请先在画布上绘制至少一个标注。"));
    return;
  }
  const QString suffix = coco ? QStringLiteral("coco.json") : QStringLiteral("labels.txt");
  const QString filter = coco ? tr("COCO JSON (*.json)") : tr("YOLO 标签 (*.txt)");
  const QString path = QFileDialog::getSaveFileName(this, tr("导出标注"), suffix, filter);
  if (path.isEmpty()) {
    return;
  }

  std::string text;
  if (coco) {
    const QString name = source_path_.isEmpty() ? QStringLiteral("image.png")
                                                : QFileInfo(source_path_).fileName();
    text = annotations_.to_coco_json(name.toStdString());
  } else {
    text = annotations_.to_yolo_txt();
    if (text.empty()) {
      QMessageBox::warning(this, tr("无法导出"),
                           tr("YOLO 需要已知的图像尺寸，请先载入图像并完成一次处理。"));
      return;
    }
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("导出失败"), file.errorString());
    return;
  }
  file.write(text.data(), static_cast<qint64>(text.size()));
  statusBar()->showMessage(tr("已导出 %1 个标注 → %2").arg(annotations_.size()).arg(
                               QFileInfo(path).fileName()),
                           4000);
}

void MainWindow::saveAnnotations() {
  const QString path = QFileDialog::getSaveFileName(this, tr("保存标注"),
                                                    QStringLiteral("annotations.json"),
                                                    tr("标注 (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("保存失败"), file.errorString());
    return;
  }
  const std::string text = annotations_.to_json(2);
  file.write(text.data(), static_cast<qint64>(text.size()));
  statusBar()->showMessage(tr("标注已保存：%1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::loadAnnotations() {
  const QString path = QFileDialog::getOpenFileName(this, tr("载入标注"), QString(),
                                                    tr("标注 (*.json)"));
  if (path.isEmpty()) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("打开失败"), file.errorString());
    return;
  }
  const QByteArray bytes = file.readAll();
  auto parsed = AnnotationSet::from_json(
      std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  if (!parsed) {
    QMessageBox::warning(this, tr("标注文件无效"), error_text(parsed.error()));
    return;
  }
  annotations_ = std::move(parsed.value());
  rebuildClassList();
  refreshAnnotationLists();
  if (canvas_ != nullptr) {
    canvas_->setAnnotationSet(&annotations_);
  }
  statusBar()->showMessage(tr("已载入标注：%1").arg(QFileInfo(path).fileName()), 4000);
}


void MainWindow::addDemoAnnotations() {
  if (annotations_.image_width() <= 0 || annotations_.image_height() <= 0) {
    return;
  }
  const float width = static_cast<float>(annotations_.image_width());
  const float height = static_cast<float>(annotations_.image_height());

  Annotation scratch;
  scratch.class_id = annotations_.classes().size() > 0 ? annotations_.classes()[0].id : 0;
  scratch.shape.type = ShapeType::kRect;
  scratch.shape.rect = RectF{width * 0.655F, height * 0.22F, width * 0.10F, height * 0.55F};
  annotations_.add(scratch);

  Annotation ring;
  ring.class_id = annotations_.classes().size() > 1 ? annotations_.classes()[1].id : 0;
  ring.shape.type = ShapeType::kPolygon;
  ring.shape.points = {
      Point2f{width * 0.255F, height * 0.33F}, Point2f{width * 0.425F, height * 0.31F},
      Point2f{width * 0.432F, height * 0.66F}, Point2f{width * 0.262F, height * 0.68F}};
  annotations_.add(ring);

  if (tabs_ != nullptr) {
    tabs_->setCurrentIndex(1);
  }
  if (canvas_ != nullptr) {
    canvas_->setSelectedIndex(0);
  }
  refreshAnnotationLists();
}

}  // namespace figtools::gui
