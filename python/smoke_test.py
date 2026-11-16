#!/usr/bin/env python
"""End-to-end smoke test for the Python worker (requires torch).

    python smoke_test.py

It generates a tiny two-class dataset, trains for three epochs, reads the
model package and predicts one image. The desktop workbench uses exactly the
same two entry points.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent


def make_dataset(root: Path) -> Path:
    data = root / "data"
    for name, colour, stripe in (
        ("alpha", (200, 60, 50), (255, 240, 230)),
        ("beta", (40, 90, 190), (20, 30, 40)),
    ):
        folder = data / name
        folder.mkdir(parents=True, exist_ok=True)
        for index in range(8):
            image = Image.new("RGB", (64, 64), colour)
            draw = ImageDraw.Draw(image)
            for step in range(8):
                x = step * 8 + index
                draw.rectangle([x, 0, x + 3, 63], fill=stripe)
            image.save(folder / f"{index:02d}.png")
    return data


def make_detection_snapshot(root: Path) -> Path:
    """A tiny annotated snapshot: red squares and blue discs on paper."""
    import random

    rng = random.Random(11)
    snapshot = root / "detect_data"
    (snapshot / "images").mkdir(parents=True, exist_ok=True)
    (snapshot / "annotations").mkdir(parents=True, exist_ok=True)
    classes = [
        {"id": 0, "name": "red_box", "color": "#B4562A"},
        {"id": 1, "name": "blue_circle", "color": "#3F6E8C"},
    ]
    items = []
    size = 96
    for index in range(16):
        image = Image.new("RGB", (size, size), (236 + rng.randint(-4, 4), 230 + rng.randint(-4, 4), 216))
        draw = ImageDraw.Draw(image)
        shapes = []
        for _ in range(rng.randint(1, 2)):
            if rng.random() < 0.5:
                side = rng.randint(18, 26)
                x = rng.randint(6, size - side - 6)
                y = rng.randint(6, size - side - 6)
                draw.rectangle([x, y, x + side, y + side], fill=(190, 60, 40), outline=(110, 38, 24), width=2)
                shapes.append({"class_id": 0, "type": "rect", "confidence": 1.0, "rect": [x, y, side, side]})
            else:
                radius = rng.randint(9, 14)
                cx = rng.randint(radius + 6, size - radius - 6)
                cy = rng.randint(radius + 6, size - radius - 6)
                draw.ellipse(
                    [cx - radius, cy - radius, cx + radius, cy + radius],
                    fill=(45, 90, 160), outline=(25, 55, 110), width=2,
                )
                shapes.append({
                    "class_id": 1, "type": "rect", "confidence": 1.0,
                    "rect": [cx - radius, cy - radius, 2 * radius, 2 * radius],
                })
        name = f"scene_{index:02d}.png"
        image.save(snapshot / "images" / name)
        annotation = {"version": 1, "image_width": size, "image_height": size,
                      "classes": classes, "shapes": shapes}
        annotation_name = name + ".json"
        (snapshot / "annotations" / annotation_name).write_text(
            json.dumps(annotation, ensure_ascii=False), encoding="utf-8"
        )
        split = "train" if index < 11 else ("val" if index < 14 else "test")
        items.append({"image": name, "annotation": annotation_name,
                      "width": size, "height": size, "split": split})
    manifest = {"name": "detect_smoke", "format": "figtools-dataset/1",
                "classes": classes, "items": items}
    (snapshot / "dataset.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    return snapshot


def smoke_detector(root: Path) -> str:
    """Train a patch detector and assert it finds a target in a new image."""
    snapshot = make_detection_snapshot(root)
    output = root / "detect_model"
    job_path = root / "detect_job.json"
    job_path.write_text(
        json.dumps({
            "data": str(snapshot), "output": str(output), "arch": "smallcnn",
            "input_size": 48, "patch_size": 48, "context": 0.35,
            "positives_per_box": 3, "negatives": 3,
            "epochs": 6, "batch_size": 16, "lr": 0.002, "seed": 11, "device": "cpu",
        }, ensure_ascii=False),
        encoding="utf-8",
    )
    train = subprocess.run(
        [sys.executable, str(ROOT / "train_detect.py"), "--job", str(job_path)],
        cwd=ROOT, capture_output=True, text=True, timeout=300, check=False,
    )
    if train.returncode != 0:
        sys.stderr.write(train.stdout + train.stderr)
        raise RuntimeError("detector training failed")
    metadata = json.loads((output / "model.json").read_text(encoding="utf-8"))
    if metadata.get("kind") != "detector":
        raise RuntimeError("detector package has wrong kind")

    probe = root / "detect_probe.png"
    image = Image.new("RGB", (128, 96), (236, 230, 216))
    ImageDraw.Draw(image).rectangle([24, 26, 58, 60], fill=(190, 60, 40), outline=(110, 38, 24), width=2)
    image.save(probe)

    predict = subprocess.run(
        [sys.executable, str(ROOT / "detect.py"), "--model", str(output),
         "--image", str(probe), "--threshold", "0.5"],
        cwd=ROOT, capture_output=True, text=True, timeout=300, check=False,
    )
    if predict.returncode != 0:
        sys.stderr.write(predict.stdout + predict.stderr)
        raise RuntimeError("detection failed")
    result = json.loads(predict.stdout)
    if not result["detections"]:
        raise RuntimeError("detector found no target in the probe image")
    labels = {box["label"] for box in result["detections"]}
    if "red_box" not in labels:
        raise RuntimeError(f"expected red_box detection, got {labels}")
    return f"{len(result['detections'])} boxes, {result['ms']} ms"


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="figtools_smoke_") as temporary:
        root = Path(temporary)
        data = make_dataset(root)
        output = root / "model"
        job_path = root / "job.json"
        job = {
            "data": str(data),
            "output": str(output),
            "arch": "smallcnn",
            "input_size": 48,
            "epochs": 4,
            "batch_size": 8,
            "lr": 0.01,
            "seed": 11,
            "device": "cpu",
            "val_ratio": 0.25,
        }
        job_path.write_text(json.dumps(job, ensure_ascii=False, indent=2), encoding="utf-8")

        trained = subprocess.run(
            [sys.executable, str(ROOT / "train.py"), "--job", str(job_path)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        if trained.returncode != 0:
            sys.stderr.write(trained.stdout + trained.stderr)
            return 1

        events = [
            json.loads(line)
            for line in trained.stdout.splitlines()
            if line.strip().startswith("{")
        ]
        if not any(event.get("event") == "done" for event in events):
            sys.stderr.write("training did not emit a 'done' event\n")
            return 1
        for required in ("model.pt", "model.json", "metrics.json", "job.json"):
            if not (output / required).is_file():
                sys.stderr.write(f"missing model package file: {required}\n")
                return 1

        predicted = subprocess.run(
            [
                sys.executable,
                str(ROOT / "predict.py"),
                "--model",
                str(output),
                "--image",
                str(data / "alpha" / "00.png"),
                "--topk",
                "3",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=120,
            check=False,
        )
        if predicted.returncode != 0:
            sys.stderr.write(predicted.stdout + predicted.stderr)
            return 1
        result = json.loads(predicted.stdout.strip().splitlines()[-1])
        if result.get("label") != "alpha":
            sys.stderr.write(f"unexpected prediction: {result}\n")
            return 1
        # Batch inference: one process for the whole folder.
        batch_job = root / "batch_job.json"
        batch_csv = root / "predictions.csv"
        batch_job.write_text(
            json.dumps(
                {
                    "model": str(output),
                    "input": str(data),
                    "output": str(batch_csv),
                    "recursive": True,
                    "topk": 2,
                    "batch_size": 8,
                    "device": "cpu",
                }
            ),
            encoding="utf-8",
        )
        batch = subprocess.run(
            [sys.executable, str(ROOT / "predict_batch.py"), "--job", str(batch_job)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        if batch.returncode != 0:
            sys.stderr.write(batch.stdout + batch.stderr)
            return 1
        batch_events = [
            json.loads(line)
            for line in batch.stdout.splitlines()
            if line.strip().startswith("{")
        ]
        if not any(event.get("event") == "done" for event in batch_events):
            sys.stderr.write("batch inference did not emit a 'done' event\n")
            return 1
        if not batch_csv.is_file() or len(batch_csv.read_text(encoding="utf-8-sig").splitlines()) != 17:
            sys.stderr.write("batch CSV should contain a header and 16 predictions\n")
            return 1

        detector_info = smoke_detector(root)
        print(
            "smoke ok: "
            f"{len(events)} train events, label={result['label']}, "
            f"confidence={result['confidence']}, {result['ms']} ms; "
            f"batch: 16 rows in {batch_events[-1]['seconds']} s; "
            f"detector: {detector_info}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
