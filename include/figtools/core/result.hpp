// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Explicit, exception-free error propagation.
//
// The project does not throw exceptions across module boundaries: every
// fallible operation returns Result<T>. This keeps the CLI, the GUI and the
// plugin layer on the same footing and makes failure paths visible in
// signatures.

#ifndef FIGTOOLS_CORE_RESULT_HPP_
#define FIGTOOLS_CORE_RESULT_HPP_

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace figtools {

enum class ErrorCode {
  kOk = 0,
  kInvalidArgument,
  kOutOfRange,
  kUnsupported,
  kIoError,
  kParseError,
  kInternal,
};

const char* to_string(ErrorCode code) noexcept;

struct Error {
  ErrorCode code = ErrorCode::kInternal;
  std::string message;

  Error() = default;
  Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
};

inline Error make_error(ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

// Result<T> holds either a value or an Error.
template <typename T>
class [[nodiscard]] Result {
 public:
  Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}  // NOLINT
  Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

  static Result ok(T value) { return Result(std::move(value)); }
  static Result fail(ErrorCode code, std::string message) {
    return Result(Error{code, std::move(message)});
  }

  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }

  T& value() & {
    assert(has_value());
    return std::get<0>(storage_);
  }
  const T& value() const& {
    assert(has_value());
    return std::get<0>(storage_);
  }
  T&& value() && {
    assert(has_value());
    return std::get<0>(std::move(storage_));
  }

  T value_or(T fallback) const {
    return has_value() ? std::get<0>(storage_) : std::move(fallback);
  }

  const Error& error() const& {
    assert(!has_value());
    return std::get<1>(storage_);
  }

 private:
  std::variant<T, Error> storage_;
};

// Result<void> for operations that only report success or failure.
template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  Result(Error error) : error_(std::move(error)), ok_(false) {}  // NOLINT

  static Result ok() { return Result(); }
  static Result fail(ErrorCode code, std::string message) {
    return Result(Error{code, std::move(message)});
  }

  [[nodiscard]] bool has_value() const noexcept { return ok_; }
  explicit operator bool() const noexcept { return ok_; }
  const Error& error() const { return error_; }

 private:
  Error error_;
  bool ok_ = true;
};

using Status = Result<void>;

}  // namespace figtools

#endif  // FIGTOOLS_CORE_RESULT_HPP_
