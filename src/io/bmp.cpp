// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// BMP codec: uncompressed 24/32-bit, bottom-up or top-down. BMP is kept
// because industrial cameras and legacy inspection tools still emit it.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "io/codecs.hpp"

namespace figtools::io_detail {
namespace {

std::uint16_t read_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int32_t read_i32(const std::uint8_t* p) { return static_cast<std::int32_t>(read_u32(p)); }

void write_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

}  // namespace

Result<ImageInfo> probe_bmp(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 30 || bytes[0] != 'B' || bytes[1] != 'M') {
    return Result<ImageInfo>::fail(ErrorCode::kParseError, "bmp: bad signature");
  }
  ImageInfo info;
  info.format = ImageFormat::kBmp;
  info.width = read_i32(bytes.data() + 18);
  info.height = read_i32(bytes.data() + 22);
  if (info.height < 0) {
    info.height = -info.height;
  }
  info.bit_depth = read_u16(bytes.data() + 28);
  info.channels = info.bit_depth / 8;
  info.has_alpha = info.bit_depth == 32;
  info.color_model = info.bit_depth == 32 ? "BGRA" : "BGR";
  info.compression = "none";
  return Result<ImageInfo>::ok(info);
}

Result<Image> decode_bmp(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M') {
    return Result<Image>::fail(ErrorCode::kParseError, "bmp: bad signature or truncated file");
  }

  const std::uint32_t data_offset = read_u32(bytes.data() + 10);
  const std::uint32_t header_size = read_u32(bytes.data() + 14);
  if (header_size < 40) {
    return Result<Image>::fail(ErrorCode::kUnsupported, "bmp: OS/2 headers are not supported");
  }
  const std::int32_t raw_width = read_i32(bytes.data() + 18);
  const std::int32_t raw_height = read_i32(bytes.data() + 22);
  const std::uint16_t bpp = read_u16(bytes.data() + 28);
  const std::uint32_t compression = read_u32(bytes.data() + 30);

  if (bpp != 24 && bpp != 32) {
    return Result<Image>::fail(ErrorCode::kUnsupported,
                               "bmp: only 24 and 32 bpp are supported, got " + std::to_string(bpp));
  }
  const bool bitfields = (compression == 3) && (bpp == 32);
  if (compression != 0 && !bitfields) {
    return Result<Image>::fail(ErrorCode::kUnsupported, "bmp: compressed bitmaps are not supported");
  }
  if (raw_width <= 0 || raw_height == 0) {
    return Result<Image>::fail(ErrorCode::kParseError, "bmp: invalid dimensions");
  }

  const int width = raw_width;
  const bool bottom_up = raw_height > 0;
  const int height = bottom_up ? raw_height : -raw_height;
  const int bytes_per_pixel = bpp / 8;
  const std::size_t row_stride =
      ((static_cast<std::size_t>(width) * static_cast<std::size_t>(bytes_per_pixel)) + 3U) & ~3U;
  const std::size_t required =
      static_cast<std::size_t>(data_offset) + row_stride * static_cast<std::size_t>(height);
  if (bytes.size() < required) {
    return Result<Image>::fail(ErrorCode::kParseError, "bmp: pixel data truncated");
  }

  Image image(width, height);
  MutableImageView out = image.mutable_view();
  for (int y = 0; y < height; ++y) {
    const int source_y = bottom_up ? (height - 1 - y) : y;
    const std::uint8_t* row = bytes.data() + data_offset + row_stride * static_cast<std::size_t>(source_y);
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* p = row + static_cast<std::ptrdiff_t>(x) * bytes_per_pixel;
      Rgba colour{p[2], p[1], p[0], 255};
      if (bytes_per_pixel == 4) {
        colour.a = (bitfields || p[3] != 0) ? p[3] : 255;
      }
      out.set(x, y, colour);
    }
  }
  return Result<Image>::ok(std::move(image));
}

Result<std::vector<std::uint8_t>> encode_bmp(const ImageView& image) {
  if (image.empty()) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kInvalidArgument, "bmp: empty image");
  }

  const int width = image.width();
  const int height = image.height();
  const std::size_t row_stride = ((static_cast<std::size_t>(width) * 3U) + 3U) & ~3U;
  const std::size_t pixel_bytes = row_stride * static_cast<std::size_t>(height);
  const std::uint32_t file_size = static_cast<std::uint32_t>(54 + pixel_bytes);

  std::vector<std::uint8_t> out;
  out.reserve(file_size);
  out.push_back('B');
  out.push_back('M');
  write_u32(out, file_size);
  write_u16(out, 0);
  write_u16(out, 0);
  write_u32(out, 54);
  write_u32(out, 40);
  write_u32(out, static_cast<std::uint32_t>(width));
  write_u32(out, static_cast<std::uint32_t>(height));
  write_u16(out, 1);
  write_u16(out, 24);
  write_u32(out, 0);
  write_u32(out, static_cast<std::uint32_t>(pixel_bytes));
  write_u32(out, 2835);
  write_u32(out, 2835);
  write_u32(out, 0);
  write_u32(out, 0);

  out.resize(54 + pixel_bytes, 0);
  for (int y = 0; y < height; ++y) {
    const int target_y = height - 1 - y;  // bottom-up
    std::uint8_t* row = out.data() + 54 + row_stride * static_cast<std::size_t>(target_y);
    for (int x = 0; x < width; ++x) {
      const Rgba colour = image.at(x, y);
      std::uint8_t* p = row + static_cast<std::ptrdiff_t>(x) * 3;
      p[0] = colour.b;
      p[1] = colour.g;
      p[2] = colour.r;
    }
  }
  return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

}  // namespace figtools::io_detail
