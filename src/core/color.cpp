// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/color.hpp"

#include <algorithm>
#include <cmath>

namespace figtools {
namespace {

constexpr double kLumaR = 0.299;
constexpr double kLumaG = 0.587;
constexpr double kLumaB = 0.114;

}  // namespace

std::uint8_t clamp_u8(double value) {
  const double rounded = std::round(value);
  if (rounded <= 0.0) {
    return 0;
  }
  if (rounded >= 255.0) {
    return 255;
  }
  return static_cast<std::uint8_t>(rounded);
}

Hsv rgb_to_hsv(Rgba color) {
  const double r = static_cast<double>(color.r) / 255.0;
  const double g = static_cast<double>(color.g) / 255.0;
  const double b = static_cast<double>(color.b) / 255.0;

  const double max_c = std::max({r, g, b});
  const double min_c = std::min({r, g, b});
  const double delta = max_c - min_c;

  Hsv out;
  out.v = max_c;
  out.s = (max_c <= 0.0) ? 0.0 : (delta / max_c);

  if (delta <= 1e-12) {
    out.h = 0.0;
    return out;
  }

  if (max_c == r) {
    out.h = 60.0 * std::fmod((g - b) / delta, 6.0);
  } else if (max_c == g) {
    out.h = 60.0 * (((b - r) / delta) + 2.0);
  } else {
    out.h = 60.0 * (((r - g) / delta) + 4.0);
  }
  if (out.h < 0.0) {
    out.h += 360.0;
  }
  return out;
}

Rgba hsv_to_rgb(Hsv hsv) {
  double h = std::fmod(hsv.h, 360.0);
  if (h < 0.0) {
    h += 360.0;
  }
  const double s = std::clamp(hsv.s, 0.0, 1.0);
  const double v = std::clamp(hsv.v, 0.0, 1.0);

  const double c = v * s;
  const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
  const double m = v - c;

  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
  if (h < 60.0) {
    r = c;
    g = x;
  } else if (h < 120.0) {
    r = x;
    g = c;
  } else if (h < 180.0) {
    g = c;
    b = x;
  } else if (h < 240.0) {
    g = x;
    b = c;
  } else if (h < 300.0) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }

  return Rgba{clamp_u8((r + m) * 255.0), clamp_u8((g + m) * 255.0), clamp_u8((b + m) * 255.0)};
}

std::uint8_t luma(Rgba color) {
  const double value = kLumaR * static_cast<double>(color.r) + kLumaG * static_cast<double>(color.g) +
                       kLumaB * static_cast<double>(color.b);
  return clamp_u8(value);
}

}  // namespace figtools
