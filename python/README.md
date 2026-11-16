# figtools Python worker

The desktop app does **not** bundle Python. It starts these scripts as child
processes and reads JSON events from stdout, so:

* a crash inside torch cannot take the editor down,
* the interpreter can be any conda/venv with torch installed,
* training and inference can be run without the GUI at all.

## Requirements

Python 3.9+ with `torch`, `torchvision` and `Pillow`. See `requirements.txt`.
The project was verified with Python 3.10 / torch 2.11 / CUDA 12.8.

**Optional — YOLO:** `pip install ultralytics>=8.4` in the training environment.
Ultralytics is **AGPL-3.0**. It is a separately installed training component and
is never bundled, linked into, or distributed with the C++ desktop app. The app
itself keeps working (classification, patch detector, all editing features)
without it; only the "YOLO 目标检测" task needs it.

In the desktop app open **模型 → 训练模型…** and set the Python interpreter.
The app also auto-detects conda environments under `~/.conda/envs` that contain
`torch`. The environment variable `FIGTOOLS_PYTHON` overrides everything.

## Train

Create a job file:

```json
{
  "data": "D:/data/parts",
  "output": "D:/models/parts-run1",
  "arch": "resnet18",
  "input_size": 224,
  "epochs": 20,
  "batch_size": 16,
  "lr": 0.001,
  "seed": 2026,
  "device": "auto",
  "val_ratio": 0.2
}
```

```bash
python train.py --job job.json
```

`data` accepts either:

1. **class folders** — `root/<class>/<image>.png`, the usual ImageFolder layout;
2. **a figtools dataset snapshot** — `images/`, `annotations/`, `splits/` and
   `dataset.json` as exported by the 数据集 page. The label is the image's most
   frequent annotation class; unannotated images are skipped.

The output directory is a model package:

| file | contents |
|---|---|
| `model.pt` | state dict + arch + classes + input size |
| `model.json` | the same metadata in a form the C++ side can read |
| `metrics.json` | per-epoch loss/accuracy and the final scores |
| `job.json` | the exact job, kept for reproducibility |

During training stdout carries one JSON object per line:

```
{"event":"dataset","classes":["a","b"],"train":28,"val":8,"test":4,...}
{"event":"epoch","epoch":1,"epochs":20,"train_loss":0.93,"val_accuracy":0.75,...}
{"event":"done","model_dir":"...","val_accuracy":0.98,...}
{"event":"error","message":"..."}
```

Human-readable logs go to stderr.

## YOLO detector (recommended)

The primary detection path uses Ultralytics YOLO (v8 / v11). It trains a real
detection head from the annotation boxes, so unlike the patch baseline it
handles multiple / multi-scale objects in one pass.

Prepare a snapshot with the 数据集 page (annotate first), then:

```json
{
  "data": "D:/data/detect-snapshot",
  "output": "D:/models/yolo-run1",
  "variant": "yolo11n.pt",
  "epochs": 50,
  "imgsz": 640,
  "batch_size": 16,
  "lr": 0.001,
  "seed": 2026,
  "device": "auto",
  "patience": 0
}
```

```bash
python train_yolo.py --job job.json
```

`train_yolo.py` first converts the snapshot to the YOLO layout
(`images/{train,val,test}`, `labels/{train,val,test}`, `data.yaml`), then trains
and writes a model package:

| file | contents |
|---|---|
| `best.pt` | native Ultralytics weights (retrain / export) |
| `weights.onnx` | ONNX export when available (best-effort) |
| `model.json` | `kind:"yolo"`, class names, id remap, metrics |
| `metrics.json` | per-epoch mAP / loss curves |
| `dataset/`, `classes.json`, `job.json` | exact inputs for reproducibility |

stdout uses the same NDJSON protocol; epoch events carry `mAP50`, `mAP50_95`,
`box_loss`, `cls_loss` and `total_epochs`. Class ids are remapped to a contiguous
YOLO range and mapped back on inference via `yolo_index_to_figtools_id`.

```bash
python detect_yolo.py --model D:/models/yolo-run1 --image D:/data/new.png \
    --conf 0.25 --iou 0.45
```

It prints the same JSON shape as `detect.py` (`detections` with
label/confidence/x/y/w/h), so the desktop canvas draws the boxes unchanged.
In the GUI choose 任务 = "YOLO 目标检测（推荐）" when training, then
**模型 → 检测当前图像** (`Ctrl+D`). If ultralytics is missing the worker emits a
clear `error` event instead of crashing.

## Detect (patch baseline, no extra dependency)

Classification assigns one label to a whole image. To find **where** something
is, train a detector from the boxes/polygons drawn in the 标注 page. This is a
sliding-window patch classifier -- deliberately not a full detection head --
so it trains in seconds on a CPU and makes the annotation layer actually feed
the model.

Build a dataset first: annotate images, open the 数据集 page, export a snapshot
(`images/`, `annotations/`, `dataset.json`). Then:

```json
{
  "data": "D:/data/detect-snapshot",
  "output": "D:/models/detector-run1",
  "arch": "smallcnn",
  "input_size": 64,
  "patch_size": 64,
  "context": 0.35,
  "positives_per_box": 3,
  "negatives": 3,
  "epochs": 15,
  "batch_size": 32,
  "lr": 0.002,
  "seed": 2026,
  "device": "auto"
}
```

```bash
python train_detect.py --job detect_job.json
```

`train_detect.py` crops each annotation's bounding box (plus jittered context)
into positive patches, samples background patches that barely overlap any
annotation, and trains a classifier that includes a `__background__` class.
The extra files it writes:

| file | contents |
|---|---|
| `patches/` | cropped training patches in class folders |
| `patches.json` | patch size, context, class list, counts, split |
| `model.json` | includes `"kind":"detector"`, `positive_classes`, `patch_size` |

Then slide it over a new image:

```bash
python detect.py --model D:/models/detector-run1 --image D:/data/new.png \
    --threshold 0.5 --nms 0.4 --scales 1.0,0.7,1.4
```

Prints one JSON object with `detections`, each `{label, confidence, x, y, w, h}`
in source-image pixels. The desktop app draws these boxes on the result image
in terracotta and reports the count in the status bar. NMS suppresses both
high-IoU overlaps and same-class windows whose centres are close (multi-scale
windows over one target).

In the GUI: **模型 → 训练模型…**, set 任务 to "目标检测（从标注框学习）";
then **模型 → 检测当前图像** (`Ctrl+D`). A classifier model cannot be used for
detection and vice versa -- the model package records its kind.

## Predict

```bash
python predict.py --model D:/models/parts-run1 --image D:/data/a.png --topk 3
```

Prints one JSON object:

```json
{"label":"a","confidence":0.876,"topk":[{"label":"a","score":0.876}],"arch":"resnet18","ms":27.1}
```

## Batch predict

```bash
python predict_batch.py --job batch_job.json
```

Job fields: `model`, `input` (folder or single image), `output` (`*.csv` or
`*.jsonl`), `recursive`, `topk`, `batch_size`, `device`, `threshold`.

stdout is NDJSON again: `phase` / `dataset` / `batch` (`done`, `total`,
`seconds`) / `done` (`counts`, `low_confidence`, `output`) / `error`.
Results contain `image`, `label`, `confidence`, and `label_1..k` / `score_1..k`.
CSV is written with a UTF-8 BOM so Excel opens Chinese labels correctly.

## Smoke test

```bash
python smoke_test.py
```

Runs four things end to end: a tiny two-class classification train, a single
prediction, batch inference over the folder with a CSV row check, and a
detector train from an annotated snapshot followed by a detection that must
find the target. Requires the same packages as training.
