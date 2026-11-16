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
#include <QTimer>

#include "figtools/core/image.hpp"
#include "figtools/pipeline/executor.hpp"
#include "figtools/annotate/annotation.hpp"
#include "figtools/dataset/dataset.hpp"
#include "figtools/pipeline/recipe.hpp"

class QComboBox;
class QLabel;
class QListWidget;
class QTabWidget;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QProgressBar;
class QVBoxLayout;
class QWidget;

namespace figtools::gui {

class HistogramWidget;
class ImageCanvas;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Loads an image from disk. Returns false when the file cannot be decoded.
  bool loadPath(const QString& path);
  void loadSample();
  void addDemoAnnotations();
  void loadDatasetDirectory(const QString& path);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void buildUi();
  void buildActions();
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

  [[nodiscard]] QWidget* buildParamEditor(const ParamSpec& spec, const ParamValue& value,
                                          std::size_t step_index, const QString& key);

  Image source_;
  QString source_path_;
  Recipe recipe_;
  Image result_;
  double last_milliseconds_ = 0.0;
  bool building_chain_ = false;
  bool first_frame_ = true;

  ImageCanvas* canvas_ = nullptr;
  HistogramWidget* histogram_ = nullptr;
  QWidget* chain_container_ = nullptr;
  QVBoxLayout* chain_layout_ = nullptr;
  QLabel* chain_summary_ = nullptr;
  QLabel* info_grid_ = nullptr;
  QLabel* stats_grid_ = nullptr;
  QComboBox* mode_combo_ = nullptr;
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
  QLabel* dataset_stats_ = nullptr;
  QLabel* dataset_split_ = nullptr;
  QListWidget* dataset_warnings_ = nullptr;
  QDoubleSpinBox* train_ratio_ = nullptr;
  QDoubleSpinBox* val_ratio_ = nullptr;
  QDoubleSpinBox* test_ratio_ = nullptr;
  QCheckBox* stratified_ = nullptr;
  QSpinBox* split_seed_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QTimer* preview_timer_ = nullptr;
};

}  // namespace figtools::gui

#endif  // FIGTOOLS_GUI_MAIN_WINDOW_HPP_
