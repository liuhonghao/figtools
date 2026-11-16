// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Application shell: operator chain on the left, comparison canvas in the
// middle, image facts on the right. Everything the panels show is derived from
// the operator registry and the recipe -- no hard-coded widget per operator.

#ifndef FIGTOOLS_GUI_MAIN_WINDOW_HPP_
#define FIGTOOLS_GUI_MAIN_WINDOW_HPP_

#include <QMainWindow>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "figtools/annotate/annotation.hpp"
#include "figtools/core/image.hpp"
#include "figtools/dataset/dataset.hpp"
#include "figtools/pipeline/executor.hpp"
#include "figtools/pipeline/history.hpp"

#include "inference_client.hpp"
#include "figtools/pipeline/recipe.hpp"

class QAction;
class QAbstractButton;
class QButtonGroup;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QProgressBar;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace figtools::gui {

class TrainDialog;

class HistogramWidget;
class ImageCanvas;
class KeyValueList;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Loads an image from disk. Returns false when the file cannot be decoded.
  bool loadPath(const QString& path);
  void loadSample();
  void addDemoAnnotations();
  void loadDatasetDirectory(const QString& path);

  // Fits the canvas before a screenshot so captures are framing-stable.
  void prepareForCapture();
  void openBatchDialog(const QString& suggested_input = QString());
  void openTrainDialog(const QString& data_hint = QString());
  void showCanvasView();
  void replayTrainingLog(const QString& path);
  void chooseModel();
  void loadModel(const QString& directory);
  void runInference();
  void runDetection();
  void runSegmentation();
  void runOCR();
  void openBatchInferDialog(const QString& data_hint = QString());
  void refreshModelPanel();

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void buildActions();
  void buildUi();
  void buildTopBar(QVBoxLayout* outer);
  void buildRibbon(QVBoxLayout* outer);
  QWidget* buildRibbonPage(int page_index);
  QWidget* buildViewToolbar(QWidget* parent);
  QWidget* buildOutputDock(QWidget* parent);
  void setLeftPage(int index);
  QWidget* buildChainPage(QWidget* parent);
  QWidget* buildAnnotatePage(QWidget* parent);
  QWidget* buildDatasetPage(QWidget* parent);
  QWidget* buildFactsPanel(QWidget* parent);

  void rebuildChain();
  void addOperator(const QString& id);
  void removeStep(std::size_t index);
  void moveStep(std::size_t index, int delta);

  void schedulePreview();
  void runPreview();
  void applyRecipeToSource(Image* output, double* milliseconds);

  void exportImage();
  void saveRecipe();
  void loadRecipe();
  void updateInfoPanel();
  void rebuildClassList();
  void refreshAnnotationLists();
  void exportAnnotations(bool coco);
  void saveAnnotations();
  void loadAnnotations();
  void saveSidecar();
  void chooseDatasetDirectory();
  void refreshDatasetUi();
  void splitDataset();
  void exportDataset();
  void setBusy(bool busy, const QString& message = QString());
  void notify(const QString& text, int timeout_ms = 3000);
  void setCompareMode(int mode);
  void toggleTheme();
  void commitRecipe(const QString& label, const QString& coalesce_token = QString());
  void undoRecipe();
  void redoRecipe();
  void updateHistoryActions();

  [[nodiscard]] QWidget* buildParamEditor(const ParamSpec& spec, const ParamValue& value,
                                          std::size_t step_index, const QString& key);

  Image source_;  // preview-resolution image held in memory (bounded size)
  QSize full_image_size_;             // original dimensions on disk
  bool has_full_source_ = false;      // true when a full-res file backs source_
  QString source_path_;
  Recipe recipe_;
  RecipeHistory history_{Recipe{}};
  Image result_;
  double last_milliseconds_ = 0.0;
  bool building_chain_ = false;
  bool first_frame_ = true;

  ImageCanvas* canvas_ = nullptr;
  QStackedWidget* centre_stack_ = nullptr;
  QSplitter* main_splitter_ = nullptr;
  QList<int> saved_pane_sizes_;
  TrainDialog* train_panel_ = nullptr;
  HistogramWidget* histogram_ = nullptr;
  QWidget* chain_container_ = nullptr;
  QVBoxLayout* chain_layout_ = nullptr;
  QLabel* chain_summary_ = nullptr;
  KeyValueList* info_list_ = nullptr;
  KeyValueList* stats_list_ = nullptr;
  KeyValueList* model_list_ = nullptr;
  KeyValueList* quality_list_ = nullptr;
  QLabel* status_file_ = nullptr;
  QLabel* status_message_ = nullptr;
  QLabel* status_size_ = nullptr;
  QLabel* status_zoom_ = nullptr;
  QLabel* status_position_ = nullptr;
  QLabel* status_time_ = nullptr;
  QListWidget* class_list_ = nullptr;
  QListWidget* shape_list_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  AnnotationSet annotations_;
  int active_class_ = 0;

  Dataset dataset_;
  QString dataset_path_;
  KeyValueList* dataset_stats_ = nullptr;
  QLabel* dataset_split_ = nullptr;
  QListWidget* dataset_warnings_ = nullptr;
  QDoubleSpinBox* train_ratio_ = nullptr;
  QDoubleSpinBox* val_ratio_ = nullptr;
  QDoubleSpinBox* test_ratio_ = nullptr;
  QAbstractButton* stratified_ = nullptr;
  QSpinBox* split_seed_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QTimer* preview_timer_ = nullptr;
  QTimer* message_timer_ = nullptr;

  QAction* open_action_ = nullptr;
  QAction* sample_action_ = nullptr;
  QAction* export_action_ = nullptr;
  QAction* fit_action_ = nullptr;
  QAction* actual_action_ = nullptr;
  QAction* undo_action_ = nullptr;
  QAction* redo_action_ = nullptr;
  QToolButton* undo_button_ = nullptr;
  QToolButton* redo_button_ = nullptr;
  QToolButton* theme_button_ = nullptr;
  QButtonGroup* mode_group_ = nullptr;
  QStackedWidget* ribbon_stack_ = nullptr;
  QWidget* output_dock_ = nullptr;
  QListWidget* output_log_ = nullptr;
  QString last_open_dir_;
  QString model_dir_;
  QString model_arch_;
  QString model_kind_;  // "classifier" or "detector"
  int model_input_ = 224;
  int model_classes_ = 0;
  double model_val_ = -1.0;
  QString prediction_label_;
  QString prediction_second_;
  double prediction_confidence_ = -1.0;
  std::unique_ptr<QTemporaryDir> inference_temp_;
  std::unique_ptr<figtools::gui::InferenceClient> inference_;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_MAIN_WINDOW_HPP_
