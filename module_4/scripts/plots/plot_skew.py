"""Skewed vs uniform — show how skew interacts with the distributed pipeline."""

import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STRONG_CSV = os.path.join(BASE_DIR, "results", "cluster", "strong_scaling.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True


def plot() -> None:
    df = pd.read_csv(STRONG_CSV)
    med = df.groupby(["impl", "workload", "nodes"])["t_total_s"].median().reset_index()

    fig, axes = plt.subplots(1, 2, figsize=(8.5, 3.6), sharey=False)
    nodes_all = sorted(df["nodes"].unique())

    for ax, impl_key, title in zip(axes, ("mpi", "hybrid"),
                                   ("MPI puro", "Hybrid")):
        for wl, color, marker in [("uniform", "C0", "o"),
                                  ("skewed",  "C3", "s")]:
            sub = med[(med["impl"] == impl_key) & (med["workload"] == wl)] \
                .sort_values("nodes")
            if sub.empty:
                continue
            ax.plot(sub["nodes"].values, sub["t_total_s"].values, color=color,
                    marker=marker, markersize=6, linewidth=1.6, label=wl)
        ax.set_xscale("log", base=2)
        ax.set_xticks(nodes_all)
        ax.set_xticklabels([str(n) for n in nodes_all])
        ax.set_xlabel("Nodi", fontsize=10)
        ax.set_title(title, fontsize=10)
        ax.tick_params(labelsize=9)
        ax.legend(fontsize=9)

    axes[0].set_ylabel("Tempo (s)", fontsize=10)
    fig.tight_layout()
    os.makedirs(REPORT_DIR, exist_ok=True)
    fig.savefig(os.path.join(REPORT_DIR, "fig_skew.pdf"), bbox_inches="tight")
    fig.savefig(os.path.join(REPORT_DIR, "fig_skew.png"), dpi=150, bbox_inches="tight")
    print("Saved fig_skew.{pdf,png}")


if __name__ == "__main__":
    plot()
