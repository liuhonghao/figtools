// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// PNG codec built on zlib. Supports the baseline subset that covers virtually
// every 8-bit image produced by cameras, screenshots and renderers:
//   colour types 0/2/3/4/6, bit depth 8, non-interlaced.
// Anything outside that subset is rejected with kUnsupported instead of being
// decoded incorrectly.

#include <zlib.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "io/codecs.hpp"

namespace figtools::io_detail {
namespace {

constexpr std::array<std::uint8_t, 8> kSignature = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

std::uint32_t read_u32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void append_chunk(std::vector<std::uint8_t>& out, const char type[4],
                  const std::vector<std::uint8_t>& data) {
  write_u32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t crc_start = out.size();
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>(type[i]));
  }
  out.insert(out.end(), data.begin(), data.end());
  const uLong crc = crc32(0L, out.data() + crc_start, static_cast<uInt>(4 + data.size()));
  write_u32(out, static_cast<std::uint32_t>(crc));
}

int paeth_predictor(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

int colour_channels(int colour_type) {
  switch (colour_type) {
    case 0:
      return 1;
    case 2:
      return 3;
    case 3:
      return 1;
    case 4:
      return 2;
    case 6:
      return 4;
    default:
      return 0;
  }
}

}  // namespace

Result<ImageInfo> probe_png(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 33) {
    return Result<ImageInfo>::fail(ErrorCode::kParseError, "png: file too short");
  }
  for (std::size_t i = 0; i < kSignature.size(); ++i) {
    if (bytes[i] != kSignature[i]) {
      return Result<ImageInfo>::fail(ErrorCode::kParseError, "png: bad signature");
    }
  }
  if (std::memcmp(bytes.data() + 12, "IHDR", 4) != 0) {
    return Result<ImageInfo>::fail(ErrorCode::kParseError, "png: missing IHDR");
  }

  ImageInfo info;
  info.format = ImageFormat::kPng;
  info.width = static_cast<int>(read_u32(bytes.data() + 16));
  info.height = static_cast<int>(read_u32(bytes.data() + 20));
  info.bit_depth = static_cast<int>(bytes[24]);
  const int colour_type = static_cast<int>(bytes[25]);
  info.channels = colour_channels(colour_type);
  info.has_alpha = (colour_type == 4 || colour_type == 6);
  info.color_model = colour_type == 0   ? "Gray"
                     : colour_type == 2 ? "RGB"
                     : colour_type == 3 ? "Palette"
                     : colour_type == 4 ? "Gray+Alpha"
                                        : "RGBA";
  info.compression = "DEFLATE";
  return Result<ImageInfo>::ok(info);
}

Result<Image> decode_png(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 8 + 25) {
    return Result<Image>::fail(ErrorCode::kParseError, "png: file too short");
  }
  for (std::size_t i = 0; i < kSignature.size(); ++i) {
    if (bytes[i] != kSignature[i]) {
      return Result<Image>::fail(ErrorCode::kParseError, "png: bad signature");
    }
  }

  int width = 0;
  int height = 0;
  int bit_depth = 0;
  int colour_type = 0;
  int interlace = 0;
  std::vector<std::uint8_t> idat;
  std::vector<Rgba> palette;

  std::size_t offset = 8;
  while (offset + 12 <= bytes.size()) {
    const std::uint32_t length = read_u32(bytes.data() + offset);
    const std::string type(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
    const std::size_t data_offset = offset + 8;
    if (data_offset + length + 4 > bytes.size()) {
      return Result<Image>::fail(ErrorCode::kParseError, "png: truncated chunk " + type);
    }

    if (type == "IHDR") {
      width = static_cast<int>(read_u32(bytes.data() + data_offset));
      height = static_cast<int>(read_u32(bytes.data() + data_offset + 4));
      bit_depth = static_cast<int>(bytes[data_offset + 8]);
      colour_type = static_cast<int>(bytes[data_offset + 9]);
      interlace = static_cast<int>(bytes[data_offset + 12]);
    } else if (type == "PLTE") {
      palette.reserve(length / 3);
      for (std::uint32_t i = 0; i + 2 < length; i += 3) {
        palette.push_back(Rgba{bytes[data_offset + i], bytes[data_offset + i + 1],
                               bytes[data_offset + i + 2], 255});
      }
    } else if (type == "IDAT") {
      idat.insert(idat.end(), bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                  bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + length));
    } else if (type == "IEND") {
      break;
    }
    offset = data_offset + length + 4;
  }

  if (width <= 0 || height <= 0) {
    return Result<Image>::fail(ErrorCode::kParseError, "png: missing or invalid IHDR");
  }
  if (bit_depth != 8) {
    return Result<Image>::fail(ErrorCode::kUnsupported,
                               "png: only 8-bit images are supported, got " +
                                   std::to_string(bit_depth) + "-bit");
  }
  if (interlace != 0) {
    return Result<Image>::fail(ErrorCode::kUnsupported, "png: interlaced images are not supported");
  }
  const int channels = colour_channels(colour_type);
  if (channels == 0) {
    return Result<Image>::fail(ErrorCode::kUnsupported, "png: unsupported colour type");
  }
  if (colour_type == 3 && palette.empty()) {
    return Result<Image>::fail(ErrorCode::kParseError, "png: palette image without PLTE");
  }
  if (idat.empty()) {
    return Result<Image>::fail(ErrorCode::kParseError, "png: no image data");
  }

  const std::size_t row_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
  const std::size_t raw_size = (row_bytes + 1) * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> raw(raw_size);

  uLongf dest_len = static_cast<uLongf>(raw_size);
  const int zret = uncompress(raw.data(), &dest_len, idat.data(), static_cast<uLong>(idat.size()));
  if (zret != Z_OK || dest_len != static_cast<uLongf>(raw_size)) {
    return Result<Image>::fail(ErrorCode::kParseError, "png: corrupted image data");
  }

  // Undo per-scanline filters.
  std::vector<std::uint8_t> pixels(row_bytes * static_cast<std::size_t>(height));
  std::vector<std::uint8_t> previous(row_bytes, 0);
  for (int y = 0; y < height; ++y) {
    const std::uint8_t* src = raw.data() + static_cast<std::size_t>(y) * (row_bytes + 1);
    const int filter = static_cast<int>(src[0]);
    const std::uint8_t* filtered = src + 1;
    std::uint8_t* current = pixels.data() + static_cast<std::size_t>(y) * row_bytes;
    for (std::size_t i = 0; i < row_bytes; ++i) {
      const int left = (i >= static_cast<std::size_t>(channels))
                           ? static_cast<int>(current[i - static_cast<std::size_t>(channels)])
                           : 0;
      const int up = static_cast<int>(previous[i]);
      const int up_left = (i >= static_cast<std::size_t>(channels))
                              ? static_cast<int>(previous[i - static_cast<std::size_t>(channels)])
                              : 0;
      int value = static_cast<int>(filtered[i]);
      switch (filter) {
        case 0:
          break;
        case 1:
          value += left;
          break;
        case 2:
          value += up;
          break;
        case 3:
          value += (left + up) / 2;
          break;
        case 4:
          value += paeth_predictor(left, up, up_left);
          break;
        default:
          return Result<Image>::fail(ErrorCode::kParseError, "png: unknown filter type");
      }
      current[i] = static_cast<std::uint8_t>(value & 0xFF);
    }
    previous.assign(current, current + row_bytes);
  }

  // Expand to RGBA8.
  Image image(width, height);
  MutableImageView out = image.mutable_view();
  for (int y = 0; y < height; ++y) {
    const std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * row_bytes;
    for (int x = 0; x < width; ++x) {
      Rgba colour{0, 0, 0, 255};
      const std::size_t base = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
      switch (colour_type) {
        case 0:
          colour = Rgba{row[base], row[base], row[base], 255};
          break;
        case 4:
          colour = Rgba{row[base], row[base], row[base], row[base + 1]};
          break;
        case 2:
          colour = Rgba{row[base], row[base + 1], row[base + 2], 255};
          break;
        case 6:
          colour = Rgba{row[base], row[base + 1], row[base + 2], row[base + 3]};
          break;
        case 3: {
          const auto index = static_cast<std::size_t>(row[base]);
          colour = (index < palette.size()) ? palette[index] : Rgba{0, 0, 0, 255};
          break;
        }
        default:
          break;
      }
      out.set(x, y, colour);
    }
  }

  return Result<Image>::ok(std::move(image));
}

Result<std::vector<std::uint8_t>> encode_png(const ImageView& image) {
  if (image.empty()) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kInvalidArgument,
                                                   "png: empty image");
  }

  const int width = image.width();
  const int height = image.height();
  const std::size_t row_bytes = static_cast<std::size_t>(width) * kChannels;

  std::vector<std::uint8_t> raw((row_bytes + 1) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    std::uint8_t* dst = raw.data() + static_cast<std::size_t>(y) * (row_bytes + 1);
    dst[0] = 0;  // filter: none
    std::memcpy(dst + 1, image.row(y), row_bytes);
  }

  uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::uint8_t> compressed(compressed_size);
  const int zret = compress2(compressed.data(), &compressed_size, raw.data(),
                             static_cast<uLong>(raw.size()), 6);
  if (zret != Z_OK) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kInternal,
                                                   "png: compression failed");
  }
  compressed.resize(compressed_size);

  std::vector<std::uint8_t> header(13, 0);
  header[0] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 24) & 0xFF);
  header[1] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 16) & 0xFF);
  header[2] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(width) >> 8) & 0xFF);
  header[3] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(width) & 0xFF);
  header[4] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 24) & 0xFF);
  header[5] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 16) & 0xFF);
  header[6] = static_cast<std::uint8_t>((static_cast<std::uint32_t>(height) >> 8) & 0xFF);
  header[7] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) & 0xFF);
  header[8] = 8;   // bit depth
  header[9] = 6;   // colour type: RGBA
  header[10] = 0;  // compression
  header[11] = 0;  // filter
  header[12] = 0;  // interlace

  std::vector<std::uint8_t> out;
  out.reserve(compressed.size() + 64);
  out.insert(out.end(), kSignature.begin(), kSignature.end());
  append_chunk(out, "IHDR", header);
  append_chunk(out, "IDAT", compressed);
  append_chunk(out, "IEND", {});
  return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

}  // namespace figtools::io_detail
