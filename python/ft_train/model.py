"""Model zoo, transforms and the on-disk model package.

A package is a directory:

    model.pt      torch state dict + arch/classes/input size
    model.json    the same metadata in a form other tools can read
    metrics.json  per-epoch history and final scores

The C++ workbench reads model.json to show the model without importing torch;
predict.py reads both to run inference.
"""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path

import torch
from torch import nn
from torchvision import models, transforms

IMAGENET_MEAN = [0.485, 0.456, 0.406]
IMAGENET_STD = [0.229, 0.224, 0.225]


class SmallCNN(nn.Module):
    """Three-block CNN for small datasets and quick experiments."""

    def __init__(self, num_classes: int) -> None:
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(3, 16, 3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
            nn.Conv2d(16, 32, 3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2),
            nn.Conv2d(32, 64, 3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d(1),
        )
        self.classifier = nn.Linear(64, num_classes)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.classifier(torch.flatten(self.features(x), 1))


def build_model(arch: str, num_classes: int) -> nn.Module:
    if arch == "smallcnn":
        return SmallCNN(num_classes)
    if arch == "resnet18":
        model = models.resnet18(weights=None)
        model.fc = nn.Linear(model.fc.in_features, num_classes)
        return model
    if arch == "mobilenet_v3_small":
        model = models.mobilenet_v3_small(weights=None)
        model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
        return model
    raise ValueError(f"unknown architecture: {arch}")


def train_transform(input_size: int) -> transforms.Compose:
    return transforms.Compose(
        [
            transforms.RandomResizedCrop(input_size, scale=(0.65, 1.0), ratio=(0.85, 1.18)),
            transforms.RandomHorizontalFlip(),
            transforms.ColorJitter(brightness=0.18, contrast=0.18, saturation=0.12),
            transforms.ToTensor(),
            transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
        ]
    )


def eval_transform(input_size: int) -> transforms.Compose:
    return transforms.Compose(
        [
            transforms.Resize(input_size),
            transforms.CenterCrop(input_size),
            transforms.ToTensor(),
            transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
        ]
    )


def save_package(
    directory: str | Path,
    model: nn.Module,
    classes: list[str],
    arch: str,
    input_size: int,
    metrics: dict,
    history: list[dict],
) -> Path:
    target = Path(directory)
    target.mkdir(parents=True, exist_ok=True)

    torch.save(
        {
            "arch": arch,
            "classes": list(classes),
            "input_size": int(input_size),
            "state_dict": model.state_dict(),
        },
        target / "model.pt",
    )

    metadata = {
        "format": "figtools-model/1",
        "kind": "classifier",
        "arch": arch,
        "classes": list(classes),
        "input_size": int(input_size),
        "mean": IMAGENET_MEAN,
        "std": IMAGENET_STD,
        "metrics": metrics,
        "created": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    (target / "model.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (target / "metrics.json").write_text(
        json.dumps({"history": history, "metrics": metrics}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    return target


def load_package(directory: str | Path):
    target = Path(directory)
    metadata_path = target / "model.json"
    weights_path = target / "model.pt"
    if not metadata_path.is_file() or not weights_path.is_file():
        raise FileNotFoundError(f"not a model package: {target}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    payload = torch.load(weights_path, map_location="cpu", weights_only=True)
    model = build_model(payload["arch"], len(payload["classes"]))
    model.load_state_dict(payload["state_dict"])
    model.eval()
    return model, metadata
