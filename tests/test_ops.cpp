// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <string>

#include "figtools/core/hash.hpp"
#include "figtools/ops/registry.hpp"
#include "figtools/pipeline/executor.hpp"
#include "test_framework.hpp"
#include "test_helpers.hpp"

using namespace figtools;

TEST(registry_contains_expected_operators) {
  const Registry& registry = Registry::instance();
  CHECK(registry.size() >= 18);
  CHECK(registry.contains("resize"));
  CHECK(registry.contains("threshold"));
  CHECK(registry.contains("unsharp"));
  CHECK(registry.find("does_not_exist") == nullptr);
}

TEST(registry_is_sorted_by_group_then_id) {
  const Registry& registry = Registry::instance();
  const auto all = registry.all();
  CHECK(!all.empty());
  for (std::size_t i = 1; i < all.size(); ++i) {
    const auto& previous = all[i - 1]->spec;
    const auto& current = all[i]->spec;
    const bool ordered = previous.group < current.group ||
                         (previous.group == current.group && previous.id < current.id);
    CHECK(ordered);
  }
}

TEST(normalize_fills_defaults) {
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("resize");
  CHECK(op != nullptr);

  Params raw;
  auto normalized = registry.normalize(op->spec, raw);
  CHECK(normalized.has_value());
  CHECK_EQ(normalized.value().size(), op->spec.params.size());
  CHECK_NEAR(normalized.value().number("scale", 0.0), 1.0, 1e-9);
  CHECK_EQ(normalized.value().text("interp", ""), std::string("bilinear"));
  CHECK_EQ(normalized.value().integer("width", -1), 0);
}

TEST(normalize_rejects_unknown_parameter) {
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("resize");
  CHECK(op != nullptr);

  Params raw;
  raw.set("not_a_parameter", 1.0);
  auto normalized = registry.normalize(op->spec, raw);
  CHECK(!normalized.has_value());
  CHECK_EQ(static_cast<int>(normalized.error().code),
           static_cast<int>(ErrorCode::kInvalidArgument));
}

TEST(normalize_rejects_out_of_range) {
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("gamma");
  CHECK(op != nullptr);

  Params raw;
  raw.set("gamma", 99.0);
  auto normalized = registry.normalize(op->spec, raw);
  CHECK(!normalized.has_value());
  CHECK_EQ(static_cast<int>(normalized.error().code), static_cast<int>(ErrorCode::kOutOfRange));
}

TEST(normalize_rejects_fractional_integer) {
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("rotate90");
  CHECK(op != nullptr);

  Params raw;
  raw.set("turns", 1.5);
  auto normalized = registry.normalize(op->spec, raw);
  CHECK(!normalized.has_value());
}

TEST(gamma_one_is_identity) {
  const Image source = ft_test::make_test_image(12, 9);
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("gamma");
  CHECK(op != nullptr);

  Params params;
  params.set("gamma", 1.0);
  auto output = op->fn(source.view(), params);
  CHECK(output.has_value());
  CHECK_EQ(figtools::to_hex(figtools::hash_image(output.value().view())),
           figtools::to_hex(figtools::hash_image(source.view())));
}

TEST(threshold_outputs_only_black_and_white) {
  const Image source = ft_test::make_test_image(16, 16);
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("threshold");
  CHECK(op != nullptr);

  Params params;
  params.set("value", 128.0);
  auto output = op->fn(source.view(), params);
  CHECK(output.has_value());
  for (int y = 0; y < output.value().height(); ++y) {
    for (int x = 0; x < output.value().width(); ++x) {
      const Rgba pixel = output.value().at(x, y);
      CHECK(pixel.r == 0 || pixel.r == 255);
      CHECK_EQ(static_cast<int>(pixel.r), static_cast<int>(pixel.g));
      CHECK_EQ(static_cast<int>(pixel.r), static_cast<int>(pixel.b));
    }
  }
}

TEST(grayscale_has_equal_channels) {
  const Image source = ft_test::make_test_image(10, 10);
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("grayscale");
  CHECK(op != nullptr);

  auto output = op->fn(source.view(), Params{});
  CHECK(output.has_value());
  const Rgba pixel = output.value().at(3, 4);
  CHECK_EQ(static_cast<int>(pixel.r), static_cast<int>(pixel.g));
  CHECK_EQ(static_cast<int>(pixel.r), static_cast<int>(pixel.b));
}

TEST(resize_operator_changes_dimensions) {
  const Image source = ft_test::make_test_image(40, 20);
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("resize");
  CHECK(op != nullptr);

  Params params;
  params.set("scale", 0.5);
  auto output = op->fn(source.view(), params);
  CHECK(output.has_value());
  CHECK_EQ(output.value().width(), 20);
  CHECK_EQ(output.value().height(), 10);
}

TEST(crop_operator_uses_percentages) {
  const Image source = ft_test::make_test_image(100, 80);
  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("crop");
  CHECK(op != nullptr);

  Params params;
  params.set("x", 10.0);
  params.set("y", 25.0);
  params.set("width", 50.0);
  params.set("height", 50.0);
  auto output = op->fn(source.view(), params);
  CHECK(output.has_value());
  CHECK_EQ(output.value().width(), 50);
  CHECK_EQ(output.value().height(), 40);
}

TEST(median_removes_isolated_noise) {
  Image source(11, 11, Rgba{100, 100, 100, 255});
  source.set(5, 5, Rgba{255, 255, 255, 255});

  const Registry& registry = Registry::instance();
  const Operator* op = registry.find("median");
  CHECK(op != nullptr);

  Params params;
  params.set("radius", 1.0);
  auto output = op->fn(source.view(), params);
  CHECK(output.has_value());
  CHECK_EQ(static_cast<int>(output.value().at(5, 5).r), 100);
}
