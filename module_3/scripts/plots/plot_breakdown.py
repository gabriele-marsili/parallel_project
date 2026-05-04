"""Phase breakdown stacked bar chart: Mod3 (loop+task uniform) vs Mod2."""

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
THREADS_SEL = [1, 4, 16]

cmap = matplotlib.colormaps["tab10"]
colors = [cmap(i) for i in range(len(PHASE_LABELS))]

# Mod3 uniform only
df3 = pd.read_csv(M3_CSV)
df3 = df3[(df3["workload"] == "uniform") & (df3["threads"].isin(THREADS_SEL))]
mean3 = df3.groupby(["impl", "threads"])[PHASES_M3].mean().reset_index()

# Mod2 (all uniform). Threads 16 not in CSV -> nearest available.
df2 = pd.read_csv(M2_CSV)
m2_thr_avail = sorted(df2["threads"].unique())


def m2_pick(t: int) -> int:
    if t in m2_thr_avail:
        return t
    # nearest >= t fallback to nearest absolute
    return min(m2_thr_avail, key=lambda x: abs(x - t))


mean2 = df2.groupby("threads")[PHASES_M2].mean().reset_index()

fig, axes = plt.subplots(1, 2, figsize=(8, 3.5))

# Left: Mod3 loop+task
ax = axes[0]
impls = ["loop", "task"]
group_labels = []
group_data = {p: [] for p in PHASES_M3}
for t in THREADS_SEL:
    for impl in impls:
        row = mean3[(mean3["threads"] == t) & (mean3["impl"] == impl)]
        for p in PHASES_M3:
            group_data[p].append(row[p].values[0] if not row.empty else 0.0)
        group_labels.append(f"T{t}\n{impl}")

x = np.arange(len(group_labels))
bottoms = np.zeros(len(group_labels))
for p, label, color in zip(PHASES_M3, PHASE_LABELS, colors):
    vals = np.array(group_data[p])
    ax.bar(x, vals, bottom=bottoms, width=0.6, label=label, color=color)
    bottoms += vals
ax.set_title("Mod3 (uniform)", fontsize=10)
ax.set_xlabel("Threads / Impl", fontsize=10)
ax.set_ylabel("Time (s)", fontsize=10)
ax.set_xticks(x)
ax.set_xticklabels(group_labels, fontsize=8)
ax.tick_params(labelsize=9)
ax.legend(fontsize=8, loc="upper right")

# Right: Mod2 stacked at same threads
ax = axes[1]
group_labels2 = []
group_data2 = {p: [] for p in PHASES_M2}
m2_used = []
for t in THREADS_SEL:
    t_use = m2_pick(t)
    m2_used.append(t_use)
    row = mean2[mean2["threads"] == t_use]
    for p in PHASES_M2:
        # ms -> s
        val = row[p].values[0] / 1000.0 if not row.empty else 0.0
        group_data2[p].append(val)
    label = f"T{t}" if t == t_use else f"T{t}\n(t={t_use})"
    group_labels2.append(label)

x2 = np.arange(len(group_labels2))
bottoms2 = np.zeros(len(group_labels2))
for p, label, color in zip(PHASES_M2, PHASE_LABELS, colors):
    vals = np.array(group_data2[p])
    ax.bar(x2, vals, bottom=bottoms2, width=0.5, label=label, color=color)
    bottoms2 += vals
ax.set_title("Mod2 (uniform)", fontsize=10)
ax.set_xlabel("Threads", fontsize=10)
ax.set_ylabel("Time (s)", fontsize=10)
ax.set_xticks(x2)
ax.set_xticklabels(group_labels2, fontsize=8)
ax.tick_params(labelsize=9)
ax.legend(fontsize=8, loc="upper right")

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(REPORT_DIR, "fig_breakdown.png"), dpi=150, bbox_inches="tight")
print("Saved fig_breakdown.pdf and fig_breakdown.png")
print("Mod2 threads used:", m2_used)
