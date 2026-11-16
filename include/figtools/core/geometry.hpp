// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Geometric primitives. Every transform returns a new image; nothing is
// modified in place (see docs/ARCHITECTURE.md, "Non-destructive by default").

#ifndef FIGTOOLS_CORE_GEOMETRY_HPP_
#define FIGTOOLS_CORE_GEOMETRY_HPP_

#include "figtools/core/image.hpp"
#include "figtools/core/result.hpp"

namespace figtools {

enum class Interp {
  kNearest = 0,
  kBilinear,
};

enum class FlipAxis {
  kHorizontal = 0,
  kVertical,
};

[[nodiscard]] Result<Image> resize(const ImageView& src, int dst_width, int dst_height,
                                   Interp interp);

// Crops to `rect`, clamped to the image bounds. Returns an error when the
// rect and the image do not intersect.
[[nodiscard]] Result<Image> crop(const ImageView& src, Rect rect);

// Rotates clockwise by 90 * quarter_turns (values outside 0..3 are wrapped).
[[nodiscard]] Result<Image> rotate90(const ImageView& src, int quarter_turns);

[[nodiscard]] Result<Image> flip(const ImageView& src, FlipAxis axis);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_GEOMETRY_HPP_
