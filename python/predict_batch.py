#!/usr/bin/env python
"""Classify a folder of images with one Python process.

One process for the whole folder is the point: starting torch for every image
would cost more than the inference itself. Results are written as CSV (Excel
friendly) or JSONL, and progress is reported as NDJSON events on stdout, the
same channel train.py uses.

    python predict_batch.py --job job.json
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from pathlib import Path

import torch
from PIL import Image
from torch.utils.data import DataLoader, Dataset

from ft_train.events import emit, fail, log
from ft_train.model import eval_transform, load_package

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".ppm", ".pgm", ".tif", ".tiff", ".webp"}


class Images(Dataset):
    def __init__(self, paths: list[Path], transform) -> None:
        self.paths = paths
        self.transform = transform

    def __len__(self) -> int:
        return len(self.paths)

    def __getitem__(self, item: int):
        image = Image.open(self.paths[item]).convert("RGB")
        return self.transform(image)


def collect_images(root: str | Path, recursive: bool) -> list[Path]:
    path = Path(root)
    if path.is_file():
        return [path]
    if not path.is_dir():
        raise FileNotFoundError(f"input not found: {path}")
    iterator = path.rglob("*") if recursive else path.iterdir()
    return sorted(
        candidate
        for candidate in iterator
        if candidate.is_file() and candidate.suffix.lower() in IMAGE_SUFFIXES
    )


def write_csv(output: Path, rows: list[dict], topk: int) -> None:
    columns = ["image", "label", "confidence"]
    for rank in range(1, topk + 1):
        columns.extend([f"label_{rank}", f"score_{rank}"])
    with output.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools batch inference")
    parser.add_argument("--job", required=True)
    args = parser.parse_args()

    try:
        job = json.loads(Path(args.job).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        fail(f"cannot read job: {error}")
        return 2

    model_dir = job.get("model", "")
    source = job.get("input", "")
    output_text = job.get("output", "")
    if not model_dir or not source or not output_text:
        fail("job must contain 'model', 'input' and 'output'")
        return 2

    topk = max(1, int(job.get("topk", 3)))
    batch_size = max(1, int(job.get("batch_size", 32)))
    threshold = float(job.get("threshold", 0.0))
    requested_device = str(job.get("device", "cpu"))
    recursive = bool(job.get("recursive", False))

    started = time.time()
    emit("phase", name="loading_model")
    try:
        model, metadata = load_package(model_dir)
    except (OSError, ValueError, KeyError) as error:
        fail(f"cannot load model: {error}")
        return 2

    classes = list(metadata.get("classes", []))
    if not classes:
        fail("model has no classes")
        return 2
    topk = min(topk, len(classes))

    try:
        images = collect_images(source, recursive)
    except OSError as error:
        fail(str(error))
        return 2
    if not images:
        fail(f"no images under {source}")
        return 2

    device = torch.device("cuda" if requested_device == "cuda" and torch.cuda.is_available()
                          else "cpu")
    model = model.to(device)
    loader = DataLoader(
        Images(images, eval_transform(int(metadata.get("input_size", 224)))),
        batch_size=batch_size,
        shuffle=False,
        num_workers=0,
        pin_memory=device.type == "cuda",
    )

    emit(
        "dataset",
        total=len(images),
        classes=classes,
        model=Path(model_dir).name,
        arch=metadata.get("arch", ""),
        device=str(device),
    )
    log(f"{len(images)} images, {len(classes)} classes, device {device}")

    rows: list[dict] = []
    counts: dict[str, int] = {name: 0 for name in classes}
    low_confidence = 0
    done = 0

    with torch.no_grad():
        for batch in loader:
            batch = batch.to(device, non_blocking=True)
            probabilities = torch.softmax(model(batch), dim=1)
            scores, indices = probabilities.topk(topk, dim=1)
            for offset in range(batch.size(0)):
                row: dict[str, object] = {"image": str(images[done + offset])}
                for rank in range(topk):
                    label = classes[int(indices[offset][rank])]
                    score = float(scores[offset][rank])
                    row[f"label_{rank + 1}"] = label
                    row[f"score_{rank + 1}"] = round(score, 4)
                row["label"] = row["label_1"]
                row["confidence"] = row["score_1"]
                if row["confidence"] < threshold:
                    row["label"] = "unknown"
                    low_confidence += 1
                else:
                    counts[str(row["label_1"])] += 1
                rows.append(row)

            done += batch.size(0)
            emit(
                "batch",
                done=done,
                total=len(images),
                seconds=round(time.time() - started, 2),
            )
            log(f"{done}/{len(images)}")

    output = Path(output_text)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.suffix.lower() == ".jsonl":
            with output.open("w", encoding="utf-8") as stream:
                for row in rows:
                    stream.write(json.dumps(row, ensure_ascii=False) + "\n")
        else:
            write_csv(output, rows, topk)
    except OSError as error:
        fail(f"cannot write results: {error}")
        return 2

    emit(
        "done",
        output=str(output.resolve()),
        total=len(images),
        counts=counts,
        low_confidence=low_confidence,
        seconds=round(time.time() - started, 2),
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - one structured error for the host
        fail(str(error))
        sys.exit(2)
