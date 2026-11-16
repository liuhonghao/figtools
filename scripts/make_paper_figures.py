"""Generate publication-quality figures and tables from a YOLO training log.

Reads Ultralytics ``results.csv`` and produces, in the output directory:

  * fig_training_loss.pdf/.png   - box/cls/dfl loss vs epoch (2 panels with mAP)
  * fig_metrics.pdf/.png         - precision / recall / mAP curves
  * table_metrics.md             - per-epoch metric table (Markdown)
  * table_metrics.tex            - the same table, LaTeX booktabs
  * table_summary.md/.tex        - final-epoch summary

Design goals (per user request): clear logic, crisp at print resolution,
legible when dropped straight into a paper. 300 DPI PNG + vector PDF, serif
font, explicit axes/units/legend, light grid, no chartjunk.

Usage::

    python scripts/make_paper_figures.py \
        --csv build/ttpla_model/run/train/results.csv \
        --out build/paper \
        --title "TTPLA 输电线路目标检测训练" --lang zh
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager

# --- publication styling ----------------------------------------------------
plt.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "DejaVu Serif"],
    "mathtext.fontset": "stix",
    "font.size": 11,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "legend.fontsize": 10,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "figure.dpi": 120,
    "savefig.dpi": 300,
    "savefig.bbox": "tight",
    "savefig.pad_inches": 0.08,
    "axes.grid": True,
    "grid.alpha": 0.35,
    "grid.linestyle": "--",
    "axes.spines.top": False,
    "axes.spines.right": False,
})

COLOURS = {
    "box": "#B4562A",
    "cls": "#2E6FB7",
    "dfl": "#4F7A4A",
    "map50": "#B4562A",
    "map5095": "#2E6FB7",
    "precision": "#A9781A",
    "recall": "#4F7A4A",
}

LABELS_ZH = {
    "epoch": "训练轮次 (Epoch)",
    "loss": "损失值 (Loss)",
    "map": "平均精度 (mAP)",
    "metric": "指标值",
    "box": "边界框损失 (box_loss)",
    "cls": "分类损失 (cls_loss)",
    "dfl": "分布焦点损失 (dfl_loss)",
    "map50": "mAP@0.5",
    "map5095": "mAP@0.5:0.95",
    "precision": "精确率 (Precision)",
    "recall": "召回率 (Recall)",
}
LABELS_EN = {
    "epoch": "Epoch",
    "loss": "Loss",
    "map": "Mean Average Precision (mAP)",
    "metric": "Metric value",
    "box": "Box loss",
    "cls": "Classification loss",
    "dfl": "Distribution focal loss",
    "map50": "mAP@0.5",
    "map5095": "mAP@0.5:0.95",
    "precision": "Precision",
    "recall": "Recall",
}


def read_csv(path: Path) -> dict[str, list[float]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        columns: dict[str, list[float]] = {name: [] for name in reader.fieldnames or []}
        for row in reader:
            for name, value in row.items():
                try:
                    columns[name].append(float(value))
                except (TypeError, ValueError):
                    columns[name].append(float("nan"))
    return columns


def use_chinese_font() -> bool:
    """Enable a CJK-capable font with graceful fallback for Latin glyphs.

    matplotlib falls back through ``font.family`` in order: SimHei draws the
    Chinese characters, Times New Roman draws the Latin/digits in serif. Setting
    only font.sans-serif is not enough when font.family is fixed to serif.
    """
    fonts = {f.name for f in font_manager.fontManager.ttflist}
    if "SimHei" in fonts:
        plt.rcParams["font.family"] = ["SimHei", "Times New Roman", "DejaVu Serif"]
        plt.rcParams["axes.unicode_minus"] = False
        return True
    return False


def figure_training(data: dict[str, list[float]], out: Path, title: str, labels: dict) -> None:
    epochs = data["epoch"]
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7.2, 7.6), sharex=True)

    series_loss = [
        ("train/box_loss", "box", COLOURS["box"], "-"),
        ("train/cls_loss", "cls", COLOURS["cls"], "-"),
        ("train/dfl_loss", "dfl", COLOURS["dfl"], "-"),
    ]
    for key, label, colour, style in series_loss:
        if key in data:
            ax1.plot(epochs, data[key], style, color=colour, linewidth=2,
                     marker="o", markersize=3.5, label=labels[label])
    ax1.set_ylabel(labels["loss"])
    ax1.set_title(title, pad=10)
    ax1.legend(loc="upper right", framealpha=0.95, edgecolor="#CCCCCC")
    ax1.grid(True, alpha=0.35)

    for key, label, colour in [
        ("metrics/mAP50(B)", "map50", COLOURS["map50"]),
        ("metrics/mAP50-95(B)", "map5095", COLOURS["map5095"]),
    ]:
        if key in data:
            ax2.plot(epochs, data[key], "-", color=colour, linewidth=2,
                     marker="s", markersize=3.5, label=labels[label])
    ax2.set_xlabel(labels["epoch"])
    ax2.set_ylabel(labels["map"])
    ax2.legend(loc="lower right", framealpha=0.95, edgecolor="#CCCCCC")
    ax2.set_ylim(bottom=0)
    ax2.grid(True, alpha=0.35)

    ax2.set_xticks(range(1, len(epochs) + 1, max(1, len(epochs) // 12)))
    fig.tight_layout()
    for suffix in ("pdf", "png"):
        fig.savefig(out / f"fig_training_loss.{suffix}")
    plt.close(fig)


def figure_metrics(data: dict[str, list[float]], out: Path, title: str, labels: dict) -> None:
    epochs = data["epoch"]
    fig, ax = plt.subplots(figsize=(7.2, 4.6))
    for key, label, colour, marker in [
        ("metrics/precision(B)", "precision", COLOURS["precision"], "o"),
        ("metrics/recall(B)", "recall", COLOURS["recall"], "s"),
        ("metrics/mAP50(B)", "map50", COLOURS["map50"], "^"),
        ("metrics/mAP50-95(B)", "map5095", COLOURS["map5095"], "D"),
    ]:
        if key in data:
            ax.plot(epochs, data[key], "-", color=colour, linewidth=2,
                    marker=marker, markersize=4, label=labels[label])
    ax.set_xlabel(labels["epoch"])
    ax.set_ylabel(labels["metric"])
    ax.set_title(title, pad=10)
    ax.set_ylim(-0.02, 1.02)
    ax.legend(loc="center right", framealpha=0.95, edgecolor="#CCCCCC")
    ax.set_xticks(range(1, len(epochs) + 1, max(1, len(epochs) // 12)))
    fig.tight_layout()
    for suffix in ("pdf", "png"):
        fig.savefig(out / f"fig_metrics.{suffix}")
    plt.close(fig)


def fmt(value: float) -> str:
    return f"{value:.4f}" if value == value else "—"  # NaN -> dash


def write_tables(data: dict[str, list[float]], out: Path) -> None:
    epochs = data["epoch"]
    columns = [
        ("epoch", "Epoch"),
        ("train/box_loss", "Box loss"),
        ("train/cls_loss", "Cls loss"),
        ("train/dfl_loss", "DFL loss"),
        ("metrics/precision(B)", "Precision"),
        ("metrics/recall(B)", "Recall"),
        ("metrics/mAP50(B)", "mAP@0.5"),
        ("metrics/mAP50-95(B)", "mAP@0.5:0.95"),
    ]
    columns = [(key, name) for key, name in columns if key in data]

    # Markdown
    lines = ["| " + " | ".join(name for _, name in columns) + " |",
             "|" + "|".join("---" for _ in columns) + "|"]
    for index in range(len(epochs)):
        lines.append("| " + " | ".join(
            str(int(data[key][index])) if key == "epoch" else fmt(data[key][index])
            for key, _ in columns) + " |")
    (out / "table_metrics.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # LaTeX booktabs
    tex = ["\\begin{table}[htbp]", "\\centering",
           "\\caption{Per-epoch training metrics.}", "\\label{tab:metrics}",
           "\\begin{tabular}{" + "c" * len(columns) + "}", "\\toprule"]
    tex.append(" & ".join(name.replace("@", "@\\,") for _, name in columns) + " \\\\")
    tex.append("\\midrule")
    for index in range(len(epochs)):
        tex.append(" & ".join(
            str(int(data[key][index])) if key == "epoch" else fmt(data[key][index])
            for key, _ in columns) + " \\\\")
    tex.extend(["\\bottomrule", "\\end{tabular}", "\\end{table}"])
    (out / "table_metrics.tex").write_text("\n".join(tex) + "\n", encoding="utf-8")

    # Final-epoch summary (best mAP is more meaningful for the paper).
    map_key = "metrics/mAP50(B)"
    best_index = max(range(len(epochs)), key=lambda i: data[map_key][i]) if map_key in data else len(epochs) - 1
    summary = [
        "| 指标 (Metric) | 数值 (Value) |",
        "|---|---|",
        f"| 训练轮次 (Epochs) | {len(epochs)} |",
        f"| 最佳轮次 (Best epoch) | {int(epochs[best_index])} |",
        f"| mAP@0.5 | {fmt(data[map_key][best_index]) if map_key in data else '—'} |",
        f"| mAP@0.5:0.95 | {fmt(data['metrics/mAP50-95(B)'][best_index]) if 'metrics/mAP50-95(B)' in data else '—'} |",
        f"| Precision | {fmt(data['metrics/precision(B)'][best_index]) if 'metrics/precision(B)' in data else '—'} |",
        f"| Recall | {fmt(data['metrics/recall(B)'][best_index]) if 'metrics/recall(B)' in data else '—'} |",
        f"| 总训练时间 (s) | {fmt(data['time'][-1]) if 'time' in data else '—'} |",
    ]
    (out / "table_summary.md").write_text("\n".join(summary) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Publication figures from YOLO results.csv")
    parser.add_argument("--csv", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--title", default="Training curves")
    parser.add_argument("--lang", choices=["zh", "en"], default="en")
    args = parser.parse_args()

    csv_path = Path(args.csv)
    if not csv_path.is_file():
        print(f"error: csv not found: {csv_path}")
        return 2
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    zh = args.lang == "zh" and use_chinese_font()
    labels = LABELS_ZH if zh else LABELS_EN

    data = read_csv(csv_path)
    if "epoch" not in data or not data["epoch"]:
        print("error: no epoch column in csv")
        return 2

    figure_training(data, out, args.title, labels)
    figure_metrics(data, out, args.title, labels)
    write_tables(data, out)
    print(f"figures and tables written to {out.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
