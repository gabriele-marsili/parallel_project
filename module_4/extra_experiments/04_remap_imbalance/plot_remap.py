#!/usr/bin/env python3
"""Esp.4 — imbalance strutturale sotto skew e remapping greedy.
Figura 1: volume ricevuto per rank (max vs media), mod vs greedy, 8 e 128 rank.
Figura 2: tempo totale e fasi, mod vs greedy.
Figura 3: metodologia con e senza barrier davanti alle collettive."""
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

rm = pd.read_csv(os.path.join(RES, "remap.csv"))
rm = rm[rm.workload == "skew"]

fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
for ax, R in [(axes[0], 8), (axes[1], 128)]:
    sub = rm[rm.ranks == R]
    x = np.arange(2); w = 0.36
    maxv = [sub[sub.remap == m]["recv_max"].median() / 1e6 for m in ["mod", "greedy"]]
    meanv = [sub[sub.remap == m]["recv_mean"].median() / 1e6 for m in ["mod", "greedy"]]
    ax.bar(x - w/2, maxv, width=w, color="#D32F2F", label="rank piu carico (max)")
    ax.bar(x + w/2, meanv, width=w, color="#9E9E9E", label="media fra i rank")
    for xi, v in zip(x - w/2, maxv):
        ax.text(xi, v + 0.6, f"{v:.1f}M", ha="center", fontsize=9.5)
    for xi, v in zip(x + w/2, meanv):
        ax.text(xi, v + 0.6, f"{v:.1f}M", ha="center", fontsize=9.5)
    ax.set_xticks(x); ax.set_xticklabels(["dest = pid mod R\n(consegnato)", "greedy sui pesi"])
    ax.set_title(f"{R} rank", fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("record ricevuti per rank (milioni)")
axes[0].legend(fontsize=9.5)
fig.suptitle("Volume ricevuto per rank sotto skew: a 128 rank il pavimento e la partizione\nindivisibile (22.6M record), nessun mapping puo scendere sotto", fontsize=12)
fig.tight_layout(rect=[0, 0, 1, 0.90])
fig.savefig(os.path.join(OUT, "remap_imbalance.png"), dpi=170)
plt.close(fig)

fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
for ax, R in [(axes[0], 8), (axes[1], 128)]:
    sub = rm[rm.ranks == R]
    phases = [("comm_payload_ms", "scambio payload"), ("join_ms", "join"), ("total_ms", "totale")]
    x = np.arange(len(phases)); w = 0.36
    for off, m, c, lbl in [(-w/2, "mod", "#9E9E9E", "pid mod R (consegnato)"),
                           (w/2, "greedy", "#2E7D32", "greedy sui pesi")]:
        med = [sub[sub.remap == m][ph].median() / 1000 for ph, _ in phases]
        std = [sub[sub.remap == m][ph].std() / 1000 for ph, _ in phases]
        ax.bar(x + off, med, width=w, color=c, yerr=std, capsize=3, label=lbl)
    ax.set_xticks(x); ax.set_xticklabels([l for _, l in phases])
    ax.set_title(f"{R} rank", fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
axes[0].set_ylabel("tempo (s)")
axes[0].legend(fontsize=9.5)
fig.suptitle("Effetto del remapping sui tempi (skew): grande a 8 rank, nullo o negativo a 128", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "remap_times.png"), dpi=170)
plt.close(fig)

ba = pd.read_csv(os.path.join(RES, "barrier.csv"))
fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
for ax, wl, title in [(axes[0], "uniform", "carico uniforme"), (axes[1], "skew", "carico skewed")]:
    sub = ba[ba.workload == wl]
    phases = [("comm_sizes_ms", "scambio contatori"), ("comm_payload_ms", "scambio payload"),
              ("total_ms", "totale")]
    x = np.arange(len(phases)); w = 0.36
    for off, b, c, lbl in [(-w/2, 1, "#1565C0", "con barrier (consegnato)"),
                           (w/2, 0, "#EF6C00", "senza barrier")]:
        med = [sub[sub.barrier == b][ph].median() / 1000 for ph, _ in phases]
        std = [sub[sub.barrier == b][ph].std() / 1000 for ph, _ in phases]
        ax.bar(x + off, med, width=w, color=c, yerr=std, capsize=3, label=lbl)
    ax.set_xticks(x); ax.set_xticklabels([l for _, l in phases], fontsize=9)
    ax.set_title(title, fontsize=11)
    ax.grid(ls=":", alpha=0.45, axis="y"); ax.set_axisbelow(True)
    ax.set_yscale("log")
axes[0].set_ylabel("tempo (s), scala log")
axes[0].legend(fontsize=9.5)
fig.suptitle("Metodologia di timing: MPI_Barrier davanti alle collettive (128 rank, mod)", fontsize=12.5)
fig.tight_layout(rect=[0, 0, 1, 0.92])
fig.savefig(os.path.join(OUT, "barrier.png"), dpi=170)
print("ok ->", OUT)
