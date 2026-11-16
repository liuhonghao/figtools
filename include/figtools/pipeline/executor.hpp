// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#ifndef FIGTOOLS_PIPELINE_EXECUTOR_HPP_
#define FIGTOOLS_PIPELINE_EXECUTOR_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "figtools/core/image.hpp"
#include "figtools/core/result.hpp"
#include "figtools/ops/registry.hpp"
#include "figtools/pipeline/recipe.hpp"

namespace figtools {

struct StepTrace {
  std::string op;
  double milliseconds = 0.0;
  Size output_size;
};

struct RunResult {
  Image image;
  std::vector<StepTrace> trace;
  std::uint64_t cache_key = 0;
  double total_milliseconds = 0.0;
};

// Validates every step (operator exists, parameters normalise) without
// touching pixels. Call this before a long run so errors surface early.
[[nodiscard]] Status validate_recipe(const Recipe& recipe, const Registry& registry);

// Deterministic key: input pixels + operator ids/versions + canonical
// parameters + seed. Two runs with the same key must yield the same bytes.
[[nodiscard]] Result<std::uint64_t> compute_cache_key(const Recipe& recipe, const ImageView& input,
                                                      const Registry& registry);

[[nodiscard]] Result<RunResult> execute(const Recipe& recipe, const ImageView& input,
                                        const Registry& registry);

}  // namespace figtools

#endif  // FIGTOOLS_PIPELINE_EXECUTOR_HPP_
