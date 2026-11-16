// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Internal registration hooks. Each translation unit exposes one function so
// that linking is explicit -- no static-initialisation order games and no
// "operators silently missing" when the linker drops an object file.

#ifndef FIGTOOLS_SRC_OPS_REGISTER_HPP_
#define FIGTOOLS_SRC_OPS_REGISTER_HPP_

#include "figtools/ops/registry.hpp"

namespace figtools::detail {

void register_geometry_ops(Registry& registry);
void register_color_ops(Registry& registry);
void register_filter_ops(Registry& registry);

}  // namespace figtools::detail

#endif  // FIGTOOLS_SRC_OPS_REGISTER_HPP_
