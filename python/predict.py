#!/usr/bin/env python
"""Run a trained classifier on one image.

Prints exactly one JSON object on stdout; diagnostics go to stderr. The desktop
workbench parses this, and so can any other tool.

    python predict.py --model models/run-1 --image photos/part.png --topk 3
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import torch
from PIL import Image

from ft_train.model import eval_transform, load_package


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools classifier inference")
    parser.add_argument("--model", required=True, help="model package directory")
    parser.add_argument("--image", required=True, help="image to classify")
    parser.add_argument("--topk", type=int, default=3)
    args = parser.parse_args()

    started = time.time()
    try:
        model, metadata = load_package(args.model)
    except (OSError, ValueError, KeyError) as error:
        sys.stderr.write(f"error: cannot load model: {error}\n")
        return 2

    try:
        image = Image.open(args.image).convert("RGB")
    except OSError as error:
        sys.stderr.write(f"error: cannot read image: {error}\n")
        return 2

    classes = list(metadata.get("classes", []))
    if not classes:
        sys.stderr.write("error: model has no classes\n")
        return 2

    tensor = eval_transform(int(metadata.get("input_size", 224)))(image).unsqueeze(0)
    with torch.no_grad():
        logits = model(tensor)
        probabilities = torch.softmax(logits, dim=1)[0]

    topk = max(1, min(args.topk, len(classes)))
    scores, indices = probabilities.topk(topk)
    result = {
        "label": classes[int(indices[0])],
        "confidence": round(float(scores[0]), 4),
        "topk": [
            {"label": classes[int(index)], "score": round(float(score), 4)}
            for score, index in zip(scores, indices)
        ],
        "arch": metadata.get("arch", ""),
        "image": str(Path(args.image).resolve()),
        "ms": round((time.time() - started) * 1000.0, 1),
    }
    sys.stdout.write(json.dumps(result, ensure_ascii=False) + "\n")
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
