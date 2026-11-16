// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Synthetic inspection scene used by `figtools demo` and by the GUI's
// "sample image" action. Deterministic for a given seed, so screenshots and
// benchmark numbers stay comparable between runs.

#ifndef FIGTOOLS_CORE_SAMPLE_SCENE_HPP_
#define FIGTOOLS_CORE_SAMPLE_SCENE_HPP_

#include "figtools/core/image.hpp"

namespace figtools {

// Brushed metal surface with grooves, an inspection ring and a scratch.
[[nodiscard]] Image make_sample_scene(int width, int height, unsigned seed);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_SAMPLE_SCENE_HPP_
