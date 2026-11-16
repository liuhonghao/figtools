// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <filesystem>
#include <string>

#include "figtools/core/hash.hpp"
#include "figtools/io/image_io.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace figtools;

namespace {

void check_rgb_equal(const Image& lhs, const Image& rhs, bool check_alpha) {
  CHECK_EQ(lhs.width(), rhs.width());
  CHECK_EQ(lhs.height(), rhs.height());
  for (int y = 0; y < lhs.height(); ++y) {
    for (int x = 0; x < lhs.width(); ++x) {
      const Rgba a = lhs.at(x, y);
      const Rgba b = rhs.at(x, y);
      CHECK_EQ(static_cast<int>(a.r), static_cast<int>(b.r));
      CHECK_EQ(static_cast<int>(a.g), static_cast<int>(b.g));
      CHECK_EQ(static_cast<int>(a.b), static_cast<int>(b.b));
      if (check_alpha) {
        CHECK_EQ(static_cast<int>(a.a), static_cast<int>(b.a));
      }
    }
  }
}

}  // namespace

TEST(png_roundtrip_preserves_pixels) {
  const Image source = ft_test::make_test_image(37, 23);
  const auto path = ft_test::temp_path("roundtrip.png");

  auto status = save_image(source, path.string());
  CHECK(status.has_value());

  auto loaded = load_image(path.string());
  CHECK(loaded.has_value());
  if (loaded) {
    check_rgb_equal(source, loaded.value(), true);
  }

  auto info = probe_image(path.string());
  CHECK(info.has_value());
  if (info) {
    CHECK_EQ(static_cast<int>(info.value().format), static_cast<int>(ImageFormat::kPng));
    CHECK_EQ(info.value().width, 37);
    CHECK_EQ(info.value().height, 23);
    CHECK_EQ(info.value().bit_depth, 8);
  }

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(bmp_roundtrip_preserves_rgb) {
  const Image source = ft_test::make_test_image(19, 13);
  const auto path = ft_test::temp_path("roundtrip.bmp");

  auto status = save_image(source, path.string());
  CHECK(status.has_value());

  auto loaded = load_image(path.string());
  CHECK(loaded.has_value());
  if (loaded) {
    check_rgb_equal(source, loaded.value(), false);
  }

  auto info = probe_image(path.string());
  CHECK(info.has_value());
  if (info) {
    CHECK_EQ(static_cast<int>(info.value().format), static_cast<int>(ImageFormat::kBmp));
    CHECK_EQ(info.value().bit_depth, 24);
  }

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(ppm_roundtrip_preserves_rgb) {
  const Image source = ft_test::make_test_image(21, 17);
  const auto path = ft_test::temp_path("roundtrip.ppm");

  auto status = save_image(source, path.string());
  CHECK(status.has_value());

  auto loaded = load_image(path.string());
  CHECK(loaded.has_value());
  if (loaded) {
    check_rgb_equal(source, loaded.value(), false);
  }

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(load_rejects_unknown_extension_and_missing_file) {
  auto missing = load_image("definitely_not_here.png");
  CHECK(!missing.has_value());

  const auto path = ft_test::temp_path("data.unknown");
  std::FILE* file = std::fopen(path.string().c_str(), "wb");
  CHECK(file != nullptr);
  if (file != nullptr) {
    const char payload[] = "not an image";
    std::fwrite(payload, 1, sizeof(payload) - 1, file);
    std::fclose(file);
  }

  auto unsupported = load_image(path.string());
  CHECK(!unsupported.has_value());
  CHECK_EQ(static_cast<int>(unsupported.error().code),
           static_cast<int>(ErrorCode::kUnsupported));

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(save_rejects_unknown_extension) {
  const Image source = ft_test::make_test_image(4, 4);
  auto status = save_image(source, "out.xyz");
  CHECK(!status.has_value());
  CHECK_EQ(static_cast<int>(status.error().code), static_cast<int>(ErrorCode::kUnsupported));
}
