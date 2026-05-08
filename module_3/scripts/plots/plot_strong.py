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
    # Ideal line capped at 16 (physical cores) — beyond that SMT
    # threads share cache bandwidth, so linear ideal is misleading.
    ideal_x = [t for t in threads_all if t <= 16]
    ax.plot(
        ideal_x, ideal_x,
        color="grey", linestyle="--", linewidth=1.0,
        label="Ideal (≤16 cores)", zorder=1, alpha=0.6,
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

    ax.set_xscale("log", base=2)
    ax.set_title(wl.capitalize(), fontsize=10)
    ax.set_xlabel("Threads", fontsize=10)
    ax.set_ylabel("Speedup", fontsize=10)
    ax.tick_params(labelsize=9)
    ax.set_xticks(threads_all)
    ax.set_xticklabels([str(t) for t in threads_all])
    ax.axvline(x=16, color="darkgray", linestyle=":", linewidth=1.0, zorder=0,
               alpha=0.7)
    ax.text(16, ax.get_ylim()[1] * 0.05, " 16 cores", fontsize=8,
            color="dimgray", va="bottom", ha="left", style="italic")
    # Tighten y-range to actual data extent: cap to the largest visible
    # speedup with a small headroom; the ideal line goes off-axis on
    # purpose since intra-implementation T(1)/T(p) is bounded by p anyway.
    sub_all = mean3[mean3["workload"] == wl]
    if not sub_all.empty:
        t1_loop = sub_all[(sub_all["impl"] == "loop") & (sub_all["threads"] == 1)]["t_total_s"].values[0]
        max_speedup = (t1_loop / sub_all["t_total_s"]).max()
        ax.set_ylim(0, max_speedup * 1.18)
    ax.legend(fontsize=9, loc="upper left")

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_strong.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(REPORT_DIR, "fig_strong.png"), dpi=150, bbox_inches="tight")
print("Saved fig_strong.pdf and fig_strong.png")
