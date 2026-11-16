// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// stb_image-backed codecs for formats the hand-written decoders do not cover:
// JPEG, WebP and TGA. stb is a single public-domain header, compiled in one
// translation unit so the rest of the project does not see its declarations.

#include "io/codecs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_WEBP
#define STBI_ONLY_TGA
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../../third_party/stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ONLY_JPEG
#include "../../third_party/stb/stb_image_write.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace figtools::io_detail {
namespace {

int stb_components(ImageFormat format) {
  switch (format) {
    case ImageFormat::kJpeg:
      return 3;
    case ImageFormat::kWebp:
    case ImageFormat::kTga:
      return 4;
    default:
      return 0;
  }
}

const char* format_name(ImageFormat format) {
  switch (format) {
    case ImageFormat::kJpeg:
      return "JPEG";
    case ImageFormat::kWebp:
      return "WebP";
    case ImageFormat::kTga:
      return "TGA";
    default:
      return "unknown";
  }
}

}  // namespace

Result<Image> decode_stb(std::span<const std::uint8_t> bytes, ImageFormat format) {
  const int wanted = stb_components(format);
  if (wanted == 0) {
    return Result<Image>::fail(ErrorCode::kUnsupported, "stb: unsupported format");
  }
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width,
                                          &height, &channels, wanted);
  if (pixels == nullptr) {
    return Result<Image>::fail(ErrorCode::kParseError,
                               std::string(format_name(format)) + ": decode failed: " +
                                   stbi_failure_reason());
  }

  Image image(width, height);
  MutableImageView out = image.mutable_view();
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
  for (int y = 0; y < height; ++y) {
    const stbi_uc* src = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) *
                                      static_cast<std::size_t>(wanted);
    std::uint8_t* dst = out.row(y);
    for (int x = 0; x < width; ++x) {
      const std::size_t s = static_cast<std::size_t>(x) * static_cast<std::size_t>(wanted);
      const std::size_t d = static_cast<std::size_t>(x) * 4U;
      dst[d + 0] = src[s + 0];
      dst[d + 1] = wanted >= 3 ? src[s + 1] : src[s + 0];
      dst[d + 2] = wanted >= 3 ? src[s + 2] : src[s + 0];
      dst[d + 3] = wanted == 4 ? src[s + 3] : 255U;
    }
    (void)row_bytes;
  }
  stbi_image_free(pixels);
  return Result<Image>::ok(std::move(image));
}

Result<ImageInfo> probe_stb(std::span<const std::uint8_t> bytes, ImageFormat format) {
  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                            &channels) == 0) {
    return Result<ImageInfo>::fail(ErrorCode::kParseError,
                                   std::string(format_name(format)) + ": cannot read header");
  }
  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.channels = channels;
  info.bit_depth = 8;
  info.has_alpha = format != ImageFormat::kJpeg;
  info.color_model = format == ImageFormat::kJpeg ? "RGB" : "RGBA";
  info.compression = "DCT" ;
  return Result<ImageInfo>::ok(info);
}

Result<std::vector<std::uint8_t>> encode_jpeg(const ImageView& image, int quality) {
  if (image.empty()) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kInvalidArgument,
                                                   "jpeg: empty image");
  }
  // stbi_write_jpg_to_mem needs contiguous RGB rows; our Image is RGBA with
  // possible stride padding, so build a packed buffer first.
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(image.width()) *
                                static_cast<std::size_t>(image.height()) * 3U);
  for (int y = 0; y < image.height(); ++y) {
    const std::uint8_t* row = image.row(y);
    for (int x = 0; x < image.width(); ++x) {
      const std::size_t s = static_cast<std::size_t>(x) * 4U;
      const std::size_t d = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width()) +
                             static_cast<std::size_t>(x)) * 3U;
      rgb[d + 0] = row[s + 0];
      rgb[d + 1] = row[s + 1];
      rgb[d + 2] = row[s + 2];
    }
  }

  struct Sink {
    std::vector<std::uint8_t> bytes;
    static void write(void* context, void* data, int size) {
      auto* sink = static_cast<Sink*>(context);
      const auto* src = static_cast<const std::uint8_t*>(data);
      sink->bytes.insert(sink->bytes.end(), src, src + size);
    }
  } sink;

  const int ok = stbi_write_jpg_to_func(Sink::write, &sink, image.width(), image.height(), 3,
                                        rgb.data(), quality);
  if (ok == 0 || sink.bytes.empty()) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kInternal, "jpeg: encode failed");
  }
  return Result<std::vector<std::uint8_t>>::ok(std::move(sink.bytes));
}

}  // namespace figtools::io_detail
