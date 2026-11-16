// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/pipeline/history.hpp"

#include <algorithm>
#include <utility>

namespace figtools {

RecipeHistory::RecipeHistory(const Recipe& initial, std::size_t capacity)
    : capacity_(std::max<std::size_t>(2, capacity)) {
  entries_.push_back(Entry{initial, {}, {}});
}

void RecipeHistory::reset(const Recipe& recipe) {
  entries_.clear();
  entries_.push_back(Entry{recipe, {}, {}});
  index_ = 0;
}

std::string RecipeHistory::undo_label() const {
  return index_ == 0 ? std::string{} : entries_[index_].label;
}

std::string RecipeHistory::redo_label() const {
  return index_ + 1 < entries_.size() ? entries_[index_ + 1].label : std::string{};
}

bool RecipeHistory::commit(const Recipe& next, std::string label, std::string coalesce_token) {
  // Canonical JSON is the equality test: it compares exactly what would be
  // persisted, so a commit can never disagree with a reload.
  if (next.to_json(0) == entries_[index_].recipe.to_json(0)) {
    return false;
  }

  const bool extend = !coalesce_token.empty() && index_ + 1 == entries_.size() &&
                      entries_[index_].token == coalesce_token;
  if (extend) {
    entries_[index_].recipe = next;
    entries_[index_].label = std::move(label);
    return true;
  }

  // A new branch invalidates anything that was undone.
  entries_.resize(index_ + 1);
  entries_.push_back(Entry{next, std::move(label), std::move(coalesce_token)});
  index_ = entries_.size() - 1;

  if (entries_.size() > capacity_) {
    const std::size_t drop = entries_.size() - capacity_;
    entries_.erase(entries_.begin(),
                   entries_.begin() + static_cast<std::ptrdiff_t>(drop));
    index_ -= drop;
  }
  return true;
}

void RecipeHistory::seal() {
  if (index_ < entries_.size()) {
    entries_[index_].token.clear();
  }
}

bool RecipeHistory::undo() {
  if (!can_undo()) {
    return false;
  }
  --index_;
  return true;
}

bool RecipeHistory::redo() {
  if (!can_redo()) {
    return false;
  }
  ++index_;
  return true;
}

}  // namespace figtools
