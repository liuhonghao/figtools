// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/ops/registry.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

#include "ops/register.hpp"

namespace figtools {
namespace {

ParamValue default_value_of(const ParamSpec& spec) {
  switch (spec.type) {
    case ParamType::kBool:
      return ParamValue{spec.default_value != 0.0};
    case ParamType::kEnum: {
      const auto index = static_cast<std::size_t>(std::max(0.0, spec.default_value));
      if (index < spec.enum_values.size()) {
        return ParamValue{spec.enum_values[index]};
      }
      return ParamValue{std::string{}};
    }
    case ParamType::kInteger:
    case ParamType::kNumber:
    default:
      return ParamValue{spec.default_value};
  }
}

bool is_integral(double value) { return std::floor(value) == value; }

}  // namespace

Registry& Registry::instance_unchecked() {
  static Registry registry;
  return registry;
}

Registry& Registry::instance() {
  // Registration happens exactly once, on first use. Doing it here (instead of
  // in static initialisers) means the linker cannot drop an operator object
  // file, and ordering is deterministic.
  static const bool initialised = []() {
    detail::register_geometry_ops(Registry::instance_unchecked());
    detail::register_color_ops(Registry::instance_unchecked());
    detail::register_filter_ops(Registry::instance_unchecked());
    return true;
  }();
  (void)initialised;
  return instance_unchecked();
}

void Registry::add(Operator op) {
  assert(!op.spec.id.empty());
  assert(operators_.find(op.spec.id) == operators_.end() && "duplicate operator id");
  operators_.emplace(op.spec.id, std::move(op));
}

const Operator* Registry::find(std::string_view id) const {
  const auto it = operators_.find(std::string(id));
  return it == operators_.end() ? nullptr : &it->second;
}

bool Registry::contains(std::string_view id) const { return find(id) != nullptr; }

std::vector<const Operator*> Registry::all() const {
  std::vector<const Operator*> result;
  result.reserve(operators_.size());
  for (const auto& [id, op] : operators_) {
    result.push_back(&op);
  }
  std::sort(result.begin(), result.end(), [](const Operator* lhs, const Operator* rhs) {
    if (lhs->spec.group != rhs->spec.group) {
      return lhs->spec.group < rhs->spec.group;
    }
    return lhs->spec.id < rhs->spec.id;
  });
  return result;
}

std::size_t Registry::size() const { return operators_.size(); }

Result<Params> Registry::normalize(const OperatorSpec& spec, const Params& raw) const {
  for (const auto& [key, value] : raw.values()) {
    const bool declared = std::any_of(spec.params.begin(), spec.params.end(),
                                      [&key](const ParamSpec& p) { return p.key == key; });
    if (!declared) {
      return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                  "operator '" + spec.id + "': unknown parameter '" + key + "'");
    }
  }

  Params normalized;
  for (const ParamSpec& param : spec.params) {
    if (!raw.has(param.key)) {
      normalized.set(param.key, default_value_of(param));
      continue;
    }

    const ParamValue& value = raw.values().at(param.key);
    switch (param.type) {
      case ParamType::kNumber: {
        if (!std::holds_alternative<double>(value)) {
          return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                      "operator '" + spec.id + "': parameter '" + param.key +
                                          "' must be a number");
        }
        const double number = std::get<double>(value);
        if (number < param.min_value || number > param.max_value) {
          return Result<Params>::fail(
              ErrorCode::kOutOfRange,
              "operator '" + spec.id + "': parameter '" + param.key + "' out of range [" +
                  std::to_string(param.min_value) + ", " + std::to_string(param.max_value) +
                  "], got " + std::to_string(number));
        }
        normalized.set(param.key, number);
        break;
      }
      case ParamType::kInteger: {
        if (!std::holds_alternative<double>(value)) {
          return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                      "operator '" + spec.id + "': parameter '" + param.key +
                                          "' must be an integer");
        }
        const double number = std::get<double>(value);
        if (!is_integral(number)) {
          return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                      "operator '" + spec.id + "': parameter '" + param.key +
                                          "' must be an integer, got " + std::to_string(number));
        }
        if (number < param.min_value || number > param.max_value) {
          return Result<Params>::fail(
              ErrorCode::kOutOfRange,
              "operator '" + spec.id + "': parameter '" + param.key + "' out of range [" +
                  std::to_string(param.min_value) + ", " + std::to_string(param.max_value) +
                  "], got " + std::to_string(number));
        }
        normalized.set(param.key, number);
        break;
      }
      case ParamType::kBool: {
        if (!std::holds_alternative<bool>(value)) {
          return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                      "operator '" + spec.id + "': parameter '" + param.key +
                                          "' must be a boolean");
        }
        normalized.set(param.key, std::get<bool>(value));
        break;
      }
      case ParamType::kEnum: {
        if (!std::holds_alternative<std::string>(value)) {
          return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                      "operator '" + spec.id + "': parameter '" + param.key +
                                          "' must be a string");
        }
        const std::string& text = std::get<std::string>(value);
        const bool allowed =
            std::find(param.enum_values.begin(), param.enum_values.end(), text) !=
            param.enum_values.end();
        if (!allowed) {
          return Result<Params>::fail(ErrorCode::kInvalidArgument,
                                      "operator '" + spec.id + "': parameter '" + param.key +
                                          "' must be one of the declared values, got '" + text + "'");
        }
        normalized.set(param.key, text);
        break;
      }
      default:
        return Result<Params>::fail(ErrorCode::kInternal, "unhandled parameter type");
    }
  }
  return Result<Params>::ok(std::move(normalized));
}


// ---------------------------------------------------------------------------
// Params
// ---------------------------------------------------------------------------

bool Params::has(const std::string& key) const { return values_.find(key) != values_.end(); }

void Params::set(const std::string& key, ParamValue value) { values_[key] = std::move(value); }

double Params::number(const std::string& key, double fallback) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return fallback;
  }
  const auto* value = std::get_if<double>(&it->second);
  return value == nullptr ? fallback : *value;
}

int Params::integer(const std::string& key, int fallback) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return fallback;
  }
  const auto* value = std::get_if<double>(&it->second);
  return value == nullptr ? fallback : static_cast<int>(std::lround(*value));
}

bool Params::boolean(const std::string& key, bool fallback) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return fallback;
  }
  const auto* value = std::get_if<bool>(&it->second);
  return value == nullptr ? fallback : *value;
}

std::string Params::text(const std::string& key, const std::string& fallback) const {
  const auto it = values_.find(key);
  if (it == values_.end()) {
    return fallback;
  }
  const auto* value = std::get_if<std::string>(&it->second);
  return value == nullptr ? fallback : *value;
}

}  // namespace figtools
