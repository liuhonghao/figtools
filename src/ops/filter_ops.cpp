// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "figtools/core/color.hpp"
#include "figtools/ops/registry.hpp"
#include "ops/register.hpp"

namespace figtools::detail {
namespace {

ParamSpec number_param(std::string key, std::string label, std::string doc, double min_value,
                       double max_value, double default_value, std::string unit = "") {
  ParamSpec spec;
  spec.key = std::move(key);
  spec.label = std::move(label);
  spec.doc = std::move(doc);
  spec.unit = std::move(unit);
  spec.type = ParamType::kNumber;
  spec.min_value = min_value;
  spec.max_value = max_value;
  spec.default_value = default_value;
  return spec;
}

ParamSpec bool_param(std::string key, std::string label, std::string doc, bool default_value) {
  ParamSpec spec;
  spec.key = std::move(key);
  spec.label = std::move(label);
  spec.doc = std::move(doc);
  spec.type = ParamType::kBool;
  spec.default_value = default_value ? 1.0 : 0.0;
  return spec;
}

Result<Image> box_blur(const ImageView& src, int radius) {
  if (radius < 1) {
    radius = 1;
  }
  const int width = src.width();
  const int height = src.height();
  Image horizontal(width, height);
  MutableImageView mid = horizontal.mutable_view();
  Image dst(width, height);
  MutableImageView out = dst.mutable_view();

  const int window = radius * 2 + 1;
  for (int y = 0; y < height; ++y) {
    int acc_r = 0;
    int acc_g = 0;
    int acc_b = 0;
    int acc_a = 0;
    for (int k = -radius; k <= radius; ++k) {
      const Rgba c = src.at_clamped(k, y);
      acc_r += c.r;
      acc_g += c.g;
      acc_b += c.b;
      acc_a += c.a;
    }
    for (int x = 0; x < width; ++x) {
      mid.set(x, y,
              Rgba{static_cast<std::uint8_t>(acc_r / window), static_cast<std::uint8_t>(acc_g / window),
                   static_cast<std::uint8_t>(acc_b / window),
                   static_cast<std::uint8_t>(acc_a / window)});
      const Rgba leaving = src.at_clamped(x - radius, y);
      const Rgba entering = src.at_clamped(x + radius + 1, y);
      acc_r += entering.r - leaving.r;
      acc_g += entering.g - leaving.g;
      acc_b += entering.b - leaving.b;
      acc_a += entering.a - leaving.a;
    }
  }

  const ImageView mid_view = horizontal.view();
  for (int x = 0; x < width; ++x) {
    int acc_r = 0;
    int acc_g = 0;
    int acc_b = 0;
    int acc_a = 0;
    for (int k = -radius; k <= radius; ++k) {
      const Rgba c = mid_view.at_clamped(x, k);
      acc_r += c.r;
      acc_g += c.g;
      acc_b += c.b;
      acc_a += c.a;
    }
    for (int y = 0; y < height; ++y) {
      out.set(x, y,
              Rgba{static_cast<std::uint8_t>(acc_r / window), static_cast<std::uint8_t>(acc_g / window),
                   static_cast<std::uint8_t>(acc_b / window),
                   static_cast<std::uint8_t>(acc_a / window)});
      const Rgba leaving = mid_view.at_clamped(x, y - radius);
      const Rgba entering = mid_view.at_clamped(x, y + radius + 1);
      acc_r += entering.r - leaving.r;
      acc_g += entering.g - leaving.g;
      acc_b += entering.b - leaving.b;
      acc_a += entering.a - leaving.a;
    }
  }
  return Result<Image>::ok(std::move(dst));
}

std::vector<float> gaussian_kernel(double sigma) {
  const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
  std::vector<float> kernel(static_cast<std::size_t>(radius) * 2U + 1U);
  double sum = 0.0;
  for (int i = -radius; i <= radius; ++i) {
    const double value = std::exp(-(static_cast<double>(i) * static_cast<double>(i)) /
                                  (2.0 * sigma * sigma));
    kernel[static_cast<std::size_t>(i + radius)] = static_cast<float>(value);
    sum += value;
  }
  for (auto& value : kernel) {
    value = static_cast<float>(static_cast<double>(value) / sum);
  }
  return kernel;
}

Result<Image> gaussian_blur(const ImageView& src, double sigma) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "gaussian_blur: empty image");
  }
  const int width = src.width();
  const int height = src.height();
  const std::vector<float> kernel = gaussian_kernel(sigma);
  const int radius = static_cast<int>(kernel.size() / 2U);

  std::vector<float> temp(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U,
                          0.0F);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::array<float, 4> acc{0.0F, 0.0F, 0.0F, 0.0F};
      for (int k = -radius; k <= radius; ++k) {
        const Rgba c = src.at_clamped(x + k, y);
        const float weight = kernel[static_cast<std::size_t>(k + radius)];
        acc[0] += static_cast<float>(c.r) * weight;
        acc[1] += static_cast<float>(c.g) * weight;
        acc[2] += static_cast<float>(c.b) * weight;
        acc[3] += static_cast<float>(c.a) * weight;
      }
      const std::size_t index =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x)) *
          4U;
      temp[index + 0] = acc[0];
      temp[index + 1] = acc[1];
      temp[index + 2] = acc[2];
      temp[index + 3] = acc[3];
    }
  }

  Image dst(width, height);
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      std::array<float, 4> acc{0.0F, 0.0F, 0.0F, 0.0F};
      for (int k = -radius; k <= radius; ++k) {
        const int sy = std::clamp(y + k, 0, height - 1);
        const std::size_t index =
            (static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)) *
            4U;
        const float weight = kernel[static_cast<std::size_t>(k + radius)];
        acc[0] += temp[index + 0] * weight;
        acc[1] += temp[index + 1] * weight;
        acc[2] += temp[index + 2] * weight;
        acc[3] += temp[index + 3] * weight;
      }
      out.set(x, y, Rgba{clamp_u8(static_cast<double>(acc[0])),
                         clamp_u8(static_cast<double>(acc[1])),
                         clamp_u8(static_cast<double>(acc[2])),
                         clamp_u8(static_cast<double>(acc[3]))});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_box_blur(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "box_blur: empty image");
  }
  return box_blur(src, params.integer("radius", 1));
}

Result<Image> op_gaussian_blur(const ImageView& src, const Params& params) {
  return gaussian_blur(src, params.number("sigma", 1.0));
}

Result<Image> op_median(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "median: empty image");
  }
  const int radius = std::clamp(params.integer("radius", 1), 1, 2);
  const int window = radius * 2 + 1;
  const std::size_t count = static_cast<std::size_t>(window) * static_cast<std::size_t>(window);
  const std::size_t middle = count / 2;

  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  std::array<std::uint8_t, 25> values{};

  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      std::size_t index = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          values[index++] = src.at_clamped(x + dx, y + dy).r;
        }
      }
      std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
      const std::uint8_t r = values[middle];
      index = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          values[index++] = src.at_clamped(x + dx, y + dy).g;
        }
      }
      std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
      const std::uint8_t g = values[middle];
      index = 0;
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          values[index++] = src.at_clamped(x + dx, y + dy).b;
        }
      }
      std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
      const std::uint8_t b = values[middle];
      out.set(x, y, Rgba{r, g, b, src.at(x, y).a});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_unsharp(const ImageView& src, const Params& params) {
  const double amount = params.number("amount", 0.6);
  const double sigma = params.number("radius", 1.2);
  const double threshold = params.number("threshold", 0.02) * 255.0;

  auto blurred = gaussian_blur(src, sigma);
  if (!blurred) {
    return blurred;
  }
  const ImageView blur_view = blurred.value().view();

  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      const Rgba c = src.at(x, y);
      const Rgba b = blur_view.at(x, y);
      const auto sharpen = [&](std::uint8_t original, std::uint8_t smooth) {
        const double diff = static_cast<double>(original) - static_cast<double>(smooth);
        if (std::abs(diff) < threshold) {
          return original;
        }
        return clamp_u8(static_cast<double>(original) + diff * amount);
      };
      out.set(x, y, Rgba{sharpen(c.r, b.r), sharpen(c.g, b.g), sharpen(c.b, b.b), c.a});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_sobel(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "sobel: empty image");
  }
  const double strength = params.number("strength", 1.0);
  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();

  constexpr std::array<int, 9> kGx{-1, 0, 1, -2, 0, 2, -1, 0, 1};
  constexpr std::array<int, 9> kGy{-1, -2, -1, 0, 0, 0, 1, 2, 1};

  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      int gx = 0;
      int gy = 0;
      int index = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int value = static_cast<int>(luma(src.at_clamped(x + dx, y + dy)));
          gx += value * kGx[static_cast<std::size_t>(index)];
          gy += value * kGy[static_cast<std::size_t>(index)];
          ++index;
        }
      }
      const double magnitude =
          std::sqrt(static_cast<double>(gx * gx + gy * gy)) * strength;
      const std::uint8_t v = clamp_u8(magnitude);
      out.set(x, y, Rgba{v, v, v, src.at(x, y).a});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_equalize(const ImageView& src, const Params& params) {
  (void)params;
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "equalize: empty image");
  }

  std::array<double, 256> histogram{};
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      histogram[luma(src.at(x, y))] += 1.0;
    }
  }
  const double total = static_cast<double>(src.size().pixel_count());
  std::array<std::uint8_t, 256> lut{};
  double cumulative = 0.0;
  for (std::size_t i = 0; i < lut.size(); ++i) {
    cumulative += histogram[i];
    lut[i] = clamp_u8(cumulative / total * 255.0);
  }

  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      const Rgba c = src.at(x, y);
      const std::uint8_t old_luma = luma(c);
      if (old_luma == 0) {
        out.set(x, y, Rgba{0, 0, 0, c.a});
        continue;
      }
      const double gain = static_cast<double>(lut[old_luma]) / static_cast<double>(old_luma);
      out.set(x, y, Rgba{clamp_u8(static_cast<double>(c.r) * gain),
                         clamp_u8(static_cast<double>(c.g) * gain),
                         clamp_u8(static_cast<double>(c.b) * gain), c.a});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> threshold_luma(const ImageView& src, std::uint8_t threshold, bool invert) {
  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      const Rgba c = src.at(x, y);
      const bool foreground = luma(c) >= threshold;
      const bool white = invert ? !foreground : foreground;
      const std::uint8_t v = white ? 255 : 0;
      out.set(x, y, Rgba{v, v, v, c.a});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_threshold(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "threshold: empty image");
  }
  const auto value = static_cast<std::uint8_t>(std::clamp(params.integer("value", 128), 0, 255));
  return threshold_luma(src, value, params.boolean("invert", false));
}

Result<Image> op_otsu(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "otsu: empty image");
  }
  std::array<double, 256> histogram{};
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      histogram[luma(src.at(x, y))] += 1.0;
    }
  }
  const double total = static_cast<double>(src.size().pixel_count());
  double sum_all = 0.0;
  for (std::size_t i = 0; i < histogram.size(); ++i) {
    sum_all += static_cast<double>(i) * histogram[i];
  }

  double sum_background = 0.0;
  double weight_background = 0.0;
  double best_variance = -1.0;
  int best_threshold = 128;
  for (std::size_t i = 0; i < histogram.size(); ++i) {
    weight_background += histogram[i];
    if (weight_background <= 0.0) {
      continue;
    }
    const double weight_foreground = total - weight_background;
    if (weight_foreground <= 0.0) {
      break;
    }
    sum_background += static_cast<double>(i) * histogram[i];
    const double mean_background = sum_background / weight_background;
    const double mean_foreground = (sum_all - sum_background) / weight_foreground;
    const double delta = mean_background - mean_foreground;
    const double variance = weight_background * weight_foreground * delta * delta;
    if (variance > best_variance) {
      best_variance = variance;
      best_threshold = static_cast<int>(i);
    }
  }

  return threshold_luma(src, static_cast<std::uint8_t>(best_threshold),
                        params.boolean("invert", false));
}

Result<Image> morphology(const ImageView& src, int radius, bool dilate) {
  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      Rgba result = src.at(x, y);
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          const Rgba c = src.at_clamped(x + dx, y + dy);
          const auto pick = [dilate](std::uint8_t a, std::uint8_t b) {
            return dilate ? std::max(a, b) : std::min(a, b);
          };
          result.r = pick(result.r, c.r);
          result.g = pick(result.g, c.g);
          result.b = pick(result.b, c.b);
        }
      }
      out.set(x, y, result);
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_dilate(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "dilate: empty image");
  }
  return morphology(src, params.integer("radius", 1), true);
}

Result<Image> op_erode(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "erode: empty image");
  }
  return morphology(src, params.integer("radius", 1), false);
}

Result<Image> op_posterize(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "posterize: empty image");
  }
  const int levels = std::clamp(params.integer("levels", 8), 2, 64);
  const double step = 255.0 / static_cast<double>(levels - 1);
  const auto quantise = [step](std::uint8_t v) {
    const double index = std::round(static_cast<double>(v) / step);
    return clamp_u8(index * step);
  };

  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      const Rgba c = src.at(x, y);
      out.set(x, y, Rgba{quantise(c.r), quantise(c.g), quantise(c.b), c.a});
    }
  }
  return Result<Image>::ok(std::move(dst));
}

}  // namespace

void register_filter_ops(Registry& registry) {
  registry.add(Operator{
      OperatorSpec{"box_blur", "均值模糊", "filter", "滑动窗口均值，速度快但会糊边。", 1, false,
                   {number_param("radius", "半径", "窗口为 (2r+1)²", 1.0, 20.0, 1.0, "px")}},
      op_box_blur});

  registry.add(Operator{
      OperatorSpec{"gaussian_blur", "高斯模糊", "filter", "可分离高斯核，sigma 越大越糊。", 1, false,
                   {number_param("sigma", "标准差", "3σ 之外的权重可忽略", 0.1, 10.0, 1.0, "px")}},
      op_gaussian_blur});

  registry.add(Operator{
      OperatorSpec{"median", "中值滤波", "filter", "对椒盐噪声很有效，半径 1 为 3×3。", 1, false,
                   {number_param("radius", "半径", "1 或 2", 1.0, 2.0, 1.0, "px")}},
      op_median});

  registry.add(Operator{
      OperatorSpec{"unsharp", "USM 锐化", "filter", "原图减去模糊图后按比例加回，阈值抑制噪声放大。",
                   1, false,
                   {number_param("amount", "强度", "0 不变，1 为全量叠加", 0.0, 2.0, 0.6),
                    number_param("radius", "模糊半径", "决定锐化尺度", 0.3, 5.0, 1.2, "px"),
                    number_param("threshold", "阈值", "低于该差异不锐化", 0.0, 0.2, 0.02)}},
      op_unsharp});

  registry.add(Operator{
      OperatorSpec{"sobel", "边缘检测", "filter", "Sobel 梯度幅值，输出灰度图。", 1, false,
                   {number_param("strength", "增益", "放大弱边缘", 0.1, 4.0, 1.0, "x")}},
      op_sobel});

  registry.add(Operator{
      OperatorSpec{"equalize", "直方图均衡", "filter", "按亮度累积分布重映射，保持色相比例。", 1,
                   false, {}},
      op_equalize});

  registry.add(Operator{
      OperatorSpec{"threshold", "固定阈值", "analysis", "按亮度二值化，适合稳定光照下的缺陷分割。", 1,
                   false,
                   {number_param("value", "阈值", "亮度高于该值判为目标", 0.0, 255.0, 128.0),
                    bool_param("invert", "反相", "目标与背景互换", false)}},
      op_threshold});

  registry.add(Operator{
      OperatorSpec{"otsu", "Otsu 自动阈值", "analysis", "最大化类间方差自动选取阈值。", 1, false,
                   {bool_param("invert", "反相", "目标与背景互换", false)}},
      op_otsu});

  registry.add(Operator{
      OperatorSpec{"dilate", "膨胀", "analysis", "扩大亮区，常用于连接断裂的缺陷。", 1, false,
                   {number_param("radius", "半径", "结构元为 (2r+1)²", 1.0, 5.0, 1.0, "px")}},
      op_dilate});

  registry.add(Operator{
      OperatorSpec{"erode", "腐蚀", "analysis", "缩小亮区，去除孤立噪点。", 1, false,
                   {number_param("radius", "半径", "结构元为 (2r+1)²", 1.0, 5.0, 1.0, "px")}},
      op_erode});

  registry.add(Operator{
      OperatorSpec{"posterize", "色调分离", "color", "把每通道量化到 N 级，用于观察色阶断层。", 1,
                   false,
                   {number_param("levels", "级数", "2 为纯黑白，256 为原图", 2.0, 64.0, 8.0)}},
      op_posterize});
}

}  // namespace figtools::detail
