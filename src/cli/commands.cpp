// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "cli/commands.hpp"

#ifndef FIGTOOLS_VERSION
#define FIGTOOLS_VERSION "0.1.0"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "figtools/core/color.hpp"
#include "figtools/core/hash.hpp"
#include "figtools/core/metrics.hpp"
#include "figtools/core/sample_scene.hpp"
#include "figtools/io/image_io.hpp"
#include "figtools/ops/registry.hpp"
#include "figtools/pipeline/executor.hpp"
#include "figtools/pipeline/json.hpp"
#include "figtools/pipeline/recipe.hpp"

namespace figtools::cli {
namespace {

std::string human_bytes(std::uint64_t bytes) {
  const char* units[] = {"B", "KB", "MB", "GB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 3) {
    value /= 1024.0;
    ++unit;
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.2f %s", value, units[unit]);
  return buffer;
}

struct ImageStats {
  double mean_r = 0.0;
  double mean_g = 0.0;
  double mean_b = 0.0;
  double mean_luma = 0.0;
  double sharpness = 0.0;
  double overexposed = 0.0;
  double underexposed = 0.0;
};

ImageStats compute_stats(const ImageView& image) {
  ImageStats stats;
  double sum_r = 0.0;
  double sum_g = 0.0;
  double sum_b = 0.0;
  double sum_luma = 0.0;
  double sum_lap = 0.0;
  int lap_count = 0;
  std::size_t over = 0;
  std::size_t under = 0;

  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const Rgba c = image.at(x, y);
      const std::uint8_t l = luma(c);
      sum_r += c.r;
      sum_g += c.g;
      sum_b += c.b;
      sum_luma += l;
      if (l >= 250) {
        ++over;
      }
      if (l <= 5) {
        ++under;
      }
    }
  }
  for (int y = 1; y < image.height() - 1; y += 2) {
    for (int x = 1; x < image.width() - 1; x += 2) {
      const int centre = static_cast<int>(luma(image.at(x, y)));
      const int up = static_cast<int>(luma(image.at(x, y - 1)));
      const int down = static_cast<int>(luma(image.at(x, y + 1)));
      const int left = static_cast<int>(luma(image.at(x - 1, y)));
      const int right = static_cast<int>(luma(image.at(x + 1, y)));
      sum_lap += std::abs(4 * centre - up - down - left - right);
      ++lap_count;
    }
  }

  const double pixels = static_cast<double>(image.size().pixel_count());
  stats.mean_r = sum_r / pixels;
  stats.mean_g = sum_g / pixels;
  stats.mean_b = sum_b / pixels;
  stats.mean_luma = sum_luma / pixels;
  stats.sharpness = lap_count > 0 ? sum_lap / static_cast<double>(lap_count) : 0.0;
  stats.overexposed = static_cast<double>(over) / pixels;
  stats.underexposed = static_cast<double>(under) / pixels;
  return stats;
}

void print_metrics(const ImageMetrics& metrics) {
  std::printf("  PSNR             : %s\n",
              std::isinf(metrics.psnr) ? "inf (identical)"
                                        : (std::to_string(metrics.psnr) + " dB").c_str());
  std::printf("  SSIM             : %.4f\n", metrics.ssim);
  std::printf("  MAE              : %.2f / 255\n", metrics.mae);
  std::printf("  Changed pixels   : %.1f %%\n", metrics.changed_ratio * 100.0);
  std::printf("  Identical        : %s\n", metrics.identical ? "yes" : "no");
}

}  // namespace

void print_usage() {
  std::printf(
      "figtools %s -- deterministic image processing workbench\n"
      "\n"
      "Usage: figtools <command> [options]\n"
      "\n"
      "Commands:\n"
      "  info <image>                          Show format, size and statistics\n"
      "  ops [--group <name>] [--json]         List registered operators\n"
      "  apply -i <in> -o <out> -r <recipe>    Apply a recipe (JSON) to an image\n"
      "        [--trace]                       Print per-step timing\n"
      "  compare -a <before> -b <after>        PSNR / SSIM / MAE report\n"
      "        [--json]\n"
      "  demo -o <dir> [--size WxH] [--seed N] Generate a sample scene and run a pipeline\n"
      "  version                               Print version\n"
      "  help                                  Show this message\n"
      "\n"
      "Formats: PNG (8-bit), BMP (24/32-bit), PPM/PGM (8-bit)\n",
      FIGTOOLS_VERSION);
}

int command_version() {
  std::printf("figtools %s\n", FIGTOOLS_VERSION);
  return 0;
}

int command_info(const std::string& path) {
  auto info = probe_image(path);
  if (!info) {
    std::fprintf(stderr, "error: %s\n", info.error().message.c_str());
    return 1;
  }
  auto image = load_image(path);
  if (!image) {
    std::fprintf(stderr, "error: %s\n", image.error().message.c_str());
    return 1;
  }
  const ImageStats stats = compute_stats(image.value().view());

  std::printf("file        : %s\n", path.c_str());
  std::printf("format      : %s\n", to_string(info.value().format));
  std::printf("size        : %d x %d\n", info.value().width, info.value().height);
  std::printf("colour      : %s (%d bit/channel)\n", info.value().color_model.c_str(),
              info.value().bit_depth);
  std::printf("alpha       : %s\n", info.value().has_alpha ? "yes" : "no");
  std::printf("compression : %s\n", info.value().compression.c_str());
  std::printf("file size   : %s\n", human_bytes(info.value().file_bytes).c_str());
  std::printf("hash        : %s\n", to_hex(hash_image(image.value().view())).c_str());
  std::printf("mean rgb    : %.1f, %.1f, %.1f\n", stats.mean_r, stats.mean_g, stats.mean_b);
  std::printf("mean luma   : %.1f\n", stats.mean_luma);
  std::printf("sharpness   : %.1f\n", stats.sharpness);
  std::printf("clipping    : %.2f%% highlights, %.2f%% shadows\n", stats.overexposed * 100.0,
              stats.underexposed * 100.0);
  return 0;
}

int command_ops(const std::string& group, bool as_json) {
  const Registry& registry = Registry::instance();
  if (as_json) {
    Json root = Json::array();
    for (const Operator* op : registry.all()) {
      if (!group.empty() && op->spec.group != group) {
        continue;
      }
      Json node = Json::object();
      node.set("id", Json(op->spec.id));
      node.set("label", Json(op->spec.label));
      node.set("group", Json(op->spec.group));
      node.set("summary", Json(op->spec.summary));
      node.set("geometry", Json(op->spec.geometry));
      node.set("version", Json(static_cast<double>(op->spec.version)));
      Json params = Json::array();
      for (const ParamSpec& param : op->spec.params) {
        Json p = Json::object();
        p.set("key", Json(param.key));
        p.set("label", Json(param.label));
        p.set("type", Json(std::string(param.type == ParamType::kNumber    ? "number"
                                       : param.type == ParamType::kInteger ? "integer"
                                       : param.type == ParamType::kBool    ? "bool"
                                                                           : "enum")));
        p.set("min", Json(param.min_value));
        p.set("max", Json(param.max_value));
        p.set("default", Json(param.default_value));
        if (!param.unit.empty()) {
          p.set("unit", Json(param.unit));
        }
        p.set("doc", Json(param.doc));
        params.push_back(std::move(p));
      }
      node.set("params", std::move(params));
      root.push_back(std::move(node));
    }
    std::printf("%s\n", root.dump(2).c_str());
    return 0;
  }

  std::string current_group;
  for (const Operator* op : registry.all()) {
    if (!group.empty() && op->spec.group != group) {
      continue;
    }
    if (op->spec.group != current_group) {
      current_group = op->spec.group;
      std::printf("\n[%s]\n", current_group.c_str());
    }
    std::printf("  %-16s %-10s %s\n", op->spec.id.c_str(), op->spec.label.c_str(),
                op->spec.summary.c_str());
    for (const ParamSpec& param : op->spec.params) {
      if (param.type == ParamType::kEnum) {
        std::string values;
        for (const auto& value : param.enum_values) {
          values += values.empty() ? value : "|" + value;
        }
        std::printf("      %-12s %-14s %s\n", param.key.c_str(), param.label.c_str(),
                    values.c_str());
      } else if (param.type == ParamType::kBool) {
        std::printf("      %-12s %-14s true|false\n", param.key.c_str(), param.label.c_str());
      } else {
        std::printf("      %-12s %-14s %g..%g  default %g %s\n", param.key.c_str(),
                    param.label.c_str(), param.min_value, param.max_value, param.default_value,
                    param.unit.c_str());
      }
    }
  }
  std::printf("\n%d operators registered.\n", static_cast<int>(registry.size()));
  return 0;
}

int command_apply(const std::string& input, const std::string& output,
                  const std::string& recipe_path, bool trace) {
  auto image = load_image(input);
  if (!image) {
    std::fprintf(stderr, "error: %s\n", image.error().message.c_str());
    return 1;
  }

  std::FILE* file = std::fopen(recipe_path.c_str(), "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "error: cannot open recipe: %s\n", recipe_path.c_str());
    return 1;
  }
  std::string text;
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, read);
  }
  std::fclose(file);

  auto recipe = Recipe::from_json(text);
  if (!recipe) {
    std::fprintf(stderr, "error: %s\n", recipe.error().message.c_str());
    return 1;
  }

  const Registry& registry = Registry::instance();
  auto validation = validate_recipe(recipe.value(), registry);
  if (!validation) {
    std::fprintf(stderr, "error: %s\n", validation.error().message.c_str());
    return 1;
  }

  auto result = execute(recipe.value(), image.value().view(), registry);
  if (!result) {
    std::fprintf(stderr, "error: %s\n", result.error().message.c_str());
    return 1;
  }

  auto status = save_image(result.value().image, output);
  if (!status) {
    std::fprintf(stderr, "error: %s\n", status.error().message.c_str());
    return 1;
  }

  std::printf("applied   : %zu step(s) in %.2f ms\n", result.value().trace.size(),
              result.value().total_milliseconds);
  std::printf("cache key : %s\n", to_hex(result.value().cache_key).c_str());
  std::printf("output    : %s (%d x %d)\n", output.c_str(), result.value().image.width(),
              result.value().image.height());
  std::printf("hash      : %s\n", to_hex(hash_image(result.value().image.view())).c_str());
  if (trace) {
    for (const StepTrace& step : result.value().trace) {
      std::printf("  %-16s %8.2f ms  %d x %d\n", step.op.c_str(), step.milliseconds,
                  step.output_size.width, step.output_size.height);
    }
  }
  return 0;
}

int command_compare(const std::string& lhs_path, const std::string& rhs_path, bool as_json) {
  auto lhs = load_image(lhs_path);
  if (!lhs) {
    std::fprintf(stderr, "error: %s\n", lhs.error().message.c_str());
    return 1;
  }
  auto rhs = load_image(rhs_path);
  if (!rhs) {
    std::fprintf(stderr, "error: %s\n", rhs.error().message.c_str());
    return 1;
  }
  auto metrics = compare_images(lhs.value().view(), rhs.value().view());
  if (!metrics) {
    std::fprintf(stderr, "error: %s\n", metrics.error().message.c_str());
    return 1;
  }

  if (as_json) {
    Json root = Json::object();
    root.set("psnr", Json(metrics.value().psnr));
    root.set("ssim", Json(metrics.value().ssim));
    root.set("mae", Json(metrics.value().mae));
    root.set("changed_ratio", Json(metrics.value().changed_ratio));
    root.set("identical", Json(metrics.value().identical));
    std::printf("%s\n", root.dump(2).c_str());
    return 0;
  }

  std::printf("compare   : %s  vs  %s\n", lhs_path.c_str(), rhs_path.c_str());
  print_metrics(metrics.value());
  return 0;
}

int command_demo(const std::string& output_dir, int width, int height, unsigned seed) {
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::fprintf(stderr, "error: cannot create %s: %s\n", output_dir.c_str(), ec.message().c_str());
    return 1;
  }

  const int w = std::max(64, width);
  const int h = std::max(64, height);
  Image before = make_sample_scene(w, h, seed);

  const std::string dir = output_dir;
  const std::string before_path = dir + "/before.png";
  const std::string after_path = dir + "/after.png";
  const std::string recipe_path = dir + "/recipe.json";
  const std::string report_path = dir + "/report.txt";

  auto save_before = save_image(before, before_path);
  if (!save_before) {
    std::fprintf(stderr, "error: %s\n", save_before.error().message.c_str());
    return 1;
  }

  Recipe recipe;
  recipe.version = 1;
  recipe.seed = seed;

  Step denoise;
  denoise.op = "median";
  denoise.params.set("radius", 1.0);
  recipe.steps.push_back(denoise);

  Step contrast;
  contrast.op = "contrast";
  contrast.params.set("factor", 1.12);
  recipe.steps.push_back(contrast);

  Step sharpen;
  sharpen.op = "unsharp";
  sharpen.params.set("amount", 0.75);
  sharpen.params.set("radius", 1.2);
  sharpen.params.set("threshold", 0.01);
  recipe.steps.push_back(sharpen);

  Step gamma;
  gamma.op = "gamma";
  gamma.params.set("gamma", 1.05);
  recipe.steps.push_back(gamma);

  std::FILE* recipe_file = std::fopen(recipe_path.c_str(), "wb");
  if (recipe_file != nullptr) {
    const std::string text = recipe.to_json(2);
    std::fwrite(text.data(), 1, text.size(), recipe_file);
    std::fclose(recipe_file);
  }

  const Registry& registry = Registry::instance();
  auto result = execute(recipe, before.view(), registry);
  if (!result) {
    std::fprintf(stderr, "error: %s\n", result.error().message.c_str());
    return 1;
  }
  auto save_after = save_image(result.value().image, after_path);
  if (!save_after) {
    std::fprintf(stderr, "error: %s\n", save_after.error().message.c_str());
    return 1;
  }

  auto metrics = compare_images(before.view(), result.value().image.view());

  std::printf("demo      : %d x %d, seed %u\n", w, h, seed);
  std::printf("before    : %s\n", before_path.c_str());
  std::printf("after     : %s\n", after_path.c_str());
  std::printf("recipe    : %s\n", recipe_path.c_str());
  std::printf("steps     : %zu in %.2f ms\n", result.value().trace.size(),
              result.value().total_milliseconds);
  for (const StepTrace& step : result.value().trace) {
    std::printf("  %-14s %7.2f ms\n", step.op.c_str(), step.milliseconds);
  }
  if (metrics) {
    print_metrics(metrics.value());
  }
  std::printf("cache key : %s\n", to_hex(result.value().cache_key).c_str());

  std::FILE* report = std::fopen(report_path.c_str(), "wb");
  if (report != nullptr) {
    const std::string text = "figtools demo report\n" + std::string("size: ") +
                             std::to_string(w) + "x" + std::to_string(h) + "\n" +
                             "steps: " + std::to_string(result.value().trace.size()) + "\n" +
                             "time_ms: " + std::to_string(result.value().total_milliseconds) + "\n" +
                             "cache_key: " + to_hex(result.value().cache_key) + "\n" +
                             (metrics ? "psnr: " + std::to_string(metrics.value().psnr) + "\n" : "") +
                             (metrics ? "ssim: " + std::to_string(metrics.value().ssim) + "\n" : "");
    std::fwrite(text.data(), 1, text.size(), report);
    std::fclose(report);
  }
  return 0;
}

}  // namespace figtools::cli
