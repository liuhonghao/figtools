// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Colour primitives shared by the operator layer. Conversions follow the
// formulas in docs/ARCHITECTURE.md ("Colour") so that the CLI, the GUI and the
// Python worker can agree on results.

#ifndef FIGTOOLS_CORE_COLOR_HPP_
#define FIGTOOLS_CORE_COLOR_HPP_

#include <cstdint>

#include "figtools/core/image.hpp"

namespace figtools {

struct Hsv {
  double h = 0.0;  // [0, 360)
  double s = 0.0;  // [0, 1]
  double v = 0.0;  // [0, 1]
};

[[nodiscard]] Hsv rgb_to_hsv(Rgba color);
[[nodiscard]] Rgba hsv_to_rgb(Hsv hsv);

// Rec. 601 luma, matching the weights used by the design document.
[[nodiscard]] std::uint8_t luma(Rgba color);

[[nodiscard]] std::uint8_t clamp_u8(double value);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_COLOR_HPP_
