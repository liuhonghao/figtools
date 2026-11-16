// Copyright 2026 figtools authors
// SPDX-License-Identifier: MIT

#include "figtools/annotate/annotation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "figtools/json/json.hpp"

namespace figtools {
namespace {

bool point_in_polygon(const std::vector<Point2f>& points, Point2f probe) {
  bool inside = false;
  const std::size_t count = points.size();
  if (count < 3) {
    return false;
  }
  for (std::size_t i = 0, j = count - 1; i < count; j = i++) {
    const Point2f& a = points[i];
    const Point2f& b = points[j];
    const bool straddles = (a.y > probe.y) != (b.y > probe.y);
    if (!straddles) {
      continue;
    }
    const float t = (probe.y - a.y) / (b.y - a.y);
    if (probe.x < a.x + t * (b.x - a.x)) {
      inside = !inside;
    }
  }
  return inside;
}

std::string shape_type_name(ShapeType type) {
  return type == ShapeType::kPolygon ? "polygon" : "rect";
}

}  // namespace

RectF Shape::bounding_box() const {
  if (type == ShapeType::kRect || points.empty()) {
    return rect;
  }
  float min_x = points.front().x;
  float max_x = points.front().x;
  float min_y = points.front().y;
  float max_y = points.front().y;
  for (const Point2f& point : points) {
    min_x = std::min(min_x, point.x);
    max_x = std::max(max_x, point.x);
    min_y = std::min(min_y, point.y);
    max_y = std::max(max_y, point.y);
  }
  return RectF{min_x, min_y, max_x - min_x, max_y - min_y};
}

float Shape::area() const {
  if (type == ShapeType::kRect) {
    return rect.area();
  }
  if (points.size() < 3) {
    return 0.0F;
  }
  double sum = 0.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const Point2f& a = points[i];
    const Point2f& b = points[(i + 1) % points.size()];
    sum += static_cast<double>(a.x) * static_cast<double>(b.y) -
           static_cast<double>(b.x) * static_cast<double>(a.y);
  }
  return static_cast<float>(std::abs(sum) * 0.5);
}

bool Shape::contains(Point2f point) const {
  if (type == ShapeType::kRect) {
    return rect.contains(point.x, point.y);
  }
  return point_in_polygon(points, point);
}

void AnnotationSet::set_image_size(int width, int height) {
  image_width_ = std::max(0, width);
  image_height_ = std::max(0, height);
}

void AnnotationSet::set_classes(std::vector<LabelClass> classes) { classes_ = std::move(classes); }

const LabelClass* AnnotationSet::find_class(int id) const {
  for (const LabelClass& label : classes_) {
    if (label.id == id) {
      return &label;
    }
  }
  return nullptr;
}

int AnnotationSet::class_index(int class_id) const {
  for (std::size_t i = 0; i < classes_.size(); ++i) {
    if (classes_[i].id == class_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void AnnotationSet::add(Annotation annotation) { items_.push_back(std::move(annotation)); }

void AnnotationSet::remove_at(std::size_t index) {
  if (index >= items_.size()) {
    return;
  }
  items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
}

void AnnotationSet::clear() { items_.clear(); }

std::string AnnotationSet::to_json(int indent) const {
  Json root = Json::object();
  root.set("version", Json(1.0));
  root.set("image_width", Json(static_cast<double>(image_width_)));
  root.set("image_height", Json(static_cast<double>(image_height_)));

  Json classes = Json::array();
  for (const LabelClass& label : classes_) {
    Json node = Json::object();
    node.set("id", Json(static_cast<double>(label.id)));
    node.set("name", Json(label.name));
    node.set("color", Json(label.color));
    classes.push_back(std::move(node));
  }
  root.set("classes", std::move(classes));

  Json shapes = Json::array();
  for (const Annotation& annotation : items_) {
    Json node = Json::object();
    node.set("class_id", Json(static_cast<double>(annotation.class_id)));
    node.set("type", Json(shape_type_name(annotation.shape.type)));
    node.set("confidence", Json(static_cast<double>(annotation.confidence)));
    if (annotation.shape.type == ShapeType::kRect) {
      Json rect = Json::array();
      rect.push_back(Json(static_cast<double>(annotation.shape.rect.x)));
      rect.push_back(Json(static_cast<double>(annotation.shape.rect.y)));
      rect.push_back(Json(static_cast<double>(annotation.shape.rect.width)));
      rect.push_back(Json(static_cast<double>(annotation.shape.rect.height)));
      node.set("rect", std::move(rect));
    } else {
      Json points = Json::array();
      for (const Point2f& point : annotation.shape.points) {
        Json pair = Json::array();
        pair.push_back(Json(static_cast<double>(point.x)));
        pair.push_back(Json(static_cast<double>(point.y)));
        points.push_back(std::move(pair));
      }
      node.set("points", std::move(points));
    }
    shapes.push_back(std::move(node));
  }
  root.set("shapes", std::move(shapes));
  return root.dump(indent);
}

Result<AnnotationSet> AnnotationSet::from_json(std::string_view text) {
  auto parsed = Json::parse(text);
  if (!parsed) {
    return Result<AnnotationSet>::fail(parsed.error().code, parsed.error().message);
  }
  const Json& root = parsed.value();
  if (!root.is_object()) {
    return Result<AnnotationSet>::fail(ErrorCode::kParseError,
                                       "annotations: root must be a JSON object");
  }

  AnnotationSet set;
  set.set_image_size(static_cast<int>(root["image_width"].as_number(0.0)),
                     static_cast<int>(root["image_height"].as_number(0.0)));

  std::vector<LabelClass> classes;
  const Json& class_array = root["classes"];
  for (std::size_t i = 0; i < class_array.size(); ++i) {
    const Json& node = class_array.at(i);
    LabelClass label;
    label.id = static_cast<int>(node["id"].as_number(static_cast<double>(i)));
    label.name = node["name"].as_string();
    if (label.name.empty()) {
      label.name = "class_" + std::to_string(label.id);
    }
    const std::string colour = node["color"].as_string();
    if (!colour.empty()) {
      label.color = colour;
    }
    classes.push_back(std::move(label));
  }
  if (classes.empty()) {
    classes = default_classes();
  }
  set.set_classes(std::move(classes));

  const Json& shape_array = root["shapes"];
  for (std::size_t i = 0; i < shape_array.size(); ++i) {
    const Json& node = shape_array.at(i);
    Annotation annotation;
    annotation.class_id = static_cast<int>(node["class_id"].as_number(0.0));
    annotation.confidence = static_cast<float>(node["confidence"].as_number(1.0));

    const std::string type = node["type"].as_string();
    if (type == "polygon") {
      annotation.shape.type = ShapeType::kPolygon;
      const Json& points = node["points"];
      for (std::size_t k = 0; k < points.size(); ++k) {
        const Json& pair = points.at(k);
        annotation.shape.points.push_back(
            Point2f{static_cast<float>(pair.at(0).as_number(0.0)),
                    static_cast<float>(pair.at(1).as_number(0.0))});
      }
      if (annotation.shape.points.size() < 3) {
        continue;
      }
    } else {
      annotation.shape.type = ShapeType::kRect;
      const Json& rect = node["rect"];
      annotation.shape.rect = RectF{
          static_cast<float>(rect.at(0).as_number(0.0)),
          static_cast<float>(rect.at(1).as_number(0.0)),
          static_cast<float>(rect.at(2).as_number(0.0)),
          static_cast<float>(rect.at(3).as_number(0.0)),
      };
      if (annotation.shape.rect.width <= 0.0F || annotation.shape.rect.height <= 0.0F) {
        continue;
      }
    }
    set.add(std::move(annotation));
  }

  return Result<AnnotationSet>::ok(std::move(set));
}

std::string AnnotationSet::to_coco_json(const std::string& image_name) const {
  Json root = Json::object();

  Json info = Json::object();
  info.set("description", Json(std::string("figtools annotation export")));
  info.set("version", Json(std::string("0.3")));
  root.set("info", std::move(info));

  Json images = Json::array();
  Json image = Json::object();
  image.set("id", Json(1.0));
  image.set("file_name", Json(image_name));
  image.set("width", Json(static_cast<double>(image_width_)));
  image.set("height", Json(static_cast<double>(image_height_)));
  images.push_back(std::move(image));
  root.set("images", std::move(images));

  Json categories = Json::array();
  for (const LabelClass& label : classes_) {
    Json category = Json::object();
    category.set("id", Json(static_cast<double>(label.id)));
    category.set("name", Json(label.name));
    category.set("supercategory", Json(std::string("figtools")));
    categories.push_back(std::move(category));
  }
  root.set("categories", std::move(categories));

  Json annotations = Json::array();
  int annotation_id = 1;
  for (const Annotation& annotation : items_) {
    const RectF box = annotation.shape.bounding_box();
    if (box.width <= 0.0F || box.height <= 0.0F) {
      continue;
    }
    Json node = Json::object();
    node.set("id", Json(static_cast<double>(annotation_id++)));
    node.set("image_id", Json(1.0));
    node.set("category_id", Json(static_cast<double>(annotation.class_id)));
    node.set("iscrowd", Json(0.0));

    Json bbox = Json::array();
    bbox.push_back(Json(static_cast<double>(box.x)));
    bbox.push_back(Json(static_cast<double>(box.y)));
    bbox.push_back(Json(static_cast<double>(box.width)));
    bbox.push_back(Json(static_cast<double>(box.height)));
    node.set("bbox", std::move(bbox));
    node.set("area", Json(static_cast<double>(annotation.shape.area())));

    Json segmentation = Json::array();
    Json ring = Json::array();
    if (annotation.shape.type == ShapeType::kPolygon) {
      for (const Point2f& point : annotation.shape.points) {
        ring.push_back(Json(static_cast<double>(point.x)));
        ring.push_back(Json(static_cast<double>(point.y)));
      }
    } else {
      const RectF& rect = annotation.shape.rect;
      ring.push_back(Json(static_cast<double>(rect.x)));
      ring.push_back(Json(static_cast<double>(rect.y)));
      ring.push_back(Json(static_cast<double>(rect.x + rect.width)));
      ring.push_back(Json(static_cast<double>(rect.y)));
      ring.push_back(Json(static_cast<double>(rect.x + rect.width)));
      ring.push_back(Json(static_cast<double>(rect.y + rect.height)));
      ring.push_back(Json(static_cast<double>(rect.x)));
      ring.push_back(Json(static_cast<double>(rect.y + rect.height)));
    }
    segmentation.push_back(std::move(ring));
    node.set("segmentation", std::move(segmentation));

    annotations.push_back(std::move(node));
  }
  root.set("annotations", std::move(annotations));
  return root.dump(2);
}

std::string AnnotationSet::to_yolo_txt() const {
  if (image_width_ <= 0 || image_height_ <= 0) {
    return {};
  }
  const double width = static_cast<double>(image_width_);
  const double height = static_cast<double>(image_height_);

  std::string out;
  for (const Annotation& annotation : items_) {
    const int index = class_index(annotation.class_id);
    if (index < 0) {
      continue;
    }
    const RectF box = annotation.shape.bounding_box();
    if (box.width <= 0.0F || box.height <= 0.0F) {
      continue;
    }
    const double cx = (static_cast<double>(box.x) + static_cast<double>(box.width) / 2.0) / width;
    const double cy = (static_cast<double>(box.y) + static_cast<double>(box.height) / 2.0) / height;
    const double bw = static_cast<double>(box.width) / width;
    const double bh = static_cast<double>(box.height) / height;

    char line[128];
    std::snprintf(line, sizeof(line), "%d %.6f %.6f %.6f %.6f\n", index, cx, cy, bw, bh);
    out += line;
  }
  return out;
}

std::vector<LabelClass> default_classes() {
  return {
      LabelClass{0, "区域", "#B4562A"},
      LabelClass{1, "目标", "#4F7A72"},
      LabelClass{2, "参考", "#A9781A"},
      LabelClass{3, "忽略", "#7A4E5E"},
  };
}

}  // namespace figtools
