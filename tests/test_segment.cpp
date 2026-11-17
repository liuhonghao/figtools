// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/segment/interactive.hpp"
#include "test_framework.hpp"

#include <algorithm>

using namespace figtools;
using namespace figtools::segment;

namespace {

Image two_tone_image() {
  Image image(100, 100, Rgba{240, 240, 240, 255});
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < 50; ++x) image.set(x, y, Rgba{20, 20, 20, 255});
  }
  // A 20x20 red block inside the dark half - the GrabCut target.
  for (int y = 40; y < 60; ++y) {
    for (int x = 20; x < 40; ++x) image.set(x, y, Rgba{220, 30, 30, 255});
  }
  return image;
}

}  // namespace

TEST(magic_wand_selects_similar_region) {
  const Image image = two_tone_image();
  const Mask wand = magic_wand(image.view(), 70, 70, 20.0);
  CHECK(wand.width == 100);
  CHECK(wand.height == 100);
  CHECK(wand.count() > 1000);
  CHECK(wand.at(90, 90));
  CHECK(!wand.at(10, 10));  // did not leak into the dark half
}

TEST(magic_wand_respects_tolerance) {
  const Image image = two_tone_image();
  const Mask tight = magic_wand(image.view(), 30, 50, 20.0);
  const Mask loose = magic_wand(image.view(), 30, 50, 400.0);
  CHECK(tight.count() < loose.count());
  CHECK(tight.at(30, 50));
}

TEST(mask_largest_component_and_contour) {
  const Image image = two_tone_image();
  const Mask wand = magic_wand(image.view(), 70, 70, 20.0);
  const Mask largest = largest_component(wand);
  CHECK(largest.count() == wand.count());
  const auto points = mask_to_polygon(wand, 2.0);
  CHECK(points.size() >= 4);
  CHECK(points.size() < 100);  // simplified
  for (const auto& point : points) {
    CHECK(point.x >= 0 && point.x < 100 && point.y >= 0 && point.y < 100);
  }
}

TEST(grabcut_extracts_boxed_target) {
  const Image image = two_tone_image();
  // Rectangle around the red block: GrabCut should keep ~400 pixels.
  const Mask mask = grabcut(image.view(), RectF{12, 34, 36, 32}, 4);
  CHECK(mask.width == 100);
  CHECK(mask.count() > 200);
  CHECK(mask.count() < 2000);  // did not swallow the whole box
  const auto polygon = mask_to_polygon(mask, 2.0);
  CHECK(polygon.size() >= 3);
}

TEST(grabcut_bounded_rectangle) {
  const Image image = two_tone_image();
  const Mask outside = grabcut(image.view(), RectF{-50, -50, 10, 10}, 2);
  CHECK(outside.empty());  // fully outside the image -> no mask
  const Mask tiny = grabcut(image.view(), RectF{10, 10, 2, 2}, 2);
  CHECK(tiny.empty());  // too small to process
}
