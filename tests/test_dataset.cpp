// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <filesystem>
#include <fstream>
#include <string>

#include "figtools/annotate/annotation.hpp"
#include "figtools/dataset/dataset.hpp"
#include "figtools/io/image_io.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace figtools;
namespace fs = std::filesystem;

namespace {

fs::path make_workspace(const std::string& name) {
  const fs::path root = fs::temp_directory_path() / ("figtools_dataset_" + name);
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  return root;
}

void write_image(const fs::path& path, int width, int height, std::uint8_t tint) {
  Image image(width, height, Rgba{tint, 128, 64, 255});
  auto status = save_image(image, path.string());
  (void)status;
}

void write_sidecar(const fs::path& image_path, int class_id) {
  AnnotationSet set;
  set.set_classes(default_classes());
  set.set_image_size(32, 24);
  Annotation annotation;
  annotation.class_id = class_id;
  annotation.shape.type = ShapeType::kRect;
  annotation.shape.rect = RectF{4.0F, 4.0F, 16.0F, 12.0F};
  set.add(annotation);

  fs::path sidecar = image_path;
  sidecar += ".annotations.json";
  std::ofstream stream(sidecar);
  stream << set.to_json(2);
}

}  // namespace

TEST(dataset_scan_counts_images_and_sidecars) {
  const fs::path root = make_workspace("scan");
  for (int i = 0; i < 6; ++i) {
    const fs::path path = root / ("img_" + std::to_string(i) + ".ppm");
    write_image(path, 32, 24, static_cast<std::uint8_t>(10 * i));
    if (i < 3) {
      write_sidecar(path, i % 2);
    }
  }

  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (dataset) {
    CHECK_EQ(dataset.value().size(), static_cast<std::size_t>(6));
    const DatasetStats stats = dataset.value().stats();
    CHECK_EQ(stats.total, static_cast<std::size_t>(6));
    CHECK_EQ(stats.with_annotations, static_cast<std::size_t>(3));
    CHECK_EQ(stats.class_instances.size(), static_cast<std::size_t>(2));
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_scan_rejects_missing_directory) {
  auto dataset = Dataset::scan("definitely/not/here", false);
  CHECK(!dataset.has_value());
  CHECK_EQ(static_cast<int>(dataset.error().code), static_cast<int>(ErrorCode::kInvalidArgument));
}

TEST(dataset_scan_finds_orphan_annotations) {
  const fs::path root = make_workspace("orphan");
  write_image(root / "only.ppm", 16, 16, 40);
  std::ofstream(root / "ghost.ppm.annotations.json") << "{\"shapes\":[]}";

  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (dataset) {
    const DatasetStats stats = dataset.value().stats();
    CHECK_EQ(stats.orphan_annotations, static_cast<std::size_t>(1));
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_split_is_deterministic) {
  const fs::path root = make_workspace("det");
  for (int i = 0; i < 20; ++i) {
    write_image(root / ("img_" + std::to_string(i) + ".ppm"), 24, 18,
                static_cast<std::uint8_t>(i * 5));
  }
  auto first = Dataset::scan(root.string(), false);
  auto second = Dataset::scan(root.string(), false);
  CHECK(first.has_value());
  CHECK(second.has_value());
  if (first && second) {
    CHECK(first.value().assign_split(0.7, 0.15, 0.15, 1234U, false).has_value());
    CHECK(second.value().assign_split(0.7, 0.15, 0.15, 1234U, false).has_value());
    for (std::size_t i = 0; i < first.value().size(); ++i) {
      CHECK_EQ(static_cast<int>(first.value().items()[i].split),
               static_cast<int>(second.value().items()[i].split));
    }
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_split_respects_ratios) {
  const fs::path root = make_workspace("ratio");
  for (int i = 0; i < 100; ++i) {
    write_image(root / ("img_" + std::to_string(i) + ".ppm"), 8, 8,
                static_cast<std::uint8_t>(i % 200));
  }
  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (dataset) {
    CHECK(dataset.value().assign_split(0.7, 0.15, 0.15, 7U, false).has_value());
    const std::size_t train = dataset.value().count(Split::kTrain);
    const std::size_t val = dataset.value().count(Split::kVal);
    const std::size_t test = dataset.value().count(Split::kTest);
    CHECK_EQ(train + val + test, static_cast<std::size_t>(100));
    // Online rounding guarantees |count - ratio * n| <= 1 per split.
    CHECK(train >= 69 && train <= 71);
    CHECK(val >= 14 && val <= 16);
    CHECK(test >= 14 && test <= 16);
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_split_rejects_bad_ratios) {
  const fs::path root = make_workspace("badratio");
  write_image(root / "a.ppm", 8, 8, 10);
  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (dataset) {
    CHECK(!dataset.value().assign_split(0.5, 0.2, 0.2, 1U, false).has_value());
    CHECK(!dataset.value().assign_split(-0.1, 0.6, 0.5, 1U, false).has_value());
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_split_keeps_every_class_in_train) {
  const fs::path root = make_workspace("strat");
  for (int i = 0; i < 30; ++i) {
    const fs::path path = root / ("img_" + std::to_string(i) + ".ppm");
    write_image(path, 16, 16, static_cast<std::uint8_t>(i * 3));
    write_sidecar(path, i % 3);
  }
  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (!dataset) {
    std::error_code ec;
    fs::remove_all(root, ec);
    return;
  }
  CHECK(dataset.value().assign_split(0.7, 0.15, 0.15, 5U, true).has_value());

  std::map<int, int> train_classes;
  for (const DatasetItem& item : dataset.value().items()) {
    if (item.split == Split::kTrain) {
      for (const auto& [class_id, count] : item.per_class_counts) {
        train_classes[class_id] += count;
      }
    }
  }
  CHECK_EQ(train_classes.size(), static_cast<std::size_t>(3));
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_detects_duplicate_content) {
  const fs::path root = make_workspace("dup");
  write_image(root / "a.ppm", 12, 12, 30);
  write_image(root / "b.ppm", 12, 12, 30);   // identical bytes
  write_image(root / "c.ppm", 12, 12, 90);   // different

  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (dataset) {
    const DatasetStats stats = dataset.value().stats();
    CHECK_EQ(stats.duplicate_groups, static_cast<std::size_t>(1));
    bool warned = false;
    for (const DatasetWarning& warning : dataset.value().warnings()) {
      if (warning.kind == DatasetWarning::Kind::kDuplicate) {
        warned = true;
      }
    }
    CHECK(warned);
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_warns_when_nothing_is_annotated) {
  const fs::path root = make_workspace("noanno");
  write_image(root / "a.ppm", 10, 10, 20);
  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (dataset) {
    bool warned = false;
    for (const DatasetWarning& warning : dataset.value().warnings()) {
      if (warning.kind == DatasetWarning::Kind::kNoAnnotations) {
        warned = true;
      }
    }
    CHECK(warned);
  }
  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST(dataset_export_writes_snapshot) {
  const fs::path root = make_workspace("export");
  for (int i = 0; i < 10; ++i) {
    const fs::path path = root / ("img_" + std::to_string(i) + ".ppm");
    write_image(path, 20, 16, static_cast<std::uint8_t>(i * 12));
    write_sidecar(path, i % 2);
  }
  auto dataset = Dataset::scan(root.string(), false);
  CHECK(dataset.has_value());
  if (!dataset) {
    std::error_code ec;
    fs::remove_all(root, ec);
    return;
  }
  CHECK(dataset.value().assign_split(0.6, 0.2, 0.2, 11U, false).has_value());

  const fs::path out = make_workspace("export_out");
  const fs::path snapshot = out / "bearing-v1";
  auto status = dataset.value().export_snapshot(snapshot.string(), "bearing-v1");
  CHECK(status.has_value());
  CHECK(fs::exists(snapshot / "dataset.json"));
  CHECK(fs::exists(snapshot / "DATASET_CARD.md"));
  CHECK(fs::exists(snapshot / "splits" / "train.txt"));
  CHECK(fs::exists(snapshot / "splits" / "val.txt"));
  CHECK(fs::exists(snapshot / "splits" / "test.txt"));

  std::size_t lines = 0;
  std::ifstream train(snapshot / "splits" / "train.txt");
  std::string line;
  while (std::getline(train, line)) {
    if (!line.empty()) {
      ++lines;
    }
  }
  CHECK_EQ(lines, dataset.value().count(Split::kTrain));

  std::error_code ec;
  fs::remove_all(root, ec);
  fs::remove_all(out, ec);
}
