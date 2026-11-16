// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/io/image_io.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "io/codecs.hpp"

namespace figtools {
namespace {

constexpr std::size_t kProbeBytes = 64U * 1024U;

std::string lowercase_extension(const std::string& path) {
  std::string extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

Result<std::vector<std::uint8_t>> read_file(const std::string& path, std::size_t max_bytes) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kIoError,
                                                   "cannot open file: " + path);
  }
  std::vector<std::uint8_t> bytes;
  if (max_bytes > 0) {
    bytes.resize(max_bytes);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(max_bytes));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
  } else {
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  }
  if (bytes.empty()) {
    return Result<std::vector<std::uint8_t>>::fail(ErrorCode::kIoError, "empty or unreadable file: " + path);
  }
  return Result<std::vector<std::uint8_t>>::ok(std::move(bytes));
}

ImageFormat sniff(std::span<const std::uint8_t> bytes) {
  if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' &&
      bytes[3] == 'G') {
    return ImageFormat::kPng;
  }
  if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
    return ImageFormat::kBmp;
  }
  if (bytes.size() >= 2 && bytes[0] == 'P' && (bytes[1] == '5' || bytes[1] == '6')) {
    return ImageFormat::kPpm;
  }
  if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
    return ImageFormat::kJpeg;
  }
  if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
      bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' &&
      bytes[11] == 'P') {
    return ImageFormat::kWebp;
  }
  return ImageFormat::kUnknown;
}

}  // namespace

const char* to_string(ImageFormat format) noexcept {
  switch (format) {
    case ImageFormat::kPng:
      return "PNG";
    case ImageFormat::kBmp:
      return "BMP";
    case ImageFormat::kPpm:
      return "PPM";
    case ImageFormat::kJpeg:
      return "JPEG";
    case ImageFormat::kWebp:
      return "WebP";
    case ImageFormat::kTga:
      return "TGA";
    case ImageFormat::kUnknown:
    default:
      return "unknown";
  }
}

ImageFormat format_from_path(const std::string& path) {
  const std::string extension = lowercase_extension(path);
  if (extension == ".png") {
    return ImageFormat::kPng;
  }
  if (extension == ".bmp") {
    return ImageFormat::kBmp;
  }
  if (extension == ".ppm" || extension == ".pgm" || extension == ".pnm") {
    return ImageFormat::kPpm;
  }
  if (extension == ".jpg" || extension == ".jpeg" || extension == ".jpe") {
    return ImageFormat::kJpeg;
  }
  if (extension == ".webp") {
    return ImageFormat::kWebp;
  }
  if (extension == ".tga") {
    return ImageFormat::kTga;
  }
  return ImageFormat::kUnknown;
}

Result<Image> load_image(const std::string& path) {
  auto bytes = read_file(path, 0);
  if (!bytes) {
    return Result<Image>::fail(bytes.error().code, bytes.error().message);
  }

  auto format = sniff(bytes.value());
  if (format == ImageFormat::kUnknown) {
    // TGA has no reliable magic; fall back to the extension.
    format = format_from_path(path);
  }
  switch (format) {
    case ImageFormat::kPng:
      return io_detail::decode_png(bytes.value());
    case ImageFormat::kBmp:
      return io_detail::decode_bmp(bytes.value());
    case ImageFormat::kPpm:
      return io_detail::decode_ppm(bytes.value());
    case ImageFormat::kJpeg:
    case ImageFormat::kWebp:
    case ImageFormat::kTga:
      return io_detail::decode_stb(bytes.value(), format);
    case ImageFormat::kUnknown:
    default:
      return Result<Image>::fail(ErrorCode::kUnsupported,
                                 "unrecognised image format: " + path +
                                     " (supported: PNG, BMP, PPM, JPEG, WebP, TGA)");
  }
}

Result<ImageInfo> probe_image(const std::string& path) {
  const ImageFormat sniffed = format_from_path(path);
  const bool needs_full_file = sniffed == ImageFormat::kJpeg || sniffed == ImageFormat::kWebp ||
                               sniffed == ImageFormat::kTga;
  auto bytes = read_file(path, needs_full_file ? 0 : kProbeBytes);
  if (!bytes) {
    return Result<ImageInfo>::fail(bytes.error().code, bytes.error().message);
  }

  Result<ImageInfo> info = Result<ImageInfo>::fail(ErrorCode::kUnsupported, "unknown format");
  auto detected = sniff(bytes.value());
  if (detected == ImageFormat::kUnknown) {
    detected = sniffed;
  }
  switch (detected) {
    case ImageFormat::kPng:
      info = io_detail::probe_png(bytes.value());
      break;
    case ImageFormat::kBmp:
      info = io_detail::probe_bmp(bytes.value());
      break;
    case ImageFormat::kPpm:
      info = io_detail::probe_ppm(bytes.value());
      break;
    case ImageFormat::kJpeg:
    case ImageFormat::kWebp:
    case ImageFormat::kTga:
      info = io_detail::probe_stb(bytes.value(), detected);
      break;
    case ImageFormat::kUnknown:
    default:
      return Result<ImageInfo>::fail(ErrorCode::kUnsupported,
                                     "unrecognised image format: " + path);
  }
  if (!info) {
    return info;
  }

  ImageInfo value = info.value();
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  value.file_bytes = ec ? 0 : static_cast<std::uint64_t>(size);
  return Result<ImageInfo>::ok(value);
}

Status save_image(const Image& image, const std::string& path) {
  if (image.empty()) {
    return Status::fail(ErrorCode::kInvalidArgument, "save: empty image");
  }

  const auto format = format_from_path(path);
  Result<std::vector<std::uint8_t>> encoded =
      Result<std::vector<std::uint8_t>>::fail(ErrorCode::kUnsupported, "unsupported extension");
  switch (format) {
    case ImageFormat::kPng:
      encoded = io_detail::encode_png(image.view());
      break;
    case ImageFormat::kBmp:
      encoded = io_detail::encode_bmp(image.view());
      break;
    case ImageFormat::kPpm:
      encoded = io_detail::encode_ppm(image.view());
      break;
    case ImageFormat::kJpeg:
      encoded = io_detail::encode_jpeg(image.view(), 90);
      break;
    case ImageFormat::kWebp:
    case ImageFormat::kTga:
      return Status::fail(ErrorCode::kUnsupported,
                          "writing this format is not supported: " + path);
    case ImageFormat::kUnknown:
    default:
      return Status::fail(ErrorCode::kUnsupported,
                          "cannot infer output format from extension: " + path +
                              " (use .png, .bmp, .ppm or .jpg)");
  }
  if (!encoded) {
    return Status::fail(encoded.error().code, encoded.error().message);
  }

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return Status::fail(ErrorCode::kIoError, "cannot write file: " + path);
  }
  stream.write(reinterpret_cast<const char*>(encoded.value().data()),
               static_cast<std::streamsize>(encoded.value().size()));
  if (!stream) {
    return Status::fail(ErrorCode::kIoError, "write failed: " + path);
  }
  return Status::ok();
}

}  // namespace figtools
