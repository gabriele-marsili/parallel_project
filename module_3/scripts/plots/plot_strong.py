"""Strong scaling plot: speedup vs threads. Mod3 loop+task vs Mod2 std::thread."""

import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BASE_DIR = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
M3_CSV = os.path.join(BASE_DIR, "results", "cluster", "strong_scaling.csv")
M2_CSV = os.path.join(BASE_DIR, "..", "module_2", "results", "strong_scaling.csv")
REPORT_DIR = os.path.join(BASE_DIR, "report")

try:
    plt.style.use("seaborn-v0_8-whitegrid")
except OSError:
    plt.rcParams["axes.grid"] = True

# Mod3
df3 = pd.read_csv(M3_CSV)
mean3 = df3.groupby(["impl", "workload", "threads"])["t_total_s"].mean().reset_index()

# Mod2 (NR=10M only, uniform-only by construction)
df2 = pd.read_csv(M2_CSV)
df2 = df2[df2["nr"] == 10_000_000].copy()

workloads = ["uniform", "skewed"]
impls = [
    ("loop", "Mod3-loop", "C0", "-", "o"),
    ("task", "Mod3-task", "C1", "--", "s"),
]

fig, axes = plt.subplots(1, 2, figsize=(8, 3.5), sharey=False)

for ax, wl in zip(axes, workloads):
    threads_all = sorted(mean3["threads"].unique())
    ax.plot(
        threads_all,
        threads_all,
        color="grey",
        linestyle="--",
        linewidth=1.0,
        label="Ideal",
        zorder=1,
    )

    for impl_key, impl_label, color, ls, marker in impls:
        sub = mean3[
            (mean3["impl"] == impl_key) & (mean3["workload"] == wl)
        ].sort_values("threads")
        if sub.empty:
            continue
        t1_arr = sub[sub["threads"] == 1]["t_total_s"].values
        if len(t1_arr) == 0:
            continue
        t1 = t1_arr[0]
        speedup = t1 / sub["t_total_s"].values
        ax.plot(
            sub["threads"].values,
            speedup,
            color=color,
            linestyle=ls,
            marker=marker,
            markersize=5,
            linewidth=1.5,
            label=impl_label,
            zorder=2,
        )

    if wl == "uniform":
        m2_thr_set = {1, 2, 4, 8, 16}
        sub2 = df2[df2["threads"].isin(m2_thr_set)].sort_values("threads")
        speedup2 = (sub2["time_seq"] / sub2["time_par"]).values
        ax.plot(
            sub2["threads"].values,
            speedup2,
            color="C2",
            linestyle=":",
            marker="^",
            markersize=5,
            linewidth=1.5,
            label="Mod2-thread",
            zorder=2,
        )

    ax.set_xscale("log", base=2)
    ax.set_title(wl.capitalize(), fontsize=10)
    ax.set_xlabel("Threads", fontsize=10)
    ax.set_ylabel("Speedup", fontsize=10)
    ax.tick_params(labelsize=9)
    ax.set_xticks(threads_all)
    ax.set_xticklabels([str(t) for t in threads_all])
    ax.legend(fontsize=9)

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_strong.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(REPORT_DIR, "fig_strong.png"), dpi=150, bbox_inches="tight")
print("Saved fig_strong.pdf and fig_strong.png")
