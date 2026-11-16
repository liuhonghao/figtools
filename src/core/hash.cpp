// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/hash.hpp"

#include <array>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace figtools {

std::uint64_t fnv1a(const void* data, std::size_t bytes, std::uint64_t seed) {
  const auto* p = static_cast<const std::uint8_t*>(data);
  std::uint64_t state = seed;
  for (std::size_t i = 0; i < bytes; ++i) {
    state ^= static_cast<std::uint64_t>(p[i]);
    state *= kFnvPrime;
  }
  return state;
}

std::uint64_t hash_image(const ImageView& image) {
  const int width = image.width();
  const int height = image.height();
  std::uint64_t state = fnv1a(&width, sizeof(width));
  state = fnv1a(&height, sizeof(height), state);
  const auto row_bytes = static_cast<std::size_t>(image.width()) * kChannels;
  for (int y = 0; y < image.height(); ++y) {
    state = fnv1a(image.row(y), row_bytes, state);
  }
  return state;
}

std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value) {
  return fnv1a(&value, sizeof(value), seed);
}

std::string to_hex(std::uint64_t value) {
  std::array<char, 17> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%016llx",
                static_cast<unsigned long long>(value));  // NOLINT(runtime/int)
  return std::string(buffer.data());
}

std::uint64_t perceptual_hash(const ImageView& image) {
  // 32x32 DCT via the separable cosine matrix; keep the top-left 8x8 block.
  constexpr int kSize = 32;
  constexpr int kLow = 8;
  if (image.empty()) {
    return 0;
  }

  double grey[kSize][kSize] = {};
  const double sx = static_cast<double>(image.width()) / static_cast<double>(kSize);
  const double sy = static_cast<double>(image.height()) / static_cast<double>(kSize);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const int px = std::min(image.width() - 1, static_cast<int>(x * sx));
      const int py = std::min(image.height() - 1, static_cast<int>(y * sy));
      const Rgba c = image.at(px, py);
      grey[y][x] = 0.299 * static_cast<double>(c.r) + 0.587 * static_cast<double>(c.g) +
                   0.114 * static_cast<double>(c.b);
    }
  }

  double cosine[kSize][kLow];
  for (int u = 0; u < kLow; ++u) {
    for (int x = 0; x < kSize; ++x) {
      cosine[x][u] = std::cos(3.14159265358979323846 * static_cast<double>(u) *
                              (static_cast<double>(x) + 0.5) / static_cast<double>(kSize));
    }
  }

  double temp[kSize][kLow] = {};
  for (int y = 0; y < kSize; ++y) {
    for (int u = 0; u < kLow; ++u) {
      double sum = 0.0;
      for (int x = 0; x < kSize; ++x) {
        sum += grey[y][x] * cosine[x][u];
      }
      temp[y][u] = sum;
    }
  }

  double low[kLow][kLow] = {};
  double sum = 0.0;
  for (int v = 0; v < kLow; ++v) {
    for (int u = 0; u < kLow; ++u) {
      double value = 0.0;
      for (int y = 0; y < kSize; ++y) {
        value += temp[y][u] * cosine[y][v];
      }
      low[v][u] = value;
      if (u > 0 || v > 0) {
        sum += value;
      }
    }
  }
  const double mean = sum / static_cast<double>(kLow * kLow - 1);

  std::uint64_t hash = 0;
  int bit = 0;
  for (int v = 0; v < kLow; ++v) {
    for (int u = 0; u < kLow; ++u) {
      if (u == 0 && v == 0) {
        continue;
      }
      if (low[v][u] > mean) {
        hash |= (1ULL << bit);
      }
      ++bit;
    }
  }
  return hash;
}

int hamming_distance(std::uint64_t a, std::uint64_t b) noexcept {
  std::uint64_t x = a ^ b;
  int count = 0;
  while (x != 0) {
    x &= x - 1;
    ++count;
  }
  return count;
}

}  // namespace figtools
