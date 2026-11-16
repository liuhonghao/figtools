// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Deterministic hashing. Used for content-addressed storage and for the
// pipeline cache key (input hash + operator version + canonical parameters +
// seed), which is what makes "run it twice, get the same bytes" checkable.

#ifndef FIGTOOLS_CORE_HASH_HPP_
#define FIGTOOLS_CORE_HASH_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "figtools/core/image.hpp"

namespace figtools {

constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

[[nodiscard]] std::uint64_t fnv1a(const void* data, std::size_t bytes,
                                  std::uint64_t seed = kFnvOffsetBasis);

[[nodiscard]] std::uint64_t hash_image(const ImageView& image);

[[nodiscard]] std::uint64_t hash_combine(std::uint64_t seed, std::uint64_t value);

// Perceptual hash: 64 bits describing coarse luminance structure. Two images
// that differ only by resize / JPEG quality / small colour shifts have a small
// Hamming distance, which is how near-duplicate leakage is detected.
[[nodiscard]] std::uint64_t perceptual_hash(const ImageView& image);
[[nodiscard]] int hamming_distance(std::uint64_t a, std::uint64_t b) noexcept;

[[nodiscard]] std::string to_hex(std::uint64_t value);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_HASH_HPP_
