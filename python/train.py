#!/usr/bin/env python
"""Train an image classifier and write a figtools model package.

Invoked by the desktop workbench with ``--job job.json``; every progress line
on stdout is a JSON event (see ft_train/events.py). Running it by hand works
too -- that is the point of keeping the job in a file.

    python train.py --job job.json
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

from ft_train.data import Sample, discover
from ft_train.events import emit, fail, log
from ft_train.model import build_model, eval_transform, save_package, train_transform


class ImageList(Dataset):
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


def evaluate(model: nn.Module, loader: DataLoader, criterion, device: torch.device):
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
    parser = argparse.ArgumentParser(description="figtools classifier trainer")
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

    # Keep the exact job next to the weights: a model without its inputs is
    # only half of an experiment. Skipped when the job already lives there.
    try:
        target = Path(output)
        target.mkdir(parents=True, exist_ok=True)
        source = Path(args.job)
        if source.resolve() != (target / "job.json").resolve():
            shutil.copyfile(source, target / "job.json")
    except OSError as error:
        log(f"warning: could not archive job.json: {error}")

    arch = str(job.get("arch", "resnet18"))
    input_size = int(job.get("input_size", 224))
    epochs = max(1, int(job.get("epochs", 20)))
    batch_size = max(1, int(job.get("batch_size", 16)))
    learning_rate = float(job.get("lr", 0.001))
    seed = int(job.get("seed", 2026))
    requested_device = str(job.get("device", "auto"))
    val_ratio = float(job.get("val_ratio", 0.2))

    random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)

    emit("phase", name="scanning")
    try:
        info = discover(data_root, val_ratio=val_ratio, seed=seed)
    except (OSError, ValueError) as error:
        fail(str(error))
        return 2

    train_samples = [sample for sample in info.samples if sample.split == "train"]
    val_samples = [sample for sample in info.samples if sample.split == "val"]
    test_samples = [sample for sample in info.samples if sample.split == "test"]
    if len(train_samples) < 2:
        fail(f"need at least two training images, found {len(train_samples)}")
        return 2

    val_is_train = not val_samples
    if val_is_train:
        log("warning: no validation split, using the training set for model selection")
        val_samples = train_samples

    counts = info.counts()
    emit(
        "dataset",
        classes=info.classes,
        counts=counts,
        train=len(train_samples),
        val=len(val_samples),
        test=len(test_samples),
        skipped=info.skipped,
        source=info.source,
        val_is_train=val_is_train,
    )
    log(f"classes: {', '.join(info.classes)}")
    log(
        f"train {len(train_samples)} / val {len(val_samples)} / test {len(test_samples)}"
        f" (skipped {info.skipped})"
    )

    if requested_device == "auto":
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    else:
        device = torch.device(requested_device)
    log(f"device: {device}  arch: {arch}  input: {input_size}")

    try:
        model = build_model(arch, len(info.classes)).to(device)
    except ValueError as error:
        fail(str(error))
        return 2

    train_loader = DataLoader(
        ImageList(train_samples, info.classes, train_transform(input_size)),
        batch_size=min(batch_size, len(train_samples)),
        shuffle=True,
        num_workers=0,
        pin_memory=device.type == "cuda",
        generator=torch.Generator().manual_seed(seed),
    )
    val_loader = DataLoader(
        ImageList(val_samples, info.classes, eval_transform(input_size)),
        batch_size=min(batch_size, len(val_samples)),
        shuffle=False,
        num_workers=0,
        pin_memory=device.type == "cuda",
    )
    test_loader = (
        DataLoader(
            ImageList(test_samples, info.classes, eval_transform(input_size)),
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
        val_loss, val_accuracy = evaluate(model, val_loader, criterion, device)
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
        _, test_accuracy = evaluate(model, test_loader, criterion, device)

    metrics = {
        "val_accuracy": round(best_accuracy, 4),
        "test_accuracy": None if test_accuracy is None else round(test_accuracy, 4),
        "best_epoch": best_epoch,
        "epochs": epochs,
        "train_images": len(train_samples),
        "val_images": len(val_samples),
        "test_images": len(test_samples),
        "classes": info.classes,
        "arch": arch,
        "input_size": input_size,
        "val_is_train": val_is_train,
        "seconds": round(time.time() - started, 2),
    }

    try:
        save_package(output, model.cpu(), info.classes, arch, input_size, metrics, history)
    except OSError as error:
        fail(f"cannot write model package: {error}")
        return 2

    emit(
        "done",
        model_dir=str(Path(output).resolve()),
        best_epoch=best_epoch,
        val_accuracy=metrics["val_accuracy"],
        test_accuracy=metrics["test_accuracy"],
        seconds=metrics["seconds"],
        classes=info.classes,
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001 - the host needs one structured error
        traceback.print_exc()
        fail(str(error))
        sys.exit(2)
