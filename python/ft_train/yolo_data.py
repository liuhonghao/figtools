"""Export a figtools dataset snapshot into the YOLO (Ultralytics) layout.

figtools stores annotations as native JSON sidecars with rect / polygon shapes.
YOLO wants one ``<image>.txt`` per image, each line::

    class_id x_center y_center width height     (normalised to 0..1)

This module converts a snapshot::

    images/  annotations/  dataset.json

into::

    <out>/images/{train,val,test}/<name>.png
    <out>/labels/{train,val,test}/<name>.txt
    <out>/data.yaml

Class ids are remapped to a contiguous 0..N-1 range (YOLO requirement); the
mapping and the original ids are recorded in ``classes.json`` so predictions
can be mapped back to the names the user drew.
"""

from __future__ import annotations

import json
import shutil
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from .data import IMAGE_SUFFIXES


@dataclass
class YoloExport:
    root: Path
    data_yaml: Path
    classes: list[str]
    id_map: dict[int, int]          # original figtools id -> YOLO index
    images: dict[str, int]          # split -> count
    labels: dict[str, int]          # split -> annotation count
    skipped: int

    def summary(self) -> dict:
        return {
            "classes": self.classes,
            "images": self.images,
            "labels": self.labels,
            "skipped": self.skipped,
            "root": str(self.root.resolve()),
        }


def _shape_boxes(document: dict) -> list[tuple[int, float, float, float, float]]:
    """Return (class_id, cx, cy, w, h) in pixels for every shape."""
    result: list[tuple[int, float, float, float, float]] = []
    for shape in document.get("shapes", []):
        try:
            class_id = int(shape.get("class_id", 0))
        except (TypeError, ValueError):
            continue
        if str(shape.get("type", "rect")) == "rect":
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
            result.append((class_id, x + w / 2.0, y + h / 2.0, w, h))
    return result


def export_yolo(snapshot: str | Path, output: str | Path) -> YoloExport:
    root = Path(snapshot)
    manifest_path = root / "dataset.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"not a figtools snapshot: {root}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    originals = sorted(
        {int(entry.get("id", 0)) for entry in manifest.get("classes", [])}
    )
    id_to_name: dict[int, str] = {
        int(entry.get("id", 0)): str(entry.get("name", entry.get("id")))
        for entry in manifest.get("classes", [])
    }
    if not originals:
        raise ValueError("snapshot has no classes; annotate some images first")

    id_map = {original: index for index, original in enumerate(originals)}
    classes = [id_to_name.get(original, str(original)) for original in originals]

    out = Path(output)
    splits = ("train", "val", "test")
    for split in splits:
        (out / "images" / split).mkdir(parents=True, exist_ok=True)
        (out / "labels" / split).mkdir(parents=True, exist_ok=True)

    image_counts = {split: 0 for split in splits}
    label_counts = {split: 0 for split in splits}
    skipped = 0

    for item in manifest.get("items", []):
        image_name = str(item.get("image", ""))
        annotation_name = str(item.get("annotation", ""))
        split = str(item.get("split", "train"))
        if split not in splits:
            split = "train"
        source = root / "images" / image_name
        if not source.is_file() or not annotation_name:
            skipped += 1
            continue
        annotation_path = root / "annotations" / annotation_name
        if not annotation_path.is_file():
            skipped += 1
            continue

        try:
            document = json.loads(annotation_path.read_text(encoding="utf-8"))
            with Image.open(source) as image:
                width, height = image.size
        except (OSError, ValueError):
            skipped += 1
            continue
        if width <= 0 or height <= 0:
            skipped += 1
            continue

        target = out / "images" / split / image_name
        if target.resolve() != source.resolve():
            shutil.copyfile(source, target)
        image_counts[split] += 1

        boxes = _shape_boxes(document)
        lines: list[str] = []
        for class_id, cx, cy, box_w, box_h in boxes:
            if class_id not in id_map:
                continue
            yolo_id = id_map[class_id]
            lines.append(
                f"{yolo_id} {cx / width:.6f} {cy / height:.6f} "
                f"{box_w / width:.6f} {box_h / height:.6f}"
            )
        label_path = out / "labels" / split / (Path(image_name).stem + ".txt")
        label_path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
        label_counts[split] += len(lines)

    if image_counts["train"] == 0:
        raise ValueError("no training images found in the snapshot")

    # Ultralytics resolves `path` relative to the yaml file; an absolute path
    # keeps training working regardless of the process working directory.
    yaml_lines = [
        f"path: {out.resolve().as_posix()}",
        "train: images/train",
        # YOLO always needs a validation set; if the snapshot has none, reuse
        # train and record that fact in classes.json instead of silently lying.
        "val: images/" + ("val" if image_counts["val"] else "train"),
    ]
    if image_counts["test"]:
        yaml_lines.append("test: images/test")
    yaml_lines.extend([f"nc: {len(classes)}", "names:"])
    yaml_lines.extend(f"  {index}: {name}" for index, name in enumerate(classes))
    data_yaml = out / "data.yaml"
    data_yaml.write_text("\n".join(yaml_lines) + "\n", encoding="utf-8")

    (out / "classes.json").write_text(
        json.dumps(
            {
                "names": classes,
                "yolo_index_to_figtools_id": {index: original for original, index in id_map.items()},
                "figtools_id_to_yolo_index": {str(k): v for k, v in id_map.items()},
                "val_reuses_train": image_counts["val"] == 0,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )

    return YoloExport(
        root=out,
        data_yaml=data_yaml,
        classes=classes,
        id_map=id_map,
        images=image_counts,
        labels=label_counts,
        skipped=skipped,
    )
