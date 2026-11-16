// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <cstdlib>

#include "figtools/core/color.hpp"
#include "figtools/core/geometry.hpp"
#include "figtools/core/hash.hpp"
#include "figtools/core/image.hpp"
#include "figtools/core/metrics.hpp"
#include "test_framework.hpp"

using namespace figtools;

namespace {

Image make_gradient(int width, int height) {
  Image image(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.set(x, y,
                Rgba{static_cast<std::uint8_t>(x % 256), static_cast<std::uint8_t>(y % 256), 64,
                     255});
    }
  }
  return image;
}

}  // namespace

TEST(image_fill_and_access) {
  Image image(4, 3, Rgba{10, 20, 30, 255});
  CHECK_EQ(image.width(), 4);
  CHECK_EQ(image.height(), 3);
  CHECK_EQ(static_cast<int>(image.byte_size()), 4 * 3 * 4);
  const Rgba pixel = image.at(2, 1);
  CHECK_EQ(static_cast<int>(pixel.r), 10);
  CHECK_EQ(static_cast<int>(pixel.g), 20);
  CHECK_EQ(static_cast<int>(pixel.b), 30);
  CHECK_EQ(static_cast<int>(pixel.a), 255);

  image.set(0, 0, Rgba{1, 2, 3, 4});
  CHECK_EQ(static_cast<int>(image.at(0, 0).a), 4);
}

TEST(image_default_alpha_is_opaque) {
  Image image(2, 2);
  CHECK_EQ(static_cast<int>(image.at(1, 1).a), 255);
}

TEST(resize_dimensions_and_identity) {
  const Image source = make_gradient(16, 12);
  auto halved = resize(source.view(), 8, 6, Interp::kBilinear);
  CHECK(halved.has_value());
  CHECK_EQ(halved.value().width(), 8);
  CHECK_EQ(halved.value().height(), 6);

  auto same = resize(source.view(), 16, 12, Interp::kNearest);
  CHECK(same.has_value());
  CHECK_EQ(static_cast<int>(same.value().at(5, 7).r),
           static_cast<int>(source.at(5, 7).r));
}

TEST(resize_rejects_invalid_size) {
  const Image source = make_gradient(8, 8);
  auto result = resize(source.view(), 0, 4, Interp::kBilinear);
  CHECK(!result.has_value());
  CHECK_EQ(static_cast<int>(result.error().code), static_cast<int>(ErrorCode::kInvalidArgument));
}

TEST(crop_matches_source_region) {
  const Image source = make_gradient(20, 10);
  auto result = crop(source.view(), Rect{4, 2, 6, 5});
  CHECK(result.has_value());
  CHECK_EQ(result.value().width(), 6);
  CHECK_EQ(result.value().height(), 5);
  CHECK_EQ(static_cast<int>(result.value().at(0, 0).r), static_cast<int>(source.at(4, 2).r));
  CHECK_EQ(static_cast<int>(result.value().at(5, 4).g), static_cast<int>(source.at(9, 6).g));
}

TEST(crop_clamps_to_bounds) {
  const Image source = make_gradient(10, 10);
  auto result = crop(source.view(), Rect{-5, -5, 8, 8});
  CHECK(result.has_value());
  CHECK_EQ(result.value().width(), 3);
  CHECK_EQ(result.value().height(), 3);
}

TEST(rotate90_four_times_is_identity) {
  const Image source = make_gradient(9, 5);
  auto first = rotate90(source.view(), 1);
  CHECK(first.has_value());
  if (!first) {
    return;
  }
  CHECK_EQ(first.value().width(), 5);
  CHECK_EQ(first.value().height(), 9);

  Image current = std::move(first.value());
  for (int i = 0; i < 3; ++i) {
    auto next = rotate90(current.view(), 1);
    CHECK(next.has_value());
    if (!next) {
      return;
    }
    current = std::move(next.value());
  }

  CHECK_EQ(current.width(), 9);
  CHECK_EQ(current.height(), 5);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 9; ++x) {
      CHECK_EQ(static_cast<int>(current.at(x, y).r), static_cast<int>(source.at(x, y).r));
    }
  }
}

TEST(flip_is_an_involution) {
  const Image source = make_gradient(7, 4);
  auto once = flip(source.view(), FlipAxis::kHorizontal);
  CHECK(once.has_value());
  CHECK_EQ(static_cast<int>(once.value().at(0, 0).r), static_cast<int>(source.at(6, 0).r));
  auto twice = flip(once.value().view(), FlipAxis::kHorizontal);
  CHECK(twice.has_value());
  CHECK_EQ(static_cast<int>(twice.value().at(3, 2).r), static_cast<int>(source.at(3, 2).r));
}

TEST(colour_conversions_round_trip) {
  const Rgba samples[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
                          {128, 64, 32, 255}, {255, 255, 255, 255}, {0, 0, 0, 255}};
  for (const Rgba& sample : samples) {
    const Hsv hsv = rgb_to_hsv(sample);
    const Rgba back = hsv_to_rgb(hsv);
    CHECK(std::abs(static_cast<int>(back.r) - static_cast<int>(sample.r)) <= 1);
    CHECK(std::abs(static_cast<int>(back.g) - static_cast<int>(sample.g)) <= 1);
    CHECK(std::abs(static_cast<int>(back.b) - static_cast<int>(sample.b)) <= 1);
  }
}

TEST(luma_matches_rec601) {
  CHECK_EQ(static_cast<int>(luma(Rgba{255, 0, 0, 255})), 76);
  CHECK_EQ(static_cast<int>(luma(Rgba{0, 255, 0, 255})), 150);
  CHECK_EQ(static_cast<int>(luma(Rgba{255, 255, 255, 255})), 255);
  CHECK_EQ(static_cast<int>(luma(Rgba{0, 0, 0, 255})), 0);
}

TEST(hash_is_deterministic_and_sensitive) {
  const Image a = make_gradient(12, 8);
  Image b = make_gradient(12, 8);
  CHECK_EQ(to_hex(hash_image(a.view())), to_hex(hash_image(b.view())));

  b.set(3, 3, Rgba{0, 0, 0, 255});
  CHECK(to_hex(hash_image(a.view())) != to_hex(hash_image(b.view())));
}

TEST(metrics_of_identical_images) {
  const Image a = make_gradient(24, 16);
  const Image b = make_gradient(24, 16);
  auto metrics = compare_images(a.view(), b.view());
  CHECK(metrics.has_value());
  CHECK(metrics.value().identical);
  CHECK_EQ(metrics.value().ssim, 1.0);
  CHECK_EQ(metrics.value().mae, 0.0);
  CHECK(std::isinf(metrics.value().psnr));
}

TEST(metrics_detect_difference) {
  Image a = make_gradient(32, 32);
  Image b = make_gradient(32, 32);
  b.set(10, 10, Rgba{0, 0, 0, 255});
  auto metrics = compare_images(a.view(), b.view());
  CHECK(metrics.has_value());
  CHECK(!metrics.value().identical);
  CHECK(metrics.value().mae > 0.0);
  CHECK(metrics.value().psnr < 100.0);
}

TEST(metrics_reject_size_mismatch) {
  const Image a = make_gradient(8, 8);
  const Image b = make_gradient(8, 9);
  auto metrics = compare_images(a.view(), b.view());
  CHECK(!metrics.has_value());
}
