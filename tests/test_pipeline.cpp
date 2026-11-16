// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <string>

#include "figtools/core/hash.hpp"
#include "figtools/ops/registry.hpp"
#include "figtools/pipeline/executor.hpp"
#include "figtools/pipeline/recipe.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace figtools;

namespace {

const char* kSampleRecipe = R"({
  "version": 1,
  "seed": 2026,
  "steps": [
    {"op": "grayscale", "enabled": true, "params": {}},
    {"op": "contrast", "enabled": true, "params": {"factor": 1.2}},
    {"op": "gamma", "enabled": false, "params": {"gamma": 2.0}}
  ]
})";

}  // namespace

TEST(recipe_parses_steps_and_parameters) {
  auto recipe = Recipe::from_json(kSampleRecipe);
  CHECK(recipe.has_value());
  if (!recipe) {
    return;
  }
  CHECK_EQ(static_cast<int>(recipe.value().version), 1);
  CHECK_EQ(recipe.value().steps.size(), static_cast<std::size_t>(3));
  CHECK_EQ(recipe.value().enabled_steps(), static_cast<std::size_t>(2));
  CHECK_EQ(recipe.value().steps[0].op, std::string("grayscale"));
  CHECK_NEAR(recipe.value().steps[1].params.number("factor", 0.0), 1.2, 1e-9);
  CHECK(!recipe.value().steps[2].enabled);
}

TEST(recipe_json_round_trip_is_stable) {
  auto first = Recipe::from_json(kSampleRecipe);
  CHECK(first.has_value());
  if (!first) {
    return;
  }
  const std::string dumped = first.value().to_json(0);
  auto second = Recipe::from_json(dumped);
  CHECK(second.has_value());
  if (!second) {
    return;
  }
  CHECK_EQ(second.value().to_json(0), dumped);
  CHECK_EQ(second.value().steps.size(), first.value().steps.size());

  const std::string again = second.value().to_json(0);
  CHECK_EQ(dumped, again);
}

TEST(recipe_rejects_malformed_json) {
  CHECK(!Recipe::from_json("{").has_value());
  CHECK(!Recipe::from_json("{\"steps\": []}").has_value());
  CHECK(!Recipe::from_json("{\"steps\": [{\"params\": {}}]}").has_value());
  CHECK(!Recipe::from_json("{\"steps\": \"nope\"}").has_value());
}

TEST(validate_rejects_unknown_operator) {
  auto recipe = Recipe::from_json(R"({"steps":[{"op":"no_such_op","params":{}}]})");
  CHECK(recipe.has_value());
  if (!recipe) {
    return;
  }
  auto status = validate_recipe(recipe.value(), Registry::instance());
  CHECK(!status.has_value());
}

TEST(validate_rejects_invalid_parameter) {
  auto recipe = Recipe::from_json(R"({"steps":[{"op":"gamma","params":{"gamma":42}}]})");
  CHECK(recipe.has_value());
  if (!recipe) {
    return;
  }
  auto status = validate_recipe(recipe.value(), Registry::instance());
  CHECK(!status.has_value());
}

TEST(execution_is_deterministic) {
  auto recipe = Recipe::from_json(kSampleRecipe);
  CHECK(recipe.has_value());
  if (!recipe) {
    return;
  }
  const Image source = ft_test::make_test_image(48, 32);

  auto first = execute(recipe.value(), source.view(), Registry::instance());
  auto second = execute(recipe.value(), source.view(), Registry::instance());
  CHECK(first.has_value());
  CHECK(second.has_value());
  if (!first || !second) {
    return;
  }
  CHECK_EQ(first.value().cache_key, second.value().cache_key);
  CHECK_EQ(to_hex(hash_image(first.value().image.view())),
           to_hex(hash_image(second.value().image.view())));
}

TEST(cache_key_depends_on_parameters) {
  auto weak = Recipe::from_json(R"({"steps":[{"op":"contrast","params":{"factor":1.1}}]})");
  auto strong = Recipe::from_json(R"({"steps":[{"op":"contrast","params":{"factor":1.4}}]})");
  CHECK(weak.has_value());
  CHECK(strong.has_value());
  if (!weak || !strong) {
    return;
  }
  const Image source = ft_test::make_test_image(16, 16);
  auto key_a = compute_cache_key(weak.value(), source.view(), Registry::instance());
  auto key_b = compute_cache_key(strong.value(), source.view(), Registry::instance());
  CHECK(key_a.has_value());
  CHECK(key_b.has_value());
  if (key_a && key_b) {
    CHECK(key_a.value() != key_b.value());
  }
}

TEST(disabled_steps_are_skipped) {
  auto only_off = Recipe::from_json(
      R"({"steps":[{"op":"gamma","enabled":false,"params":{"gamma":3.0}}]})");
  CHECK(only_off.has_value());
  if (!only_off) {
    return;
  }
  const Image source = ft_test::make_test_image(12, 12);
  auto result = execute(only_off.value(), source.view(), Registry::instance());
  CHECK(result.has_value());
  if (result) {
    CHECK_EQ(result.value().trace.size(), static_cast<std::size_t>(0));
    CHECK_EQ(to_hex(hash_image(result.value().image.view())),
             to_hex(hash_image(source.view())));
  }
}

TEST(execute_reports_per_step_trace) {
  auto recipe = Recipe::from_json(kSampleRecipe);
  CHECK(recipe.has_value());
  if (!recipe) {
    return;
  }
  const Image source = ft_test::make_test_image(24, 24);
  auto result = execute(recipe.value(), source.view(), Registry::instance());
  CHECK(result.has_value());
  if (!result) {
    return;
  }
  CHECK_EQ(result.value().trace.size(), static_cast<std::size_t>(2));
  CHECK_EQ(result.value().trace[0].op, std::string("grayscale"));
  CHECK(result.value().total_milliseconds >= 0.0);
}
