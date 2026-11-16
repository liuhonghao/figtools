// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/json/json.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace figtools {
namespace {

constexpr int kMaxDepth = 64;

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  Result<Json> parse() {
    skip_whitespace();
    auto value = parse_value(0);
    if (!value) {
      return value;
    }
    skip_whitespace();
    if (pos_ != text_.size()) {
      return Result<Json>::fail(ErrorCode::kParseError,
                                "json: trailing characters at offset " + std::to_string(pos_));
    }
    return value;
  }

 private:
  void skip_whitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool consume(char expected) {
    skip_whitespace();
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  Result<Json> parse_value(int depth) {
    if (depth > kMaxDepth) {
      return Result<Json>::fail(ErrorCode::kParseError, "json: nesting too deep");
    }
    skip_whitespace();
    if (pos_ >= text_.size()) {
      return Result<Json>::fail(ErrorCode::kParseError, "json: unexpected end of input");
    }
    const char c = text_[pos_];
    switch (c) {
      case '{':
        return parse_object(depth);
      case '[':
        return parse_array(depth);
      case '"':
        return parse_string();
      case 't':
        return parse_literal("true", Json(true));
      case 'f':
        return parse_literal("false", Json(false));
      case 'n':
        return parse_literal("null", Json());
      default:
        if (c == '-' || (c >= '0' && c <= '9')) {
          return parse_number();
        }
        return Result<Json>::fail(ErrorCode::kParseError,
                                  "json: unexpected character at offset " + std::to_string(pos_));
    }
  }

  Result<Json> parse_literal(std::string_view literal, Json value) {
    if (text_.substr(pos_, literal.size()) != literal) {
      return Result<Json>::fail(ErrorCode::kParseError, "json: invalid literal");
    }
    pos_ += literal.size();
    return Result<Json>::ok(std::move(value));
  }

  Result<Json> parse_number() {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < text_.size() &&
           ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' || text_[pos_] == 'e' ||
            text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-')) {
      ++pos_;
    }
    double value = 0.0;
    const auto* first = text_.data() + start;
    const auto* last = text_.data() + pos_;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{}) {
      return Result<Json>::fail(ErrorCode::kParseError, "json: invalid number");
    }
    return Result<Json>::ok(Json(value));
  }

  static void append_utf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
      out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
  }

  bool parse_hex4(std::uint32_t& value) {
    if (pos_ + 4 > text_.size()) {
      return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_ + static_cast<std::size_t>(i)];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return false;
      }
    }
    pos_ += 4;
    return true;
  }

  Result<Json> parse_string() {
    if (!consume('"')) {
      return Result<Json>::fail(ErrorCode::kParseError, "json: expected string");
    }
    std::string out;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return Result<Json>::ok(Json(std::move(out)));
      }
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= text_.size()) {
        break;
      }
      const char escape = text_[pos_++];
      switch (escape) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t code_point = 0;
          if (!parse_hex4(code_point)) {
            return Result<Json>::fail(ErrorCode::kParseError, "json: invalid \\u escape");
          }
          if (code_point >= 0xD800 && code_point <= 0xDBFF && pos_ + 6 <= text_.size() &&
              text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
            pos_ += 2;
            std::uint32_t low = 0;
            if (!parse_hex4(low)) {
              return Result<Json>::fail(ErrorCode::kParseError, "json: invalid surrogate pair");
            }
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
          }
          append_utf8(out, code_point);
          break;
        }
        default:
          return Result<Json>::fail(ErrorCode::kParseError, "json: invalid escape sequence");
      }
    }
    return Result<Json>::fail(ErrorCode::kParseError, "json: unterminated string");
  }

  Result<Json> parse_array(int depth) {
    consume('[');
    Json array = Json::array();
    skip_whitespace();
    if (consume(']')) {
      return Result<Json>::ok(std::move(array));
    }
    while (true) {
      auto value = parse_value(depth + 1);
      if (!value) {
        return value;
      }
      array.push_back(std::move(value.value()));
      if (consume(',')) {
        continue;
      }
      if (consume(']')) {
        return Result<Json>::ok(std::move(array));
      }
      return Result<Json>::fail(ErrorCode::kParseError, "json: expected ',' or ']'");
    }
  }

  Result<Json> parse_object(int depth) {
    consume('{');
    Json object = Json::object();
    skip_whitespace();
    if (consume('}')) {
      return Result<Json>::ok(std::move(object));
    }
    while (true) {
      auto key = parse_string();
      if (!key) {
        return key;
      }
      if (!consume(':')) {
        return Result<Json>::fail(ErrorCode::kParseError, "json: expected ':'");
      }
      auto value = parse_value(depth + 1);
      if (!value) {
        return value;
      }
      object.set(key.value().as_string(), std::move(value.value()));
      if (consume(',')) {
        continue;
      }
      if (consume('}')) {
        return Result<Json>::ok(std::move(object));
      }
      return Result<Json>::fail(ErrorCode::kParseError, "json: expected ',' or '}'");
    }
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

void escape_to(std::string& out, const std::string& value) {
  out.push_back('"');
  for (const char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  out.push_back('"');
}

void number_to(std::string& out, double value) {
  std::array<char, 32> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%.10g", value);
  out += buffer.data();
}

}  // namespace

Json::Json(bool value) : type_(Type::kBool), bool_(value) {}

Json::Json(double value) : type_(Type::kNumber), number_(value) {}

Json::Json(std::string value) : type_(Type::kString), string_(std::move(value)) {}

Json Json::array() {
  Json json;
  json.type_ = Type::kArray;
  return json;
}

Json Json::object() {
  Json json;
  json.type_ = Type::kObject;
  return json;
}

Result<Json> Json::parse(std::string_view text) { return Parser(text).parse(); }

bool Json::as_bool(bool fallback) const { return is_bool() ? bool_ : fallback; }

double Json::as_number(double fallback) const { return is_number() ? number_ : fallback; }

std::size_t Json::size() const {
  if (is_array()) {
    return array_.size();
  }
  if (is_object()) {
    return object_.size();
  }
  return 0;
}

const Json& Json::at(std::size_t index) const {
  static const Json kNull;
  if (!is_array() || index >= array_.size()) {
    return kNull;
  }
  return array_[index];
}

bool Json::contains(std::string_view key) const { return find(key) != nullptr; }

const Json* Json::find(std::string_view key) const {
  if (!is_object()) {
    return nullptr;
  }
  for (const auto& [name, value] : object_) {
    if (name == key) {
      return &value;
    }
  }
  return nullptr;
}

const Json& Json::operator[](std::string_view key) const {
  static const Json kNull;
  const Json* found = find(key);
  return found == nullptr ? kNull : *found;
}

void Json::push_back(Json value) {
  if (is_array()) {
    array_.push_back(std::move(value));
  }
}

void Json::set(std::string key, Json value) {
  if (!is_object()) {
    return;
  }
  for (auto& [name, existing] : object_) {
    if (name == key) {
      existing = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

std::string Json::dump(int indent) const {
  std::string out;
  dump_to(out, indent, 0);
  return out;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
  const auto newline = [&](int level) {
    if (indent > 0) {
      out.push_back('\n');
      out.append(static_cast<std::size_t>(indent * level), ' ');
    }
  };

  switch (type_) {
    case Type::kNull:
      out += "null";
      break;
    case Type::kBool:
      out += bool_ ? "true" : "false";
      break;
    case Type::kNumber:
      number_to(out, number_);
      break;
    case Type::kString:
      escape_to(out, string_);
      break;
    case Type::kArray: {
      out.push_back('[');
      for (std::size_t i = 0; i < array_.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        newline(depth + 1);
        array_[i].dump_to(out, indent, depth + 1);
      }
      if (!array_.empty()) {
        newline(depth);
      }
      out.push_back(']');
      break;
    }
    case Type::kObject: {
      out.push_back('{');
      for (std::size_t i = 0; i < object_.size(); ++i) {
        if (i != 0) {
          out.push_back(',');
        }
        newline(depth + 1);
        escape_to(out, object_[i].first);
        out.push_back(':');
        if (indent > 0) {
          out.push_back(' ');
        }
        object_[i].second.dump_to(out, indent, depth + 1);
      }
      if (!object_.empty()) {
        newline(depth);
      }
      out.push_back('}');
      break;
    }
    default:
      break;
  }
}

}  // namespace figtools
