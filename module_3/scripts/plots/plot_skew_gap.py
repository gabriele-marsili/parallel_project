import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
M3_CSV = os.path.join(BASE_DIR, "results", "cluster", "strong_scaling.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True

df = pd.read_csv(M3_CSV)
df = df[df["workload"] == "skewed"]
mean = df.groupby(["impl", "threads"])[["t_join", "t_total_s"]].mean().reset_index()

threads = sorted(mean["threads"].unique())
loop = mean[mean["impl"] == "loop"].set_index("threads").reindex(threads)
task = mean[mean["impl"] == "task"].set_index("threads").reindex(threads)

fig, axes = plt.subplots(1, 2, figsize=(8, 3.2))

ax = axes[0]
ax.plot(threads, loop["t_join"].values * 1000, color="C0", marker="o",
        linestyle="-", linewidth=1.5, label="loop")
ax.plot(threads, task["t_join"].values * 1000, color="C1", marker="s",
        linestyle="--", linewidth=1.5, label="task (LPT + hot-split)")
ax.set_xscale("log", base=2)
ax.set_yscale("log")
ax.set_xticks(threads)
ax.set_xticklabels([str(t) for t in threads])
ax.set_xlabel("Threads", fontsize=10)
ax.set_ylabel("Join phase time [ms] (log)", fontsize=10)
ax.set_title("(a) Skewed: join time", fontsize=10)
ax.tick_params(labelsize=9)
ax.axvline(x=16, color="darkgray", linestyle=":", linewidth=1.0,
           alpha=0.7, zorder=0)
ax.legend(fontsize=9)

ax = axes[1]
ratio = loop["t_total_s"].values / task["t_total_s"].values
ax.plot(threads, ratio, color="C2", marker="d", linewidth=1.5)
ax.axhline(y=1.0, color="grey", linestyle=":", linewidth=1.0)
ax.axvline(x=16, color="darkgray", linestyle=":", linewidth=1.0,
           alpha=0.7, zorder=0)
ax.set_xscale("log", base=2)
ax.set_xticks(threads)
ax.set_xticklabels([str(t) for t in threads])
ax.set_xlabel("Threads", fontsize=10)
ax.set_ylabel(r"$T_{\rm loop}/T_{\rm task}$", fontsize=10)
ax.set_title("(b) Skewed: task/loop total-time ratio", fontsize=10)
ax.tick_params(labelsize=9)
for xi, yi in zip(threads, ratio):
    ax.text(xi, yi + 0.012, f"{yi:.2f}", ha="center", va="bottom", fontsize=8)
ax.set_ylim(0.95, max(1.28, ratio.max() + 0.08))

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_skew_gap.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(REPORT_DIR, "fig_skew_gap.png"), dpi=150, bbox_inches="tight")
print("Saved fig_skew_gap.pdf and fig_skew_gap.png")
