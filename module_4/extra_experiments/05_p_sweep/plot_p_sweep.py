#!/usr/bin/env python3
"""Esp.5 — sensibilità al numero di partizioni P (il report usa P=256)."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

ph = pd.read_csv(os.path.join(RES, "p_sweep_hybrid.csv"))
pu = pd.read_csv(os.path.join(RES, "p_sweep_pure.csv"))

fig, axes = plt.subplots(1, 2, figsize=(11, 4.6), sharex=True)
for ax, df, title in [(axes[0], ph, "ibrido (4 rank x 32 thread)"),
                      (axes[1], pu, "pure MPI (128 rank)")]:
    ps = sorted(df.P.unique())
    for wl, c, mk, lbl in [("uniform", "#1565C0", "s", "uniforme"),
                           ("skew", "#D32F2F", "o", "skewed")]:
        g = df[df.workload == wl].groupby("P")["total_ms"]
        ax.errorbar(ps, g.median().reindex(ps).values / 1000,
                    yerr=g.std().reindex(ps).values / 1000,
                    marker=mk, color=c, lw=1.8, markersize=6, capsize=3, label=lbl)
    ax.axvline(256, color="#2E7D32", ls="--", lw=1.4)
    ax.text(262, ax.get_ylim()[1] * 0.05, "P del report", color="#2E7D32", fontsize=9, rotation=90)
    ax.set_xscale("log", base=2); ax.set_xticks(ps); ax.set_xticklabels(ps)
    ax.set_xlabel("numero di partizioni P")
    ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.set_ylim(bottom=0)
axes[0].set_ylabel("tempo totale (s)"); axes[0].legend(fontsize=9.5)
fig.suptitle("Sensibilita al numero di partizioni (4 nodi)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "p_sweep.png"), dpi=170)
print("ok ->", OUT)
