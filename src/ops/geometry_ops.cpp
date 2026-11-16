// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cmath>
#include <string>

#include "figtools/core/geometry.hpp"
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

ParamSpec int_param(std::string key, std::string label, std::string doc, double min_value,
                    double max_value, double default_value, std::string unit = "") {
  ParamSpec spec = number_param(std::move(key), std::move(label), std::move(doc), min_value,
                                max_value, default_value, std::move(unit));
  spec.type = ParamType::kInteger;
  return spec;
}

ParamSpec enum_param(std::string key, std::string label, std::string doc,
                     std::vector<std::string> values, std::string default_value) {
  ParamSpec spec;
  spec.key = std::move(key);
  spec.label = std::move(label);
  spec.doc = std::move(doc);
  spec.type = ParamType::kEnum;
  spec.enum_values = std::move(values);
  // Store the default as the index of the default string.
  const auto it = std::find(spec.enum_values.begin(), spec.enum_values.end(), default_value);
  spec.default_value = static_cast<double>(std::distance(spec.enum_values.begin(), it));
  return spec;
}

Result<Image> op_resize(const ImageView& src, const Params& params) {
  const double scale = params.number("scale", 1.0);
  int width = params.integer("width", 0);
  int height = params.integer("height", 0);
  if (width <= 0) {
    width = std::max(1, static_cast<int>(std::lround(static_cast<double>(src.width()) * scale)));
  }
  if (height <= 0) {
    height = std::max(1, static_cast<int>(std::lround(static_cast<double>(src.height()) * scale)));
  }
  const Interp interp = params.text("interp", "bilinear") == "nearest" ? Interp::kNearest
                                                                       : Interp::kBilinear;
  return resize(src, width, height, interp);
}

Result<Image> op_crop(const ImageView& src, const Params& params) {
  const double x_pct = params.number("x", 0.0);
  const double y_pct = params.number("y", 0.0);
  const double w_pct = params.number("width", 100.0);
  const double h_pct = params.number("height", 100.0);

  const int x = static_cast<int>(std::lround(x_pct / 100.0 * static_cast<double>(src.width())));
  const int y = static_cast<int>(std::lround(y_pct / 100.0 * static_cast<double>(src.height())));
  const int w = static_cast<int>(std::lround(w_pct / 100.0 * static_cast<double>(src.width())));
  const int h = static_cast<int>(std::lround(h_pct / 100.0 * static_cast<double>(src.height())));
  return crop(src, Rect{x, y, w, h});
}

Result<Image> op_rotate90(const ImageView& src, const Params& params) {
  return rotate90(src, params.integer("turns", 1));
}

Result<Image> op_flip(const ImageView& src, const Params& params) {
  const FlipAxis axis = params.text("axis", "horizontal") == "vertical" ? FlipAxis::kVertical
                                                                        : FlipAxis::kHorizontal;
  return flip(src, axis);
}

}  // namespace

void register_geometry_ops(Registry& registry) {
  registry.add(Operator{
      OperatorSpec{"resize", "缩放", "geometry",
                   "按比例或目标尺寸重采样，双线性适合连续色调，最近邻适合掩码与索引图。", 1, true,
                   {number_param("scale", "缩放比例", "1.0 表示保持原尺寸", 0.05, 8.0, 1.0, "x"),
                    int_param("width", "目标宽度", "0 表示由比例推导", 0.0, 20000.0, 0.0, "px"),
                    int_param("height", "目标高度", "0 表示由比例推导", 0.0, 20000.0, 0.0, "px"),
                    enum_param("interp", "插值", "下采样建议双线性以避免混叠", {"bilinear", "nearest"},
                               "bilinear")}},
      op_resize});

  registry.add(Operator{
      OperatorSpec{"crop", "裁剪", "geometry", "按百分比裁剪区域，越界部分自动收缩。", 1, true,
                   {number_param("x", "起点 X", "相对宽度的百分比", 0.0, 100.0, 0.0, "%"),
                    number_param("y", "起点 Y", "相对高度的百分比", 0.0, 100.0, 0.0, "%"),
                    number_param("width", "宽度", "相对宽度的百分比", 1.0, 100.0, 100.0, "%"),
                    number_param("height", "高度", "相对高度的百分比", 1.0, 100.0, 100.0, "%")}},
      op_crop});

  registry.add(Operator{
      OperatorSpec{"rotate90", "旋转 90°", "geometry", "顺时针旋转 90° 的整数倍，无损且不插值。", 1,
                   true,
                   {int_param("turns", "次数", "1 表示顺时针 90°，3 表示逆时针 90°", 0.0, 3.0, 1.0,
                              "×90°")}},
      op_rotate90});

  registry.add(Operator{
      OperatorSpec{"flip", "翻转", "geometry", "水平或垂直镜像。", 1, true,
                   {enum_param("axis", "方向", "水平镜像常用于数据增强", {"horizontal", "vertical"},
                               "horizontal")}},
      op_flip});
}

}  // namespace figtools::detail
