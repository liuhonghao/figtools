// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Interactive segmentation without a trained model.
//
// Two classic algorithms, both fully deterministic and dependency-free:
//   * magic wand  - flood fill from a seed using a colour-distance tolerance
//   * GrabCut     - Gaussian mixture colour models plus graph-cut energy
//                   minimisation inside a user rectangle
//
// Both produce a binary mask. `mask_to_polygon` traces the mask boundary into a
// polygon so the result can be handed straight to the annotation pipeline.

#ifndef FIGTOOLS_SEGMENT_INTERACTIVE_HPP_
#define FIGTOOLS_SEGMENT_INTERACTIVE_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "figtools/annotate/annotation.hpp"
#include "figtools/core/image.hpp"

namespace figtools::segment {

// Binary mask, row-major, 1 = foreground. Same size as the source image.
struct Mask {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> bits;

  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0 || bits.empty(); }
  [[nodiscard]] bool at(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height &&
           bits[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x)] != 0;
  }
  [[nodiscard]] std::size_t count() const;
};

// Flood-fill a connected region from `seed` whose pixels are within
// `tolerance` Euclidean RGB distance of the seed colour. `tolerance` is in
// 0..255 scale (e.g. 32 selects fairly similar colours).
[[nodiscard]] Mask magic_wand(const ImageView& image, int seed_x, int seed_y,
                              double tolerance);

// GrabCut inside `rect`. Pixels outside the rectangle are fixed background;
// inside is initialised as probable foreground. `iterations` GMM/graph-cut
// passes (3-5 is typical). Returns the foreground mask.
[[nodiscard]] Mask grabcut(const ImageView& image, RectF rect, int iterations = 4);

// Trace the outer boundary of the largest connected foreground component into
// a polygon. `epsilon` controls simplification (in pixels; 0 keeps all boundary
// points). Points are in image pixel coordinates, matching AnnotationSet.
[[nodiscard]] std::vector<Point2f> mask_to_polygon(const Mask& mask, double epsilon = 1.5);

// Convenience: keep only the largest connected component of a mask.
[[nodiscard]] Mask largest_component(const Mask& mask);

}  // namespace figtools::segment

#endif  // FIGTOOLS_SEGMENT_INTERACTIVE_HPP_
