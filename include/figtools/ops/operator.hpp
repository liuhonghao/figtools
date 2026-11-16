// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Operator model.
//
// An operator is a pure function `Image -> Image` plus a declarative schema.
// The schema is the single source of truth for: CLI help, GUI controls,
// parameter validation and the canonical form used to hash a pipeline. If a
// parameter is not in the schema, it does not exist.

#ifndef FIGTOOLS_OPS_OPERATOR_HPP_
#define FIGTOOLS_OPS_OPERATOR_HPP_

#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "figtools/core/image.hpp"
#include "figtools/core/result.hpp"

namespace figtools {

enum class ParamType {
  kNumber = 0,
  kInteger,
  kBool,
  kEnum,
};

struct ParamSpec {
  std::string key;
  std::string label;
  std::string doc;
  std::string unit;
  ParamType type = ParamType::kNumber;
  double min_value = 0.0;
  double max_value = 1.0;
  double default_value = 0.0;
  std::vector<std::string> enum_values;
  bool advanced = false;
};

using ParamValue = std::variant<double, bool, std::string>;

// A bag of parameters, validated against an operator schema before execution.
class Params {
 public:
  Params() = default;

  [[nodiscard]] bool has(const std::string& key) const;
  [[nodiscard]] const std::map<std::string, ParamValue>& values() const { return values_; }
  [[nodiscard]] std::size_t size() const { return values_.size(); }

  void set(const std::string& key, ParamValue value);

  [[nodiscard]] double number(const std::string& key, double fallback) const;
  [[nodiscard]] int integer(const std::string& key, int fallback) const;
  [[nodiscard]] bool boolean(const std::string& key, bool fallback) const;
  [[nodiscard]] std::string text(const std::string& key, const std::string& fallback) const;

 private:
  std::map<std::string, ParamValue> values_;
};

struct OperatorSpec {
  std::string id;       // machine name, e.g. "resize"
  std::string label;    // human label, e.g. "缩放"
  std::string group;    // "geometry" | "color" | "filter" | "analysis"
  std::string summary;  // one sentence, shown in help and GUI tooltips
  int version = 1;      // bump when output changes; participates in the cache key
  bool geometry = false;  // true when the operator may change dimensions
  std::vector<ParamSpec> params;
};

using OperatorFn = std::function<Result<Image>(const ImageView&, const Params&)>;

struct Operator {
  OperatorSpec spec;
  OperatorFn fn;
};

}  // namespace figtools

#endif  // FIGTOOLS_OPS_OPERATOR_HPP_
