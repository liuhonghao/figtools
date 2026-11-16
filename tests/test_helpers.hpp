// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#ifndef FIGTOOLS_TESTS_TEST_HELPERS_HPP_
#define FIGTOOLS_TESTS_TEST_HELPERS_HPP_

#include <cstdint>
#include <filesystem>
#include <string>

#include "figtools/core/image.hpp"

namespace ft_test {

inline figtools::Image make_test_image(int width, int height) {
  figtools::Image image(width, height);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto r = static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
      const auto g = static_cast<std::uint8_t>((x * 2 + y * 11) % 256);
      const auto b = static_cast<std::uint8_t>((x * 5 + y * 5) % 256);
      image.set(x, y, figtools::Rgba{r, g, b, 255});
    }
  }
  return image;
}

inline std::filesystem::path temp_path(const std::string& name) {
  const auto path = std::filesystem::temp_directory_path() / ("figtools_" + name);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  return path;
}

}  // namespace ft_test

#endif  // FIGTOOLS_TESTS_TEST_HELPERS_HPP_
