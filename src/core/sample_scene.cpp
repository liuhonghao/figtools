// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/sample_scene.hpp"

#include <cmath>
#include <cstdint>

#include "figtools/core/color.hpp"

namespace figtools {

Image make_sample_scene(int width, int height, unsigned seed) {
  const int w = width < 64 ? 64 : width;
  const int h = height < 64 ? 64 : height;

  Image image(w, h);
  MutableImageView view = image.mutable_view();

  std::uint32_t state = seed == 0U ? 0x9E3779B9U : seed;
  const auto next_random = [&state]() {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
  };

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double fx = static_cast<double>(x) / static_cast<double>(w);
      const double fy = static_cast<double>(y) / static_cast<double>(h);

      double value = 92.0 + 36.0 * fx + 22.0 * fy;
      value += 10.0 * std::sin(fx * 42.0) * std::exp(-fy * 1.5);
      value += 4.0 * std::sin(fy * 420.0 + fx * 12.0);
      if (x % 96 < 3) {
        value -= 26.0;
      }
      const int noise = static_cast<int>(next_random() % 13U) - 6;
      value += static_cast<double>(noise);

      const std::uint8_t v = clamp_u8(value);
      view.set(x, y, Rgba{v, v, clamp_u8(value * 0.96), 255});
    }
  }

  const int cx = w / 3;
  const int cy = h / 2;
  const int radius = (w < h ? w : h) / 5;
  for (int y = cy - radius; y <= cy + radius; ++y) {
    for (int x = cx - radius; x <= cx + radius; ++x) {
      if (!image.view().contains(x, y)) {
        continue;
      }
      const double dx = static_cast<double>(x - cx);
      const double dy = static_cast<double>(y - cy);
      const double distance = std::sqrt(dx * dx + dy * dy);
      if (std::abs(distance - static_cast<double>(radius)) < 1.6) {
        view.set(x, y, Rgba{232, 236, 240, 255});
      }
    }
  }

  // Scratch: the defect the pipeline should make obvious.
  const int x0 = w * 2 / 3;
  const int y0 = h / 4;
  for (int t = 0; t < h / 2; ++t) {
    const int x = x0 + static_cast<int>(28.0 * std::sin(static_cast<double>(t) * 0.035));
    const int y = y0 + t;
    for (int thickness = -1; thickness <= 1; ++thickness) {
      if (image.view().contains(x + thickness, y)) {
        view.set(x + thickness, y, Rgba{24, 22, 20, 255});
      }
    }
  }

  return image;
}

}  // namespace figtools
