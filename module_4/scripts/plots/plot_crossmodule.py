"""Cross-module speedup bars (M2/M3/M4) over the common M4 sequential baseline."""
import csv
import os
import statistics as st
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
CL = os.path.join(HERE, "..", "..", "results", "cluster")
OUT = os.path.join(HERE, "..", "..", "report")

# common sequential baselines (M4 setup), seconds
SEQ_UNIF, SEQ_SKEW = 4.48, 2.30


def med(rows, col):
    return st.median(float(r[col]) for r in rows)


def load(fname):
    with open(os.path.join(CL, fname)) as f:
        return list(csv.DictReader(f))


m2 = load("crossmodule_m2_50m.csv")
m3 = load("crossmodule_m3_50m.csv")
m4 = load("strong_scaling.csv")


def m3_time(wl, t):
    rows = [r for r in m3 if r["workload"] == wl and r["threads"] == str(t)]
    return med(rows, "t_total_s")


def m4_time(impl, wl, nodes):
    rows = [r for r in m4 if r["impl"] == impl and r["workload"] == wl and r["nodes"] == str(nodes)]
    return med(rows, "t_total_s")


# (label, time, is_m4) per workload
unif = [
    ("M2 threads\n(1 node)",   med(m2, "t_total_s"),        False),
    ("M3 OpenMP\n(1 node)",    m3_time("uniform", 32),      False),
    ("M4 MPI\n(1 node)",       m4_time("mpi", "uniform", 1), True),
    ("M4 MPI\n(2 nodes)",      m4_time("mpi", "uniform", 2), True),
    ("M4 hybrid\n(8 nodes)",   m4_time("hybrid", "uniform", 8), True),
]
skew = [
    ("M3 OpenMP\n(1 node, 16t)", m3_time("skewed", 16),       False),
    ("M4 MPI\n(1 node)",         m4_time("mpi", "skewed", 1),  True),
    ("M4 hybrid\n(8 nodes)",     m4_time("hybrid", "skewed", 8), True),
]

fig, (axu, axs) = plt.subplots(1, 2, figsize=(8.2, 3.4))

for ax, data, seq, title in [(axu, unif, SEQ_UNIF, "Uniform"),
                             (axs, skew, SEQ_SKEW, "Skewed")]:
    labels = [d[0] for d in data]
    sp = [seq / d[1] for d in data]
    colors = ["#dd8452" if d[2] else "#4c72b0" for d in data]
    bars = ax.bar(labels, sp, color=colors)
    for b, v in zip(bars, sp):
        ax.text(b.get_x() + b.get_width() / 2, v + 0.15, f"{v:.1f}×",
                ha="center", va="bottom", fontsize=8)
    ax.set_title(title)
    ax.set_ylabel("Speedup vs sequential baseline")
    ax.set_ylim(0, max(sp) * 1.18)
    ax.tick_params(axis="x", labelsize=7.5)
    ax.grid(axis="y", ls=":", alpha=0.5)

# shared legend: shared-memory vs distributed
from matplotlib.patches import Patch
fig.legend(handles=[Patch(color="#4c72b0", label="shared memory (M2/M3)"),
                    Patch(color="#dd8452", label="distributed (M4)")],
           loc="upper center", ncol=2, frameon=False, fontsize=8)
fig.tight_layout(rect=(0, 0, 1, 0.93))
fig.savefig(os.path.join(OUT, "fig_crossmodule.pdf"))
fig.savefig(os.path.join(OUT, "fig_crossmodule.png"), dpi=130)
print("wrote fig_crossmodule.{pdf,png}")
