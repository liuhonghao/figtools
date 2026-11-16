#!/usr/bin/env python
"""Train a sliding-window detector from figtools annotations.

The annotation shapes in a dataset snapshot become positive patches; background
patches are sampled from the same images. A normal classifier is trained over
those patches including a ``__background__`` class. ``detect.py`` then slides it
over new images and applies NMS.

This is deliberately a patch classifier, not a full detection head: it runs on
small datasets and CPU, keeps the same model package format, and -- most
importantly -- makes the annotation layer actually feed training.

    python train_detect.py --job job.json
"""

from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
import time
import traceback
from pathlib import Path

import torch
from PIL import Image
from torch import nn
from torch.utils.data import DataLoader, Dataset

from ft_train.data import Sample
from ft_train.events import emit, fail, log
from ft_train.model import build_model, eval_transform, save_package, train_transform
from ft_train.patches import BACKGROUND, PatchInfo, build_patches


class PatchList(Dataset):
    def __init__(self, samples: list[Sample], classes: list[str], transform) -> None:
        self.samples = samples
        self.index = {name: i for i, name in enumerate(classes)}
        self.transform = transform

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(self, item: int):
        sample = self.samples[item]
        image = Image.open(sample.path).convert("RGB")
        return self.transform(image), self.index[sample.label]


def _evaluate(model: nn.Module, loader: DataLoader, criterion, device: torch.device):
    model.eval()
    total_loss = 0.0
    correct = 0
    count = 0
    with torch.no_grad():
        for images, labels in loader:
            images = images.to(device, non_blocking=True)
            labels = labels.to(device, non_blocking=True)
            logits = model(images)
            total_loss += float(criterion(logits, labels)) * labels.size(0)
            correct += int((logits.argmax(dim=1) == labels).sum())
            count += labels.size(0)
    if count == 0:
        return 0.0, 0.0
    return total_loss / count, correct / count


def main() -> int:
    parser = argparse.ArgumentParser(description="figtools patch-detector trainer")
    parser.add_argument("--job", required=True, help="path to the job JSON")
    args = parser.parse_args()

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

    arch = str(job.get("arch", "smallcnn"))
    input_size = int(job.get("input_size", 64))
    patch_size = int(job.get("patch_size", input_size))
    context = float(job.get("context", 0.15))
    negatives = max(1, int(job.get("negatives", 3)))
    positives_per_box = max(1, int(job.get("positives_per_box", 3)))
    epochs = max(1, int(job.get("epochs", 20)))
    batch_size = max(1, int(job.get("batch_size", 32)))
    learning_rate = float(job.get("lr", 0.001))
    seed = int(job.get("seed", 2026))
    requested_device = str(job.get("device", "auto"))

    random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)

    emit("phase", name="patches")
    patch_dir = target / "patches"
    try:
        info: PatchInfo = build_patches(
            data_root,
            patch_dir,
            patch_size=patch_size,
            context=context,
            negatives_per_image=negatives,
            positives_per_box=positives_per_box,
            seed=seed,
        )
    except (OSError, ValueError) as error:
        fail(str(error))
        return 2

    classes = info.classes
    train_samples = [s for s in info.samples if s.split == "train"]
    val_samples = [s for s in info.samples if s.split == "val"] or train_samples
    test_samples = [s for s in info.samples if s.split == "test"]
    if len(train_samples) < 2:
        fail(f"need at least two training patches, found {len(train_samples)}")
        return 2

    emit(
        "dataset",
        classes=classes,
        positive_classes=classes[:-1],
        counts=info.counts(),
        train=len(train_samples),
        val=len(val_samples),
        test=len(test_samples),
        positives=info.positives,
        negatives=info.negatives,
        images=info.images,
        skipped=info.skipped,
        source="patches",
        val_is_train=not [s for s in info.samples if s.split == "val"],
    )
    log(
        f"patches: {info.positives} positive + {info.negatives} background "
        f"from {info.images} images"
    )

    if requested_device == "auto":
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    else:
        device = torch.device(requested_device)
    log(f"device: {device}  arch: {arch}  input: {input_size}")

    try:
        model = build_model(arch, len(classes)).to(device)
    except ValueError as error:
        fail(str(error))
        return 2

    train_loader = DataLoader(
        PatchList(train_samples, classes, train_transform(input_size)),
        batch_size=min(batch_size, len(train_samples)),
        shuffle=True,
        num_workers=0,
        generator=torch.Generator().manual_seed(seed),
    )
    val_loader = DataLoader(
        PatchList(val_samples, classes, eval_transform(input_size)),
        batch_size=min(batch_size, len(val_samples)),
        shuffle=False,
        num_workers=0,
    )
    test_loader = (
        DataLoader(
            PatchList(test_samples, classes, eval_transform(input_size)),
            batch_size=min(batch_size, len(test_samples)),
            shuffle=False,
            num_workers=0,
        )
        if test_samples
        else None
    )

    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.AdamW(model.parameters(), lr=learning_rate, weight_decay=1.0e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)

    started = time.time()
    best_accuracy = -1.0
    best_epoch = 0
    best_state: dict[str, torch.Tensor] | None = None
    history: list[dict] = []

    for epoch in range(1, epochs + 1):
        model.train()
        running_loss = 0.0
        correct = 0
        seen = 0
        for images, labels in train_loader:
            images = images.to(device, non_blocking=True)
            labels = labels.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            logits = model(images)
            loss = criterion(logits, labels)
            loss.backward()
            optimizer.step()
            running_loss += float(loss.detach()) * labels.size(0)
            correct += int((logits.argmax(dim=1) == labels).sum())
            seen += labels.size(0)

        train_loss = running_loss / max(1, seen)
        train_accuracy = correct / max(1, seen)
        val_loss, val_accuracy = _evaluate(model, val_loader, criterion, device)
        scheduler.step()

        if val_accuracy > best_accuracy + 1e-9:
            best_accuracy = val_accuracy
            best_epoch = epoch
            best_state = {key: value.detach().clone() for key, value in model.state_dict().items()}

        record = {
            "epoch": epoch,
            "train_loss": round(train_loss, 5),
            "train_accuracy": round(train_accuracy, 4),
            "val_loss": round(val_loss, 5),
            "val_accuracy": round(val_accuracy, 4),
        }
        history.append(record)
        emit("epoch", **record, epochs=epochs, lr=round(optimizer.param_groups[0]["lr"], 6))
        log(
            f"epoch {epoch:3d}/{epochs}  loss {train_loss:.4f}  "
            f"train {train_accuracy:.3f}  val {val_accuracy:.3f}"
        )

    if best_state is not None:
        model.load_state_dict(best_state)
    test_accuracy = None
    if test_loader is not None:
        _, test_accuracy = _evaluate(model, test_loader, criterion, device)

    metrics = {
        "val_accuracy": round(best_accuracy, 4),
        "test_accuracy": None if test_accuracy is None else round(test_accuracy, 4),
        "best_epoch": best_epoch,
        "epochs": epochs,
        "patches": len(train_samples),
        "positives": info.positives,
        "negatives": info.negatives,
        "arch": arch,
        "input_size": input_size,
        "patch_size": patch_size,
        "context": context,
        "seconds": round(time.time() - started, 2),
    }

    try:
        save_package(
            output,
            model.cpu(),
            classes,
            arch,
            input_size,
            metrics,
            history,
            kind="detector",
            extra={
                "positive_classes": classes[:-1],
                "background": BACKGROUND,
                "patch_size": patch_size,
                "context": context,
                "format_version": 2,
            },
        )
    except OSError as error:
        fail(f"cannot write model package: {error}")
        return 2

    emit(
        "done",
        model_dir=str(target.resolve()),
        best_epoch=best_epoch,
        val_accuracy=metrics["val_accuracy"],
        test_accuracy=metrics["test_accuracy"],
        seconds=metrics["seconds"],
        classes=classes[:-1],
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - host needs one structured error
        traceback.print_exc()
        fail(str(error))
        sys.exit(2)
