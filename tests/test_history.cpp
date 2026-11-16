// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/pipeline/history.hpp"

#include <string>

#include "test_framework.hpp"

namespace {

figtools::Recipe make_recipe(const std::string& op, double factor = 1.0) {
  figtools::Recipe recipe;
  figtools::Step step;
  step.op = op;
  if (op == "contrast") {
    step.params.set("factor", factor);
  }
  recipe.steps.push_back(step);
  return recipe;
}

figtools::Recipe with_factor(const figtools::Recipe& base, double factor) {
  figtools::Recipe next = base;
  next.steps[0].params.set("factor", factor);
  return next;
}

}  // namespace

TEST(history_starts_without_undo_or_redo) {
  const figtools::Recipe initial = make_recipe("contrast");
  figtools::RecipeHistory history(initial);
  CHECK(!history.can_undo());
  CHECK(!history.can_redo());
  CHECK_EQ(history.depth(), static_cast<std::size_t>(1));
  CHECK_EQ(history.current().to_json(0), initial.to_json(0));
}

TEST(history_commit_enables_undo_and_restores_previous) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  const figtools::Recipe edited = with_factor(initial, 1.4);
  figtools::RecipeHistory history(initial);

  CHECK(history.commit(edited, "调整对比度"));
  CHECK(history.can_undo());
  CHECK(!history.can_redo());
  CHECK_EQ(history.current().to_json(0), edited.to_json(0));
  CHECK_EQ(history.undo_label(), std::string("调整对比度"));

  CHECK(history.undo());
  CHECK_EQ(history.current().to_json(0), initial.to_json(0));
  CHECK(!history.can_undo());
  CHECK(history.can_redo());
  CHECK_EQ(history.redo_label(), std::string("调整对比度"));
}

TEST(history_redo_restores_the_newer_state) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  const figtools::Recipe edited = with_factor(initial, 1.4);
  figtools::RecipeHistory history(initial);
  CHECK(history.commit(edited, "调整对比度"));

  CHECK(history.undo());
  CHECK(history.redo());
  CHECK_EQ(history.current().to_json(0), edited.to_json(0));
  CHECK(!history.can_redo());
}

TEST(history_commit_after_undo_drops_the_redo_branch) {
  const figtools::Recipe a = make_recipe("contrast", 1.0);
  const figtools::Recipe b = with_factor(a, 1.2);
  const figtools::Recipe c = with_factor(a, 1.6);
  figtools::RecipeHistory history(a);
  CHECK(history.commit(b, "b"));
  CHECK(history.undo());
  CHECK(history.commit(c, "c"));

  CHECK(!history.can_redo());
  CHECK_EQ(history.depth(), static_cast<std::size_t>(2));
  CHECK_EQ(history.current().to_json(0), c.to_json(0));
}

TEST(history_same_token_coalesces_into_one_step) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  figtools::RecipeHistory history(initial);

  for (int i = 1; i <= 20; ++i) {
    CHECK(history.commit(with_factor(initial, 1.0 + 0.01 * i), "调整对比度",
                         "param:0:factor"));
  }
  // Twenty slider ticks are one undo step.
  CHECK_EQ(history.depth(), static_cast<std::size_t>(2));
  CHECK_EQ(history.current().to_json(0), with_factor(initial, 1.2).to_json(0));

  CHECK(history.undo());
  CHECK_EQ(history.current().to_json(0), initial.to_json(0));
}

TEST(history_seal_splits_two_drags_of_the_same_control) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  figtools::RecipeHistory history(initial);

  CHECK(history.commit(with_factor(initial, 1.2), "drag one", "param:0:factor"));
  history.seal();
  CHECK(history.commit(with_factor(initial, 1.4), "drag two", "param:0:factor"));

  CHECK_EQ(history.depth(), static_cast<std::size_t>(3));
  CHECK(history.undo());
  CHECK_EQ(history.current().to_json(0), with_factor(initial, 1.2).to_json(0));
}

TEST(history_different_token_starts_a_new_step) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  figtools::RecipeHistory history(initial);
  CHECK(history.commit(with_factor(initial, 1.2), "a", "param:0:factor"));
  CHECK(history.commit(with_factor(initial, 1.4), "b", "param:0:radius"));
  CHECK_EQ(history.depth(), static_cast<std::size_t>(3));
}

TEST(history_ignores_no_op_commits) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  figtools::RecipeHistory history(initial);
  CHECK(!history.commit(initial, "same"));
  CHECK_EQ(history.depth(), static_cast<std::size_t>(1));
  CHECK(!history.can_undo());
}

TEST(history_capacity_drops_the_oldest_entries) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  figtools::RecipeHistory history(initial, 3);
  for (int i = 1; i <= 5; ++i) {
    CHECK(history.commit(with_factor(initial, 1.0 + 0.1 * i), "step"));
  }
  CHECK_EQ(history.depth(), static_cast<std::size_t>(3));
  CHECK_EQ(history.position(), static_cast<std::size_t>(2));

  // Only the two retained steps can be undone; the oldest state is gone.
  CHECK(history.undo());
  CHECK(history.undo());
  CHECK(!history.undo());
  CHECK_EQ(history.current().to_json(0), with_factor(initial, 1.3).to_json(0));
}

TEST(history_reset_clears_everything) {
  const figtools::Recipe initial = make_recipe("contrast", 1.0);
  figtools::RecipeHistory history(initial);
  CHECK(history.commit(with_factor(initial, 1.5), "edit"));
  history.reset(initial);
  CHECK(!history.can_undo());
  CHECK(!history.can_redo());
  CHECK_EQ(history.depth(), static_cast<std::size_t>(1));
  CHECK_EQ(history.current().to_json(0), initial.to_json(0));
}
