// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#ifndef FIGTOOLS_SRC_CLI_COMMANDS_HPP_
#define FIGTOOLS_SRC_CLI_COMMANDS_HPP_

#include <string>

namespace figtools::cli {

void print_usage();

int command_info(const std::string& path);
int command_ops(const std::string& group, bool as_json);
int command_apply(const std::string& input, const std::string& output,
                  const std::string& recipe_path, bool trace);
int command_compare(const std::string& lhs_path, const std::string& rhs_path, bool as_json);
int command_batch(const std::string& input_dir, const std::string& output_dir,
                  const std::string& recipe_path, const std::string& suffix,
                  const std::string& format, bool recursive, bool overwrite, bool as_json);
int command_demo(const std::string& output_dir, int width, int height, unsigned seed);
int command_version();

}  // namespace figtools::cli

#endif  // FIGTOOLS_SRC_CLI_COMMANDS_HPP_
