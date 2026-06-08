"""Strong scaling plot for M4: speedup vs node count, pure MPI and hybrid.

Speedup is computed against the M3 sequential baseline that the M4 campaign
re-ran on the same compute hardware (`seq_baseline.csv`).
"""

import os
from typing import Iterable

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STRONG_CSV = os.path.join(BASE_DIR, "results", "cluster", "strong_scaling.csv")
SEQ_CSV = os.path.join(BASE_DIR, "results", "cluster", "seq_baseline.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True


def median_by(df: pd.DataFrame, keys: Iterable[str]) -> pd.DataFrame:
    return df.groupby(list(keys))["t_total_s"].median().reset_index()


def plot() -> None:
    df = pd.read_csv(STRONG_CSV)
    seq = pd.read_csv(SEQ_CSV)
    t_seq_by_wl = seq.groupby("workload")["t_total_s"].median().to_dict()

    med = median_by(df, ["impl", "workload", "nodes"])
    workloads = ["uniform", "skewed"]
    series = [
        ("mpi",    "Pure MPI (32 ranks/node)", "C0", "-",  "o"),
        ("hybrid", "Hybrid (1 rank/node, 32 threads)", "C1", "--", "s"),
    ]

    # Independent y-scales: uniform reaches ~12x, skewed stays under 3x, so a
    # shared axis would flatten the skewed panel into illegibility.
    fig, axes = plt.subplots(1, 2, figsize=(8.5, 3.6), sharey=False)
    nodes_all = sorted(df["nodes"].unique())

    for ax, wl in zip(axes, workloads):
        t_seq = t_seq_by_wl.get(wl, t_seq_by_wl["uniform"])
        for impl_key, label, color, ls, marker in series:
            sub = med[(med["impl"] == impl_key) & (med["workload"] == wl)].sort_values("nodes")
            if sub.empty:
                continue
            nodes = sub["nodes"].values
            speedup = t_seq / sub["t_total_s"].values
            ax.plot(nodes, speedup, color=color, linestyle=ls,
                    marker=marker, markersize=6, linewidth=1.6, label=label, zorder=2)
            # Labels above for MPI, below for hybrid, so they never collide
            # where the two curves nearly touch.
            dy = 7 if impl_key == "mpi" else -12
            va = "bottom" if dy > 0 else "top"
            for n, s in zip(nodes, speedup):
                ax.annotate(f"{s:.1f}", (n, s), textcoords="offset points",
                            xytext=(0, dy), ha="center", va=va, fontsize=7.5, color=color)

        ax.set_xscale("log", base=2)
        ax.set_xticks(nodes_all)
        ax.set_xticklabels([str(n) for n in nodes_all])
        ax.set_title(wl.capitalize(), fontsize=10)
        ax.set_xlabel("Nodes", fontsize=10)
        ax.tick_params(labelsize=9)
        ax.margins(y=0.15)

    axes[0].set_ylabel("Speedup vs sequential", fontsize=10)
    axes[1].legend(fontsize=8, loc="upper left")
    axes[0].legend(fontsize=8, loc="upper left")
    fig.tight_layout()
    os.makedirs(REPORT_DIR, exist_ok=True)
    fig.savefig(os.path.join(REPORT_DIR, "fig_strong.pdf"), bbox_inches="tight")
    fig.savefig(os.path.join(REPORT_DIR, "fig_strong.png"), dpi=150, bbox_inches="tight")
    print("Saved fig_strong.{pdf,png}")


if __name__ == "__main__":
    plot()
