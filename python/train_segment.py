#!/usr/bin/env python
"""Train a YOLO instance-segmentation model from figtools polygon annotations.

YOLO-seg wants one label line per instance::

    class_id x1 y1 x2 y2 x3 y3 ...     (normalised polygon vertices)

figtools stores polygons (or rectangles) in native sidecars. Rectangles are
converted to four corner points. Shapes without at least three points are
skipped. The model package is the same shape as train_yolo.py with
``kind: "yolo-seg"`` so the host can route inference to the mask overlay.

    python train_segment.py --job job.json
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
import traceback
from pathlib import Path

from ft_train.events import emit, fail, log


def export_segment_dataset(snapshot: str | Path, output: str | Path):
    root = Path(snapshot)
    manifest_path = root / "dataset.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"not a figtools snapshot: {root}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    originals = sorted({int(e.get("id", 0)) for e in manifest.get("classes", [])})
    id_to_name = {int(e.get("id", 0)): str(e.get("name", e.get("id")))
                  for e in manifest.get("classes", [])}
    if not originals:
        raise ValueError("snapshot has no classes")
    id_map = {original: index for index, original in enumerate(originals)}
    classes = [id_to_name[o] for o in originals]

    out = Path(output)
    splits = ("train", "val", "test")
    for split in splits:
        (out / "images" / split).mkdir(parents=True, exist_ok=True)
        (out / "labels" / split).mkdir(parents=True, exist_ok=True)

    image_counts = {s: 0 for s in splits}
    label_counts = {s: 0 for s in splits}
    skipped = 0

    for item in manifest.get("items", []):
        name = str(item.get("image", ""))
        annotation = str(item.get("annotation", ""))
        split = str(item.get("split", "train"))
        if split not in splits:
            split = "train"
        source = root / "images" / name
        sidecar = root / "annotations" / annotation
        if not source.is_file() or not sidecar.is_file():
            skipped += 1
            continue
        try:
            document = json.loads(sidecar.read_text(encoding="utf-8"))
            from PIL import Image

            with Image.open(source) as image:
                width, height = image.size
        except (OSError, ValueError):
            skipped += 1
            continue

        target = out / "images" / split / name
        if target.resolve() != source.resolve():
            shutil.copyfile(source, target)
        image_counts[split] += 1

        lines: list[str] = []
        for shape in document.get("shapes", []):
            try:
                class_id = int(shape.get("class_id", 0))
            except (TypeError, ValueError):
                continue
            if class_id not in id_map:
                continue
            if str(shape.get("type", "rect")) == "rect":
                rect = shape.get("rect", [])
                if len(rect) != 4:
                    continue
                x, y, w, h = (float(v) for v in rect)
                points = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
            else:
                raw = shape.get("points", [])
                points = [(float(p[0]), float(p[1])) for p in raw]
            if len(points) < 3:
                continue
            coords = []
            for px, py in points:
                coords.extend([f"{max(0.0, min(1.0, px / width)):.6f}",
                               f"{max(0.0, min(1.0, py / height)):.6f}"])
            lines.append(f"{id_map[class_id]} " + " ".join(coords))
        (out / "labels" / split / (Path(name).stem + ".txt")).write_text(
            "\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")
        label_counts[split] += len(lines)

    if image_counts["train"] == 0 or label_counts["train"] == 0:
        raise ValueError("no segmentation labels found; draw polygons in the annotation tab")

    yaml_lines = [
        f"path: {out.resolve().as_posix()}",
        "train: images/train",
        "val: images/" + ("val" if image_counts["val"] else "train"),
        f"nc: {len(classes)}",
        "names:",
    ]
    yaml_lines.extend(f"  {i}: {name}" for i, name in enumerate(classes))
    data_yaml = out / "data.yaml"
    data_yaml.write_text("\n".join(yaml_lines) + "\n", encoding="utf-8")
    (out / "classes.json").write_text(json.dumps({
        "names": classes,
        "figtools_id_to_yolo_index": {str(k): v for k, v in id_map.items()},
        "yolo_index_to_figtools_id": {str(v): k for k, v in id_map.items()},
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    return {"data_yaml": data_yaml, "classes": classes,
            "images": image_counts, "labels": label_counts, "skipped": skipped}


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools YOLO-seg trainer")
    parser.add_argument("--job", required=True)
    args = parser.parse_args()

    try:
        from ultralytics import YOLO
    except ImportError:
        fail("ultralytics is not installed")
        return 3

    try:
        job = json.loads(Path(args.job).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(f"cannot read job: {error}")
        return 2

    data_root = job.get("data", "")
    output = job.get("output", "")
    if not data_root or not output:
        fail("job must contain 'data' and 'output'")
        return 2
    target = Path(output)
    target.mkdir(parents=True, exist_ok=True)
    source = Path(args.job)
    if source.resolve() != (target / "job.json").resolve():
        shutil.copyfile(source, target / "job.json")

    variant = str(job.get("variant", "yolo11n-seg.yaml"))
    epochs = max(1, int(job.get("epochs", 40)))
    input_size = int(job.get("imgsz", job.get("input_size", 640)))
    batch_size = int(job.get("batch_size", 8))
    seed = int(job.get("seed", 2026))
    device = str(job.get("device", "auto"))

    emit("phase", name="export")
    try:
        exported = export_segment_dataset(data_root, target / "dataset")
    except (OSError, ValueError) as error:
        fail(str(error))
        return 2
    emit("dataset", classes=exported["classes"], counts=exported["labels"],
         images=exported["images"], skipped=exported["skipped"], source="seg")

    history: list[dict] = []
    started = time.time()

    def on_fit_epoch_end(trainer) -> None:  # noqa: ANN001
        metrics = getattr(trainer, "metrics", {}) or {}
        epoch = int(getattr(trainer, "epoch", 0)) + 1
        if epoch > epochs:
            return
        map50 = metrics.get("metrics/mAP50(M)", metrics.get("mAP50", 0.0))
        map5095 = metrics.get("metrics/mAP50-95(M)", metrics.get("mAP50-95", 0.0))
        record = {"epoch": epoch, "total_epochs": epochs,
                  "mAP50": round(float(map50), 4),
                  "mAP50_95": round(float(map5095), 4),
                  "box_loss": 0.0, "cls_loss": 0.0}
        history.append(record)
        emit("epoch", **record)

    try:
        model = YOLO(variant)
        model.add_callback("on_fit_epoch_end", on_fit_epoch_end)
        model.train(data=str(exported["data_yaml"]), epochs=epochs, imgsz=input_size,
                    batch=batch_size, seed=seed,
                    device=device if device != "auto" else None,
                    project=str(target / "run"), name="train", exist_ok=True,
                    verbose=False, plots=False)
        best = target / "run" / "train" / "weights" / "best.pt"
        if not best.is_file():
            fail("training finished without best.pt")
            return 2
        shutil.copyfile(best, target / "best.pt")
        metrics = {}
        try:
            validation = model.val(data=str(exported["data_yaml"]), verbose=False, plots=False)
            metrics = {"mAP50": round(float(validation.box.map50), 4),
                       "mAP50_95": round(float(validation.box.map), 4)}
        except (AttributeError, RuntimeError, ValueError) as error:
            log(f"warning: could not read validation metrics: {error}")
    except Exception as error:  # noqa: BLE001
        traceback.print_exc()
        fail(f"YOLO-seg training failed: {error}")
        return 2

    classes_json = json.loads((target / "dataset" / "classes.json").read_text(encoding="utf-8"))
    metadata = {
        "format": "figtools-model/1", "kind": "yolo-seg", "framework": "ultralytics",
        "variant": variant, "arch": f"YOLO-seg ({Path(variant).stem})",
        "classes": classes_json["names"],
        "yolo_index_to_figtools_id": classes_json["yolo_index_to_figtools_id"],
        "input_size": input_size, "weights": "best.pt", "metrics": metrics,
        "seconds": round(time.time() - started, 2),
    }
    (target / "model.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2),
                                       encoding="utf-8")
    (target / "metrics.json").write_text(
        json.dumps({"history": history, "metrics": metrics}, ensure_ascii=False, indent=2),
        encoding="utf-8")
    emit("done", model_dir=str(target.resolve()), mAP50=metrics.get("mAP50"),
         mAP50_95=metrics.get("mAP50_95"), seconds=metadata["seconds"],
         classes=metadata["classes"])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001
        traceback.print_exc()
        fail(str(error))
        sys.exit(2)
