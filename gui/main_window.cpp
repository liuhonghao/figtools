// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "figtools/core/color.hpp"
#include "figtools/core/geometry.hpp"
#include "figtools/core/sample_scene.hpp"
#include "figtools/io/image_io.hpp"
#include "figtools/ops/registry.hpp"
#include "histogram_widget.hpp"
#include "batch_dialog.hpp"
#include "image_canvas.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "0.5.0"
#endif

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

QString error_text(const Error& error) { return QString::fromStdString(error.message); }

// Re-evaluates style-sheet rules that depend on a dynamic property.
void restyle(QWidget* widget) {
  if (widget == nullptr) {
    return;
  }
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  widget->update();
}

QFrame* vertical_rule(QWidget* parent) {
  auto* rule = new QFrame(parent);
  rule->setFixedWidth(1);
  rule->setMinimumHeight(18);
  rule->setStyleSheet(QStringLiteral("background:%1; border:none;").arg(theme::kLine));
  return rule;
}

QFrame* segmented(QWidget* parent, QHBoxLayout** inner) {
  auto* frame = new QFrame(parent);
  frame->setObjectName(QStringLiteral("Segmented"));
  auto* layout = new QHBoxLayout(frame);
  layout->setContentsMargins(2, 2, 2, 2);
  layout->setSpacing(2);
  *inner = layout;
  return frame;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(tr("figtools — 图像工作台"));
  resize(1440, 900);
  setAcceptDrops(true);

  QSettings settings;
  if (settings.contains(QStringLiteral("window/geometry"))) {
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
  }
  last_open_dir_ = settings.value(QStringLiteral("paths/last_open")).toString();

  preview_timer_ = new QTimer(this);
  preview_timer_->setSingleShot(true);
  preview_timer_->setInterval(120);
  connect(preview_timer_, &QTimer::timeout, this, &MainWindow::runPreview);

  annotations_.set_classes(default_classes());
  if (!annotations_.classes().empty()) {
    active_class_ = annotations_.classes().front().id;
  }

  buildActions();
  buildUi();

  // A default recipe so the first frame is not an empty chain. The values are
  // chosen so the before/after difference is visible without being a caricature.
  Step denoise;
  denoise.op = "median";
  denoise.params.set("radius", 1.4);
  recipe_.steps.push_back(denoise);

  Step contrast;
  contrast.op = "contrast";
  contrast.params.set("factor", 1.26);
  recipe_.steps.push_back(contrast);

  Step sharpen;
  sharpen.op = "unsharp";
  sharpen.params.set("amount", 1.0);
  sharpen.params.set("radius", 1.5);
  sharpen.params.set("threshold", 0.01);
  recipe_.steps.push_back(sharpen);

  rebuildClassList();
  rebuildChain();
  loadSample();
}

// ---------------------------------------------------------------------------
// Actions and menus
// ---------------------------------------------------------------------------

void MainWindow::buildActions() {
  auto* file_menu = menuBar()->addMenu(tr("文件(&F)"));
  open_action_ = file_menu->addAction(tr("打开图像…"));
  open_action_->setShortcut(QKeySequence::Open);
  connect(open_action_, &QAction::triggered, this, [this]() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开图像"), last_open_dir_,
        tr("图像 (*.png *.bmp *.ppm *.pgm);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
      loadPath(path);
    }
  });

  sample_action_ = file_menu->addAction(tr("载入示例图"));
  sample_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
  connect(sample_action_, &QAction::triggered, this, &MainWindow::loadSample);

  file_menu->addSeparator();
  export_action_ = file_menu->addAction(tr("导出结果…"));
  export_action_->setShortcut(QKeySequence::Save);
  connect(export_action_, &QAction::triggered, this, &MainWindow::exportImage);

  auto* batch_action = file_menu->addAction(tr("批量处理…"));
  batch_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
  connect(batch_action, &QAction::triggered, this, [this]() { openBatchDialog(); });

  auto* export_recipe = file_menu->addAction(tr("保存配方…"));
  connect(export_recipe, &QAction::triggered, this, &MainWindow::saveRecipe);
  auto* load_recipe = file_menu->addAction(tr("载入配方…"));
  connect(load_recipe, &QAction::triggered, this, &MainWindow::loadRecipe);

  file_menu->addSeparator();
  auto* quit_action = file_menu->addAction(tr("退出"));
  quit_action->setShortcut(QKeySequence::Quit);
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  auto* view_menu = menuBar()->addMenu(tr("视图(&V)"));
  fit_action_ = view_menu->addAction(tr("适应窗口"));
  fit_action_->setShortcut(QKeySequence(QStringLiteral("F")));
  connect(fit_action_, &QAction::triggered, this, [this]() {
    if (canvas_ != nullptr) {
      canvas_->fitToWindow();
    }
  });
  actual_action_ = view_menu->addAction(tr("实际像素 1:1"));
  actual_action_->setShortcut(QKeySequence(QStringLiteral("1")));
  connect(actual_action_, &QAction::triggered, this, [this]() {
    if (canvas_ != nullptr) {
      canvas_->zoomToActualSize();
    }
  });

  auto* help_menu = menuBar()->addMenu(tr("帮助(&H)"));
  auto* about_action = help_menu->addAction(tr("关于 figtools"));
  connect(about_action, &QAction::triggered, this, [this]() {
    QMessageBox::about(this, tr("关于 figtools"),
                       tr("<b>figtools</b> 图像工作台<br/>版本 %1<br/><br/>"
                          "确定性算子管线（%2 个算子）<br/>"
                          "对比：分割 / 并排 / 结果三种视图<br/>"
                          "标注、数据集划分与快照导出")
                           .arg(QStringLiteral(FIGTOOLS_VERSION))
                           .arg(static_cast<int>(Registry::instance().size())));
  });
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* outer = new QVBoxLayout(central);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  buildTopBar(outer);

  auto* splitter = new QSplitter(Qt::Horizontal, central);
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(1);

  // ---- left ---------------------------------------------------------------
  auto* left = new QFrame(splitter);
  left->setObjectName(QStringLiteral("PanelLeft"));
  auto* left_layout = new QVBoxLayout(left);
  left_layout->setContentsMargins(0, 0, 0, 10);
  left_layout->setSpacing(0);

  tabs_ = new QTabWidget(left);
  tabs_->setDocumentMode(true);
  tabs_->tabBar()->setExpanding(false);
  tabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
  left_layout->addWidget(tabs_, 1);

  tabs_->addTab(buildChainPage(tabs_), tr("处理"));
  tabs_->addTab(buildAnnotatePage(tabs_), tr("标注"));
  tabs_->addTab(buildDatasetPage(tabs_), tr("数据集"));

  // ---- centre -------------------------------------------------------------
  canvas_ = new ImageCanvas(splitter);
  canvas_->setAnnotationSet(&annotations_);
  canvas_->setAnnotationCallback([this]() { refreshAnnotationLists(); });
  canvas_->setStatusCallback([this](const QString& text) { notify(text, 1500); });
  canvas_->setZoomCallback([this](double zoom) {
    if (status_zoom_ != nullptr) {
      status_zoom_->setText(QStringLiteral("%1%").arg(static_cast<int>(zoom * 100.0)));
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

  auto* right = buildFactsPanel(splitter);

  right->setMinimumWidth(262);
  right->setMaximumWidth(300);
  left->setMinimumWidth(304);
  left->setMaximumWidth(360);
  splitter->addWidget(left);
  splitter->addWidget(canvas_);
  splitter->addWidget(right);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setStretchFactor(2, 0);
  splitter->setSizes({330, 840, 270});

  outer->addWidget(splitter, 1);
  setCentralWidget(central);

  // ---- status bar ---------------------------------------------------------
  statusBar()->setSizeGripEnabled(false);
  status_file_ = new QLabel(tr("示例图"), this);
  status_file_->setObjectName(QStringLiteral("StatusValue"));
  status_message_ = new QLabel(QString(), this);
  status_message_->setObjectName(QStringLiteral("StatusMessage"));
  status_message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  message_timer_ = new QTimer(this);
  message_timer_->setSingleShot(true);
  connect(message_timer_, &QTimer::timeout, this, [this]() { status_message_->clear(); });
  status_size_ = new QLabel(tr("—"), this);
  status_time_ = new QLabel(tr("—"), this);
  status_position_ = new QLabel(QStringLiteral("—"), this);
  status_zoom_ = new QLabel(QStringLiteral("100%"), this);
  status_zoom_->setObjectName(QStringLiteral("StatusValue"));
  progress_ = new QProgressBar(this);
  progress_->setObjectName(QStringLiteral("Status"));
  progress_->setRange(0, 0);
  progress_->setMaximumWidth(96);
  progress_->setTextVisible(false);
  progress_->hide();

  const auto separator = [this]() {
    auto* rule = new QFrame(this);
    rule->setFixedSize(1, 11);
    rule->setStyleSheet(QStringLiteral("background:%1;").arg(theme::kLine));
    return rule;
  };

  statusBar()->addWidget(status_file_);
  auto* status_gap = new QWidget(this);
  status_gap->setFixedWidth(12);
  statusBar()->addWidget(status_gap);
  statusBar()->addWidget(status_message_, 1);
  statusBar()->addWidget(progress_);
  statusBar()->addPermanentWidget(status_position_);
  statusBar()->addPermanentWidget(separator());
  statusBar()->addPermanentWidget(status_zoom_);
  statusBar()->addPermanentWidget(separator());
  statusBar()->addPermanentWidget(status_time_);
  statusBar()->addPermanentWidget(separator());
  statusBar()->addPermanentWidget(status_size_);
  statusBar()->setContentsMargins(8, 0, 12, 0);
}

void MainWindow::buildTopBar(QVBoxLayout* outer) {
  auto* bar = new QWidget(this);
  bar->setObjectName(QStringLiteral("TopBar"));
  bar->setFixedHeight(46);
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(14, 0, 14, 0);
  layout->setSpacing(6);

  auto* mark = new QLabel(bar);
  mark->setFixedSize(7, 7);
  mark->setStyleSheet(QStringLiteral("background:%1; border-radius:3px;").arg(theme::kAccent));

  auto* brand = new QLabel(QStringLiteral("figtools"), bar);
  brand->setObjectName(QStringLiteral("Brand"));
  brand->setFont(theme::ui_font(13, QFont::DemiBold));
  auto* brand_sub = new QLabel(tr("图像工作台"), bar);
  brand_sub->setObjectName(QStringLiteral("BrandSub"));
  brand_sub->setFont(theme::ui_font(11));

  layout->addWidget(mark);
  layout->addSpacing(2);
  layout->addWidget(brand);
  layout->addSpacing(6);
  layout->addWidget(brand_sub);
  layout->addSpacing(10);
  layout->addWidget(vertical_rule(bar));

  auto* open_button = new QToolButton(bar);
  open_button->setDefaultAction(open_action_);
  open_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  open_button->setIcon(theme::icon(theme::Glyph::kOpen));
  open_button->setCursor(Qt::PointingHandCursor);

  auto* sample_button = new QToolButton(bar);
  sample_button->setDefaultAction(sample_action_);
  sample_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  sample_button->setIcon(theme::icon(theme::Glyph::kSample));
  sample_button->setCursor(Qt::PointingHandCursor);

  auto* export_button = new QToolButton(bar);
  export_button->setDefaultAction(export_action_);
  export_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  export_button->setIcon(theme::icon(theme::Glyph::kExport));
  export_button->setCursor(Qt::PointingHandCursor);

  layout->addSpacing(4);
  layout->addWidget(open_button);
  layout->addWidget(sample_button);
  layout->addSpacing(2);
  layout->addWidget(vertical_rule(bar));
  layout->addWidget(export_button);
  layout->addStretch(1);

  // Compare mode: one segmented control, always visible, one click away.
  QHBoxLayout* mode_layout = nullptr;
  auto* mode_frame = segmented(bar, &mode_layout);
  auto* mode_group = new QButtonGroup(mode_frame);
  mode_group->setExclusive(true);

  const struct {
    const char* label;
    ImageCanvas::Mode mode;
  } modes[] = {
      {QT_TR_NOOP("分割"), ImageCanvas::Mode::kSplit},
      {QT_TR_NOOP("并排"), ImageCanvas::Mode::kSide},
      {QT_TR_NOOP("结果"), ImageCanvas::Mode::kResult},
  };
  for (const auto& entry : modes) {
    auto* button = new QToolButton(mode_frame);
    button->setText(tr(entry.label));
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setCursor(Qt::PointingHandCursor);
    button->setFont(theme::ui_font(11));
    button->setMinimumWidth(46);
    button->setProperty("mode", static_cast<int>(entry.mode));
    mode_layout->addWidget(button);
    mode_group->addButton(button, static_cast<int>(entry.mode));
  }
  if (auto* first = mode_group->button(static_cast<int>(ImageCanvas::Mode::kSplit))) {
    first->setChecked(true);
  }
  connect(mode_group, &QButtonGroup::idClicked, this, [this](int id) { setCompareMode(id); });

  layout->addWidget(mode_frame);
  layout->addSpacing(6);
  layout->addWidget(vertical_rule(bar));

  auto* fit_button = new QToolButton(bar);
  fit_button->setText(tr("适应"));
  fit_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  fit_button->setFont(theme::ui_font(11));
  fit_button->setToolTip(tr("适应窗口 (F)"));
  fit_button->setCursor(Qt::PointingHandCursor);
  connect(fit_button, &QToolButton::clicked, this, [this]() {
    if (canvas_ != nullptr) {
      canvas_->fitToWindow();
    }
  });

  auto* actual_button = new QToolButton(bar);
  actual_button->setText(QStringLiteral("1:1"));
  actual_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  actual_button->setFont(theme::mono_font(11));
  actual_button->setToolTip(tr("实际像素 (1)"));
  actual_button->setCursor(Qt::PointingHandCursor);
  connect(actual_button, &QToolButton::clicked, this, [this]() {
    if (canvas_ != nullptr) {
      canvas_->zoomToActualSize();
    }
  });

  layout->addWidget(fit_button);
  layout->addWidget(actual_button);

  outer->addWidget(bar);
}

void MainWindow::openBatchDialog(const QString& suggested_input) {
  QString suggested = suggested_input;
  if (suggested.isEmpty()) {
    suggested = last_open_dir_;
  }
  if (!source_path_.isEmpty()) {
    suggested = QFileInfo(source_path_).absolutePath();
  }
  BatchDialog dialog(recipe_, suggested, this);
  dialog.exec();
}

void MainWindow::prepareForCapture() {
  if (canvas_ != nullptr) {
    canvas_->fitToWindow();
  }
}

void MainWindow::setCompareMode(int mode) {
  if (canvas_ != nullptr) {
    canvas_->setMode(static_cast<ImageCanvas::Mode>(mode));
  }
}

// ---------------------------------------------------------------------------
// Left panel: processing chain
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildChainPage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(9);

  auto* header = new QHBoxLayout();
  header->setSpacing(8);
  header->addWidget(theme::section_label(tr("管线"), page));
  header->addStretch(1);
  chain_summary_ = new QLabel(tr("0 步"), page);
  chain_summary_->setObjectName(QStringLiteral("Meta"));
  chain_summary_->setFont(theme::mono_font(10));
  header->addWidget(chain_summary_);
  layout->addLayout(header);

  auto* scroll = new QScrollArea(page);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  chain_container_ = new QWidget(scroll);
  chain_layout_ = new QVBoxLayout(chain_container_);
  chain_layout_->setContentsMargins(0, 0, 2, 0);
  chain_layout_->setSpacing(8);
  chain_layout_->addStretch(1);
  scroll->setWidget(chain_container_);
  layout->addWidget(scroll, 1);

  auto* add_button = new QPushButton(tr("添加算子"), page);
  add_button->setIcon(theme::icon(theme::Glyph::kPlus, theme::colour(theme::kTextDim), 14));
  add_button->setIconSize(QSize(14, 14));
  add_button->setMinimumHeight(30);
  add_button->setCursor(Qt::PointingHandCursor);
  add_button->setStyleSheet(
      QStringLiteral("QPushButton { background:transparent; border:1px dashed #2E3742; "
                     "color:#98A1AC; }"
                     "QPushButton:hover { background:#1A212A; border-color:#3E6FA8; "
                     "color:#E7EAEE; }"));
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
    connect(action, &QAction::triggered, this,
            [this, id]() { addOperator(QString::fromStdString(id)); });
  }
  add_button->setMenu(menu);
  layout->addWidget(add_button);

  return page;
}

// ---------------------------------------------------------------------------
// Left panel: annotation
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildAnnotatePage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(9);

  layout->addWidget(theme::section_label(tr("类别"), page));
  class_list_ = new QListWidget(page);
  class_list_->setMaximumHeight(128);
  class_list_->setIconSize(QSize(9, 9));
  layout->addWidget(class_list_);

  layout->addSpacing(3);
  layout->addWidget(theme::section_label(tr("工具"), page));

  QHBoxLayout* tool_layout = nullptr;
  auto* tool_frame = segmented(page, &tool_layout);
  auto* tool_group = new QButtonGroup(tool_frame);
  tool_group->setExclusive(true);
  const QStringList tool_names{tr("选择"), tr("矩形"), tr("多边形")};
  for (int i = 0; i < tool_names.size(); ++i) {
    auto* button = new QToolButton(tool_frame);
    button->setText(tool_names.at(i));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFont(theme::ui_font(11));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tool_layout->addWidget(button);
    tool_group->addButton(button, i);
  }
  if (auto* first = tool_group->button(0)) {
    first->setChecked(true);
  }
  layout->addWidget(tool_frame);

  layout->addSpacing(3);
  layout->addWidget(theme::section_label(tr("标注"), page));
  shape_list_ = new QListWidget(page);
  layout->addWidget(shape_list_, 1);

  auto* export_row = new QHBoxLayout();
  export_row->setSpacing(6);
  auto* coco_button = new QPushButton(tr("COCO"), page);
  auto* yolo_button = new QPushButton(tr("YOLO"), page);
  export_row->addWidget(coco_button);
  export_row->addWidget(yolo_button);
  layout->addLayout(export_row);

  auto* file_row = new QHBoxLayout();
  file_row->setSpacing(6);
  auto* save_anno = new QPushButton(tr("保存"), page);
  auto* load_anno = new QPushButton(tr("载入"), page);
  file_row->addWidget(save_anno);
  file_row->addWidget(load_anno);
  layout->addLayout(file_row);

  auto* sidecar = new QPushButton(tr("写入数据集标注"), page);
  sidecar->setToolTip(tr("写到图片旁边（.annotations.json），数据集扫描会自动识别"));
  layout->addWidget(sidecar);

  auto* hint = new QLabel(tr("矩形拖拽绘制；多边形逐点单击，双击或回车闭合，Esc 取消。"), page);
  hint->setObjectName(QStringLiteral("Hint"));
  hint->setWordWrap(true);
  hint->setFont(theme::ui_font(11));
  layout->addWidget(hint);

  connect(class_list_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row < 0 || static_cast<std::size_t>(row) >= annotations_.classes().size()) {
      return;
    }
    active_class_ = annotations_.classes()[static_cast<std::size_t>(row)].id;
    if (canvas_ != nullptr) {
      canvas_->setActiveClass(active_class_);
    }
  });
  connect(tool_group, &QButtonGroup::idClicked, this, [this](int id) {
    if (canvas_ == nullptr) {
      return;
    }
    canvas_->setTool(id == 1 ? ImageCanvas::Tool::kRect
                             : (id == 2 ? ImageCanvas::Tool::kPolygon : ImageCanvas::Tool::kNone));
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
  connect(sidecar, &QPushButton::clicked, this, &MainWindow::saveSidecar);

  return page;
}

// ---------------------------------------------------------------------------
// Left panel: dataset
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildDatasetPage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(9);

  layout->addWidget(theme::section_label(tr("数据源"), page));
  auto* choose_dir = new QPushButton(tr("选择图片目录…"), page);
  choose_dir->setMinimumHeight(30);
  choose_dir->setCursor(Qt::PointingHandCursor);
  layout->addWidget(choose_dir);

  dataset_stats_ = new KeyValueList(page);
  dataset_stats_->set_rows({{tr("图片"), tr("—")},
                            {tr("有标注"), tr("—")},
                            {tr("重复组"), tr("—")}});
  layout->addWidget(dataset_stats_);

  layout->addSpacing(4);
  layout->addWidget(theme::section_label(tr("划分"), page));

  auto* ratio_row = new QHBoxLayout();
  ratio_row->setSpacing(8);
  train_ratio_ = new QDoubleSpinBox(page);
  val_ratio_ = new QDoubleSpinBox(page);
  test_ratio_ = new QDoubleSpinBox(page);
  int column = 0;
  for (const auto& [caption_text, box] :
       {std::pair{tr("训练 %"), train_ratio_}, std::pair{tr("验证 %"), val_ratio_},
        std::pair{tr("测试 %"), test_ratio_}}) {
    auto* cell = new QVBoxLayout();
    cell->setSpacing(3);
    auto* caption = new QLabel(caption_text, page);
    caption->setObjectName(QStringLiteral("Meta"));
    caption->setFont(theme::ui_font(10));
    box->setRange(0.0, 100.0);
    box->setDecimals(0);
    box->setSingleStep(5.0);
    box->setValue(column == 0 ? 70.0 : 15.0);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignHCenter);
    box->setFont(theme::mono_font(11));
    cell->addWidget(caption);
    cell->addWidget(box);
    ratio_row->addLayout(cell);
    ++column;
  }
  layout->addLayout(ratio_row);

  auto* option_row = new QHBoxLayout();
  option_row->setSpacing(8);
  stratified_ = new ToggleSwitch(page);
  stratified_->setChecked(true);
  stratified_->setToolTip(tr("按主类别分层抽样，避免某个类别只落在一个集合"));
  auto* stratify_label = new QLabel(tr("按类别分层"), page);
  stratify_label->setFont(theme::ui_font(11));
  split_seed_ = new QSpinBox(page);
  split_seed_->setRange(0, 999999);
  split_seed_->setValue(2026);
  split_seed_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  split_seed_->setAlignment(Qt::AlignHCenter);
  split_seed_->setFont(theme::mono_font(11));
  split_seed_->setFixedWidth(58);
  auto* seed_label = new QLabel(tr("种子"), page);
  seed_label->setObjectName(QStringLiteral("Meta"));
  seed_label->setFont(theme::ui_font(10));
  option_row->addWidget(stratified_);
  option_row->addWidget(stratify_label);
  option_row->addStretch(1);
  option_row->addWidget(seed_label);
  option_row->addWidget(split_seed_);
  layout->addLayout(option_row);

  auto* split_button = new QPushButton(tr("划分数据集"), page);
  split_button->setMinimumHeight(28);
  split_button->setCursor(Qt::PointingHandCursor);
  layout->addWidget(split_button);

  dataset_split_ = new QLabel(tr("尚未划分"), page);
  dataset_split_->setObjectName(QStringLiteral("MetaStrong"));
  dataset_split_->setFont(theme::mono_font(10));
  layout->addWidget(dataset_split_);

  layout->addSpacing(4);
  layout->addWidget(theme::section_label(tr("检查"), page));
  dataset_warnings_ = new QListWidget(page);
  dataset_warnings_->setFont(theme::ui_font(11));
  layout->addWidget(dataset_warnings_, 1);

  auto* export_snapshot = new QPushButton(tr("导出数据集快照…"), page);
  export_snapshot->setMinimumHeight(28);
  export_snapshot->setCursor(Qt::PointingHandCursor);
  layout->addWidget(export_snapshot);

  connect(choose_dir, &QPushButton::clicked, this, &MainWindow::chooseDatasetDirectory);
  connect(split_button, &QPushButton::clicked, this, &MainWindow::splitDataset);
  connect(export_snapshot, &QPushButton::clicked, this, &MainWindow::exportDataset);

  return page;
}

// ---------------------------------------------------------------------------
// Right panel: image facts
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildFactsPanel(QWidget* parent) {
  auto* panel = new QFrame(parent);
  panel->setObjectName(QStringLiteral("PanelRight"));
  auto* layout = new QVBoxLayout(panel);
  layout->setContentsMargins(14, 13, 14, 14);
  layout->setSpacing(8);

  layout->addWidget(theme::section_label(tr("图像属性"), panel));
  layout->addWidget(theme::hairline(panel));
  layout->addSpacing(2);
  info_list_ = new KeyValueList(panel);
  info_list_->set_rows({{tr("文件"), tr("—")}});
  layout->addWidget(info_list_);

  layout->addSpacing(10);
  auto* hist_header = new QHBoxLayout();
  hist_header->setContentsMargins(0, 0, 0, 0);
  hist_header->addWidget(theme::section_label(tr("直方图"), panel));
  hist_header->addStretch(1);
  auto* legend = new QLabel(
      QStringLiteral("<span style='color:#A9503C'>R</span> "
                     "<span style='color:#6B7F4A'>G</span> "
                     "<span style='color:#3F6E8C'>B</span>"),
      panel);
  legend->setTextFormat(Qt::RichText);
  legend->setFont(theme::mono_font(9, QFont::DemiBold));
  hist_header->addWidget(legend);
  layout->addLayout(hist_header);
  layout->addWidget(theme::hairline(panel));
  histogram_ = new HistogramWidget(panel);
  histogram_->setFixedHeight(108);
  layout->addWidget(histogram_);

  layout->addSpacing(10);
  layout->addWidget(theme::section_label(tr("统计"), panel));
  layout->addWidget(theme::hairline(panel));
  layout->addSpacing(2);
  stats_list_ = new KeyValueList(panel);
  stats_list_->set_rows({});
  layout->addWidget(stats_list_);

  layout->addStretch(1);
  return panel;
}

// ---------------------------------------------------------------------------
// Parameter editors
// ---------------------------------------------------------------------------

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
      combo->setFont(theme::ui_font(11));
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
      auto* toggle = new ToggleSwitch(host);
      toggle->setChecked(std::get_if<bool>(&value) != nullptr && std::get<bool>(value));
      connect(toggle, &QAbstractButton::toggled, this, [this, step_index, key_text](bool on) {
        if (building_chain_) {
          return;
        }
        recipe_.steps[step_index].params.set(key_text, on);
        schedulePreview();
      });
      layout->addWidget(toggle, 0, Qt::AlignLeft);
      layout->addStretch(1);
      break;
    }
    case ParamType::kInteger: {
      auto* spin = new QSpinBox(host);
      spin->setObjectName(QStringLiteral("Value"));
      spin->setRange(static_cast<int>(spec.min_value), static_cast<int>(spec.max_value));
      spin->setSingleStep(1);
      spin->setValue(static_cast<int>(std::lround(std::get<double>(value))));
      spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
      spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      spin->setFont(theme::mono_font(11));
      spin->setFixedWidth(64);
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
      layout->addWidget(spin, 0);
      layout->addStretch(1);
      break;
    }
    case ParamType::kNumber:
    default: {
      auto* slider = new QSlider(Qt::Horizontal, host);
      slider->setRange(0, 1000);
      auto* spin = new QDoubleSpinBox(host);
      spin->setObjectName(QStringLiteral("Value"));
      spin->setDecimals(3);
      spin->setRange(spec.min_value, spec.max_value);
      spin->setSingleStep((spec.max_value - spec.min_value) / 200.0);
      spin->setValue(std::get<double>(value));
      spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
      spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      spin->setFont(theme::mono_font(11));
      spin->setFixedWidth(64);
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

// ---------------------------------------------------------------------------
// Chain cards
// ---------------------------------------------------------------------------

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

  if (recipe_.steps.empty()) {
    auto* empty = new QLabel(tr("管线为空。添加算子后，\n右侧对比视图会即时更新。"), chain_container_);
    empty->setObjectName(QStringLiteral("Hint"));
    empty->setFont(theme::ui_font(11));
    empty->setAlignment(Qt::AlignCenter);
    empty->setMinimumHeight(96);
    empty->setWordWrap(true);
    chain_layout_->insertWidget(0, empty);
  }

  const auto& registry = Registry::instance();
  for (std::size_t i = 0; i < recipe_.steps.size(); ++i) {
    Step& step = recipe_.steps[i];
    const Operator* op = registry.find(step.op);

    auto* card = new QFrame(chain_container_);
    card->setObjectName(QStringLiteral("Card"));
    card->setProperty("state", step.enabled ? "on" : "off");
    card->setToolTip(op == nullptr ? QString() : QString::fromStdString(op->spec.summary));

    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(10, 9, 10, 10);
    card_layout->setSpacing(7);

    auto* header = new QHBoxLayout();
    header->setSpacing(8);

    auto* index = new QLabel(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), card);
    index->setObjectName(QStringLiteral("CardIndex"));
    index->setFont(theme::mono_font(10));
    index->setFixedWidth(16);

    auto* title = new QLabel(op == nullptr ? QString::fromStdString(step.op)
                                           : QString::fromStdString(op->spec.label),
                             card);
    title->setObjectName(QStringLiteral("CardTitle"));
    title->setProperty("state", step.enabled ? "on" : "off");
    title->setFont(theme::ui_font(12, QFont::DemiBold));

    auto* toggle = new ToggleSwitch(card);
    toggle->setChecked(step.enabled);
    toggle->setToolTip(tr("启用 / 停用该步骤"));

    auto* more = new QToolButton(card);
    // Text rather than an icon: three dots stay crisp at any DPI and in any
    // font fallback chain, which a hand-drawn 16 px glyph does not guarantee.
    more->setText(QStringLiteral("···"));
    more->setToolButtonStyle(Qt::ToolButtonTextOnly);
    more->setFont(theme::ui_font(12, QFont::DemiBold));
    more->setFixedSize(22, 20);
    more->setStyleSheet(QStringLiteral("QToolButton { padding: 0; color: #94897A; }"
                                       "QToolButton:hover { color: #241F18; }"));
    more->setPopupMode(QToolButton::InstantPopup);
    more->setCursor(Qt::PointingHandCursor);
    auto* menu = new QMenu(more);
    auto* up = menu->addAction(tr("上移"));
    up->setEnabled(i > 0);
    auto* down = menu->addAction(tr("下移"));
    down->setEnabled(i + 1 < recipe_.steps.size());
    menu->addSeparator();
    auto* remove = menu->addAction(tr("删除"));
    more->setMenu(menu);

    header->addWidget(index);
    header->addWidget(title, 1);
    header->addWidget(toggle);
    header->addWidget(more);
    card_layout->addLayout(header);
    card_layout->addWidget(theme::hairline(card));

    auto* body = new QVBoxLayout();
    body->setSpacing(6);
    if (op != nullptr) {
      auto normalized = registry.normalize(op->spec, step.params);
      const Params& params = normalized ? normalized.value() : step.params;
      for (const ParamSpec& param : op->spec.params) {
        auto* row = new QWidget(card);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(16, 0, 0, 0);
        row_layout->setSpacing(8);

        auto* label = new QLabel(QString::fromStdString(param.label), row);
        label->setObjectName(QStringLiteral("ParamLabel"));
        label->setFont(theme::ui_font(11));
        label->setFixedWidth(52);
        row_layout->addWidget(label);

        ParamValue current = 0.0;
        if (params.has(param.key)) {
          current = params.values().at(param.key);
        }
        row_layout->addWidget(
            buildParamEditor(param, current, i, QString::fromStdString(param.key)), 1);
        body->addWidget(row);
      }
    }
    card_layout->addLayout(body);

    if (!step.enabled) {
      for (QWidget* child : card->findChildren<QWidget*>()) {
        if (child != toggle && child != more) {
          child->setEnabled(false);
        }
      }
    }

    connect(toggle, &QAbstractButton::toggled, this, [this, i, card, title, toggle, more](bool on) {
      if (building_chain_) {
        return;
      }
      recipe_.steps[i].enabled = on;
      card->setProperty("state", on ? "on" : "off");
      title->setProperty("state", on ? "on" : "off");
      restyle(card);
      restyle(title);
      for (QWidget* child : card->findChildren<QWidget*>()) {
        if (child != toggle && child != more) {
          child->setEnabled(on);
        }
      }
      schedulePreview();
    });
    connect(up, &QAction::triggered, this, [this, i]() { moveStep(i, -1); });
    connect(down, &QAction::triggered, this, [this, i]() { moveStep(i, 1); });
    connect(remove, &QAction::triggered, this, [this, i]() { removeStep(i); });

    chain_layout_->insertWidget(static_cast<int>(i), card);
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

// ---------------------------------------------------------------------------
// Preview and export
// ---------------------------------------------------------------------------

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
    notify(error_text(result.error()), 5000);
    return;
  }

  const int result_width = result.value().image.width();
  const int result_height = result.value().image.height();
  if (annotations_.image_width() != 0 && annotations_.size() > 0 &&
      (annotations_.image_width() != result_width || annotations_.image_height() != result_height)) {
    notify(tr("输出尺寸已变化，已有标注仍基于 %1 × %2")
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
  status_time_->setText(tr("%1 ms  ·  %2 步")
                            .arg(last_milliseconds_, 0, 'f', 1)
                            .arg(result.value().trace.size()));
  updateInfoPanel();
}

void MainWindow::setBusy(bool busy, const QString& message) {
  if (busy) {
    progress_->show();
    QApplication::setOverrideCursor(Qt::BusyCursor);
    if (!message.isEmpty()) {
      notify(message);
    }
  } else {
    progress_->hide();
    QApplication::restoreOverrideCursor();
    if (status_message_ != nullptr) {
      status_message_->clear();
    }
    if (message_timer_ != nullptr) {
      message_timer_->stop();
    }
  }
}

void MainWindow::notify(const QString& text, int timeout_ms) {
  if (status_message_ == nullptr) {
    return;
  }
  status_message_->setText(text);
  if (message_timer_ != nullptr) {
    message_timer_->start(std::max(500, timeout_ms));
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
  notify(
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
  notify(tr("配方已保存：%1").arg(QFileInfo(path).fileName()), 4000);
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
  auto recipe =
      Recipe::from_json(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())));
  if (!recipe) {
    QMessageBox::warning(this, tr("配方无效"), error_text(recipe.error()));
    return;
  }
  recipe_ = std::move(recipe.value());
  rebuildChain();
  notify(tr("已载入配方：%1").arg(QFileInfo(path).fileName()), 4000);
}

// ---------------------------------------------------------------------------
// Image facts
// ---------------------------------------------------------------------------

void MainWindow::updateInfoPanel() {
  if (source_.empty()) {
    info_list_->set_rows({{tr("文件"), tr("—")}, {tr("尺寸"), tr("—")}});
    stats_list_->set_rows({{tr("均值 R"), tr("—")}});
    return;
  }

  const QString name = source_path_.isEmpty() ? tr("示例图") : QFileInfo(source_path_).fileName();
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

  info_list_->set_rows({
      {tr("文件"), name},
      {tr("尺寸"), QStringLiteral("%1 × %2").arg(source_.width()).arg(source_.height())},
      {tr("色彩"), tr("RGBA · 8 bit")},
      {tr("像素"), QStringLiteral("%1 万").arg(pixels / 10000.0, 0, 'f', 1)},
      {tr("内存"), format_bytes(source_.byte_size())},
  });
  stats_list_->set_rows({
      {tr("均值 R"), QString::number(static_cast<int>(mean_r / pixels))},
      {tr("均值 G"), QString::number(static_cast<int>(mean_g / pixels))},
      {tr("均值 B"), QString::number(static_cast<int>(mean_b / pixels))},
      {tr("锐度"), QString::number(sharpness, 'f', 1)},
  });
}

// ---------------------------------------------------------------------------
// Annotations
// ---------------------------------------------------------------------------

void MainWindow::rebuildClassList() {
  if (class_list_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(class_list_);
  class_list_->clear();
  for (const LabelClass& label : annotations_.classes()) {
    QPixmap swatch(9, 9);
    swatch.fill(QColor(QString::fromStdString(label.color)));
    auto* item = new QListWidgetItem(QIcon(swatch), QString::fromStdString(label.name),
                                     class_list_);
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
    shape_list_->addItem(QStringLiteral("%1  %2 · %3")
                             .arg(i + 1, 2, 10, QLatin1Char('0'))
                             .arg(name, type));
  }
  const int selected = canvas_ != nullptr ? canvas_->selected_index() : -1;
  if (selected >= 0 && selected < shape_list_->count()) {
    shape_list_->setCurrentRow(selected);
  }
  notify(tr("标注 %1 个").arg(annotations_.size()), 2000);
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
  notify(
      tr("已导出 %1 个标注 → %2").arg(annotations_.size()).arg(QFileInfo(path).fileName()), 4000);
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
  notify(tr("标注已保存：%1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::loadAnnotations() {
  const QString path =
      QFileDialog::getOpenFileName(this, tr("载入标注"), QString(), tr("标注 (*.json)"));
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
  notify(tr("已载入标注：%1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::addDemoAnnotations() {
  if (annotations_.image_width() <= 0 || annotations_.image_height() <= 0) {
    return;
  }
  const float width = static_cast<float>(annotations_.image_width());
  const float height = static_cast<float>(annotations_.image_height());

  // Region of interest around the resolution bars.
  Annotation bars;
  bars.class_id = annotations_.classes().size() > 0 ? annotations_.classes()[0].id : 0;
  bars.shape.type = ShapeType::kRect;
  bars.shape.rect = RectF{width * 0.545F, height * 0.552F, width * 0.400F, height * 0.146F};
  annotations_.add(bars);

  // Target around the Siemens star.
  Annotation star;
  star.class_id = annotations_.classes().size() > 1 ? annotations_.classes()[1].id : 0;
  star.shape.type = ShapeType::kPolygon;
  star.shape.points = {
      Point2f{width * 0.128F, height * 0.238F}, Point2f{width * 0.502F, height * 0.238F},
      Point2f{width * 0.502F, height * 0.762F}, Point2f{width * 0.128F, height * 0.762F}};
  annotations_.add(star);

  if (tabs_ != nullptr) {
    tabs_->setCurrentIndex(1);
  }
  if (canvas_ != nullptr) {
    canvas_->setSelectedIndex(0);
  }
  refreshAnnotationLists();
}

void MainWindow::saveSidecar() {
  if (source_path_.isEmpty()) {
    QMessageBox::information(this, tr("无法保存"),
                             tr("示例图没有磁盘路径。请先打开一张真实图片再保存标注。"));
    return;
  }
  const QString path = source_path_ + QStringLiteral(".annotations.json");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QMessageBox::warning(this, tr("保存失败"), file.errorString());
    return;
  }
  const std::string text = annotations_.to_json(2);
  file.write(text.data(), static_cast<qint64>(text.size()));
  notify(tr("已写入数据集标注：%1").arg(QFileInfo(path).fileName()), 5000);
}

// ---------------------------------------------------------------------------
// Dataset
// ---------------------------------------------------------------------------

void MainWindow::chooseDatasetDirectory() {
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择图片目录"), dataset_path_);
  if (directory.isEmpty()) {
    return;
  }
  auto scanned = Dataset::scan(directory.toStdString(), false);
  if (!scanned) {
    QMessageBox::warning(this, tr("扫描失败"), error_text(scanned.error()));
    return;
  }
  dataset_ = std::move(scanned.value());
  dataset_path_ = directory;
  refreshDatasetUi();
  notify(tr("已扫描 %1 张图片").arg(dataset_.size()), 4000);
}

void MainWindow::refreshDatasetUi() {
  if (dataset_stats_ == nullptr) {
    return;
  }
  const DatasetStats stats = dataset_.stats();
  dataset_stats_->set_rows({
      {tr("目录"), dataset_path_.isEmpty() ? tr("—") : QFileInfo(dataset_path_).fileName()},
      {tr("图片"), QString::number(stats.total)},
      {tr("有标注"), QString::number(stats.with_annotations)},
      {tr("重复组"), QString::number(stats.duplicate_groups)},
  });

  if (dataset_split_ != nullptr) {
    dataset_split_->setText(tr("训练 %1   ·   验证 %2   ·   测试 %3")
                                .arg(stats.per_split[0])
                                .arg(stats.per_split[1])
                                .arg(stats.per_split[2]));
  }

  if (dataset_warnings_ != nullptr) {
    dataset_warnings_->clear();
    const auto issues = dataset_.warnings();
    if (issues.empty()) {
      auto* item = new QListWidgetItem(tr("未发现明显问题"), dataset_warnings_);
      item->setForeground(theme::colour(theme::kSuccess));
    } else {
      for (const DatasetWarning& warning : issues) {
        auto* item = new QListWidgetItem(QString::fromStdString(warning.message),
                                        dataset_warnings_);
        switch (warning.kind) {
          case DatasetWarning::Kind::kUnreadable:
            item->setForeground(theme::colour(theme::kDanger));
            break;
          case DatasetWarning::Kind::kDuplicate:
          case DatasetWarning::Kind::kClassMissingInSplit:
          case DatasetWarning::Kind::kTinySplit:
            item->setForeground(theme::colour(theme::kWarning));
            break;
          case DatasetWarning::Kind::kNoAnnotations:
          default:
            item->setForeground(theme::colour(theme::kTextDim));
            break;
        }
      }
    }
  }
}

void MainWindow::splitDataset() {
  if (dataset_.size() == 0) {
    QMessageBox::information(this, tr("没有数据"), tr("请先选择一个包含图片的目录。"));
    return;
  }
  const double train = train_ratio_->value() / 100.0;
  const double val = val_ratio_->value() / 100.0;
  const double test = test_ratio_->value() / 100.0;
  auto status = dataset_.assign_split(train, val, test, static_cast<unsigned>(split_seed_->value()),
                                      stratified_->isChecked());
  if (!status) {
    QMessageBox::warning(this, tr("划分失败"), error_text(status.error()));
    return;
  }
  refreshDatasetUi();
  notify(tr("划分完成：训练 %1 / 验证 %2 / 测试 %3")
                               .arg(dataset_.count(Split::kTrain))
                               .arg(dataset_.count(Split::kVal))
                               .arg(dataset_.count(Split::kTest)),
                           4000);
}

void MainWindow::exportDataset() {
  if (dataset_.size() == 0) {
    QMessageBox::information(this, tr("没有数据"), tr("请先扫描并划分数据集。"));
    return;
  }
  const QString parent = QFileDialog::getExistingDirectory(this, tr("选择导出目录"));
  if (parent.isEmpty()) {
    return;
  }
  const QString name = QFileInfo(dataset_path_).fileName();
  const QString target = parent + QLatin1Char('/') + name + QStringLiteral("-v1");
  auto status = dataset_.export_snapshot(target.toStdString(), name.toStdString());
  if (!status) {
    QMessageBox::warning(this, tr("导出失败"), error_text(status.error()));
    return;
  }
  notify(tr("数据集已导出到 %1").arg(target), 6000);
}

void MainWindow::loadDatasetDirectory(const QString& path) {
  auto scanned = Dataset::scan(path.toStdString(), false);
  if (!scanned) {
    notify(error_text(scanned.error()), 5000);
    return;
  }
  dataset_ = std::move(scanned.value());
  dataset_path_ = path;
  if (auto status = dataset_.assign_split(0.7, 0.15, 0.15, 2026U, true); !status) {
    notify(error_text(status.error()), 5000);
  }
  refreshDatasetUi();
  if (tabs_ != nullptr) {
    tabs_->setCurrentIndex(2);
  }
}

// ---------------------------------------------------------------------------
// Image loading
// ---------------------------------------------------------------------------

bool MainWindow::loadPath(const QString& path) {
  auto image = load_image(path.toStdString());
  if (!image) {
    QMessageBox::warning(this, tr("无法打开图像"), error_text(image.error()));
    return false;
  }
  source_ = std::move(image.value());
  source_path_ = path;
  last_open_dir_ = QFileInfo(path).absolutePath();
  annotations_.clear();
  annotations_.set_image_size(0, 0);
  refreshAnnotationLists();
  setWindowTitle(tr("figtools — %1").arg(QFileInfo(path).fileName()));
  if (status_file_ != nullptr) {
    status_file_->setText(QFileInfo(path).fileName());
  }
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
  setWindowTitle(tr("figtools — 图像工作台"));
  if (status_file_ != nullptr) {
    status_file_->setText(tr("示例图"));
  }
  updateInfoPanel();
  runPreview();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  QSettings settings;
  settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("paths/last_open"), last_open_dir_);
  QMainWindow::closeEvent(event);
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

}  // namespace figtools::gui
