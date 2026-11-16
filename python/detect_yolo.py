#!/usr/bin/env python
"""YOLO inference over one image, in the figtools detection contract.

Loads a YOLO model package (``kind: yolo``), runs Ultralytics on one image and
prints the same JSON shape as ``detect.py`` so the desktop canvas needs no
changes::

    {"image": ..., "width": ..., "height": ..., "detections": [
        {"label": ..., "confidence": ..., "x": ..., "y": ..., "w": ..., "h": ...}
    ], "scanned": N, "ms": ...}

Labels are mapped from YOLO indices back to figtools class names using the
mapping stored in model.json.

    python detect_yolo.py --model models/yolo-run1 --image photos/part.png \\
        --conf 0.25 --iou 0.45
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools YOLO detector")
    parser.add_argument("--model", required=True, help="YOLO model package directory")
    parser.add_argument("--image", required=True, help="image to run")
    parser.add_argument("--conf", type=float, default=0.25, help="confidence threshold")
    parser.add_argument("--iou", type=float, default=0.45, help="NMS IoU threshold")
    parser.add_argument("--imgsz", type=int, default=0, help="override inference size (0 = model default)")
    parser.add_argument("--device", default="auto")
    args = parser.parse_args()

    started = time.time()
    try:
        from ultralytics import YOLO
    except ImportError:
        sys.stderr.write("error: ultralytics is not installed in this Python environment\n")
        return 3

    package = Path(args.model)
    metadata_path = package / "model.json"
    if not metadata_path.is_file():
        sys.stderr.write(f"error: not a model package: {package}\n")
        return 2
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    if str(metadata.get("kind", "")) != "yolo":
        sys.stderr.write("error: model is not a YOLO package (kind != yolo)\n")
        return 2

    weights = package / str(metadata.get("weights", "best.pt"))
    if not weights.is_file():
        sys.stderr.write(f"error: missing weights: {weights}\n")
        return 2

    # Map YOLO's contiguous indices back to the figtools class ids/names.
    id_map = metadata.get("yolo_index_to_figtools_id", {})
    classes = list(metadata.get("classes", []))
    name_by_index = {
        int(index): (classes[int(index)] if int(index) < len(classes) else str(index))
        for index in range(len(classes))
    }

    try:
        with Image.open(args.image) as probe:
            width, height = probe.size
    except OSError as error:
        sys.stderr.write(f"error: cannot read image: {error}\n")
        return 2

    try:
        model = YOLO(str(weights))
        predict_kwargs = {
            "source": args.image,
            "conf": max(0.0, min(1.0, args.conf)),
            "iou": max(0.0, min(1.0, args.iou)),
            "verbose": False,
            "device": args.device if args.device != "auto" else None,
        }
        if args.imgsz > 0:
            predict_kwargs["imgsz"] = args.imgsz
        results = model.predict(**predict_kwargs)
    except (RuntimeError, ValueError) as error:
        sys.stderr.write(f"error: YOLO inference failed: {error}\n")
        return 2

    detections: list[dict] = []
    if results:
        result = results[0]
        boxes = getattr(result, "boxes", None)
        if boxes is not None:
            for xyxy, confidence, class_id in zip(boxes.xyxy, boxes.conf, boxes.cls):
                x1, y1, x2, y2 = (float(value) for value in xyxy)
                index = int(class_id)
                # Prefer the figtools class name via the remap; fall back to the
                # YOLO index name if the package predates that field.
                figtools_id = id_map.get(str(index))
                label = name_by_index.get(index, str(index))
                if figtools_id is not None and int(figtools_id) < len(classes):
                    label = classes[int(figtools_id)]
                detections.append(
                    {
                        "label": label,
                        "confidence": round(float(confidence), 4),
                        "x": round(x1, 1),
                        "y": round(y1, 1),
                        "w": round(x2 - x1, 1),
                        "h": round(y2 - y1, 1),
                    }
                )
    detections.sort(key=lambda box: box["confidence"], reverse=True)

    result_json = {
        "image": str(Path(args.image).resolve()),
        "width": width,
        "height": height,
        "detections": detections,
        "scanned": len(detections),
        "conf": args.conf,
        "iou": args.iou,
        "ms": round((time.time() - started) * 1000.0, 1),
    }
    sys.stdout.write(json.dumps(result_json, ensure_ascii=False) + "\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
