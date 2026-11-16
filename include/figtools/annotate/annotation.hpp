// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT
//
// Annotation model.
//
// Coordinates live in the coordinate space of the image that was on screen when
// the shape was drawn (normally the processed result). Storing them in that
// space keeps "what you see is what you annotate" true; the size is recorded so
// exports can state it and the GUI can warn when a pipeline change moves the
// goalposts.

#ifndef FIGTOOLS_ANNOTATE_ANNOTATION_HPP_
#define FIGTOOLS_ANNOTATE_ANNOTATION_HPP_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "figtools/core/result.hpp"

namespace figtools {

struct Point2f {
  float x = 0.0F;
  float y = 0.0F;
};

struct RectF {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;

  [[nodiscard]] float area() const { return width > 0.0F && height > 0.0F ? width * height : 0.0F; }
  [[nodiscard]] bool contains(float px, float py) const {
    return px >= x && py >= y && px <= x + width && py <= y + height;
  }
};

enum class ShapeType {
  kRect = 0,
  kPolygon,
};

struct Shape {
  ShapeType type = ShapeType::kRect;
  RectF rect;                    // kRect
  std::vector<Point2f> points;   // kPolygon

  [[nodiscard]] RectF bounding_box() const;
  [[nodiscard]] float area() const;
  [[nodiscard]] bool contains(Point2f point) const;
};

struct LabelClass {
  int id = 0;
  std::string name;
  std::string color = "#185FA5";  // #RRGGBB, used by the GUI
};

struct Annotation {
  int class_id = 0;
  Shape shape;
  float confidence = 1.0F;  // 1.0 for manual shapes, model score for pre-annotations
};

// A set of shapes belonging to one image plus the label schema used to draw them.
class AnnotationSet {
 public:
  void set_image_size(int width, int height);
  [[nodiscard]] int image_width() const { return image_width_; }
  [[nodiscard]] int image_height() const { return image_height_; }

  void set_classes(std::vector<LabelClass> classes);
  [[nodiscard]] const std::vector<LabelClass>& classes() const { return classes_; }
  [[nodiscard]] const LabelClass* find_class(int id) const;

  void add(Annotation annotation);
  void remove_at(std::size_t index);
  void clear();
  [[nodiscard]] const std::vector<Annotation>& items() const { return items_; }
  [[nodiscard]] std::vector<Annotation>& items() { return items_; }
  [[nodiscard]] std::size_t size() const { return items_.size(); }

  // Native format: round-trips classes, shapes and image size.
  [[nodiscard]] std::string to_json(int indent = 2) const;
  [[nodiscard]] static Result<AnnotationSet> from_json(std::string_view text);

  // COCO detection/segmentation export for a single image.
  [[nodiscard]] std::string to_coco_json(const std::string& image_name) const;

  // YOLO label file: one line per shape, `class_index cx cy w h`, normalised.
  // Polygons are reduced to their bounding box (YOLO detection format).
  [[nodiscard]] std::string to_yolo_txt() const;

  // Class index used by YOLO (position in the class list), -1 when unknown.
  [[nodiscard]] int class_index(int class_id) const;

 private:
  int image_width_ = 0;
  int image_height_ = 0;
  std::vector<LabelClass> classes_;
  std::vector<Annotation> items_;
};

// Default schema for the sample workflow (surface inspection).
[[nodiscard]] std::vector<LabelClass> default_classes();

}  // namespace figtools

#endif  // FIGTOOLS_ANNOTATE_ANNOTATION_HPP_
