// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/io/tiling.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "figtools/io/image_io.hpp"
#include "figtools/io/path.hpp"

namespace figtools::io {
namespace {

std::vector<int> starts(int extent, int tile, int overlap) {
  std::vector<int> result;
  const int step = std::max(1, tile - std::max(0, overlap));
  for (int position = 0; position < extent; position += step) {
    result.push_back(position);
  }
  if (result.empty()) return result;
  // Make the last tile end exactly at the image edge.
  const int last = result.back();
  if (last + tile < extent) {
    result.push_back(extent - tile);
  }
  return result;
}

}  // namespace

TilingResult tile_image(const Image& image, const std::string& output_directory,
                        const std::string& name, const TileSpec& spec,
                        const std::string& extension) {
  TilingResult result;
  if (image.empty() || spec.tile_size < 16) return result;

  const int tile = spec.tile_size;
  const int overlap = std::clamp(spec.overlap, 0, tile - 1);
  const std::vector<int> xs = starts(image.width(), tile, overlap);
  const std::vector<int> ys = starts(image.height(), tile, overlap);
  if (xs.empty() || ys.empty()) return result;

  std::error_code ec;
  const std::filesystem::path root = native_path(output_directory);
  std::filesystem::create_directories(root, ec);
  if (ec) return result;

  result.rows = static_cast<int>(ys.size());
  result.columns = static_cast<int>(xs.size());
  const std::string ext = extension.empty() ? ".png" : extension;

  for (int row = 0; row < result.rows; ++row) {
    for (int column = 0; column < result.columns; ++column) {
      const int x = xs[static_cast<std::size_t>(column)];
      const int y = ys[static_cast<std::size_t>(row)];
      const int width = std::min(tile, image.width() - x);
      const int height = std::min(tile, image.height() - y);

      Image patch(width, height);
      for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
          patch.set(px, py, image.at(x + px, y + py));
        }
      }
      TileInfo info;
      info.row = row;
      info.column = column;
      info.x = x;
      info.y = y;
      info.width = width;
      info.height = height;
      info.filename = name + "_r" + std::to_string(row) + "_c" +
                      std::to_string(column) + ext;
      const std::filesystem::path target = root / info.filename;
      if (save_image(patch, target.string())) {
        result.tiles.push_back(std::move(info));
      }
    }
  }
  return result;
}

}  // namespace figtools::io
