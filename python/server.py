#!/usr/bin/env python
"""Long-lived inference worker: one process, cached models, JSON-line protocol.

The single-shot scripts (predict.py / detect.py / detect_yolo.py) pay a 1-3 s
interpreter start on every call. This worker starts once, keeps loaded models
in memory and answers requests over stdin/stdout, cutting repeat inference to
tens of milliseconds.

Protocol (one JSON object per line, UTF-8):

    request  {"id": 1, "cmd": "detect", "model": "...", "image": "...", ...}
    response {"id": 1, "ok": true, "result": {...}}
    response {"id": 1, "ok": false, "error": "..."}

Supported commands: classify, detect, yolo, ocr, segment. Unknown commands get
a structured error; one bad request never kills the worker.

    python server.py
"""

from __future__ import annotations

import json
import sys
import time
import traceback
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

# Model cache: path -> (model object, metadata dict). Loading a model is the
# expensive part; keeping it avoids re-reading weights for every request.
_CACHE: dict[str, tuple[Any, dict]] = {}


def _load_yolo(directory: str):
    from ultralytics import YOLO

    package = Path(directory)
    metadata = json.loads((package / "model.json").read_text(encoding="utf-8"))
    weights = package / str(metadata.get("weights", "best.pt"))
    if not weights.is_file():
        raise FileNotFoundError(f"missing weights: {weights}")
    model = YOLO(str(weights))
    return model, metadata


def _cmd_yolo(request: dict) -> dict:
    from PIL import Image

    model_dir = request.get("model", "")
    image_path = request.get("image", "")
    if not model_dir or not image_path:
        raise ValueError("yolo requires 'model' and 'image'")

    key = "yolo:" + str(Path(model_dir).resolve())
    if key not in _CACHE:
        _CACHE[key] = _load_yolo(model_dir)
    model, metadata = _CACHE[key]

    with Image.open(image_path) as probe:
        width, height = probe.size

    conf = float(request.get("conf", 0.25))
    iou = float(request.get("iou", 0.45))
    imgsz = int(request.get("imgsz", 0))
    kwargs: dict[str, Any] = {
        "source": image_path,
        "conf": max(0.0, min(1.0, conf)),
        "iou": max(0.0, min(1.0, iou)),
        "verbose": False,
        "device": request.get("device") or None,
    }
    if imgsz > 0:
        kwargs["imgsz"] = imgsz
    results = model.predict(**kwargs)

    id_map = metadata.get("yolo_index_to_figtools_id", {})
    classes = list(metadata.get("classes", []))
    detections: list[dict] = []
    if results:
        boxes = getattr(results[0], "boxes", None)
        if boxes is not None:
            for xyxy, score, cls in zip(boxes.xyxy, boxes.conf, boxes.cls):
                x1, y1, x2, y2 = (float(v) for v in xyxy)
                index = int(cls)
                figtools_id = id_map.get(str(index))
                label = classes[index] if index < len(classes) else str(index)
                if figtools_id is not None and int(figtools_id) < len(classes):
                    label = classes[int(figtools_id)]
                detections.append(
                    {
                        "label": label,
                        "confidence": round(float(score), 4),
                        "x": round(x1, 1),
                        "y": round(y1, 1),
                        "w": round(x2 - x1, 1),
                        "h": round(y2 - y1, 1),
                    }
                )
    detections.sort(key=lambda item: item["confidence"], reverse=True)
    return {
        "image": str(Path(image_path).resolve()),
        "width": width,
        "height": height,
        "detections": detections,
        "kind": "yolo",
    }


def _cmd_detect(request: dict) -> dict:
    """Patch classifier detector (no ultralytics dependency)."""
    import torch
    from PIL import Image
    from torchvision import transforms

    from ft_train.model import IMAGENET_MEAN, IMAGENET_STD
    from detect import _boxes_at_scale, _nms

    model_dir = request.get("model", "")
    image_path = request.get("image", "")
    if not model_dir or not image_path:
        raise ValueError("detect requires 'model' and 'image'")
    key = "detect:" + str(Path(model_dir).resolve())
    if key not in _CACHE:
        from ft_train.model import load_package

        model, metadata = load_package(model_dir)
        _CACHE[key] = (model, metadata)
    model, metadata = _CACHE[key]

    with Image.open(image_path) as probe:
        width, height = probe.size
    image = Image.open(image_path).convert("RGB")

    classes = list(metadata.get("classes", []))
    background = str(metadata.get("background", "__background__"))
    positive = [name for name in classes if name != background]
    bg_index = classes.index(background) if background in classes else -1
    input_size = int(metadata.get("input_size", metadata.get("patch_size", 64)))
    window = int(metadata.get("patch_size", input_size))
    stride = max(4, int(window * 0.5))
    trans = transforms.Compose(
        [transforms.Resize((input_size, input_size)), transforms.ToTensor(),
         transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD)]
    )

    crops: list = []
    origins: list[tuple[float, float, float, float]] = []
    for scale in (1.0, 0.7, 1.4):
        scaled = image if scale == 1.0 else image.resize(
            (max(1, int(width * scale)), max(1, int(height * scale))), Image.Resampling.BILINEAR
        )
        for x, y, w, h in _boxes_at_scale(scaled.width, scaled.height, window, stride):
            crops.append(trans(scaled.crop((x, y, x + w, y + h))))
            origins.append((x / scale, y / scale, w / scale, h / scale))

    model.eval()
    scores_all = []
    with torch.no_grad():
        for start in range(0, len(crops), 64):
            batch = torch.stack(crops[start : start + 64])
            scores_all.append(torch.softmax(model(batch), dim=1).cpu())
    scores = torch.cat(scores_all, dim=0)

    threshold = float(request.get("threshold", 0.5))
    candidates: list[dict] = []
    for index, (x, y, w, h) in enumerate(origins):
        row = scores[index]
        best_label = -1
        best_score = 0.0
        for ci, name in enumerate(classes):
            if ci == bg_index or name == background:
                continue
            value = float(row[ci])
            if value > best_score:
                best_score, best_label = value, ci
        if best_label >= 0 and best_score >= threshold:
            candidates.append(
                {"label": classes[best_label], "confidence": round(best_score, 4),
                 "x": round(x, 1), "y": round(y, 1), "w": round(w, 1), "h": round(h, 1)}
            )
    detections = _nms(candidates, 0.4)
    return {"image": str(Path(image_path).resolve()), "width": width, "height": height,
            "detections": detections, "kind": "patch"}


def _cmd_classify(request: dict) -> dict:
    import torch
    from PIL import Image

    from ft_train.model import eval_transform

    model_dir = request.get("model", "")
    image_path = request.get("image", "")
    if not model_dir or not image_path:
        raise ValueError("classify requires 'model' and 'image'")
    key = "classify:" + str(Path(model_dir).resolve())
    if key not in _CACHE:
        from ft_train.model import load_package

        _CACHE[key] = load_package(model_dir)
    model, metadata = _CACHE[key]
    classes = list(metadata.get("classes", []))
    image = Image.open(image_path).convert("RGB")
    tensor = eval_transform(int(metadata.get("input_size", 224)))(image).unsqueeze(0)
    with torch.no_grad():
        probabilities = torch.softmax(model(tensor), dim=1)[0]
    topk = int(request.get("topk", 3))
    topk = max(1, min(topk, len(classes)))
    scores, indices = probabilities.topk(topk)
    return {
        "label": classes[int(indices[0])],
        "confidence": round(float(scores[0]), 4),
        "topk": [{"label": classes[int(i)], "score": round(float(s), 4)}
                 for s, i in zip(scores, indices)],
        "kind": "classifier",
    }


def _cmd_ocr(request: dict) -> dict:
    image_path = request.get("image", "")
    if not image_path:
        raise ValueError("ocr requires 'image'")
    key = "ocr"
    if key not in _CACHE:
        try:
            from rapidocr_onnxruntime import RapidOCR
        except ImportError as error:
            raise RuntimeError(
                "OCR engine not installed; pip install rapidocr-onnxruntime"
            ) from error
        _CACHE[key] = (RapidOCR(), {})
    engine, _ = _CACHE[key]
    result, _elapsed = engine(image_path)
    texts: list[dict] = []
    for entry in result or []:
        box, text, score = entry[0], entry[1], entry[2]
        xs = [float(point[0]) for point in box]
        ys = [float(point[1]) for point in box]
        texts.append(
            {
                "text": str(text),
                "confidence": round(float(score), 4),
                "x": round(min(xs), 1),
                "y": round(min(ys), 1),
                "w": round(max(xs) - min(xs), 1),
                "h": round(max(ys) - min(ys), 1),
            }
        )
    return {"image": str(Path(image_path).resolve()), "texts": texts, "kind": "ocr"}


def _cmd_segment(request: dict) -> dict:
    from PIL import Image
    import numpy as np

    model_dir = request.get("model", "")
    image_path = request.get("image", "")
    if not model_dir or not image_path:
        raise ValueError("segment requires 'model' and 'image'")
    key = "yolo:" + str(Path(model_dir).resolve())
    if key not in _CACHE:
        _CACHE[key] = _load_yolo(model_dir)
    model, metadata = _CACHE[key]

    kwargs: dict[str, Any] = {
        "source": image_path,
        "conf": float(request.get("conf", 0.25)),
        "verbose": False,
        "device": request.get("device") or None,
    }
    results = model.predict(**kwargs)
    id_map = metadata.get("yolo_index_to_figtools_id", {})
    classes = list(metadata.get("classes", []))
    instances: list[dict] = []
    if results:
        result = results[0]
        masks = getattr(result, "masks", None)
        boxes = getattr(result, "boxes", None)
        if masks is not None:
            polygons = [mask.astype(np.int32).reshape(-1, 2).tolist() for mask in masks.xy]
            confs = [float(v) for v in boxes.conf] if boxes is not None else [1.0] * len(polygons)
            clss = [int(v) for v in boxes.cls] if boxes is not None else [0] * len(polygons)
            for poly, conf, cls in zip(polygons, confs, clss):
                figtools_id = id_map.get(str(cls))
                label = classes[cls] if cls < len(classes) else str(cls)
                if figtools_id is not None and int(figtools_id) < len(classes):
                    label = classes[int(figtools_id)]
                instances.append({"label": label, "confidence": round(conf, 4),
                                  "polygon": [[round(float(p[0]), 1), round(float(p[1]), 1)]
                                              for p in poly]})
    with Image.open(image_path) as probe:
        width, height = probe.size
    return {"image": str(Path(image_path).resolve()), "width": width, "height": height,
            "instances": instances, "kind": "segment"}


_COMMANDS = {
    "classify": _cmd_classify,
    "detect": _cmd_detect,
    "yolo": _cmd_yolo,
    "ocr": _cmd_ocr,
    "segment": _cmd_segment,
    "ping": lambda request: {"pong": True, "cached": len(_CACHE)},
}


def main() -> int:
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request_id: Any = None
        try:
            request = json.loads(line)
            request_id = request.get("id")
            command = str(request.get("cmd", ""))
            handler = _COMMANDS.get(command)
            if handler is None:
                raise ValueError(f"unknown command: {command}")
            result = handler(request)
            response = {"id": request_id, "ok": True, "result": result}
        except Exception as error:  # noqa: BLE001 - one bad request must not kill the worker
            traceback.print_exc()
            response = {"id": request_id, "ok": False, "error": str(error)}
        sys.stdout.write(json.dumps(response, ensure_ascii=False) + "\n")
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
