"""Weak scaling efficiency for M4. Per-rank workload constant; rank count grows."""

import os
from typing import Iterable

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEAK_CSV = os.path.join(BASE_DIR, "results", "cluster", "weak_scaling.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True


def median_by(df: pd.DataFrame, keys: Iterable[str]) -> pd.DataFrame:
    return df.groupby(list(keys))["t_total_s"].median().reset_index()


def plot() -> None:
    df = pd.read_csv(WEAK_CSV)
    med = median_by(df, ["impl", "nodes"])

    series = [
        ("mpi",    "MPI puro (32 rank/nodo)", "C0", "-",  "o"),
        ("hybrid", "Hybrid (1 rank/nodo, 32 thr)", "C1", "--", "s"),
    ]

    fig, ax = plt.subplots(figsize=(5.5, 3.6))
    nodes_all = sorted(df["nodes"].unique())
    ax.axhline(y=1.0, color="grey", linestyle="--", linewidth=1.0,
               alpha=0.6, label="Ideale (= 1)", zorder=1)

    for impl_key, label, color, ls, marker in series:
        sub = med[med["impl"] == impl_key].sort_values("nodes")
        if sub.empty:
            continue
        t1 = sub[sub["nodes"] == 1]["t_total_s"].values
        if len(t1) == 0:
            continue
        eff = t1[0] / sub["t_total_s"].values
        ax.plot(sub["nodes"].values, eff, color=color, linestyle=ls,
                marker=marker, markersize=6, linewidth=1.6, label=label, zorder=2)

    ax.set_xscale("log", base=2)
    ax.set_xticks(nodes_all)
    ax.set_xticklabels([str(n) for n in nodes_all])
    ax.set_xlabel("Nodi", fontsize=10)
    ax.set_ylabel(r"Efficienza weak  $t_1 / t_N$", fontsize=10)
    ax.tick_params(labelsize=9)
    ax.set_ylim(0, 1.15)
    ax.legend(fontsize=9, loc="lower left")
    fig.tight_layout()
    os.makedirs(REPORT_DIR, exist_ok=True)
    fig.savefig(os.path.join(REPORT_DIR, "fig_weak.pdf"), bbox_inches="tight")
    fig.savefig(os.path.join(REPORT_DIR, "fig_weak.png"), dpi=150, bbox_inches="tight")
    print("Saved fig_weak.{pdf,png}")


if __name__ == "__main__":
    plot()
