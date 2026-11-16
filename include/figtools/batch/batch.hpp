// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Batch execution: run one recipe over a directory of images.
//
// Deliberately serial. Running files in parallel would make the output order,
// the progress numbers and the peak memory non-deterministic, which defeats
// the point of the deterministic core. Throughput comes from the operator
// implementation, not from decoding four 100 MP images at once.

#ifndef FIGTOOLS_BATCH_BATCH_HPP_
#define FIGTOOLS_BATCH_BATCH_HPP_

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "figtools/core/result.hpp"
#include "figtools/pipeline/recipe.hpp"

namespace figtools {

enum class OutputFormat {
  kPng = 0,
  kBmp,
  kPpm,
};

[[nodiscard]] const char* to_string(OutputFormat format) noexcept;
[[nodiscard]] const char* extension_of(OutputFormat format) noexcept;

struct BatchOptions {
  std::string input_dir;
  std::string output_dir;
  std::string suffix = "_out";     // appended to the stem, before the extension
  OutputFormat format = OutputFormat::kPng;
  bool recursive = false;
  bool overwrite = false;          // false: existing outputs are skipped
};

struct BatchItemResult {
  std::string input_path;
  std::string output_path;
  bool ok = false;
  bool skipped = false;
  double milliseconds = 0.0;
  std::string error;
};

struct BatchProgress {
  std::size_t done = 0;
  std::size_t total = 0;
  std::string current_input;
  std::string phase;  // "load" | "process" | "save"
};

struct BatchSummary {
  std::size_t total = 0;
  std::size_t succeeded = 0;
  std::size_t failed = 0;
  std::size_t skipped = 0;
  bool cancelled = false;
  double milliseconds = 0.0;
  std::vector<BatchItemResult> items;
};

// Supported images under `directory`, sorted so a run is reproducible.
[[nodiscard]] Result<std::vector<std::string>> list_images(const std::string& directory,
                                                           bool recursive);

// `<output_dir>/<stem><suffix>.<ext>`, preserving sub-directories when the
// input was discovered recursively.
[[nodiscard]] std::string batch_output_path(const BatchOptions& options,
                                            const std::string& input_path);

// `cancelled` and `on_progress` may be empty. Cancellation is polled between
// files: the file in flight always finishes, so no half-written output exists.
[[nodiscard]] Status run_batch(const Recipe& recipe, const BatchOptions& options,
                               const std::function<bool()>& cancelled,
                               const std::function<void(const BatchProgress&)>& on_progress,
                               BatchSummary* summary);

}  // namespace figtools

#endif  // FIGTOOLS_BATCH_BATCH_HPP_
