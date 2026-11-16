// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Image quality metrics used by `figtools compare` and by regression tests.
// Definitions are documented so the CLI, the GUI and the Python worker report
// the same numbers for the same pair of images.

#ifndef FIGTOOLS_CORE_METRICS_HPP_
#define FIGTOOLS_CORE_METRICS_HPP_

#include "figtools/core/image.hpp"
#include "figtools/core/result.hpp"

namespace figtools {

struct ImageMetrics {
  double psnr = 0.0;          // dB, +infinity when the images are identical
  double ssim = 1.0;          // mean SSIM over the luminance channel, [0, 1]
  double mae = 0.0;           // mean absolute error over RGB, [0, 255]
  double changed_ratio = 0.0; // fraction of pixels with any channel differing
  bool identical = true;
};

// Computes metrics over the RGB channels. Both images must share dimensions.
[[nodiscard]] Result<ImageMetrics> compare_images(const ImageView& lhs, const ImageView& rhs);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_METRICS_HPP_
