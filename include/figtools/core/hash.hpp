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

[[nodiscard]] std::string to_hex(std::uint64_t value);

}  // namespace figtools

#endif  // FIGTOOLS_CORE_HASH_HPP_
