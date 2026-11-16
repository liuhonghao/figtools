// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Image I/O facade. Codecs live behind this header; callers never depend on
// zlib, libpng or file layout details.

#ifndef FIGTOOLS_IO_IMAGE_IO_HPP_
#define FIGTOOLS_IO_IMAGE_IO_HPP_

#include <cstdint>
#include <string>

#include "figtools/core/image.hpp"
#include "figtools/core/result.hpp"

namespace figtools {

enum class ImageFormat {
  kUnknown = 0,
  kPng,
  kBmp,
  kPpm,
  kJpeg,
  kWebp,
  kTga,
};

const char* to_string(ImageFormat format) noexcept;

// Format sniffed from the file extension, falling back to the magic bytes.
[[nodiscard]] ImageFormat format_from_path(const std::string& path);

// Header-level description of an image file. Cheap: it does not decode pixels.
struct ImageInfo {
  ImageFormat format = ImageFormat::kUnknown;
  int width = 0;
  int height = 0;
  int channels = 0;
  int bit_depth = 8;
  bool has_alpha = false;
  std::string color_model;   // "RGBA", "RGB", "Gray"
  std::string compression;   // "DEFLATE", "none", ...
  std::uint64_t file_bytes = 0;
};

[[nodiscard]] Result<ImageInfo> probe_image(const std::string& path);
[[nodiscard]] Result<Image> load_image(const std::string& path);

// Format is derived from the extension; unknown extensions are rejected.
[[nodiscard]] Status save_image(const Image& image, const std::string& path);

}  // namespace figtools

#endif  // FIGTOOLS_IO_IMAGE_IO_HPP_
