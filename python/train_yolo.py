#!/usr/bin/env python
"""Train a YOLO detector with Ultralytics and emit figtools events.

This is an optional, separately installed component: it needs `ultralytics`,
which is NOT part of the desktop app. See python/README.md for the licensing
boundary (Ultralytics is AGPL-3.0; it is never bundled or linked into the app).

Input is a figtools dataset snapshot. Output is a model package containing the
native weights, an ONNX export and a C++-readable model.json:

    best.pt       Ultralytics weights (retrain / resume / export)
    weights.onnx  ONNX graph (portable inference)
    model.json    metadata in the figtools contract
    metrics.json  per-epoch curves
    data.yaml / classes.json / job.json

stdout carries NDJSON events (phase/dataset/epoch/done/error); logs go to stderr.

    python train_yolo.py --job job.json
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
import traceback
from pathlib import Path
from types import SimpleNamespace

from ft_train.events import emit, fail, log
from ft_train.yolo_data import export_yolo


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools YOLO trainer (Ultralytics)")
    parser.add_argument("--job", required=True, help="path to the job JSON")
    args = parser.parse_args()

    try:
        from ultralytics import YOLO  # imported lazily: the app itself has no dependency
    except ImportError:
        fail("ultralytics is not installed in this Python environment")
        log("hint: pip install ultralytics  (separate training component, AGPL-3.0)")
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
    try:
        target.mkdir(parents=True, exist_ok=True)
        source = Path(args.job)
        if source.resolve() != (target / "job.json").resolve():
            shutil.copyfile(source, target / "job.json")
    except OSError as error:
        log(f"warning: could not archive job.json: {error}")

    variant = str(job.get("variant", "yolo11n.pt"))
    epochs = max(1, int(job.get("epochs", 50)))
    input_size = int(job.get("imgsz", job.get("input_size", 640)))
    batch_size = int(job.get("batch_size", 16))
    learning_rate = float(job.get("lr", 0.001)) if job.get("lr") else None
    seed = int(job.get("seed", 2026))
    device = str(job.get("device", "auto"))
    patience = int(job.get("patience", 0))

    emit("phase", name="export")
    data_yaml_override = job.get("data_yaml", "")
    if data_yaml_override:
        # Pre-converted dataset (e.g. TTPLA): use its data.yaml directly instead
        # of copying gigabytes of images through the snapshot exporter.
        prepared = Path(data_yaml_override)
        if not prepared.is_file():
            fail(f"data_yaml not found: {prepared}")
            return 2
        manifest_path = prepared.parent / "conversion.json"
        names: list[str] = []
        images = {"train": 0, "val": 0, "test": 0}
        labels = {"train": 0, "val": 0, "test": 0}
        if manifest_path.is_file():
            try:
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                names = [entry["name"] for entry in manifest.get("classes", [])]
                images.update(manifest.get("images", {}))
                labels.update(manifest.get("labels", {}))
            except (OSError, ValueError, KeyError, TypeError):
                names = []
        if not names:
            # Fall back to parsing names straight out of data.yaml.
            for line in prepared.read_text(encoding="utf-8").splitlines():
                stripped = line.strip()
                if stripped[:1].isdigit() and ":" in stripped:
                    names.append(stripped.split(":", 1)[1].strip())
        exported = SimpleNamespace(
            data_yaml=prepared,
            classes=names,
            id_map={index: index for index in range(len(names))},
            images=images,
            labels=labels,
            skipped=0,
        )
        log(f"using prepared dataset: {prepared} ({names})")
    else:
        try:
            exported = export_yolo(data_root, target / "dataset")
        except (OSError, ValueError) as error:
            fail(str(error))
            return 2

    emit(
        "dataset",
        classes=exported.classes,
        counts=exported.labels,
        images=exported.images,
        skipped=exported.skipped,
        source="yolo",
        val_reuses_train=not exported.images["val"],
    )
    log(
        "dataset: "
        + ", ".join(f"{split}={count}" for split, count in exported.images.items())
        + f" images; skipped {exported.skipped}"
    )
    if not exported.images["val"]:
        log("warning: no validation split; YOLO will reuse train for val (metrics are optimistic)")

    # Keep training artefacts in a subdirectory so the package root stays clean
    # and the C++ side can find model.json without scanning.
    run_dir = target / "run"
    run_dir.mkdir(parents=True, exist_ok=True)

    # Per-epoch bridge: Ultralytics calls this once after each validation.
    history: list[dict] = []
    started = time.time()

    def on_fit_epoch_end(trainer) -> None:  # noqa: ANN001 - ultralytics callback
        metrics = getattr(trainer, "metrics", {}) or {}
        epoch = int(getattr(trainer, "epoch", 0)) + 1
        if epoch > epochs:  # validation callback may fire once after the loop
            return
        # Ultralytics keeps validation metrics in trainer.metrics (keys vary by
        # version) but training losses in trainer.loss_items, not in metrics.
        map50 = metrics.get("metrics/mAP50(B)", metrics.get("mAP50", 0.0))
        map5095 = metrics.get("metrics/mAP50-95(B)", metrics.get("mAP50-95", 0.0))
        # trainer.loss_items is a numpy array in most versions, but some
        # builds expose it as a dict; read defensively so a callback can never
        # abort a 25-epoch training run.
        loss_box = 0.0
        loss_cls = 0.0
        loss_items = getattr(trainer, "loss_items", None)
        if loss_items is not None:
            try:
                loss_box = float(loss_items[0])
                loss_cls = float(loss_items[1])
            except (KeyError, IndexError, TypeError, ValueError):
                try:
                    loss_box = float(loss_items.get("box", 0.0))
                    loss_cls = float(loss_items.get("cls", 0.0))
                except (AttributeError, TypeError, ValueError):
                    pass
        record = {
            "epoch": epoch,
            "total_epochs": epochs,
            "mAP50": round(float(map50), 4),
            "mAP50_95": round(float(map5095), 4),
            "box_loss": round(float(loss_box), 5),
            "cls_loss": round(float(loss_cls), 5),
        }
        history.append(record)
        emit("epoch", **record)
        log(
            f"epoch {epoch:3d}/{epochs}  mAP50 {record['mAP50']:.4f}  "
            f"mAP50-95 {record['mAP50_95']:.4f}"
        )

    try:
        model = YOLO(variant)
        model.add_callback("on_fit_epoch_end", on_fit_epoch_end)

        train_kwargs = {
            "data": str(exported.data_yaml),
            "epochs": epochs,
            "imgsz": input_size,
            "batch": batch_size,
            "seed": seed,
            "device": device if device != "auto" else None,
            "project": str(run_dir),
            "name": "train",
            "exist_ok": True,
            "verbose": False,
            "plots": False,
        }
        if learning_rate is not None:
            train_kwargs["lr0"] = learning_rate
        if patience > 0:
            train_kwargs["patience"] = patience

        emit("phase", name="training")
        log(f"YOLO {variant}: {epochs} epochs, imgsz {input_size}, batch {batch_size}, device {device}")
        model.train(**train_kwargs)

        best = run_dir / "train" / "weights" / "best.pt"
        if not best.is_file():
            fail("training finished without best.pt")
            return 2
        shutil.copyfile(best, target / "best.pt")

        # Final validation metrics (separate from the per-epoch callback).
        metrics = {}
        try:
            validation = model.val(data=str(exported.data_yaml), verbose=False, plots=False)
            metrics = {
                "mAP50": round(float(validation.box.map50), 4),
                "mAP50_95": round(float(validation.box.map), 4),
            }
        except (AttributeError, RuntimeError, ValueError) as error:
            log(f"warning: could not read final validation metrics: {error}")

        # ONNX export is best-effort: a failed export must not discard good
        # weights. The native .pt is always kept and is enough to run detection.
        onnx_ok = False
        try:
            emitted = model.export(format="onnx", imgsz=input_size, dynamic=False, verbose=False)
            onnx_path = Path(str(emitted))
            if onnx_path.is_file():
                if onnx_path.resolve() != (target / "weights.onnx").resolve():
                    shutil.copyfile(onnx_path, target / "weights.onnx")
                onnx_ok = True
        except (RuntimeError, ValueError, OSError) as error:
            log(f"warning: ONNX export failed (native weights kept): {error}")

    except Exception as error:  # noqa: BLE001 - report one structured error
        traceback.print_exc()
        fail(f"YOLO training failed: {error}")
        return 2

    metadata = {
        "format": "figtools-model/1",
        "kind": "yolo",
        "framework": "ultralytics",
        "variant": variant,
        "arch": f"YOLO ({variant})",
        "classes": exported.classes,
        "yolo_index_to_figtools_id": {
            str(index): original for original, index in exported.id_map.items()
        },
        "input_size": input_size,
        "mean": [0.0, 0.0, 0.0],  # YOLO does its own normalisation inside the graph
        "std": [1.0, 1.0, 1.0],
        "weights": "best.pt",
        "onnx": "weights.onnx" if onnx_ok else None,
        "metrics": {
            "mAP50": metrics.get("mAP50"),
            "mAP50_95": metrics.get("mAP50_95"),
        },
        "seconds": round(time.time() - started, 2),
        "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    (target / "model.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (target / "metrics.json").write_text(
        json.dumps(
            {"history": history, "metrics": metadata["metrics"]}, ensure_ascii=False, indent=2
        ),
        encoding="utf-8",
    )

    emit(
        "done",
        model_dir=str(target.resolve()),
        mAP50=metadata["metrics"]["mAP50"],
        mAP50_95=metadata["metrics"]["mAP50_95"],
        seconds=metadata["seconds"],
        classes=exported.classes,
        onnx=onnx_ok,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001
        traceback.print_exc()
        fail(str(error))
        sys.exit(2)
