// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Netpbm codec (P5/P6). Lossless, trivially simple and dependency free, which
// makes it the format used by golden-image tests and by the demo command.

#include <cctype>
#include <charconv>
#include <cstdint>
#include <string>
#include <vector>

#include "io/codecs.hpp"

namespace figtools::io_detail {
namespace {

struct Cursor {
  std::span<const std::uint8_t> bytes;
  std::size_t pos = 0;
};

void skip_space_and_comments(Cursor& cursor) {
  while (cursor.pos < cursor.bytes.size()) {
    const char c = static_cast<char>(cursor.bytes[cursor.pos]);
    if (c == '#') {
      while (cursor.pos < cursor.bytes.size() && cursor.bytes[cursor.pos] != '\n') {
        ++cursor.pos;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      ++cursor.pos;
      continue;
    }
    break;
  }
}

bool read_token(Cursor& cursor, std::string& token) {
  skip_space_and_comments(cursor);
  const std::size_t start = cursor.pos;
  while (cursor.pos < cursor.bytes.size()) {
    const char c = static_cast<char>(cursor.bytes[cursor.pos]);
    if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == '#') {
      break;
    }
    ++cursor.pos;
  }
  if (cursor.pos == start) {
    return false;
  }
  token.assign(reinterpret_cast<const char*>(cursor.bytes.data() + start), cursor.pos - start);
  return true;
}

bool read_int(Cursor& cursor, int& value) {
  std::string token;
  if (!read_token(cursor, token)) {
    return false;
  }
  const char* first = token.data();
  const char* last = token.data() + token.size();
  const auto result = std::from_chars(first, last, value);
  return result.ec == std::errc{} && result.ptr == last;
}

}  // namespace

Result<ImageInfo> probe_ppm(std::span<const std::uint8_t> bytes) {
  Cursor cursor{bytes, 0};
  std::string magic;
  int width = 0;
  int height = 0;
  int max_value = 0;
  if (!read_token(cursor, magic) || !read_int(cursor, width) || !read_int(cursor, height) ||
      !read_int(cursor, max_value)) {
    return Result<ImageInfo>::fail(ErrorCode::kParseError, "ppm: malformed header");
  }
  if (magic != "P6" && magic != "P5") {
    return Result<ImageInfo>::fail(ErrorCode::kUnsupported, "ppm: only P5 and P6 are supported");
  }

  ImageInfo info;
  info.format = ImageFormat::kPpm;
  info.width = width;
  info.height = height;
  info.channels = (magic == "P6") ? 3 : 1;
  info.bit_depth = (max_value > 255) ? 16 : 8;
  info.color_model = (magic == "P6") ? "RGB" : "Gray";
  info.compression = "none";
  return Result<ImageInfo>::ok(info);
}

Result<Image> decode_ppm(std::span<const std::uint8_t> bytes) {
  Cursor cursor{bytes, 0};
  std::string magic;
  int width = 0;
  int height = 0;
  int max_value = 0;
  if (!read_token(cursor, magic) || !read_int(cursor, width) || !read_int(cursor, height) ||
      !read_int(cursor, max_value)) {
    return Result<Image>::fail(ErrorCode::kParseError, "ppm: malformed header");
  }
  if (magic != "P6" && magic != "P5") {
    return Result<Image>::fail(ErrorCode::kUnsupported, "ppm: only P5 and P6 are supported");
  }
  if (width <= 0 || height <= 0) {
    return Result<Image>::fail(ErrorCode::kParseError, "ppm: invalid dimensions");
  }
  if (max_value <= 0 || max_value > 255) {
    return Result<Image>::fail(ErrorCode::kUnsupported,
                               "ppm: only 8-bit samples are supported, got maxval " +
                                   std::to_string(max_value));
  }

  // Exactly one whitespace character separates the header from the raster.
  if (cursor.pos >= bytes.size()) {
    return Result<Image>::fail(ErrorCode::kParseError, "ppm: missing raster");
  }
  ++cursor.pos;

  const int channels = (magic == "P6") ? 3 : 1;
  const std::size_t expected =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
      static_cast<std::size_t>(channels);
  if (bytes.size() < cursor.pos + expected) {
    return Result<Image>::fail(ErrorCode::kParseError, "ppm: raster truncated");
  }

  Image image(width, height);
  MutableImageView out = image.mutable_view();
  const std::uint8_t* raster = bytes.data() + cursor.pos;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)) *
                                static_cast<std::size_t>(channels);
      if (channels == 3) {
        out.set(x, y, Rgba{raster[index], raster[index + 1], raster[index + 2], 255});
      } else {
        const std::uint8_t v = raster[index];
        out.set(x, y, Rgba{v, v, v, 255});
      }
    }
  }
  return Result<Image>::ok(std::move(image));
}

Result<std::vector<std::uint8_t>> encode_ppm(const ImageView& image) {
  if (image.empty()) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kInvalidArgument, "ppm: empty image");
  }
  const std::string header = "P6\n" + std::to_string(image.width()) + " " +
                             std::to_string(image.height()) + "\n255\n";
  std::vector<std::uint8_t> out;
  out.reserve(header.size() + static_cast<std::size_t>(image.width()) *
                             static_cast<std::size_t>(image.height()) * 3U);
  out.insert(out.end(), header.begin(), header.end());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const Rgba colour = image.at(x, y);
      out.push_back(colour.r);
      out.push_back(colour.g);
      out.push_back(colour.b);
    }
  }
  return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

}  // namespace figtools::io_detail
