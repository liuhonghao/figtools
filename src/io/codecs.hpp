// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Internal codec interface. Not installed; only src/io/*.cpp may include it.

#ifndef FIGTOOLS_SRC_IO_CODECS_HPP_
#define FIGTOOLS_SRC_IO_CODECS_HPP_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "figtools/core/image.hpp"
#include "figtools/core/result.hpp"
#include "figtools/io/image_io.hpp"

namespace figtools::io_detail {

// --- PNG -------------------------------------------------------------------
[[nodiscard]] Result<Image> decode_png(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_png(const ImageView& image);
[[nodiscard]] Result<ImageInfo> probe_png(std::span<const std::uint8_t> bytes);

// --- BMP -------------------------------------------------------------------
[[nodiscard]] Result<Image> decode_bmp(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_bmp(const ImageView& image);
[[nodiscard]] Result<ImageInfo> probe_bmp(std::span<const std::uint8_t> bytes);

// --- PPM / PGM -------------------------------------------------------------
[[nodiscard]] Result<Image> decode_ppm(std::span<const std::uint8_t> bytes);
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_ppm(const ImageView& image);
[[nodiscard]] Result<ImageInfo> probe_ppm(std::span<const std::uint8_t> bytes);

// --- stb-backed: JPEG / WebP / TGA -----------------------------------------
[[nodiscard]] Result<Image> decode_stb(std::span<const std::uint8_t> bytes, ImageFormat format);
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_jpeg(const ImageView& image, int quality);
[[nodiscard]] Result<ImageInfo> probe_stb(std::span<const std::uint8_t> bytes, ImageFormat format);

}  // namespace figtools::io_detail

#endif  // FIGTOOLS_SRC_IO_CODECS_HPP_
