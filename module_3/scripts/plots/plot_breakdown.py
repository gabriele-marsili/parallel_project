"""Phase breakdown stacked bar chart for Mod3 (uniform | skewed)."""

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
M3_CSV = os.path.join(BASE_DIR, "results", "cluster", "breakdown.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True

PHASES = ["t_hist_r", "t_scatter_r", "t_hist_s", "t_scatter_s", "t_join"]
PHASE_LABELS = ["Hist R", "Scatter R", "Hist S", "Scatter S", "Join"]
# Skip T=1 because it dominates visually and obscures the parallel scaling.
THREADS_SEL = [4, 8, 16, 20]
IMPLS = ["loop", "task"]

cmap = matplotlib.colormaps["tab10"]
colors = [cmap(i) for i in range(len(PHASE_LABELS))]

df = pd.read_csv(M3_CSV)
df = df[df["threads"].isin(THREADS_SEL)]
mean = df.groupby(["impl", "workload", "threads"])[PHASES].mean().reset_index()

fig, axes = plt.subplots(1, 2, figsize=(8, 3.5), sharey=False)

for ax, wl in zip(axes, ["uniform", "skewed"]):
    labels = []
    data = {p: [] for p in PHASES}
    for t in THREADS_SEL:
        for impl in IMPLS:
            row = mean[(mean["threads"] == t) &
                       (mean["impl"] == impl) &
                       (mean["workload"] == wl)]
            for p in PHASES:
                data[p].append(row[p].values[0] if not row.empty else 0.0)
            labels.append(f"T{t}\n{impl}")

    x = np.arange(len(labels))
    # Convert to ms for readability (parallel times are < 200ms)
    bottoms = np.zeros(len(labels))
    totals = np.zeros(len(labels))
    for p, lab, color in zip(PHASES, PHASE_LABELS, colors):
        vals = np.array(data[p]) * 1000.0
        ax.bar(x, vals, bottom=bottoms, width=0.65, label=lab, color=color,
               edgecolor="white", linewidth=0.4)
        bottoms += vals
        totals += vals
    # Annotate total on top of each bar
    for xi, tot in zip(x, totals):
        ax.text(xi, tot * 1.02, f"{tot:.0f}", ha="center", va="bottom",
                fontsize=8, color="dimgray")

    # Visual separator between thread groups
    for i in range(1, len(THREADS_SEL)):
        ax.axvline(x=2 * i - 0.5, color="lightgray", linestyle="-",
                   linewidth=0.5, zorder=0)

    ax.set_title(wl.capitalize(), fontsize=10)
    ax.set_xlabel("Threads / Variant", fontsize=10)
    ax.set_ylabel("Time [ms]", fontsize=10)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=8)
    ax.tick_params(labelsize=9)
    ax.set_ylim(0, totals.max() * 1.12)
    if wl == "uniform":
        ax.legend(fontsize=8, loc="upper right", framealpha=0.9)

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown.png"), dpi=150, bbox_inches="tight")
print("Saved fig_breakdown.pdf and fig_breakdown.png")
