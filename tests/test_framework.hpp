// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Tiny test harness. Deliberately dependency free: the whole suite builds and
// runs in under a second on every CI platform, with no package manager.

#ifndef FIGTOOLS_TESTS_TEST_FRAMEWORK_HPP_
#define FIGTOOLS_TESTS_TEST_FRAMEWORK_HPP_

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ft_test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

std::vector<TestCase>& registry();
int& failure_count();

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    registry().push_back(TestCase{std::move(name), std::move(fn)});
  }
};

inline std::string to_text(int value) { return std::to_string(value); }
inline std::string to_text(long long value) { return std::to_string(value); }
inline std::string to_text(unsigned long long value) { return std::to_string(value); }
inline std::string to_text(double value) { return std::to_string(value); }
inline std::string to_text(const std::string& value) { return value; }
inline std::string to_text(const char* value) { return value; }

inline void report_failure(const char* file, int line, const std::string& message) {
  ++failure_count();
  std::fprintf(stderr, "    %s:%d: %s\n", file, line, message.c_str());
}

}  // namespace ft_test

#define TEST(name)                                             \
  static void name();                                          \
  static ::ft_test::Registrar registrar_##name(#name, name);   \
  static void name()

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      ::ft_test::report_failure(__FILE__, __LINE__, "CHECK failed: " #condition); \
    }                                                                       \
  } while (false)

#define CHECK_EQ(lhs, rhs)                                                                       \
  do {                                                                                           \
    const auto& check_lhs = (lhs);                                                               \
    const auto& check_rhs = (rhs);                                                               \
    if (!(check_lhs == check_rhs)) {                                                             \
      ::ft_test::report_failure(__FILE__, __LINE__,                                              \
                                std::string("CHECK_EQ failed: " #lhs " == " #rhs " (") +         \
                                    ::ft_test::to_text(check_lhs) + " vs " +                     \
                                    ::ft_test::to_text(check_rhs) + ")");                        \
    }                                                                                            \
  } while (false)

#define CHECK_NEAR(lhs, rhs, eps)                                                                \
  do {                                                                                           \
    const double check_lhs = (lhs);                                                              \
    const double check_rhs = (rhs);                                                              \
    if (std::fabs(check_lhs - check_rhs) > (eps)) {                                              \
      ::ft_test::report_failure(__FILE__, __LINE__,                                              \
                                std::string("CHECK_NEAR failed: " #lhs " ~ " #rhs " (") +        \
                                    ::ft_test::to_text(check_lhs) + " vs " +                     \
                                    ::ft_test::to_text(check_rhs) + ")");                        \
    }                                                                                            \
  } while (false)

#endif  // FIGTOOLS_TESTS_TEST_FRAMEWORK_HPP_
