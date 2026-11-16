// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Minimal JSON reader/writer used for recipe files.
//
// Why not a third-party library: recipes are the reproducibility contract, so
// their parser is kept small, auditable and dependency free. Object member
// order is preserved, which makes `dump()` stable and diff-friendly.

#ifndef FIGTOOLS_PIPELINE_JSON_HPP_
#define FIGTOOLS_PIPELINE_JSON_HPP_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "figtools/core/result.hpp"

namespace figtools {

class Json {
 public:
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Json() = default;
  Json(bool value);         // NOLINT(google-explicit-constructor)
  Json(double value);       // NOLINT(google-explicit-constructor)
  Json(std::string value);  // NOLINT(google-explicit-constructor)

  [[nodiscard]] static Json array();
  [[nodiscard]] static Json object();

  [[nodiscard]] static Result<Json> parse(std::string_view text);

  [[nodiscard]] Type type() const { return type_; }
  [[nodiscard]] bool is_null() const { return type_ == Type::kNull; }
  [[nodiscard]] bool is_bool() const { return type_ == Type::kBool; }
  [[nodiscard]] bool is_number() const { return type_ == Type::kNumber; }
  [[nodiscard]] bool is_string() const { return type_ == Type::kString; }
  [[nodiscard]] bool is_array() const { return type_ == Type::kArray; }
  [[nodiscard]] bool is_object() const { return type_ == Type::kObject; }

  [[nodiscard]] bool as_bool(bool fallback = false) const;
  [[nodiscard]] double as_number(double fallback = 0.0) const;
  [[nodiscard]] const std::string& as_string() const { return string_; }

  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] const Json& at(std::size_t index) const;
  [[nodiscard]] bool contains(std::string_view key) const;
  [[nodiscard]] const Json* find(std::string_view key) const;
  [[nodiscard]] const Json& operator[](std::string_view key) const;

  [[nodiscard]] const std::vector<std::pair<std::string, Json>>& members() const { return object_; }

  void push_back(Json value);
  void set(std::string key, Json value);

  // Compact when indent == 0, pretty printed otherwise. Numbers use the
  // shortest representation that round-trips.
  [[nodiscard]] std::string dump(int indent = 0) const;

 private:
  void dump_to(std::string& out, int indent, int depth) const;

  Type type_ = Type::kNull;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::vector<std::pair<std::string, Json>> object_;
};

}  // namespace figtools

#endif  // FIGTOOLS_PIPELINE_JSON_HPP_
