// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/core/hash.hpp"

#include <array>
#include <cstdio>

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

}  // namespace figtools
