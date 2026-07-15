#!/usr/bin/env python3
"""Esp.3 — il continuum fra pure MPI e hybrid a 4 nodi.
Figura 1: tempo totale vs rank per nodo (uniforme e skew).
Figura 2: comm_payload e join vs rank per nodo."""
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

df = pd.read_csv(os.path.join(RES, "rankspernode.csv"))
df["rpn"] = df.ranks // 4

fig, ax = plt.subplots(figsize=(8.8, 4.8))
for wl, c, mk, lbl in [("uniform", "#1565C0", "s", "carico uniforme"),
                       ("skew", "#D32F2F", "o", "carico skewed")]:
    sub = df[df.workload == wl]
    rpns = sorted(sub.rpn.unique())
    g = sub.groupby("rpn")["wall_s"]
    ax.errorbar(rpns, g.median().reindex(rpns).values, yerr=g.std().reindex(rpns).values,
                marker=mk, color=c, lw=2, markersize=7, capsize=3, label=lbl)
ax.set_xscale("log", base=2)
rpns = sorted(df.rpn.unique())
ax.set_xticks(rpns); ax.set_xticklabels([f"{r}\n({32//r} thr)" for r in rpns])
ax.set_xlabel("rank per nodo (thread OpenMP per rank)")
ax.set_ylabel("tempo totale (s)")
ax.annotate("ibrido del report", xy=(1, df[(df.rpn == 1) & (df.workload == "uniform")].wall_s.median()),
            xytext=(1.15, 0.32), fontsize=9, color="#555555",
            arrowprops=dict(arrowstyle="->", color="#555555"))
ax.annotate("pure MPI del report", xy=(32, df[(df.rpn == 32) & (df.workload == "uniform")].wall_s.median()),
            xytext=(10, 0.95), fontsize=9, color="#555555",
            arrowprops=dict(arrowstyle="->", color="#555555"))
ax.set_title("Il continuum fra i due estremi del report (4 nodi, 128 core totali)", fontsize=11.5)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=10)
ax.set_ylim(bottom=0)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "rankspernode_total.png"), dpi=170)
plt.close(fig)

fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
for ax, col, title in [(axes[0], "comm_payload_ms", "scambio del payload (Alltoallv)"),
                       (axes[1], "join_ms", "join locale (rank piu lento)")]:
    for wl, c, mk, lbl in [("uniform", "#1565C0", "s", "uniforme"),
                           ("skew", "#D32F2F", "o", "skewed")]:
        sub = df[df.workload == wl]
        rpns = sorted(sub.rpn.unique())
        g = sub.groupby("rpn")[col]
        ax.errorbar(rpns, g.median().reindex(rpns).values / 1000,
                    yerr=g.std().reindex(rpns).values / 1000,
                    marker=mk, color=c, lw=1.8, markersize=6, capsize=3, label=lbl)
    ax.set_xscale("log", base=2); ax.set_xticks(rpns); ax.set_xticklabels(rpns)
    ax.set_xlabel("rank per nodo"); ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.set_ylim(bottom=0)
axes[0].set_ylabel("tempo (s)"); axes[0].legend(fontsize=9.5)
fig.suptitle("Le due componenti lungo il continuum", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "rankspernode_phases.png"), dpi=170)
print("ok ->", OUT)
