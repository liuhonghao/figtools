// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Tile a large image into overlapping patches (e.g. drone orthomosaics).

#ifndef FIGTOOLS_IO_TILING_HPP_
#define FIGTOOLS_IO_TILING_HPP_

#include <cstddef>
#include <string>
#include <vector>

#include "figtools/core/image.hpp"

namespace figtools::io {

struct TileSpec {
  int tile_size = 512;
  int overlap = 0;  // pixels shared between neighbouring tiles
};

struct TileInfo {
  int row = 0;
  int column = 0;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  std::string filename;
};

struct TilingResult {
  std::vector<TileInfo> tiles;
  int rows = 0;
  int columns = 0;
};

// Cut `image` into tiles. Returns one TileInfo per written patch, in row-major
// order. `name` is used for output files: <name>_r<row>_c<col>.<ext>.
[[nodiscard]] TilingResult tile_image(const Image& image, const std::string& output_directory,
                                      const std::string& name, const TileSpec& spec,
                                      const std::string& extension);

}  // namespace figtools::io

#endif  // FIGTOOLS_IO_TILING_HPP_
