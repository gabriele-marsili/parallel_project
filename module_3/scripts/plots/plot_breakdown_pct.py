"""100%-stacked phase breakdown: Mod2 vs Mod3 (uniform + skewed)."""

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
M3_CSV = os.path.join(BASE_DIR, "results", "cluster", "breakdown.csv")
M2_CSV = os.path.join(BASE_DIR, "..", "module_2", "results", "phase_breakdown.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True

PHASES_M3 = ["t_hist_r", "t_scatter_r", "t_hist_s", "t_scatter_s", "t_join"]
PHASES_M2 = [
    "histogram_R_ms",
    "scatter_R_ms",
    "histogram_S_ms",
    "scatter_S_ms",
    "join_local_ms",
]
PHASE_LABELS = ["Hist R", "Scatter R", "Hist S", "Scatter S", "Join"]
THREADS_SEL = [1, 8, 16]

cmap = matplotlib.colormaps["tab10"]
colors = [cmap(i) for i in range(len(PHASE_LABELS))]

df3 = pd.read_csv(M3_CSV)
mean3 = df3.groupby(["impl", "workload", "threads"])[PHASES_M3].mean().reset_index()

df2 = pd.read_csv(M2_CSV)
mean2 = df2.groupby("threads")[PHASES_M2].mean().reset_index()
m2_thr_avail = sorted(mean2["threads"].unique())


def m2_pick(t: int) -> int:
    if t in m2_thr_avail:
        return t
    return min(m2_thr_avail, key=lambda x: abs(x - t))


def phase_pct_m3(impl: str, workload: str, t: int) -> np.ndarray:
    row = mean3[
        (mean3["impl"] == impl)
        & (mean3["workload"] == workload)
        & (mean3["threads"] == t)
    ]
    if row.empty:
        return np.zeros(len(PHASES_M3))
    vals = np.array([row[p].values[0] for p in PHASES_M3])
    total = vals.sum()
    return 100.0 * vals / total if total > 0 else vals


def phase_pct_m2(t: int) -> np.ndarray:
    t_use = m2_pick(t)
    row = mean2[mean2["threads"] == t_use]
    if row.empty:
        return np.zeros(len(PHASES_M2))
    vals = np.array([row[p].values[0] for p in PHASES_M2])  # ms
    total = vals.sum()
    return 100.0 * vals / total if total > 0 else vals


fig, axes = plt.subplots(1, 2, figsize=(8, 5))

# Left: uniform — Mod2 + Mod3 loop + Mod3 task
ax = axes[0]
labels = []
data = []
for t in THREADS_SEL:
    t_m2 = m2_pick(t)
    m2_lab = f"M2-t{t}" if t == t_m2 else f"M2-t{t_m2}"
    labels.append(m2_lab)
    data.append(phase_pct_m2(t))
    labels.append(f"M3-loop-t{t}")
    data.append(phase_pct_m3("loop", "uniform", t))
    labels.append(f"M3-task-t{t}")
    data.append(phase_pct_m3("task", "uniform", t))

data = np.array(data)
x = np.arange(len(labels))
bottoms = np.zeros(len(labels))
for i, (label, color) in enumerate(zip(PHASE_LABELS, colors)):
    ax.bar(x, data[:, i], bottom=bottoms, width=0.7, label=label, color=color)
    bottoms += data[:, i]
ax.set_title("Uniform", fontsize=10)
ax.set_ylabel("% of total time", fontsize=10)
ax.set_xticks(x)
ax.set_xticklabels(labels, fontsize=7, rotation=45, ha="right")
ax.tick_params(labelsize=9)
ax.set_ylim(0, 100)
ax.legend(fontsize=8, loc="lower right")

# Right: skewed (Mod3 only)
ax = axes[1]
labels_s = []
data_s = []
for t in THREADS_SEL:
    labels_s.append(f"M3-loop-t{t}")
    data_s.append(phase_pct_m3("loop", "skewed", t))
    labels_s.append(f"M3-task-t{t}")
    data_s.append(phase_pct_m3("task", "skewed", t))

data_s = np.array(data_s)
xs = np.arange(len(labels_s))
bottoms = np.zeros(len(labels_s))
for i, (label, color) in enumerate(zip(PHASE_LABELS, colors)):
    ax.bar(xs, data_s[:, i], bottom=bottoms, width=0.6, label=label, color=color)
    bottoms += data_s[:, i]
ax.set_title("Skewed (Mod3 only)", fontsize=10)
ax.set_ylabel("% of total time", fontsize=10)
ax.set_xticks(xs)
ax.set_xticklabels(labels_s, fontsize=7, rotation=45, ha="right")
ax.tick_params(labelsize=9)
ax.set_ylim(0, 100)
ax.legend(fontsize=8, loc="lower right")

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown_pct.pdf"), bbox_inches="tight")
fig.savefig(
    os.path.join(REPORT_DIR, "fig_breakdown_pct.png"), dpi=150, bbox_inches="tight"
)
print("Saved fig_breakdown_pct.pdf and fig_breakdown_pct.png")
