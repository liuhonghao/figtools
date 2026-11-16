"""Turn figtools annotations into training patches for a sliding-window detector.

A figtools snapshot stores shapes (rectangles or polygons) with a class id. This
module crops each shape's bounding box (plus a little context) into a positive
patch, and samples background patches that do not overlap any annotation. The
result is a small class-folder dataset plus a manifest that preserves the
original train/val/test split.

    images/        original pictures
    annotations/   <image>.json sidecars (native figtools format)
    dataset.json   manifest with items[] and classes[]

输出 ``<out>/patches.json`` and ``<out>/<class>/<name>_pN.png``.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

from .data import DatasetInfo, Sample, IMAGE_SUFFIXES

BACKGROUND = "__background__"


@dataclass
class Box:
    x: float
    y: float
    w: float
    h: float

    def area(self) -> float:
        return max(0.0, self.w) * max(0.0, self.h)

    def iou(self, other: "Box") -> float:
        ix = max(0.0, min(self.x + self.w, other.x + other.w) - max(self.x, other.x))
        iy = max(0.0, min(self.y + self.h, other.y + other.h) - max(self.y, other.y))
        inter = ix * iy
        union = self.area() + other.area() - inter
        return inter / union if union > 0.0 else 0.0


@dataclass
class PatchInfo:
    classes: list[str]
    samples: list[Sample] = field(default_factory=list)
    positives: int = 0
    negatives: int = 0
    images: int = 0
    skipped: int = 0

    def counts(self) -> dict[str, int]:
        result = {name: 0 for name in self.classes}
        for sample in self.samples:
            result[sample.label] = result.get(sample.label, 0) + 1
        return result

    def split_counts(self) -> dict[str, int]:
        result = {"train": 0, "val": 0, "test": 0}
        for sample in self.samples:
            result[sample.split] = result.get(sample.split, 0) + 1
        return result


def _shapes(document: dict) -> list[tuple[int, Box]]:
    result: list[tuple[int, Box]] = []
    for shape in document.get("shapes", []):
        try:
            class_id = int(shape.get("class_id", 0))
        except (TypeError, ValueError):
            continue
        shape_type = str(shape.get("type", "rect"))
        if shape_type == "rect":
            values = shape.get("rect", [])
            if len(values) != 4:
                continue
            x, y, w, h = (float(value) for value in values)
        else:
            points = shape.get("points", [])
            if len(points) < 3:
                continue
            xs = [float(point[0]) for point in points]
            ys = [float(point[1]) for point in points]
            x, y = min(xs), min(ys)
            w, h = max(xs) - x, max(ys) - y
        if w > 1.0 and h > 1.0:
            result.append((class_id, Box(x, y, w, h)))
    return result


def _crop_square(
    image: Image.Image, box: Box, context: float, rng: random.Random | None = None
) -> Image.Image:
    """Crop a square around *box* with relative context, clipped to the image.

    When *rng* is given, the context is jittered and the box centre is nudged
    inside the window. This teaches the detector that a target can appear at
    any position and scale inside a sliding window -- without it, training
    patches are tightly cropped while inference windows contain lots of
    background, and the score distribution does not transfer.
    """
    width, height = image.size
    cx = box.x + box.w / 2.0
    cy = box.y + box.h / 2.0
    effective_context = context
    jitter_x = jitter_y = 0.0
    if rng is not None:
        effective_context = max(0.05, context * rng.uniform(0.6, 1.8))
        jitter_x = rng.uniform(-0.12, 0.12)
        jitter_y = rng.uniform(-0.12, 0.12)
    side = max(box.w, box.h) * (1.0 + 2.0 * effective_context)
    side = max(side, 8.0)
    centre_x = cx + jitter_x * side
    centre_y = cy + jitter_y * side
    x0 = max(0, int(round(centre_x - side / 2.0)))
    y0 = max(0, int(round(centre_y - side / 2.0)))
    x1 = min(width, x0 + int(round(side)))
    y1 = min(height, y0 + int(round(side)))
    x0 = max(0, x1 - int(round(side)))
    y0 = max(0, y1 - int(round(side)))
    if x1 <= x0 or y1 <= y0:
        return image.crop((0, 0, min(width, 8), min(height, 8)))
    return image.crop((x0, y0, x1, y1))


def build_patches(
    snapshot: str | Path,
    output: str | Path,
    patch_size: int = 64,
    context: float = 0.15,
    negatives_per_image: int = 3,
    positives_per_box: int = 3,
    seed: int = 2026,
) -> PatchInfo:
    root = Path(snapshot)
    manifest_path = root / "dataset.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"not a figtools snapshot: {root}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    id_to_name: dict[int, str] = {}
    for entry in manifest.get("classes", []):
        id_to_name[int(entry.get("id", 0))] = str(entry.get("name", entry.get("id")))
    if not id_to_name:
        raise ValueError("snapshot has no classes; annotate some images first")

    class_names = sorted(id_to_name.values())
    all_classes = class_names + [BACKGROUND]
    out_dir = Path(output)
    for name in all_classes:
        (out_dir / name).mkdir(parents=True, exist_ok=True)

    rng = random.Random(seed)
    info = PatchInfo(classes=all_classes)
    counter = 0

    for item in manifest.get("items", []):
        image_name = item.get("image", "")
        annotation_name = item.get("annotation", "")
        split = str(item.get("split", "train"))
        if split not in {"train", "val", "test"}:
            split = "train"
        image_path = root / "images" / image_name
        annotation_path = root / "annotations" / annotation_name
        if not image_path.is_file() or not annotation_path.is_file():
            info.skipped += 1
            continue

        try:
            document = json.loads(annotation_path.read_text(encoding="utf-8"))
            image = Image.open(image_path).convert("RGB")
        except (OSError, ValueError):
            info.skipped += 1
            continue

        shapes = _shapes(document)
        annotated_boxes: list[Box] = []
        for class_id, box in shapes:
            if class_id not in id_to_name:
                continue
            annotated_boxes.append(box)
            for _ in range(max(1, positives_per_box)):
                patch = _crop_square(image, box, context, rng).resize(
                    (patch_size, patch_size), Image.Resampling.BILINEAR
                )
                counter += 1
                filename = f"{Path(image_name).stem}_p{counter:03d}.png"
                target = out_dir / id_to_name[class_id] / filename
                patch.save(target)
                info.samples.append(Sample(target, id_to_name[class_id], split))
                info.positives += 1

        if not annotated_boxes:
            info.skipped += 1
            continue
        info.images += 1

        # Background: random square crops with low overlap to every annotation.
        width, height = image.size
        side = max(12, int(patch_size * 1.2))
        attempts = 0
        found = 0
        while found < negatives_per_image and attempts < negatives_per_image * 40:
            attempts += 1
            x0 = rng.randint(0, max(0, width - side))
            y0 = rng.randint(0, max(0, height - side))
            candidate = Box(float(x0), float(y0), float(side), float(side))
            if any(candidate.iou(box) > 0.08 for box in annotated_boxes):
                continue
            patch = image.crop((x0, y0, x0 + side, y0 + side)).resize(
                (patch_size, patch_size), Image.Resampling.BILINEAR
            )
            counter += 1
            filename = f"{Path(image_name).stem}_b{counter:03d}.png"
            target = out_dir / BACKGROUND / filename
            patch.save(target)
            info.samples.append(Sample(target, BACKGROUND, split))
            info.negatives += 1
            found += 1

    if info.positives == 0:
        raise ValueError("no usable annotations found; draw some boxes on the dataset first")
    if info.negatives == 0:
        raise ValueError("could not sample any background patches")

    (out_dir / "patches.json").write_text(
        json.dumps(
            {
                "format": "figtools-patches/1",
                "source": str(root.resolve()),
                "patch_size": int(patch_size),
                "context": float(context),
                "classes": all_classes,
                "positive_classes": class_names,
                "background": BACKGROUND,
                "counts": info.counts(),
                "splits": info.split_counts(),
                "positives": info.positives,
                "positives_per_box": int(positives_per_box),
                "negatives": info.negatives,
                "images": info.images,
                "skipped": info.skipped,
                "seed": int(seed),
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    return info
