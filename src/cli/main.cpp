// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "cli/commands.hpp"

namespace {

struct Arguments {
  std::vector<std::string> positional;
  std::map<std::string, std::string> options;
  std::vector<std::string> flags;

  [[nodiscard]] bool has_flag(const std::string& name) const {
    for (const auto& flag : flags) {
      if (flag == name) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::string option(const std::string& name, const std::string& fallback = "") const {
    const auto it = options.find(name);
    return it == options.end() ? fallback : it->second;
  }
};

const std::vector<std::string> kBooleanFlags = {"trace", "json",       "help",
                                                 "version", "recursive", "overwrite"};

Arguments parse_arguments(int argc, char** argv) {
  Arguments arguments;
  for (int i = 2; i < argc; ++i) {
    std::string token = argv[i];
    if (token.size() > 2 && token[0] == '-' && token[1] == '-') {
      const std::string name = token.substr(2);
      bool is_boolean = false;
      for (const auto& flag : kBooleanFlags) {
        if (flag == name) {
          is_boolean = true;
          break;
        }
      }
      if (is_boolean) {
        arguments.flags.push_back(name);
        continue;
      }
      if (i + 1 < argc) {
        arguments.options[name] = argv[++i];
      }
      continue;
    }
    if (token.size() > 1 && token[0] == '-') {
      const std::string name = token.substr(1);
      if (i + 1 < argc) {
        arguments.options[name] = argv[++i];
      }
      continue;
    }
    arguments.positional.push_back(token);
  }
  return arguments;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    figtools::cli::print_usage();
    return 1;
  }

  const std::string command = argv[1];
  const Arguments arguments = parse_arguments(argc, argv);

  if (command == "help" || command == "--help" || command == "-h" ||
      arguments.has_flag("help")) {
    figtools::cli::print_usage();
    return 0;
  }
  if (command == "version" || command == "--version" || arguments.has_flag("version")) {
    return figtools::cli::command_version();
  }
  if (command == "info") {
    if (arguments.positional.empty()) {
      std::fprintf(stderr, "error: info requires an image path\n");
      return 1;
    }
    return figtools::cli::command_info(arguments.positional[0]);
  }
  if (command == "ops") {
    return figtools::cli::command_ops(arguments.option("group"), arguments.has_flag("json"));
  }
  if (command == "apply") {
    const std::string input = arguments.option("i");
    const std::string output = arguments.option("o");
    const std::string recipe = arguments.option("r");
    if (input.empty() || output.empty() || recipe.empty()) {
      std::fprintf(stderr, "error: apply requires -i <in> -o <out> -r <recipe>\n");
      return 1;
    }
    return figtools::cli::command_apply(input, output, recipe, arguments.has_flag("trace"));
  }
  if (command == "compare") {
    const std::string lhs = arguments.option("a");
    const std::string rhs = arguments.option("b");
    if (lhs.empty() || rhs.empty()) {
      std::fprintf(stderr, "error: compare requires -a <before> -b <after>\n");
      return 1;
    }
    return figtools::cli::command_compare(lhs, rhs, arguments.has_flag("json"));
  }
  if (command == "batch") {
    if (arguments.positional.size() < 2) {
      std::fprintf(stderr, "error: batch requires <input-dir> <output-dir> -r <recipe>\n");
      return 1;
    }
    const std::string recipe = arguments.option("r");
    if (recipe.empty()) {
      std::fprintf(stderr, "error: batch requires -r <recipe>\n");
      return 1;
    }
    return figtools::cli::command_batch(
        arguments.positional[0], arguments.positional[1], recipe,
        arguments.option("suffix", "_out"), arguments.option("format", "png"),
        arguments.has_flag("recursive"), arguments.has_flag("overwrite"),
        arguments.has_flag("json"));
  }
  if (command == "demo") {
    const std::string output_dir = arguments.option("o", "demo_out");
    int width = 1200;
    int height = 800;
    const std::string size = arguments.option("size");
    if (!size.empty()) {
      const auto separator = size.find('x');
      if (separator != std::string::npos) {
        width = std::stoi(size.substr(0, separator));
        height = std::stoi(size.substr(separator + 1));
      }
    }
    unsigned seed = 2026U;
    const std::string seed_text = arguments.option("seed");
    if (!seed_text.empty()) {
      seed = static_cast<unsigned>(std::stoul(seed_text));
    }
    return figtools::cli::command_demo(output_dir, width, height, seed);
  }

  std::fprintf(stderr, "error: unknown command '%s'\n\n", command.c_str());
  figtools::cli::print_usage();
  return 1;
}
