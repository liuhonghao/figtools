"""Convert the TTPLA power-line dataset (LabelMe JSON) into YOLO layout.

TTPLA ships one LabelMe ``<name>.json`` per 3840x2160 JPEG. Every annotation is
a ``polygon``; power lines are drawn as two-point "polygons" (segments), which
would give a near-zero-height bounding box that YOLO cannot learn. This
converter:

  * normalises class names (``Cable`` and ``cable`` are the same class),
  * expands thin segments to a usable minimum thickness,
  * splits images 8:1:1 into train/val/test,
  * references originals by absolute path (no 4.7 GB copy),

and writes ``data.yaml`` plus one ``<name>.txt`` per image in YOLO format.

Usage::

    python ttpla_to_yolo.py --src E:/电力线/图像 --out build/ttpla_yolo

Only the Python standard library is needed.
"""

from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path

from PIL import Image

# LabelMe name -> canonical figtools class name (lowercase, singular).
CLASS_ALIASES = {
    "cable": "cable",
    "Cable": "cable",
    "tower_wooden": "tower_wooden",
    "tower_lattice": "tower_lattice",
    "tower_tucohy": "tower_tucohy",
    "void": "void",
}

# Colours for the class list (kept in sync with the figtools palette style).
CLASS_COLOURS = {
    "cable": "#B4562A",
    "tower_wooden": "#A9781A",
    "tower_lattice": "#3F6E8C",
    "tower_tucohy": "#4F7A4A",
    "void": "#918A7C",
}


def bbox_of(points: list[list[float]], image_w: int, image_h: int) -> tuple[float, float, float, float] | None:
    """Bounding box (x, y, w, h) from polygon points, with a minimum thickness.

    A two-point cable segment has zero width or height. YOLO needs a box with
    both dimensions > 0; give thin shapes a small but learnable thickness based
    on the image diagonal (about 0.4% => ~18 px at 3840x2160).
    """
    xs = [float(point[0]) for point in points]
    ys = [float(point[1]) for point in points]
    if len(xs) < 2:
        return None
    x, y = min(xs), min(ys)
    w, h = max(xs) - x, max(ys) - y
    minimum = max(6.0, math.hypot(image_w, image_h) * 0.004)
    if w < minimum:
        grow = (minimum - w) / 2.0
        x -= grow
        w = minimum
    if h < minimum:
        grow = (minimum - h) / 2.0
        y -= grow
        h = minimum
    # Clamp into the image.
    x = max(0.0, min(x, float(image_w - 1)))
    y = max(0.0, min(y, float(image_h - 1)))
    w = min(w, float(image_w) - x)
    h = min(h, float(image_h) - y)
    if w <= 1.0 or h <= 1.0:
        return None
    return x, y, w, h


def main() -> int:
    parser = argparse.ArgumentParser(description="TTPLA LabelMe -> YOLO converter")
    parser.add_argument("--src", required=True, help="directory with TTPLA .jpg and .json")
    parser.add_argument("--out", required=True, help="output YOLO dataset directory")
    parser.add_argument("--seed", type=int, default=2026)
    parser.add_argument("--train", type=float, default=0.8)
    parser.add_argument("--val", type=float, default=0.1)
    parser.add_argument("--img-size", type=int, default=640,
                        help="resize images to this long edge before writing (0=copy originals); "
                             "YOLO only sees this resolution anyway, so downsampling saves GB")
    parser.add_argument("--jpeg-quality", type=int, default=80)
    args = parser.parse_args()

    source = Path(args.src)
    out = Path(args.out)
    if not source.is_dir():
        print(f"error: source directory not found: {source}")
        return 2

    pairs: list[tuple[Path, Path]] = []
    for image in sorted(source.glob("*.jpg")):
        sidecar = image.with_suffix(".json")
        if sidecar.is_file():
            pairs.append((image, sidecar))
    if not pairs:
        print(f"error: no JPEG + JSON pairs found in {source}")
        return 2

    # Discover canonical classes from the data.
    discovered: set[str] = set()
    for _, sidecar in pairs:
        try:
            document = json.loads(sidecar.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        for shape in document.get("shapes", []):
            raw = str(shape.get("label", "")).strip()
            canonical = CLASS_ALIASES.get(raw, raw.lower())
            if canonical:
                discovered.add(canonical)
    classes = sorted(discovered)
    class_index = {name: index for index, name in enumerate(classes)}
    print(f"classes ({len(classes)}): {classes}")

    # Deterministic 8:1:1 split.
    rng = random.Random(args.seed)
    order = list(range(len(pairs)))
    rng.shuffle(order)
    train_count = int(len(order) * args.train)
    val_count = int(len(order) * args.val)
    splits = ["train"] * train_count + ["val"] * val_count + ["test"] * (len(order) - train_count - val_count)
    assignment: dict[int, str] = {}
    for position, index in enumerate(order):
        assignment[index] = splits[position]

    for split in ("train", "val", "test"):
        (out / "images" / split).mkdir(parents=True, exist_ok=True)
        (out / "labels" / split).mkdir(parents=True, exist_ok=True)

    counts = {"train": 0, "val": 0, "test": 0}
    label_counts = {"train": 0, "val": 0, "test": 0}
    skipped = 0
    for index, (image, sidecar) in enumerate(pairs):
        split = assignment[index]
        try:
            document = json.loads(sidecar.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            skipped += 1
            continue
        width = int(document.get("imageWidth", 0))
        height = int(document.get("imageHeight", 0))
        if width <= 0 or height <= 0:
            skipped += 1
            continue

        lines: list[str] = []
        for shape in document.get("shapes", []):
            raw = str(shape.get("label", "")).strip()
            canonical = CLASS_ALIASES.get(raw, raw.lower())
            if canonical not in class_index:
                continue
            points = shape.get("points", [])
            if len(points) < 2:
                continue
            box = bbox_of(points, width, height)
            if box is None:
                continue
            x, y, w, h = box
            lines.append(
                f"{class_index[canonical]} {(x + w / 2) / width:.6f} "
                f"{(y + h / 2) / height:.6f} {w / width:.6f} {h / height:.6f}"
            )

        stem = image.stem
        label_path = out / "labels" / split / f"{stem}.txt"
        label_path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")

        # Ultralytics needs a real images/<split>/ tree: it derives label paths
        # from image paths and ignores absolute-path list files. Downsample to
        # the training resolution so we do not copy 4.7 GB of 4K originals.
        target_image = out / "images" / split / image.name
        try:
            with Image.open(image) as source_image:
                source_image = source_image.convert("RGB")
                if args.img_size > 0 and max(source_image.size) > args.img_size:
                    scale = args.img_size / max(source_image.size)
                    new_size = (
                        max(1, int(source_image.width * scale)),
                        max(1, int(source_image.height * scale)),
                    )
                    source_image = source_image.resize(new_size, Image.Resampling.LANCZOS)
                source_image.save(target_image, quality=args.jpeg_quality)
        except OSError:
            skipped += 1
            continue
        counts[split] += 1
        label_counts[split] += len(lines)

    yaml_lines = [
        f"path: {out.resolve().as_posix()}",
        "train: images/train",
        "val: images/" + ("val" if counts["val"] else "train"),
    ]
    if counts["test"]:
        yaml_lines.append("test: images/test")
    yaml_lines.extend([f"nc: {len(classes)}", "names:"])
    yaml_lines.extend(f"  {index}: {name}" for index, name in enumerate(classes))
    (out / "data.yaml").write_text("\n".join(yaml_lines) + "\n", encoding="utf-8")

    manifest = {
        "source": str(source.resolve()),
        "classes": [
            {"id": index, "name": name, "color": CLASS_COLOURS.get(name, "#655F53")}
            for index, name in enumerate(classes)
        ],
        "images": counts,
        "labels": label_counts,
        "skipped": skipped,
    }
    (out / "conversion.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    print(f"images: {counts}")
    print(f"labels: {label_counts}")
    print(f"skipped: {skipped}")
    print(f"data.yaml: {out / 'data.yaml'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
