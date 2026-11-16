// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/pipeline/executor.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include "figtools/core/hash.hpp"

namespace figtools {
namespace {

std::uint64_t hash_string(std::uint64_t seed, const std::string& text) {
  return fnv1a(text.data(), text.size(), seed);
}

std::uint64_t hash_param(std::uint64_t seed, const std::string& key, const ParamValue& value) {
  std::uint64_t state = hash_string(seed, key);
  if (const auto* number = std::get_if<double>(&value)) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(*number));
    std::memcpy(&bits, number, sizeof(bits));
    state = hash_combine(state, 1U);
    return hash_combine(state, bits);
  }
  if (const auto* flag = std::get_if<bool>(&value)) {
    state = hash_combine(state, 2U);
    return hash_combine(state, *flag ? 1U : 0U);
  }
  state = hash_combine(state, 3U);
  return hash_string(state, std::get<std::string>(value));
}

Image copy_image(const ImageView& source) {
  Image copy(source.width(), source.height());
  MutableImageView target = copy.mutable_view();
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      target.set(x, y, source.at(x, y));
    }
  }
  return copy;
}

}  // namespace

Status validate_recipe(const Recipe& recipe, const Registry& registry) {
  if (recipe.steps.empty()) {
    return Status::fail(ErrorCode::kInvalidArgument, "recipe has no steps");
  }
  for (std::size_t i = 0; i < recipe.steps.size(); ++i) {
    const Step& step = recipe.steps[i];
    if (!step.enabled) {
      continue;
    }
    const Operator* op = registry.find(step.op);
    if (op == nullptr) {
      return Status::fail(ErrorCode::kInvalidArgument,
                          "step " + std::to_string(i) + ": unknown operator '" + step.op + "'");
    }
    auto normalized = registry.normalize(op->spec, step.params);
    if (!normalized) {
      return Status::fail(normalized.error().code,
                          "step " + std::to_string(i) + ": " + normalized.error().message);
    }
  }
  return Status::ok();
}

Result<std::uint64_t> compute_cache_key(const Recipe& recipe, const ImageView& input,
                                        const Registry& registry) {
  if (input.empty()) {
    return Result<std::uint64_t>::fail(ErrorCode::kInvalidArgument, "cache key: empty input");
  }

  std::uint64_t state = hash_image(input);
  state = hash_combine(state, recipe.seed);
  state = hash_combine(state, static_cast<std::uint64_t>(recipe.version));

  for (const Step& step : recipe.steps) {
    if (!step.enabled) {
      continue;
    }
    const Operator* op = registry.find(step.op);
    if (op == nullptr) {
      return Result<std::uint64_t>::fail(ErrorCode::kInvalidArgument,
                                         "cache key: unknown operator '" + step.op + "'");
    }
    auto normalized = registry.normalize(op->spec, step.params);
    if (!normalized) {
      return Result<std::uint64_t>::fail(normalized.error().code, normalized.error().message);
    }
    state = hash_string(state, step.op);
    state = hash_combine(state, static_cast<std::uint64_t>(op->spec.version));
    for (const auto& [key, value] : normalized.value().values()) {
      state = hash_param(state, key, value);
    }
  }
  return Result<std::uint64_t>::ok(state);
}

Result<RunResult> execute(const Recipe& recipe, const ImageView& input, const Registry& registry) {
  if (input.empty()) {
    return Result<RunResult>::fail(ErrorCode::kInvalidArgument, "execute: empty input image");
  }
  auto validation = validate_recipe(recipe, registry);
  if (!validation) {
    return Result<RunResult>::fail(validation.error().code, validation.error().message);
  }
  auto key = compute_cache_key(recipe, input, registry);
  if (!key) {
    return Result<RunResult>::fail(key.error().code, key.error().message);
  }

  RunResult result;
  result.cache_key = key.value();
  result.image = copy_image(input);

  const auto start = std::chrono::steady_clock::now();
  for (const Step& step : recipe.steps) {
    if (!step.enabled) {
      continue;
    }
    const Operator* op = registry.find(step.op);
    if (op == nullptr) {
      return Result<RunResult>::fail(ErrorCode::kInternal,
                                     "operator disappeared during execution: " + step.op);
    }
    auto normalized = registry.normalize(op->spec, step.params);
    if (!normalized) {
      return Result<RunResult>::fail(normalized.error().code, normalized.error().message);
    }

    const auto step_start = std::chrono::steady_clock::now();
    auto output = op->fn(result.image.view(), normalized.value());
    if (!output) {
      return Result<RunResult>::fail(output.error().code,
                                     "step '" + step.op + "': " + output.error().message);
    }
    const auto step_end = std::chrono::steady_clock::now();
    result.trace.push_back(StepTrace{
        step.op,
        std::chrono::duration<double, std::milli>(step_end - step_start).count(),
        output.value().size(),
    });
    result.image = std::move(output.value());
  }
  const auto end = std::chrono::steady_clock::now();
  result.total_milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
  return Result<RunResult>::ok(std::move(result));
}

}  // namespace figtools
