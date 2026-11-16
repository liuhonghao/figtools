// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// A recipe is the persisted form of a pipeline: an ordered list of operator
// steps plus a seed. It is the unit of reproducibility -- the same recipe over
// the same input must produce byte-identical output.

#ifndef FIGTOOLS_PIPELINE_RECIPE_HPP_
#define FIGTOOLS_PIPELINE_RECIPE_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "figtools/core/result.hpp"
#include "figtools/ops/operator.hpp"

namespace figtools {

struct Step {
  std::string op;
  bool enabled = true;
  Params params;
};

struct Recipe {
  int version = 1;
  std::uint64_t seed = 0;
  std::vector<Step> steps;

  [[nodiscard]] static Result<Recipe> from_json(std::string_view text);
  [[nodiscard]] std::string to_json(int indent = 2) const;
  [[nodiscard]] std::size_t enabled_steps() const;
};

}  // namespace figtools

#endif  // FIGTOOLS_PIPELINE_RECIPE_HPP_
