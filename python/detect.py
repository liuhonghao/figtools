#!/usr/bin/env python
"""Sliding-window detection with a figtools patch-classifier model.

Loads a model package produced by ``train_detect.py`` (``kind: detector``),
slides a window over an image at several scales, classifies each crop in a
batch, rejects background / low-score windows and applies per-class NMS.

Prints exactly one JSON object on stdout::

    {"image": ..., "width": ..., "height": ..., "detections": [
        {"label": ..., "confidence": ..., "x": ..., "y": ..., "w": ..., "h": ...}
    ], "scanned": N, "ms": ...}

    python detect.py --model models/det-1 --image photos/part.png \\
        --threshold 0.6 --nms 0.35
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import torch
from PIL import Image
from torchvision import transforms

from ft_train.model import IMAGENET_MEAN, IMAGENET_STD, load_package


def _detector_transform(input_size: int):
    return transforms.Compose(
        [
            transforms.Resize((input_size, input_size)),
            transforms.ToTensor(),
            transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
        ]
    )


def _boxes_at_scale(width: int, height: int, window: int, stride: int):
    """Yield (x, y, w, h) windows covering an image, always including the edge."""
    positions_x = list(range(0, max(1, width - window + 1), stride))
    positions_y = list(range(0, max(1, height - window + 1), stride))
    if not positions_x or positions_x[-1] != max(0, width - window):
        positions_x.append(max(0, width - window))
    if not positions_y or positions_y[-1] != max(0, height - window):
        positions_y.append(max(0, height - window))
    for y in positions_y:
        for x in positions_x:
            yield x, y, min(window, width), min(window, height)


def _nms(boxes: list[dict], threshold: float) -> list[dict]:
    """Greedy NMS, grouped by label. boxes carry x/y/w/h and score."""
    keep: list[dict] = []
    by_label: dict[str, list[dict]] = {}
    for box in boxes:
        by_label.setdefault(box["label"], []).append(box)

    def containment(inner: dict, outer: dict) -> float:
        """Fraction of the smaller box covered by the larger one (0..1)."""
        area = inner["w"] * inner["h"]
        if area <= 0.0:
            return 0.0
        ix = max(
            0.0,
            min(inner["x"] + inner["w"], outer["x"] + outer["w"])
            - max(inner["x"], outer["x"]),
        )
        iy = max(
            0.0,
            min(inner["y"] + inner["h"], outer["y"] + outer["h"])
            - max(inner["y"], outer["y"]),
        )
        return (ix * iy) / area

    for label, group in by_label.items():
        group.sort(key=lambda item: item["confidence"], reverse=True)
        survivors: list[dict] = []
        for candidate in group:
            suppressed = False
            for kept in survivors:
                ix = max(
                    0.0,
                    min(candidate["x"] + candidate["w"], kept["x"] + kept["w"])
                    - max(candidate["x"], kept["x"]),
                )
                iy = max(
                    0.0,
                    min(candidate["y"] + candidate["h"], kept["y"] + kept["h"])
                    - max(candidate["y"], kept["y"]),
                )
                inter = ix * iy
                area = candidate["w"] * candidate["h"] + kept["w"] * kept["h"] - inter
                iou = inter / area if area > 0.0 else 0.0
                # Multi-scale windows over the same target have different sizes,
                # so their IoU can be low even when they are the same object.
                # Suppress when one nearly contains the other as well.
                smaller, larger = (
                    (candidate, kept)
                    if candidate["w"] * candidate["h"] <= kept["w"] * kept["h"]
                    else (kept, candidate)
                )
                contained = containment(smaller, larger) > 0.5
                # Same-class windows whose centres are close are almost always
                # the same object seen at two scales; their offsets come from
                # the stride grid, not from a second target.
                centre_dist = (
                    (candidate["x"] + candidate["w"] / 2.0 - kept["x"] - kept["w"] / 2.0) ** 2
                    + (candidate["y"] + candidate["h"] / 2.0 - kept["y"] - kept["h"] / 2.0) ** 2
                ) ** 0.5
                close_centres = centre_dist < 0.6 * max(candidate["w"], kept["w"])
                if iou > threshold or contained or close_centres:
                    suppressed = True
                    break
            if not suppressed:
                survivors.append(candidate)
        keep.extend(survivors)

    keep.sort(key=lambda item: item["confidence"], reverse=True)
    return keep


def detect(
    model,
    metadata: dict,
    image: Image.Image,
    device: torch.device,
    threshold: float,
    nms_iou: float,
    scales: list[float],
    stride_ratio: float,
    batch_size: int,
) -> tuple[list[dict], int]:
    classes = list(metadata.get("classes", []))
    background = str(metadata.get("background", "__background__"))
    labels = [name for name in classes if name != background]
    if not labels:
        raise ValueError("detector has no positive classes")
    bg_index = classes.index(background) if background in classes else -1

    input_size = int(metadata.get("input_size", metadata.get("patch_size", 64)))
    window = int(metadata.get("patch_size", input_size))
    stride = max(4, int(window * stride_ratio))
    transform = _detector_transform(input_size)

    crops: list[torch.Tensor] = []
    origins: list[tuple[float, float, float, float]] = []
    for scale in scales:
        scaled = image
        if abs(scale - 1.0) > 1e-6:
            size = (max(1, int(image.width * scale)), max(1, int(image.height * scale)))
            scaled = image.resize(size, Image.Resampling.BILINEAR)
        for x, y, w, h in _boxes_at_scale(scaled.width, scaled.height, window, stride):
            crops.append(transform(scaled.crop((x, y, x + w, y + h))))
            origins.append((x / scale, y / scale, w / scale, h / scale))

    if not crops:
        return [], 0

    model.eval()
    scores_all: list[torch.Tensor] = []
    with torch.no_grad():
        for start in range(0, len(crops), batch_size):
            batch = torch.stack(crops[start : start + batch_size]).to(device, non_blocking=True)
            scores_all.append(torch.softmax(model(batch), dim=1).cpu())

    scores = torch.cat(scores_all, dim=0)
    candidates: list[dict] = []
    for index, (x, y, w, h) in enumerate(origins):
        row = scores[index]
        best_label = -1
        best_score = 0.0
        for class_index, name in enumerate(classes):
            if class_index == bg_index or name == background:
                continue
            value = float(row[class_index])
            if value > best_score:
                best_score = value
                best_label = class_index
        if best_label >= 0 and best_score >= threshold:
            candidates.append(
                {
                    "label": classes[best_label],
                    "confidence": round(best_score, 4),
                    "x": round(x, 1),
                    "y": round(y, 1),
                    "w": round(w, 1),
                    "h": round(h, 1),
                }
            )

    return _nms(candidates, nms_iou), len(crops)


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools sliding-window detector")
    parser.add_argument("--model", required=True, help="detector model package directory")
    parser.add_argument("--image", required=True, help="image to scan")
    parser.add_argument("--threshold", type=float, default=0.6)
    parser.add_argument("--nms", type=float, default=0.35, help="IoU suppression threshold")
    parser.add_argument(
        "--scales", default="1.0,0.7,1.4", help="comma-separated image scales"
    )
    parser.add_argument("--stride", type=float, default=0.5, help="stride as a fraction of window")
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--device", default="auto")
    args = parser.parse_args()

    started = time.time()
    try:
        model, metadata = load_package(args.model)
    except (OSError, ValueError, KeyError) as error:
        sys.stderr.write(f"error: cannot load model: {error}\n")
        return 2
    if str(metadata.get("kind", "classifier")) != "detector":
        sys.stderr.write("error: model is not a detector (train with train_detect.py)\n")
        return 2

    try:
        image = Image.open(args.image).convert("RGB")
    except OSError as error:
        sys.stderr.write(f"error: cannot read image: {error}\n")
        return 2

    try:
        scales = [float(value) for value in args.scales.split(",") if value.strip()]
    except ValueError:
        scales = [1.0]
    if not scales:
        scales = [1.0]

    device = torch.device(
        "cuda" if args.device == "auto" and torch.cuda.is_available() else
        ("cpu" if args.device == "auto" else args.device)
    )
    model.to(device)

    try:
        detections, scanned = detect(
            model,
            metadata,
            image,
            device,
            threshold=max(0.0, min(1.0, args.threshold)),
            nms_iou=max(0.0, min(1.0, args.nms)),
            scales=scales,
            stride_ratio=max(0.1, min(1.0, args.stride)),
            batch_size=max(1, args.batch_size),
        )
    except (ValueError, RuntimeError) as error:
        sys.stderr.write(f"error: detection failed: {error}\n")
        return 2

    result = {
        "image": str(Path(args.image).resolve()),
        "width": image.width,
        "height": image.height,
        "detections": detections,
        "scanned": scanned,
        "threshold": args.threshold,
        "nms": args.nms,
        "ms": round((time.time() - started) * 1000.0, 1),
    }
    sys.stdout.write(json.dumps(result, ensure_ascii=False) + "\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
