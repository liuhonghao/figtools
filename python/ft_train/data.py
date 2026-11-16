"""Dataset discovery for the trainer.

Two layouts are accepted:

1. **figtools snapshot** -- what the desktop app exports:
   ``images/  annotations/  splits/  dataset.json``.
   The label of an image is its most frequent annotation class; images without
   annotations are skipped, because guessing a label is worse than dropping it.

2. **Class folders** -- the usual ``root/<class>/<image>`` tree used by
   ImageFolder. Classes are sorted so runs are reproducible.
"""

from __future__ import annotations

import json
import random
from dataclasses import dataclass, field
from pathlib import Path

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".ppm", ".pgm", ".tif", ".tiff", ".webp"}


@dataclass
class Sample:
    path: Path
    label: str
    split: str = "train"


@dataclass
class DatasetInfo:
    classes: list[str]
    samples: list[Sample] = field(default_factory=list)
    skipped: int = 0
    source: str = "folders"

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


def _images_in(directory: Path) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        path for path in directory.iterdir() if path.is_file() and path.suffix.lower() in IMAGE_SUFFIXES
    )


def _dominant_class(annotation_path: Path) -> int | None:
    try:
        document = json.loads(annotation_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    counts: dict[int, int] = {}
    for shape in document.get("shapes", []):
        class_id = int(shape.get("class_id", 0))
        counts[class_id] = counts.get(class_id, 0) + 1
    if not counts:
        return None
    # Most frequent wins; ties go to the smaller id so the choice is stable.
    return max(counts.items(), key=lambda item: (item[1], -item[0]))[0]


def _from_snapshot(root: Path) -> DatasetInfo:
    document = json.loads((root / "dataset.json").read_text(encoding="utf-8"))
    id_to_name: dict[int, str] = {}
    for entry in document.get("classes", []):
        id_to_name[int(entry.get("id", 0))] = str(entry.get("name", entry.get("id")))

    info = DatasetInfo(classes=sorted(id_to_name.values()), source="snapshot")
    images_dir = root / "images"
    annotations_dir = root / "annotations"

    for item in document.get("items", []):
        image_name = item.get("image")
        if not image_name:
            continue
        image_path = images_dir / image_name
        if not image_path.is_file():
            info.skipped += 1
            continue
        annotation_name = item.get("annotation")
        label_id = None
        if annotation_name:
            label_id = _dominant_class(annotations_dir / annotation_name)
        if label_id is None or label_id not in id_to_name:
            info.skipped += 1
            continue
        split = str(item.get("split", "train"))
        if split not in {"train", "val", "test"}:
            split = "train"
        info.samples.append(Sample(image_path, id_to_name[label_id], split))

    return info


def _from_folders(root: Path, val_ratio: float, test_ratio: float, seed: int) -> DatasetInfo:
    class_dirs = sorted(
        directory for directory in root.iterdir() if directory.is_dir() and _images_in(directory)
    )
    info = DatasetInfo(classes=[directory.name for directory in class_dirs], source="folders")
    rng = random.Random(seed)

    for directory in class_dirs:
        files = _images_in(directory)
        rng.shuffle(files)
        total = len(files)
        val = max(1, round(total * val_ratio)) if total >= 3 else 0
        test = max(1, round(total * test_ratio)) if total >= 4 else 0
        val = min(val, max(0, total - 1))
        test = min(test, max(0, total - val - 1))
        for index, path in enumerate(files):
            if index < val:
                split = "val"
            elif index < val + test:
                split = "test"
            else:
                split = "train"
            info.samples.append(Sample(path, directory.name, split))

    return info


def discover(root: str | Path, val_ratio: float = 0.2, test_ratio: float = 0.0, seed: int = 2026) -> DatasetInfo:
    root_path = Path(root)
    if not root_path.is_dir():
        raise FileNotFoundError(f"dataset directory not found: {root_path}")

    if (root_path / "dataset.json").is_file() and (root_path / "images").is_dir():
        info = _from_snapshot(root_path)
    else:
        info = _from_folders(root_path, val_ratio, test_ratio, seed)

    if not info.classes:
        raise ValueError(
            "no classes found: expected <root>/<class>/<images> or a figtools snapshot "
            "with dataset.json"
        )
    if len(info.classes) < 2:
        raise ValueError(f"need at least two classes, found {len(info.classes)}")
    return info
