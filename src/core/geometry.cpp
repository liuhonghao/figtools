// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/geometry.hpp"

#include <algorithm>
#include <cmath>

#include "figtools/core/color.hpp"

namespace figtools {
namespace {

struct Coord {
  int x = 0;
  int y = 0;
};

Coord clamp_coord(int x, int y, int width, int height) {
  return Coord{std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1)};
}

Rgba blend(Rgba a, Rgba b, double weight_b) {
  const double wa = 1.0 - weight_b;
  return Rgba{
      clamp_u8(static_cast<double>(a.r) * wa + static_cast<double>(b.r) * weight_b),
      clamp_u8(static_cast<double>(a.g) * wa + static_cast<double>(b.g) * weight_b),
      clamp_u8(static_cast<double>(a.b) * wa + static_cast<double>(b.b) * weight_b),
      clamp_u8(static_cast<double>(a.a) * wa + static_cast<double>(b.a) * weight_b),
  };
}

}  // namespace

Result<Image> resize(const ImageView& src, int dst_width, int dst_height, Interp interp) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "resize: empty source image");
  }
  if (dst_width <= 0 || dst_height <= 0) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument,
                               "resize: destination size must be positive, got " +
                                   std::to_string(dst_width) + "x" + std::to_string(dst_height));
  }

  Image dst(dst_width, dst_height);
  MutableImageView out = dst.mutable_view();

  const double scale_x = static_cast<double>(src.width()) / static_cast<double>(dst_width);
  const double scale_y = static_cast<double>(src.height()) / static_cast<double>(dst_height);

  for (int y = 0; y < dst_height; ++y) {
    for (int x = 0; x < dst_width; ++x) {
      if (interp == Interp::kNearest) {
        const int sx = std::min(src.width() - 1, static_cast<int>((static_cast<double>(x) + 0.5) *
                                                                  scale_x));
        const int sy = std::min(src.height() - 1, static_cast<int>((static_cast<double>(y) + 0.5) *
                                                                   scale_y));
        out.set(x, y, src.at(sx, sy));
        continue;
      }

      // Pixel-centre aligned bilinear sampling.
      const double fx = (static_cast<double>(x) + 0.5) * scale_x - 0.5;
      const double fy = (static_cast<double>(y) + 0.5) * scale_y - 0.5;
      const int x0 = static_cast<int>(std::floor(fx));
      const int y0 = static_cast<int>(std::floor(fy));
      const double tx = fx - static_cast<double>(x0);
      const double ty = fy - static_cast<double>(y0);

      const Coord p00 = clamp_coord(x0, y0, src.width(), src.height());
      const Coord p10 = clamp_coord(x0 + 1, y0, src.width(), src.height());
      const Coord p01 = clamp_coord(x0, y0 + 1, src.width(), src.height());
      const Coord p11 = clamp_coord(x0 + 1, y0 + 1, src.width(), src.height());

      const Rgba top = blend(src.at(p00.x, p00.y), src.at(p10.x, p10.y), tx);
      const Rgba bottom = blend(src.at(p01.x, p01.y), src.at(p11.x, p11.y), tx);
      out.set(x, y, blend(top, bottom, ty));
    }
  }

  return Result<Image>::ok(std::move(dst));
}

Result<Image> crop(const ImageView& src, Rect rect) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "crop: empty source image");
  }

  const int x0 = std::max(0, rect.x);
  const int y0 = std::max(0, rect.y);
  const int x1 = std::min(src.width(), rect.x + rect.width);
  const int y1 = std::min(src.height(), rect.y + rect.height);
  if (x1 <= x0 || y1 <= y0) {
    return Result<Image>::fail(ErrorCode::kOutOfRange,
                               "crop: rectangle does not intersect the image");
  }

  Image dst(x1 - x0, y1 - y0);
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < dst.height(); ++y) {
    const std::uint8_t* src_row = src.row(y0 + y) + static_cast<std::ptrdiff_t>(x0) * kChannels;
    std::uint8_t* dst_row = out.row(y);
    std::copy(src_row, src_row + static_cast<std::ptrdiff_t>(dst.width()) * kChannels, dst_row);
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> rotate90(const ImageView& src, int quarter_turns) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "rotate90: empty source image");
  }
  int turns = quarter_turns % 4;
  if (turns < 0) {
    turns += 4;
  }
  if (turns == 0) {
    Image copy(src.width(), src.height());
    MutableImageView out = copy.mutable_view();
    for (int y = 0; y < src.height(); ++y) {
      std::copy(src.row(y), src.row(y) + static_cast<std::ptrdiff_t>(src.width()) * kChannels,
                out.row(y));
    }
    return Result<Image>::ok(std::move(copy));
  }

  const bool swap_axes = (turns % 2) == 1;
  const int dst_width = swap_axes ? src.height() : src.width();
  const int dst_height = swap_axes ? src.width() : src.height();
  Image dst(dst_width, dst_height);
  MutableImageView out = dst.mutable_view();

  for (int y = 0; y < dst_height; ++y) {
    for (int x = 0; x < dst_width; ++x) {
      int sx = 0;
      int sy = 0;
      switch (turns) {
        case 1:  // clockwise 90
          sx = y;
          sy = src.height() - 1 - x;
          break;
        case 2:
          sx = src.width() - 1 - x;
          sy = src.height() - 1 - y;
          break;
        default:  // 270
          sx = src.width() - 1 - y;
          sy = x;
          break;
      }
      out.set(x, y, src.at(sx, sy));
    }
  }
  return Result<Image>::ok(std::move(dst));
}

Result<Image> flip(const ImageView& src, FlipAxis axis) {
  if (src.empty()) {
    return Result<Image>::fail(ErrorCode::kInvalidArgument, "flip: empty source image");
  }
  Image dst(src.width(), src.height());
  MutableImageView out = dst.mutable_view();
  for (int y = 0; y < src.height(); ++y) {
    const int sy = (axis == FlipAxis::kVertical) ? src.height() - 1 - y : y;
    for (int x = 0; x < src.width(); ++x) {
      const int sx = (axis == FlipAxis::kHorizontal) ? src.width() - 1 - x : x;
      out.set(x, y, src.at(sx, sy));
    }
  }
  return Result<Image>::ok(std::move(dst));
}

}  // namespace figtools
