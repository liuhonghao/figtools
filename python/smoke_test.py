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
        print(
            "smoke ok: "
            f"{len(events)} events, label={result['label']}, "
            f"confidence={result['confidence']}, {result['ms']} ms"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
