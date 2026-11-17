"""End-to-end check of the annotation -> YOLO training pipeline.

Mirrors what the GUI does:
  1. images + .annotations.json sidecars (the format `saveSidecar` writes and
     `Dataset::scan` reads; covered by tests/test_dataset.cpp)
  2. a figtools dataset snapshot (dataset.json + images/ + annotations/),
     the exact on-disk layout `Dataset::export_snapshot` produces
  3. ft_train.yolo_data.export_yolo -> YOLO data.yaml
  4. one short YOLO training run -> structured epoch/done events

Run with the project's Python env::

    python scripts/verify_annotation_pipeline.py --train
"""

from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def sidecar_document(width: int, height: int, class_id: int) -> dict:
    """Same schema as AnnotationSet::to_json / yolo_data._shape_boxes."""
    box_w, box_h = width * 0.45, height * 0.35
    x, y = width * 0.25, height * 0.3
    return {
        "version": 1,
        "image": {"width": width, "height": height},
        "classes": [
            {"id": 0, "name": "normal", "color": "#1D9E75"},
            {"id": 1, "name": "defect", "color": "#D85A30"},
        ],
        "shapes": [
            {"class_id": class_id, "type": "rect", "rect": [x, y, box_w, box_h]},
            {
                "class_id": class_id,
                "type": "polygon",
                "points": [
                    [width * 0.1, height * 0.1],
                    [width * 0.3, height * 0.12],
                    [width * 0.28, height * 0.25],
                    [width * 0.12, height * 0.22],
                ],
            },
        ],
    }


def build_snapshot(root: Path, count: int = 8) -> Path:
    snapshot = root / "snapshot"
    images_dir = snapshot / "images"
    annotations_dir = snapshot / "annotations"
    splits_dir = snapshot / "splits"
    for directory in (images_dir, annotations_dir, splits_dir):
        directory.mkdir(parents=True, exist_ok=True)

    rng = random.Random(2026)
    items = []
    for index in range(count):
        width, height = 320, 256
        colour = (30 + index * 12, 40, 60)
        image = Image.new("RGB", (width, height), colour)
        draw = ImageDraw.Draw(image)
        draw.rectangle([72, 76, 72 + 144, 76 + 88], outline=(240, 200, 120), width=3)
        name = f"anno_{index:03d}.png"
        image.save(images_dir / name)

        class_id = index % 2
        document = sidecar_document(width, height, class_id)
        annotation_name = name + ".json"
        (annotations_dir / annotation_name).write_text(
            json.dumps(document, indent=2), encoding="utf-8"
        )
        split = "val" if index == count - 1 else "train"
        items.append(
            {"image": name, "annotation": annotation_name, "split": split}
        )
        (splits_dir / (split + ".txt")).open("a", encoding="utf-8").write(name + "\n")

    manifest = {
        "version": 1,
        "name": "verify-annotations",
        "classes": [
            {"id": 0, "name": "normal"},
            {"id": 1, "name": "defect"},
        ],
        "items": items,
    }
    (snapshot / "dataset.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8"
    )
    return snapshot


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="build/annotation_pipeline_check")
    parser.add_argument("--train", action="store_true", help="also run one YOLO epoch")
    args = parser.parse_args()

    root = Path(args.out).resolve()
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True, exist_ok=True)

    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
    from ft_train.yolo_data import export_yolo

    snapshot = build_snapshot(root)
    yolo_root = root / "yolo"
    exported = export_yolo(snapshot, yolo_root)
    data_yaml = Path(exported.data_yaml)
    print(f"snapshot:  {snapshot}")
    print(f"classes:   {exported.classes}")
    print(f"images:    {exported.images}  labels: {exported.labels}")
    assert data_yaml.is_file(), "data.yaml not created"
    assert sum(exported.images.values()) >= 8, "images were not exported"
    assert sum(exported.labels.values()) >= 16, "labels were not exported"

    # Verify one label file reflects both shapes in its sidecar.
    first_label = yolo_root / "labels" / "train" / "anno_000.txt"
    lines = first_label.read_text(encoding="utf-8").splitlines()
    assert len(lines) == 2, f"expected 2 boxes in {first_label}, got {lines}"
    print(f"label[0]:  {lines[0]}")
    print(f"label[1]:  {lines[1]}")

    if args.train:
        import subprocess

        job = root / "job.json"
        job.write_text(
            json.dumps(
                {
                    "data": str(yolo_root),
                    "data_yaml": str(data_yaml),
                    "output": str(root / "model"),
                    "variant": "yolo11n.yaml",
                    "imgsz": 320,
                    "epochs": 1,
                    "batch_size": 4,
                    "device": "cuda",
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        process = subprocess.run(
            [sys.executable, str(root.parents[1] / "python" / "train_yolo.py"),
             "--job", str(job)],
            capture_output=True,
            text=True,
            timeout=600,
        )
        print(process.stdout[-1200:])
        assert process.returncode == 0, process.stderr[-800:]
        assert '"event": "done"' in process.stdout, "training did not finish"

    print("annotation pipeline OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
