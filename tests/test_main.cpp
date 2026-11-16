// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <string>
#include <vector>

#include "test_framework.hpp"

namespace ft_test {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

int& failure_count() {
  static int count = 0;
  return count;
}

}  // namespace ft_test

int main() {
  auto& cases = ft_test::registry();
  int failed_tests = 0;

  std::printf("running %zu test(s)\n", cases.size());
  for (const auto& test : cases) {
    const int before = ft_test::failure_count();
    test.fn();
    const bool passed = ft_test::failure_count() == before;
    if (!passed) {
      ++failed_tests;
    }
    std::printf("  [%s] %s\n", passed ? "PASS" : "FAIL", test.name.c_str());
  }

  if (failed_tests == 0) {
    std::printf("all tests passed\n");
    return 0;
  }
  std::printf("%d test(s) failed, %d assertion(s) failed\n", failed_tests, ft_test::failure_count());
  return 1;
}
