#!/usr/bin/env python3
"""Esp.5 — NUMA first-touch dei buffer di partizione: parallelo (consegnato),
sequenziale, interleave. Tempo totale e fasi piu' sensibili."""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams.update({"font.size": 11, "axes.titlesize": 12, "figure.facecolor": "white",
                     "axes.facecolor": "white"})
HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "results"); OUT = os.path.join(HERE, "plots"); os.makedirs(OUT, exist_ok=True)

ft = pd.read_csv(os.path.join(RES, "firsttouch.csv"))
ft["scatter"] = ft.scatter_r_ms + ft.scatter_s_ms

POL = [("par", "#2E7D32", "first-touch parallelo (consegnato)"),
       ("seq", "#D32F2F", "first-touch sequenziale"),
       ("interleave", "#EF6C00", "numactl interleave")]

fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
for ax, wl, title in [(axes[0], "uniform", "carico uniforme"),
                      (axes[1], "skew", "carico skewed")]:
    sub = ft[ft.workload == wl]
    ts = sorted(sub.threads.unique())
    x = np.arange(len(ts)); w = 0.26
    for i, (pol, c, lbl) in enumerate(POL):
        g = sub[sub.firsttouch == pol].groupby("threads")["total_ms"]
        ax.bar(x + (i - 1) * w, g.median().reindex(ts).values, width=w, color=c,
               yerr=g.std().reindex(ts).values, capsize=3, label=lbl)
    ax.set_xticks(x); ax.set_xticklabels(ts)
    ax.set_xlabel("numero di thread"); ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo totale (ms)")
axes[0].legend(fontsize=9)
fig.suptitle("Piazzamento NUMA dei buffer di partizione (loop, P=128)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig(os.path.join(OUT, "firsttouch_total.png"), dpi=170)
plt.close(fig)

# fasi a T=16 uniforme: dove va a finire la differenza
fig, ax = plt.subplots(figsize=(8.6, 4.4))
sub = ft[(ft.workload == "uniform") & (ft.threads == 16)]
phases = [("hist_r_ms", "histogram R"), ("scatter", "scatter R+S"), ("join_ms", "join")]
x = np.arange(len(phases)); w = 0.26
for i, (pol, c, lbl) in enumerate(POL):
    s = sub[sub.firsttouch == pol]
    med = [s[ph].median() for ph, _ in phases]
    std = [s[ph].std() for ph, _ in phases]
    ax.bar(x + (i - 1) * w, med, width=w, color=c, yerr=std, capsize=3, label=lbl)
ax.set_xticks(x); ax.set_xticklabels([lbl for _, lbl in phases])
ax.set_ylabel("tempo di fase (ms)")
ax.set_title("Dove finisce la differenza: fasi a T=16, uniforme", fontsize=11.5)
ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True); ax.legend(fontsize=9)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "firsttouch_phases.png"), dpi=170)
print("ok ->", OUT)
