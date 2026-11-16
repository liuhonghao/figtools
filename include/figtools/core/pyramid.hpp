// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Multi-resolution image pyramid for large inputs.
//
// The canvas never needs more pixels than the viewport can show. Holding a
// 400 MP original just to draw a 1200 px preview is the single most common
// reason image tools exhaust memory. This class keeps progressively halved
// copies down to a bounded size; callers pick the level closest to the size
// they need. The full-resolution image is only decoded when exporting.

#ifndef FIGTOOLS_CORE_PYRAMID_HPP_
#define FIGTOOLS_CORE_PYRAMID_HPP_

#include <cstddef>
#include <vector>

#include "figtools/core/image.hpp"

namespace figtools {

class ImagePyramid {
 public:
  // Builds levels from `source` down to at most `min_side` pixels on the long
  // edge. Level 0 is a copy of the source; the last level is the smallest.
  static ImagePyramid build(const ImageView& source, int min_side = 1024);

  [[nodiscard]] std::size_t levels() const { return levels_.size(); }
  [[nodiscard]] const Image& level(std::size_t index) const;

  // Chooses the smallest level whose long edge is at least `requested_side`,
  // falling back to the smallest available level. Avoids upsampling.
  [[nodiscard]] std::size_t best_level(int requested_side) const;

  [[nodiscard]] std::size_t memory_bytes() const;

  // True when the source is large enough that using the pyramid matters.
  [[nodiscard]] bool is_large() const { return levels_.size() > 1; }

 private:
  std::vector<Image> levels_;
};

// Halves an image with 2x2 box filtering (cheap and alias-free enough for
// display pyramids; exact pixel values are not the point here).
[[nodiscard]] Image downsample_half(const ImageView& source);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_PYRAMID_HPP_
