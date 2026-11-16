// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#ifndef FIGTOOLS_OPS_REGISTRY_HPP_
#define FIGTOOLS_OPS_REGISTRY_HPP_

#include <string>
#include <string_view>
#include <vector>

#include "figtools/ops/operator.hpp"

namespace figtools {

// Process-wide operator registry.
//
// Operators register themselves at static-initialisation time from their
// translation unit, which keeps the list in one place per file and avoids a
// central switch statement that rots.
class Registry {
 public:
  [[nodiscard]] static Registry& instance();

  // Returns the registry without triggering operator registration. Used by the
  // bootstrap path only; callers outside the library should use instance().
  [[nodiscard]] static Registry& instance_unchecked();

  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;

  void add(Operator op);

  [[nodiscard]] const Operator* find(std::string_view id) const;
  [[nodiscard]] bool contains(std::string_view id) const;
  [[nodiscard]] std::vector<const Operator*> all() const;
  [[nodiscard]] std::size_t size() const;

  // Validates `raw` against `spec`, fills defaults and rejects out-of-range
  // values. The returned Params is canonical: it contains exactly the keys the
  // operator declares, with normalised types. Cache keys are computed from it.
  [[nodiscard]] Result<Params> normalize(const OperatorSpec& spec, const Params& raw) const;

 private:
  Registry() = default;

  std::map<std::string, Operator> operators_;
};

}  // namespace figtools

#endif  // FIGTOOLS_OPS_REGISTRY_HPP_
