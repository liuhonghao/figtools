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
#include <QInputDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <functional>

#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTime>
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
#include "figtools/core/metrics.hpp"
#include "figtools/core/sample_scene.hpp"
#include "figtools/io/image_io.hpp"
#include "figtools/ops/registry.hpp"
#include "histogram_widget.hpp"
#include "batch_dialog.hpp"
#include "batch_infer_dialog.hpp"
#include "image_canvas.hpp"
#include "python_env.hpp"
#include "command_palette.hpp"
#include "train_dialog.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "1.6.0"
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
  rule->setStyleSheet(QStringLiteral("background:%1; border:none;").arg(theme::colour(theme::kLine).name()));
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

  inference_ = std::make_unique<InferenceClient>();

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

  history_.reset(recipe_);
  updateHistoryActions();

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
        tr("图像 (*.png *.bmp *.ppm *.pgm *.jpg *.jpeg *.webp *.tga);;所有文件 (*.*)"));
    if (!path.isEmpty()) {
      loadPath(path);
    }
  });

  sample_action_ = file_menu->addAction(tr("载入示例图"));
  sample_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
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

  auto* edit_menu = menuBar()->addMenu(tr("编辑(&E)"));
  undo_action_ = edit_menu->addAction(tr("撤销"));
  undo_action_->setShortcut(QKeySequence::Undo);
  connect(undo_action_, &QAction::triggered, this, &MainWindow::undoRecipe);
  redo_action_ = edit_menu->addAction(tr("重做"));
  redo_action_->setShortcuts({QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
  connect(redo_action_, &QAction::triggered, this, &MainWindow::redoRecipe);
  edit_menu->addSeparator();
  auto* clear_action = edit_menu->addAction(tr("清空管线"));
  connect(clear_action, &QAction::triggered, this, [this]() {
    if (recipe_.steps.empty()) {
      return;
    }
    recipe_.steps.clear();
    commitRecipe(tr("清空管线"));
    rebuildChain();
  });

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

  auto* model_menu = menuBar()->addMenu(tr("模型(&M)"));
  auto* train_action = model_menu->addAction(tr("训练模型…"));
  train_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
  connect(train_action, &QAction::triggered, this, [this]() { openTrainDialog(); });
  auto* choose_model_action = model_menu->addAction(tr("载入模型…"));
  connect(choose_model_action, &QAction::triggered, this, &MainWindow::chooseModel);
  auto* infer_action = model_menu->addAction(tr("分类当前图像"));
  infer_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
  connect(infer_action, &QAction::triggered, this, &MainWindow::runInference);
  auto* detect_action = model_menu->addAction(tr("检测当前图像"));
  detect_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
  connect(detect_action, &QAction::triggered, this, &MainWindow::runDetection);
  auto* segment_action = model_menu->addAction(tr("实例分割当前图像"));
  connect(segment_action, &QAction::triggered, this, &MainWindow::runSegmentation);
  auto* ocr_action = model_menu->addAction(tr("OCR 识别当前图像"));
  connect(ocr_action, &QAction::triggered, this, &MainWindow::runOCR);
  auto* batch_infer_action = model_menu->addAction(tr("批量推理…"));
  batch_infer_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
  connect(batch_infer_action, &QAction::triggered, this,
          [this]() { openBatchInferDialog(); });

  auto* help_menu = menuBar()->addMenu(tr("帮助(&H)"));
  auto* palette_action = help_menu->addAction(tr("命令搜索…"));
  palette_action->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
  connect(palette_action, &QAction::triggered, this, &MainWindow::openCommandPalette);
  auto* shortcuts_action = help_menu->addAction(tr("快捷键速查…"));
  shortcuts_action->setShortcut(QKeySequence(QStringLiteral("F1")));
  connect(shortcuts_action, &QAction::triggered, this, [this]() {
    const QString rows =
        tr("<table cellspacing='6' width='100%'>"
           "<tr><td><b>文件</b></td><td></td></tr>"
           "<tr><td>打开图像</td><td>Ctrl+O</td></tr>"
           "<tr><td>导出结果</td><td>Ctrl+S</td></tr>"
           "<tr><td>载入示例图</td><td>Ctrl+Shift+D</td></tr>"
           "<tr><td>批量处理</td><td>Ctrl+B</td></tr>"
           "<tr><td><b>编辑</b></td><td></td></tr>"
           "<tr><td>撤销 / 重做</td><td>Ctrl+Z / Ctrl+Y</td></tr>"
           "<tr><td><b>视图</b></td><td></td></tr>"
           "<tr><td>适应窗口 / 实际像素</td><td>F / 1</td></tr>"
           "<tr><td><b>模型</b></td><td></td></tr>"
           "<tr><td>训练模型</td><td>Ctrl+T</td></tr>"
           "<tr><td>分类 / 检测当前图像</td><td>Ctrl+R / Ctrl+D</td></tr>"
           "<tr><td>批量推理</td><td>Ctrl+Shift+R</td></tr>"
           "<tr><td><b>其它</b></td><td></td></tr>"
           "<tr><td>本帮助</td><td>F1</td></tr>"
           "</table>");
    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("快捷键速查"));
    dialog.setText(tr("<h3>快捷键</h3>%1").arg(rows));
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
  });
  auto* about_action = help_menu->addAction(tr("关于 figtools"));
  connect(about_action, &QAction::triggered, this, [this]() {
    QMessageBox::about(this, tr("关于 figtools"),
                       tr("<b>figtools</b> 图像工作台<br/>版本 %1<br/><br/>"
                          "确定性算子管线（%2 个算子）<br/>"
                          "分割 / 并排 / 结果三种对比视图<br/>"
                          "标注、数据集划分、YOLO 训练与检测、实例分割、OCR<br/>"
                          "暖色 / 冷色双主题<br/><br/>"
                          "核心：MIT · 可选训练组件 Ultralytics：AGPL-3.0（独立安装，不随程序分发）<br/>"
                          "详见 THIRD_PARTY_NOTICES.md")
                           .arg(QStringLiteral(FIGTOOLS_VERSION))
                           .arg(static_cast<int>(Registry::instance().size())));
  });
}

// ---------------------------------------------------------------------------
// Shell
// ---------------------------------------------------------------------------

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  central->setObjectName(QStringLiteral("Workbench"));
  auto* outer = new QVBoxLayout(central);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  buildRibbon(outer);

  auto* body = new QSplitter(Qt::Vertical, central);
  body->setChildrenCollapsible(false);
  body->setHandleWidth(1);

  auto* horizontal = new QSplitter(Qt::Horizontal, body);
  horizontal->setChildrenCollapsible(false);
  horizontal->setHandleWidth(1);

  // ---- left: project tree + the active work page --------------------------
  auto* left = new QFrame(horizontal);
  left->setObjectName(QStringLiteral("PanelLeft"));
  auto* left_layout = new QVBoxLayout(left);
  left_layout->setContentsMargins(0, 0, 0, 0);
  left_layout->setSpacing(0);

  auto* project_title = theme::dock_title(tr("项目"), left);
  left_layout->addWidget(project_title);

  QTreeWidget* project_tree = new QTreeWidget(left);
  project_tree->setObjectName(QStringLiteral("ProjectTree"));
  project_tree->setHeaderHidden(true);
  project_tree->setRootIsDecorated(false);
  project_tree->setMaximumHeight(78);   // hug three rows instead of leaving dead space
  project_tree->setMinimumHeight(78);
  auto* root_image = new QTreeWidgetItem(project_tree, {tr("图像")});
  root_image->setIcon(0, theme::icon(theme::Glyph::kImage));
  auto* root_anno = new QTreeWidgetItem(project_tree, {tr("标注")});
  root_anno->setIcon(0, theme::icon(theme::Glyph::kAnnotate));
  auto* root_data = new QTreeWidgetItem(project_tree, {tr("数据集")});
  root_data->setIcon(0, theme::icon(theme::Glyph::kDataset));
  project_tree->expandAll();
  connect(project_tree, &QTreeWidget::currentItemChanged, this,
          [this, root_image, root_anno, root_data](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current == root_image) {
              setLeftPage(0);
            } else if (current == root_anno) {
              setLeftPage(1);
            } else if (current == root_data) {
              setLeftPage(2);
            }
          });

  left_layout->addWidget(project_tree);
  left_layout->addWidget(theme::hairline(left));

  tabs_ = new QTabWidget(left);
  tabs_->setDocumentMode(true);
  tabs_->tabBar()->hide();  // navigation is now through the Ribbon / project tree
  tabs_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  tabs_->addTab(buildChainPage(tabs_), tr("处理"));
  tabs_->addTab(buildAnnotatePage(tabs_), tr("标注"));
  tabs_->addTab(buildDatasetPage(tabs_), tr("数据集"));
  left_layout->addWidget(tabs_, 1);

  // ---- centre: stacked views (canvas | embedded training workbench) -------
  auto* centre = new QWidget(horizontal);
  auto* centre_layout = new QVBoxLayout(centre);
  centre_layout->setContentsMargins(0, 0, 0, 0);
  centre_layout->setSpacing(0);

  centre_stack_ = new QStackedWidget(centre);
  auto* canvas_view = new QWidget(centre_stack_);
  auto* canvas_layout = new QVBoxLayout(canvas_view);
  canvas_layout->setContentsMargins(0, 0, 0, 0);
  canvas_layout->setSpacing(0);
  document_tabs_ = new QTabWidget(canvas_view);
  document_tabs_->setObjectName(QStringLiteral("DocumentTabs"));
  document_tabs_->setDocumentMode(true);
  document_tabs_->setTabsClosable(true);
  document_tabs_->setMovable(true);
  document_tabs_->setMaximumHeight(32);
  connect(document_tabs_, &QTabWidget::currentChanged, this, &MainWindow::switchDocument);
  connect(document_tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeDocument);
  canvas_layout->addWidget(document_tabs_);
  canvas_layout->addWidget(buildViewToolbar(canvas_view));

  canvas_ = new ImageCanvas(canvas_view);
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
  canvas_layout->addWidget(canvas_, 1);
  centre_stack_->addWidget(canvas_view);  // page 0: image canvas

  train_panel_ = new TrainDialog(QString(), centre_stack_);
  connect(train_panel_, &TrainDialog::modelReady, this, &MainWindow::loadModel);
  connect(train_panel_, &TrainDialog::closed, this, &MainWindow::showCanvasView);
  centre_stack_->addWidget(train_panel_);  // page 1: embedded training workbench
  centre_layout->addWidget(centre_stack_, 1);

  // ---- right: inspector dock ----------------------------------------------
  auto* right = buildFactsPanel(horizontal);
  right->setMinimumWidth(250);
  right->setMaximumWidth(300);
  left->setMinimumWidth(290);
  left->setMaximumWidth(360);

  horizontal->addWidget(left);
  horizontal->addWidget(centre);
  horizontal->addWidget(right);
  horizontal->setStretchFactor(0, 0);
  horizontal->setStretchFactor(1, 1);
  horizontal->setStretchFactor(2, 0);
  main_splitter_ = horizontal;
  horizontal->setSizes({310, 900, 270});

  body->addWidget(horizontal);
  output_dock_ = buildOutputDock(body);
  output_dock_->setMinimumHeight(30);
  output_dock_->setMaximumHeight(132);
  body->addWidget(output_dock_);
  body->setStretchFactor(0, 1);
  body->setStretchFactor(1, 0);

  outer->addWidget(body, 1);
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
    rule->setStyleSheet(QStringLiteral("background:%1;").arg(theme::colour(theme::kLine).name()));
    return rule;
  };

  statusBar()->addWidget(status_file_);
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

  if (output_log_ != nullptr && output_log_->count() == 1) {
    delete output_log_->takeItem(0);
  }
  notify(tr("figtools %1 已就绪").arg(FIGTOOLS_VERSION), 100);
  notify(tr("处理链 %1 步 · 算子注册表 %2 个")
             .arg(recipe_.steps.size())
             .arg(Registry::instance().all().size()), 100);
}

void MainWindow::buildTopBar(QVBoxLayout* outer) {
  auto* bar = new QWidget(this);
  bar->setObjectName(QStringLiteral("QuickBar"));
  bar->setFixedHeight(38);
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(12, 0, 12, 0);
  layout->setSpacing(5);

  auto* mark = new QLabel(bar);
  mark->setFixedSize(8, 8);
  mark->setStyleSheet(QStringLiteral("background:%1; border-radius:2px;").arg(theme::colour(theme::kAccent).name()));
  auto* brand = new QLabel(QStringLiteral("figtools"), bar);
  brand->setObjectName(QStringLiteral("Brand"));
  brand->setFont(theme::ui_font(13, QFont::DemiBold));
  auto* brand_sub = new QLabel(tr("图像工作台"), bar);
  brand_sub->setObjectName(QStringLiteral("BrandSub"));
  brand_sub->setFont(theme::ui_font(10));
  layout->addWidget(mark);
  layout->addSpacing(3);
  layout->addWidget(brand);
  layout->addWidget(brand_sub);
  layout->addSpacing(10);
  layout->addWidget(vertical_rule(bar));

  const auto quick = [bar, layout](QAction* action, theme::Glyph glyph) {
    auto* button = new QToolButton(bar);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIcon(theme::icon(glyph));
    button->setFont(theme::ui_font(10));
    layout->addWidget(button);
    return button;
  };
  quick(open_action_, theme::Glyph::kOpen);
  quick(sample_action_, theme::Glyph::kSample);
  quick(export_action_, theme::Glyph::kExport);
  layout->addWidget(vertical_rule(bar));

  undo_button_ = new QToolButton(bar);
  undo_button_->setIcon(theme::icon(theme::Glyph::kUndo));
  undo_button_->setToolTip(tr("撤销 (Ctrl+Z)"));
  connect(undo_button_, &QToolButton::clicked, this, &MainWindow::undoRecipe);
  redo_button_ = new QToolButton(bar);
  redo_button_->setIcon(theme::icon(theme::Glyph::kRedo));
  redo_button_->setToolTip(tr("重做 (Ctrl+Y)"));
  connect(redo_button_, &QToolButton::clicked, this, &MainWindow::redoRecipe);
  layout->addWidget(undo_button_);
  layout->addWidget(redo_button_);
  layout->addSpacing(2);
  theme_button_ = new QToolButton(bar);
  theme_button_->setIcon(theme::icon(theme::Glyph::kTheme));
  theme_button_->setText(theme::mode_name());
  theme_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  theme_button_->setFont(theme::ui_font(10));
  theme_button_->setToolTip(tr("点击切换界面主题（当前：%1）").arg(theme::mode_name()));
  connect(theme_button_, &QToolButton::clicked, this, &MainWindow::toggleTheme);
  layout->addWidget(theme_button_);
  layout->addStretch(1);

  auto* meta = new QLabel(tr("图像全流程工作台"), bar);
  meta->setObjectName(QStringLiteral("TopMeta"));
  layout->addWidget(meta);
  outer->addWidget(bar);
}

void MainWindow::buildRibbon(QVBoxLayout* outer) {
  auto* ribbon = new QWidget(this);
  ribbon->setObjectName(QStringLiteral("Ribbon"));
  auto* layout = new QVBoxLayout(ribbon);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* tabs = new QTabWidget(ribbon);
  tabs->setObjectName(QStringLiteral("RibbonTabs"));
  tabs->tabBar()->setObjectName(QStringLiteral("RibbonBar"));
  tabs->setMaximumHeight(106);

  // Brand on the left corner of the tab row, theme switch on the right.
  // This removes the old separate QuickBar row (whose open/save/undo buttons
  // duplicated the Ribbon) and keeps the header to menu + ribbon only.
  auto* brand_box = new QWidget(tabs);
  auto* brand_layout = new QHBoxLayout(brand_box);
  brand_layout->setContentsMargins(10, 0, 8, 0);
  brand_layout->setSpacing(5);
  auto* mark = new QLabel(brand_box);
  mark->setFixedSize(8, 8);
  mark->setStyleSheet(QStringLiteral("background:%1; border-radius:2px;")
                          .arg(theme::colour(theme::kAccent).name()));
  auto* brand = new QLabel(QStringLiteral("figtools"), brand_box);
  brand->setObjectName(QStringLiteral("Brand"));
  brand->setFont(theme::ui_font(12, QFont::DemiBold));
  brand_layout->addWidget(mark);
  brand_layout->addWidget(brand);
  tabs->setCornerWidget(brand_box, Qt::TopLeftCorner);

  theme_button_ = new QToolButton(tabs);
  theme_button_->setIcon(theme::icon(theme::Glyph::kTheme));
  theme_button_->setText(theme::mode_name());
  theme_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  theme_button_->setFont(theme::ui_font(10));
  theme_button_->setToolTip(tr("点击切换界面主题（当前：%1）").arg(theme::mode_name()));
  connect(theme_button_, &QToolButton::clicked, this, &MainWindow::toggleTheme);
  tabs->setCornerWidget(theme_button_, Qt::TopRightCorner);
  const char* names[] = {"开始", "处理", "标注", "数据集", "模型", "视图"};
  for (int i = 0; i < 6; ++i) {
    tabs->addTab(buildRibbonPage(i), tr(names[i]));
  }
  connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
    if (index >= 0 && index <= 2 && tabs_ != nullptr) {
      tabs_->setCurrentIndex(index);
    }
  });
  layout->addWidget(tabs);
  outer->addWidget(ribbon);
}

QWidget* MainWindow::buildViewToolbar(QWidget* parent) {
  auto* bar = new QWidget(parent);
  bar->setObjectName(QStringLiteral("QuickBar"));
  bar->setFixedHeight(32);
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(8, 0, 8, 0);
  layout->setSpacing(4);

  auto* label = new QLabel(tr("视图"), bar);
  label->setObjectName(QStringLiteral("Meta"));
  layout->addWidget(label);
  layout->addSpacing(4);

  QHBoxLayout* mode_layout = nullptr;
  auto* mode_frame = segmented(bar, &mode_layout);
  mode_group_ = new QButtonGroup(mode_frame);
  mode_group_->setExclusive(true);
  const struct {
    const char* label;
    ImageCanvas::Mode mode;
  } modes[] = {
      {QT_TR_NOOP("分割"), ImageCanvas::Mode::kSplit},
      {QT_TR_NOOP("并排"), ImageCanvas::Mode::kSide},
      {QT_TR_NOOP("结果"), ImageCanvas::Mode::kResult},
      {QT_TR_NOOP("闪烁"), ImageCanvas::Mode::kFlicker},
      {QT_TR_NOOP("差分"), ImageCanvas::Mode::kDifference},
  };
  for (const auto& entry : modes) {
    auto* button = new QToolButton(mode_frame);
    button->setText(tr(entry.label));
    button->setCheckable(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFont(theme::ui_font(10));
    button->setMinimumWidth(36);
    mode_layout->addWidget(button);
    mode_group_->addButton(button, static_cast<int>(entry.mode));
  }
  if (auto* first = mode_group_->button(static_cast<int>(ImageCanvas::Mode::kSplit))) {
    first->setChecked(true);
  }
  connect(mode_group_, &QButtonGroup::idClicked, this, &MainWindow::setCompareMode);
  layout->addWidget(mode_frame);
  layout->addSpacing(8);

  const auto tool_button = [this, bar, layout](const QString& text, const QString& tip, auto slot) {
    auto* button = new QToolButton(bar);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setToolTip(tip);
    button->setFont(theme::ui_font(10));
    connect(button, &QToolButton::clicked, this, [this, slot]() { slot(); });
    layout->addWidget(button);
  };
  tool_button(tr("适应"), tr("适应窗口 (F)"), [this]() {
    if (canvas_ != nullptr) {
      canvas_->fitToWindow();
    }
  });
  tool_button(QStringLiteral("1:1"), tr("实际像素 (1)"), [this]() {
    if (canvas_ != nullptr) {
      canvas_->zoomToActualSize();
    }
  });
  tool_button(tr("面板"), tr("显示 / 隐藏左右面板"), [this]() { toggleSidePanes(); });
  layout->addStretch(1);
  return bar;
}

QWidget* MainWindow::buildOutputDock(QWidget* parent) {
  auto* dock = new QFrame(parent);
  dock->setObjectName(QStringLiteral("PanelBottom"));
  auto* layout = new QVBoxLayout(dock);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(theme::dock_title(tr("输出"), dock));
  output_log_ = new QListWidget(dock);
  output_log_->setFont(theme::mono_font(10));
  output_log_->setSelectionMode(QAbstractItemView::NoSelection);
  output_log_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  output_log_->addItem(tr("就绪。"));
  output_log_->item(0)->setForeground(theme::colour(theme::kTextFaint));
  layout->addWidget(output_log_, 1);
  return dock;
}

void MainWindow::setLeftPage(int index) {
  if (tabs_ != nullptr && index >= 0 && index < tabs_->count()) {
    tabs_->setCurrentIndex(index);
  }
}

QWidget* MainWindow::buildRibbonPage(int page_index) {
  auto* page = new QWidget(this);
  page->setObjectName(QStringLiteral("RibbonPage"));
  auto* layout = new QHBoxLayout(page);
  layout->setContentsMargins(10, 7, 10, 7);
  layout->setSpacing(0);

  struct RibbonCommand {
    const char* label;
    theme::Glyph glyph;
    bool enabled;
    std::function<void()> action;
  };
  struct RibbonGroup {
    const char* name;
    std::vector<RibbonCommand> commands;
  };

  std::vector<RibbonGroup> groups;
  if (page_index == 0) {  // 开始
    groups = {
        {QT_TR_NOOP("文件"), {
            {QT_TR_NOOP("打开"), theme::Glyph::kOpen, true,
             [this]() { open_action_->trigger(); }},
            {QT_TR_NOOP("示例"), theme::Glyph::kSample, true,
             [this]() { sample_action_->trigger(); }},
            {QT_TR_NOOP("导出"), theme::Glyph::kExport, true,
             [this]() { export_action_->trigger(); }},
        }},
        {QT_TR_NOOP("编辑"), {
            {QT_TR_NOOP("撤销"), theme::Glyph::kUndo, false, [this]() { undoRecipe(); }},
            {QT_TR_NOOP("重做"), theme::Glyph::kRedo, false, [this]() { redoRecipe(); }},
        }},
        {QT_TR_NOOP("批处理"), {
            {QT_TR_NOOP("批量处理"), theme::Glyph::kBatch, true,
             [this]() { openBatchDialog(); }},
            {QT_TR_NOOP("批量推理"), theme::Glyph::kBatch, true,
             [this]() { openBatchInferDialog(); }},
        }},
        {QT_TR_NOOP("推理"), {
            {QT_TR_NOOP("分类"), theme::Glyph::kModel, true,
             [this]() { runInference(); }},
            {QT_TR_NOOP("检测"), theme::Glyph::kDetect, true,
             [this]() { runDetection(); }},
            {QT_TR_NOOP("分割"), theme::Glyph::kLayers, true,
             [this]() { runSegmentation(); }},
            {QT_TR_NOOP("训练模型"), theme::Glyph::kTrain, true,
             [this]() { openTrainDialog(); }},
        }},
        {QT_TR_NOOP("辅助"), {
            {QT_TR_NOOP("命令搜索"), theme::Glyph::kSearch, true,
             [this]() { openCommandPalette(); }},
            {QT_TR_NOOP("面板"), theme::Glyph::kLayers, true,
             [this]() { toggleSidePanes(); }},
            {QT_TR_NOOP("适应"), theme::Glyph::kFit, true,
             [this]() { if (canvas_) canvas_->fitToWindow(); }},
        }},
    };
  } else if (page_index == 1) {  // 处理
    groups = {
        {QT_TR_NOOP("配方"), {
            {QT_TR_NOOP("保存"), theme::Glyph::kSave, true, [this]() { saveRecipe(); }},
            {QT_TR_NOOP("载入"), theme::Glyph::kOpen, true, [this]() { loadRecipe(); }},
            {QT_TR_NOOP("存为预设"), theme::Glyph::kSave, true, [this]() { savePreset(); }},
            {QT_TR_NOOP("应用预设"), theme::Glyph::kPlay, true, [this]() { applyPreset(); }},
        }},
        {QT_TR_NOOP("算子"), {
            {QT_TR_NOOP("添加"), theme::Glyph::kPlus, true,
             [this]() { setLeftPage(0); }},
        }},
    };
  } else if (page_index == 2) {  // 标注
    groups = {
        {QT_TR_NOOP("标注"), {
            {QT_TR_NOOP("矩形"), theme::Glyph::kAnnotate, true, [this]() { setLeftPage(1); }},
            {QT_TR_NOOP("多边形"), theme::Glyph::kAnnotate, true, [this]() { setLeftPage(1); }},
        }},
        {QT_TR_NOOP("导出"), {
            {QT_TR_NOOP("COCO"), theme::Glyph::kExport, true,
             [this]() { exportAnnotations(true); }},
            {QT_TR_NOOP("YOLO"), theme::Glyph::kExport, true,
             [this]() { exportAnnotations(false); }},
        }},
    };
  } else if (page_index == 3) {  // 数据集
    groups = {
        {QT_TR_NOOP("数据集"), {
            {QT_TR_NOOP("选择目录"), theme::Glyph::kFolder, true,
             [this]() { chooseDatasetDirectory(); }},
            {QT_TR_NOOP("划分"), theme::Glyph::kDataset, true, [this]() { splitDataset(); }},
            {QT_TR_NOOP("导出快照"), theme::Glyph::kExport, true, [this]() { exportDataset(); }},
        }},
    };
  } else if (page_index == 4) {  // 模型
    groups = {
        {QT_TR_NOOP("训练"), {
            {QT_TR_NOOP("训练模型"), theme::Glyph::kTrain, true, [this]() { openTrainDialog(); }},
            {QT_TR_NOOP("载入模型"), theme::Glyph::kModel, true, [this]() { chooseModel(); }},
        }},
        {QT_TR_NOOP("推理"), {
            {QT_TR_NOOP("分类"), theme::Glyph::kPlay, true, [this]() { runInference(); }},
            {QT_TR_NOOP("检测"), theme::Glyph::kDetect, true, [this]() { runDetection(); }},
            {QT_TR_NOOP("批量"), theme::Glyph::kBatch, true, [this]() { openBatchInferDialog(); }},
        }},
    };
  } else {  // 视图
    groups = {
        {QT_TR_NOOP("显示"), {
            {QT_TR_NOOP("适应窗口"), theme::Glyph::kFit, true, [this]() {
               if (canvas_ != nullptr) { canvas_->fitToWindow(); }
             }},
            {QT_TR_NOOP("实际像素"), theme::Glyph::kActual, true, [this]() {
               if (canvas_ != nullptr) { canvas_->zoomToActualSize(); }
             }},
            {QT_TR_NOOP("切换主题"), theme::Glyph::kTheme, true, [this]() { toggleTheme(); }},
        }},
    };
  }

  for (std::size_t g = 0; g < groups.size(); ++g) {
    auto* group = new QFrame(page);
    group->setObjectName(QStringLiteral("RibbonGroup"));
    auto* group_layout = new QVBoxLayout(group);
    group_layout->setContentsMargins(0, 0, 16, 0);
    group_layout->setSpacing(3);

    auto* button_row = new QHBoxLayout();
    button_row->setSpacing(2);
    for (const auto& command : groups[g].commands) {
      auto* button = new QToolButton(group);
      button->setObjectName(QStringLiteral("RibbonLarge"));
      button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
      button->setIcon(theme::icon(command.glyph, theme::colour(theme::kTextDim), 24));
      button->setIconSize(QSize(24, 24));
      button->setMinimumWidth(60);
      button->setMinimumHeight(52);
      button->setText(tr(command.label));
      button->setFont(theme::ui_font(10));
      button->setToolTip(tr(command.label));
      button->setCursor(Qt::PointingHandCursor);
      connect(button, &QToolButton::clicked, this, command.action);
      button_row->addWidget(button);
    }
    group_layout->addLayout(button_row, 1);
    auto* caption = new QLabel(tr(groups[g].name), group);
    caption->setObjectName(QStringLiteral("RibbonGroupTitle"));
    caption->setAlignment(Qt::AlignCenter);
    group_layout->addWidget(caption);
    layout->addWidget(group);

    if (g + 1 < groups.size()) {
      auto* rule = new QFrame(page);
      rule->setFixedWidth(1);
      rule->setFixedHeight(52);
      rule->setStyleSheet(QStringLiteral("background:%1;").arg(theme::colour(theme::kLine).name()));
      layout->addSpacing(8);
      layout->addWidget(rule);
      layout->addSpacing(8);
    }
  }
  layout->addStretch(1);
  return page;
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

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void MainWindow::commitRecipe(const QString& label, const QString& coalesce_token) {
  history_.commit(recipe_, label.toStdString(), coalesce_token.toStdString());
  updateHistoryActions();
}

void MainWindow::undoRecipe() {
  if (!history_.undo()) {
    return;
  }
  recipe_ = history_.current();
  rebuildChain();
  updateHistoryActions();
  notify(tr("已撤销"), 1500);
}

void MainWindow::redoRecipe() {
  if (!history_.redo()) {
    return;
  }
  recipe_ = history_.current();
  rebuildChain();
  updateHistoryActions();
  notify(tr("已重做"), 1500);
}

void MainWindow::updateHistoryActions() {
  const bool can_undo = history_.can_undo();
  const bool can_redo = history_.can_redo();
  const QString undo_label = QString::fromStdString(history_.undo_label());
  const QString redo_label = QString::fromStdString(history_.redo_label());

  if (undo_action_ != nullptr) {
    undo_action_->setEnabled(can_undo);
    undo_action_->setText(can_undo ? tr("撤销 %1").arg(undo_label) : tr("撤销"));
  }
  if (redo_action_ != nullptr) {
    redo_action_->setEnabled(can_redo);
    redo_action_->setText(can_redo ? tr("重做 %1").arg(redo_label) : tr("重做"));
  }
  if (undo_button_ != nullptr) {
    undo_button_->setEnabled(can_undo);
    undo_button_->setToolTip(can_undo ? tr("撤销 %1").arg(undo_label) : tr("没有可撤销的操作"));
  }
  if (redo_button_ != nullptr) {
    redo_button_->setEnabled(can_redo);
    redo_button_->setToolTip(can_redo ? tr("重做 %1").arg(redo_label) : tr("没有可重做的操作"));
  }
}

void MainWindow::toggleTheme() {
  const theme::Mode next = theme::mode() == theme::Mode::kWarm ? theme::Mode::kCool
                                                              : theme::Mode::kWarm;
  theme::set_mode(next);
  QSettings settings;
  settings.setValue(QStringLiteral("ui/theme"), theme::mode_name());
  qApp->setPalette(QPalette());
  theme::apply(*qApp);
  // Force every widget to re-polish with the new QSS.
  for (QWidget* widget : QApplication::allWidgets()) {
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }
  if (theme_button_ != nullptr) {
    theme_button_->setText(theme::mode_name());
    theme_button_->setToolTip(tr("点击切换界面主题（当前：%1）").arg(theme::mode_name()));
  }
  notify(tr("已切换到%1主题").arg(theme::mode_name()), 2500);
}

void MainWindow::openCommandPalette() {
  std::vector<CommandPalette::Command> commands;
  for (const auto* op : Registry::instance().all()) {
    commands.push_back({QString::fromStdString(op->spec.label),
                        tr("算子 · %1").arg(QString::fromStdString(op->spec.group)),
                        QStringLiteral("op:") + QString::fromStdString(op->spec.id)});
  }
  commands.push_back({tr("打开图像"), tr("文件"), QStringLiteral("open")});
  commands.push_back({tr("载入示例图"), tr("文件"), QStringLiteral("sample")});
  commands.push_back({tr("导出结果"), tr("文件"), QStringLiteral("export")});
  commands.push_back({tr("批量处理"), tr("文件"), QStringLiteral("batch")});
  commands.push_back({tr("训练模型"), tr("模型"), QStringLiteral("train")});
  commands.push_back({tr("分类当前图像"), tr("模型"), QStringLiteral("infer")});
  commands.push_back({tr("检测当前图像"), tr("模型"), QStringLiteral("detect")});
  commands.push_back({tr("实例分割当前图像"), tr("模型"), QStringLiteral("segment")});
  commands.push_back({tr("OCR 识别当前图像"), tr("模型"), QStringLiteral("ocr")});
  commands.push_back({tr("批量推理"), tr("模型"), QStringLiteral("batchinfer")});
  commands.push_back({tr("切换界面主题"), tr("视图"), QStringLiteral("theme")});
  commands.push_back({tr("显示 / 隐藏左右面板"), tr("视图"), QStringLiteral("panes")});
  commands.push_back({tr("适应窗口"), tr("视图"), QStringLiteral("fit")});
  commands.push_back({tr("实际像素 1:1"), tr("视图"), QStringLiteral("actual")});
  commands.push_back({tr("撤销"), tr("编辑"), QStringLiteral("undo")});
  commands.push_back({tr("重做"), tr("编辑"), QStringLiteral("redo")});

  CommandPalette palette(commands, this);
  if (palette.exec() != QDialog::Accepted) {
    return;
  }
  executeCommand(palette.selectedId());
}

void MainWindow::executeCommand(const QString& id) {
  if (id.isEmpty()) {
    return;
  }
  if (id.startsWith(QStringLiteral("op:"))) {
    addOperator(id.mid(3));
    return;
  }
  if (id == QStringLiteral("open")) { open_action_->trigger(); }
  else if (id == QStringLiteral("sample")) { loadSample(); }
  else if (id == QStringLiteral("export")) { export_action_->trigger(); }
  else if (id == QStringLiteral("batch")) { openBatchDialog(); }
  else if (id == QStringLiteral("train")) { openTrainDialog(); }
  else if (id == QStringLiteral("infer")) { runInference(); }
  else if (id == QStringLiteral("detect")) { runDetection(); }
  else if (id == QStringLiteral("segment")) { runSegmentation(); }
  else if (id == QStringLiteral("ocr")) { runOCR(); }
  else if (id == QStringLiteral("batchinfer")) { openBatchInferDialog(); }
  else if (id == QStringLiteral("theme")) { toggleTheme(); }
  else if (id == QStringLiteral("panes")) { toggleSidePanes(); }
  else if (id == QStringLiteral("fit") && canvas_ != nullptr) { canvas_->fitToWindow(); }
  else if (id == QStringLiteral("actual") && canvas_ != nullptr) { canvas_->zoomToActualSize(); }
  else if (id == QStringLiteral("undo")) { undoRecipe(); }
  else if (id == QStringLiteral("redo")) { redoRecipe(); }
}

void MainWindow::toggleSidePanes() {
  if (main_splitter_ == nullptr) {
    return;
  }
  QWidget* left = main_splitter_->widget(0);
  QWidget* right = main_splitter_->widget(2);
  const bool visible = left != nullptr && left->isVisible();
  if (visible) {
    saved_pane_sizes_ = main_splitter_->sizes();
    if (left) left->hide();
    if (right) right->hide();
    notify(tr("面板已收起，画布全宽显示"), 1500);
  } else {
    if (left) left->show();
    if (right) right->show();
    if (!saved_pane_sizes_.isEmpty()) {
      main_splitter_->setSizes(saved_pane_sizes_);
    }
    notify(tr("面板已展开"), 1500);
  }
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
  add_button->setObjectName(QStringLiteral("AddStepButton"));
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
  const QStringList tool_names{tr("选择"), tr("矩形"), tr("多边形"), tr("取色"), tr("测距")};
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
    switch (id) {
      case 1: canvas_->setTool(ImageCanvas::Tool::kRect); break;
      case 2: canvas_->setTool(ImageCanvas::Tool::kPolygon); break;
      case 3: canvas_->setTool(ImageCanvas::Tool::kProbe); break;
      case 4: canvas_->setTool(ImageCanvas::Tool::kMeasure); break;
      default: canvas_->setTool(ImageCanvas::Tool::kNone); break;
    }
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
  layout->addWidget(theme::dock_title(tr("属性检查器"), panel));
  layout->setContentsMargins(10, 8, 10, 10);
  layout->setSpacing(2);

  // Collapsible sections: the first two open by default, the rest tucked away
  // so the inspector does not read as an undifferentiated wall of small text.
  const auto section = [panel](const QString& title, bool expanded) {
    auto* box = new QGroupBox(title, panel);
    box->setObjectName(QStringLiteral("InspectSection"));
    box->setCheckable(true);
    box->setChecked(expanded);
    auto* content = new QWidget(box);
    auto* box_layout = new QVBoxLayout(content);
    box_layout->setContentsMargins(10, 6, 10, 8);
    box_layout->setSpacing(4);
    auto* outer_layout = new QVBoxLayout(box);
    outer_layout->setContentsMargins(0, 4, 0, 0);
    outer_layout->addWidget(content);
    content->setVisible(expanded);
    QObject::connect(box, &QGroupBox::toggled, content, &QWidget::setVisible);
    return std::make_pair(box, box_layout);
  };

  auto info_section = section(tr("图像属性"), true);
  info_list_ = new KeyValueList(info_section.first);
  info_list_->set_rows({{tr("文件"), tr("—")}});
  info_section.second->addWidget(info_list_);
  layout->addWidget(info_section.first);

  auto hist_section = section(tr("直方图"), true);
  auto* hist_header = new QHBoxLayout();
  hist_header->setContentsMargins(0, 0, 0, 0);
  hist_header->addStretch(1);
  auto* legend = new QLabel(
      QStringLiteral("<span style='color:#D9615A'>R</span> "
                     "<span style='color:#4FA97A'>G</span> "
                     "<span style='color:#3D9BE9'>B</span>"),
      hist_section.first);
  legend->setTextFormat(Qt::RichText);
  legend->setFont(theme::mono_font(9, QFont::DemiBold));
  hist_header->addWidget(legend);
  hist_section.second->addLayout(hist_header);
  histogram_ = new HistogramWidget(hist_section.first);
  histogram_->setFixedHeight(96);
  hist_section.second->addWidget(histogram_);
  layout->addWidget(hist_section.first);

  auto stats_section = section(tr("统计"), false);
  stats_list_ = new KeyValueList(stats_section.first);
  stats_list_->set_rows({});
  stats_section.second->addWidget(stats_list_);
  layout->addWidget(stats_section.first);

  auto quality_section = section(tr("质量对比"), false);
  quality_list_ = new KeyValueList(quality_section.first);
  quality_list_->set_rows({{tr("PSNR"), tr("—")}, {tr("SSIM"), tr("—")}});
  quality_section.second->addWidget(quality_list_);
  layout->addWidget(quality_section.first);

  auto model_section = section(tr("模型"), false);
  model_list_ = new KeyValueList(model_section.first);
  model_list_->set_rows({{tr("模型"), tr("未载入")}, {tr("预测"), tr("—")}});
  model_section.second->addWidget(model_list_);
  layout->addWidget(model_section.first);

  layout->addStretch(1);
  return panel;
}

// ---------------------------------------------------------------------------
// Parameter editors
// ---------------------------------------------------------------------------

QWidget* MainWindow::buildParamEditor(const ParamSpec& spec, const ParamValue& value,
                                      std::size_t step_index, const QString& key) {
  const std::string key_text = key.toStdString();
  const Operator* owner = Registry::instance().find(recipe_.steps[step_index].op);
  const QString owner_label = owner != nullptr ? QString::fromStdString(owner->spec.label)
                                               : QString::fromStdString(recipe_.steps[step_index].op);
  const QString action_label =
      tr("调整 %1 · %2").arg(owner_label, QString::fromStdString(spec.label));
  const QString param_token =
      QStringLiteral("param:%1:%2").arg(step_index).arg(key);
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
              [this, step_index, key_text, action_label](const QString& text) {
                if (building_chain_) {
                  return;
                }
                recipe_.steps[step_index].params.set(key_text, text.toStdString());
                commitRecipe(action_label);
                schedulePreview();
              });
      layout->addWidget(combo, 1);
      break;
    }
    case ParamType::kBool: {
      auto* toggle = new ToggleSwitch(host);
      toggle->setChecked(std::get_if<bool>(&value) != nullptr && std::get<bool>(value));
      connect(toggle, &QAbstractButton::toggled, this,
              [this, step_index, key_text, action_label](bool on) {
        if (building_chain_) {
          return;
        }
        recipe_.steps[step_index].params.set(key_text, on);
        commitRecipe(action_label);
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
      connect(spin, &QSpinBox::valueChanged, this,
              [this, step_index, key_text, action_label](int number) {
        if (building_chain_) {
          return;
        }
        recipe_.steps[step_index].params.set(key_text, static_cast<double>(number));
        commitRecipe(action_label);
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
              [this, spin, spec, span, step_index, key_text, action_label, param_token](int position) {
                const double number = spec.min_value + span * static_cast<double>(position) / 1000.0;
                const QSignalBlocker blocker(spin);
                spin->setValue(number);
                if (building_chain_) {
                  return;
                }
                recipe_.steps[step_index].params.set(key_text, number);
                commitRecipe(action_label, param_token);
                schedulePreview();
              });
      // Releasing the slider closes the coalescing window: the next drag is a
      // separate undo step even though it edits the same parameter.
      connect(slider, &QSlider::sliderReleased, this, [this]() { history_.seal(); });
      connect(spin, &QDoubleSpinBox::editingFinished, this, [this]() { history_.seal(); });
      connect(spin, &QDoubleSpinBox::valueChanged, this,
              [this, slider, spec, span, step_index, key_text, action_label, param_token](double number) {
                const QSignalBlocker blocker(slider);
                slider->setValue(static_cast<int>(
                    std::lround((number - spec.min_value) / span * 1000.0)));
                if (building_chain_) {
                  return;
                }
                recipe_.steps[step_index].params.set(key_text, number);
                commitRecipe(action_label, param_token);
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
      commitRecipe(tr("%1 %2").arg(on ? tr("启用") : tr("停用"), title->text()));
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
  const Operator* op = Registry::instance().find(step.op);
  commitRecipe(tr("添加 %1").arg(op != nullptr ? QString::fromStdString(op->spec.label) : id));
  rebuildChain();
}

void MainWindow::removeStep(std::size_t index) {
  if (index >= recipe_.steps.size()) {
    return;
  }
  const Operator* op = Registry::instance().find(recipe_.steps[index].op);
  const QString name = op != nullptr ? QString::fromStdString(op->spec.label)
                                     : QString::fromStdString(recipe_.steps[index].op);
  recipe_.steps.erase(recipe_.steps.begin() + static_cast<std::ptrdiff_t>(index));
  commitRecipe(tr("删除 %1").arg(name));
  rebuildChain();
}

void MainWindow::moveStep(std::size_t index, int delta) {
  const int target = static_cast<int>(index) + delta;
  if (target < 0 || target >= static_cast<int>(recipe_.steps.size())) {
    return;
  }
  const Operator* op = Registry::instance().find(recipe_.steps[index].op);
  const QString name = op != nullptr ? QString::fromStdString(op->spec.label)
                                     : QString::fromStdString(recipe_.steps[index].op);
  std::swap(recipe_.steps[index], recipe_.steps[static_cast<std::size_t>(target)]);
  commitRecipe(tr("%1 %2").arg(delta < 0 ? tr("上移") : tr("下移")).arg(name));
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
  // The in-memory source is a bounded preview. For full-resolution output,
  // decode the original again; stb's decoder is fast enough and this keeps
  // steady-state memory small even for very large images.
  const Image* input = &source_;
  Image full;
  if (has_full_source_ && !source_path_.isEmpty()) {
    auto decoded = load_image(source_path_.toStdString());
    if (decoded) {
      full = std::move(decoded.value());
      input = &full;
    }
  }
  auto result = execute(recipe_, input->view(), Registry::instance());
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

  if (quality_list_ != nullptr) {
    auto metrics = compare_images(preview_source.view(), result.value().image.view());
    if (metrics) {
      const ImageMetrics& m = metrics.value();
      auto ratio = QStringLiteral("%1%").arg(m.changed_ratio * 100.0, 0, 'f', 1);
      auto psnr = m.identical ? tr("相同")
                              : QString::number(m.psnr, 'f', 2) + QStringLiteral(" dB");
      quality_list_->set_rows({{tr("PSNR"), psnr},
                               {tr("SSIM"), QString::number(m.ssim, 'f', 4)},
                               {tr("MAE"), QString::number(m.mae, 'f', 2)},
                               {tr("变化像素"), ratio}});
    } else {
      quality_list_->set_rows({{tr("PSNR"), tr("尺寸不同")},
                               {tr("SSIM"), tr("—")}});
    }
  }

  const QSize shown_size = has_full_source_ && !full_image_size_.isEmpty()
                               ? full_image_size_
                               : QSize(source_.width(), source_.height());
  status_size_->setText(tr("%1 × %2  ·  预览 %3 × %4")
                            .arg(shown_size.width())
                            .arg(shown_size.height())
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
  // Mirror meaningful messages into the output dock. A professional workbench
  // keeps a durable record even after the status bar message expires.
  if (output_log_ != nullptr && !text.isEmpty()) {
    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    auto* item = new QListWidgetItem(QStringLiteral("%1  %2").arg(stamp, text));
    item->setForeground(theme::colour(theme::kTextDim));
    output_log_->addItem(item);
    while (output_log_->count() > 200) {
      delete output_log_->takeItem(0);
    }
    output_log_->scrollToBottom();
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

void MainWindow::savePreset() {
  bool ok = false;
  const QString name = QInputDialog::getText(
      this, tr("保存为预设"), tr("预设名称："), QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  QSettings settings;
  settings.setValue(QStringLiteral("presets/%1").arg(name.trimmed()),
                    QString::fromStdString(recipe_.to_json(2)));
  notify(tr("已保存预设：%1").arg(name.trimmed()), 2000);
}

void MainWindow::applyPreset() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("presets"));
  const QStringList names = settings.allKeys();
  if (names.isEmpty()) {
    settings.endGroup();
    QMessageBox::information(this, tr("应用预设"), tr("还没有保存过预设。请先用「存为预设」。"));
    return;
  }
  bool ok = false;
  const QString chosen = QInputDialog::getItem(
      this, tr("应用预设"), tr("选择预设："), names, 0, false, &ok);
  if (!ok || chosen.isEmpty()) {
    settings.endGroup();
    return;
  }
  const QString json = settings.value(chosen).toString();
  settings.endGroup();
  auto parsed = Recipe::from_json(json.toStdString());
  if (!parsed) {
    QMessageBox::warning(this, tr("应用预设"), tr("预设内容无法解析。"));
    return;
  }
  recipe_ = std::move(parsed.value());
  commitRecipe(tr("应用预设 %1").arg(chosen));
  notify(tr("已应用预设：%1").arg(chosen), 2000);
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
  commitRecipe(tr("载入配方"));
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
  const QSize reported = has_full_source_ && !full_image_size_.isEmpty()
                             ? full_image_size_
                             : QSize(source_.width(), source_.height());
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
      {tr("尺寸"), QStringLiteral("%1 × %2").arg(reported.width()).arg(reported.height())},
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

// ---------------------------------------------------------------------------
// Multi-document management
// ---------------------------------------------------------------------------

void MainWindow::addDocumentTabs() {
  if (document_tabs_ == nullptr) {
    return;
  }
  switching_document_ = true;
  document_tabs_->clear();
  for (const Document& document : documents_) {
    QString label = document.display_name;
    if (label.isEmpty()) {
      label = document.path.isEmpty() ? tr("示例图") : QFileInfo(document.path).fileName();
    }
    document_tabs_->addTab(new QWidget(document_tabs_),
                           (document.dirty ? QStringLiteral("• ") : QString()) + label);
  }
  if (active_document_ >= 0 && active_document_ < documents_.size()) {
    document_tabs_->setCurrentIndex(active_document_);
  }
  switching_document_ = false;
}

void MainWindow::switchDocument(int index) {
  if (switching_document_ || index < 0 || index >= documents_.size() ||
      index == active_document_) {
    return;
  }
  // Snapshot the active document's live state.
  if (active_document_ >= 0 && active_document_ < documents_.size()) {
    Document& current = documents_[active_document_];
    current.source = source_;
    current.full_size = full_image_size_;
    current.has_full_source = has_full_source_;
    current.path = source_path_;
    current.recipe = recipe_;
    current.history = history_;
    current.annotations = annotations_;
  }
  active_document_ = index;
  const Document& document = documents_[index];
  source_ = document.source;
  full_image_size_ = document.full_size;
  has_full_source_ = document.has_full_source;
  source_path_ = document.path;
  recipe_ = document.recipe;
  history_ = document.history;
  annotations_ = document.annotations;

  switching_document_ = true;
  addDocumentTabs();
  switching_document_ = false;

  setWindowTitle(tr("figtools — %1").arg(document.display_name));
  if (status_file_ != nullptr) {
    status_file_->setText(document.display_name);
  }
  prediction_label_.clear();
  prediction_second_.clear();
  prediction_confidence_ = -1.0;
  refreshModelPanel();
  refreshAnnotationLists();
  if (canvas_ != nullptr) {
    canvas_->clearInferenceResult();
  }
  building_chain_ = true;
  rebuildChain();
  building_chain_ = false;
  updateInfoPanel();
  runPreview();
  notify(tr("切换到：%1").arg(document.display_name), 1500);
}

void MainWindow::closeDocument(int index) {
  if (index < 0 || index >= documents_.size()) {
    return;
  }
  const bool was_active = index == active_document_;
  documents_.removeAt(index);
  if (documents_.isEmpty()) {
    active_document_ = -1;
    loadSample();
    return;
  }
  if (was_active) {
    active_document_ = -1;
    const int target = std::min(index, static_cast<int>(documents_.size()) - 1);
    addDocumentTabs();
    switchDocument(target);
  } else {
    if (active_document_ > index) {
      --active_document_;
    }
    addDocumentTabs();
  }
}

void MainWindow::registerCurrentDocument(const QString& name, bool replace_current) {
  Document document;
  document.path = source_path_;
  document.display_name = name;
  document.source = source_;
  document.full_size = full_image_size_;
  document.has_full_source = has_full_source_;
  document.recipe = recipe_;
  document.history = history_;
  document.annotations = annotations_;
  if (replace_current && active_document_ >= 0 && active_document_ < documents_.size()) {
    documents_[active_document_] = document;
  } else {
    documents_.append(document);
    active_document_ = documents_.size() - 1;
  }
  addDocumentTabs();
}

bool MainWindow::loadPath(const QString& path) {
  auto image = load_image(path.toStdString());
  if (!image) {
    QMessageBox::warning(this, tr("无法打开图像"), error_text(image.error()));
    return false;
  }
  full_image_size_ = QSize(image.value().width(), image.value().height());
  has_full_source_ = true;
  source_ = makePreviewSource(image.value());
  source_path_ = path;
  last_open_dir_ = QFileInfo(path).absolutePath();
  annotations_.clear();
  annotations_.set_image_size(0, 0);
  refreshAnnotationLists();
  setWindowTitle(tr("figtools — %1").arg(QFileInfo(path).fileName()));
  prediction_label_.clear();
  prediction_second_.clear();
  prediction_confidence_ = -1.0;
  refreshModelPanel();
  if (canvas_ != nullptr) {
    canvas_->clearInferenceResult();
  }
  if (status_file_ != nullptr) {
    status_file_->setText(QFileInfo(path).fileName());
  }
  updateInfoPanel();
  registerCurrentDocument(QFileInfo(path).fileName());
  runPreview();
  return true;
}

void MainWindow::loadSample() {
  source_ = make_sample_scene(1200, 800, 2026U);
  full_image_size_ = QSize(source_.width(), source_.height());
  has_full_source_ = false;
  source_path_.clear();
  annotations_.clear();
  annotations_.set_image_size(0, 0);
  refreshAnnotationLists();
  setWindowTitle(tr("figtools — 图像工作台"));
  prediction_label_.clear();
  prediction_second_.clear();
  prediction_confidence_ = -1.0;
  refreshModelPanel();
  if (canvas_ != nullptr) {
    canvas_->clearInferenceResult();
  }
  if (status_file_ != nullptr) {
    status_file_->setText(tr("示例图"));
  }
  updateInfoPanel();
  registerCurrentDocument(tr("示例图"), true);
  runPreview();
}

// ---------------------------------------------------------------------------
// Models: training, loading, inference
// ---------------------------------------------------------------------------

void MainWindow::openTrainDialog(const QString& data_hint) {
  if (train_panel_ == nullptr || centre_stack_ == nullptr) {
    return;
  }
  const QString hint = !data_hint.isEmpty()
                           ? data_hint
                           : (dataset_path_.isEmpty() ? last_open_dir_ : dataset_path_);
  // Refresh the data hint if the caller provided one; keep other fields.
  if (!hint.isEmpty()) {
    train_panel_->setDataHint(hint);
  }
  centre_stack_->setCurrentWidget(train_panel_);
  // Give the training workbench the full width: collapse the project tree and
  // inspector so the form breathes instead of being squeezed into the centre.
  if (main_splitter_ != nullptr) {
    // Pane 0 = project tree, pane 2 = inspector; pane 1 is the centre we keep.
    if (QWidget* left = main_splitter_->widget(0)) left->hide();
    if (QWidget* right = main_splitter_->widget(2)) right->hide();
  }
  notify(tr("训练工作台已打开 · 设置参数后点“开始训练”"), 3000);
}

void MainWindow::replayTrainingLog(const QString& path) {
  if (train_panel_ != nullptr) {
    train_panel_->replayLog(path);
  }
}

void MainWindow::showCanvasView() {
  if (centre_stack_ != nullptr && canvas_ != nullptr) {
    centre_stack_->setCurrentIndex(0);
  }
  if (main_splitter_ != nullptr) {
    if (QWidget* left = main_splitter_->widget(0)) left->show();
    if (QWidget* right = main_splitter_->widget(2)) right->show();
    if (!saved_pane_sizes_.isEmpty()) {
      main_splitter_->setSizes(saved_pane_sizes_);
    }
  }
}

void MainWindow::chooseModel() {
  QSettings settings;
  const QString start = settings.value(QStringLiteral("model/last"), model_dir_).toString();
  const QString directory =
      QFileDialog::getExistingDirectory(this, tr("选择模型目录"), start);
  if (!directory.isEmpty()) {
    loadModel(directory);
  }
}

void MainWindow::loadModel(const QString& directory) {
  QFile file(directory + QStringLiteral("/model.json"));
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("无法载入模型"),
                         tr("目录里没有 model.json：%1").arg(directory));
    return;
  }
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    QMessageBox::warning(this, tr("模型无效"), tr("model.json 不是有效的 JSON 对象。"));
    return;
  }
  const QJsonObject metadata = document.object();
  model_dir_ = directory;
  model_kind_ = metadata.value(QStringLiteral("kind")).toString(QStringLiteral("classifier"));
  model_arch_ = metadata.value(QStringLiteral("arch")).toString();
  model_input_ = metadata.value(QStringLiteral("input_size")).toInt(224);
  const QJsonArray positive =
      metadata.value(QStringLiteral("positive_classes")).toArray();
  model_classes_ = positive.isEmpty()
                       ? metadata.value(QStringLiteral("classes")).toArray().size()
                       : positive.size();
  const QJsonObject metrics = metadata.value(QStringLiteral("metrics")).toObject();
  model_val_ = metrics.contains(QStringLiteral("val_accuracy"))
                   ? metrics.value(QStringLiteral("val_accuracy")).toDouble()
                   : -1.0;
  prediction_label_.clear();
  prediction_second_.clear();
  prediction_confidence_ = -1.0;
  refreshModelPanel();
  if (canvas_ != nullptr) {
    canvas_->clearInferenceResult();
    canvas_->clearDetections();
    canvas_->clearSegments();
  }

  QSettings settings;
  settings.setValue(QStringLiteral("model/last"), directory);
  notify(tr("已载入模型 %1").arg(QFileInfo(directory).fileName()), 4000);
}

void MainWindow::refreshModelPanel() {
  if (model_list_ == nullptr) {
    return;
  }
  if (model_dir_.isEmpty()) {
    model_list_->set_rows({{tr("模型"), tr("未载入")}, {tr("预测"), tr("—")}});
    return;
  }
  const QString confidence =
      prediction_confidence_ >= 0.0
          ? QStringLiteral("%1%").arg(prediction_confidence_ * 100.0, 0, 'f', 1)
          : QStringLiteral("—");
  model_list_->set_rows({
      {tr("模型"), QFileInfo(model_dir_).fileName()},
      {tr("类型"), model_kind_ == QLatin1String("yolo-seg") ? tr("YOLO 分割")
                   : (model_kind_ == QLatin1String("yolo") ? tr("YOLO 检测")
                   : (model_kind_ == QLatin1String("detector") ? tr("检测") : tr("分类")))},
      {tr("架构"), QStringLiteral("%1 · %2 px").arg(model_arch_).arg(model_input_)},
      {tr("类别"), QString::number(model_classes_)},
      {tr("验证"), model_val_ >= 0.0 ? QString::number(model_val_, 'f', 3) : QStringLiteral("—")},
      {tr("预测"), prediction_label_.isEmpty() ? QStringLiteral("—") : prediction_label_},
      {tr("置信度"), confidence},
      {tr("次优"), prediction_second_.isEmpty() ? QStringLiteral("—") : prediction_second_},
  });
}

void MainWindow::runOCR() {
  if (source_.empty()) {
    notify(tr("没有可识别的图像"), 3000);
    return;
  }
  QString image_path = source_path_;
  if (image_path.isEmpty()) {
    if (!inference_temp_) {
      inference_temp_ = std::make_unique<QTemporaryDir>();
    }
    image_path = inference_temp_->filePath(QStringLiteral("ocr_current.png"));
    if (auto saved = save_image(source_, image_path.toStdString()); !saved) {
      QMessageBox::warning(this, tr("识别失败"), error_text(saved.error()));
      return;
    }
  }
  setBusy(true, tr("OCR 识别中…"));
  QString error;
  QJsonObject fields;
  fields.insert(QStringLiteral("image"), image_path);
  const QJsonObject result = inference_->request(QStringLiteral("ocr"), fields, &error, 180000);
  setBusy(false);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, tr("OCR 不可用"),
                         tr("%1\n\n请安装 OCR 组件：pip install rapidocr-onnxruntime").arg(error));
    return;
  }
  const QJsonArray texts = result.value(QStringLiteral("texts")).toArray();
  std::vector<ImageCanvas::Detection> boxes;
  boxes.reserve(static_cast<std::size_t>(texts.size()));
  QString joined;
  for (const QJsonValue& value : texts) {
    const QJsonObject item = value.toObject();
    ImageCanvas::Detection detection;
    detection.label = item.value(QStringLiteral("text")).toString();
    detection.confidence = item.value(QStringLiteral("confidence")).toDouble();
    detection.box = QRectF(item.value(QStringLiteral("x")).toDouble(),
                           item.value(QStringLiteral("y")).toDouble(),
                           item.value(QStringLiteral("w")).toDouble(),
                           item.value(QStringLiteral("h")).toDouble());
    boxes.push_back(std::move(detection));
    if (!joined.isEmpty()) {
      joined += QStringLiteral(" ");
    }
    joined += detection.label;
  }
  if (canvas_ != nullptr) {
    canvas_->clearSegments();
    canvas_->clearDetections();
    canvas_->setDetections(std::move(boxes));
  }
  prediction_label_ = texts.isEmpty() ? tr("未识别到文字") : tr("%1 处文字").arg(texts.size());
  prediction_confidence_ = -1.0;
  prediction_second_.clear();
  refreshModelPanel();
  notify(tr("OCR：%1").arg(joined.left(80)), 6000);
}

void MainWindow::runSegmentation() {
  if (source_.empty()) {
    notify(tr("没有可分割的图像"), 3000);
    return;
  }
  if (model_kind_ != QLatin1String("yolo-seg")) {
    QMessageBox::information(this, tr("模型类型不符"),
                             tr("当前不是分割模型。请用「训练模型 → 目标检测」并选择 YOLO-seg 版本训练。"));
    return;
  }
  QString image_path = source_path_;
  if (image_path.isEmpty()) {
    if (!inference_temp_) {
      inference_temp_ = std::make_unique<QTemporaryDir>();
    }
    image_path = inference_temp_->filePath(QStringLiteral("segment_current.png"));
    if (auto saved = save_image(source_, image_path.toStdString()); !saved) {
      QMessageBox::warning(this, tr("分割失败"), error_text(saved.error()));
      return;
    }
  }
  setBusy(true, tr("分割中…"));
  QString error;
  QJsonObject fields;
  fields.insert(QStringLiteral("model"), model_dir_);
  fields.insert(QStringLiteral("image"), image_path);
  fields.insert(QStringLiteral("conf"), 0.25);
  const QJsonObject result = inference_->request(QStringLiteral("segment"), fields, &error, 180000);
  setBusy(false);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, tr("分割失败"), error);
    return;
  }
  const QJsonArray instances = result.value(QStringLiteral("instances")).toArray();
  std::vector<ImageCanvas::Segment> segments;
  segments.reserve(static_cast<std::size_t>(instances.size()));
  for (const QJsonValue& value : instances) {
    const QJsonObject item = value.toObject();
    ImageCanvas::Segment segment;
    segment.label = item.value(QStringLiteral("label")).toString();
    segment.confidence = item.value(QStringLiteral("confidence")).toDouble();
    for (const QJsonValue& point : item.value(QStringLiteral("polygon")).toArray()) {
      const QJsonArray pair = point.toArray();
      if (pair.size() == 2) {
        segment.polygon.emplace_back(pair.at(0).toDouble(), pair.at(1).toDouble());
      }
    }
    segments.push_back(std::move(segment));
  }
  const int count = static_cast<int>(segments.size());
  if (canvas_ != nullptr) {
    canvas_->clearDetections();
    canvas_->setSegments(std::move(segments));
  }
  prediction_label_ = count == 0 ? tr("未检出") : tr("%1 个实例").arg(count);
  prediction_confidence_ = -1.0;
  prediction_second_.clear();
  refreshModelPanel();
  notify(tr("分割出 %1 个实例").arg(count), 5000);
}

void MainWindow::runDetection() {
  if (source_.empty()) {
    notify(tr("没有可检测的图像"), 3000);
    return;
  }
  const bool yolo = model_kind_ == QLatin1String("yolo");
  if (!yolo && model_kind_ != QLatin1String("detector")) {
    QMessageBox::information(this, tr("模型类型不符"),
                             tr("当前模型是分类器。请用「训练模型 → YOLO 目标检测」训练检测模型，"
                                "或载入一个检测模型包。"));
    return;
  }

  QString image_path = source_path_;
  if (image_path.isEmpty()) {
    if (!inference_temp_) {
      inference_temp_ = std::make_unique<QTemporaryDir>();
    }
    image_path = inference_temp_->filePath(QStringLiteral("detect_current.png"));
    if (auto saved = save_image(source_, image_path.toStdString()); !saved) {
      QMessageBox::warning(this, tr("检测失败"), error_text(saved.error()));
      return;
    }
  }

  const QString python =
      QSettings().value(QStringLiteral("python/executable"), resolve_python()).toString();
  const QString scripts = python_scripts_dir();

  setBusy(true, tr("检测中…"));
  QProcess process;
  process.setWorkingDirectory(scripts);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  const QString script = yolo ? QStringLiteral("detect_yolo.py") : QStringLiteral("detect.py");
  QStringList arguments;
  arguments << script << QStringLiteral("--model") << model_dir_
            << QStringLiteral("--image") << image_path;
  if (yolo) {
    arguments << QStringLiteral("--conf") << QStringLiteral("0.25")
              << QStringLiteral("--iou") << QStringLiteral("0.45");
  } else {
    arguments << QStringLiteral("--threshold") << QStringLiteral("0.5")
              << QStringLiteral("--nms") << QStringLiteral("0.4");
  }
  process.start(python, arguments);
  const bool started = process.waitForStarted(5000);
  const bool finished = started && process.waitForFinished(180000);
  setBusy(false);

  if (!started) {
    QMessageBox::warning(this, tr("无法启动 Python"), process.errorString());
    return;
  }
  if (!finished) {
    process.kill();
    QMessageBox::warning(this, tr("检测超时"), tr("检测进程在 3 分钟内没有返回。"));
    return;
  }
  if (process.exitCode() != 0) {
    const QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
    QMessageBox::warning(this, tr("检测失败"),
                         message.isEmpty() ? tr("检测进程返回 %1").arg(process.exitCode())
                                           : message.right(800));
    return;
  }

  const QJsonDocument document = QJsonDocument::fromJson(process.readAllStandardOutput());
  if (!document.isObject()) {
    QMessageBox::warning(this, tr("检测失败"), tr("检测脚本没有返回 JSON。"));
    return;
  }
  const QJsonObject result = document.object();
  const QJsonArray boxes = result.value(QStringLiteral("detections")).toArray();
  std::vector<ImageCanvas::Detection> detected;
  detected.reserve(static_cast<std::size_t>(boxes.size()));
  for (const QJsonValue& value : boxes) {
    const QJsonObject box = value.toObject();
    ImageCanvas::Detection item;
    item.label = box.value(QStringLiteral("label")).toString();
    item.confidence = box.value(QStringLiteral("confidence")).toDouble();
    item.box = QRectF(box.value(QStringLiteral("x")).toDouble(),
                      box.value(QStringLiteral("y")).toDouble(),
                      box.value(QStringLiteral("w")).toDouble(),
                      box.value(QStringLiteral("h")).toDouble());
    detected.push_back(std::move(item));
  }

  const int detection_count = static_cast<int>(detected.size());
  prediction_label_ = detection_count == 0 ? tr("未检出")
                                           : tr("%1 个目标").arg(detection_count);
  if (canvas_ != nullptr) {
    canvas_->clearInferenceResult();
    canvas_->setDetections(std::move(detected));
  }
  prediction_confidence_ = -1.0;
  prediction_second_.clear();
  refreshModelPanel();
  notify(tr("检测到 %1 个目标").arg(detection_count), 5000);
}

void MainWindow::runInference() {
  if (model_kind_ == QLatin1String("detector") || model_kind_ == QLatin1String("yolo")) {
    runDetection();
    return;
  }
  if (source_.empty()) {
    notify(tr("没有可推理的图像"), 3000);
    return;
  }
  if (model_dir_.isEmpty()) {
    QMessageBox::information(this, tr("没有模型"), tr("请先训练模型，或载入一个模型包。"));
    chooseModel();
    if (model_dir_.isEmpty()) {
      return;
    }
  }

  QString image_path = source_path_;
  if (image_path.isEmpty()) {
    if (!inference_temp_) {
      inference_temp_ = std::make_unique<QTemporaryDir>();
    }
    image_path = inference_temp_->filePath(QStringLiteral("current.png"));
    if (auto saved = save_image(source_, image_path.toStdString()); !saved) {
      QMessageBox::warning(this, tr("推理失败"), error_text(saved.error()));
      return;
    }
  }

  setBusy(true, tr("推理中…"));
  QString error;
  QJsonObject fields;
  fields.insert(QStringLiteral("model"), model_dir_);
  fields.insert(QStringLiteral("image"), image_path);
  fields.insert(QStringLiteral("topk"), 3);
  const QJsonObject result = inference_->request(QStringLiteral("classify"), fields, &error, 180000);
  setBusy(false);
  if (!error.isEmpty()) {
    QMessageBox::warning(this, tr("推理失败"), error);
    return;
  }
  prediction_label_ = result.value(QStringLiteral("label")).toString();
  prediction_confidence_ = result.value(QStringLiteral("confidence")).toDouble(-1.0);
  const QJsonArray topk = result.value(QStringLiteral("topk")).toArray();
  if (topk.size() > 1) {
    const QJsonObject second = topk.at(1).toObject();
    prediction_second_ = QStringLiteral("%1 %2%")
                             .arg(second.value(QStringLiteral("label")).toString())
                             .arg(second.value(QStringLiteral("score")).toDouble() * 100.0, 0, 'f',
                                  1);
  } else {
    prediction_second_.clear();
  }
  refreshModelPanel();
  if (canvas_ != nullptr) {
    canvas_->setInferenceResult(prediction_label_, prediction_confidence_);
  }
  notify(tr("预测：%1 · %2%")
             .arg(prediction_label_)
             .arg(prediction_confidence_ * 100.0, 0, 'f', 1),
         5000);
}

void MainWindow::openBatchInferDialog(const QString& data_hint) {
  const QString hint = !data_hint.isEmpty()
                           ? data_hint
                           : (dataset_path_.isEmpty() ? last_open_dir_ : dataset_path_);
  BatchInferDialog dialog(model_dir_, hint, this);
  dialog.exec();
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
