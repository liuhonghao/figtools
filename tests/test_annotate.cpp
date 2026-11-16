// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include <string>

#include "figtools/annotate/annotation.hpp"
#include "test_framework.hpp"

using namespace figtools;

namespace {

AnnotationSet make_set() {
  AnnotationSet set;
  set.set_image_size(1000, 500);
  set.set_classes(default_classes());

  Annotation rect;
  rect.class_id = 0;
  rect.shape.type = ShapeType::kRect;
  rect.shape.rect = RectF{100.0F, 50.0F, 200.0F, 100.0F};
  set.add(rect);

  Annotation polygon;
  polygon.class_id = 1;
  polygon.shape.type = ShapeType::kPolygon;
  polygon.shape.points = {Point2f{600.0F, 100.0F}, Point2f{800.0F, 100.0F},
                          Point2f{800.0F, 300.0F}, Point2f{600.0F, 300.0F}};
  set.add(polygon);
  return set;
}

}  // namespace

TEST(rect_geometry) {
  const RectF rect{10.0F, 20.0F, 30.0F, 40.0F};
  CHECK_NEAR(rect.area(), 1200.0, 1e-6);
  CHECK(rect.contains(10.0F, 20.0F));
  CHECK(rect.contains(40.0F, 60.0F));
  CHECK(!rect.contains(9.0F, 20.0F));
  CHECK(!rect.contains(41.0F, 60.0F));
}

TEST(polygon_area_uses_shoelace) {
  Shape shape;
  shape.type = ShapeType::kPolygon;
  shape.points = {Point2f{0.0F, 0.0F}, Point2f{10.0F, 0.0F}, Point2f{0.0F, 10.0F}};
  CHECK_NEAR(shape.area(), 50.0, 1e-4);
}

TEST(polygon_bounding_box) {
  Shape shape;
  shape.type = ShapeType::kPolygon;
  shape.points = {Point2f{5.0F, 5.0F}, Point2f{25.0F, 10.0F}, Point2f{15.0F, 40.0F}};
  const RectF box = shape.bounding_box();
  CHECK_NEAR(box.x, 5.0, 1e-4);
  CHECK_NEAR(box.y, 5.0, 1e-4);
  CHECK_NEAR(box.width, 20.0, 1e-4);
  CHECK_NEAR(box.height, 35.0, 1e-4);
}

TEST(polygon_contains_point) {
  Shape shape;
  shape.type = ShapeType::kPolygon;
  shape.points = {Point2f{0.0F, 0.0F}, Point2f{100.0F, 0.0F}, Point2f{100.0F, 100.0F},
                  Point2f{0.0F, 100.0F}};
  CHECK(shape.contains(Point2f{50.0F, 50.0F}));
  CHECK(!shape.contains(Point2f{150.0F, 50.0F}));
  CHECK(!shape.contains(Point2f{-1.0F, 50.0F}));
}

TEST(annotation_set_class_helpers) {
  const AnnotationSet set = make_set();
  CHECK_EQ(set.size(), static_cast<std::size_t>(2));
  CHECK_EQ(set.class_index(1), 1);
  CHECK_EQ(set.class_index(99), -1);
  CHECK(set.find_class(0) != nullptr);
  CHECK(set.find_class(42) == nullptr);
}

TEST(yolo_export_normalises_coordinates) {
  const AnnotationSet set = make_set();
  const std::string text = set.to_yolo_txt();
  CHECK(!text.empty());
  // rect: x=100..300, y=50..150 in 1000x500 -> cx=0.2 cy=0.2 w=0.2 h=0.2
  CHECK(text.find("0 0.200000 0.200000 0.200000 0.200000") != std::string::npos);
  // polygon: bbox 600..800 x 100..300 -> cx=0.7 cy=0.4 w=0.2 h=0.4
  CHECK(text.find("1 0.700000 0.400000 0.200000 0.400000") != std::string::npos);
}

TEST(yolo_export_requires_image_size) {
  AnnotationSet set = make_set();
  set.set_image_size(0, 0);
  CHECK(set.to_yolo_txt().empty());
}

TEST(coco_export_contains_categories_and_boxes) {
  const AnnotationSet set = make_set();
  const std::string text = set.to_coco_json("bearing.png");
  CHECK(text.find("\"file_name\"") != std::string::npos);
  CHECK(text.find("bearing.png") != std::string::npos);
  CHECK(text.find("\"categories\"") != std::string::npos);
  CHECK(text.find("\"bbox\"") != std::string::npos);
  CHECK(text.find("\"segmentation\"") != std::string::npos);
  CHECK(text.find("划痕") != std::string::npos);
}

TEST(annotation_json_round_trip) {
  const AnnotationSet original = make_set();
  const std::string text = original.to_json(2);

  auto parsed = AnnotationSet::from_json(text);
  CHECK(parsed.has_value());
  if (!parsed) {
    return;
  }
  CHECK_EQ(parsed.value().image_width(), 1000);
  CHECK_EQ(parsed.value().image_height(), 500);
  CHECK_EQ(parsed.value().size(), static_cast<std::size_t>(2));
  CHECK_EQ(parsed.value().classes().size(), static_cast<std::size_t>(4));

  const auto& items = parsed.value().items();
  CHECK_EQ(static_cast<int>(items[0].shape.type), static_cast<int>(ShapeType::kRect));
  CHECK_NEAR(items[0].shape.rect.width, 200.0, 1e-3);
  CHECK_EQ(static_cast<int>(items[1].shape.type), static_cast<int>(ShapeType::kPolygon));
  CHECK_EQ(items[1].shape.points.size(), static_cast<std::size_t>(4));

  // Re-export must be byte identical: the file is a review artefact.
  CHECK_EQ(parsed.value().to_json(2), text);
}

TEST(annotation_json_rejects_garbage) {
  CHECK(!AnnotationSet::from_json("not json").has_value());
  CHECK(!AnnotationSet::from_json("[]").has_value());
}

TEST(default_classes_are_stable) {
  const auto classes = default_classes();
  CHECK_EQ(classes.size(), static_cast<std::size_t>(4));
  CHECK_EQ(classes[0].name, std::string("划痕"));
  CHECK_EQ(classes[0].id, 0);
}
