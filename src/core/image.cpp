// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/image.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace figtools {

Image::Image(int width, int height) : width_(std::max(0, width)), height_(std::max(0, height)) {
  pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
                     static_cast<std::size_t>(kChannels),
                 0);
  // Default alpha is opaque; RGB starts black.
  for (std::size_t i = 3; i < pixels_.size(); i += kChannels) {
    pixels_[i] = 255;
  }
}

Image::Image(int width, int height, Rgba fill_color) : Image(width, height) { fill(fill_color); }

Image Image::filled(int width, int height, Rgba color) { return Image(width, height, color); }

Rgba Image::at(int x, int y) const {
  assert(x >= 0 && y >= 0 && x < width_ && y < height_);
  const std::uint8_t* p = row(y) + static_cast<std::ptrdiff_t>(x) * kChannels;
  return Rgba{p[0], p[1], p[2], p[3]};
}

void Image::set(int x, int y, Rgba color) {
  assert(x >= 0 && y >= 0 && x < width_ && y < height_);
  std::uint8_t* p = row(y) + static_cast<std::ptrdiff_t>(x) * kChannels;
  p[0] = color.r;
  p[1] = color.g;
  p[2] = color.b;
  p[3] = color.a;
}

void Image::fill(Rgba color) {
  for (std::size_t i = 0; i + kChannels <= pixels_.size(); i += kChannels) {
    pixels_[i + 0] = color.r;
    pixels_[i + 1] = color.g;
    pixels_[i + 2] = color.b;
    pixels_[i + 3] = color.a;
  }
}

}  // namespace figtools
