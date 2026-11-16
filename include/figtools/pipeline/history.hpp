// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Undo/redo for the pipeline.
//
// The unit is a whole recipe snapshot, not a command object. Recipes are tiny
// (a handful of steps), and snapshots cannot drift out of sync with the editor
// the way inverse operations can.
//
// Parameter drags would otherwise push one entry per pixel of slider travel.
// Callers pass a coalesce token such as "param:1:amount": consecutive commits
// with the same token merge into the entry they extend, so one drag is one
// undo step. Any other action changes the token and starts a new entry.

#ifndef FIGTOOLS_PIPELINE_HISTORY_HPP_
#define FIGTOOLS_PIPELINE_HISTORY_HPP_

#include <cstddef>
#include <string>
#include <vector>

#include "figtools/pipeline/recipe.hpp"

namespace figtools {

class RecipeHistory {
 public:
  explicit RecipeHistory(const Recipe& initial, std::size_t capacity = 64);

  // Drops all history and restarts from `recipe`.
  void reset(const Recipe& recipe);

  [[nodiscard]] const Recipe& current() const { return entries_[index_].recipe; }
  [[nodiscard]] bool can_undo() const { return index_ > 0; }
  [[nodiscard]] bool can_redo() const { return index_ + 1 < entries_.size(); }

  // Label of the action that undo/redo would apply; empty when unavailable.
  [[nodiscard]] std::string undo_label() const;
  [[nodiscard]] std::string redo_label() const;

  // Records `next` as the new state. Commits that do not change the recipe are
  // ignored. Returns true when the history changed.
  bool commit(const Recipe& next, std::string label, std::string coalesce_token = {});

  // Ends the current coalescing window: the next commit with the same token
  // starts a new entry. Call on slider release / editing finished so two
  // separate drags are two undo steps.
  void seal();

  bool undo();
  bool redo();

  [[nodiscard]] std::size_t depth() const { return entries_.size(); }
  [[nodiscard]] std::size_t position() const { return index_; }

 private:
  struct Entry {
    Recipe recipe;
    std::string label;
    std::string token;
  };

  std::vector<Entry> entries_;
  std::size_t index_ = 0;
  std::size_t capacity_ = 64;
};

}  // namespace figtools

#endif  // FIGTOOLS_PIPELINE_HISTORY_HPP_
