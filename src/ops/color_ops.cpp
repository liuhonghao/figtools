// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

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

// Applies `fn` to every pixel and writes the result into a new image.
template <typename Fn>
Result<Image> map_pixels(const ImageView& src, Fn fn) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "colour op: empty image");
  }
  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      out.set(x, y, fn(src.at(x, y)));
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> op_brightness(const ImageView& src, const Params& params) {
  const double delta = params.number("delta", 0.0) * 255.0;
  return map_pixels(src, [delta](Rgba c) {
    return Rgba{clamp_u8(static_cast<double>(c.r) + delta),
                clamp_u8(static_cast<double>(c.g) + delta),
                clamp_u8(static_cast<double>(c.b) + delta), c.a};
  });
}

Result<Image> op_contrast(const ImageView& src, const Params& params) {
  const double factor = params.number("factor", 1.0);
  return map_pixels(src, [factor](Rgba c) {
    const auto adjust = [factor](std::uint8_t v) {
      const double normalised = static_cast<double>(v) / 255.0;
      const double result = (normalised - 0.5) * factor + 0.5;
      return clamp_u8(result * 255.0);
    };
    return Rgba{adjust(c.r), adjust(c.g), adjust(c.b), c.a};
  });
}

Result<Image> op_saturation(const ImageView& src, const Params& params) {
  const double factor = params.number("factor", 1.0);
  return map_pixels(src, [factor](Rgba c) {
    Hsv hsv = rgb_to_hsv(c);
    hsv.s = std::clamp(hsv.s * factor, 0.0, 1.0);
    Rgba result = hsv_to_rgb(hsv);
    result.a = c.a;
    return result;
  });
}

Result<Image> op_gamma(const ImageView& src, const Params& params) {
  const double gamma = params.number("gamma", 1.0);
  const double exponent = 1.0 / gamma;
  return map_pixels(src, [exponent](Rgba c) {
    const auto adjust = [exponent](std::uint8_t v) {
      const double normalised = static_cast<double>(v) / 255.0;
      return clamp_u8(std::pow(normalised, exponent) * 255.0);
    };
    return Rgba{adjust(c.r), adjust(c.g), adjust(c.b), c.a};
  });
}

Result<Image> op_grayscale(const ImageView& src, const Params& params) {
  (void)params;
  return map_pixels(src, [](Rgba c) {
    const std::uint8_t y = luma(c);
    return Rgba{y, y, y, c.a};
  });
}

Result<Image> op_invert(const ImageView& src, const Params& params) {
  (void)params;
  return map_pixels(src, [](Rgba c) {
    return Rgba{static_cast<std::uint8_t>(255 - c.r), static_cast<std::uint8_t>(255 - c.g),
                static_cast<std::uint8_t>(255 - c.b), c.a};
  });
}

// Grey-world white balance: scale each channel so that its mean matches the
// mean of the green channel, blended by `strength`.
Result<Image> op_white_balance(const ImageView& src, const Params& params) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "white_balance: empty image");
  }
  const double strength = params.number("strength", 1.0);

  double sum_r = 0.0;
  double sum_g = 0.0;
  double sum_b = 0.0;
  for (int y = 0; y < src.height(); ++y) {
    for (int x = 0; x < src.width(); ++x) {
      const Rgba c = src.at(x, y);
      sum_r += static_cast<double>(c.r);
      sum_g += static_cast<double>(c.g);
      sum_b += static_cast<double>(c.b);
    }
  }
  const double pixels = static_cast<double>(src.size().pixel_count());
  const double mean_r = sum_r / pixels;
  const double mean_g = sum_g / pixels;
  const double mean_b = sum_b / pixels;

  const auto gain = [strength](double mean, double reference) {
    if (mean <= 1e-6) {
      return 1.0;
    }
    return 1.0 + (reference / mean - 1.0) * strength;
  };
  const double gain_r = gain(mean_r, mean_g);
  const double gain_b = gain(mean_b, mean_g);

  return map_pixels(src, [gain_r, gain_b](Rgba c) {
    return Rgba{clamp_u8(static_cast<double>(c.r) * gain_r), c.g,
                clamp_u8(static_cast<double>(c.b) * gain_b), c.a};
  });
}

}  // namespace

void register_color_ops(Registry& registry) {
  registry.add(Operator{
      OperatorSpec{"brightness", "亮度", "color", "整体加减亮度，等价于曝光补偿。", 1, false,
                   {number_param("delta", "增量", "以 50% 灰为原点，-1 到 1 对应 -255 到 +255",
                                 -1.0, 1.0, 0.0)}},
      op_brightness});

  registry.add(Operator{
      OperatorSpec{"contrast", "对比度", "color", "以中灰为支点拉伸或压缩动态范围。", 1, false,
                   {number_param("factor", "强度", "1.0 不变，0 变灰，大于 1 增强", 0.0, 3.0, 1.0)}},
      op_contrast});

  registry.add(Operator{
      OperatorSpec{"saturation", "饱和度", "color", "在 HSV 空间缩放色度，0 为灰度。", 1, false,
                   {number_param("factor", "强度", "1.0 不变，0 为灰度，大于 1 增艳", 0.0, 3.0, 1.0)}},
      op_saturation});

  registry.add(Operator{
      OperatorSpec{"gamma", "伽马", "color", "输出 = 输入^(1/gamma)，大于 1 提亮暗部。", 1, false,
                   {number_param("gamma", "伽马值", "显示端常见 2.2，处理端常用 1.0", 0.1, 5.0, 1.0)}},
      op_gamma});

  registry.add(Operator{
      OperatorSpec{"grayscale", "灰度化", "color", "按 Rec.601 权重转为亮度图。", 1, false, {}},
      op_grayscale});

  registry.add(Operator{
      OperatorSpec{"invert", "反相", "color", "255 减去每个通道，用于观察暗部细节。", 1, false, {}},
      op_invert});

  registry.add(Operator{
      OperatorSpec{"white_balance", "白平衡", "color", "灰度世界假设下的自动白平衡。", 1, false,
                   {number_param("strength", "强度", "0 不变，1 完全校正", 0.0, 1.0, 1.0)}},
      op_white_balance});
}

}  // namespace figtools::detail
