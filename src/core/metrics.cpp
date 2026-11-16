// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "figtools/core/color.hpp"

namespace figtools {
namespace {

constexpr double kMaxValue = 255.0;
constexpr int kBlockSize = 8;
constexpr double kC1 = (0.01 * kMaxValue) * (0.01 * kMaxValue);
constexpr double kC2 = (0.03 * kMaxValue) * (0.03 * kMaxValue);

// SSIM over one 8x8 block of the luminance channel.
double block_ssim(const ImageView& lhs, const ImageView& rhs, int x0, int y0, int x1, int y1) {
  const double n = static_cast<double>((x1 - x0) * (y1 - y0));
  double sum_a = 0.0;
  double sum_b = 0.0;
  double sum_aa = 0.0;
  double sum_bb = 0.0;
  double sum_ab = 0.0;

  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const double a = static_cast<double>(luma(lhs.at(x, y)));
      const double b = static_cast<double>(luma(rhs.at(x, y)));
      sum_a += a;
      sum_b += b;
      sum_aa += a * a;
      sum_bb += b * b;
      sum_ab += a * b;
    }
  }

  const double mean_a = sum_a / n;
  const double mean_b = sum_b / n;
  const double var_a = (sum_aa / n) - mean_a * mean_a;
  const double var_b = (sum_bb / n) - mean_b * mean_b;
  const double cov = (sum_ab / n) - mean_a * mean_b;

  const double numerator = (2.0 * mean_a * mean_b + kC1) * (2.0 * cov + kC2);
  const double denominator = (mean_a * mean_a + mean_b * mean_b + kC1) * (var_a + var_b + kC2);
  if (denominator <= 0.0) {
    return 1.0;
  }
  return numerator / denominator;
}

}  // namespace

Result<ImageMetrics> compare_images(const ImageView& lhs, const ImageView& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return Result<ImageMetrics>::fail(ErrorCode::kInvalidArgument, "compare: empty image");
  }
  if (lhs.width() != rhs.width() || lhs.height() != rhs.height()) {
    return Result<ImageMetrics>::fail(ErrorCode::kInvalidArgument,
                                      "compare: size mismatch (" + std::to_string(lhs.width()) +
                                          "x" + std::to_string(lhs.height()) + " vs " +
                                          std::to_string(rhs.width()) + "x" +
                                          std::to_string(rhs.height()) + ")");
  }

  ImageMetrics metrics;
  double squared_error = 0.0;
  double absolute_error = 0.0;
  std::size_t changed = 0;
  const std::size_t pixels = lhs.size().pixel_count();

  for (int y = 0; y < lhs.height(); ++y) {
    for (int x = 0; x < lhs.width(); ++x) {
      const Rgba a = lhs.at(x, y);
      const Rgba b = rhs.at(x, y);
      const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
      const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
      const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
      squared_error += static_cast<double>(dr * dr + dg * dg + db * db);
      absolute_error += static_cast<double>(std::abs(dr) + std::abs(dg) + std::abs(db));
      if (dr != 0 || dg != 0 || db != 0) {
        ++changed;
      }
    }
  }

  const double samples = static_cast<double>(pixels) * 3.0;
  const double mse = squared_error / samples;
  metrics.mae = absolute_error / samples;
  metrics.changed_ratio = static_cast<double>(changed) / static_cast<double>(pixels);
  metrics.identical = (changed == 0);

  if (mse <= 0.0) {
    metrics.psnr = std::numeric_limits<double>::infinity();
  } else {
    metrics.psnr = 10.0 * std::log10((kMaxValue * kMaxValue) / mse);
  }

  double ssim_sum = 0.0;
  int blocks = 0;
  for (int y0 = 0; y0 < lhs.height(); y0 += kBlockSize) {
    for (int x0 = 0; x0 < lhs.width(); x0 += kBlockSize) {
      const int x1 = std::min(x0 + kBlockSize, lhs.width());
      const int y1 = std::min(y0 + kBlockSize, lhs.height());
      if (x1 <= x0 || y1 <= y0) {
        continue;
      }
      ssim_sum += block_ssim(lhs, rhs, x0, y0, x1, y1);
      ++blocks;
    }
  }
  metrics.ssim = (blocks == 0) ? 1.0 : ssim_sum / static_cast<double>(blocks);
  return Result<ImageMetrics>::ok(metrics);
}

}  // namespace figtools
