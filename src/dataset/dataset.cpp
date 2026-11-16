// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/dataset/dataset.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "figtools/core/hash.hpp"
#include "figtools/io/image_io.hpp"
#include "figtools/io/path.hpp"
#include "figtools/json/json.hpp"

namespace figtools {
namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t kProbeHashLimit = 1U << 20U;  // 1 MiB is enough to differ

bool is_supported_image(const fs::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".png" || extension == ".bmp" || extension == ".ppm" ||
         extension == ".pgm" || extension == ".jpg" || extension == ".jpeg" ||
         extension == ".jpe" || extension == ".webp" || extension == ".tga";
}

std::uint64_t hash_file(const fs::path& path, std::uint64_t* total_bytes) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return 0;
  }
  std::vector<char> buffer(64U * 1024U);
  std::uint64_t state = kFnvOffsetBasis;
  std::uint64_t bytes = 0;
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = static_cast<std::size_t>(stream.gcount());
    if (read == 0) {
      break;
    }
    const std::size_t take = static_cast<std::size_t>(
        std::min<std::uint64_t>(read, kProbeHashLimit - std::min(kProbeHashLimit, bytes)));
    if (take > 0) {
      state = fnv1a(buffer.data(), take, state);
    }
    bytes += read;
  }
  if (total_bytes != nullptr) {
    *total_bytes = bytes;
  }
  return state;
}

AnnotationSet load_sidecar(const fs::path& path, bool* ok) {
  *ok = false;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return {};
  }
  std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  auto parsed = AnnotationSet::from_json(text);
  if (!parsed) {
    return {};
  }
  *ok = true;
  return std::move(parsed.value());
}

}  // namespace

const char* to_string(Split split) noexcept {
  switch (split) {
    case Split::kTrain:
      return "train";
    case Split::kVal:
      return "val";
    case Split::kTest:
      return "test";
    default:
      return "unknown";
  }
}

Result<Dataset> Dataset::scan(const std::string& directory, bool recursive) {
  std::error_code ec;
  const fs::path root = io::native_path(directory);
  if (!fs::is_directory(root, ec)) {
    return Result<Dataset>::fail(ErrorCode::kInvalidArgument,
                                 "dataset: not a directory: " + directory);
  }

  Dataset dataset;
  std::vector<fs::path> images;
  std::vector<fs::path> sidecars;

  const auto handle = [&](const fs::directory_entry& entry) {
    if (!entry.is_regular_file(ec)) {
      return;
    }
    const fs::path path = entry.path();
    if (is_supported_image(path)) {
      images.push_back(path);
    } else if (path.extension() == ".json" &&
               path.stem().extension() == ".annotations") {
      sidecars.push_back(path);
    }
  };

  if (recursive) {
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
      handle(entry);
    }
  } else {
    for (const auto& entry : fs::directory_iterator(root, ec)) {
      handle(entry);
    }
  }
  if (ec) {
    return Result<Dataset>::fail(ErrorCode::kIoError,
                                 "dataset: cannot scan " + directory + ": " + ec.message());
  }

  std::sort(images.begin(), images.end());
  std::vector<bool> sidecar_used(sidecars.size(), false);

  std::vector<LabelClass> merged_classes;
  for (const fs::path& path : images) {
    DatasetItem item;
    item.image_path = path.string();

    auto info = probe_image(item.image_path);
    if (!info) {
      continue;  // unreadable files are skipped rather than failing the scan
    }
    item.width = info.value().width;
    item.height = info.value().height;
    item.content_hash = hash_file(path, nullptr);
    // Perceptual hash needs decoded pixels. A corrupt image simply gets zero,
    // which means "unknown" and is skipped by the near-duplicate check.
    if (auto decoded = load_image(path.string()); decoded) {
      item.perceptual_hash = figtools::perceptual_hash(decoded.value().view());
    }

    fs::path sidecar = path;
    sidecar += ".annotations.json";
    if (fs::exists(sidecar, ec)) {
      bool ok = false;
      AnnotationSet set = load_sidecar(sidecar, &ok);
      if (ok) {
        item.annotation_path = sidecar.string();
        item.class_instances = set.size();
        for (const Annotation& annotation : set.items()) {
          ++item.per_class_counts[annotation.class_id];
        }
        for (const LabelClass& label : set.classes()) {
          const bool known = std::any_of(merged_classes.begin(), merged_classes.end(),
                                         [&label](const LabelClass& existing) {
                                           return existing.id == label.id;
                                         });
          if (!known) {
            merged_classes.push_back(label);
          }
        }
        const auto it = std::find(sidecars.begin(), sidecars.end(), sidecar);
        if (it != sidecars.end()) {
          sidecar_used[static_cast<std::size_t>(std::distance(sidecars.begin(), it))] = true;
        }
      }
    }
    dataset.items_.push_back(std::move(item));
  }

  if (merged_classes.empty()) {
    merged_classes = default_classes();
  } else {
    std::sort(merged_classes.begin(), merged_classes.end(),
              [](const LabelClass& lhs, const LabelClass& rhs) { return lhs.id < rhs.id; });
  }
  dataset.classes_ = std::move(merged_classes);

  // Orphans: sidecars whose image is missing.
  for (std::size_t i = 0; i < sidecars.size(); ++i) {
    if (sidecar_used[i]) {
      continue;
    }
    DatasetItem orphan;
    orphan.annotation_path = sidecars[i].string();
    dataset.items_.push_back(std::move(orphan));
  }

  return Result<Dataset>::ok(std::move(dataset));
}

void Dataset::set_classes(std::vector<LabelClass> classes) { classes_ = std::move(classes); }

Status Dataset::assign_split(double train, double val, double test, unsigned seed,
                             bool stratified) {
  if (train < 0.0 || val < 0.0 || test < 0.0) {
    return Status::fail(ErrorCode::kInvalidArgument, "dataset: ratios must be non-negative");
  }
  const double sum = train + val + test;
  if (std::abs(sum - 1.0) > 1e-6) {
    return Status::fail(ErrorCode::kInvalidArgument, "dataset: ratios must sum to 1.0");
  }
  if (items_.empty()) {
    return Status::fail(ErrorCode::kInvalidArgument, "dataset: no images to split");
  }

  std::mt19937 rng(seed);

  // Build the assignment order. Stratified: group by dominant class so that
  // consecutive items belong to the same class, then rely on online rounding
  // to spread them across splits. Plain: one shuffled list.
  std::vector<std::size_t> order(items_.size());
  std::iota(order.begin(), order.end(), 0U);
  if (stratified) {
    std::stable_sort(order.begin(), order.end(), [this](std::size_t lhs, std::size_t rhs) {
      const auto dominant = [this](std::size_t index) {
        int best_class = -1;
        int best_count = -1;
        for (const auto& [class_id, count] : items_[index].per_class_counts) {
          if (count > best_count) {
            best_count = count;
            best_class = class_id;
          }
        }
        return best_class;
      };
      return dominant(lhs) < dominant(rhs);
    });
    // Shuffle inside each class block so the split is not biased by file order.
    std::size_t begin = 0;
    while (begin < order.size()) {
      std::size_t end = begin + 1;
      const auto dominant_of = [this](std::size_t index) {
        int best_class = -1;
        int best_count = -1;
        for (const auto& [class_id, count] : items_[index].per_class_counts) {
          if (count > best_count) {
            best_count = count;
            best_class = class_id;
          }
        }
        return best_class;
      };
      while (end < order.size() &&
             dominant_of(order[end]) == dominant_of(order[begin])) {
        ++end;
      }
      std::shuffle(order.begin() + static_cast<std::ptrdiff_t>(begin),
                   order.begin() + static_cast<std::ptrdiff_t>(end), rng);
      begin = end;
    }
  } else {
    std::shuffle(order.begin(), order.end(), rng);
  }

  // Online rounding: at step k pick the split whose target share of the first
  // k items most exceeds what it already has. Final counts differ from
  // ratio * n by at most one per split.
  const std::array<double, 3> ratios{train, val, test};
  std::array<std::size_t, 3> counts{0, 0, 0};
  for (std::size_t k = 0; k < order.size(); ++k) {
    const double position = static_cast<double>(k + 1);
    std::size_t chosen = 0;
    double best_gap = -1.0e9;
    for (std::size_t i = 0; i < 3; ++i) {
      const double gap = ratios[i] * position - static_cast<double>(counts[i]);
      if (gap > best_gap) {
        best_gap = gap;
        chosen = i;
      }
    }
    counts[chosen] += 1;
    items_[order[k]].split = static_cast<Split>(chosen);
  }
  return Status::ok();
}

std::size_t Dataset::count(Split split) const {
  return static_cast<std::size_t>(
      std::count_if(items_.begin(), items_.end(),
                    [split](const DatasetItem& item) { return item.split == split; }));
}

DatasetStats Dataset::stats() const {
  DatasetStats result;
  result.total = items_.size();
  for (const DatasetItem& item : items_) {
    if (!item.annotation_path.empty()) {
      ++result.with_annotations;
    }
    if (item.image_path.empty()) {
      ++result.orphan_annotations;
    }
    for (const auto& [class_id, count] : item.per_class_counts) {
      result.class_instances[class_id] += count;
    }
  }
  result.per_split = {count(Split::kTrain), count(Split::kVal), count(Split::kTest)};

  std::map<std::uint64_t, int> hash_counts;
  for (const DatasetItem& item : items_) {
    if (!item.image_path.empty() && item.content_hash != 0) {
      ++hash_counts[item.content_hash];
    }
  }
  for (const auto& [hash, count] : hash_counts) {
    if (count > 1) {
      ++result.duplicate_groups;
    }
  }
  return result;
}

std::vector<DatasetWarning> Dataset::warnings() const {
  std::vector<DatasetWarning> out;
  const DatasetStats summary = stats();

  if (summary.with_annotations == 0) {
    out.push_back({DatasetWarning::Kind::kNoAnnotations,
                   "没有任何图片带有 .annotations.json，训练前需要先标注"});
  }
  if (summary.duplicate_groups > 0) {
    out.push_back({DatasetWarning::Kind::kDuplicate,
                   "检测到 " + std::to_string(summary.duplicate_groups) +
                       " 组内容完全相同的图片，可能造成训练/验证泄漏"});
  }
  // Near duplicates across splits are the most common silent leakage: a
  // resized or recompressed copy of a train image ending up in val/test.
  // Compare pHash only across different splits to keep the cost bounded.
  std::size_t near_duplicate_pairs = 0;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (items_[i].perceptual_hash == 0) {
      continue;
    }
    for (std::size_t j = i + 1; j < items_.size(); ++j) {
      if (items_[j].perceptual_hash == 0 || items_[i].split == items_[j].split) {
        continue;
      }
      if (hamming_distance(items_[i].perceptual_hash, items_[j].perceptual_hash) <= 5) {
        ++near_duplicate_pairs;
      }
    }
  }
  if (near_duplicate_pairs > 0) {
    out.push_back({DatasetWarning::Kind::kNearDuplicate,
                   "检测到 " + std::to_string(near_duplicate_pairs) +
                       " 对跨集合近重复图片（缩放/压缩后同图），会造成指标虚高"});
  }
  if (summary.orphan_annotations > 0) {
    out.push_back({DatasetWarning::Kind::kUnreadable,
                   std::to_string(summary.orphan_annotations) + " 个标注文件找不到对应图片"});
  }
  if (summary.per_split[1] < 30 || summary.per_split[2] < 30) {
    out.push_back({DatasetWarning::Kind::kTinySplit,
                   "验证集或测试集少于 30 张，指标波动会很大"});
  }

  // Classes present overall but missing in validation or test.
  std::map<int, std::array<int, 3>> class_by_split;
  for (const DatasetItem& item : items_) {
    for (const auto& [class_id, count] : item.per_class_counts) {
      class_by_split[class_id][static_cast<std::size_t>(item.split)] += count;
    }
  }
  for (const auto& [class_id, counts] : class_by_split) {
    if (counts[1] == 0) {
      out.push_back({DatasetWarning::Kind::kClassMissingInSplit,
                     "类别 " + std::to_string(class_id) + " 在验证集中没有样本"});
    }
  }
  return out;
}

Status Dataset::export_snapshot(const std::string& output_directory,
                                const std::string& name) const {
  namespace fs = std::filesystem;
  std::error_code ec;

  const fs::path root = io::native_path(output_directory);
  const fs::path images_dir = root / "images";
  const fs::path annotations_dir = root / "annotations";
  const fs::path splits_dir = root / "splits";
  fs::create_directories(images_dir, ec);
  fs::create_directories(annotations_dir, ec);
  fs::create_directories(splits_dir, ec);
  if (ec) {
    return Status::fail(ErrorCode::kIoError, "dataset: cannot create " + root.string());
  }

  std::map<Split, std::vector<std::string>> lists;
  Json items_json = Json::array();

  for (const DatasetItem& item : items_) {
    if (item.image_path.empty()) {
      continue;
    }
    const fs::path source = io::native_path(item.image_path);
    fs::path target = images_dir / source.filename();
    int suffix = 1;
    while (fs::exists(target, ec)) {
      target = images_dir / (source.stem().string() + "_" + std::to_string(suffix) +
                             source.extension().string());
      ++suffix;
    }
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      return Status::fail(ErrorCode::kIoError,
                          "dataset: cannot copy " + source.string() + ": " + ec.message());
    }

    std::string annotation_name;
    if (!item.annotation_path.empty()) {
      const fs::path annotation_source = io::native_path(item.annotation_path);
      const fs::path annotation_target = annotations_dir / (target.filename().string() + ".json");
      fs::copy_file(annotation_source, annotation_target, fs::copy_options::overwrite_existing, ec);
      if (ec) {
        return Status::fail(ErrorCode::kIoError, "dataset: cannot copy annotations: " + ec.message());
      }
      annotation_name = annotation_target.filename().string();
    }

    lists[item.split].push_back(target.filename().string());

    Json node = Json::object();
    node.set("image", Json(target.filename().string()));
    if (!annotation_name.empty()) {
      node.set("annotation", Json(annotation_name));
    }
    node.set("width", Json(static_cast<double>(item.width)));
    node.set("height", Json(static_cast<double>(item.height)));
    node.set("split", Json(std::string(to_string(item.split))));
    items_json.push_back(std::move(node));
  }

  for (const auto& [split, files] : lists) {
    std::ofstream stream(splits_dir / (std::string(to_string(split)) + ".txt"));
    if (!stream) {
      return Status::fail(ErrorCode::kIoError, "dataset: cannot write split list");
    }
    for (const std::string& file : files) {
      stream << file << '\n';
    }
  }

  Json manifest = Json::object();
  manifest.set("name", Json(name));
  manifest.set("format", Json(std::string("figtools-dataset/1")));
  Json classes = Json::array();
  for (const LabelClass& label : classes_) {
    Json node = Json::object();
    node.set("id", Json(static_cast<double>(label.id)));
    node.set("name", Json(label.name));
    classes.push_back(std::move(node));
  }
  manifest.set("classes", std::move(classes));
  manifest.set("items", std::move(items_json));
  {
    std::ofstream stream(root / "dataset.json");
    if (!stream) {
      return Status::fail(ErrorCode::kIoError, "dataset: cannot write dataset.json");
    }
    stream << manifest.dump(2) << '\n';
  }

  const DatasetStats summary = stats();
  {
    std::ofstream card(root / "DATASET_CARD.md");
    if (!card) {
      return Status::fail(ErrorCode::kIoError, "dataset: cannot write DATASET_CARD.md");
    }
    card << "# " << name << "\n\n";
    card << "| 项 | 值 |\n|---|---|\n";
    card << "| 图片总数 | " << summary.total << " |\n";
    card << "| 有标注 | " << summary.with_annotations << " |\n";
    card << "| 训练 / 验证 / 测试 | " << summary.per_split[0] << " / " << summary.per_split[1]
         << " / " << summary.per_split[2] << " |\n";
    card << "| 重复组 | " << summary.duplicate_groups << " |\n\n";
    card << "## 类别实例数\n\n| class id | 实例数 |\n|---|---|\n";
    for (const auto& [class_id, count] : summary.class_instances) {
      card << "| " << class_id << " | " << count << " |\n";
    }
    const auto issues = warnings();
    if (!issues.empty()) {
      card << "\n## 数据检查\n\n";
      for (const DatasetWarning& warning : issues) {
        card << "- " << warning.message << "\n";
      }
    }
  }

  return Status::ok();
}

}  // namespace figtools
