// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/batch/batch.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <utility>

#include "figtools/io/image_io.hpp"
#include "figtools/pipeline/executor.hpp"

namespace figtools {
namespace {

namespace fs = std::filesystem;

std::string lower_extension(const fs::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return extension;
}

bool is_supported(const fs::path& path) {
  const std::string extension = lower_extension(path);
  return extension == ".png" || extension == ".bmp" || extension == ".ppm" ||
         extension == ".pgm" || extension == ".jpg" || extension == ".jpeg" ||
         extension == ".jpe" || extension == ".webp" || extension == ".tga";
}

}  // namespace

const char* to_string(OutputFormat format) noexcept {
  switch (format) {
    case OutputFormat::kBmp:
      return "BMP";
    case OutputFormat::kPpm:
      return "PPM";
    case OutputFormat::kPng:
    default:
      return "PNG";
  }
}

const char* extension_of(OutputFormat format) noexcept {
  switch (format) {
    case OutputFormat::kBmp:
      return ".bmp";
    case OutputFormat::kPpm:
      return ".ppm";
    case OutputFormat::kPng:
    default:
      return ".png";
  }
}

Result<std::vector<std::string>> list_images(const std::string& directory, bool recursive) {
  std::error_code error;
  if (!fs::is_directory(directory, error)) {
    return Result<std::vector<std::string>>::fail(ErrorCode::kIoError,
                                                  "batch: not a directory: " + directory);
  }

  std::vector<std::string> files;
  const auto collect = [&files](const fs::directory_entry& entry) {
    std::error_code status_error;
    if (entry.is_regular_file(status_error) && !status_error && is_supported(entry.path())) {
      files.push_back(entry.path().string());
    }
  };

  if (recursive) {
    fs::recursive_directory_iterator it(directory, error);
    if (error) {
      return Result<std::vector<std::string>>::fail(ErrorCode::kIoError,
                                                    "batch: cannot read " + directory);
    }
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(error)) {
      if (error) {
        // A single unreadable sub-directory must not abort the listing.
        error.clear();
        continue;
      }
      collect(*it);
    }
  } else {
    fs::directory_iterator it(directory, error);
    if (error) {
      return Result<std::vector<std::string>>::fail(ErrorCode::kIoError,
                                                    "batch: cannot read " + directory);
    }
    const fs::directory_iterator end;
    for (; it != end; it.increment(error)) {
      if (error) {
        error.clear();
        continue;
      }
      collect(*it);
    }
  }

  // Sorting makes the run, the progress numbers and the summary reproducible.
  std::sort(files.begin(), files.end());
  return Result<std::vector<std::string>>::ok(std::move(files));
}

std::string batch_output_path(const BatchOptions& options, const std::string& input_path) {
  const fs::path input(input_path);
  fs::path relative = input.filename();
  std::error_code error;
  const fs::path base(options.input_dir);
  const fs::path absolute_input = fs::absolute(input, error);
  const fs::path absolute_base = fs::absolute(base, error);
  if (!error) {
    const fs::path rel = fs::relative(absolute_input, absolute_base, error);
    if (!error && !rel.empty()) {
      relative = rel;
    }
  }

  fs::path name = relative.parent_path() / (relative.stem().string() + options.suffix +
                                            extension_of(options.format));
  return (fs::path(options.output_dir) / name).string();
}

Status run_batch(const Recipe& recipe, const BatchOptions& options,
                 const std::function<bool()>& cancelled,
                 const std::function<void(const BatchProgress&)>& on_progress,
                 BatchSummary* summary) {
  if (summary == nullptr) {
    return Status::fail(ErrorCode::kInvalidArgument, "batch: summary must not be null");
  }
  *summary = BatchSummary{};

  if (options.input_dir.empty() || options.output_dir.empty()) {
    return Status::fail(ErrorCode::kInvalidArgument,
                        "batch: input and output directories are required");
  }

  auto listing = list_images(options.input_dir, options.recursive);
  if (!listing) {
    return Status::fail(listing.error().code, listing.error().message);
  }

  const Registry& registry = Registry::instance();
  if (auto valid = validate_recipe(recipe, registry); !valid) {
    return valid;
  }

  std::error_code error;
  fs::create_directories(options.output_dir, error);
  if (error) {
    return Status::fail(ErrorCode::kIoError,
                        "batch: cannot create " + options.output_dir + ": " + error.message());
  }
  if (!fs::is_directory(options.output_dir, error) || error) {
    return Status::fail(ErrorCode::kIoError,
                        "batch: output is not a directory: " + options.output_dir);
  }

  const auto started = std::chrono::steady_clock::now();
  const auto& files = listing.value();
  summary->total = files.size();
  summary->items.reserve(files.size());

  for (std::size_t index = 0; index < files.size(); ++index) {
    if (cancelled && cancelled()) {
      summary->cancelled = true;
      break;
    }

    BatchItemResult item;
    item.input_path = files[index];
    item.output_path = batch_output_path(options, files[index]);

    const auto report = [&](const char* phase) {
      if (!on_progress) {
        return;
      }
      BatchProgress progress;
      progress.done = index;
      progress.total = files.size();
      progress.current_input = item.input_path;
      progress.phase = phase;
      on_progress(progress);
    };

    // Existing outputs are skipped before decoding: re-running a batch after a
    // crash should only pay for the files that are actually missing.
    if (!options.overwrite && fs::exists(item.output_path, error) && !error) {
      item.skipped = true;
      summary->items.push_back(std::move(item));
      ++summary->skipped;
      if (on_progress) {
        BatchProgress progress;
        progress.done = index + 1;
        progress.total = files.size();
        progress.current_input = summary->items.back().input_path;
        progress.phase = "skip";
        on_progress(progress);
      }
      continue;
    }

    const auto file_started = std::chrono::steady_clock::now();

    report("load");
    auto loaded = load_image(item.input_path);
    if (!loaded) {
      item.error = loaded.error().message;
      summary->items.push_back(std::move(item));
      ++summary->failed;
      continue;
    }

    report("process");
    auto result = execute(recipe, loaded.value().view(), registry);
    if (!result) {
      item.error = result.error().message;
      summary->items.push_back(std::move(item));
      ++summary->failed;
      continue;
    }

    const fs::path target(item.output_path);
    if (target.has_parent_path()) {
      fs::create_directories(target.parent_path(), error);
      error.clear();
    }

    report("save");
    if (auto saved = save_image(result.value().image, item.output_path); !saved) {
      item.error = saved.error().message;
      summary->items.push_back(std::move(item));
      ++summary->failed;
      continue;
    }

    item.ok = true;
    item.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - file_started)
            .count();
    summary->items.push_back(std::move(item));
    ++summary->succeeded;

    if (on_progress) {
      BatchProgress progress;
      progress.done = index + 1;
      progress.total = files.size();
      progress.current_input = summary->items.back().input_path;
      progress.phase = "done";
      on_progress(progress);
    }
  }

  summary->milliseconds =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  return Status::ok();
}

}  // namespace figtools
