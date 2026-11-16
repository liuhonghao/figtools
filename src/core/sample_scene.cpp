// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// The built-in sample is a calibration target printed on warm paper, not a
// photograph. It is honest about being synthetic, it exercises the operators
// (paper grain for denoise, bar patterns for sharpen, step wedge for contrast)
// and it renders identically on every machine.

#include "figtools/core/sample_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "figtools/core/color.hpp"

namespace figtools {
namespace {

struct Rgb {
  double r;
  double g;
  double b;
};

constexpr Rgb kPaper{243.0, 236.0, 223.0};
constexpr Rgb kInk{42.0, 36.0, 28.0};

std::uint32_t hash_2d(int x, int y) {
  std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393U +
                    static_cast<std::uint32_t>(y) * 668265263U;
  h = (h ^ (h >> 13U)) * 1274126177U;
  return h ^ (h >> 16U);
}

double unit_noise(int x, int y) {
  return static_cast<double>(hash_2d(x, y) & 0xFFFFU) / 65535.0;
}

Rgb mix(const Rgb& a, const Rgb& b, double t) {
  return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Rgb hex(int r, int g, int b) {
  return Rgb{static_cast<double>(r), static_cast<double>(g), static_cast<double>(b)};
}

void fill(MutableImageView& view, int x0, int y0, int x1, int y1, const Rgb& colour) {
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (!view.as_const().contains(x, y)) {
        continue;
      }
      view.set(x, y, Rgba{clamp_u8(colour.r), clamp_u8(colour.g), clamp_u8(colour.b), 255});
    }
  }
}

void stroke_rect(MutableImageView& view, int x0, int y0, int x1, int y1, const Rgb& colour) {
  for (int x = x0; x < x1; ++x) {
    fill(view, x, y0, x + 1, y0 + 1, colour);
    fill(view, x, y1 - 1, x + 1, y1, colour);
  }
  for (int y = y0; y < y1; ++y) {
    fill(view, x0, y, x0 + 1, y, colour);
    fill(view, x1 - 1, y, x1, y, colour);
  }
}

void crosshair(MutableImageView& view, int cx, int cy, int arm, const Rgb& colour) {
  fill(view, cx - arm, cy, cx + arm + 1, cy + 1, colour);
  fill(view, cx, cy - arm, cx + 1, cy + arm + 1, colour);
}

// Siemens star: alternating sectors. Near the centre the sectors are narrower
// than a pixel, so it is blended to mid grey instead of shimmering.
void siemens_star(MutableImageView& view, int cx, int cy, int radius, int sectors) {
  const double two_pi = 6.283185307179586;
  for (int y = cy - radius; y <= cy + radius; ++y) {
    for (int x = cx - radius; x <= cx + radius; ++x) {
      if (!view.as_const().contains(x, y)) {
        continue;
      }
      const double dx = static_cast<double>(x - cx);
      const double dy = static_cast<double>(y - cy);
      const double distance = std::sqrt(dx * dx + dy * dy);
      if (distance > static_cast<double>(radius)) {
        continue;
      }
      double angle = std::atan2(dy, dx) + two_pi;
      const double sector = angle / (two_pi / static_cast<double>(sectors));
      const bool dark = static_cast<int>(sector) % 2 == 1;
      const double blend = std::clamp((distance - 12.0) / 26.0, 0.0, 1.0);
      const Rgb target = dark ? kInk : kPaper;
      const Rgb grey = mix(kPaper, kInk, 0.42);
      const Rgb colour = mix(grey, target, blend);
      view.set(x, y, Rgba{clamp_u8(colour.r), clamp_u8(colour.g), clamp_u8(colour.b), 255});
    }
  }
  // Ring boundary.
  for (int i = 0; i < 720; ++i) {
    const double a = static_cast<double>(i) * 6.283185307179586 / 720.0;
    const int x = cx + static_cast<int>(std::lround(std::cos(a) * radius));
    const int y = cy + static_cast<int>(std::lround(std::sin(a) * radius));
    fill(view, x, y, x + 2, y + 1, kInk);
  }
}

}  // namespace

Image make_sample_scene(int width, int height, unsigned seed) {
  const int w = std::max(64, width);
  const int h = std::max(64, height);

  Image image(w, h);
  MutableImageView view = image.mutable_view();
  const std::uint32_t base_seed = seed == 0U ? 0x9E3779B9U : seed;

  // ---- paper --------------------------------------------------------------
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double fx = static_cast<double>(x) / static_cast<double>(w);
      const double fy = static_cast<double>(y) / static_cast<double>(h);
      // Fibre grain plus a soft light falloff toward the edges: enough texture
      // that a denoise pass has something to remove.
      const double grain = unit_noise(x, y) - 0.5;
      const double fibre = unit_noise(x / 3, y * 7 + 11) - 0.5;
      const double vignette =
          -10.0 * std::pow(std::max(std::abs(fx - 0.5), std::abs(fy - 0.5)) * 2.0, 2.4);
      const double tint = (fx - 0.5) * 3.0;
      view.set(x, y,
               Rgba{clamp_u8(kPaper.r + grain * 7.0 + fibre * 3.5 + vignette + tint),
                    clamp_u8(kPaper.g + grain * 6.0 + fibre * 3.0 + vignette),
                    clamp_u8(kPaper.b + grain * 5.0 + fibre * 2.5 + vignette - tint), 255});
    }
  }

  const int margin = std::max(24, w / 24);
  const Rgb rule = hex(196, 184, 164);

  // Chart border and registration marks.
  stroke_rect(view, margin, margin, w - margin, h - margin, rule);
  const int arm = std::max(6, w / 120);
  const int inset = margin + std::max(10, w / 100);
  crosshair(view, inset, inset, arm, kInk);
  crosshair(view, w - inset, inset, arm, kInk);
  crosshair(view, inset, h - inset, arm, kInk);
  crosshair(view, w - inset, h - inset, arm, kInk);

  // ---- Siemens star -------------------------------------------------------
  const int star_cx = static_cast<int>(w * 0.315);
  const int star_cy = static_cast<int>(h * 0.500);
  const int star_radius = static_cast<int>(std::min(w, h) * 0.300);
  siemens_star(view, star_cx, star_cy, star_radius, 36);

  // ---- step wedge ---------------------------------------------------------
  const int panel_left = static_cast<int>(w * 0.545);
  const int panel_right = w - margin - std::max(14, w / 80);
  const int steps = 12;
  const int wedge_y = static_cast<int>(h * 0.105);
  const int wedge_h = static_cast<int>(h * 0.085);
  const int step_w = (panel_right - panel_left) / steps;
  for (int i = 0; i < steps; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(steps - 1);
    const Rgb colour = mix(hex(24, 22, 19), hex(246, 241, 232), t);
    const int x0 = panel_left + i * step_w;
    fill(view, x0, wedge_y, x0 + step_w - 4, wedge_y + wedge_h, colour);
  }
  stroke_rect(view, panel_left - 2, wedge_y - 2, panel_right + 2, wedge_y + wedge_h + 2, rule);

  // ---- colour patches -----------------------------------------------------
  const Rgb patches[12] = {
      hex(180, 86, 42),  hex(201, 138, 62), hex(138, 139, 74), hex(79, 122, 114),
      hex(74, 100, 128), hex(122, 78, 94),  hex(232, 213, 195), hex(201, 156, 126),
      hex(138, 106, 82), hex(217, 210, 196), hex(142, 135, 120), hex(58, 52, 44),
  };
  const int patch_top = static_cast<int>(h * 0.245);
  const int patch_size = static_cast<int>((panel_right - panel_left - 5 * 10) / 6.0);
  const int patch_h = std::min(patch_size, static_cast<int>(h * 0.115));
  for (int i = 0; i < 12; ++i) {
    const int col = i % 6;
    const int row = i / 6;
    const int x0 = panel_left + col * (patch_size + 10);
    const int y0 = patch_top + row * (patch_h + 14);
    fill(view, x0, y0, x0 + patch_size, y0 + patch_h, patches[i]);
    stroke_rect(view, x0, y0, x0 + patch_size, y0 + patch_h, rule);
  }

  // ---- resolution bars ----------------------------------------------------
  const int bars_top = static_cast<int>(h * 0.560);
  const int bars_h = static_cast<int>(h * 0.130);
  const int periods[6] = {2, 3, 4, 6, 9, 14};
  const int group_w = (panel_right - panel_left) / 6;
  for (int g = 0; g < 6; ++g) {
    const int x0 = panel_left + g * group_w;
    const int period = periods[g];
    for (int x = 0; x < group_w - 10; ++x) {
      if ((x / period) % 2 == 0) {
        for (int y = bars_top; y < bars_top + bars_h; ++y) {
          const int px = x0 + x;
          if (view.as_const().contains(px, y)) {
            view.set(px, y, Rgba{clamp_u8(kInk.r), clamp_u8(kInk.g), clamp_u8(kInk.b), 255});
          }
        }
      }
    }
  }
  stroke_rect(view, panel_left - 2, bars_top - 2, panel_right + 2, bars_top + bars_h + 2, rule);

  // ---- diagonal line block ------------------------------------------------
  const int diag_top = static_cast<int>(h * 0.750);
  const int diag_h = static_cast<int>(h * 0.120);
  for (int y = diag_top; y < diag_top + diag_h; ++y) {
    for (int x = panel_left; x < panel_left + (panel_right - panel_left) / 2; ++x) {
      if ((x + y) % 5 < 2) {
        view.set(x, y, Rgba{clamp_u8(kInk.r), clamp_u8(kInk.g), clamp_u8(kInk.b), 255});
      }
    }
  }
  stroke_rect(view, panel_left - 2, diag_top - 2, panel_left + (panel_right - panel_left) / 2 + 2,
              diag_top + diag_h + 2, rule);

  // Small warm reference chip in the lower left of the panel area.
  const int chip_size = static_cast<int>(h * 0.085);
  const int chip_x = panel_left + (panel_right - panel_left) / 2 + 24;
  fill(view, chip_x, diag_top, chip_x + chip_size, diag_top + chip_size, hex(180, 86, 42));

  (void)base_seed;
  return image;
}

}  // namespace figtools
