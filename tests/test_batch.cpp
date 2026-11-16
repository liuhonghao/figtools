// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/batch/batch.hpp"

#include <cstring>
#include <filesystem>
#include <system_error>

#include "figtools/io/image_io.hpp"
#include "figtools/pipeline/executor.hpp"
#include "figtools/ops/registry.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

namespace {

namespace fs = std::filesystem;

fs::path scratch(const std::string& name) {
  const fs::path path = fs::temp_directory_path() / ("figtools_batch_" + name);
  std::error_code error;
  fs::remove_all(path, error);
  fs::create_directories(path, error);
  return path;
}

figtools::Recipe contrast_recipe() {
  figtools::Recipe recipe;
  figtools::Step step;
  step.op = "contrast";
  step.params.set("factor", 1.2);
  recipe.steps.push_back(step);
  return recipe;
}

void write_ppm(const fs::path& path, int width, int height) {
  const auto image = ft_test::make_test_image(width, height);
  const auto status = figtools::save_image(image, path.string());
  CHECK(status.has_value());
}

figtools::BatchOptions options_for(const fs::path& input, const fs::path& output) {
  figtools::BatchOptions options;
  options.input_dir = input.string();
  options.output_dir = output.string();
  options.format = figtools::OutputFormat::kPpm;
  options.suffix = "_proc";
  return options;
}

}  // namespace

TEST(batch_lists_supported_images_sorted) {
  const fs::path dir = scratch("list");
  write_ppm(dir / "b.ppm", 8, 6);
  write_ppm(dir / "a.ppm", 8, 6);
  write_ppm(dir / "c.png", 8, 6);
  std::error_code error;
  fs::create_directories(dir / "nested", error);
  write_ppm(dir / "nested" / "d.ppm", 8, 6);

  auto flat = figtools::list_images(dir.string(), false);
  CHECK(flat.has_value());
  if (flat) {
    CHECK_EQ(flat.value().size(), static_cast<std::size_t>(3));
    CHECK(flat.value().front().find("a.ppm") != std::string::npos);
    CHECK(flat.value().back().find("c.png") != std::string::npos);
  }

  auto deep = figtools::list_images(dir.string(), true);
  CHECK(deep.has_value());
  if (deep) {
    CHECK_EQ(deep.value().size(), static_cast<std::size_t>(4));
  }
}

TEST(batch_lists_rejects_missing_directory) {
  auto listed = figtools::list_images("definitely/not/here", false);
  CHECK(!listed.has_value());
  CHECK(listed.error().code == figtools::ErrorCode::kIoError);
}

TEST(batch_runs_recipe_and_writes_outputs) {
  const fs::path input = scratch("run_in");
  const fs::path output = scratch("run_out");
  write_ppm(input / "one.ppm", 12, 9);
  write_ppm(input / "two.ppm", 12, 9);
  write_ppm(input / "three.ppm", 12, 9);

  const figtools::Recipe recipe = contrast_recipe();
  auto summary = figtools::BatchSummary{};
  const auto status =
      figtools::run_batch(recipe, options_for(input, output), {}, {}, &summary);

  CHECK(status.has_value());
  CHECK_EQ(summary.total, static_cast<std::size_t>(3));
  CHECK_EQ(summary.succeeded, static_cast<std::size_t>(3));
  CHECK_EQ(summary.failed, static_cast<std::size_t>(0));
  CHECK(!summary.cancelled);

  // Output must be byte-identical to running the same recipe on one image:
  // batch is a loop, not a second implementation.
  auto direct = figtools::execute(recipe, ft_test::make_test_image(12, 9).view(),
                                  figtools::Registry::instance());
  CHECK(direct.has_value());
  auto stored = figtools::load_image((output / "one_proc.ppm").string());
  CHECK(stored.has_value());
  if (direct && stored) {
    CHECK_EQ(stored.value().byte_size(), direct.value().image.byte_size());
    CHECK(std::memcmp(stored.value().data(), direct.value().image.data(),
                      static_cast<std::size_t>(direct.value().image.byte_size())) == 0);
  }
}

TEST(batch_skips_existing_outputs) {
  const fs::path input = scratch("skip_in");
  const fs::path output = scratch("skip_out");
  write_ppm(input / "a.ppm", 8, 8);
  write_ppm(input / "b.ppm", 8, 8);

  const figtools::BatchOptions options = options_for(input, output);
  auto first = figtools::BatchSummary{};
  CHECK(figtools::run_batch(contrast_recipe(), options, {}, {}, &first).has_value());
  CHECK_EQ(first.succeeded, static_cast<std::size_t>(2));

  auto second = figtools::BatchSummary{};
  CHECK(figtools::run_batch(contrast_recipe(), options, {}, {}, &second).has_value());
  CHECK_EQ(second.succeeded, static_cast<std::size_t>(0));
  CHECK_EQ(second.skipped, static_cast<std::size_t>(2));
}

TEST(batch_overwrite_replaces_outputs) {
  const fs::path input = scratch("over_in");
  const fs::path output = scratch("over_out");
  write_ppm(input / "a.ppm", 8, 8);

  figtools::BatchOptions options = options_for(input, output);
  auto first = figtools::BatchSummary{};
  CHECK(figtools::run_batch(contrast_recipe(), options, {}, {}, &first).has_value());

  options.overwrite = true;
  auto second = figtools::BatchSummary{};
  CHECK(figtools::run_batch(contrast_recipe(), options, {}, {}, &second).has_value());
  CHECK_EQ(second.succeeded, static_cast<std::size_t>(1));
  CHECK_EQ(second.skipped, static_cast<std::size_t>(0));
}

TEST(batch_cancel_stops_before_any_work) {
  const fs::path input = scratch("cancel_in");
  const fs::path output = scratch("cancel_out");
  write_ppm(input / "a.ppm", 8, 8);
  write_ppm(input / "b.ppm", 8, 8);

  auto summary = figtools::BatchSummary{};
  const auto status =
      figtools::run_batch(contrast_recipe(), options_for(input, output), []() { return true; }, {},
                          &summary);

  CHECK(status.has_value());
  CHECK(summary.cancelled);
  CHECK_EQ(summary.succeeded, static_cast<std::size_t>(0));
  CHECK_EQ(summary.failed, static_cast<std::size_t>(0));

  std::error_code error;
  CHECK(!fs::exists(output / "a_proc.ppm", error));
}

TEST(batch_preserves_subdirectories_when_recursive) {
  const fs::path input = scratch("deep_in");
  const fs::path output = scratch("deep_out");
  std::error_code error;
  fs::create_directories(input / "line_a", error);
  write_ppm(input / "top.ppm", 8, 8);
  write_ppm(input / "line_a" / "inner.ppm", 8, 8);

  figtools::BatchOptions options = options_for(input, output);
  options.recursive = true;
  auto summary = figtools::BatchSummary{};
  CHECK(figtools::run_batch(contrast_recipe(), options, {}, {}, &summary).has_value());
  CHECK_EQ(summary.succeeded, static_cast<std::size_t>(2));
  CHECK(fs::exists(output / "top_proc.ppm", error));
  CHECK(fs::exists(output / "line_a" / "inner_proc.ppm", error));
}

TEST(batch_reports_progress_monotonically) {
  const fs::path input = scratch("prog_in");
  const fs::path output = scratch("prog_out");
  write_ppm(input / "a.ppm", 8, 8);
  write_ppm(input / "b.ppm", 8, 8);

  std::size_t last_done = 0;
  bool saw_process = false;
  auto summary = figtools::BatchSummary{};
  CHECK(figtools::run_batch(
            contrast_recipe(), options_for(input, output), {},
            [&](const figtools::BatchProgress& progress) {
              CHECK(progress.done >= last_done);
              last_done = progress.done;
              if (progress.phase == "process") {
                saw_process = true;
              }
            },
            &summary)
            .has_value());

  CHECK(saw_process);
  CHECK_EQ(last_done, static_cast<std::size_t>(2));
}
