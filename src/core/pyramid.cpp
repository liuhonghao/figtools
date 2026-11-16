// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/pyramid.hpp"

#include <algorithm>
#include <utility>

namespace figtools {

Image downsample_half(const ImageView& source) {
  const int width = std::max(1, source.width() / 2);
  const int height = std::max(1, source.height() / 2);
  Image out(width, height);
  MutableImageView target = out.mutable_view();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int sx = std::min(source.width() - 1, x * 2);
      const int sy = std::min(source.height() - 1, y * 2);
      const Rgba a = source.at(sx, sy);
      const Rgba b = source.at(std::min(source.width() - 1, sx + 1), sy);
      const Rgba c = source.at(sx, std::min(source.height() - 1, sy + 1));
      const Rgba d = source.at(std::min(source.width() - 1, sx + 1),
                               std::min(source.height() - 1, sy + 1));
      const auto avg = [&](std::uint8_t Rgba::*channel) {
        return static_cast<std::uint8_t>(
            (static_cast<int>(a.*channel) + static_cast<int>(b.*channel) +
             static_cast<int>(c.*channel) + static_cast<int>(d.*channel) + 2) / 4);
      };
      target.set(x, y, Rgba{avg(&Rgba::r), avg(&Rgba::g), avg(&Rgba::b), avg(&Rgba::a)});
    }
  }
  return out;
}

ImagePyramid ImagePyramid::build(const ImageView& source, int min_side) {
  ImagePyramid pyramid;
  if (source.empty()) {
    return pyramid;
  }
  Image current(source.width(), source.height());
  MutableImageView out = current.mutable_view();
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      out.set(x, y, source.at(x, y));
    }
  }
  pyramid.levels_.push_back(std::move(current));

  while (std::max(pyramid.levels_.back().width(), pyramid.levels_.back().height()) > min_side) {
    Image smaller = downsample_half(pyramid.levels_.back().view());
    const int before = std::max(pyramid.levels_.back().width(), pyramid.levels_.back().height());
    const int after = std::max(smaller.width(), smaller.height());
    if (after >= before) {
      break;
    }
    pyramid.levels_.push_back(std::move(smaller));
  }
  return pyramid;
}

const Image& ImagePyramid::level(std::size_t index) const {
  const std::size_t safe = std::min(index, levels_.size() - 1);
  return levels_[safe];
}

std::size_t ImagePyramid::best_level(int requested_side) const {
  if (levels_.empty()) {
    return 0;
  }
  for (std::size_t i = 0; i < levels_.size(); ++i) {
    if (std::max(levels_[i].width(), levels_[i].height()) <= requested_side) {
      return i;
    }
  }
  return levels_.size() - 1;
}

std::size_t ImagePyramid::memory_bytes() const {
  std::size_t total = 0;
  for (const Image& image : levels_) {
    total += static_cast<std::size_t>(image.width()) * static_cast<std::size_t>(image.height()) * 4U;
  }
  return total;
}

}  // namespace figtools
