// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Dataset layer: scan a folder, assign train/val/test splits deterministically,
// report statistics, export a snapshot that a training job can consume.
//
// The split is a pure function of (file list, seed, ratios, strategy). Two runs
// with the same inputs produce the same assignment -- otherwise "same experiment"
// is a lie and metrics stop being comparable.

#ifndef FIGTOOLS_DATASET_DATASET_HPP_
#define FIGTOOLS_DATASET_DATASET_HPP_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "figtools/annotate/annotation.hpp"
#include "figtools/core/result.hpp"

namespace figtools {

enum class Split {
  kTrain = 0,
  kVal = 1,
  kTest = 2,
};

const char* to_string(Split split) noexcept;

struct DatasetItem {
  std::string image_path;
  std::string annotation_path;  // empty when no sidecar is present
  int width = 0;
  int height = 0;
  std::uint64_t content_hash = 0;
  std::size_t class_instances = 0;      // total shapes in the sidecar
  std::map<int, int> per_class_counts;  // class id -> instances
  Split split = Split::kTrain;
};

struct DatasetStats {
  std::size_t total = 0;
  std::size_t with_annotations = 0;
  std::size_t duplicate_groups = 0;
  std::size_t orphan_annotations = 0;  // sidecar without an image
  std::map<int, int> class_instances;  // class id -> instances across the set
  std::vector<std::size_t> per_split{0, 0, 0};
};

// One sanity check the export refuses to ignore.
struct DatasetWarning {
  enum class Kind { kNoAnnotations, kDuplicate, kClassMissingInSplit, kTinySplit, kUnreadable };
  Kind kind = Kind::kNoAnnotations;
  std::string message;
};

class Dataset {
 public:
  // Scans `directory` for supported images. Annotations are picked up from
  // `<image>.annotations.json` sidecars written by the labelling tab.
  [[nodiscard]] static Result<Dataset> scan(const std::string& directory, bool recursive);

  void set_classes(std::vector<LabelClass> classes);
  [[nodiscard]] const std::vector<LabelClass>& classes() const { return classes_; }

  [[nodiscard]] std::vector<DatasetItem>& items() { return items_; }
  [[nodiscard]] const std::vector<DatasetItem>& items() const { return items_; }
  [[nodiscard]] std::size_t size() const { return items_.size(); }

  // Deterministic assignment. `train + val + test` must be 1.0 (+/- 1e-6).
  // Stratified keeps each class's ratio across splits; plain ignores labels.
  [[nodiscard]] Status assign_split(double train, double val, double test, unsigned seed,
                                    bool stratified);

  [[nodiscard]] DatasetStats stats() const;
  [[nodiscard]] std::vector<DatasetWarning> warnings() const;
  [[nodiscard]] std::size_t count(Split split) const;

  // Writes images/, annotations/, splits/*.txt, dataset.json and DATASET_CARD.md.
  [[nodiscard]] Status export_snapshot(const std::string& output_directory,
                                       const std::string& name) const;

 private:
  std::vector<LabelClass> classes_;
  std::vector<DatasetItem> items_;
};

}  // namespace figtools

#endif  // FIGTOOLS_DATASET_DATASET_HPP_
