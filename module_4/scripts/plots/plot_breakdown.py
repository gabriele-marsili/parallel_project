"""Stacked phase breakdown — communication vs local computation."""

import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BD_CSV = os.path.join(BASE_DIR, "results", "cluster", "breakdown.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

PHASES = [
    ("t_hist_local",    "Histogram local",  "#4c72b0"),
    ("t_scatter_local", "Scatter local",    "#55a868"),
    ("t_comm_sizes",    "Comm sizes",       "#c4c4c4"),
    ("t_comm_payload",  "Comm payload",     "#dd8452"),
    ("t_hist_post",     "Histogram post",   "#8172b3"),
    ("t_scatter_post",  "Scatter post",     "#937860"),
    ("t_join",          "Join local",       "#da8bc3"),
    ("t_reduce",        "Reduce final",     "#8c8c8c"),
]

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True


def plot() -> None:
    df = pd.read_csv(BD_CSV)
    agg_cols = [c for c, _, _ in PHASES]
    med = df.groupby(["impl", "workload"])[agg_cols].median().reset_index()

    labels = [f"{row['impl']}\n{row['workload']}" for _, row in med.iterrows()]
    x = np.arange(len(labels))
    bottom = np.zeros(len(labels))

    fig, ax = plt.subplots(figsize=(7.5, 4.0))
    for col, label, color in PHASES:
        vals = med[col].values
        ax.bar(x, vals, bottom=bottom, label=label, color=color, edgecolor="white",
               linewidth=0.4)
        bottom += vals

    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=9)
    ax.set_ylabel("Tempo (s, median rank-max)", fontsize=10)
    ax.tick_params(labelsize=9)
    ax.legend(fontsize=8, loc="center left", bbox_to_anchor=(1.01, 0.5),
              frameon=False)
    fig.tight_layout()
    os.makedirs(REPORT_DIR, exist_ok=True)
    fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown.pdf"), bbox_inches="tight")
    fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown.png"), dpi=150, bbox_inches="tight")
    print("Saved fig_breakdown.{pdf,png}")


if __name__ == "__main__":
    plot()
