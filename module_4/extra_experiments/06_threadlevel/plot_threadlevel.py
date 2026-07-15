#!/usr/bin/env python3
"""Esp.6 — MPI_THREAD_FUNNELED vs MPI_THREAD_MULTIPLE sulla stessa pipeline ibrida."""
import os
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

tl = pd.read_csv(os.path.join(RES, "threadlevel.csv"))

fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
for ax, wl, title in [(axes[0], "uniform", "carico uniforme"), (axes[1], "skew", "carico skewed")]:
    sub = tl[tl.workload == wl]
    phases = [("comm_payload_ms", "scambio payload"), ("join_ms", "join"), ("total_ms", "totale")]
    x = np.arange(len(phases)); w = 0.36
    for off, lv, c, lbl in [(-w/2, "funneled", "#1565C0", "MPI_THREAD_FUNNELED (consegnato)"),
                            (w/2, "multiple", "#EF6C00", "MPI_THREAD_MULTIPLE")]:
        med = [sub[sub.threadlevel == lv][ph].median() / 1000 for ph, _ in phases]
        std = [sub[sub.threadlevel == lv][ph].std() / 1000 for ph, _ in phases]
        ax.bar(x + off, med, width=w, color=c, yerr=std, capsize=3, label=lbl)
    ax.set_xticks(x); ax.set_xticklabels([l for _, l in phases], fontsize=9.5)
    ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo (s)")
axes[0].legend(fontsize=9)
fig.suptitle("Livello di thread support MPI: differenza dentro il rumore\n(ibrido, 4 nodi, 4 rank x 32 thread, collettive fuori dalle regioni OpenMP)", fontsize=12)
fig.tight_layout(rect=[0, 0, 1, 0.89])
fig.savefig(os.path.join(OUT, "threadlevel.png"), dpi=170)
print("ok ->", OUT)
