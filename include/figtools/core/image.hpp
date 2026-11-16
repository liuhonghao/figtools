// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Pixel storage and views.
//
// Design notes
// ------------
// * All images are 8-bit RGBA, row major, tightly packed unless a view says
//   otherwise. A single canonical format removes an entire class of bugs
//   (channel order, alpha premultiplication, bit depth) from the operator
//   layer. Other formats are converted at the I/O boundary.
// * `Image` owns its pixels. `ImageView` never does -- it is what operators
//   accept, so nothing can mutate a buffer it does not own by accident.
// * Copying an `Image` copies pixels. Pass `ImageView` or move.

#ifndef FIGTOOLS_CORE_IMAGE_HPP_
#define FIGTOOLS_CORE_IMAGE_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace figtools {

struct Rgba {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

constexpr bool operator==(const Rgba& lhs, const Rgba& rhs) {
  return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
}

struct Size {
  int width = 0;
  int height = 0;

  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
  [[nodiscard]] std::size_t pixel_count() const {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  }
};

constexpr bool operator==(const Size& lhs, const Size& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height;
}

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
  [[nodiscard]] bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + width && py < y + height;
  }
};

constexpr int kChannels = 4;

// Non-owning read-only view over pixel data.
class ImageView {
 public:
  ImageView() = default;
  ImageView(const std::uint8_t* data, int width, int height, std::ptrdiff_t stride)
      : data_(data), width_(width), height_(height), stride_(stride) {}

  [[nodiscard]] const std::uint8_t* data() const { return data_; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] std::ptrdiff_t stride() const { return stride_; }
  [[nodiscard]] Size size() const { return Size{width_, height_}; }
  [[nodiscard]] bool empty() const { return data_ == nullptr || width_ <= 0 || height_ <= 0; }

  [[nodiscard]] bool contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
  }

  [[nodiscard]] const std::uint8_t* row(int y) const {
    return data_ + static_cast<std::ptrdiff_t>(y) * stride_;
  }

  [[nodiscard]] Rgba at(int x, int y) const {
    const std::uint8_t* p = row(y) + static_cast<std::ptrdiff_t>(x) * kChannels;
    return Rgba{p[0], p[1], p[2], p[3]};
  }

  // Clamped fetch: used by filters that read outside the image.
  [[nodiscard]] Rgba at_clamped(int x, int y) const {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width_) x = width_ - 1;
    if (y >= height_) y = height_ - 1;
    return at(x, y);
  }

 private:
  const std::uint8_t* data_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  std::ptrdiff_t stride_ = 0;
};

// Non-owning mutable view, used by in-place helpers (fills, debug drawing).
class MutableImageView {
 public:
  MutableImageView() = default;
  MutableImageView(std::uint8_t* data, int width, int height, std::ptrdiff_t stride)
      : data_(data), width_(width), height_(height), stride_(stride) {}

  [[nodiscard]] std::uint8_t* data() const { return data_; }
  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] std::ptrdiff_t stride() const { return stride_; }
  [[nodiscard]] ImageView as_const() const { return ImageView(data_, width_, height_, stride_); }

  [[nodiscard]] std::uint8_t* row(int y) const {
    return data_ + static_cast<std::ptrdiff_t>(y) * stride_;
  }

  void set(int x, int y, Rgba c) const {
    std::uint8_t* p = row(y) + static_cast<std::ptrdiff_t>(x) * kChannels;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
  }

 private:
  std::uint8_t* data_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  std::ptrdiff_t stride_ = 0;
};

// Owning RGBA8 image.
class Image {
 public:
  Image() = default;
  Image(int width, int height);
  Image(int width, int height, Rgba fill_color);

  [[nodiscard]] static Image filled(int width, int height, Rgba color);

  [[nodiscard]] int width() const { return width_; }
  [[nodiscard]] int height() const { return height_; }
  [[nodiscard]] Size size() const { return Size{width_, height_}; }
  [[nodiscard]] bool empty() const { return pixels_.empty(); }
  [[nodiscard]] std::ptrdiff_t stride() const {
    return static_cast<std::ptrdiff_t>(width_) * kChannels;
  }
  [[nodiscard]] std::size_t byte_size() const { return pixels_.size(); }

  [[nodiscard]] std::uint8_t* data() { return pixels_.data(); }
  [[nodiscard]] const std::uint8_t* data() const { return pixels_.data(); }
  [[nodiscard]] std::uint8_t* row(int y) {
    return pixels_.data() + static_cast<std::ptrdiff_t>(y) * stride();
  }
  [[nodiscard]] const std::uint8_t* row(int y) const {
    return pixels_.data() + static_cast<std::ptrdiff_t>(y) * stride();
  }

  [[nodiscard]] ImageView view() const {
    return ImageView(pixels_.data(), width_, height_, stride());
  }
  [[nodiscard]] MutableImageView mutable_view() {
    return MutableImageView(pixels_.data(), width_, height_, stride());
  }

  [[nodiscard]] Rgba at(int x, int y) const;
  void set(int x, int y, Rgba color);
  void fill(Rgba color);

 private:
  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> pixels_;
};

}  // namespace figtools

#endif  // FIGTOOLS_CORE_IMAGE_HPP_
