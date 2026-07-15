#!/usr/bin/env python3
"""Esp.1 — anatomia di MPI_Alltoallv con buffer sintetici.
Figura 1: sweep del rank count (volume globale fisso e volume per rank fisso).
Figura 2: algoritmo forzato via MCA a 128 rank.
Figura 3: sweep del volume a 128 rank."""
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

st = pd.read_csv(os.path.join(RES, "rank_sweep_strong.csv"))
wk = pd.read_csv(os.path.join(RES, "rank_sweep_weak.csv"))

fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
for ax, df, title, ylab in [
        (axes[0], st, "volume globale fisso (150M record, come lo strong)", "tempo Alltoallv, max fra i rank (s)"),
        (axes[1], wk, "volume per rank fisso (6M record, come il weak)", "")]:
    ranks = sorted(df.ranks.unique())
    med = df.groupby("ranks")["t_max_s"].median().reindex(ranks)
    lo = df.groupby("ranks")["t_max_s"].min().reindex(ranks)
    hi = df.groupby("ranks")["t_max_s"].max().reindex(ranks)
    ax.plot(ranks, med.values, marker="s", color="#1565C0", lw=1.8, markersize=6, label="mediana")
    ax.fill_between(ranks, lo.values, hi.values, color="#1565C0", alpha=0.18, label="min-max su 10 rep")
    ax.set_xscale("log", base=2); ax.set_xticks(ranks); ax.set_xticklabels(ranks)
    ax.set_xlabel("numero di rank (su 8 nodi)")
    ax.set_ylabel(ylab)
    ax.set_title(title, fontsize=10.5)
    ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.set_ylim(bottom=0)
    ax.legend(fontsize=9)
fig.suptitle("Alltoallv sintetico: il costo segue il rank count, non i nodi", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.93])
fig.savefig(os.path.join(OUT, "rank_sweep.png"), dpi=170)
plt.close(fig)

af = pd.read_csv(os.path.join(RES, "algo_forcing.csv"))
fig, axes = plt.subplots(1, 2, figsize=(11, 4.4), sharey=True)
LBL = {"default": ("decisione della libreria", "#9E9E9E"),
       "algo1": ("basic linear forzato", "#D32F2F"),
       "algo2": ("pairwise forzato", "#2E7D32")}
for ax, nodes in [(axes[0], 4), (axes[1], 8)]:
    sub = af[af.nodes == nodes]
    x = np.arange(3)
    labels = ["default", "algo1", "algo2"]
    med = [sub[sub.label == l]["t_max_s"].median() for l in labels]
    lo = [sub[sub.label == l]["t_max_s"].min() for l in labels]
    hi = [sub[sub.label == l]["t_max_s"].max() for l in labels]
    yerr = [np.array(med) - np.array(lo), np.array(hi) - np.array(med)]
    ax.bar(x, med, color=[LBL[l][1] for l in labels], yerr=yerr, capsize=4)
    for xi, v in zip(x, med):
        ax.text(xi, v + 0.02, f"{v:.2f}", ha="center", fontsize=9.5)
    ax.set_xticks(x); ax.set_xticklabels([LBL[l][0] for l in labels], fontsize=9)
    ax.set_title(f"128 rank su {nodes} nodi", fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo Alltoallv (s), mediana con min-max")
fig.suptitle("Algoritmo della collettiva forzato via MCA (stesso volume del caso strong)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "algo_forcing.png"), dpi=170)
plt.close(fig)

vs = pd.read_csv(os.path.join(RES, "volume_sweep.csv"))
fig, ax = plt.subplots(figsize=(8.6, 4.5))
mb = vs.recs_per_rank * 8 / 1e6
ranks = sorted(vs.recs_per_rank.unique())
med = vs.groupby("recs_per_rank")["t_max_s"].median()
lo = vs.groupby("recs_per_rank")["t_max_s"].min()
hi = vs.groupby("recs_per_rank")["t_max_s"].max()
xs = [r * 8 / 1e6 for r in ranks]
ax.plot(xs, med.values, marker="s", color="#1565C0", lw=1.8, markersize=6, label="mediana")
ax.fill_between(xs, lo.values, hi.values, color="#1565C0", alpha=0.18, label="min-max su 10 rep")
ax.set_xscale("log", base=2)
ax.set_xlabel("volume inviato per rank (MB)")
ax.set_ylabel("tempo Alltoallv, max fra i rank (s)")
ax.set_title("Sweep del volume a 128 rank su 4 nodi (algoritmo default):\nvarianza enorme sui messaggi piccoli, costo lineare su quelli grandi", fontsize=11)
ax.grid(ls=":", alpha=0.45); ax.set_axisbelow(True); ax.legend(fontsize=9.5)
ax.set_ylim(bottom=0)
fig.tight_layout()
fig.savefig(os.path.join(OUT, "volume_sweep.png"), dpi=170)
print("ok ->", OUT)
