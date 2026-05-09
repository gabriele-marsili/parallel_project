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

THREADS_SEL_M3 = [1, 2, 4, 8, 16, 20, 32]
THREADS_SEL_M2 = [1, 2, 4, 8, 16]

df3 = pd.read_csv(M3_CSV)
df3 = df3[(df3["workload"] == "uniform") & (df3["threads"].isin(THREADS_SEL_M3))]
mean3 = df3.groupby(["impl", "threads"])["t_total_s"].mean().reset_index()

df2 = pd.read_csv(M2_CSV)
m2_avail = sorted(df2["threads"].unique())
THREADS_SEL_M2 = [t for t in [1, 2, 4, 8, 16, 20, 32] if t in m2_avail]
df2 = df2[(df2["nr"] == 10_000_000) & (df2["threads"].isin(THREADS_SEL_M2))].sort_values(
    "threads"
)

m2_t = df2["threads"].values
m2_time = df2["time_par"].values
m2_speedup = (df2["time_seq"] / df2["time_par"]).values

loop_sub = mean3[mean3["impl"] == "loop"].sort_values("threads")
task_sub = mean3[mean3["impl"] == "task"].sort_values("threads")
loop_t1 = loop_sub[loop_sub["threads"] == 1]["t_total_s"].values[0]
task_t1 = task_sub[task_sub["threads"] == 1]["t_total_s"].values[0]

fig, axes = plt.subplots(3, 1, figsize=(8, 7.5))

ax = axes[0]
ax.plot(
    m2_t, m2_time, color="C2", linestyle=":", marker="^", linewidth=1.5,
    label="Mod2-thread",
)
ax.plot(
    loop_sub["threads"], loop_sub["t_total_s"], color="C0", linestyle="-",
    marker="o", linewidth=1.5, label="Mod3-loop",
)
ax.plot(
    task_sub["threads"], task_sub["t_total_s"], color="C1", linestyle="--",
    marker="s", linewidth=1.5, label="Mod3-task",
)
ax.set_xscale("log", base=2)
ax.set_yscale("log")
ax.set_xticks(THREADS_SEL_M3)
ax.set_xticklabels([str(t) for t in THREADS_SEL_M3])
ax.axvline(x=16, color="darkgray", linestyle=":", linewidth=1.0,
           alpha=0.7, zorder=0)
ax.set_xlabel("Threads", fontsize=10)
ax.set_ylabel("Time [s] (log)", fontsize=10)
ax.set_title("(a) Absolute time, NR=10M NS=20M uniform", fontsize=10)
ax.tick_params(labelsize=9)
ax.legend(fontsize=9)

T_SEQ = 0.802

ax = axes[1]
ideal_x = [t for t in THREADS_SEL_M3 if t <= 16]
ax.plot(ideal_x, ideal_x, color="grey", linestyle="--", linewidth=1.0,
        label="Ideal (≤16 cores)", alpha=0.6)
ax.plot(m2_t, T_SEQ / m2_time, color="C2", linestyle=":", marker="^",
        linewidth=1.5, label="Mod2-thread")
ax.plot(loop_sub["threads"], T_SEQ / loop_sub["t_total_s"], color="C0",
        linestyle="-", marker="o", linewidth=1.5, label="Mod3-loop")
ax.plot(task_sub["threads"], T_SEQ / task_sub["t_total_s"], color="C1",
        linestyle="--", marker="s", linewidth=1.5, label="Mod3-task")
ax.set_xscale("log", base=2)
ax.set_xticks(THREADS_SEL_M3)
ax.set_xticklabels([str(t) for t in THREADS_SEL_M3])
ax.axvline(x=16, color="darkgray", linestyle=":", linewidth=1.0,
           alpha=0.7, zorder=0)
ax.set_xlabel("Threads", fontsize=10)
ax.set_ylabel(r"Speedup vs $T_{\rm seq}=0.802$ s", fontsize=10)
ax.set_title("(b) Speedup", fontsize=10)
ax.tick_params(labelsize=9)
ax.set_ylim(0, 22)
ax.legend(fontsize=9, loc="upper left")

ax = axes[2]
ax.axhline(y=1.0, color="grey", linestyle="--", linewidth=1.0,
           label="Ideal", alpha=0.6)
ax.plot(m2_t, (T_SEQ / m2_time) / m2_t, color="C2", linestyle=":",
        marker="^", linewidth=1.5, label="Mod2-thread")
ax.plot(loop_sub["threads"],
        (T_SEQ / loop_sub["t_total_s"]) / loop_sub["threads"],
        color="C0", linestyle="-", marker="o", linewidth=1.5,
        label="Mod3-loop")
ax.plot(task_sub["threads"],
        (T_SEQ / task_sub["t_total_s"]) / task_sub["threads"],
        color="C1", linestyle="--", marker="s", linewidth=1.5,
        label="Mod3-task")
ax.set_xscale("log", base=2)
ax.set_xticks(THREADS_SEL_M3)
ax.set_xticklabels([str(t) for t in THREADS_SEL_M3])
ax.axvline(x=16, color="darkgray", linestyle=":", linewidth=1.0,
           alpha=0.7, zorder=0)
ax.set_xlabel("Threads", fontsize=10)
ax.set_ylabel("Efficiency", fontsize=10)
ax.set_title("(c) Efficiency", fontsize=10)
ax.tick_params(labelsize=9)
ax.set_ylim(0, 1.6)
ax.legend(fontsize=9, loc="upper right")

fig.tight_layout()
os.makedirs(REPORT_DIR, exist_ok=True)
fig.savefig(os.path.join(REPORT_DIR, "fig_m2_vs_m3.pdf"), bbox_inches="tight")
fig.savefig(os.path.join(REPORT_DIR, "fig_m2_vs_m3.png"), dpi=150, bbox_inches="tight")
print("Saved fig_m2_vs_m3.pdf and fig_m2_vs_m3.png")
