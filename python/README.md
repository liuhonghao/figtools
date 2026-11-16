# figtools Python worker

The desktop app does **not** bundle Python. It starts these scripts as child
processes and reads JSON events from stdout, so:

* a crash inside torch cannot take the editor down,
* the interpreter can be any conda/venv with torch installed,
* training and inference can be run without the GUI at all.

## Requirements

Python 3.9+ with `torch`, `torchvision` and `Pillow`. See `requirements.txt`.
The project was verified with Python 3.10 / torch 2.11 (CUDA optional).

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

Generates a tiny two-class dataset, trains for a few epochs, predicts one
image, then runs batch inference over the whole folder and checks the CSV row
count. Requires the same packages as training.
