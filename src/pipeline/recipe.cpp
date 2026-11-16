// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/pipeline/recipe.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include "figtools/pipeline/json.hpp"

namespace figtools {
namespace {

Result<ParamValue> to_param_value(const Json& value, const std::string& key) {
  switch (value.type()) {
    case Json::Type::kBool:
      return Result<ParamValue>::ok(ParamValue{value.as_bool()});
    case Json::Type::kNumber:
      return Result<ParamValue>::ok(ParamValue{value.as_number()});
    case Json::Type::kString:
      return Result<ParamValue>::ok(ParamValue{value.as_string()});
    case Json::Type::kNull:
      return Result<ParamValue>::fail(ErrorCode::kParseError,
                                      "recipe: parameter '" + key + "' is null");
    case Json::Type::kArray:
    case Json::Type::kObject:
    default:
      return Result<ParamValue>::fail(ErrorCode::kParseError,
                                      "recipe: parameter '" + key +
                                          "' must be a number, boolean or string");
  }
}

Json param_to_json(const ParamValue& value) {
  if (const auto* number = std::get_if<double>(&value)) {
    return Json(*number);
  }
  if (const auto* flag = std::get_if<bool>(&value)) {
    return Json(*flag);
  }
  return Json(std::get<std::string>(value));
}

}  // namespace

Result<Recipe> Recipe::from_json(std::string_view text) {
  auto parsed = Json::parse(text);
  if (!parsed) {
    return Result<Recipe>::fail(parsed.error().code, parsed.error().message);
  }
  const Json& root = parsed.value();
  if (!root.is_object()) {
    return Result<Recipe>::fail(ErrorCode::kParseError, "recipe: root must be a JSON object");
  }

  Recipe recipe;
  recipe.version = static_cast<int>(root["version"].as_number(1.0));
  const double seed = root["seed"].as_number(0.0);
  recipe.seed = seed < 0.0 ? 0U : static_cast<std::uint64_t>(seed);

  const Json& steps = root["steps"];
  if (!steps.is_array()) {
    return Result<Recipe>::fail(ErrorCode::kParseError, "recipe: 'steps' must be an array");
  }
  if (steps.size() == 0) {
    return Result<Recipe>::fail(ErrorCode::kParseError, "recipe: 'steps' is empty");
  }

  for (std::size_t i = 0; i < steps.size(); ++i) {
    const Json& node = steps.at(i);
    if (!node.is_object()) {
      return Result<Recipe>::fail(ErrorCode::kParseError,
                                  "recipe: step " + std::to_string(i) + " must be an object");
    }
    Step step;
    step.op = node["op"].as_string();
    if (step.op.empty()) {
      return Result<Recipe>::fail(ErrorCode::kParseError,
                                  "recipe: step " + std::to_string(i) + " has no 'op'");
    }
    step.enabled = node["enabled"].as_bool(true);

    const Json& params = node["params"];
    if (params.is_object()) {
      for (const auto& [key, value] : params.members()) {
        auto converted = to_param_value(value, key);
        if (!converted) {
          return Result<Recipe>::fail(converted.error().code,
                                      "recipe: step " + std::to_string(i) + ": " +
                                          converted.error().message);
        }
        step.params.set(key, std::move(converted.value()));
      }
    } else if (!params.is_null()) {
      return Result<Recipe>::fail(ErrorCode::kParseError,
                                  "recipe: step " + std::to_string(i) + ": 'params' must be an object");
    }
    recipe.steps.push_back(std::move(step));
  }

  return Result<Recipe>::ok(std::move(recipe));
}

std::string Recipe::to_json(int indent) const {
  Json root = Json::object();
  root.set("version", Json(static_cast<double>(version)));
  root.set("seed", Json(static_cast<double>(seed)));

  Json steps_json = Json::array();
  for (const Step& step : steps) {
    Json node = Json::object();
    node.set("op", Json(step.op));
    node.set("enabled", Json(step.enabled));
    Json params = Json::object();
    for (const auto& [key, value] : step.params.values()) {
      params.set(key, param_to_json(value));
    }
    node.set("params", std::move(params));
    steps_json.push_back(std::move(node));
  }
  root.set("steps", std::move(steps_json));
  return root.dump(indent);
}

std::size_t Recipe::enabled_steps() const {
  std::size_t count = 0;
  for (const Step& step : steps) {
    if (step.enabled) {
      ++count;
    }
  }
  return count;
}

}  // namespace figtools
